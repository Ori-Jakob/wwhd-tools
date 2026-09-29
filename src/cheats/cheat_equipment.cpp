#include "cheats/cheat_equipment.h"

#include "app/cemu.h"
#include "app/hooks.h"
#include "core/hotkeys.h"
#include "core/logger.h"
#include "libwwhd/libwwhd.h"

namespace Cheats {
namespace Equipment {
struct Info {
    const char* name;
    const char* key;
};

static const Info kInfo[MOD_COUNT] = {
    { "Remote bombs",                 "equipRemoteBombs" },
    { "No bomb limit",                "equipNoBombLimit" },
    { "Super Hookshot",               "equipSuperHookshot" },
    { "Free Magic Armor",             "equipFreeMagicArmor" },
    { "Fast boomerang",               "equipFastBoomerang" },
    { "Quick magic arrows",           "equipQuickMagicArrows" },
    { "Long Grappling Hook",          "equipLongGrapple" },
    { "Fast rope climbing",           "equipFastRopeClimb" },
    { "Fast Iron Boots",              "equipFastIronBoots" },
    { "Unrestricted items",           "equipUnrestrictedItems" },
    { "Quick Hurricane Spin",         "equipQuickSpin" },
    { "Hurricane Spin without magic", "equipFreeSpin" },
};

// Kept inside the hookshot's limits: range + pitch * shot speed <= 300 pitches, return < 20 pitches.
static const f32 kHookshotPitch  = 10.0f;
static const f32 kHookshotRange  = 2750.0f;
static const f32 kHookshotShot   = 21.0f;
static const f32 kHookshotReturn = 126.0f;

static const f32 kBoomerangSpeed     = 120.0f;
static const f32 kBoomerangRange     = 5000.0f;
static const f32 kBoomerangRangeFar  = 8000.0f;

static const f32 kGrappleRange    = 2000.0f;
static const f32 kGrappleVertical = 2250.0f;
static const f32 kGrappleAim      = 120.0f;

static const f32 kClimbScale = 2.0f;
static const f32 kClimbAnmRate = 0.8f;
static const s16 kQuickSpinCharge = 1;

static bool s_enabled[MOD_COUNT] = {};
static bool s_detonate = false;
static bool s_armorPatched = false;
static bool s_armorFailed = false;
static bool s_armorFailedWant = false;
static u8   s_savedSword = (u8)WWHD_ITEM_NONE;
static s32  s_lastProc = -1;
static f32  s_lastY = 0.0f;

static RplHook s_armorPatch =
    RPL_HOOK_REWRITE("magicArmorCost", 0u, 0x1C000014u, RPL_HOOK_OPTIONAL, 0x1C000000u);

static bool valid(Mod mod) { return mod >= 0 && mod < MOD_COUNT; }

const char* Name(Mod mod)      { return valid(mod) ? kInfo[mod].name : "?"; }
const char* ConfigKey(Mod mod) { return valid(mod) ? kInfo[mod].key : ""; }
bool IsEnabled(Mod mod)        { return valid(mod) && s_enabled[mod]; }

void SetEnabled(Mod mod, bool enabled)
{
    if (valid(mod))
        s_enabled[mod] = enabled;
}

uint32_t PackFlags()
{
    return s_enabled[MOD_FREE_MAGIC_ARMOR] ? PACK_FLAG_FREE_MAGIC_ARMOR : 0u;
}

// Only the stock value or ours is overwritten, so a wrong address is left alone.
static void setConst(f32* p, f32 stock, f32 value, bool on)
{
    if (!p || (*p != stock && *p != value))
        return;
    *p = on ? value : stock;
}

static void applyConsts()
{
    const bool hook = s_enabled[MOD_SUPER_HOOKSHOT];
    setConst(daHookshot_param(WWHD_HOOKSHOT_OFF_PITCH), 7.0f, kHookshotPitch, hook);
    setConst(daHookshot_param(WWHD_HOOKSHOT_OFF_PITCH_Y), -7.0f, -kHookshotPitch, hook);
    setConst(daHookshot_param(WWHD_HOOKSHOT_OFF_RANGE), 1500.0f, kHookshotRange, hook);
    setConst(daHookshot_param(WWHD_HOOKSHOT_OFF_SHOT_SPEED), 15.0f, kHookshotShot, hook);
    setConst(daHookshot_param(WWHD_HOOKSHOT_OFF_RETURN_SPEED), 63.0f, kHookshotReturn, hook);

    const bool boom = s_enabled[MOD_FAST_BOOMERANG];
    setConst(daBoomerang_getFlyMaxPtr(0), daBoomerang_STOCK_FLY_MAX, kBoomerangRange, boom);
    setConst(daBoomerang_getFlyMaxPtr(1), daBoomerang_STOCK_FLY_MAX_FAR, kBoomerangRangeFar, boom);

    const bool grapple = s_enabled[MOD_LONG_GRAPPLE];
    setConst(daHimo2_param(WWHD_GRAPPLE_OFF_RANGE), 1000.0f, kGrappleRange, grapple);
    setConst(daHimo2_param(WWHD_GRAPPLE_OFF_VERTICAL), 1500.0f, kGrappleVertical, grapple);
    setConst(daHimo2_param(WWHD_GRAPPLE_OFF_AIM_WIDTH), 60.0f, kGrappleAim, grapple);
}

// Console rewrites the instruction; under Cemu the pack reads PackFlags instead.
static void syncArmorPatch()
{
    const bool want = s_enabled[MOD_FREE_MAGIC_ARMOR];
    if (App::g_underCemu || want == s_armorPatched || !wwhd_regionResolved)
        return;
    if (s_armorFailed && s_armorFailedWant == want)
        return;
    s_armorPatch.linkAddr = wwhd_map->magicArmorCostSite;
    if (App::SetCodePatch(&s_armorPatch, want)) {
        s_armorPatched = want;
        s_armorFailed = false;
        return;
    }
    s_armorFailed = true;
    s_armorFailedWant = want;
    Logger::LogWarn("[equipment] magic armor patch %s failed", want ? "apply" : "removal");
}

void Tick(bool acceptInput)
{
    applyConsts();
    syncArmorPatch();
    if (acceptInput && s_enabled[MOD_REMOTE_BOMBS] &&
        Hotkeys::Pressed(Hotkeys::HOTKEY_DETONATE_BOMBS))
        s_detonate = true;
}

// A held fuse is topped up to full; a detonated one is set to 1 so the game explodes it itself.
static void tendBombs()
{
    const bool detonate = s_detonate;
    s_detonate = false;
    for (wwhd_gptr_t node = fopAcTg_firstNode(); node; node = fopAcTg_nextNode(node)) {
        u8* bomb = daBomb_asLinkBomb(fopAcTg_nodeActor(node));
        if (!bomb)
            continue;
        s16* rest = daBomb_restTime(bomb);
        if (*rest <= 1)
            continue;
        if (detonate && !daBomb_isCarried(bomb))
            *rest = 1;
        else if (*rest < daBomb_FUSE)
            *rest = daBomb_FUSE;
    }
}

// FF1 takes the sword on arrival; give back the one Link had, or the Hero's Sword.
static void restoreFf1Sword()
{
    const int type = dStage_stagInfo_getStageType(dComIfGp_getStageStagInfo());
    const u8 sword = dSv_getEquip(WWHD_EQUIP_SWORD);
    if (type != WWHD_STAGE_TYPE_FF1) {
        if (sword != (u8)WWHD_ITEM_NONE)
            s_savedSword = sword;
        return;
    }
    if (sword == (u8)WWHD_ITEM_NONE)
        dSv_setSword(s_savedSword != (u8)WWHD_ITEM_NONE ? s_savedSword : (u8)dItemNo_HEROS_SWORD);
}

static void tendSpin(daPy_lk_c* link)
{
    if (link->mCurProc != daPyProc_CUT_TURN_MOVE_e)
        return;
    s16* charge = (s16*)((u8*)link + WWHD_DAPY_OFF_SPIN_CHARGE);
    const s16 full = s_enabled[MOD_QUICK_SPIN] ? kQuickSpinCharge : (s16)WWHD_SPIN_CHARGE_FRAMES;
    if (s_enabled[MOD_FREE_SPIN] && *charge == -1) {
        const u16 held = *(const u16*)((const u8*)link + WWHD_DAPY_OFF_EQUIP_ITEM);
        const u8 game = dComIfGp_getMiniGameType();
        if (dComIfGs_isEventBit(WWHD_EVFLAG_HURRICANE_SPIN) && held == WWHD_ITEM_EQUIP_SWORD &&
            game != 2 && game != 6)
            *charge = full;
    }
    if (s_enabled[MOD_QUICK_SPIN] && *charge > kQuickSpinCharge)
        *charge = kQuickSpinCharge;
}

static void moveLinkY(daPy_lk_c* link, f32 dy)
{
    cXyz* store = daPy_getStorePos();
    if (store)
        store->y += dy;
    link->base.current.pos.y += dy;
    link->base.old.pos.y += dy;
}

// The game climbs 5 a frame up and up to 27 down; add the rest of the scale on top.
static void tendRopeClimb(daPy_lk_c* link)
{
    const s32 proc = link->mCurProc;
    const f32 y = link->base.current.pos.y;
    const bool sameProc = proc == s_lastProc;
    const f32 lastY = s_lastY;
    s_lastProc = proc;
    s_lastY = y;
    if (!sameProc)
        return;

    u8* base = (u8*)link;
    f32* anmRate = (f32*)(base + WWHD_DAPY_OFF_LOWER_ANM_RATE);
    if (proc == daPyProc_ROPE_UP_e) {
        if (*anmRate >= 0.01f && *anmRate < kClimbAnmRate * kClimbScale)
            *anmRate = kClimbAnmRate * kClimbScale;
        const f32 target = *(const f32*)(base + WWHD_DAPY_OFF_ROPE_TARGET);
        f32 extra = (kClimbScale - 1.0f) * WWHD_ROPE_UP_STEP;
        if (extra > target - y)
            extra = target - y;
        if (y > lastY && extra > 0.0f) {
            moveLinkY(link, extra);
            s_lastY = y + extra;
        }
    } else if (proc == daPyProc_ROPE_DOWN_e) {
        const f32 step = *(const f32*)(base + WWHD_DAPY_OFF_ROPE_TARGET);
        const f32 bottom = *(const f32*)(base + WWHD_DAPY_OFF_ROPE_BOTTOM);
        f32 extra = (kClimbScale - 1.0f) * step;
        if (extra > y - bottom)
            extra = y - bottom;
        if (y < lastY && extra > 0.0f) {
            moveLinkY(link, -extra);
            s_lastY = y - extra;
        }
    }
}

void OnFrameEarly()
{
    if (s_enabled[MOD_REMOTE_BOMBS])
        tendBombs();
    else
        s_detonate = false;

    if (s_enabled[MOD_UNRESTRICTED_ITEMS]) {
        if (u8* mode = dComIfGp_getButtonActionMode())
            *mode |= WWHD_BUTTON_MODE_B | WWHD_BUTTON_MODE_X | WWHD_BUTTON_MODE_Y |
                     WWHD_BUTTON_MODE_R | WWHD_BUTTON_MODE_TOUCH;
        restoreFf1Sword();
    }

    daPy_lk_c* link = daPy_lk_c_getPlayer();
    if (!link) {
        s_lastProc = -1;
        return;
    }
    u8* base = (u8*)link;

    if (s_enabled[MOD_NO_BOMB_LIMIT])
        base[daPy_OFF_activeBombs] = 0;
    if (s_enabled[MOD_QUICK_MAGIC_ARROWS])
        *daPy_u32At(link, WWHD_DAPY_OFF_FLAGS1) &= ~WWHD_DAPY_FLAGS1_ARROW_EFFECT;
    if (s_enabled[MOD_FAST_BOOMERANG]) {
        fopAc_ac_c* boomerang = daBoomerang_findThrown();
        if (boomerang && boomerang->speedF > 0.0f && boomerang->speedF < kBoomerangSpeed)
            boomerang->speedF = kBoomerangSpeed;
    }
    if (s_enabled[MOD_QUICK_SPIN] || s_enabled[MOD_FREE_SPIN])
        tendSpin(link);
    if (s_enabled[MOD_FAST_ROPE_CLIMB])
        tendRopeClimb(link);
    else
        s_lastProc = -1;
}

// The boots halve the stick before the speed target is taken; double the speed back, capped at a run.
void OnMoveProc(void* self)
{
    if (!self || !s_enabled[MOD_FAST_IRON_BOOTS])
        return;
    daPy_lk_c* link = (daPy_lk_c*)self;
    if (link != daPy_lk_c_getPlayer() || !daPy_hasIronBootsOn(link))
        return;

    f32* speed = (f32*)((u8*)self + WWHD_DAPY_OFF_NORMAL_SPEED);
    f32 stick = *(const f32*)((const u8*)self + WWHD_DAPY_OFF_STICK_DISTANCE) /
                WWHD_IRON_BOOTS_STICK_SCALE;
    if (stick > 1.0f)
        stick = 1.0f;
    const f32 cap = link->mMaxNormalSpeed * stick;
    f32 boosted = *speed / WWHD_IRON_BOOTS_STICK_SCALE;
    if (boosted > cap)
        boosted = cap;
    else if (boosted < -cap)
        boosted = -cap;
    *speed = boosted;
}

void OnApplicationStart()
{
    s_armorPatched = false;
    s_armorFailed = false;
    s_detonate = false;
    s_lastProc = -1;
    s_savedSword = (u8)WWHD_ITEM_NONE;
}

void ResetToDefaults()
{
    for (int i = 0; i < MOD_COUNT; ++i)
        s_enabled[i] = false;
    applyConsts();
    syncArmorPatch();
}
}
}
