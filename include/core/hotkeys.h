#pragma once

#include <stddef.h>
#include <stdint.h>

namespace Hotkeys {
enum Id {
    HOTKEY_MENU = 0,
    HOTKEY_FLY_CAM,
    HOTKEY_MOON_JUMP,
    HOTKEY_BOAT_MOON_JUMP,
    HOTKEY_TELEPORT_TO_BOAT,
    HOTKEY_BOAT_BOOST,
    HOTKEY_MOVE_BOOST,
    HOTKEY_CRAWL_BOOST,
    HOTKEY_SWIM_BOOST,
    HOTKEY_SWIM_FULL_SPEED,
    HOTKEY_SWIM_STOP,
    HOTKEY_LAUNCH,
    HOTKEY_LAUNCH_STOP,
    HOTKEY_STORAGE_ARM,
    HOTKEY_STORAGE_CLEAR,
    HOTKEY_STORAGE_CHEST,
    HOTKEY_STORAGE_DOOR,
    HOTKEY_MSS,
    HOTKEY_SAVE_STATE,
    HOTKEY_LOAD_STATE,
    HOTKEY_GAME_RESET,
    HOTKEY_STAGE_RELOAD,
    HOTKEY_QUICK_ACCESS,
    HOTKEY_TEXT_ADVANCE,
    HOTKEY_COUNT
};

enum Scope {
    SCOPE_GLOBAL = 0,
    SCOPE_SWIM,
};

uint32_t    Get(Id id);
void        Set(Id id, uint32_t buttons);
uint32_t    Default(Id id);
const char* Name(Id id);
const char* Group(Id id);
const char* ConfigKey(Id id);
bool        IsHold(Id id);
Scope       ScopeOf(Id id);

bool        Conflicts(Id target, uint32_t buttons, Id other);
const char* Validate(Id target, uint32_t buttons);

bool OverlayComboHeld(uint32_t held);

bool Pressed(Id id);
bool PressedIgnoringExtras(Id id);
bool Held(Id id);

void SuppressUntilReleased(uint32_t buttons);

void Tick();

void ResetToDefaults();
void OnApplicationStart();
}
