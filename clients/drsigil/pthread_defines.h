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

////////////////////////////////////////////
// PTHREAD CREATE
////////////////////////////////////////////
static void
wrap_pre_pthread_create(void *wrapcxt, OUT void **user_data)
{
    void *drcontext  = dr_get_current_drcontext();
    per_thread_t *tcxt = drmgr_get_tls_field(drcontext, tls_idx);
    tcxt->active = false;
}
static void
wrap_post_pthread_create(void *wrapcxt, void *user_data)
{
    void *drcontext  = dr_get_current_drcontext();
    per_thread_t *tcxt = drmgr_get_tls_field(drcontext, tls_idx);
    tcxt->active = true;

    if(tcxt->buffer.events_ptr == tcxt->buffer.events_end)
        set_shared_memory_buffer(tcxt);

    tcxt->buffer.events_ptr->tag       = SGL_SYNC_TAG;
    tcxt->buffer.events_ptr->sync.type = SGLPRIM_SYNC_CREATE;

    ++tcxt->buffer.events_ptr;
    ++*(tcxt->buffer.events_used);
}


////////////////////////////////////////////
// PTHREAD JOIN
////////////////////////////////////////////
static void
wrap_pre_pthread_join(void *wrapcxt, OUT void **user_data)
{
    void *drcontext  = dr_get_current_drcontext();
    per_thread_t *tcxt = drmgr_get_tls_field(drcontext, tls_idx);
    tcxt->active = false;

    if(tcxt->buffer.events_ptr == tcxt->buffer.events_end)
        set_shared_memory_buffer(tcxt);

    tcxt->buffer.events_ptr->tag       = SGL_SYNC_TAG;
    tcxt->buffer.events_ptr->sync.type = SGLPRIM_SYNC_JOIN;

    ++tcxt->buffer.events_ptr;
    ++*(tcxt->buffer.events_used);

    force_thread_flush(tcxt);
}
static void
wrap_post_pthread_join(void *wrapcxt, void *user_data)
{
    void *drcontext  = dr_get_current_drcontext();
    per_thread_t *tcxt = drmgr_get_tls_field(drcontext, tls_idx);
    tcxt->active = true;
}


////////////////////////////////////////////
// PTHREAD MUTEX LOCK
////////////////////////////////////////////
static void
wrap_pre_pthread_mutex_lock(void *wrapcxt, OUT void **user_data)
{
    void *drcontext  = dr_get_current_drcontext();
    per_thread_t *tcxt = drmgr_get_tls_field(drcontext, tls_idx);
    tcxt->active = false;
}
static void
wrap_post_pthread_mutex_lock(void *wrapcxt, void *user_data)
{
    void *drcontext  = dr_get_current_drcontext();
    per_thread_t *tcxt = drmgr_get_tls_field(drcontext, tls_idx);
    tcxt->active = true;

    if(tcxt->buffer.events_ptr == tcxt->buffer.events_end)
        set_shared_memory_buffer(tcxt);

    tcxt->buffer.events_ptr->tag       = SGL_SYNC_TAG;
    tcxt->buffer.events_ptr->sync.type = SGLPRIM_SYNC_LOCK;

    ++tcxt->buffer.events_ptr;
    ++*(tcxt->buffer.events_used);
}


////////////////////////////////////////////
// PTHREAD MUTEX UNLOCK
////////////////////////////////////////////
static void
wrap_pre_pthread_mutex_unlock(void *wrapcxt, OUT void **user_data)
{
    void *drcontext  = dr_get_current_drcontext();
    per_thread_t *tcxt = drmgr_get_tls_field(drcontext, tls_idx);
    tcxt->active = false;
}
static void
wrap_post_pthread_mutex_unlock(void *wrapcxt, void *user_data)
{
    void *drcontext  = dr_get_current_drcontext();
    per_thread_t *tcxt = drmgr_get_tls_field(drcontext, tls_idx);
    tcxt->active = true;

    if(tcxt->buffer.events_ptr == tcxt->buffer.events_end)
        set_shared_memory_buffer(tcxt);

    tcxt->buffer.events_ptr->tag       = SGL_SYNC_TAG;
    tcxt->buffer.events_ptr->sync.type = SGLPRIM_SYNC_UNLOCK;

    ++tcxt->buffer.events_ptr;
    ++*(tcxt->buffer.events_used);
}


////////////////////////////////////////////
// PTHREAD BARRIER
////////////////////////////////////////////
static void
wrap_pre_pthread_barrier(void *wrapcxt, OUT void **user_data)
{
    dr_printf("entering pthread_barrier\n");
    //size_t sz = (size_t) drwrap_get_arg(wrapcxt, IF_WINDOWS_ELSE(2,0));
}
static void
wrap_post_pthread_barrier(void *wrapcxt, void *user_data)
{
    dr_printf("exiting pthread_barrier\n");
}


////////////////////////////////////////////
// PTHREAD CONDITIONAL WAIT
////////////////////////////////////////////
static void
wrap_pre_pthread_cond_wait(void *wrapcxt, OUT void **user_data)
{
    dr_printf("entering pthread_cond_wait\n");
    //size_t sz = (size_t) drwrap_get_arg(wrapcxt, IF_WINDOWS_ELSE(2,0));
}
static void
wrap_post_pthread_cond_wait(void *wrapcxt, void *user_data)
{
    dr_printf("exiting pthread_cond_wait\n");
}


////////////////////////////////////////////
// PTHREAD CONDITIONAL SIGNAL
////////////////////////////////////////////
static void
wrap_pre_pthread_cond_sig(void *wrapcxt, OUT void **user_data)
{
    dr_printf("entering pthread_cond_sig\n");
    //size_t sz = (size_t) drwrap_get_arg(wrapcxt, IF_WINDOWS_ELSE(2,0));
}
static void
wrap_post_pthread_cond_sig(void *wrapcxt, void *user_data)
{
    dr_printf("exiting pthread_cond_sig\n");
}


////////////////////////////////////////////
// PTHREAD SPIN LOCK
////////////////////////////////////////////
static void
wrap_pre_pthread_spin_lock(void *wrapcxt, OUT void **user_data)
{
    dr_printf("entering pthread_spin_lock\n");
    //size_t sz = (size_t) drwrap_get_arg(wrapcxt, IF_WINDOWS_ELSE(2,0));
}
static void
wrap_post_pthread_spin_lock(void *wrapcxt, void *user_data)
{
    dr_printf("exiting pthread_spin_lock\n");
}


////////////////////////////////////////////
// PTHREAD SPIN UNLOCK
////////////////////////////////////////////
static void
wrap_pre_pthread_spin_unlock(void *wrapcxt, OUT void **user_data)
{
    dr_printf("entering pthread_spin_unlock\n");
    //size_t sz = (size_t) drwrap_get_arg(wrapcxt, IF_WINDOWS_ELSE(2,0));
}
static void
wrap_post_pthread_spin_unlock(void *wrapcxt, void *user_data)
{
    dr_printf("exiting pthread_spin_unlock\n");
}

#endif
