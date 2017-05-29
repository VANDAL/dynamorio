#include "drsigil.h"
#include <string.h>
#include <time.h>
#include <limits.h>
#include <unistd.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <sys/types.h>

#define STRINGIFY(x) #x

#define MAX_IPC_CHANNELS 256 //fudge number
ipc_channel_t IPC[MAX_IPC_CHANNELS];
/* Initialize all possible IPC channels (some will not be used) */

#ifdef SGLDEBUG
#define SGL_DEBUG(...) dr_printf(__VA_ARGS__)
#else
#define SGL_DEBUG(...)
#endif


static inline void
notify_full_buffer(ipc_channel_t *channel)
{
    /* Tell Sigil2 that the active buffer, on the given IPC channel,
     * is full and ready to be consumed */

    if (channel->standalone == false)
        dr_write_file(channel->full_fifo,
                      &channel->shmem_buf_idx, sizeof(channel->shmem_buf_idx));

    /* Flag the shared memory buffer as used (by Sigil2) */
    channel->empty_buf_idx[channel->shmem_buf_idx] = false;
}


static inline EventBuffer*
get_next_buffer(ipc_channel_t *channel)
{
    /* Increment to the next buffer and try to acquire it for writing */

    /* Circular buffer, must be power of 2 */
    channel->shmem_buf_idx = (channel->shmem_buf_idx+1) & (SIGIL2_IPC_BUFFERS-1);

    /* Sigil2 tells us when it's finished with a shared memory buffer */
    if(channel->empty_buf_idx[channel->shmem_buf_idx] == false)
    {
        if (channel->standalone == false)
            dr_read_file(channel->empty_fifo,
                         &channel->shmem_buf_idx, sizeof(channel->shmem_buf_idx));
        channel->empty_buf_idx[channel->shmem_buf_idx] = true;
    }

    channel->shared_mem->eventBuffers[channel->shmem_buf_idx].used = 0;
    channel->shared_mem->nameBuffers[channel->shmem_buf_idx].used = 0;
    return channel->shared_mem->eventBuffers + channel->shmem_buf_idx;
}


static inline EventBuffer*
get_buffer(ipc_channel_t *channel, uint required)
{
    /* Check if enough space is available in the current buffer */
    EventBuffer *current_shmem_buffer = channel->shared_mem->eventBuffers +
                                        channel->shmem_buf_idx;
    uint available = SIGIL2_EVENTS_BUFFER_SIZE - current_shmem_buffer->used;

    if(available < required)
    {
        /* First inform Sigil2 the current buffer can be read */
        notify_full_buffer(channel);

        /* Then get a new buffer for writing */
        current_shmem_buffer = get_next_buffer(channel);
    }

    return current_shmem_buffer;
}


static inline void
ordered_lock(ipc_channel_t *channel, uint tid)
{
    dr_mutex_lock(channel->queue_lock);

    ticket_queue_t *q = &channel->ticket_queue;
    if (q->locked)
    {
        DR_ASSERT(q->head != NULL && q->tail != NULL);

        ticket_node_t *node = dr_global_alloc(sizeof(ticket_node_t));
        if (node == NULL)
            DR_ABORT_MSG("Failed to allocate ticket node\n");
        node->next = NULL;
        node->dr_event = dr_event_create();
        node->waiting = true;
        node->thread_id = tid;

        DR_ASSERT(q->tail->next == NULL);
        q->tail = q->tail->next = node;

        SGL_DEBUG("Sleeping Thread :%d\n", tid);
        dr_mutex_unlock(channel->queue_lock);

        /* MDL20170425 TODO(soonish)
         * how likely is it that we'll miss a wakeup here? */
        while (node->waiting)
            dr_event_wait(node->dr_event);

        /* clean up */
        dr_event_destroy(node->dr_event);

        SGL_DEBUG("Awakened Thread :%d\n", tid);
    }
    else
    {
        ticket_node_t *node = dr_global_alloc(sizeof(ticket_node_t));
        if (node == NULL)
            DR_ABORT_MSG("Failed to allocate ticket node\n");
        node->next = NULL;
        node->dr_event = NULL;
        node->waiting = false;
        node->thread_id = tid;

        DR_ASSERT(q->head == NULL && q->tail == NULL);
        q->head = q->tail = node;
        q->locked = true;

        dr_mutex_unlock(channel->queue_lock);
    }
}


static inline void
ordered_unlock(ipc_channel_t *channel)
{
    dr_mutex_lock(channel->queue_lock);

    ticket_queue_t *q = &channel->ticket_queue;
    DR_ASSERT(q->locked);
    DR_ASSERT(q->head != NULL);

    /* Calling thread MUST be the owner of the
     * head of the queue.
     *
     * Pop the head of the queue and signal
     * the new head. If there are no waiting
     * threads, then just set the queue to unlocked */

    if (q->head == q->tail)
    {
        dr_global_free(q->head, sizeof(ticket_node_t));
        q->head = NULL;
        q->tail = NULL;
        q->locked = false;
    }
    else
    {
        /* remove self from queue */
        ticket_node_t *this_head = q->head;
        q->head = this_head->next;
        dr_global_free(this_head, sizeof(ticket_node_t));

        /* wake up next waiting thread */
        SGL_DEBUG("Waking   Thread :%d\n", q->head->thread_id);
        q->head->waiting = false;
        dr_event_signal(q->head->dr_event);
    }

    dr_mutex_unlock(channel->queue_lock);
}


static inline ipc_channel_t*
get_locked_channel(per_thread_t *tcxt)
{
    /* Calculate the channel index.
     * Each native dynamoRIO thread will write to a runtime-determined
     * shared memory buffer, in order to reduce the amount of contention
     * for sending data to Sigil2. However, more buffers from dynamoRIO
     * means more load (threads) on Sigil2. Therefore, the total number
     * of buffers from DynamoRIO -> Sigil2 is a command line variable. */
    uint channel_idx = tcxt->thread_id % clo.frontend_threads;
    ipc_channel_t *channel = &IPC[channel_idx];

    /* Requeue self to lock channel,
     * to avoid starvation of other threads */
    if(tcxt->has_channel_lock)
        ordered_unlock(channel);

    /* Lock the shared memory channel */
    ordered_lock(channel, tcxt->thread_id);
    tcxt->has_channel_lock = true;

    return channel;
}


static inline void
set_shared_memory_buffer_helper(per_thread_t *tcxt, ipc_channel_t *channel)
{
    EventBuffer *current_shmem_buffer = get_buffer(channel, MIN_DR_PER_THREAD_BUFFER_EVENTS);
    SglEvVariant *current_event = current_shmem_buffer->events + current_shmem_buffer->used;
    SGLEV_PTR(tcxt->seg_base) = current_event;
    SGLEND_PTR(tcxt->seg_base) = current_event + MIN_DR_PER_THREAD_BUFFER_EVENTS;
    SGLUSED_PTR(tcxt->seg_base) = &(current_shmem_buffer->used);
}


/////////////////////////////////////////////////////////////////////
// IPC interface
/////////////////////////////////////////////////////////////////////
void set_shared_memory_buffer(per_thread_t *tcxt)
{
    ipc_channel_t *channel = get_locked_channel(tcxt);
    set_shared_memory_buffer_helper(tcxt, channel);

    if(channel->last_active_tid != tcxt->thread_id)
    {
        /* Write thread swap event */
        SglSyncEv ev = {
            .type    = SGLPRIM_SYNC_SWAP,
            .data[0] = tcxt->thread_id
        };
        SglEvVariant *slot = SGLEV_PTR(tcxt->seg_base);
        slot->tag = SGL_SYNC_TAG;
        slot->sync = ev;
        ++(SGLEV_PTR(tcxt->seg_base));
        ++*(SGLUSED_PTR(tcxt->seg_base));
    }

    channel->last_active_tid = tcxt->thread_id;
}

void force_thread_flush(per_thread_t *tcxt)
{
    if(tcxt->has_channel_lock)
    {
        uint channel_idx = tcxt->thread_id % clo.frontend_threads;
        ipc_channel_t *channel = &IPC[channel_idx];
        notify_full_buffer(channel);
        get_next_buffer(channel);
        ordered_unlock(channel);
        tcxt->has_channel_lock = false;
        SGLEV_PTR(tcxt->seg_base) = NULL;
        SGLEND_PTR(tcxt->seg_base) = NULL;
        SGLUSED_PTR(tcxt->seg_base) = NULL;
    }
}

static file_t
open_sigil2_fifo(const char *path, int flags)
{
    /* Wait for Sigil2 to create pipes
     * Timeout is empirical */
    uint max_tests = 10;
    for(uint i=0; i<max_tests+1; ++i)
    {
        if(dr_file_exists(path))
            break;

        if(i == max_tests)
        {
            dr_printf("%s\n", path);
            DR_ASSERT_MSG(false, "DrSigil timed out waiting for sigil2 fifos");
        }

        struct timespec ts;
        ts.tv_sec  = 0;
        ts.tv_nsec = 200000000L;
        nanosleep(&ts, NULL);
    }

    file_t f = dr_open_file(path, flags);
    if(f == INVALID_FILE)
        DR_ABORT_MSG("error opening empty fifo");

    return f;
}

void
init_IPC(int idx, const char *path, bool standalone)
{
    DR_ASSERT(idx < MAX_IPC_CHANNELS);

    int path_len, pad_len, shmem_len, fullfifo_len, emptyfifo_len;
    ipc_channel_t *channel = &IPC[idx];

    channel->standalone = standalone;

    /* Initialize channel state */
    channel->queue_lock        = dr_mutex_create();
    channel->ticket_queue.head = NULL;
    channel->ticket_queue.tail = NULL;
    channel->ticket_queue.locked = false;
    channel->shared_mem    = NULL;
    channel->full_fifo     = -1;
    channel->empty_fifo    = -1;
    channel->shmem_buf_idx = 0;

    for(uint i=0; i<sizeof(channel->empty_buf_idx)/sizeof(channel->empty_buf_idx[0]); ++i)
        channel->empty_buf_idx[i] = true;

    channel->last_active_tid = 0;
    channel->initialized     = false;

    if (standalone)
    {
        /* mimic shared memory writes */
        channel->shared_mem = dr_raw_mem_alloc(sizeof(Sigil2DBISharedData),
                                               DR_MEMPROT_READ | DR_MEMPROT_WRITE,
                                               NULL);
        if (channel->shared_mem == NULL)
            DR_ABORT_MSG("Failed to allocate pseudo shared memory buffer\n");
        for (int i=0; i<SIGIL2_IPC_BUFFERS; ++i)
            channel->shared_mem->eventBuffers[i].used = 0;
    }
    else
    {
        /* Connect to Sigil2 */
        path_len = strlen(path);
        pad_len = 4; /* extra space for '/', 2x'-', '\0' */
        shmem_len     = (path_len + pad_len +
                         sizeof(SIGIL2_IPC_SHMEM_BASENAME) +
                         sizeof(STRINGIFY(MAX_IPC_CHANNELS)));
        fullfifo_len  = (path_len + pad_len +
                         sizeof(SIGIL2_IPC_FULLFIFO_BASENAME) +
                         sizeof(STRINGIFY(MAX_IPC_CHANNELS)));
        emptyfifo_len = (path_len + pad_len +
                         sizeof(SIGIL2_IPC_EMPTYFIFO_BASENAME) +
                         sizeof(STRINGIFY(MAX_IPC_CHANNELS)));

        /* set up names of IPC files */
        char shmem_name[shmem_len];
        sprintf(shmem_name, "%s/%s-%d", path, SIGIL2_IPC_SHMEM_BASENAME, idx);

        char fullfifo_name[fullfifo_len];
        sprintf(fullfifo_name, "%s/%s-%d", path, SIGIL2_IPC_FULLFIFO_BASENAME, idx);

        char emptyfifo_name[emptyfifo_len];
        sprintf(emptyfifo_name, "%s/%s-%d", path, SIGIL2_IPC_EMPTYFIFO_BASENAME, idx);


        /* initialize read/write pipes */
        channel->empty_fifo = open_sigil2_fifo(emptyfifo_name, DR_FILE_READ);
        channel->full_fifo = open_sigil2_fifo(fullfifo_name, DR_FILE_WRITE_ONLY);

        /* no need to timeout on file because shared memory MUST be initialized
         * by Sigil2 before the fifos are created */
        file_t map_file = dr_open_file(shmem_name, DR_FILE_READ|DR_FILE_WRITE_APPEND);
        if(map_file == INVALID_FILE)
            DR_ABORT_MSG("error opening shared memory file");

        size_t mapped_size = sizeof(Sigil2DBISharedData);
        channel->shared_mem = dr_map_file(map_file, &mapped_size,
                                          0, 0, /* assume this is not honored */
                                          DR_MEMPROT_READ|DR_MEMPROT_WRITE, 0);

        if(mapped_size != sizeof(Sigil2DBISharedData) || channel->shared_mem == NULL)
            DR_ABORT_MSG("error mapping shared memory");

        dr_close_file(map_file);
    }

    channel->initialized = true;
}


void
terminate_IPC(int idx)
{
    ipc_channel_t *channel = &IPC[idx];
    DR_ASSERT(channel->ticket_queue.head == NULL &&
              channel->ticket_queue.tail == NULL);

    if (channel->standalone)
    {
        dr_raw_mem_free(channel->shared_mem, sizeof(Sigil2DBISharedData));
    }
    else
    {
        /* send terminate sequence */
        uint finished = SIGIL2_IPC_FINISHED;
        uint last_buffer = channel->shmem_buf_idx;
        if(dr_write_file(channel->full_fifo, &last_buffer, sizeof(last_buffer)) != sizeof(last_buffer) ||
           dr_write_file(channel->full_fifo, &finished,    sizeof(finished))    != sizeof(finished))
            DR_ABORT_MSG("error writing finish sequence sigil2 fifos");

        /* wait for sigil2 to disconnect */
        while(dr_read_file(channel->empty_fifo, &finished, sizeof(finished)) > 0);

        dr_close_file(channel->empty_fifo);
        dr_close_file(channel->full_fifo);
        dr_unmap_file(channel->shared_mem, sizeof(Sigil2DBISharedData));
    }

    dr_mutex_destroy(channel->queue_lock);
}
