#include "tools/zombie_hover.h"

#include "core/hotkeys.h"
#include "core/input.h"
#include "core/logger.h"
#include "core/settings.h"
#include "libwwhd/libwwhd.h"
#include "tools/flycam.h"
#include "ui/notifications.h"

#include <vpad/input.h>

#include <string.h>

namespace Tools {
namespace ZombieHover {
static const uint32_t kAcchGroundHit = 0x20u;

static const uint32_t kModeFlagOffset = 0x6A70u;
static const uint32_t kModeFlagDamage = 0x8u;

static const u16 kHealQuarters = 1;

static Run  s_live;
static Run  s_last;
static Run  s_history[HISTORY];
static int  s_historyCount = 0;

static bool     s_hovering = false;
static s32      s_lastProc = -1;
static bool     s_haveReference = false;
static uint32_t s_referenceFrame = 0;
static uint32_t s_streak = 0;

static bool     s_simulating = false;
static bool     s_simAirborne = false;
static bool     s_simHoldExtra = false;
static uint32_t s_simCountdown = 0;
static uint32_t s_simRng = 0x2545F491u;

static bool s_restoreValid = false;
static u16  s_restoreLife = 0;
static bool s_restoreFairy[WWHD_BOTTLE_COUNT];

bool IsEnabled()          { return Config::g_settings.zombieHoverEnabled; }
void SetEnabled(bool on)  { Config::g_settings.zombieHoverEnabled = on; }
bool IsHovering()         { return s_hovering; }
bool IsSimulating()       { return s_simulating; }

const Run& Current()      { return s_hovering ? s_live : s_last; }

int HistoryCount()        { return s_historyCount; }

const Run& HistoryAt(int index)
{
    static const Run kEmpty = {};
    if (index < 0 || index >= s_historyCount)
        return kEmpty;
    return s_history[index];
}

Grade GradeForGap(uint32_t gap)
{
    if (gap <= (uint32_t)GAP_PERFECT) return GRADE_PERFECT;
    if (gap == (uint32_t)GAP_GOOD)    return GRADE_GOOD;
    if (gap == (uint32_t)GAP_OK)      return GRADE_OK;
    return GRADE_BAD;
}

const char* GradeName(uint8_t grade)
{
    switch (grade) {
    case GRADE_FIRST:   return "First";
    case GRADE_PERFECT: return "Perfect";
    case GRADE_GOOD:    return "Good";
    case GRADE_OK:      return "OK";
    case GRADE_BAD:     return "Bad";
    case GRADE_HELD:    return "Too fast";
    case GRADE_WASTED:  return "No attack";
    }
    return "?";
}

const char* EndReasonName(uint8_t reason)
{
    switch (reason) {
    case END_NONE:   return "Hovering";
    case END_GROUND: return "Landed";
    case END_WATER:  return "Water";
    case END_SHIP:   return "Ship";
    case END_DIED:   return "Died";
    case END_HEALED: return "Healed";
    case END_LOST:   return "Lost";
    case END_RESET:  return "Reset";
    }
    return "?";
}

float HeightPerAttack(uint32_t gap)
{
    if (gap < 1) gap = 1;
    const float falling = (float)(gap - 1);
    return 15.0f - 2.5f * falling * (falling + 1.0f) * 0.5f;
}

static void clearRun(Run& r)
{
    memset(&r, 0, sizeof(r));
}

static void pushHistory(const Run& r)
{
    if (s_historyCount < HISTORY)
        ++s_historyCount;
    for (int i = s_historyCount - 1; i > 0; --i)
        s_history[i] = s_history[i - 1];
    s_history[0] = r;
}

static void startRun(float y)
{
    clearRun(s_live);
    s_live.valid = true;
    s_live.startY = y;
    s_hovering = true;
    s_haveReference = false;
    s_streak = 0;
    s_lastProc = -1;
    Logger::Log("zombie hover: run started at y=%.1f", (double)y);
}

static void recordAttack(Run& r)
{
    Input in;
    in.frame = r.frames > 0xFFFFu ? (uint16_t)0xFFFFu : (uint16_t)r.frames;
    if (!s_haveReference) {
        in.gap = 0;
        in.grade = GRADE_FIRST;
        s_streak = 0;
    } else {
        const uint32_t gap = r.frames - s_referenceFrame;
        in.gap = gap > 255u ? (uint8_t)255u : (uint8_t)gap;
        in.grade = (uint8_t)GradeForGap(gap);
        r.gapSum += gap;
        if (gap > r.maxGap) r.maxGap = gap;
        if (in.grade == GRADE_PERFECT) {
            if (++s_streak > r.perfectStreak) r.perfectStreak = s_streak;
        } else {
            s_streak = 0;
        }
    }
    r.counts[in.grade]++;
    r.inputs++;
    if (r.recorded < (uint32_t)MAX_INPUTS)
        r.samples[r.recorded++] = in;
    s_haveReference = true;
    s_referenceFrame = r.frames;
}

static void recordEvent(Run& r, Grade grade)
{
    Input in;
    in.frame = r.frames > 0xFFFFu ? (uint16_t)0xFFFFu : (uint16_t)r.frames;
    in.gap = 0;
    in.grade = (uint8_t)grade;
    r.counts[grade]++;
    if (r.recorded < (uint32_t)MAX_INPUTS)
        r.samples[r.recorded++] = in;
}

static void endRun(EndReason reason, bool healed)
{
    if (!s_hovering)
        return;
    s_hovering = false;
    s_live.endReason = (uint8_t)reason;
    s_live.healed = healed;

    Logger::Log("zombie hover: %s after %u frames, %u attacks (P%u G%u O%u B%u), %u held, %u wasted%s, "
                "gain %.1f max %.1f",
                EndReasonName(reason), (unsigned)s_live.frames, (unsigned)s_live.inputs,
                (unsigned)s_live.counts[GRADE_PERFECT], (unsigned)s_live.counts[GRADE_GOOD],
                (unsigned)s_live.counts[GRADE_OK], (unsigned)s_live.counts[GRADE_BAD],
                (unsigned)s_live.counts[GRADE_HELD], (unsigned)s_live.counts[GRADE_WASTED],
                healed ? ", healed" : "", (double)s_live.gainY, (double)s_live.maxGainY);

    if (s_live.inputs == 0)
        return;

    s_last = s_live;
    pushHistory(s_live);

    const uint32_t graded = s_live.inputs - s_live.counts[GRADE_FIRST];
    const unsigned perfectPct =
        graded ? (unsigned)(s_live.counts[GRADE_PERFECT] * 100u / graded) : 0u;
    Notifications::ShowKeyedTitledf("zombie_hover", Notifications::Info, "Zombie hover",
                                    "%s: %.2f s, %u attacks, %u%% perfect",
                                    EndReasonName(reason), (double)s_live.frames / 30.0,
                                    (unsigned)s_live.inputs, perfectPct);
}

void ResetStats()
{
    if (s_hovering)
        endRun(END_RESET, false);
    clearRun(s_live);
    clearRun(s_last);
    s_hovering = false;
    s_haveReference = false;
    s_streak = 0;
}

void ClearHistory()
{
    for (int i = 0; i < HISTORY; ++i)
        clearRun(s_history[i]);
    s_historyCount = 0;
}

static bool airborneProc(s32 proc)
{
    return proc == (s32)daPyProc_FALL_e || proc == (s32)daPyProc_JUMP_CUT_e;
}

bool CanRestorePractice() { return s_restoreValid; }

bool SetupPractice()
{
    if (!wwhd_regionResolved || !dComIfGs_getPlayerSave()) {
        Notifications::ShowKeyed("zombie_hover", Notifications::Error, "Zombie hover",
                                 "No game in progress.");
        return false;
    }
    if (!s_restoreValid) {
        s_restoreLife = dSv_getLife();
        for (int i = 0; i < WWHD_BOTTLE_COUNT; ++i)
            s_restoreFairy[i] = false;
    }
    int taken = 0;
    for (int i = 0; i < WWHD_BOTTLE_COUNT; ++i) {
        if (dSv_getBottle(i) != dItemNo_FAIRY)
            continue;
        dSv_setBottle(i, dItemNo_EMPTY_BOTTLE);
        s_restoreFairy[i] = true;
        ++taken;
    }
    dComIfGp_clearItemDeltas();
    dSv_setLife(kHealQuarters);
    s_restoreValid = true;
    Logger::Log("zombie hover: practice setup, life %u -> 1, %d fairies taken",
                (unsigned)s_restoreLife, taken);
    if (taken)
        Notifications::ShowKeyedTitledf("zombie_hover", Notifications::Info, "Zombie hover",
                                        "One quarter heart, %d %s bottled", taken,
                                        taken == 1 ? "fairy" : "fairies");
    else
        Notifications::ShowKeyed("zombie_hover", Notifications::Info, "Zombie hover",
                                 "One quarter heart");
    return true;
}

bool RestorePractice()
{
    if (!s_restoreValid || !wwhd_regionResolved || !dComIfGs_getPlayerSave())
        return false;
    int given = 0;
    for (int i = 0; i < WWHD_BOTTLE_COUNT; ++i) {
        if (!s_restoreFairy[i])
            continue;
        if (dSv_getBottle(i) == dItemNo_EMPTY_BOTTLE) {
            dSv_setBottle(i, dItemNo_FAIRY);
            ++given;
        }
        s_restoreFairy[i] = false;
    }
    dComIfGp_clearItemDeltas();
    dSv_setLife(s_restoreLife);
    s_restoreValid = false;
    Logger::Log("zombie hover: practice restored, life %u, %d fairies back",
                (unsigned)s_restoreLife, given);
    Notifications::ShowKeyed("zombie_hover", Notifications::Success, "Zombie hover",
                             "Hearts and fairies restored");
    return true;
}

static void heal()
{
    dComIfGp_clearItemDeltas();
    dSv_setLife(kHealQuarters);
}

void Tick(bool acceptInput)
{
    if (acceptInput && Hotkeys::Pressed(Hotkeys::HOTKEY_ZOMBIE_RESET)) {
        ResetStats();
        Notifications::ShowKeyed("zombie_hover", Notifications::Info, "Zombie hover stats reset");
    }

    if (!IsEnabled() || !wwhd_regionResolved) {
        endRun(END_LOST, false);
        s_simulating = false;
        return;
    }

    daPy_lk_c* link = daPy_lk_c_getPlayer();
    const u32* acchFlags = daPy_getAcchFlagsPtr();
    if (!link || !acchFlags) {
        endRun(END_LOST, false);
        s_simulating = false;
        return;
    }

    const u16  life = dSv_getLife();
    const s32  proc = link->mCurProc;
    const bool grounded = (*acchFlags & kAcchGroundHit) != 0;
    const bool swimming = daPy_isSwimming() != 0;
    const bool ship = daPy_isRidingShip() != 0;
    const bool damageMode =
        (*(const u32*)((const u8*)link + kModeFlagOffset) & kModeFlagDamage) != 0;
    const float y = link->base.current.pos.y;

    bool healedNow = false;
    if (Config::g_settings.zombieHoverHeal && life == 0 && !damageMode &&
        (grounded || swimming || ship) &&
        proc != (s32)daPyProc_DEMO_DEAD_e) {
        heal();
        healedNow = true;
        if (!s_hovering)
            Logger::Log("zombie hover: healed with no hover (proc %ld)", (long)proc);
    }

    if (!s_hovering) {
        if (life == 0 && airborneProc(proc) && !grounded && !swimming && !ship)
            startRun(y);
        else
            return;
    }

    Run& r = s_live;
    r.frames++;

    const bool bDown = (::Input::Current().held & ::Input::BTN_B) != 0;
    const bool bEdge = (::Input::Current().pressed & ::Input::BTN_B) != 0;
    if (bEdge)
        r.presses++;

    const bool attacked =
        proc == (s32)daPyProc_JUMP_CUT_e && s_lastProc != (s32)daPyProc_JUMP_CUT_e;
    if (attacked)
        recordAttack(r);
    else if (!airborneProc(proc))
        s_haveReference = false;

    // Fall takes B on any frame, so mashing too fast means B was never released.
    if (airborneProc(proc) && !attacked) {
        if (bDown && !bEdge)
            recordEvent(r, GRADE_HELD);
        else if (bEdge)
            recordEvent(r, GRADE_WASTED);
    }
    s_lastProc = proc;

    r.gainY = y - r.startY;
    if (r.gainY > r.maxGainY)
        r.maxGainY = r.gainY;

    if (life > 0) {
        endRun(END_HEALED, false);
        return;
    }
    if (proc == (s32)daPyProc_DEMO_DEAD_e) {
        endRun(END_DIED, false);
        return;
    }

    EndReason reason = END_NONE;
    if (swimming)      reason = END_WATER;
    else if (ship)     reason = END_SHIP;
    else if (grounded) reason = END_GROUND;
    if (reason == END_NONE)
        return;

    endRun(reason, healedNow);
}

static uint32_t simRandom()
{
    s_simRng = s_simRng * 1664525u + 1013904223u;
    return s_simRng >> 8;
}

// Mixed: 65% perfect, 20% good, 10% ok, 3% bad, 2% held.
static uint32_t simNextGap(bool* holdExtra)
{
    *holdExtra = false;
    if (Config::g_settings.zombieHoverSimPerfect)
        return (uint32_t)GAP_PERFECT;
    const uint32_t roll = simRandom() % 100u;
    if (roll < 65u) return 2u;
    if (roll < 85u) return 3u;
    if (roll < 95u) return 4u;
    if (roll < 98u) return 5u;
    *holdExtra = true;
    return 3u;
}

bool NextButtons(uint32_t held, uint32_t* vpadMask)
{
    if (!vpadMask)
        return false;
    *vpadMask = 0;

    const uint32_t combo = Hotkeys::Get(Hotkeys::HOTKEY_ZOMBIE_SIM);
    const bool comboHeld = combo != 0 && (held & combo) == combo;
    bool run = comboHeld && IsEnabled() && !FlyCam::IsActive() && wwhd_regionResolved;
    s32 proc = -1;
    if (run) {
        daPy_lk_c* link = daPy_lk_c_getPlayer();
        proc = link ? link->mCurProc : (s32)-1;
        run = link && dSv_getLife() == 0 && proc != (s32)daPyProc_DEMO_DEAD_e;
    }

    if (run != s_simulating) {
        s_simulating = run;
        s_simAirborne = false;
        s_simHoldExtra = false;
        s_simCountdown = 0;
        Logger::Log("zombie hover: simulation %s (%s, proc %ld)", run ? "on" : "off",
                    Config::g_settings.zombieHoverSimPerfect ? "perfect only" : "mixed",
                    (long)proc);
    }
    if (!run)
        return false;

    const bool airborne = airborneProc(proc);
    if (airborne != s_simAirborne) {
        s_simAirborne = airborne;
        s_simCountdown = 0;
        Logger::Log("zombie hover: simulation %s (proc %ld)",
                    airborne ? "airborne, B cadence" : "grounded, target + A", (long)proc);
    }

    if (airborne) {
        if (s_simCountdown == 0) {
            *vpadMask = VPAD_BUTTON_B;
            const uint32_t gap = simNextGap(&s_simHoldExtra);
            s_simCountdown = gap - 1u;
        } else {
            if (s_simHoldExtra) {
                *vpadMask = VPAD_BUTTON_B;
                s_simHoldExtra = false;
            }
            --s_simCountdown;
        }
    } else {
        *vpadMask = VPAD_BUTTON_ZL;
        if (s_simCountdown == 0) {
            *vpadMask |= VPAD_BUTTON_A;
            s_simCountdown = 1;
        } else {
            --s_simCountdown;
        }
    }
    return true;
}

void OnApplicationStart()
{
    clearRun(s_live);
    clearRun(s_last);
    ClearHistory();
    s_hovering = false;
    s_lastProc = -1;
    s_haveReference = false;
    s_streak = 0;
    s_simulating = false;
    s_simAirborne = false;
    s_simCountdown = 0;
    s_restoreValid = false;
}
}
}
