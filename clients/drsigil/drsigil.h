#ifndef DRSIGIL_H
#define DRSIGIL_H

#include "dr_api.h"
#include "Frontends/DbiIpcCommon.h"

///////////////////////////////////////////////////////
// Thread Data
///////////////////////////////////////////////////////


/* The internal event buffer for this thread.
 * This buffer is eventually flushed to shared memory */
#define DR_PER_THREAD_BUFFER_EVENTS 10000
#define DR_PER_THREAD_POOL_BYTES 10000
typedef struct _per_thread_buffer_t per_thread_buffer_t;
struct _per_thread_buffer_t
{
    BufferedSglEv  *events_ptr;
    BufferedSglEv  *events_base;
    ptr_int_t      events_end;
    char* pool_ptr;
    char* pool_base;
    char* pool_end;
};

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

    /* Instrumentation is enabled/disabled for this thread.
     * This typically depends on specific a given function has been reached */
    bool active;

    /* stores the events */
    per_thread_buffer_t buffer;
};

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
volatile extern bool roi;

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
void instrument_comp(void *drcontext, instrlist_t *ilist, instr_t *where, CompCostType type);

///////////////////////////////////////////////////////
// Sigil2 Interprocess Communication
///////////////////////////////////////////////////////

void init_IPC(int idx, const char *path);
void terminate_IPC(int idx);

/* flush events to IPC */
void flush(per_thread_t *tcxt);

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
    const char *ipc_dir;
    const char *start_func;
    const char *stop_func;
} clo;
void parse(int argc, char *argv[], command_line_options *clo);

#endif
