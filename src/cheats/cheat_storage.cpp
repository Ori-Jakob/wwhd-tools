#include "cheats/cheat_storage.h"

#include "core/hotkeys.h"
#include "libwwhd/libwwhd.h"
#include "ui/notifications.h"

namespace Cheats {
namespace Storage {
static const char* const kToastKey = "storage";

static bool s_enabled = false;

bool Enabled() { return s_enabled; }
void SetEnabled(bool enabled) { s_enabled = enabled; }

bool IsArmed() { return dEvt_isStorageArmed() != 0; }

bool Arm()
{
    if (!dComIfGp_getEvent())
        return false;
    if (dEvt_isEventRunning()) {
        Notifications::ShowKeyed(kToastKey, Notifications::Warning, "Storage",
                                 "Wait until the current event ends");
        return false;
    }
    dEvt_setEndPending(1);
    Notifications::ShowKeyed(kToastKey, Notifications::Success, "Storage",
                             "Storage armed");
    return true;
}

void Clear()
{
    if (!dComIfGp_getEvent())
        return;
    if (dEvt_isEventRunning()) {
        Notifications::ShowKeyed(kToastKey, Notifications::Warning, "Storage",
                                 "Nothing to clear while an event runs");
        return;
    }
    const bool wasArmed = dEvt_isStorageArmed() != 0;
    dEvt_setEndPending(0);
    Notifications::ShowKeyed(kToastKey, Notifications::Info, "Storage",
                             wasArmed ? "Storage cleared" : "Storage was not armed");
}

Collision CurrentCollision()
{
    const u32* flags = daPy_getAcchFlagsPtr();
    if (!flags)
        return COLLISION_NORMAL;
    const bool wallNone = (*flags & WWHD_ACCH_WALL_NONE) != 0;
    const bool lineNone = (*flags & WWHD_ACCH_LINE_CHECK_NONE) != 0;
    if (wallNone && lineNone)
        return COLLISION_DOOR;
    if (wallNone)
        return COLLISION_CHEST;
    if (lineNone)
        return COLLISION_OTHER;
    return COLLISION_NORMAL;
}

const char* CollisionName(Collision collision)
{
    switch (collision) {
    case COLLISION_CHEST: return "chest storage";
    case COLLISION_DOOR:  return "door storage";
    case COLLISION_OTHER: return "no line check";
    default:              return "normal";
    }
}

void SetCollision(Collision want)
{
    u32* flags = daPy_getAcchFlagsPtr();
    if (!flags) {
        Notifications::ShowKeyed(kToastKey, Notifications::Error, "Storage",
                                 "Link is not spawned");
        return;
    }

    const bool door = want == COLLISION_DOOR;
    if (door)
        *flags |= WWHD_ACCH_WALL_NONE | WWHD_ACCH_LINE_CHECK_NONE;
    else
        *flags = (*flags & ~(WWHD_ACCH_LINE_CHECK | WWHD_ACCH_LINE_CHECK_NONE))
               | WWHD_ACCH_WALL_NONE;

    if (dComIfGp_getEvent() && !dEvt_isEventRunning()) {
        dEvt_setEndPending(1);
        Notifications::ShowKeyed(kToastKey, Notifications::Success, "Storage",
                                 door ? "Door storage collision, storage armed"
                                      : "Chest storage collision, storage armed");
    } else {
        Notifications::ShowKeyed(kToastKey, Notifications::Warning, "Storage",
                                 door ? "Door storage collision; storage waits for the event to end"
                                      : "Chest storage collision; storage waits for the event to end");
    }
}

void RestoreCollision()
{
    u32* flags = daPy_getAcchFlagsPtr();
    if (!flags) {
        Notifications::ShowKeyed(kToastKey, Notifications::Error, "Storage",
                                 "Link is not spawned");
        return;
    }

    *flags = (*flags & ~(WWHD_ACCH_WALL_NONE | WWHD_ACCH_LINE_CHECK_NONE))
           | WWHD_ACCH_LINE_CHECK;

    if (dComIfGp_getEvent() && !dEvt_isEventRunning()) {
        dEvt_setEndPending(0);
        Notifications::ShowKeyed(kToastKey, Notifications::Info, "Storage",
                                 "Normal collision, storage cleared");
    } else {
        Notifications::ShowKeyed(kToastKey, Notifications::Info, "Storage",
                                 "Normal collision");
    }
}

void ToggleCollision(Collision want)
{
    if (CurrentCollision() == want)
        RestoreCollision();
    else
        SetCollision(want);
}

void Tick(bool acceptInput)
{
    if (!s_enabled || !acceptInput)
        return;
    if (Hotkeys::Pressed(Hotkeys::HOTKEY_STORAGE_ARM))
        Arm();
    else if (Hotkeys::Pressed(Hotkeys::HOTKEY_STORAGE_CLEAR))
        Clear();
    else if (Hotkeys::Pressed(Hotkeys::HOTKEY_STORAGE_CHEST))
        ToggleCollision(COLLISION_CHEST);
    else if (Hotkeys::Pressed(Hotkeys::HOTKEY_STORAGE_DOOR))
        ToggleCollision(COLLISION_DOOR);
}

void ResetToDefaults() { s_enabled = false; }
}
}
