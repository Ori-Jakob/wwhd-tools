; Cemu hook stubs for wwhd_tools.rpl; the reason numbers mirror RPL_CEMU_* in include/app/cemu.h

[WWHDv16]
moduleMatches = 0x475bd29f, 0xb7e748de, 0x18005ce3

0x0200E6F0 = _cCt_Counter_rest:
0x0200E55C = _cCcS_Move_rest:

.origin = codecave

_pack_block:
_pk_draw_addr:
.int 0
_pk_drawidx_addr:
.int 0
_pk_draw_stub:
.int 0
_pk_drawidx_stub:
.int 0
_mode_stats:
.int 0, 0, 0, 0, 0, 0, 0, 0
.int 0, 0, 0, 0, 0, 0, 0, 0
.int 0, 0, 0, 0, 0, 0, 0, 0
.int 0, 0, 0, 0, 0, 0, 0, 0
.int 0, 0, 0, 0, 0, 0, 0, 0
.int 0, 0, 0, 0, 0, 0, 0, 0
_pk_version:
.int 2
_pk_copy_stub:
.int 0

_rpl_state:
.byte 0
.align 4

_rpl_module:
.int 0

_rpl_fn:
.int 0

_rpl_name:
.string "wwhd_tools"
.align 4

_rpl_export:
.string "rpl_cemu_entry"
.align 4

_rpl_missing:
.string "[wwhd_tools] OSDynLoad_Acquire(wwhd_tools) failed -- is wwhd_tools.rpl in the title code folder?\n"
.align 4

_rpl_noexport:
.string "[wwhd_tools] wwhd_tools.rpl loaded but has no rpl_cemu_entry\n"
.align 4

_rpl_ok:
.string "[wwhd_tools] rpl_cemu_entry resolved\n"
.align 4

_rpl_alloc_msg:
.string "[wwhd_tools] loader alloc %u bytes align %u -> %08x\n"
.align 4

_rpl_game_alloc:
.int 0
_rpl_game_free:
.int 0

; Cemu places a late-loaded rpl's data with the allocator the game registered,
; which is ErrEula's small heap here; use the MEM2 base heap instead
_rpl_alloc:
    stwu  r1, -0x30(r1)
    mflr  r0
    stw   r0, 0x24(r1)
    stw   r3, 0x08(r1)
    stw   r4, 0x0C(r1)
    stw   r5, 0x10(r1)
    li    r3, 1
    bl    import.coreinit.MEMGetBaseHeapHandle
    cmpwi r3, 0
    beq   _rpl_alloc_fail
    lwz   r4, 0x08(r1)
    lwz   r5, 0x0C(r1)
    bl    import.coreinit.MEMAllocFromExpHeapEx
    stw   r3, 0x14(r1)
    lwz   r5, 0x10(r1)
    stw   r3, 0(r5)
    mr    r6, r3
    lwz   r4, 0x08(r1)
    lwz   r5, 0x0C(r1)
    lis   r3, _rpl_alloc_msg@ha
    addi  r3, r3, _rpl_alloc_msg@l
    bl    import.coreinit.OSReport
    lwz   r3, 0x14(r1)
    cmpwi r3, 0
    beq   _rpl_alloc_fail
    li    r3, 0
    b     _rpl_alloc_done
_rpl_alloc_fail:
    lwz   r5, 0x10(r1)
    li    r0, 0
    stw   r0, 0(r5)
    lis   r3, 0xFFFF
    ori   r3, r3, 0xFFFF
_rpl_alloc_done:
    lwz   r0, 0x24(r1)
    mtlr  r0
    addi  r1, r1, 0x30
    blr

_rpl_free:
    stwu  r1, -0x30(r1)
    mflr  r0
    stw   r0, 0x24(r1)
    stw   r3, 0x08(r1)
    li    r3, 1
    bl    import.coreinit.MEMGetBaseHeapHandle
    cmpwi r3, 0
    beq   _rpl_free_done
    lwz   r4, 0x08(r1)
    bl    import.coreinit.MEMFreeToExpHeap
_rpl_free_done:
    lwz   r0, 0x24(r1)
    mtlr  r0
    addi  r1, r1, 0x30
    blr

_rpl_resolve:
    stwu  r1, -0x30(r1)
    mflr  r0
    stw   r0, 0x24(r1)

    lis   r3, _rpl_state@ha
    lbz   r4, _rpl_state@l(r3)
    cmpwi r4, 0
    bne   _rpl_resolve_done

    li    r4, 1
    stb   r4, _rpl_state@l(r3)

    lis   r3, _rpl_game_alloc@ha
    addi  r3, r3, _rpl_game_alloc@l
    lis   r4, _rpl_game_free@ha
    addi  r4, r4, _rpl_game_free@l
    bl    import.coreinit.OSDynLoad_GetAllocator

    lis   r3, _rpl_alloc@ha
    addi  r3, r3, _rpl_alloc@l
    lis   r4, _rpl_free@ha
    addi  r4, r4, _rpl_free@l
    bl    import.coreinit.OSDynLoad_SetAllocator

    lis   r3, _rpl_name@ha
    addi  r3, r3, _rpl_name@l
    lis   r4, _rpl_module@ha
    addi  r4, r4, _rpl_module@l
    bl    import.coreinit.OSDynLoad_Acquire

    lis   r3, _rpl_game_alloc@ha
    lwz   r3, _rpl_game_alloc@l(r3)
    lis   r4, _rpl_game_free@ha
    lwz   r4, _rpl_game_free@l(r4)
    bl    import.coreinit.OSDynLoad_SetAllocator

    lis   r3, _rpl_module@ha
    lwz   r3, _rpl_module@l(r3)
    cmpwi r3, 0
    bne   _rpl_have_module

    lis   r3, _rpl_missing@ha
    addi  r3, r3, _rpl_missing@l
    bl    import.coreinit.OSReport
    b     _rpl_resolve_done

_rpl_have_module:
    li    r4, 0
    lis   r5, _rpl_export@ha
    addi  r5, r5, _rpl_export@l
    lis   r6, _rpl_fn@ha
    addi  r6, r6, _rpl_fn@l
    bl    import.coreinit.OSDynLoad_FindExport

    lis   r3, _rpl_fn@ha
    lwz   r3, _rpl_fn@l(r3)
    cmpwi r3, 0
    bne   _rpl_report_ok

    lis   r3, _rpl_noexport@ha
    addi  r3, r3, _rpl_noexport@l
    bl    import.coreinit.OSReport
    b     _rpl_resolve_done

_rpl_report_ok:
    lis   r3, _rpl_ok@ha
    addi  r3, r3, _rpl_ok@l
    bl    import.coreinit.OSReport

_rpl_resolve_done:
    lis   r9, _rpl_fn@ha
    lwz   r9, _rpl_fn@l(r9)
    lwz   r0, 0x24(r1)
    mtlr  r0
    addi  r1, r1, 0x30
    blr

gx2draw_hook:
    cmpwi r3, 0
    blt   gx2draw_go
    cmpwi r3, 24
    bge   gx2draw_go
    add   r0, r3, r3
    add   r0, r0, r0
    add   r0, r0, r0
    lis   r9, _mode_stats@ha
    addi  r9, r9, _mode_stats@l
    add   r9, r9, r0
    lwz   r11, 0(r9)
    add   r11, r11, r4
    stw   r11, 0(r9)
    lwz   r11, 4(r9)
    addi  r11, r11, 1
    stw   r11, 4(r9)

gx2draw_go:
    lis   r9, _pk_draw_addr@ha
    lwz   r9, _pk_draw_addr@l(r9)
    cmpwi r9, 0
    beq   gx2draw_skip
    mtctr r9
    bctr

gx2draw_skip:
    blr

gx2drawidx_hook:
    cmpwi r3, 0
    blt   gx2drawidx_go
    cmpwi r3, 24
    bge   gx2drawidx_go
    add   r0, r3, r3
    add   r0, r0, r0
    add   r0, r0, r0
    lis   r9, _mode_stats@ha
    addi  r9, r9, _mode_stats@l
    add   r9, r9, r0
    lwz   r11, 0(r9)
    add   r11, r11, r4
    stw   r11, 0(r9)
    lwz   r11, 4(r9)
    addi  r11, r11, 1
    stw   r11, 4(r9)

gx2drawidx_go:
    lis   r9, _pk_drawidx_addr@ha
    lwz   r9, _pk_drawidx_addr@l(r9)
    cmpwi r9, 0
    beq   gx2drawidx_skip
    mtctr r9
    bctr

gx2drawidx_skip:
    blr

counter_hook:
    stwu  r1, -0x40(r1)
    mflr  r0
    stw   r0, 0x34(r1)
    stw   r3, 0x10(r1)

    lis   r9, _pack_block@ha
    addi  r9, r9, _pack_block@l
    lis   r10, gx2draw_hook@ha
    addi  r10, r10, gx2draw_hook@l
    stw   r10, 8(r9)
    lis   r10, gx2drawidx_hook@ha
    addi  r10, r10, gx2drawidx_hook@l
    stw   r10, 12(r9)
    lis   r10, copy_hook@ha
    addi  r10, r10, copy_hook@l
    stw   r10, 0xD4(r9)

    bl    _rpl_resolve
    cmpwi r9, 0
    beq   counter_done

    mtctr r9
    li    r3, 0
    lis   r4, _pack_block@ha
    addi  r4, r4, _pack_block@l
    li    r5, 0
    li    r6, 0
    bctrl

counter_done:
    lwz   r3, 0x10(r1)
    lwz   r0, 0x34(r1)
    mtlr  r0
    addi  r1, r1, 0x40
    lis   r10, 0x1020
    b     _cCt_Counter_rest

copy_hook:
    stwu  r1, -0x30(r1)
    mflr  r0
    stw   r0, 0x24(r1)
    stw   r3, 0x10(r1)
    stw   r4, 0x0c(r1)

    lis   r9, _rpl_fn@ha
    lwz   r9, _rpl_fn@l(r9)
    cmpwi r9, 0
    beq   copy_real

    mtctr r9
    li    r3, 1
    lwz   r4, 0x10(r1)
    lwz   r5, 0x0c(r1)
    li    r6, 0
    bctrl
    cmpwi r3, 0
    bne   copy_done

copy_real:
    lwz   r3, 0x10(r1)
    lwz   r4, 0x0c(r1)
    bl    import.gx2.GX2CopyColorBufferToScanBuffer

copy_done:
    lwz   r0, 0x24(r1)
    mtlr  r0
    addi  r1, r1, 0x30
    blr

context_hook:
    stwu  r1, -0x20(r1)
    mflr  r0
    stw   r0, 0x14(r1)
    stw   r3, 0x10(r1)

    lis   r9, _rpl_fn@ha
    lwz   r9, _rpl_fn@l(r9)
    cmpwi r9, 0
    beq   context_real

    mtctr r9
    li    r3, 3
    lwz   r4, 0x10(r1)
    li    r5, 0
    li    r6, 0
    bctrl

context_real:
    lwz   r3, 0x10(r1)
    bl    import.gx2.GX2SetContextState

    lwz   r0, 0x14(r1)
    mtlr  r0
    addi  r1, r1, 0x20
    blr

vpad_read_hook:
    stwu  r1, -0x30(r1)
    mflr  r0
    stw   r0, 0x24(r1)
    stw   r4, 0x10(r1)

    bl    import.vpad.VPADRead
    stw   r3, 0x0c(r1)

    cmpwi r3, 0
    ble   vpad_done

    lis   r9, _rpl_fn@ha
    lwz   r9, _rpl_fn@l(r9)
    cmpwi r9, 0
    beq   vpad_done

    mtctr r9
    li    r3, 2
    lwz   r4, 0x10(r1)
    lwz   r5, 0x0c(r1)
    li    r6, 0
    bctrl

vpad_done:
    lwz   r3, 0x0c(r1)
    lwz   r0, 0x24(r1)
    mtlr  r0
    addi  r1, r1, 0x30
    blr

kpad_read_hook:
    stwu  r1, -0x30(r1)
    mflr  r0
    stw   r0, 0x24(r1)
    stw   r3, 0x14(r1)
    stw   r4, 0x10(r1)

    bl    import.padscore.KPADReadEx
    stw   r3, 0x0c(r1)

    cmpwi r3, 0
    ble   kpad_done

    lis   r9, _rpl_fn@ha
    lwz   r9, _rpl_fn@l(r9)
    cmpwi r9, 0
    beq   kpad_done

    mtctr r9
    li    r3, 4
    lwz   r4, 0x10(r1)
    lwz   r5, 0x0c(r1)
    lwz   r6, 0x14(r1)
    bctrl

kpad_done:
    lwz   r3, 0x0c(r1)
    lwz   r0, 0x24(r1)
    mtlr  r0
    addi  r1, r1, 0x30
    blr

execute_hook:
    stwu  r1, -0x60(r1)
    mflr  r0
    stw   r0, 0x54(r1)

    lis   r9, _rpl_fn@ha
    lwz   r9, _rpl_fn@l(r9)
    cmpwi r9, 0
    beq   execute_call

    mtctr r9
    li    r3, 5
    li    r4, 0
    li    r5, 0
    li    r6, 0
    bctrl

execute_call:
    bl    execute_tramp

    lis   r9, _rpl_fn@ha
    lwz   r9, _rpl_fn@l(r9)
    cmpwi r9, 0
    beq   execute_done

    mtctr r9
    li    r3, 6
    li    r4, 0
    li    r5, 0
    li    r6, 0
    bctrl

execute_done:
    lwz   r0, 0x54(r1)
    mtlr  r0
    addi  r1, r1, 0x60
    blr

execute_tramp:
    mflr  r0
    b     _fapGm_Execute_rest

msgbox_hook:
    stwu  r1, -0x60(r1)
    mflr  r0
    stw   r0, 0x54(r1)
    stw   r3, 0x20(r1)

    bl    msgbox_tramp
    stw   r3, 0x1c(r1)

    lis   r9, _rpl_fn@ha
    lwz   r9, _rpl_fn@l(r9)
    cmpwi r9, 0
    beq   msgbox_done

    mtctr r9
    li    r3, 7
    lwz   r4, 0x20(r1)
    li    r5, 0
    li    r6, 0
    bctrl

msgbox_done:
    lwz   r3, 0x1c(r1)
    lwz   r0, 0x54(r1)
    mtlr  r0
    addi  r1, r1, 0x60
    blr

msgbox_tramp:
    lis   r12, 0x101F
    b     _dMsgBox_setInput_rest

ccsmove_hook:
    stwu  r1, -0x60(r1)
    mflr  r0
    stw   r0, 0x54(r1)
    stw   r3, 0x20(r1)

    lis   r9, _rpl_fn@ha
    lwz   r9, _rpl_fn@l(r9)
    cmpwi r9, 0
    beq   ccsmove_call

    mtctr r9
    li    r3, 8
    lwz   r4, 0x20(r1)
    li    r5, 0
    li    r6, 0
    bctrl

ccsmove_call:
    lwz   r3, 0x20(r1)
    bl    ccsmove_tramp
    stw   r3, 0x1c(r1)

    lis   r9, _rpl_fn@ha
    lwz   r9, _rpl_fn@l(r9)
    cmpwi r9, 0
    beq   ccsmove_done

    mtctr r9
    li    r3, 9
    lwz   r4, 0x20(r1)
    li    r5, 0
    li    r6, 0
    bctrl

ccsmove_done:
    lwz   r3, 0x1c(r1)
    lwz   r0, 0x54(r1)
    mtlr  r0
    addi  r1, r1, 0x60
    blr

ccsmove_tramp:
    mflr  r0
    b     _cCcS_Move_rest

camrun_hook:
    stwu  r1, -0x60(r1)
    mflr  r0
    stw   r0, 0x54(r1)
    stw   r3, 0x20(r1)

    bl    camrun_tramp
    stw   r3, 0x1c(r1)

    lis   r9, _rpl_fn@ha
    lwz   r9, _rpl_fn@l(r9)
    cmpwi r9, 0
    beq   camrun_done

    mtctr r9
    li    r3, 10
    lwz   r4, 0x20(r1)
    li    r5, 0
    li    r6, 0
    bctrl

camrun_done:
    lwz   r3, 0x1c(r1)
    lwz   r0, 0x54(r1)
    mtlr  r0
    addi  r1, r1, 0x60
    blr

camrun_tramp:
    stwu  r1, -0x88(r1)
    b     _dCam_run_rest

mass_hook:
    stwu  r1, -0x60(r1)
    mflr  r0
    stw   r0, 0x54(r1)
    stw   r3, 0x20(r1)
    stw   r4, 0x24(r1)

    bl    mass_tramp
    stw   r3, 0x1c(r1)

    lis   r9, _rpl_fn@ha
    lwz   r9, _rpl_fn@l(r9)
    cmpwi r9, 0
    beq   mass_done

    mtctr r9
    li    r3, 11
    lwz   r4, 0x20(r1)
    lwz   r5, 0x24(r1)
    lwz   r6, 0x1c(r1)
    bctrl

mass_done:
    lwz   r3, 0x1c(r1)
    lwz   r0, 0x54(r1)
    mtlr  r0
    addi  r1, r1, 0x60
    blr

mass_tramp:
    stwu  r1, -0xa0(r1)
    b     _dCcMassS_Chk_rest

playerdraw_hook:
    stwu  r1, -0x60(r1)
    mflr  r0
    stw   r0, 0x54(r1)
    stw   r3, 0x20(r1)

    lis   r9, _rpl_fn@ha
    lwz   r9, _rpl_fn@l(r9)
    cmpwi r9, 0
    beq   playerdraw_call

    mtctr r9
    li    r3, 12
    lwz   r4, 0x20(r1)
    li    r5, 0
    li    r6, 0
    bctrl

playerdraw_call:
    lwz   r3, 0x20(r1)
    bl    playerdraw_tramp
    stw   r3, 0x1c(r1)

    lis   r9, _rpl_fn@ha
    lwz   r9, _rpl_fn@l(r9)
    cmpwi r9, 0
    beq   playerdraw_done

    mtctr r9
    li    r3, 13
    lwz   r4, 0x20(r1)
    li    r5, 0
    li    r6, 0
    bctrl

playerdraw_done:
    lwz   r3, 0x1c(r1)
    lwz   r0, 0x54(r1)
    mtlr  r0
    addi  r1, r1, 0x60
    blr

playerdraw_tramp:
    stwu  r1, -0x140(r1)
    b     _daPy_draw_rest

footmove_hook:
    stwu  r1, -0x60(r1)
    mflr  r0
    stw   r0, 0x54(r1)
    stw   r3, 0x20(r1)
    stw   r4, 0x24(r1)

    lis   r9, _rpl_fn@ha
    lwz   r9, _rpl_fn@l(r9)
    cmpwi r9, 0
    beq   footmove_call

    mtctr r9
    li    r3, 14
    lwz   r4, 0x20(r1)
    li    r5, 0
    li    r6, 0
    bctrl

footmove_call:
    lwz   r3, 0x20(r1)
    lwz   r4, 0x24(r1)
    bl    footmove_tramp

    lwz   r0, 0x54(r1)
    mtlr  r0
    addi  r1, r1, 0x60
    blr

footmove_tramp:
    stwu  r1, -0x128(r1)
    b     _daPy_footMove_rest

actormove_hook:
    stwu  r1, -0x60(r1)
    mflr  r0
    stw   r0, 0x54(r1)
    stw   r3, 0x20(r1)
    stw   r4, 0x24(r1)

    lis   r9, _rpl_fn@ha
    lwz   r9, _rpl_fn@l(r9)
    cmpwi r9, 0
    beq   actormove_call

    mtctr r9
    li    r3, 15
    lwz   r4, 0x20(r1)
    li    r5, 0
    li    r6, 0
    bctrl

actormove_call:
    lwz   r3, 0x20(r1)
    lwz   r4, 0x24(r1)
    bl    actormove_tramp

    lwz   r0, 0x54(r1)
    mtlr  r0
    addi  r1, r1, 0x60
    blr

actormove_tramp:
    lfs   f12, 0x340(r3)
    b     _fopAcM_posMove_rest

procmove_hook:
    stwu  r1, -0x60(r1)
    mflr  r0
    stw   r0, 0x54(r1)
    stw   r3, 0x20(r1)

    bl    procmove_tramp
    stw   r3, 0x1c(r1)

    lis   r9, _rpl_fn@ha
    lwz   r9, _rpl_fn@l(r9)
    cmpwi r9, 0
    beq   procmove_done

    mtctr r9
    li    r3, 16
    lwz   r4, 0x20(r1)
    li    r5, 0
    li    r6, 0
    bctrl

procmove_done:
    lwz   r3, 0x1c(r1)
    lwz   r0, 0x54(r1)
    mtlr  r0
    addi  r1, r1, 0x60
    blr

procmove_tramp:
    mflr  r0
    b     _daPy_procMove_rest

proccrawl_hook:
    stwu  r1, -0x60(r1)
    mflr  r0
    stw   r0, 0x54(r1)
    stw   r3, 0x20(r1)

    bl    proccrawl_tramp
    stw   r3, 0x1c(r1)

    lis   r9, _rpl_fn@ha
    lwz   r9, _rpl_fn@l(r9)
    cmpwi r9, 0
    beq   proccrawl_done

    mtctr r9
    li    r3, 17
    lwz   r4, 0x20(r1)
    li    r5, 0
    li    r6, 0
    bctrl

proccrawl_done:
    lwz   r3, 0x1c(r1)
    lwz   r0, 0x54(r1)
    mtlr  r0
    addi  r1, r1, 0x60
    blr

proccrawl_tramp:
    stwu  r1, -0x180(r1)
    b     _daPy_procCrawl_rest

procswim_hook:
    stwu  r1, -0x60(r1)
    mflr  r0
    stw   r0, 0x54(r1)
    stw   r3, 0x20(r1)

    bl    procswim_tramp
    stw   r3, 0x1c(r1)

    lis   r9, _rpl_fn@ha
    lwz   r9, _rpl_fn@l(r9)
    cmpwi r9, 0
    beq   procswim_done

    mtctr r9
    li    r3, 18
    lwz   r4, 0x20(r1)
    li    r5, 0
    li    r6, 0
    bctrl

procswim_done:
    lwz   r3, 0x1c(r1)
    lwz   r0, 0x54(r1)
    mtlr  r0
    addi  r1, r1, 0x60
    blr

procswim_tramp:
    stwu  r1, -0x58(r1)
    b     _daPy_procSwim_rest

gx2color_hook:
    stwu  r1, -0x30(r1)
    mflr  r0
    stw   r0, 0x24(r1)
    stw   r3, 0x10(r1)
    stw   r4, 0x0c(r1)

    lis   r9, _rpl_fn@ha
    lwz   r9, _rpl_fn@l(r9)
    cmpwi r9, 0
    beq   gx2color_real

    mtctr r9
    li    r3, 19
    lwz   r4, 0x10(r1)
    lwz   r5, 0x0c(r1)
    li    r6, 0
    bctrl

gx2color_real:
    lwz   r3, 0x10(r1)
    lwz   r4, 0x0c(r1)
    bl    import.gx2.GX2SetColorBuffer

    lwz   r0, 0x24(r1)
    mtlr  r0
    addi  r1, r1, 0x30
    blr

gx2depth_hook:
    stwu  r1, -0x30(r1)
    mflr  r0
    stw   r0, 0x24(r1)
    stw   r3, 0x10(r1)

    lis   r9, _rpl_fn@ha
    lwz   r9, _rpl_fn@l(r9)
    cmpwi r9, 0
    beq   gx2depth_real

    mtctr r9
    li    r3, 20
    lwz   r4, 0x10(r1)
    li    r5, 0
    li    r6, 0
    bctrl

gx2depth_real:
    lwz   r3, 0x10(r1)
    bl    import.gx2.GX2SetDepthBuffer

    lwz   r0, 0x24(r1)
    mtlr  r0
    addi  r1, r1, 0x30
    blr

0x0200E6EC = b   counter_hook
0x0200E558 = b   ccsmove_hook
0x02035274 = bla context_hook

[WWHDv16_USA]
moduleMatches = 0x475bd29f, 0x18005ce3

0x025D42F0 = _fapGm_Execute_rest:
0x026FF5B0 = _dMsgBox_setInput_rest:
0x024FE3EC = _dCam_run_rest:
0x025170DC = _dCcMassS_Chk_rest:
0x023D9824 = _daPy_draw_rest:
0x023FCBA0 = _daPy_footMove_rest:
0x025D6804 = _fopAcM_posMove_rest:
0x024198D8 = _daPy_procMove_rest:
0x0242C810 = _daPy_procCrawl_rest:
0x0242F710 = _daPy_procSwim_rest:

0x025D42EC = b   execute_hook
0x026FF5AC = b   msgbox_hook
0x024FE3E8 = b   camrun_hook
0x025170D8 = b   mass_hook
0x023D9820 = b   playerdraw_hook
0x023FCB9C = b   footmove_hook
0x025D6800 = b   actormove_hook
0x024198D4 = b   procmove_hook
0x0242C80C = b   proccrawl_hook
0x0242F70C = b   procswim_hook

0x027510A4 = bla copy_hook
0x027B9938 = bla copy_hook
0x027B9958 = bla copy_hook
0x0273E420 = bla vpad_read_hook
0x0273D93C = bla kpad_read_hook
0x0274C59C = bla context_hook
0x0274C5D0 = bla context_hook
0x0274C630 = bla context_hook
0x0274C664 = bla context_hook
0x02750E88 = bla context_hook
0x02750EC0 = bla context_hook
0x02750EE8 = bla context_hook
0x02750F00 = bla context_hook
0x02750F38 = bla context_hook
0x02750F68 = bla context_hook
0x02750F98 = bla context_hook
0x0276B150 = b   context_hook
0x0276B158 = b   context_hook
0x027510DC = bla gx2color_hook
0x027B9DE4 = bla gx2color_hook
0x027B9E10 = bla gx2color_hook
0x027510E4 = bla gx2depth_hook
0x027B9E50 = bla gx2depth_hook

[WWHDv16_EUR]
moduleMatches = 0xb7e748de

0x025D42B0 = _fapGm_Execute_rest:
0x026FFE6C = _dMsgBox_setInput_rest:
0x024FE3F0 = _dCam_run_rest:
0x025170E0 = _dCcMassS_Chk_rest:
0x023D9828 = _daPy_draw_rest:
0x023FCBA4 = _daPy_footMove_rest:
0x025D67C4 = _fopAcM_posMove_rest:
0x024198DC = _daPy_procMove_rest:
0x0242C814 = _daPy_procCrawl_rest:
0x0242F714 = _daPy_procSwim_rest:

0x025D42AC = b   execute_hook
0x026FFE68 = b   msgbox_hook
0x024FE3EC = b   camrun_hook
0x025170DC = b   mass_hook
0x023D9824 = b   playerdraw_hook
0x023FCBA0 = b   footmove_hook
0x025D67C0 = b   actormove_hook
0x024198D8 = b   procmove_hook
0x0242C810 = b   proccrawl_hook
0x0242F710 = b   procswim_hook

0x02751960 = bla copy_hook
0x027BA1F8 = bla copy_hook
0x027BA218 = bla copy_hook
0x0273ECDC = bla vpad_read_hook
0x0273E1F8 = bla kpad_read_hook
0x0274CE58 = bla context_hook
0x0274CE8C = bla context_hook
0x0274CEEC = bla context_hook
0x0274CF20 = bla context_hook
0x02751744 = bla context_hook
0x0275177C = bla context_hook
0x027517A4 = bla context_hook
0x027517BC = bla context_hook
0x027517F4 = bla context_hook
0x02751824 = bla context_hook
0x02751854 = bla context_hook
0x0276BA10 = b   context_hook
0x0276BA18 = b   context_hook
0x02751998 = bla gx2color_hook
0x027BA6A4 = bla gx2color_hook
0x027BA6D0 = bla gx2color_hook
0x027519A0 = bla gx2depth_hook
0x027BA710 = bla gx2depth_hook
