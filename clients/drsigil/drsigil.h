#ifndef DRSIGIL_H
#define DRSIGIL_H

#include "dr_api.h"
#include "Frontends/DbiIpcCommon.h"

/////////////////////////////////////////////////////////////////////
//                          IPC Management                         //
/////////////////////////////////////////////////////////////////////

typedef struct _ordered_mutex_t ordered_mutex_t;
struct _ordered_mutex_t
{
    /* Manage threads waiting to write to the shared memory
     *
     * Each thread will write directly to shared memory to
     * avoid the memory usage+bandwidth overhead of writing
     * to a local buffer and then copying to shared memory.
     *
     * MDL20170222 To help with order of the threads trying to
     * lock shared memory, we have to use futex syscalls
     * because DynamoRIO does not provide conditional waits/broadcasts yet.
     *
     * XXX The method used for enforcing order is quite hacky and naive.
     * It suffers from the 'thundering herd' problem.
     * The moral of the story: parallel programming is hard >.< */

    uint counter;
    uint next;
    int  seq;
};


typedef struct _ipc_channel_t ipc_channel_t;
struct _ipc_channel_t
{
    /* The shared memory channel between this DynamoRIO client application and
     * Sigil2. Multiple channels can exist to reduce contention on the channels;
     * the number of channels is determined by Sigil2 when DynamoRIO is invoked,
     * via command line. Additionally, the number of channels will match the
     * number of frontend Sigil2 threads, so that each thread will process one
     * buffer. The buffer an application thread writes to depends on its thread
     * id (thread id % number of channels). That is, if there is one channel,
     * then all threads vie over that channel. */

    ordered_mutex_t ord;
    /* Multiple threads can write via this IPC channel.
     * Only allow one at a time. */

    Sigil2DBISharedData *shared_mem;
    /* Produce data to this buffer */

    file_t full_fifo;
    /* Update Sigil2 via this fifo which buffers
     * are full and ready to be consumed */

    file_t empty_fifo;
    /* Sigil2 updates DynamoRIO with the last
     * buffer consumed(empty) via this fifo */

    uint shmem_buf_idx;
    /* The current buffer being filled in shared memory
     * Must wrap around back to 0 at 'SIGIL2_DBI_BUFFERS' */

    bool empty_buf_idx[SIGIL2_DBI_BUFFERS];
    /* Corresponds to each buffer that is available for writing */

    uint last_active_tid;
    /* Required to let Sigil2 know when the TID of the current thread has changed */

    bool initialized;
    /* If this is a valid channel */
};

/////////////////////////////////////////////////////////////////////
//                           Thread Data                           //
/////////////////////////////////////////////////////////////////////

#define DR_PER_THREAD_BUFFER_EVENTS (1UL << 22)
#define MIN_DR_PER_THREAD_BUFFER_EVENTS (1UL << 15)
typedef struct _per_thread_buffer_t per_thread_buffer_t;
struct _per_thread_buffer_t
{
    SglEvVariant  *events_ptr;
    SglEvVariant  *events_end;
    size_t        *events_used;
};

typedef struct _per_thread_t per_thread_t;
struct _per_thread_t
{
    /* per-application-thread data
     *
     * This data tracks Sigil2 events for a given thread.
     * The events are buffered from buf_base to buf_end,
     * and flushed when either the buffer is full, or the thread exits.
     *
     * Synchronization events, i.e. thread library calls like pthread_create
     * should only be tracked at a high level. The memory and compute events
     * within each library call should not be tracked */

    uint thread_id;
    /* Unique ID
     * Sigil2 expects threads to start from '1' */

    bool active;
    /* Instrumentation is enabled/disabled for this thread.
     * This typically depends on specific a given function has been reached */

    bool has_channel_lock;
    per_thread_buffer_t buffer;
};


volatile extern bool roi;
/* Region-Of-Interest (ROI)
 *
 * If data should be collected or not, depending on command line arguments.
 * If no relevant args are supplied, then the ROI is assumed to be the
 * entirety of the application.
 *
 * Assumes the ROI is correctly implemented, and gets turned on/off in the
 * serial portion of the application.
 * XXX There is no per-thread ROI.
 * TODO Make atomic */

extern int tls_idx;
/* thread-local storage for per_thread_t */

/////////////////////////////////////////////////////////////////////
//                           Option Parsing                        //
/////////////////////////////////////////////////////////////////////
typedef struct _command_line_options command_line_options;
struct _command_line_options
{
    int frontend_threads;
    const char *ipc_dir;
    const char *start_func;
    const char *stop_func;
} clo;


/////////////////////////////////////////////////////////////////////
//                         FUNCTION DECLARATIONS                   //
/////////////////////////////////////////////////////////////////////

void instrument_mem(void *drcontext, instrlist_t *ilist, instr_t *where, int pos, MemType type);
void instrument_instr(void *drcontext, instrlist_t *ilist, instr_t *where);
void instrument_comp(void *drcontext, instrlist_t *ilist, instr_t *where, CompCostType type);

void init_IPC(int idx, const char *path);
void terminate_IPC(int idx);
void set_shared_memory_buffer(per_thread_t *tcxt);
void force_thread_flush(per_thread_t *tcxt);

void dr_abort_w_msg(const char *msg);

void parse(int argc, char *argv[], command_line_options *clo);

#endif
