#include "core/hotkeys.h"

#include "core/input.h"

namespace Hotkeys {
struct Entry {
    const char* name;
    const char* group;
    const char* key;
    uint32_t    fallback;
    bool        hold;
    Scope       scope;
};

// Defaults only; the config overrides them. Hold entries are modifiers and may share a button.
static const Entry kEntries[HOTKEY_COUNT] = {
    { "Open menu",        "General",  "menuCombo",
      Input::BTN_L | Input::BTN_ZL | Input::BTN_MINUS,               false, SCOPE_GLOBAL },
    { "Fly Cam",          "Camera",   "flyCamCombo",
      Input::BTN_ZL | Input::BTN_ZR | Input::BTN_L3 | Input::BTN_R3, false, SCOPE_GLOBAL },
    { "Moon Jump",        "Mods",     "moonJumpCombo",
      Input::BTN_L3,                                                 true,  SCOPE_GLOBAL },
    { "Boat Moon Jump",   nullptr,    "boatMoonJumpCombo",
      Input::BTN_ZR,                                                 true,  SCOPE_GLOBAL },
    { "Teleport to Boat", nullptr,    "boatWarpCombo",
      Input::BTN_A | Input::BTN_LEFT,                                false, SCOPE_GLOBAL },
    { "Boat speed boost", nullptr,    "boatBoostCombo",
      Input::BTN_A,                                                  true,  SCOPE_GLOBAL },
    { "Move speed boost", nullptr,    "moveBoostCombo",
      Input::BTN_ZR,                                                 true,  SCOPE_GLOBAL },
    { "Crawl speed boost", nullptr,   "crawlBoostCombo",
      Input::BTN_ZR,                                                 true,  SCOPE_GLOBAL },
    { "Swim speed boost", nullptr,    "swimBoostCombo",
      Input::BTN_ZR,                                                 true,  SCOPE_GLOBAL },
    { "Swim full speed",  nullptr,    "swimFullSpeedCombo",
      Input::BTN_A,                                                  false, SCOPE_SWIM   },
    { "Swim stop",        nullptr,    "swimStopCombo",
      Input::BTN_B,                                                  false, SCOPE_SWIM   },
    { "Launch Link",      nullptr,    "launchCombo",
      Input::BTN_L | Input::BTN_A,                                   true,  SCOPE_GLOBAL },
    { "Stop Link",        nullptr,    "launchStopCombo",
      Input::BTN_L | Input::BTN_B,                                   false, SCOPE_GLOBAL },
    { "Give storage",     nullptr,    "storageArmCombo",
      Input::BTN_L | Input::BTN_ZL | Input::BTN_X,                   false, SCOPE_GLOBAL },
    { "Clear storage",    nullptr,    "storageClearCombo",
      Input::BTN_L | Input::BTN_ZL | Input::BTN_Y,                   false, SCOPE_GLOBAL },
    { "Chest storage collision", nullptr, "storageChestCombo",
      Input::BTN_L | Input::BTN_ZL | Input::BTN_R,                   false, SCOPE_GLOBAL },
    { "Door storage collision",  nullptr, "storageDoorCombo",
      Input::BTN_L | Input::BTN_ZL | Input::BTN_ZR,                  false, SCOPE_GLOBAL },
    { "Super swim macro", nullptr,    "mssCombo",
      Input::BTN_R,                                                  true,  SCOPE_GLOBAL },
    { "Save state",       "Save States", "saveStateCombo",
      Input::BTN_L | Input::BTN_ZL | Input::BTN_UP,                  false, SCOPE_GLOBAL },
    { "Load state",       nullptr,    "loadStateCombo",
      Input::BTN_L | Input::BTN_ZL | Input::BTN_DOWN,                false, SCOPE_GLOBAL },
    { "Reset game",       "Stage",    "gameResetCombo",
      Input::BTN_L | Input::BTN_ZL | Input::BTN_LEFT,                false, SCOPE_GLOBAL },
    { "Reload stage",     nullptr,    "stageReloadCombo",
      Input::BTN_L | Input::BTN_ZL | Input::BTN_RIGHT,               false, SCOPE_GLOBAL },
    { "Quick Access",     "General",  "quickAccessWindowCombo",
      Input::BTN_ZL | Input::BTN_L | Input::BTN_PLUS,                false, SCOPE_GLOBAL },
    { "Auto-advance text", "Text",    "textAdvanceCombo",
      Input::BTN_B,                                                  true,  SCOPE_GLOBAL },
};

static uint32_t s_bindings[HOTKEY_COUNT];
static uint32_t s_suppressed = 0;

static bool valid(Id id) { return id >= 0 && id < HOTKEY_COUNT; }

uint32_t    Get(Id id)       { return valid(id) ? s_bindings[id] : 0u; }
uint32_t    Default(Id id)   { return valid(id) ? kEntries[id].fallback : 0u; }
const char* Name(Id id)      { return valid(id) ? kEntries[id].name : "?"; }
const char* Group(Id id)     { return valid(id) ? kEntries[id].group : nullptr; }
const char* ConfigKey(Id id) { return valid(id) ? kEntries[id].key : ""; }
bool        IsHold(Id id)    { return valid(id) && kEntries[id].hold; }
Scope       ScopeOf(Id id)   { return valid(id) ? kEntries[id].scope : SCOPE_GLOBAL; }

void Set(Id id, uint32_t buttons)
{
    if (valid(id))
        s_bindings[id] = buttons;
}

bool Conflicts(Id target, uint32_t buttons, Id other)
{
    if (!valid(target) || !valid(other) || target == other)
        return false;
    if (IsHold(target) || IsHold(other))
        return false;
    if (kEntries[target].scope != kEntries[other].scope)
        return false;
    return buttons == s_bindings[other];
}

const char* Validate(Id target, uint32_t buttons)
{
    if (!valid(target))
        return "Unknown hotkey.";
    if (!buttons)
        return "Press at least one button.";
    if (target != HOTKEY_MENU && buttons == s_bindings[HOTKEY_MENU])
        return "That combo opens the menu. Rebind the menu hotkey first.";
    return nullptr;
}

void SuppressUntilReleased(uint32_t buttons)
{
    s_suppressed |= buttons;
}

bool OverlayComboHeld(uint32_t held)
{
    static const Id kOverlay[] = { HOTKEY_MENU, HOTKEY_QUICK_ACCESS };
    for (unsigned i = 0; i < sizeof(kOverlay) / sizeof(kOverlay[0]); ++i) {
        const uint32_t combo = Get(kOverlay[i]);
        if (combo && (held & combo) == combo)
            return true;
    }
    return false;
}

bool Pressed(Id id)
{
    const uint32_t combo = Get(id);
    if (!combo || IsHold(id) || (combo & s_suppressed))
        return false;

    const Input::Snapshot& in = Input::Current();
    return in.held == combo && (in.pressed & combo) != 0;
}

bool PressedIgnoringExtras(Id id)
{
    const uint32_t combo = Get(id);
    if (!combo || IsHold(id) || (combo & s_suppressed))
        return false;

    const Input::Snapshot& in = Input::Current();
    return (in.held & combo) == combo && (in.pressed & combo) != 0;
}

bool Held(Id id)
{
    const uint32_t combo = Get(id);
    if (!combo || (combo & s_suppressed))
        return false;
    return (Input::Current().held & combo) == combo;
}

void Tick()
{
    s_suppressed &= Input::Current().held;
}

void ResetToDefaults()
{
    for (int i = 0; i < HOTKEY_COUNT; ++i)
        s_bindings[i] = kEntries[i].fallback;
}

void OnApplicationStart()
{
    s_suppressed = 0;
}
}
