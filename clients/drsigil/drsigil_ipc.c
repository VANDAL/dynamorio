#include "drsigil.h"
#include <string.h>
#include <time.h>
#include <limits.h>
#include <unistd.h>
#include <linux/futex.h>
#include <sys/syscall.h>
#include <sys/types.h>

#define STRINGIFY(x) #x

/* Initialize all possible IPC channels (some will not be used) */
#define MAX_IPC_CHANNELS 256 //fudge number
ipc_channel_t IPC[MAX_IPC_CHANNELS];


/* Tell Sigil2 that the active buffer, on the given IPC channel,
 * is full and ready to be consumed */
static inline void
notify_full_buffer(ipc_channel_t *channel)
{
    dr_write_file(channel->full_fifo,
                  &channel->shmem_buf_idx, sizeof(channel->shmem_buf_idx));

    /* Flag the shared memory buffer as used (by Sigil2) */
    channel->empty_buf_idx[channel->shmem_buf_idx] = false;
}


/* Increment to the next buffer and try to acquire it for writing */
static inline EventBuffer*
get_next_buffer(ipc_channel_t *channel)
{
    /* Circular buffer, must be power of 2 */
    channel->shmem_buf_idx = (channel->shmem_buf_idx+1) & (SIGIL2_DBI_BUFFERS-1);

    /* Sigil2 tells us when it's finished with a shared memory buffer */
    if(channel->empty_buf_idx[channel->shmem_buf_idx] == false)
    {
        dr_read_file(channel->empty_fifo,
                     &channel->shmem_buf_idx, sizeof(channel->shmem_buf_idx));
        channel->empty_buf_idx[channel->shmem_buf_idx] = true;
    }

    channel->shared_mem->buf[channel->shmem_buf_idx].events_used = 0;
    channel->shared_mem->buf[channel->shmem_buf_idx].pool_used   = 0;
    return channel->shared_mem->buf + channel->shmem_buf_idx;
}


static inline EventBuffer*
get_buffer(ipc_channel_t *channel, uint required)
{
    /* check if enough space is available in the current buffer */
    EventBuffer *current_shmem_buffer = channel->shared_mem->buf + channel->shmem_buf_idx;
    uint available = SIGIL2_MAX_EVENTS - current_shmem_buffer->events_used;

    if(available < required)
    {
        notify_full_buffer(channel); // First inform Sigil2 the current buffer can be read
        current_shmem_buffer = get_next_buffer(channel); // Then get a new buffer for writing
    }

    return current_shmem_buffer;
}


static inline int
futex(int *uaddr, int futex_op, int val,
	  const struct timespec *timeout, int *uaddr2, int val3)
{
	return syscall(SYS_futex, uaddr, futex_op, val,
				   timeout, uaddr2, val3);
}

static inline void
ordered_lock(ipc_channel_t *channel)
{
    int seq = channel->ord.seq;
    uint turn = __sync_fetch_and_add(&channel->ord.counter, 1);
    while(turn != channel->ord.next)
    {
		/* TODO Timeout in case the next-in-line arrived AFTER
		 * the unlock. Would that even cause a problem? */
		futex(&channel->ord.seq, FUTEX_WAIT_PRIVATE, seq, NULL, NULL, 0);
        seq = channel->ord.seq;
    }
}

static inline void
ordered_unlock(volatile ipc_channel_t *channel)
{
	++channel->ord.next;
	++channel->ord.seq;
	futex((int*)&channel->ord.seq, FUTEX_WAKE_PRIVATE, INT_MAX, NULL, NULL, 0);
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

    /* requeue self to lock channel */
    if(tcxt->has_channel_lock)
    {
        ordered_unlock(channel);
    }

    /* Lock the shared memory channel */
    ordered_lock(channel);
    tcxt->has_channel_lock = true;

    return channel;
}

static inline BufferedSglEv*
set_shared_memory_buffer_helper(per_thread_t *tcxt, ipc_channel_t *channel)
{

    EventBuffer *current_shmem_buffer = channel->shared_mem->buf + channel->shmem_buf_idx;
    size_t available = SIGIL2_MAX_EVENTS - current_shmem_buffer->events_used;

    if (available < MIN_DR_PER_THREAD_BUFFER_EVENTS)
    {
        notify_full_buffer(channel); // First inform Sigil2 the current buffer can be read
        current_shmem_buffer = get_next_buffer(channel); // Then get a new buffer for writing
        available = SIGIL2_MAX_EVENTS - current_shmem_buffer->events_used;
    }

    size_t events = (available >= DR_PER_THREAD_BUFFER_EVENTS) ? 
        DR_PER_THREAD_BUFFER_EVENTS : available;

    tcxt->buffer.events_ptr = current_shmem_buffer->events + current_shmem_buffer->events_used;
    tcxt->buffer.events_end = tcxt->buffer.events_ptr + events;
    tcxt->buffer.events_used = &current_shmem_buffer->events_used;
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
            .type = SGLPRIM_SYNC_SWAP,
            .id   = tcxt->thread_id
        };
        tcxt->buffer.events_ptr->tag  = SGL_SYNC_TAG;
        tcxt->buffer.events_ptr->sync = ev;
        ++tcxt->buffer.events_ptr;
        ++*(tcxt->buffer.events_used);
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
        tcxt->buffer.events_ptr = 0;
        tcxt->buffer.events_end = 0;
        tcxt->buffer.events_used = NULL;
    }
}

void
init_IPC(int idx, const char *path)
{
    DR_ASSERT(idx < MAX_IPC_CHANNELS);

    int path_len, pad_len, shmem_len, fullfifo_len, emptyfifo_len;
    ipc_channel_t *channel = &IPC[idx];

    /* Initialize channel state */
    channel->ord.counter   = 0;
    channel->ord.next      = 0;
    channel->ord.seq       = 0;
    channel->shared_mem    = NULL;
    channel->full_fifo     = -1;
    channel->empty_fifo    = -1;
    channel->shmem_buf_idx = 0;

    for(uint i=0; i<sizeof(channel->empty_buf_idx)/sizeof(channel->empty_buf_idx[0]); ++i)
        channel->empty_buf_idx[i] = true;

    channel->last_active_tid = 0;
    channel->initialized     = false;

    /* Connect to Sigil2 */
    path_len = strlen(path);
    pad_len = 4; /* extra space for '/', 2x'-', '\0' */
    shmem_len     = (path_len + pad_len +
                     sizeof(SIGIL2_DBI_SHMEM_NAME) +
                     sizeof(STRINGIFY(MAX_IPC_CHANNELS)));
    fullfifo_len  = (path_len + pad_len +
                     sizeof(SIGIL2_DBI_FULLFIFO_NAME) +
                     sizeof(STRINGIFY(MAX_IPC_CHANNELS)));
    emptyfifo_len = (path_len + pad_len +
                     sizeof(SIGIL2_DBI_EMPTYFIFO_NAME) +
                     sizeof(STRINGIFY(MAX_IPC_CHANNELS)));

    /* set up names of IPC files */
    char shmem_name[shmem_len];
    sprintf(shmem_name, "%s/%s-%d", path, SIGIL2_DBI_SHMEM_NAME, idx);

    char fullfifo_name[fullfifo_len];
    sprintf(fullfifo_name, "%s/%s-%d", path, SIGIL2_DBI_FULLFIFO_NAME, idx);

    char emptyfifo_name[emptyfifo_len];
    sprintf(emptyfifo_name, "%s/%s-%d", path, SIGIL2_DBI_EMPTYFIFO_NAME, idx);

    /* Wait for Sigil2 to create pipes
     * Timeout is arbitrary */
    uint max_tests = 5;
    for(uint i=0; i<max_tests+1; ++i)
    {
        if(dr_file_exists(emptyfifo_name) == true &&
           dr_file_exists(fullfifo_name)  == true &&
           dr_file_exists(shmem_name)     == true)
            break;

        if(i == max_tests)
            dr_printf("%s\n", emptyfifo_name), dr_abort_w_msg("DrSigil timed out waiting for sigil2 fifos");

        struct timespec ts;
        ts.tv_sec  = 0;
        ts.tv_nsec = 200000000L;
        nanosleep(&ts, NULL);
    }

    /* initialize read/write pipes */
    channel->empty_fifo = dr_open_file(emptyfifo_name, DR_FILE_READ);
    if(channel->empty_fifo == INVALID_FILE)
        dr_abort_w_msg("error opening empty fifo");

    channel->full_fifo = dr_open_file(fullfifo_name, DR_FILE_WRITE_ONLY);
    if(channel->full_fifo == INVALID_FILE)
        dr_abort_w_msg("error opening full fifo");

    /* shared memory MUST be initialized by Sigil2 before the fifos are created */
    file_t map_file = dr_open_file(shmem_name, DR_FILE_READ|DR_FILE_WRITE_APPEND);
    if(map_file == INVALID_FILE)
        dr_abort_w_msg("error opening shared memory file");

    size_t mapped_size = sizeof(Sigil2DBISharedData);
    channel->shared_mem = dr_map_file(map_file, &mapped_size,
                                      0, 0, /* assume this is not honored */
                                      DR_MEMPROT_READ|DR_MEMPROT_WRITE, 0);


    if(mapped_size != sizeof(Sigil2DBISharedData) || channel->shared_mem == NULL)
        dr_abort_w_msg("error mapping shared memory");

    dr_close_file(map_file);
    channel->initialized = true;
}


void
terminate_IPC(int idx)
{
    /* send terminate sequence */
    uint finished = SIGIL2_DBI_FINISHED;
    uint last_buffer = IPC[idx].shmem_buf_idx;
    if(dr_write_file(IPC[idx].full_fifo, &last_buffer, sizeof(last_buffer)) != sizeof(last_buffer) ||
       dr_write_file(IPC[idx].full_fifo, &finished,    sizeof(finished))    != sizeof(finished))
        dr_abort_w_msg("error writing finish sequence sigil2 fifos");

    /* wait for sigil2 to disconnect */
    while(dr_read_file(IPC[idx].empty_fifo, &finished, sizeof(finished)) > 0);

    dr_close_file(IPC[idx].empty_fifo);
    dr_close_file(IPC[idx].full_fifo);
    dr_unmap_file(IPC[idx].shared_mem, sizeof(Sigil2DBISharedData));
}
