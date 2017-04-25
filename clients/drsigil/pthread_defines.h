#ifndef PTHREAD_DEFINES_H
#define PTHREAD_DEFINES_H

#include "drsigil.h"
#include "drmgr.h"

#define MAIN           "main"
#define P_CREATE       "pthread_create"
#define P_JOIN         "pthread_join"
#define P_MUTEX_LOCK   "pthread_mutex_lock"
#define P_MUTEX_UNLOCK "pthread_mutex_unlock"
#define P_BARRIER      "pthread_barrier_wait"
#define P_COND_WAIT    "pthread_cond_wait"
#define P_COND_SIG     "pthread_cond_signal"
#define P_SPIN_LOCK    "pthread_spin_lock"
#define P_SPIN_UNLOCK  "pthread_spin_unlock"

#ifdef SGLDEBUG
#define LOG_SYNC_ENTER(sync_event, thread_id) \
    dr_printf("entering "#sync_event": %d\n", thread_id)
#define LOG_SYNC_EXIT(sync_event, thread_id) \
    dr_printf("exiting  "#sync_event": %d\n", thread_id)
#else
#define LOG_SYNC_ENTER(sync_event, thread_id)
#define LOG_SYNC_EXIT(sync_event, thread_id)
#endif

static inline void
send_sync_event(per_thread_t *tcxt, SyncType type)
{
    if(tcxt->buffer.events_ptr == tcxt->buffer.events_end)
        set_shared_memory_buffer(tcxt);

    SglEvVariant *event_slot = tcxt->buffer.events_ptr;
    event_slot->tag       = SGL_SYNC_TAG;
    event_slot->sync.type = type;

    ++tcxt->buffer.events_ptr;
    ++*(tcxt->buffer.events_used);
}

static inline void
set_blocked_and_deactivate(per_thread_t *tcxt)
{
    tcxt->active = false;
    tcxt->is_blocked = true;
    force_thread_flush(tcxt);
}

static inline void
set_unblocked_and_reactivate(per_thread_t *tcxt)
{
    tcxt->active = true;
    tcxt->is_blocked = false;
}

static inline void
deactivate(per_thread_t *tcxt)
{
    tcxt->active = false;
}

static inline void
reactivate(per_thread_t *tcxt)
{
    tcxt->active = false;
}

////////////////////////////////////////////
// PTHREAD CREATE
////////////////////////////////////////////
static void
wrap_pre_pthread_create(void *wrapcxt, OUT void **user_data)
{
    per_thread_t *tcxt = drmgr_get_tls_field(dr_get_current_drcontext(),
                                             tls_idx);
    LOG_SYNC_ENTER(pthread_create, tcxt->thread_id);
    deactivate(tcxt);
}
static void
wrap_post_pthread_create(void *wrapcxt, void *user_data)
{
    per_thread_t *tcxt = drmgr_get_tls_field(dr_get_current_drcontext(),
                                             tls_idx);
    LOG_SYNC_EXIT(pthread_create, tcxt->thread_id);
    reactivate(tcxt);
    send_sync_event(tcxt, SGLPRIM_SYNC_CREATE);
}


////////////////////////////////////////////
// PTHREAD JOIN
////////////////////////////////////////////
static void
wrap_pre_pthread_join(void *wrapcxt, OUT void **user_data)
{
    per_thread_t *tcxt = drmgr_get_tls_field(dr_get_current_drcontext(),
                                             tls_idx);
    LOG_SYNC_ENTER(pthread_join, tcxt->thread_id);
    set_blocked_and_deactivate(tcxt);
}
static void
wrap_post_pthread_join(void *wrapcxt, void *user_data)
{
    per_thread_t *tcxt = drmgr_get_tls_field(dr_get_current_drcontext(),
                                             tls_idx);
    LOG_SYNC_EXIT(pthread_join, tcxt->thread_id);
    set_unblocked_and_reactivate(tcxt);
    send_sync_event(tcxt, SGLPRIM_SYNC_JOIN);
}


////////////////////////////////////////////
// PTHREAD MUTEX LOCK
////////////////////////////////////////////
static void
wrap_pre_pthread_mutex_lock(void *wrapcxt, OUT void **user_data)
{
    per_thread_t *tcxt = drmgr_get_tls_field(dr_get_current_drcontext(),
                                             tls_idx);
    LOG_SYNC_ENTER(pthread_mutex_lock, tcxt->thread_id);
    set_blocked_and_deactivate(tcxt);
}
static void
wrap_post_pthread_mutex_lock(void *wrapcxt, void *user_data)
{
    per_thread_t *tcxt = drmgr_get_tls_field(dr_get_current_drcontext(),
                                             tls_idx);
    LOG_SYNC_EXIT(pthread_mutex_lock, tcxt->thread_id);
    set_unblocked_and_reactivate(tcxt);

    /* TODO(soon) Causing deadlocks */
    //send_sync_event(tcxt, SGLPRIM_SYNC_LOCK);
}


////////////////////////////////////////////
// PTHREAD MUTEX UNLOCK
////////////////////////////////////////////
static void
wrap_pre_pthread_mutex_unlock(void *wrapcxt, OUT void **user_data)
{
    per_thread_t *tcxt = drmgr_get_tls_field(dr_get_current_drcontext(),
                                             tls_idx);
    LOG_SYNC_ENTER(pthread_mutex_unlock, tcxt->thread_id);
    deactivate(tcxt);
}
static void
wrap_post_pthread_mutex_unlock(void *wrapcxt, void *user_data)
{
    per_thread_t *tcxt = drmgr_get_tls_field(dr_get_current_drcontext(),
                                             tls_idx);
    LOG_SYNC_EXIT(pthread_mutex_unlock, tcxt->thread_id);
    reactivate(tcxt);
    send_sync_event(tcxt, SGLPRIM_SYNC_UNLOCK);
}


////////////////////////////////////////////
// PTHREAD BARRIER
////////////////////////////////////////////
static void
wrap_pre_pthread_barrier(void *wrapcxt, OUT void **user_data)
{
    //size_t sz = (size_t) drwrap_get_arg(wrapcxt, IF_WINDOWS_ELSE(2,0));

    per_thread_t *tcxt = drmgr_get_tls_field(dr_get_current_drcontext(),
                                             tls_idx);
    LOG_SYNC_ENTER(pthread_barrier, tcxt->thread_id);
    set_blocked_and_deactivate(tcxt);
}
static void
wrap_post_pthread_barrier(void *wrapcxt, void *user_data)
{
    per_thread_t *tcxt = drmgr_get_tls_field(dr_get_current_drcontext(),
                                             tls_idx);
    LOG_SYNC_EXIT(pthread_barrier, tcxt->thread_id);
    set_unblocked_and_reactivate(tcxt);
    send_sync_event(tcxt, SGLPRIM_SYNC_BARRIER);
}


////////////////////////////////////////////
// PTHREAD CONDITIONAL WAIT
////////////////////////////////////////////
static void
wrap_pre_pthread_cond_wait(void *wrapcxt, OUT void **user_data)
{
    //size_t sz = (size_t) drwrap_get_arg(wrapcxt, IF_WINDOWS_ELSE(2,0));

    per_thread_t *tcxt = drmgr_get_tls_field(dr_get_current_drcontext(),
                                             tls_idx);
    LOG_SYNC_ENTER(pthread_cond_wait, tcxt->thread_id);
    set_blocked_and_deactivate(tcxt);
}
static void
wrap_post_pthread_cond_wait(void *wrapcxt, void *user_data)
{
    per_thread_t *tcxt = drmgr_get_tls_field(dr_get_current_drcontext(),
                                             tls_idx);
    LOG_SYNC_EXIT(pthread_cond_wait, tcxt->thread_id);
    set_unblocked_and_reactivate(tcxt);

    /* TODO(soon) Causing deadlocks */
    //send_sync_event(tcxt, SGLPRIM_SYNC_CONDWAIT);
}


////////////////////////////////////////////
// PTHREAD CONDITIONAL SIGNAL
////////////////////////////////////////////
static void
wrap_pre_pthread_cond_sig(void *wrapcxt, OUT void **user_data)
{
    //size_t sz = (size_t) drwrap_get_arg(wrapcxt, IF_WINDOWS_ELSE(2,0));
    per_thread_t *tcxt = drmgr_get_tls_field(dr_get_current_drcontext(),
                                             tls_idx);
    LOG_SYNC_ENTER(pthread_cond_sig, tcxt->thread_id);
    deactivate(tcxt);
}
static void
wrap_post_pthread_cond_sig(void *wrapcxt, void *user_data)
{
    per_thread_t *tcxt = drmgr_get_tls_field(dr_get_current_drcontext(),
                                             tls_idx);
    LOG_SYNC_EXIT(pthread_cond_sig, tcxt->thread_id);
    reactivate(tcxt);
    send_sync_event(tcxt, SGLPRIM_SYNC_CONDSIG);
}


////////////////////////////////////////////
// PTHREAD SPIN LOCK
////////////////////////////////////////////
static void
wrap_pre_pthread_spin_lock(void *wrapcxt, OUT void **user_data)
{
    per_thread_t *tcxt = drmgr_get_tls_field(dr_get_current_drcontext(),
                                             tls_idx);
    (void)tcxt;
    LOG_SYNC_ENTER(pthread_spin_lock, tcxt->thread_id);
    //size_t sz = (size_t) drwrap_get_arg(wrapcxt, IF_WINDOWS_ELSE(2,0));
}
static void
wrap_post_pthread_spin_lock(void *wrapcxt, void *user_data)
{
    per_thread_t *tcxt = drmgr_get_tls_field(dr_get_current_drcontext(),
                                             tls_idx);
    (void)tcxt;
    LOG_SYNC_EXIT(pthread_spin_lock, tcxt->thread_id);
}


////////////////////////////////////////////
// PTHREAD SPIN UNLOCK
////////////////////////////////////////////
static void
wrap_pre_pthread_spin_unlock(void *wrapcxt, OUT void **user_data)
{
    per_thread_t *tcxt = drmgr_get_tls_field(dr_get_current_drcontext(),
                                             tls_idx);
    (void)tcxt;
    LOG_SYNC_ENTER(pthread_spin_unlock, tcxt->thread_id);
    //size_t sz = (size_t) drwrap_get_arg(wrapcxt, IF_WINDOWS_ELSE(2,0));
}
static void
wrap_post_pthread_spin_unlock(void *wrapcxt, void *user_data)
{
    per_thread_t *tcxt = drmgr_get_tls_field(dr_get_current_drcontext(),
                                             tls_idx);
    (void)tcxt;
    LOG_SYNC_EXIT(pthread_spin_unlock, tcxt->thread_id);
}

#endif
