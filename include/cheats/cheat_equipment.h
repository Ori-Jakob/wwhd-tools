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

// Switches the Cemu pack's code patches read, published every frame.
enum PackFlag {
    PACK_FLAG_FREE_MAGIC_ARMOR = 1u << 0,
};
uint32_t PackFlags();

void Tick(bool acceptInput);
void OnFrameEarly();
void OnMoveProc(void* self);
void OnApplicationStart();
void ResetToDefaults();
}
}
