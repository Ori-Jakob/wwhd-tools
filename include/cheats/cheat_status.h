#pragma once

namespace Cheats {
namespace Status {
enum Pin {
    PIN_HEARTS = 0,
    PIN_MAGIC,
    PIN_AMMO,
    PIN_RUPEES,
    PIN_SWIM_STAMINA,
    PIN_INVINCIBLE,
    PIN_COUNT
};

bool        IsEnabled(Pin pin);
void        SetEnabled(Pin pin, bool enabled);
const char* Name(Pin pin);
const char* ConfigKey(Pin pin);

bool BeginPlayerDraw(void* self);
void EndPlayerDraw(void* self);

void Tick();
void ResetToDefaults();
}
}
