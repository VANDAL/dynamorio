#ifndef DRSIGIL_H
#define DRSIGIL_H

#include "dr_api.h"
#include "Frontends/CommonShmemIPC.h"

#define DR_ABORT_MSG(msg) DR_ASSERT_MSG(false, msg)

#define MINSERT instrlist_meta_preinsert

#define RESERVE_REGISTER(reg) \
    if (drreg_reserve_register(drcontext, ilist, where, NULL, &reg) != DRREG_SUCCESS) \
        DR_ASSERT(false);
#define UNRESERVE_REGISTER(reg) \
    if (drreg_unreserve_register(drcontext, ilist, where, reg) != DRREG_SUCCESS) \
        DR_ASSERT(false);



/////////////////////////////////////////////////////////////////////
//                          IPC Management                         //
/////////////////////////////////////////////////////////////////////

typedef struct _ticket_node_t ticket_node_t;
struct _ticket_node_t
{
    void *dr_event;
    ticket_node_t *next;
    uint thread_id;
    volatile bool waiting;
};


typedef struct _ticket_queue_t
{
    /* Manage threads waiting to write to the shared memory
     *
     * Each thread will write directly to shared memory to
     * avoid the memory usage+bandwidth overhead of writing
     * to a local buffer and then copying to shared memory. */

    ticket_node_t *head;
    ticket_node_t *tail;
    volatile bool locked;
} ticket_queue_t;


typedef struct _ipc_channel_t
{
    /* The shared memory channel between this DynamoRIO client application and
     * Sigil2. Multiple channels can exist to reduce contention on the channels;
     * the number of channels is determined by Sigil2 when DynamoRIO is invoked,
     * via command line. Additionally, the number of channels will match the
     * number of frontend Sigil2 threads, so that each thread will process one
     * buffer. The buffer an application thread writes to depends on its thread
     * id (thread id % number of channels). That is, if there is one channel,
     * then all threads vie over that channel. */

    void *queue_lock;
    ticket_queue_t ticket_queue;
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

    bool empty_buf_idx[SIGIL2_IPC_BUFFERS];
    /* Corresponds to each buffer that is available for writing */

    uint last_active_tid;
    /* Required to let Sigil2 know when the TID of the current thread has changed */

    bool initialized;
    /* If this is a valid channel */

    bool standalone;
    /* Will be TRUE if this channel was not initialized with Sigil2 IPC;
     * will 'fake' any IPC. */
} ipc_channel_t;

/////////////////////////////////////////////////////////////////////
//                           Thread Data                           //
/////////////////////////////////////////////////////////////////////

typedef struct _mem_ref_t
{
    MemType type;
    ushort size; // XXX big enough?
    void *addr;
} mem_ref_t;

#define MAX_NUM_MEM_REFS 2048
#define MEM_BUF_SIZE (sizeof(mem_ref_t) * MAX_NUM_MEM_REFS)
/* we should not have more than this many memory references per basic block */

typedef struct _instr_block_t
{
    instr_t *instr;
    uint mem_ref_count;
    uint comp_count;
} instr_block_t;

#define MAX_NUM_COMPS 1024
#define COMP_BUF_SIZE (sizeof(SglCompEv) * MAX_NUM_COMPS)

#define MAX_NUM_IBLOCKS 1024
#define IBLOCK_BUF_SIZE (sizeof(instr_block_t) * MAX_NUM_IBLOCKS)
/* we should not need more than this per basic block */

typedef struct _per_thread_t
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

    bool has_channel_lock;
    /* Is allowed to use the ipc channel */

    bool is_blocked;
    /* Mostly used for debugging.
     * Is about to wait on a application-side lock.
     * We must take care to ensure this thread never
     * has the channel lock while blocked, otherwise
     * we end up with an application-side deadlock */

    mem_ref_t *memrefs;
    /* Track memory references per event block.
     * This is used as an optimization in the instrumentation implementation.
     * A local buffer will hold all mem refs, and only at the end of each
     * branching instruction, all sigil events will be written to shared memory
     * (e.g. the mem refs, instructions, compute, synchronization, ...) */

    SglCompEv *comps;
    SglCompEv *current_iblock_comp;
    /* Keep a local buffer of compute per event block.
     * This just simplifies instrumentation */

    SglSyncEv *sync_ev;
    /* The latest sync event */

    instr_block_t *iblocks;
    uint iblock_count;
    /* Each iblock holds sigil event data for a single instruction.
     * The iblock buffer holds event data for a single event block */

    uint event_block_events;
    /* total sigil events for the current event block
     * (single-entry, single-exit) */

    byte *seg_base;
    /* So we can access the raw TLS from client clean calls */

} per_thread_t;

#define MIN_DR_PER_THREAD_BUFFER_EVENTS (1UL << 20)

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

enum
{
    MEMREF_TLS_OFFS_PERTHR_PTR = 0,
    /* the client's TLS (from drmgr) */

    MEMREF_TLS_OFFS_SGLEV_PTR,
    MEMREF_TLS_OFFS_SGLEND_PTR,
    MEMREF_TLS_OFFS_SGLUSED_PTR,
    /* sigil shared memory buffer */

    MEMREF_TLS_OFFS_IBLOCKS_PTR,

    MEMREF_TLS_OFFS_MEMREFBASE_PTR,
    MEMREF_TLS_OFFS_MEMREFCURR_PTR,
    /* the mem cache where addresses are stored */

    MEMREF_TLS_OFFS_SGLSYNCEV_PTR,
    /* holds NULL if no event, otherwise holds a SglSyncEv */

    MEMREF_TLS_OFFS_ACTIVE,
    /* whether the thread is under active instrumentation,
     * e.g. a thread is inactive in a lock */

    MEMREF_TLS_COUNT,
};
extern reg_id_t raw_tls_seg;
extern uint     raw_tls_memref_offs;
/* raw tls for faster access from instrumented code */

#define TLS_SLOT(tls_base, enum_val) (void **)((byte *)(tls_base)+raw_tls_memref_offs+(sizeof(void*)*enum_val))

#define PERTHR_PTR(tls_base) *(per_thread_t **)TLS_SLOT(tls_base, MEMREF_TLS_OFFS_PERTHR_PTR)
#define SGLEV_PTR(tls_base) *(SglEvVariant **)TLS_SLOT(tls_base, MEMREF_TLS_OFFS_SGLEV_PTR)
#define SGLEND_PTR(tls_base) *(SglEvVariant **)TLS_SLOT(tls_base, MEMREF_TLS_OFFS_SGLEND_PTR)
#define SGLUSED_PTR(tls_base) *(size_t **)TLS_SLOT(tls_base, MEMREF_TLS_OFFS_SGLUSED_PTR)
#define IBLOCKS_PTR(tls_base) *(instr_block_t **)TLS_SLOT(tls_base, MEMREF_TLS_OFFS_IBLOCKS_PTR)
#define MEMREFBASE_PTR(tls_base) *(mem_ref_t **)TLS_SLOT(tls_base, MEMREF_TLS_OFFS_MEMREFBASE_PTR)
#define MEMREFCURR_PTR(tls_base) *(mem_ref_t **)TLS_SLOT(tls_base, MEMREF_TLS_OFFS_MEMREFCURR_PTR)
#define ACTIVE(tls_base) *(bool *)TLS_SLOT(tls_base, MEMREF_TLS_OFFS_ACTIVE)
#define SGLSYNCEV_PTR(tls_base) *(SglSyncEv **)TLS_SLOT(tls_base, MEMREF_TLS_OFFS_SGLSYNCEV_PTR)

#define PERTHR_OFFS (raw_tls_memref_offs + MEMREF_TLS_OFFS_PERTHR_PTR*sizeof(void*))
#define SGLEV_OFFS (raw_tls_memref_offs + MEMREF_TLS_OFFS_SGLEV_PTR*sizeof(void*))
#define SGLEND_OFFS (raw_tls_memref_offs + MEMREF_TLS_OFFS_SGLEND_PTR*sizeof(void*))
#define SGLUSED_OFFS (raw_tls_memref_offs + MEMREF_TLS_OFFS_SGLUSED_PTR*sizeof(void*))
#define IBLOCKS_OFFS (raw_tls_memref_offs + MEMREF_TLS_OFFS_IBLOCKS_PTR*sizeof(void*))
#define MEMREFBASE_OFFS (raw_tls_memref_offs + MEMREF_TLS_OFFS_MEMREFBASE_PTR*sizeof(void*))
#define MEMREFCURR_OFFS (raw_tls_memref_offs + MEMREF_TLS_OFFS_MEMREFCURR_PTR*sizeof(void*))
#define ACTIVE_OFFS (raw_tls_memref_offs + MEMREF_TLS_OFFS_ACTIVE*sizeof(void*))
#define SGLSYNCEV_OFFS (raw_tls_memref_offs + MEMREF_TLS_OFFS_SGLSYNCEV_PTR*sizeof(void*))

/////////////////////////////////////////////////////////////////////
//                           Option Parsing                        //
/////////////////////////////////////////////////////////////////////
typedef struct _command_line_options command_line_options;
struct _command_line_options
{
    const char *ipc_dir;
    /* Directory where shared memory and named fifos
     * are located; generated by Sigil2 core */

    const char *start_func;
    const char *stop_func;
    /* DrSigil will begin and end event generation at these functions */

    int frontend_threads;
    /* Essentially, DrSigil will serialize the
     * instrumented binary into this many threads */

    bool standalone;
    /* In some cases (mainly testing), it is desirable
     * to run this tool without Sigil2.
     * This flag instructs the tool to ignore IPC with the
     * Sigil2 core */
} clo;


/////////////////////////////////////////////////////////////////////
//                         FUNCTION DECLARATIONS                   //
/////////////////////////////////////////////////////////////////////

/* The instrumentation functions MUST be called in a specific order */
/* TODO document preconditions/postconditions */
void instrument_reset(void *drcontext, instrlist_t *ilist, instr_t *where,
                      per_thread_t *tcxt);

void instrument_mem_cache(void *drcontext, instrlist_t *ilist, instr_t *where,
                          uint *mem_ref_count);

void instrument_comp_cache(instr_t *instr, uint *comp_count, SglCompEv *cache);

void instrument_sigil_events(void *drcontext, instrlist_t *ilist, instr_t *where,
                             per_thread_t *tcxt);

/* IPC */
void init_IPC(int idx, const char *path, bool standalone);
void terminate_IPC(int idx);
void set_shared_memory_buffer(per_thread_t *tcxt);
void force_thread_flush(per_thread_t *tcxt);

void parse(int argc, char *argv[], command_line_options *clo);

#endif
