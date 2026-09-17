#include "app/hooks.h"
#include "app/cemu.h"
#include "app/present.h"
#include "cheats/cheat_movement.h"
#include "cheats/cheat_status.h"
#include "cheats/cheat_text.h"
#include "core/frame_stats.h"
#include "hud/hud_collision.h"
#include "render/gbuffer.h"
#include "tools/flycam.h"

#include "libwwhd/libwwhd.h"

#include <gx2/context.h>
#include <gx2/draw.h>
#include <gx2/state.h>
#include <gx2/surface.h>
#include <gx2/swap.h>

namespace App {
RPL_DECL_REPLACE(void, cCt_Counter, int reset)
{
    real_cCt_Counter(reset);
    Present::OnGameFrame();
}

RPL_DECL_REPLACE(void, GX2CopyColorBufferToScanBuffer,
                 const GX2ColorBuffer* buffer, GX2ScanTarget target)
{
    Present::OnCopyToScanBuffer(real_GX2CopyColorBufferToScanBuffer, buffer, target);
}

RPL_DECL_REPLACE(void, GX2SetContextState, GX2ContextState* state)
{
    Present::NoteContext(state);
    if (real_GX2SetContextState)
        real_GX2SetContextState(state);
}

RPL_DECL_REPLACE(void, GX2SetDepthBuffer, const GX2DepthBuffer* buffer)
{
    GBuffer::NoteDepth(buffer);
    if (real_GX2SetDepthBuffer)
        real_GX2SetDepthBuffer(buffer);
}

RPL_DECL_REPLACE(void, GX2SetColorBuffer, const GX2ColorBuffer* buffer, GX2RenderTarget target)
{
    GBuffer::NoteColor(buffer, (int)target);
    if (real_GX2SetColorBuffer)
        real_GX2SetColorBuffer(buffer, target);
}

RPL_DECL_REPLACE(void, GX2DrawEx, GX2PrimitiveMode mode, uint32_t count, uint32_t offset,
                 uint32_t numInstances)
{
    FrameStats::OnDraw((uint32_t)mode, count, numInstances);
    if (real_GX2DrawEx)
        real_GX2DrawEx(mode, count, offset, numInstances);
}

RPL_DECL_REPLACE(void, GX2DrawIndexedEx, GX2PrimitiveMode mode, uint32_t count,
                 GX2IndexType indexType, const void* indices, uint32_t offset,
                 uint32_t numInstances)
{
    FrameStats::OnDraw((uint32_t)mode, count, numInstances);
    if (real_GX2DrawIndexedEx)
        real_GX2DrawIndexedEx(mode, count, indexType, indices, offset, numInstances);
}

RPL_DECL_REPLACE(void, dMsgBox_setInput, dMsgBox_c* box)
{
    real_dMsgBox_setInput(box);
    Cheats::Text::OnBoxInput(box);
}

RPL_DECL_REPLACE(void, fapGm_Execute, void)
{
    Cheats::Text::OnFrameEarly();
    Tools::FlyCam::OnFrameEarly();
    FrameStats::OnExecuteBegin();
    real_fapGm_Execute();
    FrameStats::OnExecuteEnd();
}

RPL_DECL_REPLACE(uint32_t, daPy_procMove, void* self)
{
    const uint32_t result = real_daPy_procMove(self);
    Cheats::Movement::OnMoveProc(self);
    return result;
}

RPL_DECL_REPLACE(uint32_t, daPy_procCrawlMove, void* self)
{
    const uint32_t result = real_daPy_procCrawlMove(self);
    Cheats::Movement::OnCrawlProc(self);
    return result;
}

RPL_DECL_REPLACE(uint32_t, daPy_procSwimMove, void* self)
{
    const uint32_t result = real_daPy_procSwimMove(self);
    Cheats::Movement::OnSwimProc(self);
    return result;
}

RPL_DECL_REPLACE(void, daPy_posMoveFromFootPos, void* self)
{
    Cheats::Movement::OnBeforePosMove(self);
    real_daPy_posMoveFromFootPos(self);
}

RPL_DECL_REPLACE(void, fopAcM_posMove, void* actor, void* offset)
{
    Cheats::Movement::OnBeforeActorPosMove(actor);
    real_fopAcM_posMove(actor, offset);
}

RPL_DECL_REPLACE(uint32_t, daPy_draw, void* self)
{
    const bool hideFlash = Cheats::Status::BeginPlayerDraw(self);
    const uint32_t result = real_daPy_draw(self);
    if (hideFlash)
        Cheats::Status::EndPlayerDraw(self);
    return result;
}

RPL_DECL_REPLACE(void, cCcS_Move, void* ccs)
{
    Hud::Collision::OnBeforeMove(ccs);
    real_cCcS_Move(ccs);
    Hud::Collision::OnAfterMove(ccs);
}

RPL_DECL_REPLACE(uint32_t, dCcMassS_Chk, void* mng, const void* pos, void* actor,
                 void* hitInf)
{
    const uint32_t result = real_dCcMassS_Chk(mng, pos, actor, hitInf);
    Hud::Collision::OnMassCheck(mng, pos, result);
    return result;
}

RPL_DECL_REPLACE(uint32_t, dCam_run, void* camera)
{
    const uint32_t result = real_dCam_run(camera);
    Hud::Collision::OnCameraRun(camera);
    return result;
}

static RplHook s_dynamicHooks[] = {
    RPL_REPLACE(dMsgBox_setInput, 0u, 0x3D80101Fu, RPL_HOOK_OPTIONAL),
    RPL_REPLACE(fapGm_Execute,    0u, 0x7C0802A6u, RPL_HOOK_OPTIONAL),
    RPL_REPLACE(daPy_procMove,    0u, 0x7C0802A6u, RPL_HOOK_OPTIONAL),
    RPL_REPLACE(daPy_procCrawlMove, 0u, 0x9421FE80u, RPL_HOOK_OPTIONAL),
    RPL_REPLACE(daPy_procSwimMove,  0u, 0x9421FFA8u, RPL_HOOK_OPTIONAL),
    RPL_REPLACE(daPy_posMoveFromFootPos, 0u, 0x9421FED8u, RPL_HOOK_OPTIONAL),
    RPL_REPLACE(fopAcM_posMove,   0u, 0xC1830340u, RPL_HOOK_OPTIONAL),
    RPL_REPLACE(daPy_draw,        0u, 0x9421FEC0u, RPL_HOOK_OPTIONAL),
    RPL_REPLACE(cCcS_Move,        0u, 0x7C0802A6u, RPL_HOOK_OPTIONAL),
    RPL_REPLACE(dCam_run,         0u, 0x9421FF78u, RPL_HOOK_OPTIONAL),
    RPL_REPLACE(dCcMassS_Chk,     0u, 0x9421FF60u, RPL_HOOK_OPTIONAL),
};

bool RegisterDynamicHooks(const RplHost* host)
{
    if (!host || !wwhd_regionResolved)
        return false;

    if (g_underCemu) {
        host->log(host, RPL_LOG_INFO,
                  "dynamic hooks come from the Cemu graphics pack");
        return true;
    }

    s_dynamicHooks[0].linkAddr = wwhd_map->dMsgBox_setInput;
    s_dynamicHooks[1].linkAddr = wwhd_map->fapGm_Execute;
    s_dynamicHooks[2].linkAddr = wwhd_map->daPy_procMove;
    s_dynamicHooks[3].linkAddr = wwhd_map->daPy_procCrawlMove;
    s_dynamicHooks[4].linkAddr = wwhd_map->daPy_procSwimMove;
    s_dynamicHooks[5].linkAddr = wwhd_map->daPy_posMoveFromFootPos;
    s_dynamicHooks[6].linkAddr = wwhd_map->fopAcM_posMove;
    s_dynamicHooks[7].linkAddr = wwhd_map->daPy_draw;
    s_dynamicHooks[8].linkAddr = wwhd_map->cCcS_Move;
    s_dynamicHooks[9].linkAddr = wwhd_map->dCam_run;
    s_dynamicHooks[10].linkAddr = wwhd_map->dCcMassS_Chk;

    bool ok = true;
    for (uint32_t i = 0; i < sizeof(s_dynamicHooks) / sizeof(s_dynamicHooks[0]); ++i)
        if (host->addHook(host, &s_dynamicHooks[i]) != 0)
            ok = false;
    return ok;
}

const RplHook gHooks[] = {
    RPL_REPLACE(cCt_Counter, 0x0200E6ECu, 0x3D401020u, RPL_HOOK_REQUIRED),
    RPL_REPLACE_LIB(GX2CopyColorBufferToScanBuffer, "gx2", RPL_HOOK_REQUIRED),
    RPL_REPLACE_LIB(GX2SetContextState, "gx2", RPL_HOOK_REQUIRED),
    RPL_REPLACE_LIB(GX2SetDepthBuffer, "gx2", RPL_HOOK_OPTIONAL),
    RPL_REPLACE_LIB(GX2SetColorBuffer, "gx2", RPL_HOOK_OPTIONAL),
    RPL_REPLACE_LIB(GX2DrawEx, "gx2", RPL_HOOK_OPTIONAL),
    RPL_REPLACE_LIB(GX2DrawIndexedEx, "gx2", RPL_HOOK_OPTIONAL),
};
const uint32_t gHookCount = sizeof(gHooks) / sizeof(gHooks[0]);
}
