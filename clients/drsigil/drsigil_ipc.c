#include <string.h>
#include "drsigil.h"
#include "DrSigil/DrSigilIPC.h"

#define STRINGIFY(x) #x
#define DRSIGIL_MIN(x, y) (x) > (y) ? (y) : (x)

/*
 * FIXME
 * State machine between Sigil2 and DynamoRIO
 * requires substational clean up.
 *
 * Generally there's a lot of clean up to do here
 */

typedef struct _ipc_channel_t ipc_channel_t;
struct _ipc_channel_t
{
    /* Produce data to this buffer */
    DrSigilSharedData *shared_mem;

    /* Multiple threads can write via
     * this IPC channel. Only allow one
     * at a time. */
    void *shared_mem_lock;

    /* Update Sigil2 via this fifo which buffers
     * are full and ready to be consumed */
    file_t full_fifo;

    /* The current buffer being
     * filled in shared memory */
    uint shmem_buf_idx;

    /* Corresponds to each buffer that is
       empty and ready to be filled */
    bool empty_buf_idx[DRSIGIL_BUFNUM];

    /* The next location to be filled in
     * the current shared memory buffer */
    uint shmem_buf_used;

    /* Sigil2 updates DynamoRIO with the last
     * buffer consumed(empty) via this fifo */
    file_t empty_fifo;
};

/* Initialize all possible IPC channels (some will not be used) */
#define MAX_IPC_CHANNELS 256 //fudge number
ipc_channel_t IPC[MAX_IPC_CHANNELS];


static inline int
read_empty_fifo_available(ipc_channel_t *channel)
{
    int idx = channel->shmem_buf_idx;
    if(channel->empty_buf_idx[channel->shmem_buf_idx] == false)
    {
        /* block until buffer available in shared memory */
        dr_read_file(channel->empty_fifo, &idx, sizeof(idx));

        /* reset empty status */
        channel->empty_buf_idx[idx] = true;
    }
    return idx;
}


static inline void
write_full_fifo_available(ipc_channel_t *channel, int idx, int size)
{
    dr_write_file(channel->full_fifo, &size, sizeof(size));
    dr_write_file(channel->full_fifo, &idx, sizeof(idx));
}


void
flush(int idx, per_thread_t *data, bool force)
{
    /*assumption*/DR_ASSERT(DRSIGIL_BUFSIZE > BUFFER_SIZE);
    ipc_channel_t *channel = &IPC[idx];

    /* lock shared memory */
    dr_mutex_lock(channel->shared_mem_lock);

    /* may have to flush across mutliple shared memory buffers
     * if current buffer does not have enough empty space */
    int begin = 0;
    uint remaining = data->buf_ptr - data->buf_base;

    while(remaining > 0)
    {
        /* get an available shared memory buffer */
        channel->shmem_buf_idx = read_empty_fifo_available(channel);
        DrSigilEvent *shmem_buffer = channel->shared_mem->buf[channel->shmem_buf_idx];

        /* fill shared memory buffer based on buffer space left
         * and amount of local buffer full */
        int end = DRSIGIL_MIN(/*shmem buffer*/DRSIGIL_BUFSIZE - channel->shmem_buf_used,
                              /*local buffer*/remaining + begin); /* XXX This is  essentially
                                                                   * just data->used */

        /* write to shared memory buffer */
        for(int i=channel->shmem_buf_used, j=begin; j<end; ++i, ++j)
        {
            DR_ASSERT(data->thread_id % clo.frontend_threads == idx);
            shmem_buffer[i].thread_id = data->thread_id;
            shmem_buffer[i].ev.tag = data->buf_base[j].tag;
            switch(data->buf_base[j].tag)
            {
            case SGL_MEM_TAG:
                shmem_buffer[i].ev.mem = data->buf_base[j].mem;
                break;
            case SGL_COMP_TAG:
                shmem_buffer[i].ev.comp = data->buf_base[j].comp;
                break;
            case SGL_SYNC_TAG:
                shmem_buffer[i].ev.sync = data->buf_base[j].sync;
                break;
            case SGL_CXT_TAG:
                shmem_buffer[i].ev.cxt = data->buf_base[j].cxt;
                break;
            default:
                break;
            }
        }
        /* update shared memory */
        channel->shmem_buf_used += (end-begin);

        /* tell Sigil2 if buffer was filled and ready to be consumed */
        DR_ASSERT(channel->shmem_buf_used <= DRSIGIL_BUFSIZE);
        if(channel->shmem_buf_used == DRSIGIL_BUFSIZE)
        {
            write_full_fifo_available(channel, channel->shmem_buf_idx, channel->shmem_buf_used);
            channel->empty_buf_idx[channel->shmem_buf_idx] = false;
            channel->shmem_buf_idx++;
            channel->shmem_buf_used = 0;
        }

        /* check if local buffer was completely written */
        remaining -= (end-begin);
        begin = end;
    }

    if(force == true)
    {
        if(channel->shmem_buf_used > 0)
        {
            write_full_fifo_available(channel, channel->shmem_buf_idx, channel->shmem_buf_used);
            channel->empty_buf_idx[channel->shmem_buf_idx] = false;
            channel->shmem_buf_idx++;
            channel->shmem_buf_used = 0;
        }
    }

    /* reset */
    data->buf_ptr = data->buf_base;
    dr_mutex_unlock(channel->shared_mem_lock);
}


void
init_IPC(int idx, const char *path, const char *uid)
{
    DR_ASSERT(idx < MAX_IPC_CHANNELS);

    int path_len, uid_len, pad_len, shmem_len, fullfifo_len, emptyfifo_len;
    ipc_channel_t *channel = &IPC[idx];

    path_len = strlen(path);
    uid_len = strlen(uid);
    pad_len = 4; /* extra space for '/', 2x'-', '\0' */

    shmem_len = (path_len + uid_len + pad_len +
                 sizeof(DRSIGIL_SHMEM_NAME) +
                 sizeof(STRINGIFY(MAX_IPC_CHANNELS)));
    fullfifo_len = (path_len + uid_len + pad_len +
                    sizeof(DRSIGIL_FULLFIFO_NAME) +
                    sizeof(STRINGIFY(MAX_IPC_CHANNELS)));
    emptyfifo_len = (path_len + uid_len + pad_len +
                     sizeof(DRSIGIL_EMPTYFIFO_NAME) +
                     sizeof(STRINGIFY(MAX_IPC_CHANNELS)));

    /* set up names of IPC files */
    char shmem_name[shmem_len];
    sprintf(shmem_name, "%s/%s-%d-%s", path, DRSIGIL_SHMEM_NAME, idx, uid);

    char fullfifo_name[fullfifo_len];
    sprintf(fullfifo_name, "%s/%s-%d-%s", path, DRSIGIL_FULLFIFO_NAME, idx, uid);

    char emptyfifo_name[emptyfifo_len];
    sprintf(emptyfifo_name, "%s/%s-%d-%s", path, DRSIGIL_EMPTYFIFO_NAME, idx, uid);

    /* initialize read/write pipes */
    channel->empty_fifo = dr_open_file(emptyfifo_name, DR_FILE_READ);
    if(channel->empty_fifo == INVALID_FILE)
    {
        dr_abort_w_msg("error opening empty fifo");
    }

    channel->full_fifo = dr_open_file(fullfifo_name, DR_FILE_WRITE_ONLY);
    if(channel->full_fifo == INVALID_FILE)
    {
        dr_abort_w_msg("error opening full fifo");
    }

    /* initialize shared memory */
    file_t map_file = dr_open_file(shmem_name, DR_FILE_READ|DR_FILE_WRITE_APPEND);

    size_t mapped_size = sizeof(DrSigilSharedData);
    channel->shared_mem = dr_map_file(map_file, &mapped_size,
                                      0,
                                      0, /* assume this is not honored */
                                      DR_MEMPROT_READ|DR_MEMPROT_WRITE, 0);


    if(mapped_size != sizeof(DrSigilSharedData) ||
       channel->shared_mem == NULL)
    {
        dr_abort_w_msg("error opening shared memory");
    }

    dr_close_file(map_file);

    /* initialize channel state */
    channel->shmem_buf_used = 0;
    channel->shmem_buf_idx = 0;
    for(uint i=0; i<sizeof(channel->empty_buf_idx)/sizeof(channel->empty_buf_idx[0]); ++i)
    {
        channel->empty_buf_idx[i] = true;
    }
    channel->shared_mem_lock = dr_mutex_create();
}


void
terminate_IPC(int idx)
{
    /* send terminate sequence */
    dr_printf("disconnecting from %d\n", idx);
    uint finished = DRSIGIL_FINISHED;
    if(dr_write_file(IPC[idx].full_fifo, &finished, sizeof(finished)) != sizeof(finished) ||
       dr_write_file(IPC[idx].full_fifo, &IPC[idx].shmem_buf_idx, sizeof(IPC[idx].shmem_buf_idx)) != sizeof(IPC[idx].shmem_buf_idx) ||
       dr_write_file(IPC[idx].full_fifo, &IPC[idx].shmem_buf_used, sizeof(IPC[idx].shmem_buf_used)) != sizeof(IPC[idx].shmem_buf_used))
    {
        dr_abort_w_msg("error writing finish sequence sigil2 fifos");
    }

    /* wait for sigil2 to disconnect */
    while(dr_read_file(IPC[idx].empty_fifo, &finished, sizeof(finished)) > 0);
    dr_printf("disconnected from %d\n", idx);

    dr_close_file(IPC[idx].empty_fifo);
    dr_close_file(IPC[idx].full_fifo);
    dr_unmap_file(IPC[idx].shared_mem, sizeof(DrSigilSharedData));
    dr_mutex_destroy(IPC[idx].shared_mem_lock);
}
