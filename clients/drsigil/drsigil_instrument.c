/*
 * Much of this implementation is based off
 * the 'memtrace_x86.c' dynamorio api example
 */

#include <stddef.h> /* for offsetof */

#include "drsigil.h"

#include "drmgr.h"
#include "drutil.h"



/* Appends MEMORY EVENT to dynamorio per-thread sigil event buffer,
 * and flushes the buffer to shared memory if full */
static void
clean_call_mem(per_thread_t *this_thread, ptr_uint_t address, int size, int type)
{
    if(this_thread->active == true && roi == true)
    {
        BufferedSglEv *event_slot = this_thread->buffer.events_ptr;
        event_slot->tag = SGL_MEM_TAG;
        event_slot->mem.type = type;
        event_slot->mem.begin_addr = address;
        event_slot->mem.size = size;

        if(++this_thread->buffer.events_ptr == this_thread->buffer.events_end)
            flush(this_thread);
    }
}
void
instrument_mem(void *drcontext, instrlist_t *ilist, instr_t *where, int pos, MemType type)
{
    opnd_t   ref;
    reg_id_t reg1 = DR_REG_XBX;
    reg_id_t reg2 = DR_REG_XCX;
    reg_id_t spill1 = SPILL_SLOT_2;
    reg_id_t spill2 = SPILL_SLOT_3;

    dr_save_reg(drcontext, ilist, where, reg1, SPILL_SLOT_2);
    dr_save_reg(drcontext, ilist, where, reg2, SPILL_SLOT_3);

    if (type == SGLPRIM_MEM_STORE)
       ref = instr_get_dst(where, pos);
    else if (type == SGLPRIM_MEM_LOAD)
       ref = instr_get_src(where, pos);

    drutil_insert_get_mem_addr(drcontext, ilist, where, ref, reg1, reg2);
    drmgr_insert_read_tls_field(drcontext, tls_idx, ilist, where, reg2);

    dr_insert_clean_call(drcontext, ilist, where,
                         (void *)clean_call_mem, false, 4,
                         opnd_create_reg(reg2),
                         opnd_create_reg(reg1),
                         OPND_CREATE_INT32(drutil_opnd_mem_size_in_bytes(ref, where)),
                         OPND_CREATE_INT32(type));

    /* restore %reg */
    dr_restore_reg(drcontext, ilist, where, reg1, spill1);
    dr_restore_reg(drcontext, ilist, where, reg2, spill2);
}


/* Appends INSTRUCTION to dynamorio per-thread sigil event buffer,
 * and flushes the buffer to shared memory if full */
static void
clean_call_instr(per_thread_t *this_thread, ptr_int_t pc)
{
    if(this_thread->active == true && roi == true)
    {
        BufferedSglEv *event_slot = this_thread->buffer.events_ptr;
        event_slot->tag = SGL_CXT_TAG;
        event_slot->cxt.type = SGLPRIM_CXT_INSTR;
        event_slot->cxt.id = pc;

        if(++this_thread->buffer.events_ptr == this_thread->buffer.events_end)
            flush(this_thread);
    }
}
void
instrument_instr(void *drcontext, instrlist_t *ilist, instr_t *where)
{
    instr_t *first, *second;
    opnd_t   opnd1;
    reg_id_t reg1 = DR_REG_XBX; /* We can optimize it by picking dead reg */
    reg_id_t reg2 = DR_REG_XCX; /* reg2 must be ECX or RCX for jecxz */
    reg_id_t spill1 = SPILL_SLOT_2;
    reg_id_t spill2 = SPILL_SLOT_3;
    app_pc pc;

    dr_save_reg(drcontext, ilist, where, reg1, SPILL_SLOT_2);
    dr_save_reg(drcontext, ilist, where, reg2, SPILL_SLOT_3);

    drmgr_insert_read_tls_field(drcontext, tls_idx, ilist, where, reg2);

    /* Store pc */
    pc = instr_get_app_pc(where);
    /* For 64-bit, we can't use a 64-bit immediate so we split pc into two halves.
     * We use a convenience routine that does the two-step store for us.
     */
    opnd1 = opnd_create_reg(reg1);
    instrlist_insert_mov_immed_ptrsz(drcontext, (ptr_int_t) pc, opnd1,
                                     ilist, where, &first, &second);
    instr_set_meta(first);
    if (second != NULL)
        instr_set_meta(second);

    dr_insert_clean_call(drcontext, ilist, where,
                         (void *)clean_call_instr, false, 2,
                         opnd_create_reg(reg2),
                         opnd_create_reg(reg1));

    /* restore %reg */
    dr_restore_reg(drcontext, ilist, where, reg1, spill1);
    dr_restore_reg(drcontext, ilist, where, reg2, spill2);
}


static void
clean_call_comp(per_thread_t *this_thread, CompCostType type)
{
    if(this_thread->active == true && roi == true)
    {
        BufferedSglEv *event_slot = this_thread->buffer.events_ptr;
        event_slot->tag = SGL_COMP_TAG;
        event_slot->comp.type = type;
        //comp.arity = TODO
        //comp.op = TODO
        //comp.size = TODO

        if(++this_thread->buffer.events_ptr == this_thread->buffer.events_end)
            flush(this_thread);
    }
}
void
instrument_comp(void *drcontext, instrlist_t *ilist, instr_t *where, CompCostType type)
{
    reg_id_t reg1 = DR_REG_XBX;
    reg_id_t spill1 = SPILL_SLOT_2;

    dr_save_reg(drcontext, ilist, where, reg1, SPILL_SLOT_2);

    drmgr_insert_read_tls_field(drcontext, tls_idx, ilist, where, reg1);
    dr_insert_clean_call(drcontext, ilist, where,
                         (void *)clean_call_comp, false, 2,
                         opnd_create_reg(reg1),
                         OPND_CREATE_INT32(type));

    /* restore %reg */
    dr_restore_reg(drcontext, ilist, where, reg1, spill1);
}
