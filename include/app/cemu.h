#pragma once

#include <stdint.h>

#include <rplloader/rplloader.h>

// Mirrors the reason table in Cemu/graphicspack/patch_wwhd_tools.asm; keep the two in step.
enum {
    RPL_CEMU_FRAME       = 0,
    RPL_CEMU_PRESENT     = 1,
    RPL_CEMU_VPAD        = 2,
    RPL_CEMU_CONTEXT     = 3,
    RPL_CEMU_KPAD        = 4,
    RPL_CEMU_EXEC_BEGIN  = 5,
    RPL_CEMU_EXEC_END    = 6,
    RPL_CEMU_MSGBOX      = 7,
    RPL_CEMU_CCS_BEFORE  = 8,
    RPL_CEMU_CCS_AFTER   = 9,
    RPL_CEMU_CAM_RUN     = 10,
    RPL_CEMU_MASS_CHK    = 11,
    RPL_CEMU_DRAW_BEGIN  = 12,
    RPL_CEMU_DRAW_END    = 13,
    RPL_CEMU_FOOT_MOVE   = 14,
    RPL_CEMU_ACTOR_MOVE  = 15,
    RPL_CEMU_PROC_MOVE   = 16,
    RPL_CEMU_PROC_CRAWL  = 17,
    RPL_CEMU_PROC_SWIM   = 18,
    RPL_CEMU_GX2_COLOR   = 19,
    RPL_CEMU_GX2_DEPTH   = 20,
};

namespace App {
extern bool g_underCemu;
}

RPL_EXPORT uint32_t rpl_cemu_entry(uint32_t reason, void* a, void* b, void* c);
