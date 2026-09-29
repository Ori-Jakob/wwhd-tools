#include "tools/camera.h"

#include "core/logger.h"
#include "core/settings.h"
#include "tools/flycam.h"

#include "libwupatch/wupatch.h"
#include "libwwhd/libwwhd.h"

#include <math.h>
#include <string.h>

namespace Tools {
namespace Camera {
static const float kPitchLo   = -45.0f;
static const float kPitchHi   = 70.0f;
static const float kPitchRate = 3.0f;
static const float kNeverExit = -1.0f;
static const float kFovBlend  = 0.2f;
static const float kFovMin    = 1.0f, kFovMax = 170.0f;
static const int   kMaxManual = 48;

struct ManualStyle {
    dCamera_style_c* style;
    f32              orig[30];
};

struct Applied {
    bool  valid;
    bool  modern, sailing;
    float sensX, sensY;
};

struct State {
    bool        captured;
    int         manualCount;
    ManualStyle manual[kMaxManual];
    Applied     applied;

    dCamera_c* cam;
    float      exitOrig;
    bool       exitOwned;

    WuPatch::Handle execSwap;
    u32             realExec;
    bool            execOn;
    float           fovRatio;
};

static State s;

static bool captureManual()
{
    if (s.captured)
        return true;
    if (!wwhd_regionResolved || !wwhd_dataResolved)
        return false;
    dCamera_style_c* table = dCam_getStyleTable();
    const int count = dCam_getStyleCount();
    if (!table || count <= 0 || count > 1024)
        return false;
    for (int i = 0; i < count && s.manualCount < kMaxManual; ++i) {
        if (table[i].mAlgorithm != dCamAlg_MANUAL)
            continue;
        ManualStyle& m = s.manual[s.manualCount++];
        m.style = &table[i];
        memcpy(m.orig, table[i].mParam, sizeof(m.orig));
    }
    s.captured = true;
    Logger::Log("[camera] %d manual styles of %d", s.manualCount, count);
    return true;
}

// Stick Y only pitches, over a wide range; distance, height and fovy stay where the view was.
static void applyStyles(const Applied& a)
{
    for (int i = 0; i < s.manualCount; ++i) {
        ManualStyle& m = s.manual[i];
        f32* p = m.style->mParam;
        memcpy(p, m.orig, sizeof(m.orig));
        if (!a.modern)
            continue;
        if (m.style->mName == dCamStyle_NAME_BOAT_MANUAL && !a.sailing)
            continue;
        p[dCamManualPrm_HEIGHT_RATE] = 0.0f;
        p[dCamManualPrm_DIST_RATE]   = 0.0f;
        p[dCamManualPrm_FOVY_RATE]   = 0.0f;
        p[dCamManualPrm_PITCH_LO]    = kPitchLo;
        p[dCamManualPrm_PITCH_HI]    = kPitchHi;
        p[dCamManualPrm_PITCH_RATE]  = kPitchRate * a.sensY;
        p[dCamManualPrm_YAW_RATE]    = m.orig[dCamManualPrm_YAW_RATE] * a.sensX;
    }
}

static void syncStyles()
{
    if (!captureManual())
        return;
    const Config::Settings& cfg = Config::g_settings;
    Applied want = {};
    want.valid   = true;
    want.modern  = cfg.modernCam;
    want.sailing = cfg.modernCamSailing;
    want.sensX   = cfg.camSensX;
    want.sensY   = cfg.camSensY;
    const Applied& had = s.applied;
    if (had.valid && had.modern == want.modern && had.sailing == want.sailing &&
        had.sensX == want.sensX && had.sensY == want.sensY)
        return;
    applyStyles(want);
    if (!had.valid || had.modern != want.modern || had.sailing != want.sailing)
        Logger::Log("[camera] manual styles %s%s", want.modern ? "modern" : "stock",
                    want.modern && want.sailing ? ", sailing too" : "");
    s.applied = want;
}

static void scaleFov(u8* process)
{
    dCam_view_t* view = (dCam_view_t*)(process + WWHD_CAMPROC_OFF_VIEW);
    const Config::Settings& cfg = Config::g_settings;
    const bool gameplay = !dEvt_isEventRunning() || FlyCam::IsActive();
    const float want = (cfg.cameraFovAll || gameplay) ? cfg.cameraFov / dCam_DEFAULT_FOVY : 1.0f;
    s.fovRatio += (want - s.fovRatio) * kFovBlend;
    if (fabsf(want - s.fovRatio) < 0.001f)
        s.fovRatio = want;

    float fov = view->mFovy * s.fovRatio;
    if (fov < kFovMin) fov = kFovMin;
    if (fov > kFovMax) fov = kFovMax;
    view->mFovy = fov;
}

// view_setup refills the view every execute, so scaling after it never compounds.
static int executeHook(void* process)
{
    const dCam_procMethod_t real = (dCam_procMethod_t)(uintptr_t)s.realExec;
    const int result = real ? real(process) : 1;
    if (process)
        scaleFov((u8*)process);
    return result;
}

static WuPatch::Handle execSwap()
{
    const wwhd_addr_t slot = dCam_getProcMethodSlotAddr(WWHD_CAMPROC_METHOD_EXECUTE);
    if (s.execSwap == WuPatch::kInvalidHandle && slot) {
        WuPatch::Data::SwapDesc d = {};
        d.owner      = "Camera";
        d.linkAddr   = slot;
        d.stride     = 4;
        d.count      = 1;
        d.wordOffset = 0;
        d.value      = (uint32_t)(uintptr_t)&executeHook;
        s.execSwap   = WuPatch::Data::Declare(d);
    }
    return s.execSwap;
}

static void setFovHook(bool on)
{
    if (on == s.execOn)
        return;
    if (on) {
        if (!wwhd_dataResolved || execSwap() == WuPatch::kInvalidHandle)
            return;
        const u32 current = dCam_getProcMethod(WWHD_CAMPROC_METHOD_EXECUTE);
        if (current != (u32)(uintptr_t)&executeHook)
            s.realExec = current;
        if (!s.realExec)
            return;
        s.fovRatio = 1.0f;
        WuPatch::Data::SetEnabled(s.execSwap, true);
        Logger::Log("[camera] fov hook on, execute %08X", (unsigned)s.realExec);
    } else {
        WuPatch::Data::SetEnabled(s.execSwap, false);
        Logger::Log("[camera] fov hook off");
    }
    s.execOn = on;
}

void Tick()
{
    if (!wwhd_regionResolved)
        return;
    syncStyles();
    const float fov = Config::g_settings.cameraFov;
    setFovHook(fabsf(fov - dCam_DEFAULT_FOVY) > 0.05f);
}

// Releasing the stick drops back to the follow camera unless the exit distance is unreachable.
void OnCameraRun(void* camera)
{
    dCamera_c* cam = (dCamera_c*)camera;
    if (!cam || cam->mPlayerIdx != 0)
        return;
    const Config::Settings& cfg = Config::g_settings;
    const bool hold = cfg.modernCam && (cfg.modernCamSailing || !daPy_isRidingShip());

    if (cam != s.cam) {
        s.cam = cam;
        s.exitOwned = false;
    }
    if (s.exitOwned && cam->mManualExitDist != kNeverExit)
        s.exitOwned = false;

    if (hold) {
        if (!s.exitOwned) {
            s.exitOrig = cam->mManualExitDist;
            s.exitOwned = true;
            Logger::Log("[camera] holding manual mode (exit distance was %.1f)",
                        (double)s.exitOrig);
        }
        cam->mManualExitDist = kNeverExit;
    } else if (s.exitOwned) {
        cam->mManualExitDist = s.exitOrig;
        s.exitOwned = false;
        Logger::Log("[camera] manual mode released");
    }
}

void OnApplicationStart()
{
    s = State();
    s.execSwap = WuPatch::kInvalidHandle;
    s.fovRatio = 1.0f;
}

void OnApplicationEnd()
{
    if (s.captured) {
        Applied stock = {};
        applyStyles(stock);
    }
    if (s.execSwap != WuPatch::kInvalidHandle)
        WuPatch::Data::SetEnabled(s.execSwap, false);
    s.execOn = false;
    s.applied.valid = false;
    s.cam = nullptr;
    s.exitOwned = false;
}
}
}
