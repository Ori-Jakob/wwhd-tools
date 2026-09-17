#include "cheats/cheat_sailing.h"

#include "core/config.h"
#include "core/hotkeys.h"
#include "core/logger.h"
#include "libwwhd/libwwhd.h"
#include "ui/notifications.h"
#include "ui/ui_field.h"
#include "ui/ui_hotkey.h"

#include "imgui.h"

namespace Cheats {
namespace Sailing {
static const float kMoonJumpVelocity = 36.0f;
static const float kBoatDropHeight = 100.0f;

static bool s_moonJump = false;
static bool s_autoWind = false;
static bool s_boost = false;
static int  s_boostMultiplier = 3;
static int s_sailApplied = 0;
static int  s_windDirection = WIND_DIR_DEFAULT;
static bool s_windForced = false;
static u8   s_windAngleOnSaved = 0;

static const s16 kWindFacing[WIND_DIR_COUNT - 1] = {
    (s16)0x8000, (s16)0x6000, (s16)0x4000, (s16)0x2000,
    (s16)0x0000, (s16)0xE000, (s16)0xC000, (s16)0xA000,
};

static const char* const kWindNames[WIND_DIR_COUNT] = {
    "Game default", "North", "Northeast", "East", "Southeast",
    "South", "Southwest", "West", "Northwest",
};

static void applyWind(bool active, s16 facing)
{
    if (!active) {
        if (s_windForced) {
            dKy_restoreWindAngleOn(s_windAngleOnSaved);
            s_windForced = false;
        }
        return;
    }

    u8 previous = 0;
    if (!dKy_setWindAngle(facing, s_windForced ? (u8*)0 : &previous))
        return;
    if (!s_windForced) {
        s_windAngleOnSaved = previous;
        s_windForced = true;
    }
}

static void applySailSpeed(int multiplier)
{
    if (multiplier == s_sailApplied)
        return;
    float* speed = daShip_getSailSpeedPtr();
    if (!speed)
        return;
    *speed = multiplier ? WWHD_SAIL_SPEED_STOCK * (float)multiplier
                        : WWHD_SAIL_SPEED_STOCK;
    s_sailApplied = multiplier;
}

bool MoonJumpEnabled() { return s_moonJump; }
void SetMoonJumpEnabled(bool enabled) { s_moonJump = enabled; }
bool AutoWindEnabled() { return s_autoWind; }
void SetAutoWindEnabled(bool enabled) { s_autoWind = enabled; }

int WindDirection() { return s_windDirection; }

void SetWindDirection(int dir)
{
    if (dir < 0 || dir >= WIND_DIR_COUNT)
        dir = WIND_DIR_DEFAULT;
    s_windDirection = dir;
}

const char* WindDirectionName(int dir)
{
    if (dir < 0 || dir >= WIND_DIR_COUNT)
        dir = WIND_DIR_DEFAULT;
    return kWindNames[dir];
}
bool BoostEnabled() { return s_boost; }
void SetBoostEnabled(bool enabled) { s_boost = enabled; }
int  BoostMultiplier() { return s_boostMultiplier; }

void SetBoostMultiplier(int multiplier)
{
    if (multiplier < BOOST_MULTIPLIER_MIN) multiplier = BOOST_MULTIPLIER_MIN;
    if (multiplier > BOOST_MULTIPLIER_MAX) multiplier = BOOST_MULTIPLIER_MAX;
    s_boostMultiplier = multiplier;
}

void TeleportLinkToBoat()
{
    daShip_c* ship = get_daShip();
    if (!ship) {
        Notifications::Show(Notifications::Error, "Boat", "The boat is not spawned here");
        return;
    }
    cXyz pos;
    if (!daShip_getPosition(ship, &pos)) {
        Notifications::Show(Notifications::Error, "Boat", "Boat position unavailable");
        return;
    }
    if (!daPy_lk_c_getPlayer()) {
        Notifications::Show(Notifications::Error, "Boat", "Link is not spawned");
        return;
    }
    pos.y += kBoatDropHeight;
    daPy_setPosition(&pos);
    Notifications::Show(Notifications::Success, "Boat", "Teleported to the boat");
    Logger::Log("[sailing] Link teleported to boat at (%.1f %.1f %.1f)",
                (double)pos.x, (double)pos.y, (double)pos.z);
}

void Tick(bool acceptInput)
{
    if (!acceptInput)
        applySailSpeed(0);

    if (s_autoWind) {
        daPy_lk_c* link = daPy_lk_c_getPlayer();
        applyWind(link != 0, link ? link->base.shape_angle.y : (s16)0);
    } else if (s_windDirection != WIND_DIR_DEFAULT) {
        applyWind(true, kWindFacing[s_windDirection - 1]);
    } else {
        applyWind(false, 0);
    }

    if (!acceptInput)
        return;

    if (Hotkeys::Pressed(Hotkeys::HOTKEY_TELEPORT_TO_BOAT))
        TeleportLinkToBoat();

    daShip_c* ship = get_daShip();
    const bool riding = ship && daPy_isRidingShip();

    const bool boosting = riding && s_boost && s_boostMultiplier > 1 &&
                          Hotkeys::Held(Hotkeys::HOTKEY_BOAT_BOOST);
    applySailSpeed(boosting ? s_boostMultiplier : 0);
    if (!riding)
        return;

    if (s_moonJump && Hotkeys::Held(Hotkeys::HOTKEY_BOAT_MOON_JUMP))
        ship->base.speed.y = kMoonJumpVelocity;
}

void ResetToDefaults()
{
    s_moonJump = false;
    s_autoWind = false;
    s_windDirection = WIND_DIR_DEFAULT;
    applyWind(false, 0);
    s_boost = false;
    s_boostMultiplier = 3;
    applySailSpeed(0);
}
}
}
