#ifndef START_STOP_H
#define START_STOP_H

#include "drsigil.h"
#include "drmgr.h"


static void
wrap_pre_start_at_main()
{
    void *drcontext  = dr_get_current_drcontext();
    per_thread_t *data = drmgr_get_tls_field(drcontext, tls_idx);
    data->active = true;
}
static void
wrap_post_start_at_main()
{
    void *drcontext  = dr_get_current_drcontext();
    per_thread_t *data = drmgr_get_tls_field(drcontext, tls_idx);
    data->active = false;
}

////////////////////////////////////////////
// START COLLECTING EVENTS
////////////////////////////////////////////
static void
wrap_pre_start_func(void *wrapcxt, OUT void **user_data)
{
    roi = true;
}
static void
wrap_post_start_func(void *wrapcxt, void *user_data)
{
}


////////////////////////////////////////////
// STOP COLLECTING EVENTS
////////////////////////////////////////////
static void
wrap_pre_stop_func(void *wrapcxt, OUT void **user_data)
{
}
static void
wrap_post_stop_func(void *wrapcxt, void *user_data)
{
    roi = false;
}

#endif
