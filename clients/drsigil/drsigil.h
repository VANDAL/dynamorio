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

/* region-of-interest (ROI)
 *
 * If data should be collected or not, depending on command line arguments.
 * If no relevant args are supplied, then the ROI is assumed to be
 * the entirety of the application.
 *
 * Assumes the ROI is correctly implemented,
 * and gets turned on/off in the serial portion of the application */
extern bool roi;

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

/* flush data to IPC; IPC is flushed if full */
void flush(int idx, per_thread_t *data, bool force);

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
