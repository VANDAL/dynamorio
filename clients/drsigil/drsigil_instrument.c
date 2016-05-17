/*
 * Much of this implementation is based off
 * the 'memtrace_x86.c' dynamorio api example
 */

#include <stddef.h> /* for offsetof */

#include "drsigil.h"

#include "drmgr.h"
#include "drutil.h"

/* Convenience function for Sigil2 instrumentation.
 * Saves off reg1 and reg2, while placing the TLS pointer in reg2
 *
 * reg1 SHOULD NOT be modified between invocations of this function
 * and the corresponding teardown function. If it is modified it will
 * need to be manually restored.
 */
static void
setup_instrument(void *drcontext, instrlist_t *ilist, instr_t *where,
				 reg_id_t reg1, reg_id_t reg2, reg_id_t spill1, reg_id_t spill2)
{
    instr_t *instr;
    opnd_t   opnd1, opnd2;

    /* Steal the register for memory reference address *
     * We can optimize away the unnecessary register save and restore
     * by analyzing the code and finding the register is dead.
     */
    dr_save_reg(drcontext, ilist, where, reg1, spill1);
    dr_save_reg(drcontext, ilist, where, reg2, spill2);

    drmgr_insert_read_tls_field(drcontext, tls_idx, ilist, where, reg2);

    /* Load data->buf_ptr into reg2 */
    opnd1 = opnd_create_reg(reg2);
    opnd2 = OPND_CREATE_MEMPTR(reg2, offsetof(per_thread_t, buf_ptr));
    instr = INSTR_CREATE_mov_ld(drcontext, opnd1, opnd2);
    instrlist_meta_preinsert(ilist, where, instr);
}

/* Convenience function for Sigil2 instrumentation.
 * Increments to next event buffer and flushes with a clean call if full */
static void
teardown_instrument(void *drcontext, instrlist_t *ilist, instr_t *where,
				    reg_id_t reg1, reg_id_t reg2, reg_id_t spill1, reg_id_t spill2)
{
    instr_t *instr, *call, *restore;
    opnd_t   opnd1, opnd2;

    /* The following assembly performs the following instructions
     * data->buf_ptr++;
     * data->used++;
     * if (buf_ptr >= buf_end_ptr)
     *    clean_call();
     */

    /* Increment reg value by pointer size using lea instr */
    opnd1 = opnd_create_reg(reg2);
    opnd2 = opnd_create_base_disp(reg2, DR_REG_NULL, 0, sizeof(BufferedSglEv), OPSZ_lea);
    instr = INSTR_CREATE_lea(drcontext, opnd1, opnd2);
    instrlist_meta_preinsert(ilist, where, instr);

    /* Update the data->buf_ptr */
    drmgr_insert_read_tls_field(drcontext, tls_idx, ilist, where, reg1);
    opnd1 = OPND_CREATE_MEMPTR(reg1, offsetof(per_thread_t, buf_ptr));
    opnd2 = opnd_create_reg(reg2);
    instr = INSTR_CREATE_mov_st(drcontext, opnd1, opnd2);
    instrlist_meta_preinsert(ilist, where, instr);

    /* we use lea + jecxz trick for better performance
     * lea and jecxz won't disturb the eflags, so we won't insert
     * code to save and restore application's eflags.
     */
    /* lea [reg2 - buf_end] => reg2 */
    opnd1 = opnd_create_reg(reg1);
    opnd2 = OPND_CREATE_MEMPTR(reg1, offsetof(per_thread_t, buf_end));
    instr = INSTR_CREATE_mov_ld(drcontext, opnd1, opnd2);
    instrlist_meta_preinsert(ilist, where, instr);
    opnd1 = opnd_create_reg(reg2);
    opnd2 = opnd_create_base_disp(reg1, reg2, 1, 0, OPSZ_lea);
    instr = INSTR_CREATE_lea(drcontext, opnd1, opnd2);
    instrlist_meta_preinsert(ilist, where, instr);

    /* jecxz call */
    call  = INSTR_CREATE_label(drcontext);
    opnd1 = opnd_create_instr(call);
    instr = INSTR_CREATE_jecxz(drcontext, opnd1);
    instrlist_meta_preinsert(ilist, where, instr);

    /* jump restore to skip clean call */
    restore = INSTR_CREATE_label(drcontext);
    opnd1 = opnd_create_instr(restore);
    instr = INSTR_CREATE_jmp(drcontext, opnd1);
    instrlist_meta_preinsert(ilist, where, instr);

    /* clean call */
    /* We jump to lean procedure which performs full context switch and
     * clean call invocation. This is to reduce the code cache size.
     */
    instrlist_meta_preinsert(ilist, where, call);
    /* mov restore DR_REG_XCX */
    opnd1 = opnd_create_reg(reg2);
    /* this is the return address for jumping back from lean procedure */
    opnd2 = opnd_create_instr(restore);
    /* We could use instrlist_insert_mov_instr_addr(), but with a register
     * destination we know we can use a 64-bit immediate.
     */
    instr = INSTR_CREATE_mov_imm(drcontext, opnd1, opnd2);
    instrlist_meta_preinsert(ilist, where, instr);
    /* jmp code_cache */
    opnd1 = opnd_create_pc(code_cache);
    instr = INSTR_CREATE_jmp(drcontext, opnd1);
    instrlist_meta_preinsert(ilist, where, instr);

    /* restore %reg */
    instrlist_meta_preinsert(ilist, where, restore);
    dr_restore_reg(drcontext, ilist, where, reg1, spill1);
    dr_restore_reg(drcontext, ilist, where, reg2, spill2);
}


/*
 * Appends MEMORY EVENT to dynamorio per-thread sigil event buffer,
 * and flushes the buffer to shared memory if full
 */
void
instrument_mem(void *drcontext, instrlist_t *ilist, instr_t *where, int pos, MemType type)
{
	HAVING PROBLEMS WITH EFLAGS! 

    instr_t *instr, *call, *restore;
    opnd_t   ref, opnd1, opnd2;
    reg_id_t reg1 = DR_REG_XBX; /* We can optimize it by picking dead reg */
    reg_id_t reg2    = DR_REG_XCX; /* reg2 must be ECX or RCX for jecxz */
    //reg_id_t reg2_8  = DR_REG_CL; /* reg2 must be ECX or RCX for jecxz */
	reg_id_t spill1 = SPILL_SLOT_2;
	reg_id_t spill2 = SPILL_SLOT_3;

    /* setup assembly labels */
    call  = INSTR_CREATE_label(drcontext);
    restore = INSTR_CREATE_label(drcontext);

    /* Steal the register for memory reference address *
     * We can optimize away the unnecessary register save and restore
     * by analyzing the code and finding the register is dead.
     */
    dr_save_reg(drcontext, ilist, where, reg1, SPILL_SLOT_2);
    dr_save_reg(drcontext, ilist, where, reg2, SPILL_SLOT_3);
    instr = INSTR_CREATE_pushf(drcontext);
    instrlist_meta_preinsert(ilist, where, instr);

    ///* Load data->active into reg2 */
    ///* Load tls data into reg1 */
    //drmgr_insert_read_tls_field(drcontext, tls_idx, ilist, where, reg1);
    ///* Load data->active into reg2 */
    //opnd1 = opnd_create_reg(reg2_8);
    //opnd2 = OPND_CREATE_INT8(0);
    //instr = INSTR_CREATE_mov_imm(drcontext, opnd1, opnd2);
    ////opnd2 = OPND_CREATE_MEM8(reg1, offsetof(per_thread_t, active));
    ////instr = INSTR_CREATE_mov_ld(drcontext, opnd1, opnd2);
    //instrlist_meta_preinsert(ilist, where, instr);
    ///* check if '0' (false) */
    //opnd1 = opnd_create_reg(reg2_8);
    //opnd2 = OPND_CREATE_INT8(0);
    //instr = INSTR_CREATE_cmp(drcontext, opnd1, opnd2);
    //instrlist_meta_preinsert(ilist, where, instr);
    ///* exit instrumentation if currently inactive */
    //opnd1 = opnd_create_instr(restore);
    //instr = INSTR_CREATE_jcc(drcontext, OP_je, opnd1);
    //instrlist_meta_preinsert(ilist, where, instr);

    ///* restore %reg */
    //dr_restore_reg(drcontext, ilist, where, reg1, spill1);
    //dr_restore_reg(drcontext, ilist, where, reg2, spill2);
    ///* TODO is this necessary? */
    //dr_save_reg(drcontext, ilist, where, reg1, SPILL_SLOT_2);
    //dr_save_reg(drcontext, ilist, where, reg2, SPILL_SLOT_3);

    if (type == SGLPRIM_MEM_STORE)
       ref = instr_get_dst(where, pos);
    else if (type == SGLPRIM_MEM_LOAD)
       ref = instr_get_src(where, pos);

    /* use drutil to get mem address
     *
     * NOTE: all registers used in 'ref'  must hold the original 
     * application values, so compute the address before setting up 
     * buf_ptr in either of the used registers */
    drutil_insert_get_mem_addr(drcontext, ilist, where, ref, reg1, reg2);

    /* Load data->buf_ptr into reg2 */
    drmgr_insert_read_tls_field(drcontext, tls_idx, ilist, where, reg2);
    opnd1 = opnd_create_reg(reg2);
    opnd2 = OPND_CREATE_MEMPTR(reg2, offsetof(per_thread_t, buf_ptr));
    instr = INSTR_CREATE_mov_ld(drcontext, opnd1, opnd2);
    instrlist_meta_preinsert(ilist, where, instr);

    /* The following assembly performs the following instructions
     * data->buf_ptr->tag            = SGL_MEM_TAG;
     * data->buf_ptr->mem.type       = type;
     * data->buf_ptr->mem.begin_addr = addr;
     * data->buf_ptr->mem.size       = size;
     */

    /* Set BufferedSglEv->tag */
    opnd1 = OPND_CREATE_MEM32(reg2, offsetof(BufferedSglEv, tag));
    opnd2 = OPND_CREATE_INT32(SGL_MEM_TAG);
    instr = INSTR_CREATE_mov_imm(drcontext, opnd1, opnd2);
    instrlist_meta_preinsert(ilist, where, instr);

    /* Set BufferedSglEv->mem.type */
    opnd1 = OPND_CREATE_MEM32(reg2, offsetof(BufferedSglEv, mem.type));
    opnd2 = OPND_CREATE_INT32(type);
    instr = INSTR_CREATE_mov_imm(drcontext, opnd1, opnd2);
    instrlist_meta_preinsert(ilist, where, instr);

    /* Set BufferedSglEv->mem.begin_addr */
    opnd1 = OPND_CREATE_MEMPTR(reg2, offsetof(BufferedSglEv, mem.begin_addr));
    opnd2 = opnd_create_reg(reg1);
    instr = INSTR_CREATE_mov_st(drcontext, opnd1, opnd2);
    instrlist_meta_preinsert(ilist, where, instr);

    /* Set BufferedSglEv->mem.size */
    opnd1 = OPND_CREATE_MEM32(reg2, offsetof(BufferedSglEv, mem.size)); /* drutil_opnd_mem_size_in_bytes handles OP_enter */ opnd2 = OPND_CREATE_INT32(drutil_opnd_mem_size_in_bytes(ref, where)); instr = INSTR_CREATE_mov_st(drcontext, opnd1, opnd2);
    instrlist_meta_preinsert(ilist, where, instr);

    /* Increment reg value by pointer size using lea instr */
    opnd1 = opnd_create_reg(reg2);
    opnd2 = opnd_create_base_disp(reg2, DR_REG_NULL, 0, sizeof(BufferedSglEv), OPSZ_lea);
    instr = INSTR_CREATE_lea(drcontext, opnd1, opnd2);
    instrlist_meta_preinsert(ilist, where, instr);

    /* Update the data->buf_ptr */
    drmgr_insert_read_tls_field(drcontext, tls_idx, ilist, where, reg1);
    opnd1 = OPND_CREATE_MEMPTR(reg1, offsetof(per_thread_t, buf_ptr));
    opnd2 = opnd_create_reg(reg2);
    instr = INSTR_CREATE_mov_st(drcontext, opnd1, opnd2);
    instrlist_meta_preinsert(ilist, where, instr);

    /* we use lea + jecxz trick for better performance
     * lea and jecxz won't disturb the eflags, so we won't insert
     * code to save and restore application's eflags.
     */
    /* lea [reg2 - buf_end] => reg2 */
    opnd1 = opnd_create_reg(reg1);
    opnd2 = OPND_CREATE_MEMPTR(reg1, offsetof(per_thread_t, buf_end));
    instr = INSTR_CREATE_mov_ld(drcontext, opnd1, opnd2);
    instrlist_meta_preinsert(ilist, where, instr);
    opnd1 = opnd_create_reg(reg2);
    opnd2 = opnd_create_base_disp(reg1, reg2, 1, 0, OPSZ_lea);
    instr = INSTR_CREATE_lea(drcontext, opnd1, opnd2);
    instrlist_meta_preinsert(ilist, where, instr);

    /* jecxz call */
    opnd1 = opnd_create_instr(call);
    instr = INSTR_CREATE_jecxz(drcontext, opnd1);
    instrlist_meta_preinsert(ilist, where, instr);

    /* jump restore to skip clean call */
    opnd1 = opnd_create_instr(restore);
    instr = INSTR_CREATE_jmp(drcontext, opnd1);
    instrlist_meta_preinsert(ilist, where, instr);

    /* clean call */
    /* We jump to lean procedure which performs full context switch and
     * clean call invocation. This is to reduce the code cache size.
     */
    instrlist_meta_preinsert(ilist, where, call);
    /* mov restore DR_REG_XCX */
    opnd1 = opnd_create_reg(reg2);
    /* this is the return address for jumping back from lean procedure */
    opnd2 = opnd_create_instr(restore);
    /* We could use instrlist_insert_mov_instr_addr(), but with a register
     * destination we know we can use a 64-bit immediate.
     */
    instr = INSTR_CREATE_mov_imm(drcontext, opnd1, opnd2);
    instrlist_meta_preinsert(ilist, where, instr);
    /* jmp code_cache */
    opnd1 = opnd_create_pc(code_cache);
    instr = INSTR_CREATE_jmp(drcontext, opnd1);
    instrlist_meta_preinsert(ilist, where, instr);

    /* restore %reg */
    instrlist_meta_preinsert(ilist, where, restore);
    instr = INSTR_CREATE_popf(drcontext);
    instrlist_meta_preinsert(ilist, where, instr);
    dr_restore_reg(drcontext, ilist, where, reg1, spill1);
    dr_restore_reg(drcontext, ilist, where, reg2, spill2);
}

/*
 * Appends INSTRUCTION to dynamorio per-thread sigil event buffer,
 * and flushes the buffer to shared memory if full
 */
void
instrument_instr(void *drcontext, instrlist_t *ilist, instr_t *where)
{
    instr_t *instr, *first, *second;
    opnd_t   opnd1, opnd2;
    reg_id_t reg1 = DR_REG_XBX; /* We can optimize it by picking dead reg */
    reg_id_t reg2 = DR_REG_XCX; /* reg2 must be ECX or RCX for jecxz */
	reg_id_t spill1 = SPILL_SLOT_2;
	reg_id_t spill2 = SPILL_SLOT_3;
    app_pc pc;

	setup_instrument(drcontext, ilist, where, reg1, reg2, spill1, spill2);

    /* The following assembly performs the following instructions
     * data->buf_ptr->tag            = SGL_CXT_TAG;
     * data->buf_ptr->cxt.type       = type;
     * data->buf_ptr->cxt.id         = instruction addr;
     */

    /* Set BufferedSglEv->tag */
    opnd1 = OPND_CREATE_MEM32(reg2, offsetof(BufferedSglEv, tag));
    opnd2 = OPND_CREATE_INT32(SGL_CXT_TAG);
    instr = INSTR_CREATE_mov_imm(drcontext, opnd1, opnd2);
    instrlist_meta_preinsert(ilist, where, instr);

    /* Set BufferedSglEv->cxt.type */
    opnd1 = OPND_CREATE_MEM32(reg2, offsetof(BufferedSglEv, cxt.type));
    opnd2 = OPND_CREATE_INT32(SGLPRIM_CXT_INSTR);
    instr = INSTR_CREATE_mov_imm(drcontext, opnd1, opnd2);
    instrlist_meta_preinsert(ilist, where, instr);

    /* Set BufferedSglEv->cxt.id */
    /* Store pc */
    pc = instr_get_app_pc(where);
    /* For 64-bit, we can't use a 64-bit immediate so we split pc into two halves.
     * We could alternatively load it into reg1 and then store reg1.
     * We use a convenience routine that does the two-step store for us.
     */
    opnd1 = OPND_CREATE_MEMPTR(reg2, offsetof(BufferedSglEv, cxt.id));
    instrlist_insert_mov_immed_ptrsz(drcontext, (ptr_int_t) pc, opnd1,
                                     ilist, where, &first, &second);
    instr_set_meta(first);
    if (second != NULL)
        instr_set_meta(second);

	teardown_instrument(drcontext, ilist, where, reg1, reg2, spill1, spill2);
}


void
instrument_flop(void *drcontext, instrlist_t *ilist, instr_t *where)
{
    instr_t *instr;
    opnd_t   opnd1, opnd2;
    reg_id_t reg1 = DR_REG_XBX; /* We can optimize it by picking dead reg */
    reg_id_t reg2 = DR_REG_XCX; /* reg2 must be ECX or RCX for jecxz */
	reg_id_t spill1 = SPILL_SLOT_2;
	reg_id_t spill2 = SPILL_SLOT_3;
	setup_instrument(drcontext, ilist, where, reg1, reg2, spill1, spill2);

    /* The following assembly performs the following instructions
     * data->buf_ptr->tag            = SGL_COMP_TAG;
     * data->buf_ptr->comp.type      = SGLPRIM_COMP_FLOP;
     * data->buf_ptr->comp.arity     = TODO
     * data->buf_ptr->comp.op        = TODO
     * data->buf_ptr->comp.size      = TODO
     */

    /* Set BufferedSglEv->tag */
    opnd1 = OPND_CREATE_MEM32(reg2, offsetof(BufferedSglEv, tag));
    opnd2 = OPND_CREATE_INT32(SGL_COMP_TAG);
    instr = INSTR_CREATE_mov_imm(drcontext, opnd1, opnd2);
    instrlist_meta_preinsert(ilist, where, instr);

    /* Set BufferedSglEv->cxt.type */
    opnd1 = OPND_CREATE_MEM32(reg2, offsetof(BufferedSglEv, comp.type));
    opnd2 = OPND_CREATE_INT32(SGLPRIM_COMP_FLOP);
    instr = INSTR_CREATE_mov_imm(drcontext, opnd1, opnd2);
    instrlist_meta_preinsert(ilist, where, instr);

	teardown_instrument(drcontext, ilist, where, reg1, reg2, spill1, spill2);
}


void
instrument_iop(void *drcontext, instrlist_t *ilist, instr_t *where)
{
    instr_t *instr;
    opnd_t   opnd1, opnd2;
    reg_id_t reg1 = DR_REG_XBX; /* We can optimize it by picking dead reg */
    reg_id_t reg2 = DR_REG_XCX; /* reg2 must be ECX or RCX for jecxz */
	reg_id_t spill1 = SPILL_SLOT_2;
	reg_id_t spill2 = SPILL_SLOT_3;
	setup_instrument(drcontext, ilist, where, reg1, reg2, spill1, spill2);

    /* The following assembly performs the following instructions
     * data->buf_ptr->tag            = SGL_COMP_TAG;
     * data->buf_ptr->comp.type      = SGLPRIM_COMP_IOP;
     * data->buf_ptr->comp.arity     = TODO
     * data->buf_ptr->comp.op        = TODO
     * data->buf_ptr->comp.size      = TODO
     */

    /* Set BufferedSglEv->tag */
    opnd1 = OPND_CREATE_MEM32(reg2, offsetof(BufferedSglEv, tag));
    opnd2 = OPND_CREATE_INT32(SGL_COMP_TAG);
    instr = INSTR_CREATE_mov_imm(drcontext, opnd1, opnd2);
    instrlist_meta_preinsert(ilist, where, instr);

    /* Set BufferedSglEv->cxt.type */
    opnd1 = OPND_CREATE_MEM32(reg2, offsetof(BufferedSglEv, comp.type));
    opnd2 = OPND_CREATE_INT32(SGLPRIM_COMP_IOP);
    instr = INSTR_CREATE_mov_imm(drcontext, opnd1, opnd2);
    instrlist_meta_preinsert(ilist, where, instr);

	teardown_instrument(drcontext, ilist, where, reg1, reg2, spill1, spill2);
}
