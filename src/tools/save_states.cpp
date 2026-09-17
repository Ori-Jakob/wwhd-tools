#include "tools/save_states.h"

#include "core/hotkeys.h"
#include "core/logger.h"
#include "libwupatch/wupatch.h"
#include "libwwhd/libwwhd.h"
#include "ui/notifications.h"

#include <coreinit/cache.h>
#include <coreinit/messagequeue.h>
#include <coreinit/thread.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace Tools {
namespace SaveStates {
static const u32 kMagic   = 0x57575353u;
static const u32 kVersion = 2u;

struct Header {
    u32  magic;
    u32  version;
    char stage[WWHD_STAGE_NAME_MAX];
    s8   room;
    s8   layer;
    s16  point;
    u8   saveTable;
    u8   riding;
    u8   _pad[2];
    cXyz pos;
    s16  angle;
    u8   _pad2[2];
    cXyz shipPos;
    s16  shipAngle;
    u8   _pad3[2];
    s8   zoneNo[WWHD_ROOM_MAX];
    u32  infoSize;
};

static const u32 kImageSize = (u32)(sizeof(Header) + sizeof(dSv_info_c));

enum Phase { PHASE_IDLE = 0, PHASE_READ, PHASE_ARM, PHASE_WARP, PHASE_SETTLE };
enum Job   { JOB_NONE = 0, JOB_WRITE, JOB_READ_STATE, JOB_READ_FOREIGN };

static const int   kArmFrames      = 120;
static const int   kWarpFrames     = 900;
static const int   kSettleFrames   = 900;
static const int   kSameLinkFrames = 120;
static const int   kBgmFadeFrames  = 30;
static const int   kLoadLogEvery   = 30;
static const float kPlaceTolerance = 200.0f;

static u8         s_image[sizeof(Header) + sizeof(dSv_info_c)];
static dSv_info_c s_pendingInfo;
static Header     s_loading;
static bool       s_verifyPlace = false;
static bool       s_fromTitle = false;
static bool       s_banksRequested = false;
static bool       s_titleWaiting = false;
static char       s_label[Storage::SAVE_PATH_MAX] = "";
static Phase      s_phase = PHASE_IDLE;
static int        s_frames = 0;
static bool       s_sawNoLink = false;
static const void* s_oldLink = nullptr;
static u32        s_realPhase1 = 0;

static WuPatch::Handle s_hookSwap = WuPatch::kInvalidHandle;

static volatile u32 s_installWanted = 0;
static volatile u32 s_installed = 0;

static OSThread       s_thread;
static bool           s_threadStarted = false;
static OSMessageQueue s_queue;
static OSMessage      s_msgBuf[4];
static __attribute__((aligned(16))) u8 s_stack[16 * 1024];
static volatile u32   s_jobBusy = 0;
static volatile int   s_jobOk = 0;
static volatile u32   s_jobSize = 0;
static Job            s_job = JOB_NONE;
static ReadFn         s_jobFn = nullptr;
static void*          s_jobBuf = nullptr;
static u32            s_jobCap = 0;
static char           s_jobName[Storage::SAVE_PATH_MAX] = "";
static bool           s_foreignDone = false;
static bool           s_foreignOk = false;
static u32            s_foreignSize = 0;

static char s_status[96] = "";
static char s_selected[kNameMax] = "";
static Storage::StateEntry s_list[kListMax];
static int  s_count = 0;
static bool s_listValid = false;

static const char kNotifyKey[] = "savestate";

static void setStatus(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vsnprintf(s_status, sizeof(s_status), fmt, args);
    va_end(args);
}

static bool fail(const char* message)
{
    setStatus("%s", message);
    Notifications::ShowKeyed(kNotifyKey, Notifications::Error, "Save States", message);
    return false;
}

static bool sanitizeName(const char* in, char* out, int cap)
{
    int n = 0;
    for (const char* p = in; p && *p && n < cap - 1; ++p) {
        const char c = *p;
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                        (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (ok)
            out[n++] = c;
    }
    out[n] = '\0';
    return n > 0;
}

static bool nameExists(const char* name)
{
    for (int i = 0; i < s_count; ++i)
        if (strcmp(s_list[i].name, name) == 0)
            return true;
    return false;
}

static bool generateName(char* out, int cap)
{
    RefreshList();
    for (int n = 1; n < 1000; ++n) {
        snprintf(out, (size_t)cap, "state_%03d", n);
        if (!nameExists(out))
            return true;
    }
    return false;
}

static int worker(int argc, const char** argv)
{
    (void)argc; (void)argv;
    for (;;) {
        OSMessage msg;
        OSReceiveMessage(&s_queue, &msg, OS_MESSAGE_FLAGS_BLOCKING);
        OSMemoryBarrier();
        int ok = 0;
        u32 size = 0;
        const Job job = (Job)msg.args[0];
        if (job == JOB_WRITE) {
            ok = Storage::WriteStateFile(s_jobName, s_image, kImageSize) ? 1 : 0;
            size = ok ? kImageSize : 0;
        } else if (job == JOB_READ_STATE || job == JOB_READ_FOREIGN) {
            ok = s_jobFn && s_jobFn(s_jobName, s_jobBuf, s_jobCap, &size) == Storage::READ_OK;
        }
        s_jobOk = ok;
        s_jobSize = size;
        OSMemoryBarrier();
        s_jobBusy = 0;
    }
    return 0;
}

static bool startWorker()
{
    if (s_threadStarted)
        return true;
    OSInitMessageQueue(&s_queue, s_msgBuf, (int32_t)(sizeof(s_msgBuf) / sizeof(s_msgBuf[0])));
    void* stackTop = s_stack + sizeof(s_stack);
    if (!OSCreateThread(&s_thread, worker, 0, nullptr, stackTop, sizeof(s_stack), 16,
                        OS_THREAD_ATTRIB_AFFINITY_ANY)) {
        Logger::LogError("[savestate] worker thread create failed");
        return false;
    }
    OSSetThreadName(&s_thread, "wwhd_tools_state");
    OSResumeThread(&s_thread);
    s_threadStarted = true;
    return true;
}

static bool postJob(Job job, const char* name, ReadFn fn, void* buf, u32 cap)
{
    if (s_job != JOB_NONE || !startWorker())
        return false;
    strncpy(s_jobName, name, sizeof(s_jobName) - 1);
    s_jobName[sizeof(s_jobName) - 1] = '\0';
    s_jobFn  = fn;
    s_jobBuf = buf;
    s_jobCap = cap;
    s_job    = job;
    s_jobOk  = 0;
    s_jobSize = 0;
    s_jobBusy = 1;
    OSMemoryBarrier();
    OSMessage msg;
    msg.message = nullptr;
    msg.args[0] = (uint32_t)job;
    msg.args[1] = msg.args[2] = 0;
    if (!OSSendMessage(&s_queue, &msg, OS_MESSAGE_FLAGS_NONE)) {
        s_jobBusy = 0;
        s_job = JOB_NONE;
        return false;
    }
    return true;
}

bool BeginRead(ReadFn fn, const char* name, void* out, uint32_t capacity)
{
    if (!fn || !name || !name[0] || !out || !capacity || IsBusy())
        return false;
    s_foreignDone = false;
    return postJob(JOB_READ_FOREIGN, name, fn, out, capacity);
}

bool ReadFinished(bool* ok, uint32_t* size)
{
    if (!s_foreignDone)
        return false;
    s_foreignDone = false;
    if (ok)
        *ok = s_foreignOk;
    if (size)
        *size = s_foreignSize;
    return true;
}

static int scenePhase1Hook(void* scene)
{
    if (s_installWanted) {
        OSMemoryBarrier();
        dSv_info_c* live = dComIfGs_getSaveInfo();
        if (live)
            memcpy(live, &s_pendingInfo, sizeof(dSv_info_c));
        dStage_roomStatus_c* rooms = dStage_getRoomStatusTable();
        if (rooms) {
            for (int i = 0; i < WWHD_ROOM_MAX; ++i)
                rooms[i].mZoneNo = s_loading.zoneNo[i];
        }
        s_installWanted = 0;
        OSMemoryBarrier();
        s_installed = 1;
    }

    dScnPly_phase_t real = (dScnPly_phase_t)(uintptr_t)s_realPhase1;
    return real ? real(scene) : 0;
}

static WuPatch::Handle hookSwap()
{
    const wwhd_addr_t slot = dScnPly_getPhase1SlotAddr();
    if (s_hookSwap == WuPatch::kInvalidHandle && slot) {
        WuPatch::Data::SwapDesc d = {};
        d.owner      = "Save States";
        d.linkAddr   = slot;
        d.stride     = 4;
        d.count      = 1;
        d.wordOffset = 0;
        d.value      = (uint32_t)(uintptr_t)&scenePhase1Hook;
        s_hookSwap   = WuPatch::Data::Declare(d);
    }
    return s_hookSwap;
}

static void releaseHook()
{
    if (s_hookSwap != WuPatch::kInvalidHandle)
        WuPatch::Data::SetEnabled(s_hookSwap, false);
    s_installWanted = 0;
    s_phase = PHASE_IDLE;
}

static void logLoadState(const char* tag)
{
    const daPy_lk_c* link = daPy_lk_c_getPlayer();
    Logger::Log("[savestate] %s: phase=%d frames=%d installed=%d | scene=%d cur=%.7s "
                "next=%.7s enable=%d | link=%d proc=%d demo=%u | overlap=%08X busy=%d "
                "bgm=%u bgmBusy=%d | event=%u camPlay=%u | audio stage cur=%d req=%d "
                "track playing=%08X pending=%08X request=%d restart=%d "
                "banks=%u/%u changed=%d/%d",
                tag, (int)s_phase, s_frames, (int)s_installed,
                (int)fopScn_getName(fopScnM_getStageScene()), dComIfGp_getCurStageName(),
                dComIfGp_getNextStageName(), dComIfGp_isNextStagePending(),
                link != nullptr, link ? (int)link->mCurProc : -1,
                (unsigned)daPy_getDemoMode(), (unsigned)fopScnM_getOverlap(),
                fopScnM_isChangeBusy(), (unsigned)dComIfG_getBgmStageTimer(),
                dComIfG_isStageBgmBusy(), (unsigned)dEvt_getEventMode(),
                (unsigned)dEvent_getCameraPlay(),
                (int)mDoAud_getCurrentStage(), (int)mDoAud_getRequestedStage(),
                (unsigned)mDoAud_getPlayingBgm(), (unsigned)mDoAud_getPendingBgm(),
                mDoAud_isRequestPending(), mDoAud_isRestartWanted(),
                (unsigned)mDoAud_getBankSet(), (unsigned)mDoAud_getBankSet2(),
                mDoAud_bankSetChanged(), mDoAud_bankSet2Changed());
}

static void abortLoad(const char* why)
{
    logLoadState("abort");

    if (s_phase == PHASE_WARP && !s_installed)
        dComIfGp_cancelNextStage();
    releaseHook();
    Logger::LogError("[savestate] load of %s aborted: %s", s_label, why);
    fail(why);
}

bool Save(const char* name)
{
    if (IsBusy())
        return fail("Still busy with the last state.");

    dSv_info_c*   live  = dComIfGs_getSaveInfo();
    daPy_lk_c*    link  = daPy_lk_c_getPlayer();
    const char*   stage = dComIfGp_getCurStageName();
    const cXyz*   pos   = daPy_getStorePos();
    const csXyz*  shape = daPy_getStoreShapeAngle();
    if (!live || !link || !stage[0] || !pos || !shape)
        return fail("Nothing to save: no game in progress.");
    if (dComIfGp_isNextStagePending())
        return fail("Wait for the stage change to finish.");
    const int table = dComIfGp_getStageSaveTblNo();
    if (table < 0)
        return fail("This stage has no save table.");
    const s8 room = daPy_getRoomNo();
    if (room < 0)
        return fail("Link is not in a room.");

    char stem[kNameMax];
    if (name && name[0]) {
        if (!sanitizeName(name, stem, sizeof(stem)))
            return fail("Names use letters, digits, - and _.");
    } else if (!generateName(stem, sizeof(stem))) {
        return fail("No free state name.");
    }

    const daShip_c* ship = get_daShip();
    const bool riding = ship && dComIfGp_isPlayerShipRide();

    Header* h = (Header*)s_image;
    memset(h, 0, sizeof(*h));
    h->magic     = kMagic;
    h->version   = kVersion;
    snprintf(h->stage, sizeof(h->stage), "%s", stage);
    h->room      = room;
    h->layer     = dComIfGp_getCurStageLayer();
    h->point     = riding ? WWHD_STAGE_POINT_TURN_RESTART : WWHD_STAGE_POINT_RESTART;
    h->saveTable = (u8)table;
    h->riding    = riding ? 1 : 0;
    h->pos       = *pos;
    h->angle     = shape->y;
    if (riding) {
        h->shipPos   = ship->base.current.pos;
        h->shipAngle = ship->base.shape_angle.y;
    }
    for (int i = 0; i < WWHD_ROOM_MAX; ++i) {
        const dStage_roomStatus_c* r = dStage_getRoomStatus(i);
        h->zoneNo[i] = r ? r->mZoneNo : (s8)-1;
    }
    h->infoSize = (u32)sizeof(dSv_info_c);

    dSv_info_c* info = (dSv_info_c*)(s_image + sizeof(Header));
    memcpy(info, live, sizeof(dSv_info_c));

    info->mSavedata.mMemory[table] = info->mMemory;

    dSv_restart_c* r = &info->mRestart;
    r->mRestartRoom  = room;
    r->mRestartPos   = *pos;
    r->mRestartAngle = shape->y;
    r->mRestartParam = daPy_packStartParam(room, WWHD_PLAYER_START_MODE_NORMAL,
                                           WWHD_PLAYER_EVENT_NONE);
    r->mStartCode    = h->point;
    r->mLastSpeedF   = 0.0f;
    r->mLastMode     = 0u;
    if (riding) {
        dSv_turnRestart_c* t = &info->mTurnRestart;
        t->mPosition   = *pos;
        t->mParam      = daPy_packStartParam(room, WWHD_PLAYER_START_MODE_SHIP,
                                             WWHD_PLAYER_EVENT_NONE) |
                         WWHD_PLAYER_PARAM_HAS_SHIP | WWHD_PLAYER_PARAM_KEEP_RESTART;
        t->mAngleY     = shape->y;
        t->mRoomNo     = room;
        t->_pad_13     = 0;
        t->mShipPos    = h->shipPos;
        t->mShipAngleY = h->shipAngle;
        t->mHasShip    = 1u;
    }

    if (!postJob(JOB_WRITE, stem, nullptr, nullptr, 0))
        return fail("Could not start writing the state.");

    SetSelected(stem);
    setStatus("Saving %s ...", stem);
    Logger::Log("[savestate] saving %s: stage=%s room=%d layer=%d table=%d "
                "pos=(%.1f,%.1f,%.1f) angle=%d%s",
                stem, h->stage, (int)room, (int)h->layer, table,
                pos->x, pos->y, pos->z, (int)shape->y, riding ? " aboard" : "");
    return true;
}

static void finishWrite()
{
    s_listValid = false;
    if (!s_jobOk) {
        fail("Could not write the state. Is the SD card mounted?");
        return;
    }
    setStatus("Saved %s", s_jobName);
    Notifications::ShowKeyedTitledf(kNotifyKey, Notifications::Success, "Save States",
                                    "Saved %s", s_jobName);
}

static bool beginBlockLoad(const dSv_info_c* block, const char* stage, s16 point,
                           s8 room, s8 layer, const s8* zoneNo, const char* label)
{
    if (!block || !stage || !stage[0] || !label)
        return fail("Nothing to load.");
    if (!wwhd_regionResolved || !wwhd_textResolved)
        return fail("Game addresses are unresolved.");
    if (!dComIfGs_getSaveInfo())
        return fail("Start or load a game first.");
    if (dComIfGp_isNextStagePending())
        return fail("Wait for the stage change to finish.");

    const s16 sceneName = fopScn_getName(fopScnM_getStageScene());
    if (sceneName != WWHD_SCENE_PLAY && sceneName != WWHD_SCENE_OPENING &&
        sceneName != WWHD_SCENE_TITLE)
        return fail("Go to the title screen or into the game first.");
    s_fromTitle = sceneName != WWHD_SCENE_PLAY;
    if (s_fromTitle && !daPy_lk_c_getPlayer())
        return fail("Wait for the title screen to finish loading.");

    wwhd_gptr_t* table = dScnPly_getPhaseTable();
    const WuPatch::Handle swap = hookSwap();
    if (!table || swap == WuPatch::kInvalidHandle)
        return fail("The scene phase table is unavailable.");
    const u32 current = table[WWHD_DSCNPLY_PHASE1_SLOT];
    if (current == (u32)(uintptr_t)&scenePhase1Hook)
        return fail("The scene hook is still installed from the last load.");
    if (current != WWHD_TEXT(wwhd_map->dScnPly_phase1))
        Logger::LogWarn("[savestate] phase_1 slot holds %08X, expected %08X; chaining to it",
                        (unsigned)current, (unsigned)WWHD_TEXT(wwhd_map->dScnPly_phase1));

    if (block != &s_pendingInfo)
        memcpy(&s_pendingInfo, block, sizeof(dSv_info_c));
    s_pendingInfo.mRestart.mStartCode = point;
    snprintf(s_loading.stage, sizeof(s_loading.stage), "%s", stage);
    s_loading.point = point;
    s_loading.room  = room;
    s_loading.layer = layer;
    for (int i = 0; i < WWHD_ROOM_MAX; ++i)
        s_loading.zoneNo[i] = zoneNo ? zoneNo[i] : (s8)-1;
    strncpy(s_label, label, sizeof(s_label) - 1);
    s_label[sizeof(s_label) - 1] = '\0';

    s_realPhase1    = current;
    s_installed     = 0;
    s_banksRequested = false;
    s_titleWaiting = false;
    s_installWanted = 0;
    s_sawNoLink     = false;
    s_oldLink       = daPy_lk_c_getPlayer();
    s_frames        = 0;
    s_phase         = PHASE_ARM;
    WuPatch::Data::SetEnabled(swap, true);

    Notifications::ShowStickyf(kNotifyKey, Notifications::Info, "Save States",
                               "Loading %s ...", s_label);
    Logger::Log("[savestate] loading %s: stage=%s room=%d layer=%d point=%d%s",
                s_label, s_loading.stage, (int)room, (int)layer, (int)point,
                s_fromTitle ? " from the title" : "");
    logLoadState("load begin");
    return true;
}

bool LoadBlock(const dSv_info_c* block, const char* stage, int16_t point,
               int8_t room, int8_t layer, const int8_t* zoneNo, const char* label)
{
    if (IsBusy())
        return fail("Still busy with the last state.");
    s_verifyPlace = false;
    s_loading.riding = 0;
    return beginBlockLoad(block, stage, point, room, layer, zoneNo, label);
}

bool Load(const char* name)
{
    if (IsBusy())
        return fail("Still busy with the last state.");
    if (!name || !name[0])
        return fail("No state selected.");
    if (!wwhd_regionResolved || !wwhd_textResolved)
        return fail("Game addresses are unresolved.");
    if (!dComIfGs_getSaveInfo())
        return fail("Start or load a game first.");
    if (dComIfGp_isNextStagePending())
        return fail("Wait for the stage change to finish.");
    if (!postJob(JOB_READ_STATE, name, Storage::ReadStateFile, s_image, kImageSize))
        return fail("Could not start reading the state.");

    SetSelected(name);
    s_phase = PHASE_READ;
    setStatus("Loading %s ...", name);
    return true;
}

static void finishRead()
{
    s_phase = PHASE_IDLE;
    if (!s_jobOk || s_jobSize != kImageSize) {
        fail("Could not read the state file.");
        return;
    }
    const Header* h = (const Header*)s_image;
    if (h->magic != kMagic || h->version != kVersion ||
        h->infoSize != sizeof(dSv_info_c) || !h->stage[0]) {
        fail("Not a state file this build understands.");
        return;
    }

    s_loading = *h;
    s_verifyPlace = h->riding == 0;
    memcpy(&s_pendingInfo, s_image + sizeof(Header), sizeof(dSv_info_c));
    beginBlockLoad(&s_pendingInfo, h->stage, h->point, h->room, h->layer,
                   h->zoneNo, s_selected);
}

static void finishLoad()
{
    const cXyz* at = daPy_getStorePos();
    float dx = 0.0f, dy = 0.0f, dz = 0.0f;
    if (at && s_verifyPlace) {
        dx = at->x - s_loading.pos.x;
        dy = at->y - s_loading.pos.y;
        dz = at->z - s_loading.pos.z;
    }
    const float off2 = dx * dx + dy * dy + dz * dz;

    if (s_verifyPlace && off2 > kPlaceTolerance * kPlaceTolerance) {
        daPy_setPosition(&s_loading.pos);
        daPy_setFacing(s_loading.angle);
        Logger::LogWarn("[savestate] respawn missed by (%.1f,%.1f,%.1f); placed directly",
                        dx, dy, dz);
    }
    releaseHook();
    setStatus("Loaded %s", s_label);
    Notifications::ShowKeyedTitledf(kNotifyKey, Notifications::Success, "Save States",
                                    "Loaded %s", s_label);
    Logger::Log("[savestate] loaded %s after %d frames, off by %.1f", s_label,
                s_frames, off2 > 0.0f ? sqrtf(off2) : 0.0f);
    logLoadState("after load");
}

static void tickLoad()
{
    switch (s_phase) {
    case PHASE_ARM: {
        const WuPatch::State st = WuPatch::Data::GetState(s_hookSwap);
        if (st == WuPatch::STATE_APPLIED) {
            if (s_fromTitle) {
                if (!s_banksRequested) {
                    s_banksRequested = true;
                    dComIfG_loadCommonBgmBanks();
                    Logger::Log("[savestate] title start: common sound banks requested");
                }
                if (fopScn_getName(fopScnM_getStageScene()) != WWHD_SCENE_OPENING) {
                    abortLoad("The title screen went away before the game could start.");
                    return;
                }
                const bool ready = dComIfG_commonBgmBanksReady() != 0 &&
                                   daTitle_isWaitingForStart() != 0 &&
                                   !fopScnM_isChangeBusy() && !fopScnM_getOverlap();
                if (!ready) {
                    if (!s_titleWaiting) {
                        s_titleWaiting = true;
                        setStatus("Waiting for the title screen to settle ...");
                        Notifications::ShowStickyf(kNotifyKey, Notifications::Info,
                                                   "Save States",
                                                   "Waiting for the title screen to settle ...");
                        Logger::Log("[savestate] title start: waiting for the title screen");
                    }
                    ++s_frames;
                    return;
                }
                if (s_titleWaiting) {
                    s_titleWaiting = false;
                    setStatus("Loading %s ...", s_label);
                    Notifications::ShowStickyf(kNotifyKey, Notifications::Info,
                                               "Save States", "Loading %s ...", s_label);
                }
                Logger::Log("[savestate] title start: PRESS START up and sound banks "
                            "ready after %d frames", s_frames);
            }
            s_installWanted = 1;
            OSMemoryBarrier();
            if (!dComIfGp_setNextStage(s_loading.stage, s_loading.point,
                                       s_loading.room, s_loading.layer, 0)) {
                s_installWanted = 0;
                abortLoad("The game refused the stage change.");
                return;
            }
            if (s_fromTitle && !fopScnM_changeToPlay(fopScnM_getStageScene(), 0)) {
                s_installWanted = 0;
                dComIfGp_cancelNextStage();
                abortLoad("The title screen would not start the game.");
                return;
            }
            if (s_fromTitle) {
                dComIfG_stopBgm(kBgmFadeFrames);
                const bool primed = dComIfG_prepareStageBgm() != 0;
                Logger::Log("[savestate] title start queued, stage bgm %s",
                            primed ? "armed" : "not armed");
                logLoadState("title start");
            }
            s_phase = PHASE_WARP;
            s_frames = 0;
        } else if (++s_frames > kArmFrames) {
            Logger::LogError("[savestate] scene hook not applied: %s", WuPatch::StateName(st));
            abortLoad("The scene hook could not be installed.");
        }
        break;
    }
    case PHASE_WARP:
        if (!daPy_lk_c_getPlayer())
            s_sawNoLink = true;
        if (s_frames % kLoadLogEvery == 0)
            logLoadState("warp");
        if (s_installed) {
            s_phase = PHASE_SETTLE;
            s_frames = 0;
        } else if (++s_frames > kWarpFrames) {
            abortLoad("The stage never reloaded.");
        }
        break;
    case PHASE_SETTLE: {
        const daPy_lk_c* link = daPy_lk_c_getPlayer();
        if (!link)
            s_sawNoLink = true;
        ++s_frames;
        if (s_frames % kLoadLogEvery == 0)
            logLoadState("settle");
        const bool inStage = link && !dComIfGp_isNextStagePending() &&
                             strncmp(dComIfGp_getCurStageName(), s_loading.stage,
                                     WWHD_STAGE_NAME_MAX) == 0;

        const bool fresh = s_sawNoLink || (const void*)link != s_oldLink ||
                           s_frames > kSameLinkFrames;
        if (inStage && fresh)
            finishLoad();
        else if (s_frames > kSettleFrames)
            abortLoad("Link did not reappear in the loaded stage.");
        break;
    }
    case PHASE_READ:
    case PHASE_IDLE:
    default:
        break;
    }
}

bool Delete(const char* name)
{
    if (!name || !name[0])
        return false;
    if (IsBusy())
        return fail("Still busy with the last state.");
    if (!Storage::DeleteStateFile(name))
        return fail("Could not delete the state.");
    if (strcmp(s_selected, name) == 0)
        s_selected[0] = '\0';
    s_listValid = false;
    setStatus("Deleted %s", name);
    Logger::Log("[savestate] deleted %s", name);
    return true;
}

bool        IsBusy()  { return s_phase != PHASE_IDLE || s_job != JOB_NONE; }
const char* Status()  { return s_status; }

void RefreshList()
{
    s_count = Storage::ListStateFiles(s_list, kListMax);
    s_listValid = true;
}

int Count()
{
    if (!s_listValid)
        RefreshList();
    return s_count;
}

const char* NameAt(int index)
{
    return (index >= 0 && index < Count()) ? s_list[index].name : "";
}

const char* Selected() { return s_selected; }

void SetSelected(const char* name)
{
    if (!name)
        name = "";
    strncpy(s_selected, name, sizeof(s_selected) - 1);
    s_selected[sizeof(s_selected) - 1] = '\0';
}

void Tick(bool acceptInput)
{
    if (s_job != JOB_NONE && !s_jobBusy) {
        OSMemoryBarrier();
        const Job done = s_job;
        s_job = JOB_NONE;
        if (done == JOB_WRITE) {
            finishWrite();
        } else if (done == JOB_READ_STATE) {
            finishRead();
        } else {
            s_foreignOk   = s_jobOk != 0;
            s_foreignSize = s_jobSize;
            s_foreignDone = true;
        }
    }
    if (acceptInput && !IsBusy() && !kHidden) {
        if (Hotkeys::Pressed(Hotkeys::HOTKEY_SAVE_STATE))
            Save(nullptr);
        else if (Hotkeys::Pressed(Hotkeys::HOTKEY_LOAD_STATE))
            Load(s_selected);
    }
    if (s_phase != PHASE_IDLE)
        tickLoad();
}

void OnApplicationStart()
{
    s_job = JOB_NONE;
    s_jobBusy = 0;
    s_foreignDone = false;
    s_phase = PHASE_IDLE;
    s_installWanted = 0;
    s_installed = 0;
    s_realPhase1 = 0;
    s_hookSwap = WuPatch::kInvalidHandle;
    s_listValid = false;
    s_count = 0;
    s_status[0] = '\0';
}

void OnApplicationEnd()
{
    s_installWanted = 0;
    s_phase = PHASE_IDLE;
}
}
}
