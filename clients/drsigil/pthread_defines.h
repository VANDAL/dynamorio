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
    per_thread_t *data = drmgr_get_tls_field(drcontext, tls_idx);
    dr_printf("entering pthread_create: %d\n", data->thread_id);
    data->active = false;
}
static void
wrap_post_pthread_create(void *wrapcxt, void *user_data)
{
    void *drcontext  = dr_get_current_drcontext();
    per_thread_t *data = drmgr_get_tls_field(drcontext, tls_idx);
    dr_printf("exiting pthread_create: %d\n", data->thread_id);
    data->active = true;

    data->buffer.events_ptr->tag = SGL_SYNC_TAG;
    data->buffer.events_ptr->sync.type = SGLPRIM_SYNC_CREATE;
    data->buffer.events_ptr++;
    if((ptr_int_t)data->buffer.events_ptr + data->buffer.events_end == 0)
    {
        flush(data);
    }
}


////////////////////////////////////////////
// PTHREAD JOIN
////////////////////////////////////////////
static void
wrap_pre_pthread_join(void *wrapcxt, OUT void **user_data)
{
    void *drcontext  = dr_get_current_drcontext();
    per_thread_t *data = drmgr_get_tls_field(drcontext, tls_idx);
    dr_printf("entering pthread_join: %d\n", data->thread_id);
    data->active = false;

    data->buffer.events_ptr->tag       = SGL_SYNC_TAG;
    data->buffer.events_ptr->sync.type = SGLPRIM_SYNC_JOIN;
    data->buffer.events_ptr++;

    if((ptr_int_t)data->buffer.events_ptr + data->buffer.events_end == 0)
        flush(data);
}
static void
wrap_post_pthread_join(void *wrapcxt, void *user_data)
{
    void *drcontext  = dr_get_current_drcontext();
    per_thread_t *data = drmgr_get_tls_field(drcontext, tls_idx);
    dr_printf("exiting pthread_join: %d\n", data->thread_id);
    data->active = true;
}


////////////////////////////////////////////
// PTHREAD MUTEX LOCK
////////////////////////////////////////////
static void
wrap_pre_pthread_mutex_lock(void *wrapcxt, OUT void **user_data)
{
    void *drcontext  = dr_get_current_drcontext();
    per_thread_t *data = drmgr_get_tls_field(drcontext, tls_idx);
    dr_printf("entering pthread_mutex_lock: %d\n", data->thread_id);
    data->active = false;
}
static void
wrap_post_pthread_mutex_lock(void *wrapcxt, void *user_data)
{
    void *drcontext  = dr_get_current_drcontext();
    per_thread_t *data = drmgr_get_tls_field(drcontext, tls_idx);
    dr_printf("exiting pthread_mutex_lock: %d\n", data->thread_id);
    data->active = true;

    data->buffer.events_ptr->tag       = SGL_SYNC_TAG;
    data->buffer.events_ptr->sync.type = SGLPRIM_SYNC_LOCK;
    data->buffer.events_ptr++;

    if((ptr_int_t)data->buffer.events_ptr + data->buffer.events_end == 0)
        flush(data);
}


////////////////////////////////////////////
// PTHREAD MUTEX UNLOCK
////////////////////////////////////////////
static void
wrap_pre_pthread_mutex_unlock(void *wrapcxt, OUT void **user_data)
{
    void *drcontext  = dr_get_current_drcontext();
    per_thread_t *data = drmgr_get_tls_field(drcontext, tls_idx);
    dr_printf("entering pthread_mutex_unlock: %d\n", data->thread_id);
    data->active = false;
}
static void
wrap_post_pthread_mutex_unlock(void *wrapcxt, void *user_data)
{
    void *drcontext  = dr_get_current_drcontext();
    per_thread_t *data = drmgr_get_tls_field(drcontext, tls_idx);
    dr_printf("exiting pthread_mutex_unlock: %d\n", data->thread_id);
    data->active = true;

    data->buffer.events_ptr->tag       = SGL_SYNC_TAG;
    data->buffer.events_ptr->sync.type = SGLPRIM_SYNC_UNLOCK;
    data->buffer.events_ptr++;

    if((ptr_int_t)data->buffer.events_ptr + data->buffer.events_end == 0)
        flush(data);
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
