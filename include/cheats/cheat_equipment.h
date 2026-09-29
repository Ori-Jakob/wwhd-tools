#pragma once

#include <stdint.h>

namespace Cheats {
namespace Equipment {
enum Mod {
    MOD_REMOTE_BOMBS = 0,
    MOD_NO_BOMB_LIMIT,
    MOD_SUPER_HOOKSHOT,
    MOD_FREE_MAGIC_ARMOR,
    MOD_FAST_BOOMERANG,
    MOD_QUICK_MAGIC_ARROWS,
    MOD_LONG_GRAPPLE,
    MOD_FAST_ROPE_CLIMB,
    MOD_FAST_IRON_BOOTS,
    MOD_UNRESTRICTED_ITEMS,
    MOD_QUICK_SPIN,
    MOD_FREE_SPIN,
    MOD_COUNT
};

const char* Name(Mod mod);
const char* ConfigKey(Mod mod);
bool IsEnabled(Mod mod);
void SetEnabled(Mod mod, bool enabled);

// Whole-number multipliers some mods take.
enum Level {
    LEVEL_BOOMERANG_RANGE = 0,
    LEVEL_BOOMERANG_SPEED,
    LEVEL_HOOKSHOT_RANGE,
    LEVEL_HOOKSHOT_SPEED,
    LEVEL_GRAPPLE_RANGE,
    LEVEL_CLIMB_SPEED,
    LEVEL_COUNT
};

const char* LevelName(Level level);
const char* LevelKey(Level level);
Mod         LevelMod(Level level);
int         LevelMin(Level level);
int         LevelMax(Level level);
int         GetLevel(Level level);
void        SetLevel(Level level, int value);

// Switches the Cemu pack's code patches read, published every frame.
enum PackFlag {
    PACK_FLAG_FREE_MAGIC_ARMOR  = 1u << 0,
    PACK_FLAG_NORMAL_IRON_BOOTS = 1u << 1,
    PACK_FLAG_HOOKSHOT_ANY      = 1u << 2,
};
uint32_t PackFlags();

void Tick(bool acceptInput);
void OnFrameEarly();
void OnApplicationStart();
void ResetToDefaults();
}
}
