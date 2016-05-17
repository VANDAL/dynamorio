#ifndef DRSIGIL_H
#define DRSIGIL_H

#include "dr_api.h"

#include "DrSigil/DrSigilIPC.h"

/* internal buffer events to be flushed to shared memory */
#define BUFFER_SIZE 1024

///////////////////////////////////////////////////////
// Thread Data
///////////////////////////////////////////////////////

/* per-application-thread data
 *
 * This data tracks Sigil2 events for a given thread.
 * The events are buffered from buf_base to buf_end,
 * and flushed when either the buffer is full, or the thread exits.
 *
 * Synchronization events, i.e. thread library calls like pthread_create
 * should only be tracked at a high level. The memory and compute events
 * within each library call should not be tracked */
typedef struct _per_thread_t per_thread_t;
struct _per_thread_t
{
    /* unique id */
    uint thread_id;

    /* instrumentation is on/off for this thread */
    bool active;

    /* The internal event buffer for this thread.
     * This buffer is eventually flushed to shared memory */
    BufferedSglEv *buf_ptr;
    BufferedSglEv *buf_base;
    /* buf_end holds the negative value of real address of buffer end. */
    ptr_int_t buf_end;
};

/* thread-local storage for per_thread_t */
extern int tls_idx;

///////////////////////////////////////////////////////
// Sigil2 Event Instrumentation
///////////////////////////////////////////////////////

/* used to directly jump to code cache in instruction instrumentation,
 * instead of making a context switch to a dynamorio clean call for every
 * instruction */
extern app_pc code_cache;

void instrument_mem(void *drcontext, instrlist_t *ilist, instr_t *where, int pos, MemType type);
void instrument_instr(void *drcontext, instrlist_t *ilist, instr_t *where);
void instrument_flop(void *drcontext, instrlist_t *ilist, instr_t *where);
void instrument_iop(void *drcontext, instrlist_t *ilist, instr_t *where);

///////////////////////////////////////////////////////
// Sigil2 Interprocess Communication
///////////////////////////////////////////////////////

void init_IPC(int idx, const char *path);
void terminate_IPC(int idx);

/* flush data to IPC; IPC is flushed if full */
void flush(int idx, per_thread_t *data);

/* force data to flush to Sigil2 immediately,
 * regardless if IPC shmem is full */
void force_flush(int idx, per_thread_t *data);

///////////////////////////////////////////////////////
// Misc Utilities
///////////////////////////////////////////////////////

/* fatal */
void dr_abort_w_msg(const char *msg);

/* option parsing */
typedef struct _command_line_options command_line_options;
struct _command_line_options
{
    int frontend_threads;
    const char *tmp_dir;
} clo;
void parse(int argc, char *argv[], command_line_options *clo);

#endif
