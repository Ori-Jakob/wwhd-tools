#include "tools/flycam.h"

#include "core/config.h"
#include "core/hotkeys.h"
#include "core/input.h"
#include "core/logger.h"
#include "core/settings.h"
#include "ui/notifications.h"

#include "libwupatch/wupatch.h"
#include "libwwhd/libwwhd.h"

#include <math.h>
#include <stddef.h>

namespace Tools {
namespace FlyCam {
static const char  kNotifyKey[] = "flycam";
static const float kSpeedMin = 5.0f, kSpeedMax = 500.0f, kSpeedStep = 2.0f;
static const float kTurnRate = 0.08f, kPitchLimit = 1.3705f, kTargetDist = 40.0f;
static const float kFloorFarBelow = 1.0e30f, kPi = 3.14159265f, kTeleportAhead = 100.0f;
static const int   kArmFrames = 60, kLeaveFrames = 3;
static const u32   kFreezeButton = Input::BTN_DOWN;
static const u16   kStyleBumpBits = dCamStyleFlag_BUMP_FULL | dCamStyleFlag_BUMP_BASIC |
                                    dCamStyleFlag_LOCKON_SIGHT;

enum Phase { PHASE_OFF = 0, PHASE_ARMING, PHASE_FLYING, PHASE_LEAVING };

struct Pose   { cXyz eye; float yaw, pitch; };
struct Freeze { bool holding; u8 savedMode; u32 savedPt1, savedPt2; bool endPending, cutsceneToasted; };

struct State {
    Phase phase; int frames;
    bool swapOn; WuPatch::Handle swap;
    bool cameraOwned; u32 savedCameraPlay;
    bool restoreView, leaveDone;
    Pose pose; cXyz savedEye, savedCenter; float speed;
    bool frozen; Freeze freeze;
    dCamera_style_c* heldStyle; u16 heldFlags;
    bool origCaptured; dCam_modeFn_t origFn[dCamAlg_MAX];
};

static State s;

static float clampf(float v, float lo, float hi) { return v < lo ? lo : v > hi ? hi : v; }

static void fail(const char* message)
{
    Notifications::ShowKeyed(kNotifyKey, Notifications::Error, "Fly Cam", message);
    Logger::LogError("[flycam] %s", message);
}

static void toast(Notifications::Kind kind, const char* message)
{
    Notifications::ShowKeyed(kNotifyKey, kind, "Fly Cam", message);
}

static void forwardOf(const Pose& p, cXyz* fwd, cXyz* right)
{
    const float cy = cosf(p.yaw), sy = sinf(p.yaw), cp = cosf(p.pitch);
    fwd->x = cy * cp;  fwd->y = sinf(p.pitch);  fwd->z = sy * cp;
    right->x = sy;     right->y = 0.0f;         right->z = -cy;
}

static void restoreStyle()
{
    if (s.heldStyle)
        s.heldStyle->mFlags = s.heldFlags;
    s.heldStyle = nullptr;
}

static void holdStyle(dCamera_c* cam)
{
    dCamera_style_c* style = dCam_getCurStyle(cam);
    if (style != s.heldStyle) {
        restoreStyle();
        if (!style)
            return;
        s.heldStyle = style;
        s.heldFlags = style->mFlags;
    }
    style->mFlags = (u16)(s.heldFlags & ~kStyleBumpBits);
}

static void seedPose(const dCamera_c* cam)
{
    s.savedEye = cam->mEye;
    s.savedCenter = cam->mCenter;
    s.pose.eye = cam->mEye;
    const float dx = cam->mCenter.x - cam->mEye.x;
    const float dy = cam->mCenter.y - cam->mEye.y;
    const float dz = cam->mCenter.z - cam->mEye.z;
    const float len = sqrtf(dx * dx + dy * dy + dz * dz);
    s.pose.yaw = (dx != 0.0f || dz != 0.0f) ? atan2f(dz, dx) : 0.0f;
    s.pose.pitch = len > 0.0001f ? clampf(asinf(clampf(dy / len, -1.0f, 1.0f)),
                                          -kPitchLimit, kPitchLimit)
                                 : 0.0f;
    Logger::Log("[flycam] flying from (%.0f %.0f %.0f)",
                (double)s.pose.eye.x, (double)s.pose.eye.y, (double)s.pose.eye.z);
}

static int modeFn(dCamera_c* cam, int styleIdx);

static bool captureOriginals()
{
    if (s.origCaptured)
        return true;
    const dCamera_algEntry_c* t = dCam_getAlgTable();
    if (!t)
        return false;
    for (int i = 0; i < dCamAlg_MAX; ++i) {
        const wwhd_gptr_t fn = t[i].mFn;
        if (!fn || fn == (wwhd_gptr_t)(uintptr_t)&modeFn)
            return false;
        s.origFn[i] = (dCam_modeFn_t)(uintptr_t)fn;
    }
    s.origCaptured = true;
    return true;
}

/* The game's mode function for the style Run is dispatching, resolved the way
 * Run does it: mStyleIdx -> style record -> mAlgorithm -> table slot. */
static dCam_modeFn_t originalFor(const dCamera_c* cam, int* algOut)
{
    const dCamera_style_c* style = dCam_getStyle(cam->mStyleIdx);
    if (!style)
        style = dCam_getCurStyle(const_cast<dCamera_c*>(cam));
    const int alg = style ? style->mAlgorithm : -1;
    if (algOut)
        *algOut = alg;
    if (!s.origCaptured || alg < 0 || alg >= dCamAlg_MAX)
        return nullptr;
    return s.origFn[alg];
}

/* The last dispatch we answer. Run has already run its type/mode/style
 * machine for this frame, and releasing mCameraPlay usually made it pick a new
 * mode, whose one init frame (mModeFrame == 0) is this very call. Answering it
 * ourselves would leave the game's function to blend from a work block it
 * never initialised (see dCam_restartMode in d_camera.h), so restart the mode
 * and let the real function take the frame from the view we hand it. */
static int handBack(dCamera_c* cam, int styleIdx)
{
    restoreStyle();
    if (s.restoreView) {
        cam->mWorkEye = s.savedEye;
        cam->mWorkCenter = s.savedCenter;
    }
    dCam_restartMode(cam);

    int alg = -1;
    const dCam_modeFn_t fn = originalFor(cam, &alg);
    Logger::Log("[flycam] hand-back: style %d alg %d fn %08X frame %d",
                (int)cam->mStyleIdx, alg, (unsigned)(uintptr_t)fn, (int)cam->mModeFrame);

    s.leaveDone = true;
    s.phase = PHASE_OFF;
    if (!fn) {
        cam->mModeFrame = -1;
        return 1;
    }
    return fn(cam, styleIdx);
}

static int modeFn(dCamera_c* cam, int styleIdx)
{
    if (s.phase == PHASE_ARMING) {
        seedPose(cam);
        s.phase = PHASE_FLYING;
    }
    if (s.phase == PHASE_FLYING) {
        cam->mCalcFlags = 0;
        cam->mFloorY = -kFloorFarBelow;
        holdStyle(cam);
        cXyz fwd, right;
        forwardOf(s.pose, &fwd, &right);
        cam->mWorkEye = s.pose.eye;
        cam->mWorkCenter.x = s.pose.eye.x + fwd.x * kTargetDist;
        cam->mWorkCenter.y = s.pose.eye.y + fwd.y * kTargetDist;
        cam->mWorkCenter.z = s.pose.eye.z + fwd.z * kTargetDist;
        return 1;
    }
    if (s.phase == PHASE_LEAVING)
        return handBack(cam, styleIdx);

    const dCam_modeFn_t fn = originalFor(cam, nullptr);
    return fn ? fn(cam, styleIdx) : 1;
}

static WuPatch::Handle swapHandle()
{
    const wwhd_addr_t table = dCam_getAlgTableAddr();
    if (s.swap == WuPatch::kInvalidHandle && table) {
        WuPatch::Data::SwapDesc d = {};
        d.owner      = "Fly Cam";
        d.linkAddr   = table;
        d.stride     = (uint32_t)sizeof(dCamera_algEntry_c);
        d.count      = (uint16_t)dCamAlg_MAX;
        d.wordOffset = (uint16_t)offsetof(dCamera_algEntry_c, mFn);
        d.value      = (uint32_t)(uintptr_t)&modeFn;
        s.swap = WuPatch::Data::Declare(d);
    }
    return s.swap;
}

static void freezeHold()
{
    dEvt_control_c* e = dComIfGp_getEvent();
    if (!e)
        return;

    Freeze& f = s.freeze;
    const u8 mode = e->mMode;
    if (mode == dEvtMode_DEMO_e || mode == dEvtMode_COMPULSORY_e) {
        if (f.holding) {
            f.holding = false;
            f.endPending = false;
            Logger::Log("[flycam] freeze lost to event mode %u", (unsigned)mode);
        } else if (!f.cutsceneToasted) {
            f.cutsceneToasted = true;
            toast(Notifications::Info, "Cutscene keeps playing");
        }
        return;
    }

    if (!f.holding) {
        f.savedMode = mode;
        dEvt_getPartners(&f.savedPt1, &f.savedPt2);
        f.holding = true;
        Logger::Log("[flycam] freeze hold: mode %u partners %08X %08X",
                    (unsigned)mode, (unsigned)f.savedPt1, (unsigned)f.savedPt2);
    }
    e->mMode = dEvtMode_TALK_e;
    dEvt_setPartners(fpcM_ERROR_PROCESS_ID, fpcM_ERROR_PROCESS_ID);
    if (dEvt_takeEndRequest())
        f.endPending = true;
}

static void freezeRelease()
{
    Freeze& f = s.freeze;
    if (!f.holding)
        return;
    f.holding = false;

    dEvt_control_c* e = dComIfGp_getEvent();
    if (!e) {
        f.endPending = false;
        return;
    }
    dEvt_setPartners(f.savedPt1, f.savedPt2);
    e->mMode = f.savedMode;
    if (f.endPending)
        dEvt_setEndPending(1);
    f.endPending = false;
    Logger::Log("[flycam] freeze release: mode %u", (unsigned)f.savedMode);
}

static void toggleFreeze()
{
    s.frozen = !s.frozen;
    if (s.frozen)
        freezeHold();
    else
        freezeRelease();
    toast(Notifications::Info, s.frozen ? "World frozen" : "World running");
}

static void activate()
{
    if (!wwhd_dataResolved)
        return fail("Game data address unresolved");
    if (swapHandle() == WuPatch::kInvalidHandle)
        return fail("Camera table not declared");
    if (!captureOriginals())
        return fail("Camera table not readable");

    WuPatch::Data::SetEnabled(s.swap, true);
    s.swapOn = true;
    s.frames = 0;
    s.cameraOwned = false;
    s.leaveDone = false;
    s.speed = clampf(Config::g_settings.flyCamSpeed, kSpeedMin, kSpeedMax);
    s.frozen = true;
    s.freeze = Freeze();
    s.phase = PHASE_ARMING;

    Hotkeys::SuppressUntilReleased(Input::Current().held);
    toast(Notifications::Success, "On");
    Logger::Log("[flycam] arming");
}

static void deactivate(const char* reason, bool restoreView, bool quiet = false)
{
    if (s.phase == PHASE_OFF || s.phase == PHASE_LEAVING)
        return;

    freezeRelease();
    if (s.cameraOwned) {
        dEvent_setCameraPlay(dEvt_isEventRunning() ? (s.savedCameraPlay != 0u) : 0);
        s.cameraOwned = false;
    }
    if (Config::g_settings.flyCamSpeed != s.speed) {
        Config::g_settings.flyCamSpeed = s.speed;
        Config::MarkDirty();
    }

    if (s.phase == PHASE_FLYING) {
        s.phase = PHASE_LEAVING;
        s.frames = 0;
        s.leaveDone = false;
        s.restoreView = restoreView;
    } else {
        s.phase = PHASE_OFF;
    }

    Hotkeys::SuppressUntilReleased(Input::Current().held);
    if (!quiet)
        toast(Notifications::Info, restoreView ? "Off" : "Off, camera left in place");
    Logger::Log("[flycam] leaving: %s (restore=%d)", reason, (int)restoreView);
}

static bool teleportLink()
{
    if (!daPy_lk_c_getPlayer()) {
        toast(Notifications::Error, "Nothing to teleport");
        return false;
    }
    cXyz fwd, right, pos;
    forwardOf(s.pose, &fwd, &right);
    pos.x = s.pose.eye.x + fwd.x * kTeleportAhead;
    pos.y = s.pose.eye.y + fwd.y * kTeleportAhead;
    pos.z = s.pose.eye.z + fwd.z * kTeleportAhead;
    daPy_setPosition(&pos);
    toast(Notifications::Success, "Teleported Link");
    Logger::Log("[flycam] Link teleported to (%.1f %.1f %.1f)",
                (double)pos.x, (double)pos.y, (double)pos.z);
    return true;
}

static void steer(const Input::Snapshot& in)
{
    if (in.pressed & Input::BTN_R) s.speed *= kSpeedStep;
    if (in.pressed & Input::BTN_L) s.speed /= kSpeedStep;
    s.speed = clampf(s.speed, kSpeedMin, kSpeedMax);

    float speed = s.speed;
    if (in.held & Input::BTN_A) speed = kSpeedMax;
    if (in.held & Input::BTN_B) speed = kSpeedMin;

    s.pose.yaw += in.rx * kTurnRate;
    if (s.pose.yaw > kPi)       s.pose.yaw -= 2.0f * kPi;
    else if (s.pose.yaw < -kPi) s.pose.yaw += 2.0f * kPi;
    s.pose.pitch = clampf(s.pose.pitch + in.ry * kTurnRate, -kPitchLimit, kPitchLimit);

    cXyz fwd, right;
    forwardOf(s.pose, &fwd, &right);
    const float move = in.ly * speed, strafe = -in.lx * speed;
    float rise = 0.0f;
    if (in.held & Input::BTN_ZR) rise += speed;
    if (in.held & Input::BTN_ZL) rise -= speed;

    s.pose.eye.x += fwd.x * move + right.x * strafe;
    s.pose.eye.y += fwd.y * move + rise;
    s.pose.eye.z += fwd.z * move + right.z * strafe;
}

bool IsEnabled() { return Config::g_settings.flyCamEnabled; }

void SetEnabled(bool enabled)
{
    Config::g_settings.flyCamEnabled = enabled;
    if (!enabled)
        deactivate("disabled", true);
}

bool IsActive() { return s.phase != PHASE_OFF; }

void Tick(bool acceptInput)
{
    const bool enabled = Config::g_settings.flyCamEnabled;
    if (!enabled)
        deactivate("disabled", true, true);
    else if ((s.phase == PHASE_ARMING || s.phase == PHASE_FLYING) && dComIfGp_isNextStagePending())
        deactivate("stage change", false);

    switch (s.phase) {
    case PHASE_OFF:
        if (s.swapOn) {
            WuPatch::Data::SetEnabled(s.swap, false);
            s.swapOn = false;
        }
        if (enabled && acceptInput && Hotkeys::PressedIgnoringExtras(Hotkeys::HOTKEY_FLY_CAM))
            activate();
        return;
    case PHASE_ARMING:
        if (++s.frames > kArmFrames) {
            fail("Camera did not respond");
            deactivate("arming timeout", false, true);
        }
        return;
    case PHASE_LEAVING:
        if (s.leaveDone || ++s.frames > kLeaveFrames) {
            restoreStyle();
            s.phase = PHASE_OFF;
        }
        return;
    case PHASE_FLYING:
        break;
    }

    if (!s.cameraOwned) {
        s.savedCameraPlay = dEvent_getCameraPlay();
        s.cameraOwned = true;
    }
    dEvent_setCameraPlay(1);
    if (s.frozen)
        freezeHold();

    if (!acceptInput)
        return;
    if (Hotkeys::PressedIgnoringExtras(Hotkeys::HOTKEY_FLY_CAM))
        return deactivate("hotkey", true);

    const Input::Snapshot& in = Input::Current();
    if (in.pressed & Input::BTN_L3)
        return deactivate("L3", true);
    if (in.pressed & Input::BTN_R3)
        return deactivate("R3", false, teleportLink());
    if (in.pressed & kFreezeButton)
        toggleFreeze();
    steer(in);
}

void OnFrameEarly()
{
    if (s.phase == PHASE_FLYING && s.frozen)
        freezeHold();
}

void OnApplicationStart()
{
    s = State();
    s.swap = WuPatch::kInvalidHandle;
}

void OnApplicationEnd()
{
    deactivate("application end", true, true);
    restoreStyle();
    WuPatch::Data::SetEnabled(s.swap, false);
    s.swapOn = false;
    s.phase = PHASE_OFF;
}
}
}
