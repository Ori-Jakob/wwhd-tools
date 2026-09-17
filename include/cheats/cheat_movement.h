#pragma once

namespace Cheats {
namespace Movement {
bool MoonJumpEnabled();
void SetMoonJumpEnabled(bool enabled);

bool SpeedModifiersEnabled();
void SetSpeedModifiersEnabled(bool enabled);

bool SwimSpeedControlsEnabled();
void SetSwimSpeedControlsEnabled(bool enabled);

int  SwimMultiplier();
void SetSwimMultiplier(int multiplier);

int  MoveMultiplier();
void SetMoveMultiplier(int multiplier);

int  CrawlMultiplier();
void SetCrawlMultiplier(int multiplier);

bool LaunchEnabled();
void SetLaunchEnabled(bool enabled);

int  LaunchVelocity();
void SetLaunchVelocity(int velocity);

bool LaunchReversed();
void SetLaunchReversed(bool reversed);

float LaunchRampSeconds();
void  SetLaunchRampSeconds(float seconds);

static const int SPEED_MULTIPLIER_MIN = 1;
static const int SPEED_MULTIPLIER_MAX = 500;

static const int MOVE_MULTIPLIER_MIN = 1;
static const int MOVE_MULTIPLIER_MAX = 5;

static const int CRAWL_MULTIPLIER_MIN = 1;
static const int CRAWL_MULTIPLIER_MAX = 5;

static const int   LAUNCH_VELOCITY_MIN       = 50;
static const int   LAUNCH_VELOCITY_MAX       = 2000;
static const float LAUNCH_RAMP_MIN_SECONDS   = 0.0f;
static const float LAUNCH_RAMP_MAX_SECONDS   = 3.0f;
static const int   LAUNCH_FRAMES_PER_SECOND  = 30;

void Tick(bool acceptInput);
void OnMoveProc(void* self);
void OnCrawlProc(void* self);
void OnSwimProc(void* self);
void OnBeforePosMove(void* self);
void OnBeforeActorPosMove(void* actor);
void ResetToDefaults();
}
}
