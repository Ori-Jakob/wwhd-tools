#include "tools/coordinates.h"

#include "core/config.h"
#include "core/hotkeys.h"
#include "core/logger.h"
#include "tools/save_states.h"
#include "tools/great_sea_map.h"
#include "ui/notifications.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace Tools {
namespace Coordinates {
static const char kNotifyKey[] = "coordinates";

static const int   kRoomFrames     = 600;
static const int   kBgFrames       = 180;
static const int   kWarpFrames     = 900;
static const int   kSettleFrames   = 900;
static const int   kSameLinkFrames = 120;
static const int   kLogEvery       = 30;
static const float kPlaceTolerance = 200.0f;

enum Phase { PHASE_IDLE = 0, PHASE_ROOM, PHASE_WARP, PHASE_SETTLE };

struct Target {
    char  stage[WWHD_STAGE_NAME_MAX];
    s8    room;
    cXyz  pos;
    bool  setFacing;
    s16   angle;
    bool  hasCamera;
    cXyz  camEye;
    cXyz  camCenter;
    char  label[40];
};

static Slot   s_slots[SLOT_COUNT];
static int    s_selected = 0;
static Target s_t;
static Phase  s_phase = PHASE_IDLE;
static int    s_frames = 0;
static int    s_sceneUpFrame = -1;
static bool   s_sawNoLink = false;
static const void* s_oldLink = nullptr;
static char   s_status[96] = "";

static dCamera_c* s_cam = nullptr;

static bool validIndex(int i) { return i >= 0 && i < SLOT_COUNT; }

static void setStatus(const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(s_status, sizeof(s_status), fmt, ap);
    va_end(ap);
}

static bool fail(const char* message)
{
    Notifications::ShowKeyed(kNotifyKey, Notifications::Error, "Coordinates", message);
    setStatus("%s", message);
    return false;
}

int  Selected()            { return s_selected; }
void SetSelected(int i)    { if (validIndex(i)) s_selected = i; }
bool IsBusy()              { return s_phase != PHASE_IDLE; }
const char* Status()       { return s_status; }

const Slot& At(int i)
{
    static const Slot kEmpty = {};
    return validIndex(i) ? s_slots[i] : kEmpty;
}

void Set(int i, const Slot& slot)
{
    if (!validIndex(i))
        return;
    s_slots[i] = slot;
    s_slots[i].name[NAME_MAX - 1] = '\0';
    s_slots[i].stage[WWHD_STAGE_NAME_MAX - 1] = '\0';
}

void ClearSlot(int i)
{
    if (!validIndex(i))
        return;
    memset(&s_slots[i], 0, sizeof(Slot));
    Config::MarkDirty();
}

void Describe(int i, char* out, int cap)
{
    if (!out || cap <= 0)
        return;
    const Slot& s = At(i);
    if (!s.valid) {
        snprintf(out, (size_t)cap, "empty");
        return;
    }
    char place[48];
    wwhd_placeName(s.stage, s.room, place, sizeof(place));
    snprintf(out, (size_t)cap, "%s (%s) room %d", place, s.stage, (int)s.room);
}

void OnCameraRun(void* camera)
{
    dCamera_c* cam = (dCamera_c*)camera;
    if (!cam)
        return;
    if (cam->mPlayerIdx == 0 || !s_cam)
        s_cam = cam;
}

static void applyCamera(const cXyz& eye, const cXyz& center)
{
    if (!s_cam)
        return;
    s_cam->mWorkEye = eye;
    s_cam->mWorkCenter = center;
    s_cam->mEye = eye;
    s_cam->mCenter = center;
    dCam_restartMode(s_cam);
}

void SetCamera(const cXyz& eye, const cXyz& center)
{
    applyCamera(eye, center);
}

static void place(const Target& t, bool withCamera)
{
    daPy_setPosition(&t.pos);
    if (t.setFacing)
        daPy_setFacing(t.angle);
    if (withCamera && t.hasCamera)
        applyCamera(t.camEye, t.camCenter);
}

static void finish(const char* how)
{
    s_phase = PHASE_IDLE;
    setStatus("%s %s", how, s_t.label);
    Notifications::ShowKeyedTitledf(kNotifyKey, Notifications::Success, "Coordinates", "%s %s",
                                    how, s_t.label);
    Logger::Log("[coords] %s %s after %d frames: stage=%s room=%d pos=(%.1f,%.1f,%.1f)",
                how, s_t.label, s_frames, s_t.stage, (int)s_t.room, (double)s_t.pos.x,
                (double)s_t.pos.y, (double)s_t.pos.z);
}

static void abortMove(const char* why)
{
    if (s_phase == PHASE_WARP)
        dComIfGp_cancelNextStage();
    s_phase = PHASE_IDLE;
    Logger::LogError("[coords] %s aborted: %s", s_t.label, why);
    fail(why);
}

static bool roomReady(s8 room)
{
    const u8 flags = dStage_getRoomFlags(room);
    if (!(flags & WWHD_ROOM_FLAG_LOADED) || (flags & WWHD_ROOM_FLAG_NO_BG))
        return false;
    return daPy_getRoomNo() == room;
}

static void unhideEntryRooms(const roomRead_data_class* e)
{
    const u8* rooms = WWHD_AT(const u8, e->mRooms);
    for (int i = 0; i < (int)e->num; ++i) {
        if (!(rooms[i] & WWHD_ROOMREAD_BG_BIT))
            continue;
        const int r = rooms[i] & WWHD_ROOMREAD_ROOM_MASK;
        dStage_roomStatus_c* st = dStage_getRoomStatus(r);
        if (!st || !(st->mFlags & WWHD_ROOM_FLAG_LOADED) || !(st->mFlags & WWHD_ROOM_FLAG_NO_BG))
            continue;
        st->mFlags &= (u8)~WWHD_ROOM_FLAG_NO_BG;
        Logger::Log("[coords] room %d was resident without its collision; unhidden", r);
    }
}

static void logState(const char* tag)
{
    const daPy_lk_c* link = daPy_lk_c_getPlayer();
    Logger::Log("[coords] %s: phase=%d frames=%d | scene=%d cur=%.7s next=%.7s enable=%d | "
                "link=%d room=%d stay=%d flags[%d]=%02X busy=%d",
                tag, (int)s_phase, s_frames, (int)fopScn_getName(fopScnM_getStageScene()),
                dComIfGp_getCurStageName(), dComIfGp_getNextStageName(),
                dComIfGp_isNextStagePending(), link != nullptr, (int)daPy_getRoomNo(),
                (int)dStage_getStayNo(), (int)s_t.room, (unsigned)dStage_getRoomFlags(s_t.room),
                dStage_isRoomStreamingBusy());
}

static void arrive(const char* how)
{
    place(s_t, true);
    Notifications::Dismiss(kNotifyKey);
    finish(how);
}

static bool startStageWarp()
{
    dSv_restart_c* r = dComIfGs_getRestart();
    if (!r)
        return fail("The save block is unavailable.");
    r->mRestartRoom  = s_t.room;
    r->mRestartPos   = s_t.pos;
    r->mRestartAngle = s_t.angle;
    r->mRestartParam = daPy_packStartParam(s_t.room, WWHD_PLAYER_START_MODE_NORMAL,
                                           WWHD_PLAYER_EVENT_NONE);
    r->mStartCode    = WWHD_STAGE_POINT_RESTART;
    r->mLastSpeedF   = 0.0f;
    r->mLastMode     = 0u;
    if (!dComIfGp_setNextStage(s_t.stage, WWHD_STAGE_POINT_RESTART, s_t.room,
                               WWHD_STAGE_LAYER_KEEP, 0))
        return fail("The game refused the stage change.");

    s_phase = PHASE_WARP;
    s_frames = 0;
    s_sawNoLink = false;
    s_oldLink = daPy_lk_c_getPlayer();
    Notifications::ShowStickyf(kNotifyKey, Notifications::Info, "Coordinates", "Loading %s ...",
                               s_t.label);
    setStatus("Loading %s ...", s_t.label);
    Logger::Log("[coords] stage change to %s room %d for %s", s_t.stage, (int)s_t.room, s_t.label);
    logState("warp requested");
    return true;
}

static bool startRoomLoad()
{
    roomRead_data_class* entry = dStage_getRoomReadEntry(s_t.room);
    if (!entry || !dStage_roomReadEntryHas(entry, s_t.room)) {
        Logger::LogWarn("[coords] room %d has no usable RTBL entry; changing stage instead",
                        (int)s_t.room);
        return startStageWarp();
    }
    if (dStage_isRoomStreamingBusy()) {
        Logger::Log("[coords] room streaming already busy; waiting for it");
    }
    s_phase = PHASE_ROOM;
    s_frames = 0;
    s_sceneUpFrame = -1;
    place(s_t, false);
    unhideEntryRooms(entry);
    dStage_loadRoomFor(s_t.room);
    Notifications::ShowStickyf(kNotifyKey, Notifications::Info, "Coordinates",
                               "Loading room %d ...", (int)s_t.room);
    setStatus("Loading room %d for %s ...", (int)s_t.room, s_t.label);
    Logger::Log("[coords] streaming room %d (entry lists %d rooms) for %s", (int)s_t.room,
                (int)entry->num, s_t.label);
    logState("room load begin");
    return true;
}

static bool begin(const Target& t)
{
    if (!wwhd_regionResolved || !wwhd_textResolved)
        return fail("Game addresses are unresolved.");
    if (IsBusy())
        return fail("Still moving to the last place.");
    if (SaveStates::IsBusy())
        return fail("Wait for the save state to finish.");
    if (GreatSeaMap::IsBusy())
        return fail("Wait for the Great Sea Map move to finish.");
    if (dComIfGp_isNextStagePending())
        return fail("A stage change is already queued.");
    if (fopScn_getName(fopScnM_getStageScene()) != WWHD_SCENE_PLAY)
        return fail("Not in a stage.");
    if (!daPy_lk_c_getPlayer())
        return fail("Link is not spawned.");

    s_t = t;
    s_frames = 0;
    const char* cur = dComIfGp_getCurStageName();
    if (strncmp(cur, s_t.stage, WWHD_STAGE_NAME_MAX) != 0)
        return startStageWarp();

    const s8 here = daPy_getRoomNo();
    if (s_t.room < 0 || s_t.room == here) {
        place(s_t, true);
        finish("Teleported to");
        return true;
    }
    return startRoomLoad();
}

bool GoToInStage(s8 room, const cXyz& pos, bool setFacing, s16 angle, const char* label)
{
    Target t = {};
    snprintf(t.stage, sizeof(t.stage), "%s", dComIfGp_getCurStageName());
    t.room = room;
    t.pos = pos;
    t.setFacing = setFacing;
    t.angle = angle;
    t.hasCamera = false;
    snprintf(t.label, sizeof(t.label), "%s", label ? label : "the place");
    return begin(t);
}

bool SaveSlot(int i)
{
    if (!validIndex(i))
        return false;
    if (!wwhd_regionResolved)
        return fail("Game addresses are unresolved.");
    const char*  stage = dComIfGp_getCurStageName();
    const cXyz*  pos   = daPy_getStorePos();
    const csXyz* shape = daPy_getStoreShapeAngle();
    if (!stage[0] || !pos || !shape || !daPy_lk_c_getPlayer())
        return fail("No game in progress.");
    if (dComIfGp_isNextStagePending())
        return fail("Wait for the stage change to finish.");
    const s8 room = daPy_getRoomNo();
    if (room < 0)
        return fail("Link is not in a room.");

    Slot& s = s_slots[i];
    const bool keepName = s.valid && s.name[0];
    char name[NAME_MAX];
    memcpy(name, s.name, sizeof(name));
    memset(&s, 0, sizeof(s));
    s.valid = true;
    if (keepName)
        memcpy(s.name, name, sizeof(name));
    snprintf(s.stage, sizeof(s.stage), "%s", stage);
    s.room  = room;
    s.layer = dComIfGp_getCurStageLayer();
    s.pos   = *pos;
    s.angle = shape->y;

    const dCam_view_t* view = s_cam ? dCam_getView(dCam_getProcess(s_cam)) : nullptr;
    if (view) {
        s.hasCamera = true;
        s.camEye = view->mEye;
        s.camCenter = view->mCenter;
    }
    Config::MarkDirty();

    char place[48];
    wwhd_placeName(s.stage, s.room, place, sizeof(place));
    setStatus("Saved slot %d: %s", i + 1, place);
    Notifications::ShowKeyedTitledf(kNotifyKey, Notifications::Success, "Coordinates",
                                    "Slot %d saved: %s, room %d", i + 1, place, (int)room);
    Logger::Log("[coords] slot %d saved: stage=%s room=%d layer=%d pos=(%.1f,%.1f,%.1f) "
                "angle=%d camera=%d",
                i + 1, s.stage, (int)room, (int)s.layer, (double)pos->x, (double)pos->y,
                (double)pos->z, (int)shape->y, (int)s.hasCamera);
    return true;
}

bool LoadSlot(int i)
{
    if (!validIndex(i))
        return false;
    const Slot& s = s_slots[i];
    if (!s.valid)
        return fail("That slot is empty.");

    Target t = {};
    memcpy(t.stage, s.stage, sizeof(t.stage));
    t.room = s.room;
    t.pos = s.pos;
    t.setFacing = true;
    t.angle = s.angle;
    t.hasCamera = s.hasCamera;
    t.camEye = s.camEye;
    t.camCenter = s.camCenter;
    if (s.name[0])
        snprintf(t.label, sizeof(t.label), "%s", s.name);
    else
        snprintf(t.label, sizeof(t.label), "slot %d", i + 1);
    return begin(t);
}

static void tickRoom()
{
    ++s_frames;
    const int step = dStage_loadRoomFor(s_t.room);
    place(s_t, false);
    if (s_frames % kLogEvery == 0)
        logState("room load");

    const bool sceneUp = dStage_isRoomLoaded(s_t.room) && !dStage_isRoomStreamingBusy();
    if (sceneUp && s_sceneUpFrame < 0) {
        s_sceneUpFrame = s_frames;
        Logger::Log("[coords] room %d scene up after %d frames; waiting for its collision",
                    (int)s_t.room, s_frames);
    }
    if (sceneUp && roomReady(s_t.room)) {
        arrive("Teleported to");
        return;
    }
    if (step < 0) {
        abortMove("The stage lost its room table.");
        return;
    }
    if (sceneUp && s_frames - s_sceneUpFrame > kBgFrames) {
        Logger::LogWarn("[coords] Link did not land in room %d within %d frames of it coming up; placing anyway",
                        (int)s_t.room, s_frames - s_sceneUpFrame);
        logState("no landing");
        arrive("Placed (no floor found) at");
        return;
    }
    if (s_frames > kRoomFrames) {
        logState("room load timeout");
        abortMove("The room never finished loading.");
    }
}

static void tickWarp()
{
    const daPy_lk_c* link = daPy_lk_c_getPlayer();
    if (!link)
        s_sawNoLink = true;
    ++s_frames;
    if (s_frames % kLogEvery == 0)
        logState(s_phase == PHASE_WARP ? "warp" : "settle");

    if (s_phase == PHASE_WARP) {
        if (!dComIfGp_isNextStagePending()) {
            s_phase = PHASE_SETTLE;
            s_frames = 0;
        } else if (s_frames > kWarpFrames) {
            abortMove("The stage never changed.");
        }
        return;
    }

    const bool inStage = link && !dComIfGp_isNextStagePending() &&
                         fopScn_getName(fopScnM_getStageScene()) == WWHD_SCENE_PLAY &&
                         strncmp(dComIfGp_getCurStageName(), s_t.stage, WWHD_STAGE_NAME_MAX) == 0;
    const bool fresh = s_sawNoLink || (const void*)link != s_oldLink || s_frames > kSameLinkFrames;
    if (inStage && fresh) {
        const cXyz* at = daPy_getStorePos();
        float off2 = 0.0f;
        if (at) {
            const float dx = at->x - s_t.pos.x, dy = at->y - s_t.pos.y, dz = at->z - s_t.pos.z;
            off2 = dx * dx + dy * dy + dz * dz;
        }
        if (off2 > kPlaceTolerance * kPlaceTolerance) {
            Logger::LogWarn("[coords] restart record missed by %.1f; placing directly",
                            (double)sqrtf(off2));
            if (s_t.room >= 0 && daPy_getRoomNo() != s_t.room) {
                startRoomLoad();
                return;
            }
        }
        place(s_t, true);
        Notifications::Dismiss(kNotifyKey);
        finish("Arrived at");
    } else if (s_frames > kSettleFrames) {
        abortMove("Link did not reappear in the loaded stage.");
    }
}

void Tick(bool acceptInput)
{
    switch (s_phase) {
    case PHASE_ROOM:   tickRoom(); break;
    case PHASE_WARP:
    case PHASE_SETTLE: tickWarp(); break;
    default: break;
    }

    if (!acceptInput)
        return;
    if (Hotkeys::Pressed(Hotkeys::HOTKEY_COORD_SAVE))
        SaveSlot(s_selected);
    else if (Hotkeys::Pressed(Hotkeys::HOTKEY_COORD_LOAD))
        LoadSlot(s_selected);
}

void OnApplicationStart()
{
    s_phase = PHASE_IDLE;
    s_frames = 0;
    s_cam = nullptr;
    s_status[0] = '\0';
}
}
}
