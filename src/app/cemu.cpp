#include "app/cemu.h"
#include "app/present.h"
#include "cheats/cheat_movement.h"
#include "cheats/cheat_status.h"
#include "cheats/cheat_text.h"
#include "core/frame_stats.h"
#include "hud/hud_collision.h"
#include "render/gbuffer.h"
#include "tools/flycam.h"
#include "ui/overlay.h"

#include "libwupatch/wupatch.h"
#include "libwupatch/wupatch_apply.h"

#include <coreinit/cache.h>
#include <coreinit/debug.h>
#include <coreinit/dynload.h>
#include <coreinit/time.h>
#include <coreinit/title.h>
#include <sys/stat.h>
#include <gx2/context.h>
#include <gx2/enum.h>
#include <gx2/surface.h>
#include <gx2/swap.h>
#include <malloc.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <padscore/kpad.h>
#include <padscore/wpad.h>
#include <vpad/input.h>

RPL_EXPORT const RplManifest* rpl_manifest();

namespace App {
bool g_underCemu = false;

namespace Cemu {
namespace {
RplHost  s_host;
bool     s_ready = false;
bool     s_failed = false;

bool     s_hidPlayerFlash = false;
RplPad   s_pad;
bool     s_havePad = false;
RplKpad  s_kpad[RPL_KPAD_CHANNELS];
bool     s_haveKpad[RPL_KPAD_CHANNELS];
bool     s_vpadLogged = false;
bool     s_kpadLogged = false;
int      s_inputMode = RPL_INPUT_PASS;
bool     s_stickHeld = false;
float    s_stickX = 0.0f, s_stickY = 0.0f;

bool     s_vpadStickApplied = false;
bool     s_kpadStickApplied = false;
uint32_t s_vpadReads = 0;
uint32_t s_vpadReadsLogged = 0xFFFFFFFFu;
unsigned s_vpadRateLogs = 0;

const char* const kLogDir = "fs:/vol/external01/wwhd_tools/logs";

FILE* s_logFile = 0;
bool  s_logTried = false;

void openLogFile()
{
    if (s_logTried)
        return;
    s_logTried = true;

    mkdir("fs:/vol/external01/wwhd_tools", 0777);
    mkdir(kLogDir, 0777);

    char path[160];
    snprintf(path, sizeof(path), "%s/%016llX.log", kLogDir,
             (unsigned long long)OSGetTitleID());
    s_logFile = fopen(path, "w");
    if (!s_logFile)
        return;

    OSCalendarTime ct;
    OSTicksToCalendarTime(OSGetTime(), &ct);
    char head[96];
    snprintf(head, sizeof(head),
             "---- session %04d-%02d-%02d %02d:%02d:%02d ----",
             ct.tm_year, ct.tm_mon + 1, ct.tm_mday, ct.tm_hour, ct.tm_min,
             ct.tm_sec);
    fputs(head, s_logFile);
    fputc(10, s_logFile);
    fflush(s_logFile);
    OSReport("[wwhd_tools] logging to %s\n", path);
}

void writeLine(const char* line)
{
    if (!s_logFile)
        openLogFile();
    if (!s_logFile)
        return;
    fputs(line, s_logFile);
    fputc(10, s_logFile);
    fflush(s_logFile);
}

void report(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

void report(const char* fmt, ...)
{
    char line[320];
    va_list args;
    va_start(args, fmt);
    vsnprintf(line, sizeof(line), fmt, args);
    va_end(args);
    OSReport("%s\n", line);
    writeLine(line);
}

void hostLog(const RplHost*, int level, const char* fmt, ...)
{
    char body[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(body, sizeof(body), fmt, args);
    va_end(args);

    char line[560];
    snprintf(line, sizeof(line), "[wwhd_tools][%s] %s",
             level == RPL_LOG_ERROR ? "error" : level == RPL_LOG_WARN ? "warn" : "info",
             body);
    OSReport("%s\n", line);
    writeLine(line);
}

void hostNotify(const RplHost*, int, const char* text)
{
    char line[256];
    snprintf(line, sizeof(line), "[wwhd_tools] %s", text ? text : "");
    OSReport("%s\n", line);
    writeLine(line);
}

struct CemuPatch {
    const RplHook*  hook;
    WuPatch::Handle handle;
};

const int kMaxCemuPatches = 16;
CemuPatch s_patches[kMaxCemuPatches];
int       s_patchCount = 0;

const uint32_t kBranchReach = 32u * 1024u * 1024u;
bool s_rangeReported = false;

int findPatch(const RplHook* h)
{
    for (int i = 0; i < s_patchCount; ++i)
        if (s_patches[i].hook == h)
            return i;
    return -1;
}

uint32_t hostOriginal(const RplHost*, const RplHook* h)
{
    const int i = findPatch(h);
    return i < 0 ? 0 : WuPatch::GetOriginalThunk(s_patches[i].handle);
}

int hostState(const RplHost*, const RplHook* h)
{
    const int i = findPatch(h);
    if (i < 0)
        return RPL_STATE_DECLARED;
    switch (WuPatch::GetState(s_patches[i].handle)) {
    case WuPatch::STATE_APPLIED:  return RPL_STATE_APPLIED;
    case WuPatch::STATE_REFUSED:  return RPL_STATE_REFUSED;
    case WuPatch::STATE_COLLIDED: return RPL_STATE_COLLIDED;
    case WuPatch::STATE_BLOCKED:  return RPL_STATE_BLOCKED;
    case WuPatch::STATE_FAILED:   return RPL_STATE_FAILED;
    default:                      return RPL_STATE_DECLARED;
    }
}

int hostAddHook(const RplHost*, const RplHook* h)
{
    if (!h)
        return -1;
    if (findPatch(h) >= 0)
        return WuPatch::GetState(s_patches[findPatch(h)].handle) ==
               WuPatch::STATE_APPLIED ? 0 : -2;
    if (s_patchCount >= kMaxCemuPatches)
        return -3;
    if (h->function) {
        report("[wwhd_tools] %s: library exports cannot be hooked here",
               h->name ? h->name : "?");
        return -3;
    }

    const uint32_t site = h->linkAddr;
    const uint32_t dispatcher = (uint32_t)(uintptr_t)h->hook;
    const uint32_t apart = site > dispatcher ? site - dispatcher : dispatcher - site;
    // Cemu loads this RPL about a gigabyte from the title; a branch reaches 32 MiB.
    if (h->shape != RPL_SHAPE_REWRITE && apart > kBranchReach) {
        if (!s_rangeReported) {
            s_rangeReported = true;
            report("[wwhd_tools] dynamic hooks unavailable here: this RPL sits at "
                   "%08X and the title's text at %08X, %u MiB apart, and a branch "
                   "reaches 32 MiB. Hook points have to come from the Cemu patch "
                   "instead.", dispatcher, site,
                   (unsigned)(apart / (1024u * 1024u)));
        }
        return -5;
    }

    WuPatch::Desc d;
    memset(&d, 0, sizeof(d));
    d.owner         = "wwhd_tools";
    d.site.linkAddr = h->linkAddr;
    d.site.expected = h->expected;
    d.hook          = h->hook;
    d.replacement   = h->replacement;
    d.flags         = (h->flags & RPL_HOOK_EXCLUSIVE) ? WuPatch::FLAG_EXCLUSIVE : 0;
    switch (h->shape) {
    case RPL_SHAPE_JUMP:    d.shape = WuPatch::SHAPE_JUMP;    break;
    case RPL_SHAPE_CALL:    d.shape = WuPatch::SHAPE_CALL;    break;
    case RPL_SHAPE_REWRITE: d.shape = WuPatch::SHAPE_REWRITE; break;
    default:                return -3;
    }

    const WuPatch::Handle handle = WuPatch::Declare(d);
    if (handle == WuPatch::kInvalidHandle)
        return -3;
    WuPatch::SetEnabled(handle, true);
    WuPatch::Tick();

    const WuPatch::State st = WuPatch::GetState(handle);
    report("[wwhd_tools] dynamic %-18s %08X %s", h->name ? h->name : "?",
           (unsigned)h->linkAddr, WuPatch::StateName(st));
    if (st != WuPatch::STATE_APPLIED) {
        WuPatch::SetEnabled(handle, false);
        WuPatch::Tick();
        WuPatch::Undeclare(handle);
        return -4;
    }

    if (h->original)
        *h->original = (void*)(uintptr_t)WuPatch::GetOriginalThunk(handle);
    s_patches[s_patchCount].hook = h;
    s_patches[s_patchCount].handle = handle;
    ++s_patchCount;
    return 0;
}

int hostRemoveHook(const RplHost*, const RplHook* h)
{
    const int i = findPatch(h);
    if (i < 0)
        return -1;
    WuPatch::SetEnabled(s_patches[i].handle, false);
    WuPatch::Tick();
    WuPatch::Undeclare(s_patches[i].handle);
    s_patches[i] = s_patches[--s_patchCount];
    return 0;
}

void* hostAlloc(const RplHost*, uint32_t size, uint32_t align)
{
    return memalign(align < 4 ? 4 : align, size);
}

void hostFree(const RplHost*, void* p) { free(p); }

uint64_t hostTitleId(const RplHost*)   { return OSGetTitleID(); }
uint32_t hostTextDelta(const RplHost*) { return 0; }
uint32_t hostDataDelta(const RplHost*) { return 0; }

int hostGetBool(const RplHost*, const char*, int def) { return def; }
int hostSetBool(const RplHost*, const char*, int)     { return -1; }

int hostPad(const RplHost*, RplPad* out)
{
    if (!out || !s_havePad)
        return 0;
    *out = s_pad;
    return 1;
}

int hostKpad(const RplHost*, uint32_t chan, RplKpad* out)
{
    if (!out || chan >= RPL_KPAD_CHANNELS || !s_haveKpad[chan])
        return 0;
    *out = s_kpad[chan];
    return 1;
}

void hostSetInputMode(const RplHost*, int mode)
{
    s_inputMode = mode == RPL_INPUT_BLOCK ? RPL_INPUT_BLOCK : RPL_INPUT_PASS;
}

void hostSetStick(const RplHost*, const float* leftXY)
{
    if (!leftXY) {
        s_stickHeld = false;
        return;
    }
    s_stickX = leftXY[0];
    s_stickY = leftXY[1];
    s_stickHeld = true;
}

void buildHost()
{
    memset(&s_host, 0, sizeof(s_host));
    s_host.version      = RPL_ABI_VERSION;
    s_host.name         = "wwhd_tools";
    s_host.dir          = "fs:/vol/external01/wwhd_tools/";
    s_host.impl         = nullptr;
    s_host.log          = hostLog;
    s_host.notify       = hostNotify;
    s_host.original     = hostOriginal;
    s_host.state        = hostState;
    s_host.addHook      = hostAddHook;
    s_host.removeHook   = hostRemoveHook;
    s_host.alloc        = hostAlloc;
    s_host.free         = hostFree;
    s_host.titleId      = hostTitleId;
    s_host.textDelta    = hostTextDelta;
    s_host.dataDelta    = hostDataDelta;
    s_host.getBool      = hostGetBool;
    s_host.setBool      = hostSetBool;
    s_host.pad          = hostPad;
    s_host.kpad         = hostKpad;
    s_host.setInputMode = hostSetInputMode;
    s_host.setStick     = hostSetStick;
}

bool s_initialising = false;

struct PackBlock {
    uint32_t drawAddr;
    uint32_t drawIdxAddr;
    uint32_t drawStub;
    uint32_t drawIdxStub;
    uint32_t modeStats[48];
    uint32_t version;
    uint32_t copyStub;
};

bool s_drawSitesDone = false;

uint32_t resolveGx2Export(const char* name)
{
    OSDynLoad_Module mod = nullptr;
    if (OSDynLoad_Acquire("gx2", &mod) != OS_DYNLOAD_OK || !mod)
        return 0;
    void* addr = nullptr;
    const OSDynLoad_Error err =
        OSDynLoad_FindExport(mod, OS_DYNLOAD_EXPORT_FUNC, name, &addr);
    OSDynLoad_Release(mod);
    return err == OS_DYNLOAD_OK ? (uint32_t)(uintptr_t)addr : 0u;
}

// Keeps each site link bit, so bl calls and tail-call b sites both survive the redirect.
uint32_t retargetBranches(uint32_t target, uint32_t stub,
                          uint32_t textAddr, uint32_t textSize)
{
    if (!target || !stub || !textAddr || textSize < 4)
        return 0;

    uint32_t* const words = (uint32_t*)(uintptr_t)textAddr;
    const uint32_t count = textSize / 4u;
    uint32_t patched = 0;

    for (uint32_t i = 0; i < count; ++i) {
        const uint32_t w = words[i];
        if ((w & 0xFC000002u) != 0x48000000u)
            continue;

        int32_t disp = (int32_t)(w & 0x03FFFFFCu);
        if (disp & 0x02000000)
            disp -= 0x04000000;

        const uint32_t pc = textAddr + i * 4u;
        if ((uint32_t)((int32_t)pc + disp) != target)
            continue;

        const int32_t to = (int32_t)stub - (int32_t)pc;
        if (to > 0x01FFFFFC || to < -0x02000000)
            continue;

        words[i] = 0x48000000u | ((uint32_t)to & 0x03FFFFFCu) | (w & 1u);
        ++patched;
    }

    if (patched) {
        DCFlushRange(words, textSize);
        ICInvalidateRange(words, textSize);
    }
    return patched;
}

void tryPatchDrawSites(void* blockPtr)
{
    if (s_drawSitesDone || !blockPtr)
        return;

    PackBlock* block = (PackBlock*)blockPtr;
    if (!block->drawStub || !block->drawIdxStub)
        return;

    const uint32_t textAddr = WuPatch::Apply::ModuleTextAddr();
    const uint32_t textSize = WuPatch::Apply::ModuleTextSize();
    if (!textAddr || !textSize)
        return;

    s_drawSitesDone = true;

    block->drawAddr = resolveGx2Export("GX2DrawEx");
    block->drawIdxAddr = resolveGx2Export("GX2DrawIndexedEx");
    DCFlushRange(block, sizeof(PackBlock));

    const uint32_t a = retargetBranches(block->drawAddr, block->drawStub,
                                        textAddr, textSize);
    const uint32_t b = retargetBranches(block->drawIdxAddr, block->drawIdxStub,
                                        textAddr, textSize);
    // Copy sites stay with the pack: retargeting them at run time is the suspect for a Cemu hang.
    report("[wwhd_tools] draw sites in %08X+%08X: GX2DrawEx %08X -> %08X, %u site(s); "
           "GX2DrawIndexedEx %08X -> %08X, %u site(s); pack block v%u",
           textAddr, textSize, block->drawAddr, block->drawStub, a,
           block->drawIdxAddr, block->drawIdxStub, b, (unsigned)block->version);
}

bool ensureReady()
{
    if (s_ready)
        return true;
    if (s_failed || s_initialising)
        return false;
    s_initialising = true;

    App::g_underCemu = true;

    const RplManifest* manifest = rpl_manifest();
    if (!manifest || !manifest->onInit) {
        s_failed = true;
        s_initialising = false;
        return false;
    }

    buildHost();
    const int rc = manifest->onInit(&s_host);
    if (rc != 0) {
        report("[wwhd_tools] onInit returned %d under Cemu", rc);
        s_failed = true;
        return false;
    }

    s_ready = true;
    return true;
}

void copyToScanBuffer(const GX2ColorBuffer* buffer, GX2ScanTarget target)
{
    GX2CopyColorBufferToScanBuffer(buffer, target);
}

// Same-frame block decision, between publish and edit, as the loader dispatches onPadSampled.
void decideAtSample(const char* source, uint32_t hold)
{
    const int before = s_inputMode;
    ::Ui::Overlay::OnPadSampled();
    if (s_inputMode != before)
        report("[wwhd_tools] %s: game input %s at the sample, hold=%08X", source,
               s_inputMode == RPL_INPUT_BLOCK ? "blocked" : "released",
               (unsigned)hold);
}

void noteFrameReads()
{
    if (s_vpadReads != s_vpadReadsLogged && s_vpadRateLogs < 8) {
        ++s_vpadRateLogs;
        s_vpadReadsLogged = s_vpadReads;
        report("[wwhd_tools] %u VPAD read(s) in the last game frame",
               (unsigned)s_vpadReads);
    }
    s_vpadReads = 0;
}

void noteVpad(VPADStatus* buffers, uint32_t count)
{
    if (!buffers || count == 0)
        return;

    if (!s_vpadLogged) {
        s_vpadLogged = true;
        report("[wwhd_tools] first VPAD reason: count=%u hold=%08X",
               (unsigned)count, (unsigned)buffers[0].hold);
    }

    const VPADStatus& st = buffers[0];
    s_pad.hold    = st.hold;
    s_pad.trigger = st.trigger;
    s_pad.release = st.release;
    s_pad.leftX   = st.leftStick.x;
    s_pad.leftY   = st.leftStick.y;
    s_pad.rightX  = st.rightStick.x;
    s_pad.rightY  = st.rightStick.y;
    s_pad.touch.x        = st.tpNormal.x;
    s_pad.touch.y        = st.tpNormal.y;
    s_pad.touch.touched  = st.tpNormal.touched;
    s_pad.touch.validity = st.tpNormal.validity;
    s_pad.sample += 1;
    s_havePad = true;

    decideAtSample("vpad", s_pad.hold);

    ++s_vpadReads;
    if (s_stickHeld != s_vpadStickApplied) {
        s_vpadStickApplied = s_stickHeld;
        report("[wwhd_tools] vpad: stick override %s (x=%.2f y=%.2f), read %u of the frame",
               s_stickHeld ? "on" : "off", s_stickX, s_stickY, (unsigned)s_vpadReads);
    }

    const bool block = s_inputMode == RPL_INPUT_BLOCK;
    if (!block && !s_stickHeld)
        return;

    for (uint32_t i = 0; i < count; ++i) {
        VPADStatus& s = buffers[i];
        if (block) {
            s.hold = s.trigger = s.release = 0;
            s.rightStick.x = s.rightStick.y = 0.0f;
            s.tpNormal.touched = 0;
            s.tpFiltered1.touched = 0;
            s.tpFiltered2.touched = 0;
        }
        if (s_stickHeld) {
            s.leftStick.x = s_stickX;
            s.leftStick.y = s_stickY;
        } else if (block) {
            s.leftStick.x = s.leftStick.y = 0.0f;
        }
    }
}

void noteKpad(KPADStatus* buffers, uint32_t count, uint32_t chan)
{
    if (!buffers || count == 0 || chan >= RPL_KPAD_CHANNELS)
        return;

    if (!s_kpadLogged) {
        s_kpadLogged = true;
        report("[wwhd_tools] first KPAD reason: chan=%u count=%u ext=%u",
               (unsigned)chan, (unsigned)count,
               (unsigned)buffers[0].extensionType);
    }

    const KPADStatus& st = buffers[0];
    RplKpad k;
    memset(&k, 0, sizeof(k));
    k.extension = (uint32_t)st.extensionType;

    if (st.extensionType == WPAD_EXT_PRO_CONTROLLER) {
        k.hold    = st.pro.hold;
        k.trigger = st.pro.trigger;
        k.release = st.pro.release;
        k.leftX   = st.pro.leftStick.x;
        k.leftY   = st.pro.leftStick.y;
        k.rightX  = st.pro.rightStick.x;
        k.rightY  = st.pro.rightStick.y;
    } else if (st.extensionType == WPAD_EXT_CLASSIC ||
               st.extensionType == WPAD_EXT_MPLUS_CLASSIC) {
        k.hold    = st.classic.hold;
        k.trigger = st.classic.trigger;
        k.release = st.classic.release;
        k.leftX   = st.classic.leftStick.x;
        k.leftY   = st.classic.leftStick.y;
        k.rightX  = st.classic.rightStick.x;
        k.rightY  = st.classic.rightStick.y;
    }

    k.sample = s_kpad[chan].sample + 1;
    s_kpad[chan] = k;
    // Cemu keeps every configured controller live; an idle one is published as absent, as hardware does.
    const bool idle = k.hold == 0 && k.leftX > -0.1f && k.leftX < 0.1f && k.leftY > -0.1f &&
                      k.leftY < 0.1f && k.rightX > -0.1f && k.rightX < 0.1f &&
                      k.rightY > -0.1f && k.rightY < 0.1f;
    s_haveKpad[chan] = !idle;

    decideAtSample("kpad", k.hold);

    if (s_stickHeld != s_kpadStickApplied) {
        s_kpadStickApplied = s_stickHeld;
        report("[wwhd_tools] kpad: stick override %s (x=%.2f y=%.2f), ext=%u",
               s_stickHeld ? "on" : "off", s_stickX, s_stickY, (unsigned)k.extension);
    }

    const bool block = s_inputMode == RPL_INPUT_BLOCK;
    if (!block && !s_stickHeld)
        return;

    for (uint32_t i = 0; i < count; ++i) {
        KPADStatus& s = buffers[i];
        if (block) {
            s.hold = s.trigger = s.release = 0;
            s.pro.hold = s.pro.trigger = s.pro.release = 0;
            s.pro.rightStick.x = s.pro.rightStick.y = 0.0f;
            s.classic.hold = s.classic.trigger = s.classic.release = 0;
            s.classic.rightStick.x = s.classic.rightStick.y = 0.0f;
            s.nunchuk.hold = s.nunchuk.trigger = s.nunchuk.release = 0;
        }
        if (s_stickHeld) {
            s.pro.leftStick.x = s_stickX;
            s.pro.leftStick.y = s_stickY;
            s.classic.leftStick.x = s_stickX;
            s.classic.leftStick.y = s_stickY;
        } else if (block) {
            s.pro.leftStick.x = s.pro.leftStick.y = 0.0f;
            s.classic.leftStick.x = s.classic.leftStick.y = 0.0f;
            s.nunchuk.stick.x = s.nunchuk.stick.y = 0.0f;
        }
    }
}
}
}
}

RPL_EXPORT uint32_t rpl_cemu_entry(uint32_t reason, void* a, void* b, void* c)
{
    if (!App::Cemu::ensureReady())
        return 0;

    switch (reason) {
    case RPL_CEMU_FRAME:
        App::Cemu::tryPatchDrawSites(a);
        FrameStats::TakePackDraws(a ? (uint8_t*)a + 16 : nullptr);
        App::Cemu::noteFrameReads();
        App::Present::OnGameFrame();
        return 1;

    case RPL_CEMU_PRESENT:
        App::Present::OnCopyToScanBuffer(App::Cemu::copyToScanBuffer,
                                         (const GX2ColorBuffer*)a,
                                         (GX2ScanTarget)(uintptr_t)b);
        return 1;

    case RPL_CEMU_VPAD:
        App::Cemu::noteVpad((VPADStatus*)a, (uint32_t)(uintptr_t)b);
        return 1;

    case RPL_CEMU_CONTEXT:
        App::Present::NoteContext((GX2ContextState*)a);
        return 1;

    case RPL_CEMU_KPAD:
        App::Cemu::noteKpad((KPADStatus*)a, (uint32_t)(uintptr_t)b,
                            (uint32_t)(uintptr_t)c);
        return 1;

    case RPL_CEMU_EXEC_BEGIN:
        Cheats::Text::OnFrameEarly();
        Tools::FlyCam::OnFrameEarly();
        FrameStats::OnExecuteBegin();
        return 1;

    case RPL_CEMU_EXEC_END:
        FrameStats::OnExecuteEnd();
        return 1;

    case RPL_CEMU_MSGBOX:
        Cheats::Text::OnBoxInput((dMsgBox_c*)a);
        return 1;

    case RPL_CEMU_CCS_BEFORE:
        Hud::Collision::OnBeforeMove(a);
        return 1;

    case RPL_CEMU_CCS_AFTER:
        Hud::Collision::OnAfterMove(a);
        return 1;

    case RPL_CEMU_CAM_RUN:
        Hud::Collision::OnCameraRun(a);
        return 1;

    case RPL_CEMU_MASS_CHK:
        Hud::Collision::OnMassCheck(a, b, (unsigned)(uintptr_t)c);
        return 1;

    case RPL_CEMU_DRAW_BEGIN:
        App::Cemu::s_hidPlayerFlash = Cheats::Status::BeginPlayerDraw(a);
        return 1;

    case RPL_CEMU_DRAW_END:
        if (App::Cemu::s_hidPlayerFlash) {
            App::Cemu::s_hidPlayerFlash = false;
            Cheats::Status::EndPlayerDraw(a);
        }
        return 1;

    case RPL_CEMU_FOOT_MOVE:
        Cheats::Movement::OnBeforePosMove(a);
        return 1;

    case RPL_CEMU_ACTOR_MOVE:
        Cheats::Movement::OnBeforeActorPosMove(a);
        return 1;

    case RPL_CEMU_PROC_MOVE:
        Cheats::Movement::OnMoveProc(a);
        return 1;

    case RPL_CEMU_PROC_CRAWL:
        Cheats::Movement::OnCrawlProc(a);
        return 1;

    case RPL_CEMU_PROC_SWIM:
        Cheats::Movement::OnSwimProc(a);
        return 1;

    case RPL_CEMU_GX2_COLOR:
        GBuffer::NoteColor((const GX2ColorBuffer*)a, (int)(uintptr_t)b);
        return 1;

    case RPL_CEMU_GX2_DEPTH:
        GBuffer::NoteDepth((const GX2DepthBuffer*)a);
        return 1;
    }
    return 0;
}
