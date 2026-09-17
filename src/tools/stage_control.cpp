#include "tools/stage_control.h"

#include "core/hotkeys.h"
#include "core/logger.h"
#include "libwwhd/libwwhd.h"
#include "tools/save_states.h"
#include "ui/notifications.h"

#include <stdio.h>
#include <string.h>

namespace Tools {
namespace StageControl {
static const char kNotifyKey[] = "stage";
static const int  kWatchFrames = 600;
static const int  kWatchEvery  = 30;

static int  s_watchFrames = -1;
static char s_watchStage[WWHD_STAGE_NAME_MAX] = "";
static bool s_watchEnable = false;
static bool s_watchLink = false;
static bool s_watchSawNoLink = false;
static s16  s_watchScene = -1;

static bool fail(const char* message)
{
    Notifications::ShowKeyed(kNotifyKey, Notifications::Error, "Game", message);
    return false;
}

static void logSceneState(const char* tag)
{
    const dStage_nextStage_c* n = dComIfGp_getNextStage();
    const dStage_startStage_c* c = dComIfGp_getCurStage();
    const daPy_lk_c* link = daPy_lk_c_getPlayer();
    Logger::Log("[stage] %s: next=%.7s pt=%d room=%d layer=%d wipe=%d enable=%d | "
                "cur=%.7s pt=%d room=%d layer=%d | scene=%d link=%d proc=%d | "
                "overlap=%08X busy=%d bgm=%u",
                tag,
                n ? n->mName : "?", n ? (int)n->mPoint : -1,
                n ? (int)n->mRoomNo : -1, n ? (int)n->mLayer : -1,
                n ? (int)n->mWipe : -1, n ? (int)n->mEnable : -1,
                c ? c->mName : "?", c ? (int)c->mPoint : -1,
                c ? (int)c->mRoomNo : -1, c ? (int)c->mLayer : -1,
                (int)fopScn_getName(fopScnM_getStageScene()), link != nullptr,
                link ? (int)link->mCurProc : -1,
                (unsigned)fopScnM_getOverlap(), fopScnM_isChangeBusy(),
                (unsigned)dComIfG_getBgmStageTimer());
}

static void startWatch()
{
    const dStage_startStage_c* c = dComIfGp_getCurStage();
    memset(s_watchStage, 0, sizeof(s_watchStage));
    if (c)
        memcpy(s_watchStage, c->mName, sizeof(s_watchStage) - 1);
    s_watchFrames = 0;
    s_watchEnable = dComIfGp_isNextStagePending() != 0;
    s_watchLink = daPy_lk_c_getPlayer() != nullptr;
    s_watchSawNoLink = false;
    s_watchScene = fopScn_getName(fopScnM_getStageScene());
}

static void tickWatch()
{
    if (s_watchFrames < 0)
        return;
    ++s_watchFrames;

    const bool enable = dComIfGp_isNextStagePending() != 0;
    const bool haveLink = daPy_lk_c_getPlayer() != nullptr;
    const s16 scene = fopScn_getName(fopScnM_getStageScene());
    if (!haveLink)
        s_watchSawNoLink = true;

    char tag[48];
    if (enable != s_watchEnable || haveLink != s_watchLink || scene != s_watchScene) {
        snprintf(tag, sizeof(tag), "reload +%d frames (state change)", s_watchFrames);
        logSceneState(tag);
        s_watchEnable = enable;
        s_watchLink = haveLink;
        s_watchScene = scene;
    } else if (s_watchFrames % kWatchEvery == 0) {
        snprintf(tag, sizeof(tag), "reload +%d frames", s_watchFrames);
        logSceneState(tag);
    }

    const bool done = !enable && haveLink && s_watchSawNoLink &&
                      scene == WWHD_SCENE_PLAY &&
                      strncmp(dComIfGp_getCurStageName(), s_watchStage,
                              WWHD_STAGE_NAME_MAX) == 0;
    if (done) {
        Logger::Log("[stage] reload finished after %d frames", s_watchFrames);
        s_watchFrames = -1;
    } else if (s_watchFrames >= kWatchFrames) {
        Logger::LogWarn("[stage] reload still not finished after %d frames", s_watchFrames);
        s_watchFrames = -1;
    }
}

bool ResetGame()
{
    if (SaveStates::IsBusy())
        return fail("Wait for the load to finish.");
    if (!dComIfG_requestReset())
        return fail("Game addresses are unresolved.");
    Notifications::ShowKeyed(kNotifyKey, Notifications::Warning, "Game", "Resetting the game");
    Logger::Log("[stage] reset requested");
    return true;
}

bool ReloadStage()
{
    if (SaveStates::IsBusy())
        return fail("Wait for the load to finish.");
    if (dComIfGp_isNextStagePending())
        return fail("A stage change is already queued.");

    if (fopScn_getName(fopScnM_getStageScene()) != WWHD_SCENE_PLAY)
        return fail("Not in a stage.");

    const dStage_startStage_c* cur = dComIfGp_getCurStage();
    if (!cur || !cur->mName[0])
        return fail("No stage is loaded.");
    if (!dComIfGp_reloadStage()) {
        logSceneState("reload refused");
        return fail("The stage record no longer names this stage.");
    }

    char name[WWHD_STAGE_NAME_MAX];
    memcpy(name, cur->mName, sizeof(name));
    name[sizeof(name) - 1] = '\0';
    char place[48];
    wwhd_placeName(name, cur->mRoomNo, place, sizeof(place));
    Notifications::ShowKeyedTitledf(kNotifyKey, Notifications::Info, "Game", "Reloading %s", place);
    Logger::Log("[stage] reload: stage=%s room=%d point=%d layer=%d",
                name, (int)cur->mRoomNo, (int)cur->mPoint, (int)cur->mLayer);
    logSceneState("reload requested");
    startWatch();
    return true;
}

void Tick(bool acceptInput)
{
    tickWatch();
    if (!acceptInput)
        return;
    if (Hotkeys::Pressed(Hotkeys::HOTKEY_GAME_RESET))
        ResetGame();
    else if (Hotkeys::Pressed(Hotkeys::HOTKEY_STAGE_RELOAD))
        ReloadStage();
}
}
}
