#include "drsigil.h"
#include "drmgr.h"
#include "drutil.h"
#include "drreg.h"
#include <stddef.h> /* for offsetof */
#include <limits.h> /* for INT_MAX */

#define SIZEOF_EVENT_SLOT sizeof(SglEvVariant)

void
instrument_reset(void *drcontext, instrlist_t *ilist, instr_t *where,
                 per_thread_t *tcxt)
{
    /* Reset event block data */
    tcxt->iblock_count = 0;
    tcxt->event_block_events = 0;
    tcxt->current_iblock_comp = tcxt->comps;

    /* reset mem ref buffer to point to the beginning */
    reg_id_t reg;
    RESERVE_REGISTER(reg);
    dr_insert_read_raw_tls(drcontext, ilist, where,
                           raw_tls_seg, MEMREFBASE_OFFS, reg);
    dr_insert_write_raw_tls(drcontext, ilist, where,
                            raw_tls_seg, MEMREFCURR_OFFS, reg);
    UNRESERVE_REGISTER(reg);
}


static void
instrument_mem_cache_helper(void *drcontext, instrlist_t *ilist, instr_t *where,
                            reg_id_t buf_ptr, reg_id_t addr_reg, reg_id_t scratch_reg,
                            ushort size, MemType type, opnd_t ref)
{
    /* set the attributes */

    /* READ/WRITE */
    MINSERT(ilist, where,
            XINST_CREATE_store_1byte(drcontext,
                                     OPND_CREATE_MEM8(buf_ptr, offsetof(mem_ref_t, type)),
                                     OPND_CREATE_INT8(type)));

    /* size of the mem ref operand */
    MINSERT(ilist, where,
            XINST_CREATE_store_2bytes(drcontext,
                                      OPND_CREATE_MEM16(buf_ptr, offsetof(mem_ref_t, size)),
                                      OPND_CREATE_INT16(size)));

    /* memory address */
    drutil_insert_get_mem_addr(drcontext, ilist, where, ref, addr_reg, scratch_reg);
    MINSERT(ilist, where,
            XINST_CREATE_store(drcontext,
                               OPND_CREATE_MEMPTR(buf_ptr, offsetof(mem_ref_t, addr)),
                               opnd_create_reg(addr_reg)));
}
void
instrument_mem_cache(void *drcontext, instrlist_t *ilist, instr_t *where,
                     uint *mem_ref_count)
{
    reg_id_t buf_ptr, scratch1, scratch2;
    drreg_reserve_aflags(drcontext, ilist, where);
    RESERVE_REGISTER(buf_ptr);
    RESERVE_REGISTER(scratch1);
    RESERVE_REGISTER(scratch2);

    /* current mem ref buffer pointer */
    dr_insert_read_raw_tls(drcontext, ilist, where,
                           raw_tls_seg, MEMREFCURR_OFFS, buf_ptr);

    if (instr_reads_memory(where))
    {
        for (int i=0; i<instr_num_srcs(where); ++i)
        {
            opnd_t ref = instr_get_src(where, i);
            if (opnd_is_memory_reference(ref))
            {
                instrument_mem_cache_helper(drcontext, ilist, where,
                                            buf_ptr, scratch1, scratch2,
                                            drutil_opnd_mem_size_in_bytes(ref, where),
                                            SGLPRIM_MEM_LOAD, ref);

                /* update count and buffer */
                ++*mem_ref_count;
                MINSERT(ilist, where,
                        XINST_CREATE_add(drcontext,
                                         opnd_create_reg(buf_ptr),
                                         OPND_CREATE_INT32(sizeof(mem_ref_t))));
            }
        }
    }

    if (instr_writes_memory(where))
    {
        for (int i=0; i<instr_num_dsts(where); ++i)
        {
            opnd_t ref = instr_get_dst(where, i);
            if (opnd_is_memory_reference(ref))
            {

                instrument_mem_cache_helper(drcontext, ilist, where,
                                            buf_ptr, scratch1, scratch2,
                                            drutil_opnd_mem_size_in_bytes(ref, where),
                                            SGLPRIM_MEM_STORE, ref);

                /* update count and buffer */
                ++*mem_ref_count;
                MINSERT(ilist, where,
                        XINST_CREATE_add(drcontext,
                                         opnd_create_reg(buf_ptr),
                                         OPND_CREATE_INT32(sizeof(mem_ref_t))));
            }
        }
    }

    /* update the TLS mem ref pointer
     * so new mem refs will be in the right place */
    dr_insert_write_raw_tls(drcontext, ilist, where,
                            raw_tls_seg, MEMREFCURR_OFFS, buf_ptr);

    UNRESERVE_REGISTER(scratch2);
    UNRESERVE_REGISTER(scratch1);
    UNRESERVE_REGISTER(buf_ptr);
    drreg_unreserve_aflags(drcontext, ilist, where);
}


void
instrument_comp_cache(instr_t *instr, uint *comp_count, SglCompEv *cache)
{
    /* TODO(soon) review these conditions */
    dr_fp_type_t fp_t;
    if(instr_is_floating_ex(instr, &fp_t) && (fp_t == DR_FP_MATH))
    {
        cache->type = SGLPRIM_COMP_FLOP;
        ++*comp_count;
    }
    else
    {
        /* Brute force checking of opcode */
        switch(instr_get_opcode(instr))
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
        case OP_xor:
        case OP_and:
        case OP_or:
        case OP_bt:
        case OP_bts:
        case OP_btr:
        case OP_aas:
            cache->type = SGLPRIM_COMP_IOP;
            ++*comp_count;
        default:
            break;
        }
    }
}

static void
setup_sgl_ev_buf_clean_call(void)
{
    set_shared_memory_buffer(drmgr_get_tls_field(dr_get_current_drcontext(), tls_idx));
}

static void
instrument_setup_buffer(void *drcontext, instrlist_t *ilist, instr_t *where,
                        reg_id_t evptr_reg, reg_id_t scratch1, reg_id_t scratch2,
                        uint size)
{
    /* Make sure enough space exists in the current shared memory event buffer */

    /* scratch1 = events_ptr + size */
    const size_t bytes = size * SIZEOF_EVENT_SLOT;
    DR_ASSERT(bytes < INT_MAX);
    MINSERT(ilist, where,
            XINST_CREATE_add_2src(drcontext,
                                  opnd_create_reg(scratch1),
                                  opnd_create_reg(evptr_reg),
                                  OPND_CREATE_INT32(bytes)));

    /* Load (TLS) per_thread_t->buffer.events_end into scratch2 */
    dr_insert_read_raw_tls(drcontext, ilist, where,
                           raw_tls_seg, SGLEND_OFFS, scratch2);

    /* if (events_ptr+size >= events_end)
     *     perform clean call to reserve space in event buffer */
    instr_t *end = INSTR_CREATE_label(drcontext);
    MINSERT(ilist, where,
            XINST_CREATE_cmp(drcontext,
                             opnd_create_reg(scratch1),
                             opnd_create_reg(scratch2)));
    MINSERT(ilist, where,
            INSTR_CREATE_jcc(drcontext,
                             OP_jb_short,
                             opnd_create_instr(end)));
    dr_insert_clean_call(drcontext, ilist, where,
                         (void *)setup_sgl_ev_buf_clean_call, false, 0);
    /* reload evptr_reg, since it's been updated by the clean call */
    dr_insert_read_raw_tls(drcontext, ilist, where,
                           raw_tls_seg, SGLEV_OFFS, evptr_reg);

    MINSERT(ilist, where, end);
    /* update the events_used (events_ptr is updated as events are filled in)
     * *(per_thread_t->buffer.events_used) += size */
    dr_insert_read_raw_tls(drcontext, ilist, where,
                           raw_tls_seg, SGLUSED_OFFS, scratch1);
    MINSERT(ilist, where,
            XINST_CREATE_add_s(drcontext,
                               OPND_CREATE_MEMPTR(scratch1, 0),
                               OPND_CREATE_INT32(size)));
}


static void
instrument_mem(void *drcontext, instrlist_t *ilist, instr_t *where,
               reg_id_t evptr_reg, reg_id_t memref_xax,
               reg_id_t scratch_reg, reg_id_t h_scratch_reg, reg_id_t l_scratch_reg,
               uint mem_used)
{
    for (uint i=0; i<mem_used; ++i)
    {
        /* Fill in event slots */
        /* Set the tag
         * ev->tag = SGL_MEM_TAG */
        MINSERT(ilist, where,
                XINST_CREATE_store_1byte(drcontext,
                                         OPND_CREATE_MEM8(evptr_reg,
                                                          offsetof(SglEvVariant, tag)),
                                         OPND_CREATE_INT8(SGL_MEM_TAG)));

        /* Set read/write
         * ev->mem.type = memref->type */
        MINSERT(ilist, where,
                XINST_CREATE_load_1byte_zext4(drcontext,
                                              opnd_create_reg(scratch_reg),
                                              OPND_CREATE_MEM8(memref_xax,
                                                               offsetof(mem_ref_t, type))));
        MINSERT(ilist, where,
                XINST_CREATE_store_1byte(drcontext,
                                         OPND_CREATE_MEM8(evptr_reg,
                                                          offsetof(SglEvVariant, mem) +
                                                          offsetof(SglMemEv, type)),
                                         opnd_create_reg(l_scratch_reg)));

        /* Set size
         * ev->mem.size = memref->size */
        MINSERT(ilist, where,
                XINST_CREATE_load_2bytes(drcontext,
                                         opnd_create_reg(h_scratch_reg),
                                         OPND_CREATE_MEM16(memref_xax,
                                                           offsetof(mem_ref_t, size))));
        MINSERT(ilist, where,
                XINST_CREATE_store_2bytes(drcontext,
                                          OPND_CREATE_MEM16(evptr_reg,
                                                            offsetof(SglEvVariant, mem) +
                                                            offsetof(SglMemEv, size)),
                                          opnd_create_reg(h_scratch_reg)));

        /* Set address
         * ev->mem.begin_addr = memref->addr */
        MINSERT(ilist, where,
                XINST_CREATE_load(drcontext,
                                  opnd_create_reg(scratch_reg),
                                  OPND_CREATE_MEMPTR(memref_xax,
                                                     offsetof(mem_ref_t, addr))));
        MINSERT(ilist, where,
                XINST_CREATE_store(drcontext,
                                   OPND_CREATE_MEMPTR(evptr_reg,
                                                      offsetof(SglEvVariant, mem) +
                                                      offsetof(SglMemEv, begin_addr)),
                                   opnd_create_reg(scratch_reg)));

        /* Increment to the next event slot
         * ev += 1 */
        MINSERT(ilist, where,
                XINST_CREATE_add(drcontext,
                                 opnd_create_reg(evptr_reg),
                                 OPND_CREATE_INT32(SIZEOF_EVENT_SLOT)));

        /* Increment to next mem ref
         * memref += 1 */
        MINSERT(ilist, where,
                XINST_CREATE_add(drcontext,
                                 opnd_create_reg(memref_xax),
                                 OPND_CREATE_INT32(sizeof(mem_ref_t))));
    }
}

static void
instrument_instr(void *drcontext, instrlist_t *ilist, instr_t *where,
                 reg_id_t evptr_reg, reg_id_t scratch1,
                 instr_t *instr)
{
    /* Store pc */
    app_pc pc = instr_get_app_pc(instr);

    /* For 64-bit, we can't use a 64-bit immediate so we split pc into two halves.
     * We use a convenience routine that does the two-step store for us. */
    opnd_t pc_opnd = opnd_create_reg(scratch1);
    instr_t *first, *second;
    instrlist_insert_mov_immed_ptrsz(drcontext, (ptr_int_t) pc, pc_opnd,
                                     ilist, where, &first, &second);
    instr_set_meta(first);
    if (second != NULL)
        instr_set_meta(second);

    /* set the event slot attributes */
    /* ev->tag = SGL_CXT_TAG */
    MINSERT(ilist, where,
            XINST_CREATE_store_1byte(drcontext,
                                     OPND_CREATE_MEM8(evptr_reg, offsetof(SglEvVariant, tag)),
                                     OPND_CREATE_INT8(SGL_CXT_TAG)));

    /* CXT.TYPE
     * ev->cxt.type = SGLPRIM_CXT_INSTR */
    DR_ASSERT(sizeof(CxtType) == 1);
    MINSERT(ilist, where,
            XINST_CREATE_store(drcontext,
                               OPND_CREATE_MEM8(evptr_reg,
                                                (offsetof(SglEvVariant, cxt) +
                                                 offsetof(SglCxtEv, type))),
                               OPND_CREATE_INT8(SGLPRIM_CXT_INSTR)));

    /* CXT.ID (instruction addr)
     * ev->cxt.id = pc */
    DR_ASSERT(sizeof(PtrVal) == 8);
    MINSERT(ilist, where,
            XINST_CREATE_store(drcontext,
                               OPND_CREATE_MEMPTR(evptr_reg,
                                                  (offsetof(SglEvVariant, cxt) +
                                                   offsetof(SglCxtEv, id))),
                               pc_opnd));

    /* increment the sigil event slot
     * ev += 1 */
    MINSERT(ilist, where,
            XINST_CREATE_add(drcontext,
                             opnd_create_reg(evptr_reg),
                             OPND_CREATE_INT32(SIZEOF_EVENT_SLOT)));
}


static void
instrument_comp(void *drcontext, instrlist_t *ilist, instr_t *where,
                reg_id_t evptr_reg, uint comp_used, SglCompEv **ev)
{
    for (uint i=0; i<comp_used; ++i)
    {
        /* Set the tag */
        /* TAG
         * ev->tag = SGL_COMP_TAG */
        MINSERT(ilist, where,
                XINST_CREATE_store_1byte(drcontext,
                                         OPND_CREATE_MEM8(evptr_reg, offsetof(SglEvVariant, tag)),
                                         OPND_CREATE_INT8(SGL_COMP_TAG)));

        /* COMP.COMPCOSTTYPE
         * ev->sync.type = compev->type */
        DR_ASSERT(sizeof(CompCostType) == 1);
        MINSERT(ilist, where,
                XINST_CREATE_store_1byte(drcontext,
                                         OPND_CREATE_MEM8(evptr_reg,
                                                          (offsetof(SglEvVariant, comp) +
                                                           offsetof(SglCompEv, type))),
                                         OPND_CREATE_INT8((*ev)->type)));

        /* Increment to the next sigil event slot
         * ev += 1 */
        MINSERT(ilist, where,
                XINST_CREATE_add(drcontext,
                                 opnd_create_reg(evptr_reg),
                                 OPND_CREATE_INT32(SIZEOF_EVENT_SLOT)));

        /* Increment to the next cached compute event */
        ++*ev;
    }
}

static void
instrument_sync(void *drcontext, instrlist_t *ilist, instr_t *where,
                reg_id_t evptr_reg, reg_id_t sglsyncptr_reg,
                reg_id_t scratch_reg, reg_id_t l_scratch_reg)
{
    /* TAG
     * ev->tag = SGL_SYNC_TAG */
    MINSERT(ilist, where,
            XINST_CREATE_store_1byte(drcontext,
                                     OPND_CREATE_MEM8(evptr_reg, offsetof(SglEvVariant, tag)),
                                     OPND_CREATE_INT8(SGL_SYNC_TAG)));

    DR_ASSERT(sizeof(SyncType) == 1);
    /* SYNC.SYNCTYPE
     * ev->sync.type = TLS syncev->type */
    MINSERT(ilist, where,
            XINST_CREATE_load_1byte_zext4(drcontext,
                                          opnd_create_reg(scratch_reg),
                                          OPND_CREATE_MEM8(sglsyncptr_reg,
                                                           offsetof(SglSyncEv, type))));
    MINSERT(ilist, where,
            XINST_CREATE_store_1byte(drcontext,
                                     OPND_CREATE_MEM8(evptr_reg,
                                                      (offsetof(SglEvVariant, sync) +
                                                       offsetof(SglSyncEv, type))),
                                     opnd_create_reg(l_scratch_reg)));

    /* SYNC.SYNCID[0]
     * ev->sync.data[0] = TLS syncev->data[0] */
    DR_ASSERT(sizeof(SyncID) == 8);
    MINSERT(ilist, where,
            XINST_CREATE_load(drcontext,
                              opnd_create_reg(scratch_reg),
                              OPND_CREATE_MEMPTR(sglsyncptr_reg,
                                                 offsetof(SglSyncEv, data))));
    MINSERT(ilist, where,
            XINST_CREATE_store(drcontext,
                               OPND_CREATE_MEMPTR(evptr_reg,
                                                  (offsetof(SglEvVariant, sync) +
                                                   offsetof(SglSyncEv, data))),
                               opnd_create_reg(scratch_reg)));
    /* SYNC.SYNCID[1]
     * ev->sync.data[1] = TLS syncev->data[1] */
    MINSERT(ilist, where,
            XINST_CREATE_load(drcontext,
                              opnd_create_reg(scratch_reg),
                              OPND_CREATE_MEMPTR(sglsyncptr_reg,
                                                 offsetof(SglSyncEv, data) +
                                                 1*sizeof(SyncID))));
    MINSERT(ilist, where,
            XINST_CREATE_store(drcontext,
                               OPND_CREATE_MEMPTR(evptr_reg,
                                                  (offsetof(SglEvVariant, sync) +
                                                   offsetof(SglSyncEv, data) +
                                                   1*sizeof(SyncID))),
                               opnd_create_reg(scratch_reg)));

    /* reset the sync event
     * TLS syncev = (SglSyncEv*)0 */
    MINSERT(ilist, where,
            XINST_CREATE_load_int(drcontext,
                                  opnd_create_reg(scratch_reg),
                                  OPND_CREATE_INTPTR(NULL)));
    dr_insert_write_raw_tls(drcontext, ilist, where,
                            raw_tls_seg, SGLSYNCEV_OFFS, scratch_reg);

    /* Increment to the next event slot
     * ev += 1 */
    MINSERT(ilist, where,
            XINST_CREATE_add(drcontext,
                             opnd_create_reg(evptr_reg),
                             OPND_CREATE_INT32(SIZEOF_EVENT_SLOT)));
}


void
instrument_sigil_events(void *drcontext, instrlist_t *ilist, instr_t *where,
                        per_thread_t *tcxt)
{

    /* need to specify so we can access 1/2 byte registers.
     * e.g. XAX so we can access AL / AX
     * cannot use drreg here (no API for this) */
    reg_id_t xax, xbx, xcx, evptr_reg;
    dr_spill_slot_t spill_xax, spill_xbx, spill_xcx, spill_evptr, spill_aflags;

    spill_aflags = SPILL_SLOT_10;
    dr_save_arith_flags(drcontext, ilist, where, spill_aflags);

    xax = DR_REG_XAX;
    spill_xax = SPILL_SLOT_11;
    dr_save_reg(drcontext, ilist, where, xax, spill_xax);

    xbx = DR_REG_XBX;
    spill_xbx = SPILL_SLOT_12;
    dr_save_reg(drcontext, ilist, where, xbx, spill_xbx);

    xcx = DR_REG_XCX;
    spill_xcx = SPILL_SLOT_13;
    dr_save_reg(drcontext, ilist, where, xcx, spill_xcx);

    evptr_reg = DR_REG_XDX;
    spill_evptr = SPILL_SLOT_14;
    dr_save_reg(drcontext, ilist, where, evptr_reg, spill_evptr);

    //-----------------------------------------------------------
    /* skip over instrumented event generation, if not enabled,
     * e.g. inside a pthread event, or outside a ROI */

    /* load per_thread_t->active from TLS into reg1 */
    /* test if active, can't do a (jecxz) near jump */
    dr_insert_read_raw_tls(drcontext, ilist, where,
                           raw_tls_seg, ACTIVE_OFFS, xax);
    MINSERT(ilist, where,
            XINST_CREATE_cmp(drcontext,
                             opnd_create_reg(xax),
                             OPND_CREATE_INT8(false)));

    /* skip sigil events if inactive */
    instr_t *skip_sigil = INSTR_CREATE_label(drcontext);
    MINSERT(ilist, where,
            INSTR_CREATE_jcc(drcontext,
                             OP_je,
                             opnd_create_instr(skip_sigil)));

    /* Load (TLS) per_thread_t->buffer.events_ptr into evptr_reg
     * this is used and updated by all instrumentation to write event attr */
    dr_insert_read_raw_tls(drcontext, ilist, where,
                           raw_tls_seg, SGLEV_OFFS, evptr_reg);
    //-----------------------------------------------------------

    /* Check if we've just exited a synchronization event
     * Skip if no synch event, can't do a (jecxz) near jump */
    dr_insert_read_raw_tls(drcontext, ilist, where,
                           raw_tls_seg, SGLSYNCEV_OFFS, xax);
    /* if (TLS syncevptr == NULL)
     * can't do a 64 bit cmp to NULL, need a reg :( */
    MINSERT(ilist, where,
            XINST_CREATE_load_int(drcontext,
                                  opnd_create_reg(xbx),
                                  OPND_CREATE_INTPTR(NULL)));
    MINSERT(ilist, where,
            XINST_CREATE_cmp(drcontext,
                             opnd_create_reg(xax),
                             opnd_create_reg(xbx)));
    /* jmp skip_sync */
    instr_t *skip_sync = INSTR_CREATE_label(drcontext);
    MINSERT(ilist, where,
            INSTR_CREATE_jcc(drcontext,
                             OP_je,
                             opnd_create_instr(skip_sync)));

    /* if (TLS syncevptr != NULL) */
    instrument_setup_buffer(drcontext, ilist, where,
                            evptr_reg, xbx, xcx,
                            tcxt->event_block_events + 1);
    instrument_sync(drcontext, ilist, where, evptr_reg, xax, xbx, DR_REG_BL);

    /* already setup the sigil event buffer, so skip over this next setup */
    instr_t *skip_setup = INSTR_CREATE_label(drcontext);
    MINSERT(ilist, where,
            XINST_CREATE_jump(drcontext,
                              opnd_create_instr(skip_setup)));

    /* SKIP SYNC LABEL */
    MINSERT(ilist, where, skip_sync);

    /* add sigil event instrumentation */
    instrument_setup_buffer(drcontext, ilist, where,
                            evptr_reg, xbx, xcx,
                            tcxt->event_block_events);

    /* SKIP SETUP BUFFER LABEL */
    MINSERT(ilist, where, skip_setup);

    /* sigil mem/compute events are filled in from buffer[0] to buffer[N] */
    SglCompEv *comp_ev = tcxt->comps;
    /* xax <= TLS memrefbase */
    dr_insert_read_raw_tls(drcontext, ilist, where,
                           raw_tls_seg, MEMREFBASE_OFFS, xax);

    for (uint i=0; i<tcxt->iblock_count; ++i)
    {
        /* Iterate over all potential events.
         * There may be multiple memory and compute events per instruction.
         * Upon each instrumentation, the memory/compute cache may be
         * incremented multiple times as each event is written to sigil shared
         * memory. This means both the cache pointers for both need to be
         * persistent */
        instr_block_t *iblock = tcxt->iblocks + i;
        instrument_instr(drcontext, ilist, where,
                         evptr_reg, xcx, iblock->instr);
        instrument_mem(drcontext, ilist, where,
                       evptr_reg, xax, xcx, DR_REG_CX, DR_REG_CL,
                       iblock->mem_ref_count);
        instrument_comp(drcontext, ilist, where,
                        evptr_reg, iblock->comp_count, &comp_ev);
    }

    //-----------------------------------------------------------
    /* Finally, update (TLS) per_thread_t->buffer.events_ptr */
    dr_insert_write_raw_tls(drcontext, ilist, where,
                            raw_tls_seg, SGLEV_OFFS, evptr_reg);
    //-----------------------------------------------------------

    MINSERT(ilist, where, skip_sigil);

    dr_restore_reg(drcontext, ilist, where, evptr_reg, spill_evptr);
    dr_restore_reg(drcontext, ilist, where, xcx, spill_xcx);
    dr_restore_reg(drcontext, ilist, where, xbx, spill_xbx);
    dr_restore_reg(drcontext, ilist, where, xax, spill_xax);
    dr_restore_arith_flags(drcontext, ilist, where, spill_aflags);
}
