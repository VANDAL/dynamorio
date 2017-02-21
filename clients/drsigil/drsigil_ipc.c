#include "drsigil.h"
#include <string.h>
#include <sys/types.h>
#include <time.h>

/* Convenience macros */
#define STRINGIFY(x) #x
#define DRSIGIL_MIN(x, y) (x) > (y) ? (y) : (x)


/* The shared memory channel between this DynamoRIO client application
 * and Sigil2. Multiple channels can exist to reduce contention on the
 * channels; the number of channels is determined by Sigil2 when DynamoRIO
 * is invoked, via command line. Additionally, the number of channels will
 * match the number of frontend Sigil2 threads, so that each thread will
 * process one buffer. The buffer an application thread writes
 * to depends on its thread id (thread id % number of channels).
 * That is, if there is one channel, then all threads vie over that channel.
 */
typedef struct _ipc_channel_t ipc_channel_t;
struct _ipc_channel_t
{
    /* Produce data to this buffer */
    Sigil2DBISharedData *shared_mem;

    /* Multiple threads can write via this IPC channel.
     * Only allow one at a time. */
    void *shared_mem_lock;

    /* Update Sigil2 via this fifo which buffers
     * are full and ready to be consumed */
    file_t full_fifo;

    /* Sigil2 updates DynamoRIO with the last
     * buffer consumed(empty) via this fifo */
    file_t empty_fifo;

    /* The current buffer being filled in shared memory */
    uint shmem_buf_idx;

    /* Corresponds to each buffer that is
       empty and ready to be filled */
    bool empty_buf_idx[SIGIL2_DBI_BUFFERS];

    /* If this is a valid channel */
    bool initialized;
};

/* Initialize all possible IPC channels (some will not be used) */
#define MAX_IPC_CHANNELS 256 //fudge number
ipc_channel_t IPC[MAX_IPC_CHANNELS];


static inline int
read_empty_fifo_available(file_t empty_fifo)
{
    int idx;
    dr_read_file(empty_fifo, &idx, sizeof(idx));
    return idx;
}


static inline void
write_full_fifo_available(file_t full_fifo, int idx)
{
    dr_write_file(full_fifo, &idx, sizeof(idx));
}


/* Tell Sigil2 that the active buffer, on the given IPC channel,
 * is full and ready to be consumed */
static inline void
notify_full_buffer(ipc_channel_t *channel)
{
    write_full_fifo_available(channel->full_fifo, channel->shmem_buf_idx);

    /* Flag the shared memory buffer as used (by Sigil2) */
    channel->empty_buf_idx[channel->shmem_buf_idx] = false;
}


/* Increment to the next buffer and try to acquire it for writing */
static inline EventBuffer*
get_next_buffer(ipc_channel_t *channel)
{
    /* Sigil2 tells us when it's finished with a shared memory buffer */
    if(channel->empty_buf_idx[++channel->shmem_buf_idx] == false)
    {
        channel->shmem_buf_idx = read_empty_fifo_available(channel->empty_fifo);
        channel->empty_buf_idx[channel->shmem_buf_idx] = true;
    }

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
        return get_next_buffer(channel); // Then get a new buffer for writing
    }
    else
    {
        return current_shmem_buffer;
    }
}


static inline void
flush_n_events(BufferedSglEv *restrict from, BufferedSglEv *restrict to, uint n)
{
    for(uint i=0; i<n; ++i)
        *to++ = *from++;
}


/* Flush this thread's event buffer to Sigil2 buffer
 * TODO profile this function for bottlenecks */
void
flush(per_thread_t *tcxt)
{
    /* Calculate the channel index.
     * Each native dynamoRIO thread will write to a runtime-determined
     * shared memory buffer, in order to reduce the amount of contention
     * for sending data to Sigil2. However, more buffers from dynamoRIO
     * means more load (threads) on Sigil2. Therefore, the total number
     * of buffers from DynamoRIO -> Sigil2 is a command line variable. */
    uint channel_idx = tcxt->thread_id % clo.frontend_threads;

    /* Lock the shared memory channel to prevent
     * other application threads from flushing */
    ipc_channel_t *channel = &IPC[channel_idx];
    dr_mutex_lock(channel->shared_mem_lock);

    /* the event buffer for this thread */
    per_thread_buffer_t *buffer = &tcxt->buffer;
    uint events_to_flush  = buffer->events_ptr - buffer->events_base;

    /* get a shared memory buffer with enough space to write to */
    EventBuffer *shmem_buffer = get_buffer(channel, events_to_flush);

    flush_n_events(buffer->events_base, shmem_buffer->events + shmem_buffer->events_used,
                   events_to_flush);
    shmem_buffer->events_used += events_to_flush;

    /* reset */
    dr_mutex_unlock(channel->shared_mem_lock);
    buffer->events_ptr = buffer->events_base;
}


void
init_IPC(int idx, const char *path)
{
    DR_ASSERT(idx < MAX_IPC_CHANNELS);

    int path_len, pad_len, shmem_len, fullfifo_len, emptyfifo_len;
    ipc_channel_t *channel = &IPC[idx];

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
            dr_abort_w_msg("sigil2 fifos not found");

        struct timespec ts;
        ts.tv_sec  = 0;
        ts.tv_nsec = 100000000L;
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

    /* initialize channel state */
    channel->shmem_buf_idx = 0;
    for(uint i=0; i<sizeof(channel->empty_buf_idx)/sizeof(channel->empty_buf_idx[0]); ++i)
        channel->empty_buf_idx[i] = true;
    channel->shared_mem_lock = dr_mutex_create();
}


void
terminate_IPC(int idx)
{
    /* send terminate sequence */
    dr_printf("disconnecting from %d\n", idx);
    uint finished = SIGIL2_DBI_FINISHED;
    if(dr_write_file(IPC[idx].full_fifo, &finished, sizeof(finished)) != sizeof(finished) ||
       dr_write_file(IPC[idx].full_fifo, &IPC[idx].shmem_buf_idx, sizeof(IPC[idx].shmem_buf_idx)) != sizeof(IPC[idx].shmem_buf_idx))
        dr_abort_w_msg("error writing finish sequence sigil2 fifos");

    /* wait for sigil2 to disconnect */
    while(dr_read_file(IPC[idx].empty_fifo, &finished, sizeof(finished)) > 0);
    dr_printf("disconnected from %d\n", idx);

    dr_close_file(IPC[idx].empty_fifo);
    dr_close_file(IPC[idx].full_fifo);
    dr_unmap_file(IPC[idx].shared_mem, sizeof(Sigil2DBISharedData));
    dr_mutex_destroy(IPC[idx].shared_mem_lock);
}
