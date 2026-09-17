#include "cheats/cheat_status.h"

#include "core/config.h"
#include "libwwhd/libwwhd.h"

#include "imgui.h"

namespace Cheats {
namespace Status {
struct Descriptor {
    const char* name;
    const char* key;
    const char* hint;
};

static const Descriptor kPins[PIN_COUNT] = {
    { "Infinite Hearts", "cheatInfiniteHearts",
      "Holds life at the current heart total." },
    { "Infinite Magic",  "cheatInfiniteMagic",
      "Holds magic at capacity. Does nothing before the first magic upgrade." },
    { "Infinite Ammo",   "cheatInfiniteAmmo",
      "Holds arrows and bombs at their quiver and bag capacities." },
    { "Infinite Rupees", "cheatInfiniteRupees",
      "Holds rupees at what the current wallet can carry." },
    { "Infinite Swim Stamina", "cheatInfiniteSwimStamina",
      "Keeps the swim stamina meter full." },
    { "Invincibility",   "cheatInvincible",
      "Keeps the post-hit invincibility timer running, so nothing can land a hit." },
};

static const s16 kInvincibleTimer = 2;

static bool s_enabled[PIN_COUNT];
static s16  s_savedDamageTimer = 0;

static bool valid(Pin pin) { return pin >= 0 && pin < PIN_COUNT; }

bool IsEnabled(Pin pin) { return valid(pin) && s_enabled[pin]; }
const char* Name(Pin pin) { return valid(pin) ? kPins[pin].name : "?"; }
const char* ConfigKey(Pin pin) { return valid(pin) ? kPins[pin].key : ""; }

void SetEnabled(Pin pin, bool enabled)
{
    if (valid(pin))
        s_enabled[pin] = enabled;
}

void Tick()
{
    if (s_enabled[PIN_HEARTS])
        dSv_setLife(dSv_getMaxLife());
    if (s_enabled[PIN_MAGIC])
        dSv_refillMagic();
    if (s_enabled[PIN_AMMO])
        dSv_refillAmmo();
    if (s_enabled[PIN_RUPEES]) {
        const u16 max = dSv_getMaxRupee();
        dSv_setRupee(max);
        dMeter_setRupeeDisplay(dMeter_searchByProc(), max);
    }
    if (s_enabled[PIN_SWIM_STAMINA] && daPy_isSwimming()) {
        s32* stamina = dComIfGp_getSwimStamina();
        if (stamina)
            *stamina = WWHD_SWIM_STAMINA_MAX;
    }
    if (s_enabled[PIN_INVINCIBLE]) {
        daPy_lk_c* link = daPy_lk_c_getPlayer();
        if (link)
            link->mDamageWaitTimer = kInvincibleTimer;
    }
}

bool BeginPlayerDraw(void* self)
{
    if (!s_enabled[PIN_INVINCIBLE] || !self)
        return false;
    daPy_lk_c* link = daPy_lk_c_getPlayer();
    if (!link || self != (void*)link)
        return false;
    s_savedDamageTimer = link->mDamageWaitTimer;
    link->mDamageWaitTimer = 0;
    return true;
}

void EndPlayerDraw(void* self)
{
    daPy_lk_c* link = (daPy_lk_c*)self;
    if (link)
        link->mDamageWaitTimer = s_savedDamageTimer;
}

void ResetToDefaults()
{
    for (int i = 0; i < PIN_COUNT; ++i)
        s_enabled[i] = false;
}
}
}
