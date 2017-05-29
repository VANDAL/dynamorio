#ifndef PTHREAD_DEFINES_H
#define PTHREAD_DEFINES_H

#include "drsigil.h"
#include "drmgr.h"
#include "drwrap.h"
#include "pthread.h"

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
set_sync_event(per_thread_t *tcxt, SyncType type, SyncID data[const static 2])
{
    tcxt->sync_ev->type = type;
    tcxt->sync_ev->data[0] = data[0];
    tcxt->sync_ev->data[1] = data[1];
}
static inline void
send_sync_event(per_thread_t *tcxt)
{
    /* sets the raw TLS sync event pointer,
     * letting instrumentation know to send the
     * event to sigil */
    SGLSYNCEV_PTR(tcxt->seg_base) = tcxt->sync_ev;
}

static inline void
set_blocked_and_deactivate(per_thread_t *tcxt)
{
    ACTIVE(tcxt->seg_base) = false;
    tcxt->is_blocked = true;
    force_thread_flush(tcxt);
}
static inline void
set_unblocked_and_reactivate(per_thread_t *tcxt)
{
    ACTIVE(tcxt->seg_base) = true;
    tcxt->is_blocked = false;
}
static inline void
deactivate(per_thread_t *tcxt)
{
    ACTIVE(tcxt->seg_base) = false;
}
static inline void
reactivate(per_thread_t *tcxt)
{
    ACTIVE(tcxt->seg_base) = true;
}

////////////////////////////////////////////
// PTHREAD CREATE
////////////////////////////////////////////
static void
wrap_pre_pthread_create(void *wrapcxt, OUT void **user_data)
{
    per_thread_t *tcxt = drmgr_get_tls_field(dr_get_current_drcontext(), tls_idx);
    LOG_SYNC_ENTER(pthread_create, tcxt->thread_id);
    deactivate(tcxt);

    /* TODO dereference the pthread_t? */
    SyncID data[2] = {(uintptr_t)(pthread_t*)drwrap_get_arg(wrapcxt, 0), 0};
    set_sync_event(tcxt, SGLPRIM_SYNC_CREATE, data);
}
static void
wrap_post_pthread_create(void *wrapcxt, void *user_data)
{
    per_thread_t *tcxt = drmgr_get_tls_field(dr_get_current_drcontext(), tls_idx);
    LOG_SYNC_EXIT(pthread_create, tcxt->thread_id);
    reactivate(tcxt);

    send_sync_event(tcxt);
}


////////////////////////////////////////////
// PTHREAD JOIN
////////////////////////////////////////////
static void
wrap_pre_pthread_join(void *wrapcxt, OUT void **user_data)
{
    per_thread_t *tcxt = drmgr_get_tls_field(dr_get_current_drcontext(), tls_idx);
    LOG_SYNC_ENTER(pthread_join, tcxt->thread_id);
    set_blocked_and_deactivate(tcxt);

    SyncID data[2] = {(uintptr_t)(pthread_t*)drwrap_get_arg(wrapcxt, 0), 0};
    set_sync_event(tcxt, SGLPRIM_SYNC_JOIN, data);
}
static void
wrap_post_pthread_join(void *wrapcxt, void *user_data)
{
    per_thread_t *tcxt = drmgr_get_tls_field(dr_get_current_drcontext(), tls_idx);
    LOG_SYNC_EXIT(pthread_join, tcxt->thread_id);
    set_unblocked_and_reactivate(tcxt);
    send_sync_event(tcxt);
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

    SyncID data[2] = {(uintptr_t)(pthread_mutex_t*)drwrap_get_arg(wrapcxt, 0), 0};
    set_sync_event(tcxt, SGLPRIM_SYNC_LOCK, data);
}
static void
wrap_post_pthread_mutex_lock(void *wrapcxt, void *user_data)
{
    per_thread_t *tcxt = drmgr_get_tls_field(dr_get_current_drcontext(), tls_idx);
    LOG_SYNC_EXIT(pthread_mutex_lock, tcxt->thread_id);
    set_unblocked_and_reactivate(tcxt);

    send_sync_event(tcxt);
}


////////////////////////////////////////////
// PTHREAD MUTEX UNLOCK
////////////////////////////////////////////
static void
wrap_pre_pthread_mutex_unlock(void *wrapcxt, OUT void **user_data)
{
    per_thread_t *tcxt = drmgr_get_tls_field(dr_get_current_drcontext(), tls_idx);
    LOG_SYNC_ENTER(pthread_mutex_unlock, tcxt->thread_id);
    deactivate(tcxt);

    SyncID data[2] = {(uintptr_t)(pthread_mutex_t*)drwrap_get_arg(wrapcxt, 0), 0};
    set_sync_event(tcxt, SGLPRIM_SYNC_UNLOCK, data);
}
static void
wrap_post_pthread_mutex_unlock(void *wrapcxt, void *user_data)
{
    per_thread_t *tcxt = drmgr_get_tls_field(dr_get_current_drcontext(), tls_idx);
    LOG_SYNC_EXIT(pthread_mutex_unlock, tcxt->thread_id);
    reactivate(tcxt);

    send_sync_event(tcxt);
}


////////////////////////////////////////////
// PTHREAD BARRIER
////////////////////////////////////////////
static void
wrap_pre_pthread_barrier(void *wrapcxt, OUT void **user_data)
{
    per_thread_t *tcxt = drmgr_get_tls_field(dr_get_current_drcontext(), tls_idx);
    LOG_SYNC_ENTER(pthread_barrier, tcxt->thread_id);
    set_blocked_and_deactivate(tcxt);

    SyncID data[2] = {(uintptr_t)(pthread_barrier_t*)drwrap_get_arg(wrapcxt, 0), 0};
    set_sync_event(tcxt, SGLPRIM_SYNC_BARRIER, data);
}
static void
wrap_post_pthread_barrier(void *wrapcxt, void *user_data)
{
    per_thread_t *tcxt = drmgr_get_tls_field(dr_get_current_drcontext(), tls_idx);
    LOG_SYNC_EXIT(pthread_barrier, tcxt->thread_id);
    set_unblocked_and_reactivate(tcxt);

    send_sync_event(tcxt);
}


////////////////////////////////////////////
// PTHREAD CONDITIONAL WAIT
////////////////////////////////////////////
static void
wrap_pre_pthread_cond_wait(void *wrapcxt, OUT void **user_data)
{
    per_thread_t *tcxt = drmgr_get_tls_field(dr_get_current_drcontext(), tls_idx);
    LOG_SYNC_ENTER(pthread_cond_wait, tcxt->thread_id);
    set_blocked_and_deactivate(tcxt);

    SyncID data[2] = {(uintptr_t)(pthread_cond_t*)drwrap_get_arg(wrapcxt, 0),
                      (uintptr_t)(pthread_mutex_t*)drwrap_get_arg(wrapcxt, 1)};
    set_sync_event(tcxt, SGLPRIM_SYNC_CONDWAIT, data);
}
static void
wrap_post_pthread_cond_wait(void *wrapcxt, void *user_data)
{
    per_thread_t *tcxt = drmgr_get_tls_field(dr_get_current_drcontext(), tls_idx);
    LOG_SYNC_EXIT(pthread_cond_wait, tcxt->thread_id);
    set_unblocked_and_reactivate(tcxt);

    send_sync_event(tcxt);
}


////////////////////////////////////////////
// PTHREAD CONDITIONAL SIGNAL
////////////////////////////////////////////
static void
wrap_pre_pthread_cond_sig(void *wrapcxt, OUT void **user_data)
{
    per_thread_t *tcxt = drmgr_get_tls_field(dr_get_current_drcontext(), tls_idx);
    LOG_SYNC_ENTER(pthread_cond_sig, tcxt->thread_id);
    deactivate(tcxt);

    SyncID data[2] = {(uintptr_t)(pthread_cond_t*)drwrap_get_arg(wrapcxt, 0), 0};
    set_sync_event(tcxt, SGLPRIM_SYNC_CONDSIG, data);
}
static void
wrap_post_pthread_cond_sig(void *wrapcxt, void *user_data)
{
    per_thread_t *tcxt = drmgr_get_tls_field(dr_get_current_drcontext(), tls_idx);
    LOG_SYNC_EXIT(pthread_cond_sig, tcxt->thread_id);
    reactivate(tcxt);

    send_sync_event(tcxt);
}


////////////////////////////////////////////
// PTHREAD SPIN LOCK
////////////////////////////////////////////
static void
wrap_pre_pthread_spin_lock(void *wrapcxt, OUT void **user_data)
{
    per_thread_t *tcxt = drmgr_get_tls_field(dr_get_current_drcontext(), tls_idx);
    LOG_SYNC_ENTER(pthread_spin_lock, tcxt->thread_id);

    SyncID data[2] = {(uintptr_t)(pthread_spinlock_t*)drwrap_get_arg(wrapcxt, 0), 0};
    set_sync_event(tcxt, SGLPRIM_SYNC_SPINLOCK, data);
}
static void
wrap_post_pthread_spin_lock(void *wrapcxt, void *user_data)
{
    per_thread_t *tcxt = drmgr_get_tls_field(dr_get_current_drcontext(), tls_idx);
    LOG_SYNC_EXIT(pthread_spin_lock, tcxt->thread_id);

    send_sync_event(tcxt);
}


////////////////////////////////////////////
// PTHREAD SPIN UNLOCK
////////////////////////////////////////////
static void
wrap_pre_pthread_spin_unlock(void *wrapcxt, OUT void **user_data)
{
    per_thread_t *tcxt = drmgr_get_tls_field(dr_get_current_drcontext(), tls_idx);
    LOG_SYNC_ENTER(pthread_spin_unlock, tcxt->thread_id);

    SyncID data[2] = {(uintptr_t)(pthread_spinlock_t*)drwrap_get_arg(wrapcxt, 0), 0};
    set_sync_event(tcxt, SGLPRIM_SYNC_SPINUNLOCK, data);
}
static void
wrap_post_pthread_spin_unlock(void *wrapcxt, void *user_data)
{
    per_thread_t *tcxt = drmgr_get_tls_field(dr_get_current_drcontext(), tls_idx);
    LOG_SYNC_EXIT(pthread_spin_unlock, tcxt->thread_id);

    send_sync_event(tcxt);
}

#endif
