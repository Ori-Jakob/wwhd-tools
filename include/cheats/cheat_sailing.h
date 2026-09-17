#pragma once

namespace Cheats {
namespace Sailing {
bool MoonJumpEnabled();
void SetMoonJumpEnabled(bool enabled);

bool AutoWindEnabled();
void SetAutoWindEnabled(bool enabled);

enum WindDir {
    WIND_DIR_DEFAULT = 0,
    WIND_DIR_N,
    WIND_DIR_NE,
    WIND_DIR_E,
    WIND_DIR_SE,
    WIND_DIR_S,
    WIND_DIR_SW,
    WIND_DIR_W,
    WIND_DIR_NW,
    WIND_DIR_COUNT
};

int         WindDirection();
void        SetWindDirection(int dir);
const char* WindDirectionName(int dir);

bool BoostEnabled();
void SetBoostEnabled(bool enabled);

int  BoostMultiplier();
void SetBoostMultiplier(int multiplier);

static const int BOOST_MULTIPLIER_MIN = 1;
static const int BOOST_MULTIPLIER_MAX = 10;

void TeleportLinkToBoat();

void Tick(bool acceptInput);
void ResetToDefaults();
}
}
