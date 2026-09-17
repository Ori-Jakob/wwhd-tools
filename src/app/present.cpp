#include "app/present.h"

#include "core/frame_stats.h"
#include "core/logger.h"
#include "core/settings.h"
#include "hud/hud_collision.h"
#include "render/gbuffer.h"
#include "render/renderer.h"
#include "render/scene_depth.h"
#include "ui/overlay.h"

#include "libwwhd/libwwhd.h"

#include <coreinit/cache.h>
#include <coreinit/debug.h>
#include <coreinit/time.h>
#include <gx2/context.h>
#include <gx2/mem.h>
#include <gx2/registers.h>
#include <gx2/state.h>
#include <gx2/surface.h>
#include <malloc.h>
#include <string.h>

namespace App {
namespace Present {
static const float kLogicalWidth  = 1920.0f;
static const float kLogicalHeight = 1080.0f;
static const uint32_t kSrgbBit = 0x400u;

static volatile bool s_active = false;
static bool s_logStarted = false;
static bool s_drawLogged = false;
static uint32_t s_gameFrames = 0;
static bool s_beginLogged = false;
static bool s_contextLogged = false;

static GX2ContextState* s_overlayContext = nullptr;
static GX2ContextState* s_gameContext = nullptr;
static bool s_overlayContextReady = false;
static uint32_t s_blockedFrames = 0;

static bool s_frameAttempted = false;
static bool s_framePrepared = false;
static bool s_drawnTarget[2] = {};

static void resetPresentationFrame()
{
    s_frameAttempted = false;
    s_framePrepared = false;
    s_drawnTarget[0] = false;
    s_drawnTarget[1] = false;
}

// The game presents DRC then TV from one image; draw at most once per target per frame.
static int targetSlot(GX2ScanTarget target)
{
    return target == GX2_SCAN_TARGET_TV ? 0 : 1;
}

#ifdef WWHD_TOOLS_DEBUG
static uint64_t s_tBegin = 0, s_tPrepare = 0, s_tEnd = 0;
static uint64_t s_tDraw[2] = {};
static unsigned s_tDraws[2] = {};
static unsigned s_tFrames = 0;
static unsigned s_tSamples = 0;

struct ScopeTimer {
    uint64_t* sink;
    OSTime    start;
    explicit ScopeTimer(uint64_t* s) : sink(s), start(OSGetSystemTime()) {}
    ~ScopeTimer() { *sink += (uint64_t)(OSGetSystemTime() - start); }
};
#define WWHD_TIME(sink) ScopeTimer wwhdScopeTimer_(&(sink))
#else
#define WWHD_TIME(sink) ((void)0)
#endif

static bool setupOverlayContext()
{
    if (s_overlayContextReady)
        return true;
    if (!s_overlayContext)
        return false;

    Logger::Log("GX2SetupContextStateEx on %p (%u bytes)", s_overlayContext,
                (unsigned)sizeof(GX2ContextState));
    GX2SetupContextStateEx(s_overlayContext, GX2_TRUE);
    Logger::Log("GX2SetupContextStateEx returned");
    DCInvalidateRange(s_overlayContext, sizeof(GX2ContextState));
    s_overlayContextReady = true;

    GX2SetContextState(s_overlayContext);
    GX2SetDefaultState();
    return true;
}

static GX2ContextState* beginOverlayDraw()
{
    GX2ContextState* restoreContext = s_gameContext;
    if (!restoreContext || !setupOverlayContext())
        return nullptr;

    GBuffer::SetIgnore(true);
    FrameStats::SetIgnore(true);
    GX2SetContextState(s_overlayContext);
    if (!s_beginLogged) {
        s_beginLogged = true;
        Logger::Log("overlay context bound, restoring to %p later", restoreContext);
    }
    return restoreContext;
}

static void endOverlayDraw(GX2ContextState* restoreContext)
{
    FrameStats::OnOverlayGpuEnd();
    GX2Flush();
    GX2SetContextState(restoreContext);
    GBuffer::SetIgnore(false);
    FrameStats::SetIgnore(false);
}

static const int kTargetCache = 8;
struct TargetCache {
    GX2ColorBuffer buf;
    const void*    image;
    uint32_t       width, height, format, tileMode, swizzle, aa;
    bool           linear;
    bool           valid;
};
static TargetCache s_targetCache[kTargetCache];
static int         s_targetCacheNext;

static void forgetTargets()
{
    for (int i = 0; i < kTargetCache; ++i)
        s_targetCache[i].valid = false;
    s_targetCacheNext = 0;
}

static bool rebuildColorBuffer(const GX2ColorBuffer* source,
                               GX2ColorBuffer* rebuilt,
                               bool linearView)
{
    if (!source || !rebuilt || !source->surface.image ||
        source->surface.width == 0 || source->surface.height == 0)
        return false;

    for (int i = 0; i < kTargetCache; ++i) {
        const TargetCache& c = s_targetCache[i];
        if (c.valid && c.linear == linearView &&
            c.image == source->surface.image &&
            c.width == source->surface.width &&
            c.height == source->surface.height &&
            c.format == (uint32_t)source->surface.format &&
            c.tileMode == (uint32_t)source->surface.tileMode &&
            c.swizzle == source->surface.swizzle &&
            c.aa == (uint32_t)source->surface.aa) {
            *rebuilt = c.buf;
            return true;
        }
    }

    memset(rebuilt, 0, sizeof(*rebuilt));
    rebuilt->surface.dim = source->surface.dim;
    rebuilt->surface.width = source->surface.width;
    rebuilt->surface.height = source->surface.height;
    rebuilt->surface.depth = source->surface.depth;
    rebuilt->surface.mipLevels = 1;

    const uint32_t sourceFormat = (uint32_t)source->surface.format;
    rebuilt->surface.format =
        linearView ? (GX2SurfaceFormat)(sourceFormat & ~kSrgbBit)
                   : source->surface.format;
    rebuilt->surface.aa = source->surface.aa;
    rebuilt->surface.use = GX2_SURFACE_USE_TEXTURE_COLOR_BUFFER_TV;
    rebuilt->surface.tileMode = source->surface.tileMode;
    rebuilt->surface.swizzle = source->surface.swizzle;
    rebuilt->viewNumSlices = 1;
    rebuilt->aaBuffer = source->aaBuffer;
    rebuilt->aaSize = source->aaSize;

    GX2CalcSurfaceSizeAndAlignment(&rebuilt->surface);
    GX2InitColorBufferRegs(rebuilt);
    rebuilt->surface.image = source->surface.image;

    TargetCache& c = s_targetCache[s_targetCacheNext];
    s_targetCacheNext = (s_targetCacheNext + 1) % kTargetCache;
    c.buf = *rebuilt;
    c.image = source->surface.image;
    c.width = source->surface.width;
    c.height = source->surface.height;
    c.format = (uint32_t)source->surface.format;
    c.tileMode = (uint32_t)source->surface.tileMode;
    c.swizzle = source->surface.swizzle;
    c.aa = (uint32_t)source->surface.aa;
    c.linear = linearView;
    c.valid = true;
    return true;
}

void Init()
{
    if (!s_overlayContext)
        s_overlayContext = (GX2ContextState*)memalign(GX2_CONTEXT_STATE_ALIGNMENT,
                                                      sizeof(GX2ContextState));
    s_overlayContextReady = false;
    s_gameContext = nullptr;
    s_logStarted = false;
    s_drawLogged = false;
    s_beginLogged = false;
    s_contextLogged = false;
    s_gameFrames = 0;
    s_blockedFrames = 0;
    resetPresentationFrame();

    OSMemoryBarrier();
    s_active = s_overlayContext != nullptr;
    if (!s_overlayContext)
        OSReport("[wwhd_tools_rpl] no memory for an overlay context state\n");
}

void Shutdown()
{
    s_active = false;
    OSMemoryBarrier();
}

static unsigned s_presentCount[2];
static int      s_firstSlot = -1;
static unsigned s_patternLogs = 0;
static uint32_t s_lastPattern = 0xFFFFFFFFu;

// Logged when the per-frame present pattern changes: which screens, how often, which first.
static void notePresentPattern()
{
    const uint32_t pattern = (s_presentCount[0] << 16) | (s_presentCount[1] << 8) |
                             (uint32_t)(s_firstSlot + 1);
    if (pattern != s_lastPattern && s_patternLogs < 24) {
        ++s_patternLogs;
        s_lastPattern = pattern;
        Logger::Log("presents per frame: tv=%u drc=%u, first=%s, display mode %u",
                    s_presentCount[0], s_presentCount[1],
                    s_firstSlot == 0 ? "tv" : s_firstSlot == 1 ? "drc" : "none",
                    (unsigned)wwhd_getDisplayMode());
    }
    s_presentCount[0] = s_presentCount[1] = 0;
    s_firstSlot = -1;
}

void OnGameFrame()
{
    if (!s_active)
        return;

    ++s_gameFrames;
    FrameStats::OnPresentationFrame();

    notePresentPattern();
    resetPresentationFrame();
    GBuffer::OnFrameEnd();
    Hud::Collision::OnFrameEnd();
    const OSTime tickStart = OSGetSystemTime();
    Ui::Overlay::Tick();
    FrameStats::AddOverlayTicks((uint64_t)(OSGetSystemTime() - tickStart));
}

void OnReleaseForeground()
{
    if (!s_active)
        return;
    forgetTargets();
    Renderer::ResetDeviceObjects();
    resetPresentationFrame();
    s_gameContext = nullptr;
    s_overlayContextReady = false;
}

void OnAcquiredForeground()
{
    if (!s_active)
        return;
    forgetTargets();
    resetPresentationFrame();
    s_gameContext = nullptr;
    s_overlayContextReady = false;
}

void NoteContext(GX2ContextState* state)
{
    if (!s_active || !state || state == s_overlayContext)
        return;
    if (!s_contextLogged) {
        s_contextLogged = true;
        Logger::Log("game context %p captured", state);
    }
    s_gameContext = state;
}

void OnCopyToScanBuffer(CopyFn copy, const GX2ColorBuffer* buffer, GX2ScanTarget target)
{
    if (!copy)
        return;
    if (!s_active || !buffer ||
        (target != GX2_SCAN_TARGET_TV && target != GX2_SCAN_TARGET_DRC)) {
        copy(buffer, target);
        return;
    }

    FrameStats::OnPresent(target == GX2_SCAN_TARGET_TV);
    const OSTime overlayStart = OSGetSystemTime();

    if (!s_logStarted) {
        s_logStarted = true;
        Logger::Log("first present, %ux%u on %s, gameContext=%p",
                    (unsigned)buffer->surface.width,
                    (unsigned)buffer->surface.height,
                    target == GX2_SCAN_TARGET_TV ? "tv" : "drc", s_gameContext);
    }

    GX2ColorBuffer rebuilt;
    GX2ColorBuffer drawTarget;
    if (!rebuildColorBuffer(buffer, &rebuilt, false) ||
        !rebuildColorBuffer(buffer, &drawTarget, true)) {
        copy(buffer, target);
        return;
    }

    const Ui::Overlay::ScreenContent content =
        Ui::Overlay::ContentForScreen(target == GX2_SCAN_TARGET_TV);
    const bool wantDraw =
        Ui::Overlay::HasContentFor(content, target == GX2_SCAN_TARGET_TV);
    const int slot = targetSlot(target);
    ++s_presentCount[slot];
    if (!s_frameAttempted)
        s_firstSlot = slot;

#ifdef WWHD_TOOLS_DEBUG
    const bool firstThisFrame = !s_frameAttempted;
    bool drewThisCall = false;
#endif

    if (!s_frameAttempted) {
        s_frameAttempted = true;
#ifdef WWHD_TOOLS_DEBUG
        ++s_tFrames;
#endif
        GX2ContextState* restoreContext;
        {
            WWHD_TIME(s_tBegin);
            restoreContext = beginOverlayDraw();
        }
        if (restoreContext) {
            s_blockedFrames = 0;
            if (!s_drawLogged)
                Logger::Log("entering PrepareFrame");
            SceneDepth::Prepare(Config::g_settings.collisionView &&
                                Config::g_settings.collisionDepth);
            {
                WWHD_TIME(s_tPrepare);
                s_framePrepared =
                    Ui::Overlay::PrepareFrame(kLogicalWidth, kLogicalHeight);
            }
            if (s_framePrepared && wantDraw) {
                {
                    WWHD_TIME(s_tDraw[slot]);
                    Ui::Overlay::DrawPrepared(&drawTarget, content,
                                          target == GX2_SCAN_TARGET_TV);
                }
                s_drawnTarget[slot] = true;
#ifdef WWHD_TOOLS_DEBUG
                ++s_tDraws[slot];
                drewThisCall = true;
#endif
            }
            if (!s_drawLogged) {
                s_drawLogged = true;
                Logger::Log("first draw attempt: prepared=%d content=%d drew=%d "
                            "frames=%u", (int)s_framePrepared, (int)content,
                            (int)(s_framePrepared && wantDraw),
                            (unsigned)s_gameFrames);
            }
            {
                WWHD_TIME(s_tEnd);
                endOverlayDraw(restoreContext);
            }
        } else if (++s_blockedFrames == 60) {
            Logger::LogWarn("60 presents with nothing to draw under "
                            "(overlay=%p ready=%d game=%p)", s_overlayContext,
                            (int)s_overlayContextReady, s_gameContext);
        }
    } else if (s_framePrepared && wantDraw && !s_drawnTarget[slot]) {
        GX2ContextState* restoreContext;
        {
            WWHD_TIME(s_tBegin);
            restoreContext = beginOverlayDraw();
        }
        if (restoreContext) {
            {
                WWHD_TIME(s_tDraw[slot]);
                Ui::Overlay::DrawPrepared(&drawTarget, content,
                                          target == GX2_SCAN_TARGET_TV);
            }
            s_drawnTarget[slot] = true;
            {
                WWHD_TIME(s_tEnd);
                endOverlayDraw(restoreContext);
            }
#ifdef WWHD_TOOLS_DEBUG
            ++s_tDraws[slot];
            drewThisCall = true;
#endif
        }
    }

#ifdef WWHD_TOOLS_DEBUG
    static unsigned s_presentSeen = 0;
    if (++s_presentSeen <= 24u || (s_presentSeen % 600u) == 0u) {
        Logger::Log("present #%u %s %ux%u content=%d first=%d prepared=%d "
                    "drew=%d img=%p mode=%u",
                    s_presentSeen, target == GX2_SCAN_TARGET_TV ? "TV " : "DRC",
                    (unsigned)buffer->surface.width,
                    (unsigned)buffer->surface.height,
                    (int)content, (int)firstThisFrame, (int)s_framePrepared,
                    (int)drewThisCall, rebuilt.surface.image,
                    (unsigned)wwhd_getDisplayMode());
    }
#endif

#ifdef WWHD_TOOLS_DEBUG
    if (++s_tSamples >= 300u) {
        const unsigned f = s_tFrames ? s_tFrames : 1u;
        const unsigned dTv = s_tDraws[0] ? s_tDraws[0] : 1u;
        const unsigned dDrc = s_tDraws[1] ? s_tDraws[1] : 1u;
        Logger::Log("overlay us: begin %llu prepare %llu end %llu per frame | "
                    "draw tv %llu x%u drc %llu x%u | frames %u",
                    (unsigned long long)(OSTicksToMicroseconds(s_tBegin) / f),
                    (unsigned long long)(OSTicksToMicroseconds(s_tPrepare) / f),
                    (unsigned long long)(OSTicksToMicroseconds(s_tEnd) / f),
                    (unsigned long long)(OSTicksToMicroseconds(s_tDraw[0]) / dTv),
                    s_tDraws[0],
                    (unsigned long long)(OSTicksToMicroseconds(s_tDraw[1]) / dDrc),
                    s_tDraws[1], s_tFrames);
        s_tBegin = s_tPrepare = s_tEnd = 0;
        s_tDraw[0] = s_tDraw[1] = 0;
        s_tDraws[0] = s_tDraws[1] = 0;
        s_tFrames = 0;
        s_tSamples = 0;
    }
#endif

    FrameStats::AddOverlayTicks((uint64_t)(OSGetSystemTime() - overlayStart));
    copy(s_drawnTarget[slot] ? &rebuilt : buffer, target);
}
}
}
