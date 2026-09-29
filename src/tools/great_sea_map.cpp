#include "tools/great_sea_map.h"

#include "core/input.h"
#include "core/logger.h"
#include "tools/coordinates.h"
#include "tools/save_states.h"
#include "ui/notifications.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace Tools {
namespace GreatSeaMap {
static const char kNotifyKey[] = "great_sea_map";

static const float kSeaLevel     = 0.0f;
// Link is held this high while a square loads so his ground check finds its surface.
static const float kProbeY       = 30000.0f;
static const float kBoatDrop     = 100.0f;
static const float kBoatAhead    = 300.0f;
static const float kCamBack      = 450.0f;
static const float kCamUp        = 220.0f;
static const float kCamLook      = 110.0f;

static const int   kRoomFrames   = 600;
static const int   kGroundPrefer = 20;
static const int   kGroundWait   = 60;
static const int   kBoatSettle   = 8;
static const int   kBoatRide     = 60;
static const int   kLogEvery     = 30;

static const float kCursorSpeed  = 0.018f;
static const float kFacingDead   = 0.35f;
static const float kSelectDead   = 0.55f;
static const int   kRepeatDelay  = 10;
static const int   kRepeatEvery  = 4;

enum JobKind { JOB_NONE = 0, JOB_LINK_POINT, JOB_LINK_BOAT, JOB_STREAM };

struct Job {
    JobKind kind;
    s8      room;
    float   x, z;
    s16     angle;
    int     frames;
    int     readyFrames;
    int     rideFrames;
    bool    released;
    bool    noTable;
    char    label[48];
};

static State s_ui = { VIEW_CHART, 3, 3, 0.5f, 0.5f, 0, false };
static Job   s_job;
static bool  s_noteWindow = false;
static bool  s_noteMap = false;
static int   s_repeatFrames = 0;
static int   s_repeatDx = 0, s_repeatDy = 0;

State& Ui() { return s_ui; }

void NoteWindow(bool windowFocused, bool mapFocused)
{
    s_noteWindow = windowFocused;
    s_noteMap = mapFocused;
}

static float angleToRad(s16 a) { return (float)a * (3.14159265f / 32768.0f); }

static s16 dirToAngle(float dx, float dz)
{
    return (s16)(int)(atan2f(dx, dz) * (32768.0f / 3.14159265f));
}

bool OnGreatSea()
{
    if (!wwhd_regionResolved)
        return false;
    return strcmp(dComIfGp_getCurStageName(), "sea") == 0;
}

bool IsBusy() { return s_job.kind != JOB_NONE; }

int RoomAt(float x, float z)
{
    const int col = (int)floorf((x + SEA_HALF) / SQUARE);
    const int row = (int)floorf((z + SEA_HALF) / SQUARE);
    if (col < 0 || col >= GRID || row < 0 || row >= GRID)
        return -1;
    return row * GRID + col + 1;
}

void SquareName(int col, int row, char* out, int cap)
{
    if (!out || cap <= 0)
        return;
    const char* island = wwhd_islandName(row * GRID + col + 1);
    snprintf(out, (size_t)cap, "%c%d  %s", 'A' + col, row + 1, island ? island : "");
}

void CursorWorld(float* x, float* z)
{
    if (x) *x = -SEA_HALF + ((float)s_ui.col + s_ui.cu) * SQUARE;
    if (z) *z = -SEA_HALF + ((float)s_ui.row + s_ui.cv) * SQUARE;
}

static bool toChart(float x, float z, float* u, float* v)
{
    const float span = SEA_HALF * 2.0f;
    const float cu = (x + SEA_HALF) / span;
    const float cv = (z + SEA_HALF) / span;
    if (cu < 0.0f || cu > 1.0f || cv < 0.0f || cv > 1.0f)
        return false;
    if (u) *u = cu;
    if (v) *v = cv;
    return true;
}

bool LinkOnChart(float* u, float* v, s16* facing)
{
    if (!OnGreatSea() || !daPy_lk_c_getPlayer())
        return false;
    const cXyz* pos = daPy_getStorePos();
    const csXyz* shape = daPy_getStoreShapeAngle();
    if (!pos || !shape)
        return false;
    if (facing)
        *facing = shape->y;
    return toChart(pos->x, pos->z, u, v);
}

bool BoatOnChart(float* u, float* v, s16* facing)
{
    if (!OnGreatSea())
        return false;
    daShip_c* ship = get_daShip();
    if (!ship)
        return false;
    if (facing)
        *facing = daShip_getFacing(ship);
    return toChart(ship->base.current.pos.x, ship->base.current.pos.z, u, v);
}

const char* Blocker(Action action)
{
    if (!OnGreatSea())
        return "Only on the Great Sea.";
    if (!daPy_lk_c_getPlayer())
        return "Link isn't loaded yet.";
    if (IsBusy() || Coordinates::IsBusy() || SaveStates::IsBusy())
        return "Still busy with the last move.";
    if (dComIfGp_isNextStagePending())
        return "Wait for the area to load.";
    if (dEvt_isEventRunning())
        return "Not during a cutscene.";

    const bool ship = get_daShip() != nullptr;
    const bool riding = daPy_isRidingShip() != 0;
    switch (action) {
    case ACTION_TELEPORT_LINK:
        return riding && !ship ? "The boat is not on the sea." : nullptr;
    case ACTION_TELEPORT_BOAT:
    case ACTION_TELEPORT_BOTH:
        return ship ? nullptr : "The boat is not on the sea.";
    case ACTION_LINK_TO_BOAT:
        if (!ship)
            return "The boat is not on the sea.";
        return riding ? "Link is already in the boat." : nullptr;
    case ACTION_BOAT_TO_LINK:
        if (!ship)
            return "The boat is not on the sea.";
        return daPy_isSwimming() ? nullptr : "Link has to be swimming.";
    default:
        return "Unknown action.";
    }
}

static void notify(Notifications::Kind kind, const char* fmt, const char* what)
{
    Notifications::ShowKeyedTitledf(kNotifyKey, kind, "Great Sea Map", fmt, what);
}

static void cameraBehind(const cXyz& pos, s16 angle, float back, float up)
{
    const float a = angleToRad(angle);
    const float sx = sinf(a), cz = cosf(a);
    cXyz eye = { pos.x - sx * back, pos.y + up, pos.z - cz * back };
    cXyz center = { pos.x, pos.y + kCamLook, pos.z };
    Coordinates::SetCamera(eye, center);
}

// Moving the fall start with him keeps the landing from counting as a long fall.
static void pin(const cXyz& pos)
{
    daPy_setPosition(&pos);
    daPy_resetFallStart(&pos);
}

static void setFacingBoth(s16 angle)
{
    daPy_setFacing(angle);
    if (csXyz* store = daPy_getStoreAngle())
        store->y = angle;
    if (daPy_lk_c* link = daPy_lk_c_getPlayer())
        link->base.current.angle.y = angle;
}

static bool roomReady(int room)
{
    const u8 flags = dStage_getRoomFlags(room);
    return (flags & WWHD_ROOM_FLAG_LOADED) && !(flags & WWHD_ROOM_FLAG_NO_BG) &&
           !dStage_isRoomStreamingBusy();
}

// Only when wrong: the zone update also clears the room switches of active zones.
static void settleStayRoom(int room)
{
    if (room >= 1 && dStage_getStayNo() != room && dStage_isRoomLoaded(room)) {
        Logger::Log("[seamap] stay room %d -> %d", (int)dStage_getStayNo(), room);
        dStage_zoneCountCheck(room);
    }
}

static void logJob(const char* tag)
{
    Logger::Log("[seamap] %s: kind=%d room=%d frames=%d ready=%d flags=%02X busy=%d "
                "stay=%d linkRoom=%d ground=%.1f riding=%d",
                tag, (int)s_job.kind, (int)s_job.room, s_job.frames, s_job.readyFrames,
                (unsigned)dStage_getRoomFlags(s_job.room), dStage_isRoomStreamingBusy(),
                (int)dStage_getStayNo(), (int)daPy_getRoomNo(), (double)daPy_getGroundHeight(),
                daPy_isRidingShip());
}

static void finish(const char* how)
{
    Logger::Log("[seamap] %s %s after %d frames", how, s_job.label, s_job.frames);
    Notifications::Dismiss(kNotifyKey);
    Notifications::ShowKeyedTitledf(kNotifyKey, Notifications::Success, "Great Sea Map", "%s %s",
                                    how, s_job.label);
    s_job.kind = JOB_NONE;
}

static void abortJob(const char* why)
{
    Logger::LogWarn("[seamap] %s aborted: %s", s_job.label, why);
    Notifications::Dismiss(kNotifyKey);
    notify(Notifications::Error, "%s", why);
    s_job.kind = JOB_NONE;
}

static void startJob(JobKind kind, float x, float z, s16 angle, const char* label)
{
    memset(&s_job, 0, sizeof(s_job));
    s_job.kind = kind;
    s_job.room = (s8)RoomAt(x, z);
    s_job.x = x;
    s_job.z = z;
    s_job.angle = angle;
    snprintf(s_job.label, sizeof(s_job.label), "%s", label);
    if (!roomReady(s_job.room))
        Notifications::ShowStickyf(kNotifyKey, Notifications::Info, "Great Sea Map",
                                   "Loading %s ...", s_job.label);
    logJob("start");
}

static bool moveBoat(float x, float z, s16 angle)
{
    daShip_c* ship = get_daShip();
    if (!ship)
        return false;
    float y = ship->base.current.pos.y;
    if (y > kSeaLevel + 300.0f || y < kSeaLevel - 300.0f)
        y = kSeaLevel;
    cXyz pos = { x, y, z };
    daShip_setPosition(ship, &pos);
    daShip_setFacing(ship, angle);
    Logger::Log("[seamap] boat moved to (%.1f %.1f %.1f) facing %d", (double)x, (double)y,
                (double)z, (int)(u16)angle);
    return true;
}

// Aboard, Link goes wherever the boat goes.
static bool sailAboard(float x, float z, const char* label)
{
    if (!moveBoat(x, z, s_ui.facing))
        return false;
    daShip_c* ship = get_daShip();
    cameraBehind(ship->base.current.pos, s_ui.facing, kCamBack * 2.0f, kCamUp * 1.5f);
    startJob(JOB_STREAM, x, z, s_ui.facing, label);
    return true;
}

static void cursorLabel(char* out, int cap)
{
    char square[40];
    SquareName(s_ui.col, s_ui.row, square, sizeof(square));
    snprintf(out, (size_t)cap, "%s", square);
}

bool Run(Action action)
{
    if (const char* why = Blocker(action)) {
        notify(Notifications::Error, "%s", why);
        return false;
    }

    float x = 0.0f, z = 0.0f;
    CursorWorld(&x, &z);
    char label[48];
    cursorLabel(label, sizeof(label));
    const bool riding = daPy_isRidingShip() != 0;

    switch (action) {
    case ACTION_TELEPORT_LINK:
        if (riding)
            return sailAboard(x, z, label);
        startJob(JOB_LINK_POINT, x, z, s_ui.facing, label);
        return true;

    case ACTION_TELEPORT_BOAT:
        if (riding)
            return sailAboard(x, z, label);
        if (!moveBoat(x, z, s_ui.facing))
            return false;
        notify(Notifications::Success, "Boat moved to %s", label);
        return true;

    case ACTION_TELEPORT_BOTH:
        if (riding)
            return sailAboard(x, z, label);
        if (!moveBoat(x, z, s_ui.facing))
            return false;
        startJob(JOB_LINK_BOAT, x, z, s_ui.facing, label);
        return true;

    case ACTION_LINK_TO_BOAT: {
        daShip_c* ship = get_daShip();
        const cXyz& p = ship->base.current.pos;
        startJob(JOB_LINK_BOAT, p.x, p.z, daShip_getFacing(ship), "the boat");
        return true;
    }

    case ACTION_BOAT_TO_LINK: {
        const cXyz* pos = daPy_getStorePos();
        const csXyz* shape = daPy_getStoreShapeAngle();
        if (!pos || !shape)
            return false;
        const float a = angleToRad(shape->y);
        const float bx = pos->x + sinf(a) * kBoatAhead;
        const float bz = pos->z + cosf(a) * kBoatAhead;
        daShip_c* ship = get_daShip();
        cXyz boat = { bx, pos->y, bz };
        daShip_setPosition(ship, &boat);
        daShip_setFacing(ship, (s16)(shape->y + 0x4000));
        Logger::Log("[seamap] boat brought to Link at (%.1f %.1f %.1f)", (double)bx,
                    (double)pos->y, (double)bz);
        notify(Notifications::Success, "%s", "Boat brought to Link");
        return true;
    }

    default:
        return false;
    }
}

static void tickLinkPoint(bool ready)
{
    const cXyz probe = { s_job.x, kProbeY, s_job.z };
    const bool gaveUp = s_job.noTable || s_job.frames >= kRoomFrames;
    if (s_job.frames <= 2 || (!ready && !gaveUp)) {
        pin(probe);
        return;
    }

    ++s_job.readyFrames;
    const float ground = daPy_getGroundHeight();
    const bool valid = ground > -1.0e8f && ground < kProbeY - 1.0f;
    const bool inRoom = daPy_getRoomNo() == s_job.room;
    const bool decide = (valid && (inRoom || s_job.readyFrames > kGroundPrefer)) ||
                        s_job.readyFrames > kGroundWait || gaveUp;
    if (!decide) {
        pin(probe);
        return;
    }

    const bool land = valid && ground > kSeaLevel - 5.0f;
    const cXyz dest = { s_job.x, land ? ground : kSeaLevel, s_job.z };
    Logger::Log("[seamap] placing Link: ground %s %.1f in room %d (target %d) -> %s y=%.1f",
                valid ? "at" : "none,", (double)ground, (int)daPy_getRoomNo(), (int)s_job.room,
                land ? "land" : "water", (double)dest.y);
    if (gaveUp)
        logJob("placing without the room");
    pin(dest);
    setFacingBoth(s_job.angle);
    cameraBehind(dest, s_job.angle, kCamBack, kCamUp);
    if (!land)
        settleStayRoom(s_job.room);
    finish(gaveUp && !ready ? "Placed (square still loading) at" : "Teleported to");
}

static void tickLinkBoat(bool ready)
{
    daShip_c* ship = get_daShip();
    if (!ship) {
        abortJob("The boat went away.");
        return;
    }
    const bool gaveUp = s_job.noTable || s_job.frames >= kRoomFrames;

    if (daPy_isRidingShip()) {
        if (ready || gaveUp) {
            settleStayRoom(s_job.room);
            finish("Aboard at");
        }
        return;
    }

    const cXyz& boat = ship->base.current.pos;
    const cXyz above = { boat.x, boat.y + kBoatDrop, boat.z };

    if (!s_job.released) {
        if (ready || gaveUp)
            ++s_job.readyFrames;
        pin(above);
        setFacingBoth(daShip_getFacing(ship));
        if (s_job.readyFrames > kBoatSettle) {
            s_job.released = true;
            cameraBehind(boat, daShip_getFacing(ship), kCamBack * 2.0f, kCamUp * 1.5f);
            logJob("dropping into the boat");
        }
        return;
    }

    if (++s_job.rideFrames > kBoatRide) {
        logJob("did not board");
        settleStayRoom(s_job.room);
        finish("Dropped beside the boat at");
    }
}

static void tickStream(bool ready)
{
    const bool gaveUp = s_job.noTable || s_job.frames >= kRoomFrames;
    if (!ready && !gaveUp)
        return;
    if (gaveUp && !ready)
        logJob("room did not report ready");
    settleStayRoom(s_job.room);
    finish("Sailed to");
}

static void tickJob()
{
    if (s_job.kind == JOB_NONE)
        return;
    if (!OnGreatSea() || !daPy_lk_c_getPlayer()) {
        abortJob("Link left the Great Sea.");
        return;
    }

    ++s_job.frames;
    const bool ready = s_job.room < 1 || roomReady(s_job.room);
    if (!ready && !s_job.noTable && s_job.room >= 1 && dStage_loadRoomFor(s_job.room) < 0) {
        s_job.noTable = true;
        logJob("no room table entry");
    }
    if (s_job.frames % kLogEvery == 0)
        logJob("streaming");

    switch (s_job.kind) {
    case JOB_LINK_POINT: tickLinkPoint(ready); break;
    case JOB_LINK_BOAT:  tickLinkBoat(ready);  break;
    case JOB_STREAM:     tickStream(ready);    break;
    default: s_job.kind = JOB_NONE; break;
    }
}

void ZoomIn()
{
    s_ui.view = VIEW_SQUARE;
    s_ui.cu = 0.5f;
    s_ui.cv = 0.5f;
    float u = 0.0f, v = 0.0f;
    s16 facing = 0;
    const bool haveLink = LinkOnChart(&u, &v, &facing);
    if (haveLink)
        s_ui.facing = facing;
    float bu = 0.0f, bv = 0.0f;
    if (haveLink && (int)(u * GRID) == s_ui.col && (int)(v * GRID) == s_ui.row) {
        s_ui.cu = u * GRID - (float)s_ui.col;
        s_ui.cv = v * GRID - (float)s_ui.row;
    } else if (BoatOnChart(&bu, &bv, nullptr) && (int)(bu * GRID) == s_ui.col &&
               (int)(bv * GRID) == s_ui.row) {
        s_ui.cu = bu * GRID - (float)s_ui.col;
        s_ui.cv = bv * GRID - (float)s_ui.row;
    }
    s_ui.focusMap = true;
}

void ZoomOut()
{
    s_ui.view = VIEW_CHART;
    s_ui.focusMap = true;
}

static void moveSelection(int dx, int dy)
{
    int col = s_ui.col + dx, row = s_ui.row + dy;
    if (col < 0) col = 0;
    if (col >= GRID) col = GRID - 1;
    if (row < 0) row = 0;
    if (row >= GRID) row = GRID - 1;
    s_ui.col = col;
    s_ui.row = row;
}

static void tickChartInput(const Input::Snapshot& in)
{
    int dx = 0, dy = 0;
    if (in.pressed & Input::BTN_LEFT)  dx = -1;
    if (in.pressed & Input::BTN_RIGHT) dx = 1;
    if (in.pressed & Input::BTN_UP)    dy = -1;
    if (in.pressed & Input::BTN_DOWN)  dy = 1;
    if (dx || dy)
        moveSelection(dx, dy);

    int sx = 0, sy = 0;
    if (in.lx < -kSelectDead) sx = -1;
    if (in.lx > kSelectDead)  sx = 1;
    if (in.ly > kSelectDead)  sy = -1;
    if (in.ly < -kSelectDead) sy = 1;
    if (sx || sy) {
        if (sx != s_repeatDx || sy != s_repeatDy) {
            moveSelection(sx, sy);
            s_repeatFrames = 0;
        } else if (++s_repeatFrames >= kRepeatDelay &&
                   (s_repeatFrames - kRepeatDelay) % kRepeatEvery == 0) {
            moveSelection(sx, sy);
        }
    }
    s_repeatDx = sx;
    s_repeatDy = sy;

    if (in.pressed & Input::BTN_A)
        ZoomIn();
    else if (in.pressed & Input::BTN_X)
        Run(ACTION_LINK_TO_BOAT);
    else if (in.pressed & Input::BTN_Y)
        Run(ACTION_BOAT_TO_LINK);
}

static void tickSquareInput(const Input::Snapshot& in, bool mapFocused)
{
    s_ui.cu += in.lx * fabsf(in.lx) * kCursorSpeed;
    s_ui.cv -= in.ly * fabsf(in.ly) * kCursorSpeed;
    if (s_ui.cu < 0.0f) s_ui.cu = 0.0f;
    if (s_ui.cu > 1.0f) s_ui.cu = 1.0f;
    if (s_ui.cv < 0.0f) s_ui.cv = 0.0f;
    if (s_ui.cv > 1.0f) s_ui.cv = 1.0f;

    if (in.rx * in.rx + in.ry * in.ry > kFacingDead * kFacingDead)
        s_ui.facing = dirToAngle(in.rx, -in.ry);

    if (in.pressed & Input::BTN_B) {
        ZoomOut();
        return;
    }
    if (!mapFocused)
        return;
    if (in.pressed & Input::BTN_A)
        Run(ACTION_TELEPORT_LINK);
    else if (in.pressed & Input::BTN_X)
        Run(ACTION_TELEPORT_BOAT);
    else if (in.pressed & Input::BTN_Y)
        Run(ACTION_TELEPORT_BOTH);
}

void Tick(bool menuOpen)
{
    tickJob();

    const bool windowFocused = s_noteWindow;
    const bool mapFocused = s_noteMap;
    s_noteWindow = s_noteMap = false;
    if (!menuOpen || !windowFocused) {
        s_repeatDx = s_repeatDy = 0;
        return;
    }

    const Input::Snapshot& in = Input::Current();
    if (s_ui.view == VIEW_CHART) {
        if (mapFocused)
            tickChartInput(in);
        else
            s_repeatDx = s_repeatDy = 0;
    } else {
        tickSquareInput(in, mapFocused);
    }
}

void OnApplicationStart()
{
    memset(&s_job, 0, sizeof(s_job));
    s_ui.view = VIEW_CHART;
    s_ui.focusMap = false;
    s_noteWindow = s_noteMap = false;
    s_repeatDx = s_repeatDy = 0;
}
}
}
