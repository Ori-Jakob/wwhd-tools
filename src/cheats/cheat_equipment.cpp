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

struct LevelInfo {
    const char* name;
    const char* key;
    Mod         mod;
    int         lo, hi, fallback;
};

static const LevelInfo kLevels[LEVEL_COUNT] = {
    { "Range",       "equipBoomerangRange", MOD_FAST_BOOMERANG,  1, 4, 2 },
    { "Speed",       "equipBoomerangSpeed", MOD_FAST_BOOMERANG,  1, 4, 2 },
    { "Range",       "equipHookshotRange",  MOD_SUPER_HOOKSHOT,  1, 20, 2 },
    { "Speed",       "equipHookshotSpeed",  MOD_SUPER_HOOKSHOT,  1, 25, 2 },
    { "Range",       "equipGrappleRange",   MOD_LONG_GRAPPLE,    2, 4, 2 },
    { "Climb speed", "equipClimbSpeed",     MOD_FAST_ROPE_CLIMB, 2, 4, 2 },
};

static const f32 kHookshotStockPitch  = 7.0f;
static const f32 kHookshotStockRange  = 1500.0f;
static const f32 kHookshotStockShot   = 15.0f;
static const f32 kHookshotStockReturn = 63.0f;
// Without the chain patches the link model (10 long) and the 300-link chain cap bound everything.
static const f32 kHookshotPitch = 10.0f;
static const f32 kHookshotLinks = 300.0f;
static const u32 kHookshotPackVersion = 4;
// Past this the unpatched range cap would shrink below stock.
static const f32 kHookshotUnpatchedSpeedMax = 5.0f;
static const int kStuckPullFrames = 20;

static const f32 kBoomerangStockSpeed = 60.0f;
static const f32 kClimbAnmRate = 0.8f;
static const s16 kQuickSpinCharge = 1;

static bool s_enabled[MOD_COUNT] = {};
static int  s_levels[LEVEL_COUNT] = { 2, 2, 2, 2, 2, 2 };
static bool s_detonate = false;
static u8   s_savedSword = (u8)WWHD_ITEM_NONE;
static s32  s_lastProc = -1;
static f32  s_lastY = 0.0f;

// Code patches a mod needs on console, added and removed together; the Cemu pack has its own.
struct PatchGroup {
    const char* name;
    RplHook*    hooks;
    int         count;
    bool        applied;
    bool        failed;
    bool        failedWant;
};

static RplHook s_armorHooks[1];
static RplHook s_bootsHooks[WWHD_HEAVY_WALK_SITES];
static RplHook s_hookshotHooks[5];
static PatchGroup s_armorGroup = { "magic armor", s_armorHooks, 1, false, false, false };
static PatchGroup s_bootsGroup = { "iron boots", s_bootsHooks, WWHD_HEAVY_WALK_SITES,
                                   false, false, false };
static PatchGroup s_hookshotGroup = { "hookshot", s_hookshotHooks, 5, false, false, false };
static s32 s_pullCount = 0;
static int s_pullStall = 0;
static bool s_hooksBuilt = false;
static f32* s_hsPitch = nullptr;

typedef int (*daPy_checkHeavyStateOn_t)(void* self);

// Called in place of checkHeavyStateOn where it shapes walking: the boots alone do not count.
static int heavyWithoutBoots(void* self)
{
    const daPy_checkHeavyStateOn_t real =
        WWHD_FN(daPy_checkHeavyStateOn_t, wwhd_map->daPy_checkHeavyStateOn);
    u32* flags = (u32*)((u8*)self + WWHD_DAPY_OFF_FLAGS0);
    if (!s_enabled[MOD_FAST_IRON_BOOTS] || !(*flags & WWHD_DAPY_FLAGS0_IRON_BOOTS))
        return real(self);
    *flags &= ~WWHD_DAPY_FLAGS0_IRON_BOOTS;
    const int heavy = real(self);
    *flags |= WWHD_DAPY_FLAGS0_IRON_BOOTS;
    return heavy;
}

// Sets an FPR the way lfs does, both paired-single halves.
static void setSingle(RplContext* ctx, int f, f32 value)
{
    ctx->fpr[f] = value;
    ctx->ps1[f] = value;
}

// dBgS::ChkPolyHSStick, before it tests the stick bit: any polygon sticks but lava and void.
static void hookshotStickHook(RplContext* ctx)
{
    if (!s_enabled[MOD_SUPER_HOOKSHOT])
        return;
    const u32 info = *(const u32*)(uintptr_t)(ctx->gpr[12] + 4u);
    const u32 attr = (info >> 16) & 0x1Fu;
    if (attr != WWHD_HS_ATTR_LAVA && attr != WWHD_HS_ATTR_VOID)
        ctx->gpr[9] |= WWHD_HS_STICK_BIT;
}

// The chain draw, after its pitch load: the real length (count links at the hookshot's pitch)
// laid at the stock step, stretched once that needs more than the 300 links it can hold.
static void hookshotDrawHook(RplContext* ctx)
{
    // The sine table stays live past the site, and r11 may be spent on the way in.
    ctx->gpr[11] = ctx->gpr[12] - ctx->gpr[10];
    f32 step = -(f32)ctx->fpr[12];
    if (step <= 0.0f)
        step = kHookshotStockPitch;
    const f32 pitch = s_hsPitch ? *s_hsPitch : step;
    const f32 length = (f32)(s32)ctx->gpr[31] * pitch + 0.5f * (pitch - step);
    s32 drawn = (s32)(length / step);
    f32 spacing = step;
    if (drawn > WWHD_HS_CHAIN_MAX) {
        drawn = WWHD_HS_CHAIN_MAX;
        spacing = length / (f32)WWHD_HS_CHAIN_MAX;
        setSingle(ctx, 12, -spacing);
    } else if (drawn < 1) {
        drawn = 1;
    }
    setSingle(ctx, 11, spacing);
    ctx->gpr[31] = (u32)drawn;
}

static void buildHooks()
{
    if (s_hooksBuilt || !wwhd_regionResolved)
        return;
    const RplHook armor = RPL_HOOK_REWRITE("magicArmorCost", 0u, 0x1C000014u,
                                           RPL_HOOK_OPTIONAL, 0x1C000000u);
    s_armorHooks[0] = armor;
    s_armorHooks[0].linkAddr = wwhd_map->magicArmorCostSite;
    for (int i = 0; i < WWHD_HEAVY_WALK_SITES; ++i) {
        const wwhd_codeSite_t* site = &wwhd_heavyWalkSites[i];
        const RplHook call = RPL_HOOK_CALL("ironBootsWalk", 0u, 0u, RPL_HOOK_OPTIONAL,
                                           (const void*)&heavyWithoutBoots, nullptr);
        s_bootsHooks[i] = call;
        s_bootsHooks[i].linkAddr = wwhd_codeSiteAddr(site);
        s_bootsHooks[i].expected = site->word;
    }

    // The reticle loads the hookshot's range; its words carry the data address, so relocate them.
    const u32 range = wwhd_map->hookshotParams + WWHD_HOOKSHOT_OFF_RANGE + wwhd_dataDelta;
    const u32 rangeHa = ((range + 0x8000u) >> 16) & 0xFFFFu;
    const RplHook stick = RPL_HOOK_CONTEXT("hookshotStick", 0u, WWHD_HS_STICK_WORD,
                                           RPL_HOOK_OPTIONAL, &hookshotStickHook);
    const RplHook draw = RPL_HOOK_CONTEXT("hookshotDraw", 0u, WWHD_HS_DRAW_SHIFT_WORD,
                                          RPL_HOOK_OPTIONAL, &hookshotDrawHook);
    const RplHook sightLis = RPL_HOOK_REWRITE("hookshotSightHi", 0u, WWHD_HS_SIGHT_LIS_WORD,
                                              RPL_HOOK_OPTIONAL, 0x3D000000u | rangeHa);
    const RplHook sightLfs = RPL_HOOK_REWRITE("hookshotSightLo", 0u, WWHD_HS_SIGHT_LFS_WORD,
                                              RPL_HOOK_OPTIONAL, 0xC0280000u | (range & 0xFFFFu));
    const RplHook clamp = RPL_HOOK_REWRITE("hookshotChain", 0u, WWHD_HS_CLAMP_WORD,
                                           RPL_HOOK_OPTIONAL, RPL_NOP);
    // The draw goes in before the clamp comes out, and leaves after it is back.
    s_hookshotHooks[0] = stick;
    s_hookshotHooks[0].linkAddr = wwhd_map->hsStickSite;
    s_hookshotHooks[1] = draw;
    s_hookshotHooks[1].linkAddr = wwhd_map->hsDrawPitchSite + 4u;
    s_hookshotHooks[2] = sightLis;
    s_hookshotHooks[2].linkAddr = wwhd_map->hsSightLisSite;
    s_hookshotHooks[3] = sightLfs;
    s_hookshotHooks[3].linkAddr = wwhd_map->hsSightLfsSite;
    s_hookshotHooks[4] = clamp;
    s_hookshotHooks[4].linkAddr = wwhd_map->hsChainClampSite;
    s_hsPitch = daHookshot_param(WWHD_HOOKSHOT_OFF_PITCH);
    s_hooksBuilt = true;
}

static bool valid(Mod mod) { return mod >= 0 && mod < MOD_COUNT; }

const char* Name(Mod mod)      { return valid(mod) ? kInfo[mod].name : "?"; }
const char* ConfigKey(Mod mod) { return valid(mod) ? kInfo[mod].key : ""; }
bool IsEnabled(Mod mod)        { return valid(mod) && s_enabled[mod]; }

void SetEnabled(Mod mod, bool enabled)
{
    if (valid(mod))
        s_enabled[mod] = enabled;
}

static bool validLevel(Level level) { return level >= 0 && level < LEVEL_COUNT; }

const char* LevelName(Level level) { return validLevel(level) ? kLevels[level].name : "?"; }
const char* LevelKey(Level level)  { return validLevel(level) ? kLevels[level].key : ""; }
Mod         LevelMod(Level level)  { return validLevel(level) ? kLevels[level].mod : MOD_COUNT; }
int         LevelMin(Level level)  { return validLevel(level) ? kLevels[level].lo : 1; }
int         LevelMax(Level level)  { return validLevel(level) ? kLevels[level].hi : 1; }
int         GetLevel(Level level)  { return validLevel(level) ? s_levels[level] : 1; }

void SetLevel(Level level, int value)
{
    if (!validLevel(level))
        return;
    if (value < kLevels[level].lo) value = kLevels[level].lo;
    if (value > kLevels[level].hi) value = kLevels[level].hi;
    s_levels[level] = value;
}

static f32 levelScale(Level level) { return (f32)s_levels[level]; }

uint32_t PackFlags()
{
    uint32_t flags = 0;
    if (s_enabled[MOD_FREE_MAGIC_ARMOR])
        flags |= PACK_FLAG_FREE_MAGIC_ARMOR;
    if (s_enabled[MOD_FAST_IRON_BOOTS])
        flags |= PACK_FLAG_NORMAL_IRON_BOOTS;
    if (s_enabled[MOD_SUPER_HOOKSHOT])
        flags |= PACK_FLAG_HOOKSHOT_ANY;
    return flags;
}

// The chain patches lift the 300-link cap and draw the chain at the stock pitch at any speed.
static bool hookshotPatched()
{
    return App::g_underCemu ? App::PackVersion() >= kHookshotPackVersion
                            : s_hookshotGroup.applied;
}

enum ConstSlot {
    CONST_HOOK_PITCH, CONST_HOOK_PITCH_Y, CONST_HOOK_RANGE, CONST_HOOK_SHOT, CONST_HOOK_RETURN,
    CONST_BOOM_RANGE, CONST_BOOM_RANGE_FAR,
    CONST_GRAPPLE_RANGE, CONST_GRAPPLE_VERTICAL, CONST_GRAPPLE_AIM,
    CONST_COUNT
};
static f32  s_written[CONST_COUNT];
static bool s_wrote[CONST_COUNT];

// Only the stock value or the last one written is overwritten, so a wrong address is left alone.
static void setConst(ConstSlot slot, f32* p, f32 stock, bool on, f32 value)
{
    if (!p)
        return;
    const f32 cur = *p;
    if (cur != stock && !(s_wrote[slot] && cur == s_written[slot]))
        return;
    const f32 want = on ? value : stock;
    if (cur != want)
        *p = want;
    s_written[slot] = want;
    s_wrote[slot] = true;
}

// Patched, the pitch carries the speed: shot, return and pull all move k times as far a frame.
// Unpatched, range plus one shot step must fit the 300 drawn links.
static void applyHookshot()
{
    const bool on = s_enabled[MOD_SUPER_HOOKSHOT];
    const f32 speed = levelScale(LEVEL_HOOKSHOT_SPEED);
    f32 range = kHookshotStockRange * levelScale(LEVEL_HOOKSHOT_RANGE);
    f32 pitch, drawPitch, shot, ret;
    if (hookshotPatched()) {
        pitch = kHookshotStockPitch * speed;
        drawPitch = kHookshotStockPitch;
        shot = kHookshotStockShot;
        ret = kHookshotStockReturn * speed;
    } else {
        const f32 slow = speed < kHookshotUnpatchedSpeedMax ? speed : kHookshotUnpatchedSpeedMax;
        const f32 shotStep = kHookshotStockPitch * kHookshotStockShot * slow;
        pitch = drawPitch = kHookshotPitch;
        shot = shotStep / kHookshotPitch;
        const f32 reach = kHookshotPitch * kHookshotLinks - shotStep;
        if (range > reach)
            range = reach;
        ret = kHookshotStockReturn * slow;
        if (ret > kHookshotPitch * 19.0f)
            ret = kHookshotPitch * 19.0f;
    }

    setConst(CONST_HOOK_PITCH, daHookshot_param(WWHD_HOOKSHOT_OFF_PITCH),
             kHookshotStockPitch, on, pitch);
    setConst(CONST_HOOK_PITCH_Y, daHookshot_param(WWHD_HOOKSHOT_OFF_PITCH_Y),
             -kHookshotStockPitch, on, -drawPitch);
    setConst(CONST_HOOK_RANGE, daHookshot_param(WWHD_HOOKSHOT_OFF_RANGE),
             kHookshotStockRange, on, range);
    setConst(CONST_HOOK_SHOT, daHookshot_param(WWHD_HOOKSHOT_OFF_SHOT_SPEED),
             kHookshotStockShot, on, shot);
    setConst(CONST_HOOK_RETURN, daHookshot_param(WWHD_HOOKSHOT_OFF_RETURN_SPEED),
             kHookshotStockReturn, on, ret);
}

static void applyConsts()
{
    applyHookshot();

    const bool boom = s_enabled[MOD_FAST_BOOMERANG];
    const f32 boomRange = levelScale(LEVEL_BOOMERANG_RANGE);
    setConst(CONST_BOOM_RANGE, daBoomerang_getFlyMaxPtr(0), daBoomerang_STOCK_FLY_MAX,
             boom, daBoomerang_STOCK_FLY_MAX * boomRange);
    setConst(CONST_BOOM_RANGE_FAR, daBoomerang_getFlyMaxPtr(1), daBoomerang_STOCK_FLY_MAX_FAR,
             boom, daBoomerang_STOCK_FLY_MAX_FAR * boomRange);

    const bool grapple = s_enabled[MOD_LONG_GRAPPLE];
    const f32 reach = levelScale(LEVEL_GRAPPLE_RANGE);
    setConst(CONST_GRAPPLE_RANGE, daHimo2_param(WWHD_GRAPPLE_OFF_RANGE), 1000.0f,
             grapple, 1000.0f * reach);
    setConst(CONST_GRAPPLE_VERTICAL, daHimo2_param(WWHD_GRAPPLE_OFF_VERTICAL), 1500.0f,
             grapple, 1500.0f * (1.0f + (reach - 1.0f) * 0.5f));
    setConst(CONST_GRAPPLE_AIM, daHimo2_param(WWHD_GRAPPLE_OFF_AIM_WIDTH), 60.0f,
             grapple, 60.0f * reach);
}

// Console patches the code; under Cemu the pack reads PackFlags instead. A group goes in in order,
// comes out in reverse, and a half-applied one is undone.
static RplHook* groupHook(PatchGroup& g, int step, bool want)
{
    return &g.hooks[want ? step : g.count - 1 - step];
}

static void syncGroup(PatchGroup& g, bool want)
{
    if (App::g_underCemu || want == g.applied || !s_hooksBuilt)
        return;
    if (g.failed && g.failedWant == want)
        return;
    int done = 0;
    while (done < g.count && App::SetCodePatch(groupHook(g, done, want), want))
        ++done;
    if (done == g.count) {
        g.applied = want;
        g.failed = false;
        return;
    }
    while (done-- > 0)
        App::SetCodePatch(groupHook(g, done, want), !want);
    g.failed = true;
    g.failedWant = want;
    Logger::LogWarn("[equipment] %s patch %s failed at %d of %d", g.name,
                    want ? "apply" : "removal", done + 1, g.count);
}

static void syncPatches()
{
    buildHooks();
    syncGroup(s_armorGroup, s_enabled[MOD_FREE_MAGIC_ARMOR]);
    syncGroup(s_bootsGroup, s_enabled[MOD_FAST_IRON_BOOTS]);
    syncGroup(s_hookshotGroup, s_enabled[MOD_SUPER_HOOKSHOT]);
}

// A hook on the floor or behind a lip can hold Link in the pull; send it back when it stops closing.
static void watchPull(daPy_lk_c* link)
{
    if (link->mCurProc != daPyProc_HOOKSHOT_FLY_e) {
        s_pullStall = 0;
        return;
    }
    const wwhd_gptr_t p = *(const wwhd_gptr_t*)((const u8*)link + daPy_OFF_equipActor);
    if (!p)
        return;
    u8* hook = WWHD_AT(u8, p);
    s32* mode = (s32*)(hook + daHookshot_OFF_mode);
    const s32 count = *(const s32*)(hook + daHookshot_OFF_chainCnt);
    if (*mode != WWHD_HOOKSHOT_MODE_PULL) {
        s_pullStall = 0;
        return;
    }
    if (s_pullStall == 0 || count < s_pullCount) {
        s_pullCount = count;
        s_pullStall = 1;
        return;
    }
    if (++s_pullStall > kStuckPullFrames) {
        *mode = WWHD_HOOKSHOT_MODE_RETURN;
        s_pullStall = 0;
    }
}

void Tick(bool acceptInput)
{
    applyConsts();
    syncPatches();
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
    const f32 scale = levelScale(LEVEL_CLIMB_SPEED);
    f32* anmRate = (f32*)(base + WWHD_DAPY_OFF_LOWER_ANM_RATE);
    if (proc == daPyProc_ROPE_UP_e) {
        if (*anmRate >= 0.01f && *anmRate < kClimbAnmRate * scale)
            *anmRate = kClimbAnmRate * scale;
        const f32 target = *(const f32*)(base + WWHD_DAPY_OFF_ROPE_TARGET);
        f32 extra = (scale - 1.0f) * WWHD_ROPE_UP_STEP;
        if (extra > target - y)
            extra = target - y;
        if (y > lastY && extra > 0.0f) {
            moveLinkY(link, extra);
            s_lastY = y + extra;
        }
    } else if (proc == daPyProc_ROPE_DOWN_e) {
        const f32 step = *(const f32*)(base + WWHD_DAPY_OFF_ROPE_TARGET);
        const f32 bottom = *(const f32*)(base + WWHD_DAPY_OFF_ROPE_BOTTOM);
        f32 extra = (scale - 1.0f) * step;
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
        const f32 speed = kBoomerangStockSpeed * levelScale(LEVEL_BOOMERANG_SPEED);
        fopAc_ac_c* boomerang = daBoomerang_findThrown();
        if (boomerang && boomerang->speedF > 0.0f && boomerang->speedF < speed)
            boomerang->speedF = speed;
    }
    if (s_enabled[MOD_QUICK_SPIN] || s_enabled[MOD_FREE_SPIN])
        tendSpin(link);
    if (s_enabled[MOD_SUPER_HOOKSHOT])
        watchPull(link);
    if (s_enabled[MOD_FAST_ROPE_CLIMB])
        tendRopeClimb(link);
    else
        s_lastProc = -1;
}

void OnApplicationStart()
{
    s_armorGroup.applied = s_armorGroup.failed = false;
    s_bootsGroup.applied = s_bootsGroup.failed = false;
    s_hookshotGroup.applied = s_hookshotGroup.failed = false;
    s_hooksBuilt = false;
    s_pullStall = 0;
    s_detonate = false;
    s_lastProc = -1;
    s_savedSword = (u8)WWHD_ITEM_NONE;
}

void ResetToDefaults()
{
    for (int i = 0; i < MOD_COUNT; ++i)
        s_enabled[i] = false;
    for (int i = 0; i < LEVEL_COUNT; ++i)
        s_levels[i] = kLevels[i].fallback;
    applyConsts();
    syncPatches();
}
}
}
