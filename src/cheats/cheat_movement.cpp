#include "cheats/cheat_movement.h"

#include "core/hotkeys.h"
#include "core/input.h"
#include "libwwhd/libwwhd.h"
#include "tools/flycam.h"

#include <math.h>

namespace Cheats {
namespace Movement {
static const float kMoonJumpVelocity = 36.0f;
static const float kAngleToRadians = 6.28318530718f / 65536.0f;

static bool s_moonJump = false;
static bool s_speedModifiers = false;
static bool s_swimSpeedControls = false;
static int  s_swimMultiplier = 4;
static int  s_moveMultiplier = 2;
static int  s_crawlMultiplier = 2;
static bool s_acceptInput = false;
static int  s_swimApplied = 0;

static bool  s_launch = false;
static int   s_launchVelocity = 500;
static bool  s_launchReversed = false;
static float s_launchRampSeconds = 1.0f;
static float s_launchSpeed = 0.0f;
static bool  s_launchHeld = false;
static bool  s_launchStop = false;

bool MoonJumpEnabled() { return s_moonJump; }
void SetMoonJumpEnabled(bool enabled) { s_moonJump = enabled; }

bool SpeedModifiersEnabled() { return s_speedModifiers; }
void SetSpeedModifiersEnabled(bool enabled) { s_speedModifiers = enabled; }

bool SwimSpeedControlsEnabled() { return s_swimSpeedControls; }
void SetSwimSpeedControlsEnabled(bool enabled) { s_swimSpeedControls = enabled; }

int SwimMultiplier() { return s_swimMultiplier; }

int MoveMultiplier() { return s_moveMultiplier; }
int CrawlMultiplier() { return s_crawlMultiplier; }

void SetCrawlMultiplier(int multiplier)
{
    if (multiplier < CRAWL_MULTIPLIER_MIN) multiplier = CRAWL_MULTIPLIER_MIN;
    if (multiplier > CRAWL_MULTIPLIER_MAX) multiplier = CRAWL_MULTIPLIER_MAX;
    s_crawlMultiplier = multiplier;
}

void SetMoveMultiplier(int multiplier)
{
    if (multiplier < MOVE_MULTIPLIER_MIN) multiplier = MOVE_MULTIPLIER_MIN;
    if (multiplier > MOVE_MULTIPLIER_MAX) multiplier = MOVE_MULTIPLIER_MAX;
    s_moveMultiplier = multiplier;
}

void SetSwimMultiplier(int multiplier)
{
    if (multiplier < SPEED_MULTIPLIER_MIN) multiplier = SPEED_MULTIPLIER_MIN;
    if (multiplier > SPEED_MULTIPLIER_MAX) multiplier = SPEED_MULTIPLIER_MAX;
    s_swimMultiplier = multiplier;
}

static void clearLaunch()
{
    s_launchSpeed = 0.0f;
    s_launchHeld = false;
    s_launchStop = false;
}

bool LaunchEnabled() { return s_launch; }

void SetLaunchEnabled(bool enabled)
{
    s_launch = enabled;
    if (!enabled)
        clearLaunch();
}

int LaunchVelocity() { return s_launchVelocity; }

void SetLaunchVelocity(int velocity)
{
    if (velocity < LAUNCH_VELOCITY_MIN) velocity = LAUNCH_VELOCITY_MIN;
    if (velocity > LAUNCH_VELOCITY_MAX) velocity = LAUNCH_VELOCITY_MAX;
    s_launchVelocity = velocity;
}

bool LaunchReversed() { return s_launchReversed; }
void SetLaunchReversed(bool reversed) { s_launchReversed = reversed; }

float LaunchRampSeconds() { return s_launchRampSeconds; }

void SetLaunchRampSeconds(float seconds)
{
    if (seconds < LAUNCH_RAMP_MIN_SECONDS) seconds = LAUNCH_RAMP_MIN_SECONDS;
    if (seconds > LAUNCH_RAMP_MAX_SECONDS) seconds = LAUNCH_RAMP_MAX_SECONDS;
    s_launchRampSeconds = seconds;
}

static void applySwimSpeed(int multiplier)
{
    if (multiplier == s_swimApplied)
        return;
    float* speed = daPy_getSwimSpeedPtr();
    if (!speed)
        return;

    *speed = multiplier ? WWHD_SWIM_SPEED_STOCK * (float)multiplier
                        : WWHD_SWIM_SPEED_STOCK;
    s_swimApplied = multiplier;
}

static void boostNormalSpeed(void* self, int multiplier, Hotkeys::Id hotkey)
{
    if (!self || !s_acceptInput || !s_speedModifiers || multiplier <= 1)
        return;
    if (Tools::FlyCam::IsActive() || !Hotkeys::Held(hotkey))
        return;

    daPy_lk_c* link = (daPy_lk_c*)self;
    f32* speed = (f32*)((u8*)self + WWHD_DAPY_OFF_NORMAL_SPEED);
    const f32 scale = (f32)multiplier;
    const f32 cap = link->mMaxNormalSpeed * scale;

    f32 boosted = *speed * scale;
    if (boosted > cap)
        boosted = cap;
    else if (boosted < -cap)
        boosted = -cap;
    *speed = boosted;
}

void OnMoveProc(void* self)
{
    boostNormalSpeed(self, s_moveMultiplier, Hotkeys::HOTKEY_MOVE_BOOST);
}

void OnCrawlProc(void* self)
{
    boostNormalSpeed(self, s_crawlMultiplier, Hotkeys::HOTKEY_CRAWL_BOOST);
}

void OnSwimProc(void* self)
{
    if (!self || !s_acceptInput || !s_speedModifiers || !s_swimSpeedControls)
        return;
    if (Tools::FlyCam::IsActive() ||
        !Hotkeys::Held(Hotkeys::HOTKEY_SWIM_BOOST))
        return;

    daPy_lk_c* link = (daPy_lk_c*)self;
    f32* speed = (f32*)((u8*)self + WWHD_DAPY_OFF_NORMAL_SPEED);

    if (Hotkeys::PressedIgnoringExtras(Hotkeys::HOTKEY_SWIM_FULL_SPEED))
        *speed = link->mMaxNormalSpeed;
    else if (Hotkeys::PressedIgnoringExtras(Hotkeys::HOTKEY_SWIM_STOP))
        *speed = 0.0f;
}

static bool launchWanted()
{
    return s_acceptInput && s_launch && (s_launchStop || s_launchHeld);
}

void OnBeforePosMove(void* self)
{
    if (!self || !launchWanted())
        return;
    if (self != (void*)daPy_lk_c_getPlayer() || daPy_isRidingShip())
        return;

    daPy_lk_c* link = (daPy_lk_c*)self;
    f32* normalSpeed = (f32*)((u8*)self + WWHD_DAPY_OFF_NORMAL_SPEED);

    if (s_launchStop) {
        *normalSpeed = 0.0f;
        link->base.speedF = 0.0f;
        link->base.speed.x = 0.0f;
        link->base.speed.z = 0.0f;
        return;
    }

    link->base.current.angle.y = link->base.shape_angle.y;
    *normalSpeed = s_launchSpeed;
}

void OnBeforeActorPosMove(void* actor)
{
    if (!actor || !launchWanted())
        return;
    daShip_c* ship = get_daShip();
    if (actor != (void*)ship || !daPy_isRidingShip())
        return;

    fopAc_ac_c* base = &ship->base;
    if (s_launchStop) {
        base->speedF = 0.0f;
        base->speed.x = 0.0f;
        base->speed.z = 0.0f;
        return;
    }

    const float heading = (float)base->shape_angle.y * kAngleToRadians;
    base->speedF = s_launchSpeed;
    base->speed.x = s_launchSpeed * sinf(heading);
    base->speed.z = s_launchSpeed * cosf(heading);
}

static float currentForwardSpeed()
{
    const daShip_c* ship = get_daShip();
    if (ship && daPy_isRidingShip())
        return ship->base.speedF;
    const f32* normalSpeed = daPy_getNormalSpeedPtr();
    return normalSpeed ? *normalSpeed : 0.0f;
}

static void tickLaunch(bool allowed)
{
    const bool active = allowed && s_launch;
    const bool wasHeld = s_launchHeld;
    s_launchStop = active && Hotkeys::PressedIgnoringExtras(Hotkeys::HOTKEY_LAUNCH_STOP);
    s_launchHeld = active && !s_launchStop && Hotkeys::Held(Hotkeys::HOTKEY_LAUNCH);
    if (!s_launchHeld) {
        s_launchSpeed = 0.0f;
        return;
    }

    const float target = s_launchReversed ? -(float)s_launchVelocity
                                          : (float)s_launchVelocity;
    if (!wasHeld)
        s_launchSpeed = currentForwardSpeed();

    const int frames = (int)(s_launchRampSeconds * (float)LAUNCH_FRAMES_PER_SECOND + 0.5f);
    if (frames <= 0) {
        s_launchSpeed = target;
        return;
    }

    const float step = (float)s_launchVelocity / (float)frames;
    if (s_launchSpeed < target) {
        s_launchSpeed += step;
        if (s_launchSpeed > target)
            s_launchSpeed = target;
    } else if (s_launchSpeed > target) {
        s_launchSpeed -= step;
        if (s_launchSpeed < target)
            s_launchSpeed = target;
    }
}

void Tick(bool acceptInput)
{
    s_acceptInput = acceptInput;

    daPy_lk_c* link = acceptInput ? daPy_lk_c_getPlayer() : nullptr;
    if (!link) {
        applySwimSpeed(0);
        tickLaunch(false);
        return;
    }

    const bool flying = Tools::FlyCam::IsActive();
    if (s_moonJump && !flying && Hotkeys::Held(Hotkeys::HOTKEY_MOON_JUMP))
        link->base.speed.y = kMoonJumpVelocity;

    const bool boosting = s_speedModifiers && s_swimMultiplier > 1 &&
                          daPy_isSwimming() &&
                          Hotkeys::Held(Hotkeys::HOTKEY_SWIM_BOOST);
    applySwimSpeed(boosting ? s_swimMultiplier : 0);

    tickLaunch(!flying);
}

void ResetToDefaults()
{
    s_moonJump = false;
    s_speedModifiers = false;
    s_swimSpeedControls = false;
    s_swimMultiplier = 4;
    s_moveMultiplier = 2;
    s_crawlMultiplier = 2;
    s_launch = false;
    s_launchVelocity = 500;
    s_launchReversed = false;
    s_launchRampSeconds = 1.0f;
    clearLaunch();
    applySwimSpeed(0);
}
}
}
