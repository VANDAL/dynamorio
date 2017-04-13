#include <stddef.h> /* for offsetof */
#include <string.h> /* for strlen */

#include "drsigil.h"
#include "pthread_defines.h"
#include "start_stop_functions.h"

#include "drmgr.h"
#include "drwrap.h"
#include "drutil.h"


///////////////////////////////////////////////////////
// Definitions
///////////////////////////////////////////////////////

int tls_idx;
volatile bool roi = true;

static uint64 num_threads = 0;
/* Thread IDs are generated by the order of each thread's initialization */

enum {
    /* Allocated TLS slot offsets */
    DRSIGIL_TLS_BUF_PTR = 0,
    DRSIGIL_TLS_COUNT, /* total number of TLS slots allocated */
};


///////////////////////////////////////////////////////
// Die with error msg
///////////////////////////////////////////////////////
void
dr_abort_w_msg(const char *msg)
{
    dr_fprintf(STDERR, "[DRSIGIL] [abort] %s\n", msg);
    dr_abort();
}


///////////////////////////////////////////////////////
// DynamoRIO event callbacks
///////////////////////////////////////////////////////
static dr_emit_flags_t
event_bb_instrument(void *drcontext, /*UNUSED*/ void *tag,
                    instrlist_t *bb, instr_t *where,
                    /*UNUSED*/ bool for_trace,
                    /*UNUSED*/ bool translating,
                    /*UNUSED*/ void *user_data)
{
    /* some checks to make sure this is an
     * actual application instruction being
     * instrumented */
    if (instr_is_app(where) == false ||
        instr_get_app_pc(where) == NULL ||
        instr_is_nop(where) == true) /* ignore instructions that don't do 'work' */
        /* TODO count control flow instructions? */
        return DR_EMIT_DEFAULT;


    /*************************************/
    /* Sigil Context Event - Instruction */
    /*************************************/
    instrument_instr(drcontext, bb, where);

    /**********************/
    /* Sigil Memory Event */
    /**********************/
    if (instr_reads_memory(where) == true)
        for (int i=0; i<instr_num_srcs(where); i++)
            if (opnd_is_memory_reference(instr_get_src(where, i)))
                instrument_mem(drcontext, bb, where, i, SGLPRIM_MEM_LOAD);

    if (instr_writes_memory(where) == true)
        for (int i=0; i<instr_num_dsts(where); i++)
            if (opnd_is_memory_reference(instr_get_dst(where, i)))
                instrument_mem(drcontext, bb, where, i, SGLPRIM_MEM_STORE);

    /******************************/
    /* Sigil Compute Event - FLOP */
    /******************************/
    dr_fp_type_t fp_t;
    if(instr_is_floating_ex(where, &fp_t) && (fp_t == DR_FP_MATH))
    {
        instrument_comp(drcontext, bb, where, SGLPRIM_COMP_FLOP);
    }
    else
    /*****************************/
    /* Sigil Compute Event - IOP */
    /*****************************/
    {
        /* Brute force checking of opcode */
        switch(instr_get_opcode(where))
        {
        case OP_add:
        case OP_xadd:
        case OP_paddq:
        case OP_adc:
        case OP_sub:
        case OP_sbb:
        case OP_mul:
        case OP_imul:
        case OP_div:
        case OP_idiv:
        case OP_neg:
        case OP_inc:
        case OP_dec:
        /* TODO count bit ops as IOP? */
        case OP_xor:
        case OP_and:
        case OP_or:
        case OP_bt:
        case OP_bts:
        case OP_btr:

        case OP_aas:
            instrument_comp(drcontext, bb, where, SGLPRIM_COMP_IOP);
        default:
            break;
        }
    }

    return DR_EMIT_DEFAULT;
}


static dr_emit_flags_t
event_bb_app2app(void *drcontext, void *tag, instrlist_t *bb,
                 bool for_trace, bool translating)
{
    /* we transform string loops into regular loops so we can more easily
     * monitor every memory reference they make */
    /* XXX
     * We run into reachability problems,
     * as-per the documentation on 'drutil_expand_rep_string',
     * due to extra instrumentation added by Sigil2.
     *
     * We don't expect string loops to be significant in benchmarks,
     * so this should be OK.

    if (!drutil_expand_rep_string(drcontext, bb)) {
        DR_ASSERT(false);
    }

     */
    return DR_EMIT_DEFAULT;
}


static void
event_thread_init(void *drcontext)
{
    per_thread_t *init = dr_thread_alloc(drcontext, sizeof(per_thread_t));
    per_thread_buffer_t *init_buffer = &init->buffer;

    init->active = true;
    init->thread_id = __sync_add_and_fetch(&num_threads,1);
    init->has_channel_lock = false;
    init_buffer->events_ptr = NULL;
    init_buffer->events_end = NULL;
    init_buffer->events_used = NULL;

    drmgr_set_tls_field(drcontext, tls_idx, init);
}


static void
event_thread_exit(void *drcontext)
{
    per_thread_t *tcxt = drmgr_get_tls_field(drcontext, tls_idx);
    force_thread_flush(tcxt);
    dr_thread_free(drcontext, tcxt, sizeof(per_thread_t));
}


static void
event_exit(void)
{
    for(int i=0; i<clo.frontend_threads; ++i)
        terminate_IPC(i);

    if (!drmgr_unregister_thread_init_event(event_thread_init) ||
        !drmgr_unregister_thread_exit_event(event_thread_exit) ||
        !drmgr_unregister_tls_field(tls_idx))
        dr_abort_w_msg("failed to unregister drmgr event callbacks");

    drutil_exit();
    drmgr_exit();
    drwrap_exit();
}

static void
module_load_event(void *drcontext, const module_data_t *mod, bool loaded)
{
    app_pc towrap;

    if ((clo.start_func != NULL) &&
        (towrap = (app_pc)dr_get_proc_address(mod->handle, clo.start_func)) != NULL)
        drwrap_wrap(towrap, wrap_pre_start_func, wrap_post_start_func);

    if ((clo.stop_func != NULL) &&
        (towrap = (app_pc)dr_get_proc_address(mod->handle, clo.stop_func)) != NULL)
        drwrap_wrap(towrap, wrap_pre_stop_func, wrap_post_stop_func);

    if ((towrap = (app_pc)dr_get_proc_address(mod->handle, MAIN)) != NULL)
        drwrap_wrap(towrap, wrap_pre_start_at_main, wrap_post_start_at_main);

    if ((towrap = (app_pc)dr_get_proc_address(mod->handle, P_CREATE)) != NULL)
        drwrap_wrap(towrap, wrap_pre_pthread_create, wrap_post_pthread_create);

    if ((towrap = (app_pc)dr_get_proc_address(mod->handle, P_JOIN)) != NULL)
        drwrap_wrap(towrap, wrap_pre_pthread_join, wrap_post_pthread_join);

    if ((towrap = (app_pc)dr_get_proc_address(mod->handle, P_MUTEX_LOCK)) != NULL)
        drwrap_wrap(towrap, wrap_pre_pthread_mutex_lock, wrap_post_pthread_mutex_lock);

    if ((towrap = (app_pc)dr_get_proc_address(mod->handle, P_MUTEX_UNLOCK)) != NULL)
        drwrap_wrap(towrap, wrap_pre_pthread_mutex_unlock, wrap_post_pthread_mutex_unlock);

    if ((towrap = (app_pc)dr_get_proc_address(mod->handle, P_BARRIER)) != NULL)
        drwrap_wrap(towrap, wrap_pre_pthread_barrier, wrap_post_pthread_barrier);

    if ((towrap = (app_pc)dr_get_proc_address(mod->handle, P_COND_WAIT)) != NULL)
        drwrap_wrap(towrap, wrap_pre_pthread_cond_wait, wrap_post_pthread_cond_wait);

    if ((towrap = (app_pc)dr_get_proc_address(mod->handle, P_COND_SIG)) != NULL)
        drwrap_wrap(towrap, wrap_pre_pthread_cond_sig, wrap_post_pthread_cond_sig);

    if ((towrap = (app_pc)dr_get_proc_address(mod->handle, P_SPIN_LOCK)) != NULL)
        drwrap_wrap(towrap, wrap_pre_pthread_spin_lock, wrap_post_pthread_spin_lock);

    if ((towrap = (app_pc)dr_get_proc_address(mod->handle, P_SPIN_UNLOCK)) != NULL)
        drwrap_wrap(towrap, wrap_pre_pthread_spin_unlock, wrap_post_pthread_spin_unlock);
}


///////////////////////////////////////////////////////
// DynamoRIO client initialization
///////////////////////////////////////////////////////
DR_EXPORT void
dr_client_main(client_id_t id, int argc, const char *argv[])
{
    parse(argc, (char**)argv, &clo);

    dr_set_client_name("DrSigil",
                       "https://github.com/mdlui/Sigil2/issues");

    dr_register_exit_event(event_exit);

    if (!drmgr_init() ||
        !drutil_init() ||
        !drwrap_init())
        DR_ASSERT(false);

    /* Specify priority relative to other instrumentation operations: */
    drmgr_priority_t priority = {
        sizeof(priority), /* size of struct */
        "sigil2",         /* name of our operation */
        NULL,             /* optional name of operation we should precede */
        NULL,             /* optional name of operation we should follow */
        0};               /* numeric priority */

    if (!drmgr_register_bb_instrumentation_event(NULL, event_bb_instrument, NULL) ||
        !drmgr_register_bb_app2app_event(event_bb_app2app, &priority) ||
        !drmgr_register_thread_init_event(event_thread_init) ||
        !drmgr_register_thread_exit_event(event_thread_exit) ||
        !drmgr_register_module_load_event(module_load_event))
        dr_abort_w_msg("failed to register drmgr event callbacks");

    /* threads spawned so far */
    num_threads = 0;

    /* Initialize IPC */
    /* There are 'frontend_threads' number of channels */
    for(int i=0; i<clo.frontend_threads; ++i)
        init_IPC(i, clo.ipc_dir);

    /* initialize thread local resources */
    tls_idx = drmgr_register_tls_field();
}
