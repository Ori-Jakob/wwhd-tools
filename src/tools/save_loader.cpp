#include "tools/save_loader.h"

#include "core/logger.h"
#include "core/storage.h"
#include "libwwhd/libwwhd.h"
#include "tools/save_states.h"
#include "ui/notifications.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace Tools {
namespace SaveLoader {
static const u32 kFileCapacity = 0x2400;
static const int kListMax = 64;
static const int kLabelMax = 112;

static u8   s_file[kFileCapacity];
static u32  s_fileSize = 0;
static char s_reading[Storage::SAVE_PATH_MAX] = "";
static char s_opened[Storage::SAVE_PATH_MAX] = "";
static dSv_save_c s_slot[kSlots];
static bool s_slotValid[kSlots];
static char s_slotLabel[kSlots][kLabelMax];
static dSv_info_c s_block;

static Storage::SavePathEntry s_list[kListMax];
static int  s_count = 0;
static bool s_listValid = false;
static char s_status[96] = "";

static const char kNotifyKey[] = "saveloader";

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
    Notifications::ShowKeyed(kNotifyKey, Notifications::Error, "Save Loader", message);
    return false;
}

static bool validReturnPlace(const dSv_player_return_place_c* rp)
{
    if (!rp->mName[0] || !memchr(rp->mName, '\0', sizeof(rp->mName)))
        return false;
    for (const char* c = rp->mName; *c; ++c)
        if (*c < 0x21 || *c > 0x7E)
            return false;
    return rp->mRoomNo >= 0 && rp->mRoomNo < WWHD_ROOM_MAX;
}

static const char* roomText(int room)
{
    static char text[8];
    snprintf(text, sizeof(text), "%d", room);
    return text;
}

static void describeSlots()
{
    for (int i = 0; i < kSlots; ++i) {
        const u8* slot = dSv_saveFileSlot(s_file, s_fileSize, i);
        s_slotValid[i] = false;
        if (!slot) {
            snprintf(s_slotLabel[i], kLabelMax, "%d: missing", i + 1);
            continue;
        }
        memset(&s_slot[i], 0, sizeof(s_slot[i]));
        dSv_unpackSlot(slot, &s_slot[i]);
        const dSv_player_c* p = &s_slot[i].mPlayer;
        if (!validReturnPlace(&p->mReturnPlace)) {
            snprintf(s_slotLabel[i], kLabelMax, "%d: empty", i + 1);
            continue;
        }
        s_slotValid[i] = true;

        const bool saved = p->mPlayerStatusB.mDateIPL != 0;
        char place[40];
        wwhd_placeName(p->mReturnPlace.mName, p->mReturnPlace.mRoomNo, place, sizeof(place));
        const bool island = strncmp(p->mReturnPlace.mName, "sea", 8) == 0;
        snprintf(s_slotLabel[i], kLabelMax, "%d: %s%s%s, %d hearts, %d rupees%s",
                 i + 1, place, island ? "" : " room ",
                 island ? "" : roomText(p->mReturnPlace.mRoomNo),
                 (int)(p->mPlayerStatusA.mMaxLife / 4), (int)p->mPlayerStatusA.mRupee,
                 saved ? "" : " (never saved)");
    }
}

void RefreshList()
{
    s_count = Storage::ListSaveFiles(s_list, kListMax);
    s_listValid = true;
}

int Count()
{
    if (!s_listValid)
        RefreshList();
    return s_count;
}

const char* PathAt(int index)
{
    return (index >= 0 && index < Count()) ? s_list[index].path : "";
}

bool Open(const char* relPath)
{
    if (!relPath || !relPath[0])
        return false;
    if (IsBusy())
        return fail("Still busy with the last file.");
    if (!SaveStates::BeginRead(Storage::ReadSaveFile, relPath, s_file, kFileCapacity))
        return fail("Could not start reading the save.");
    strncpy(s_reading, relPath, sizeof(s_reading) - 1);
    s_reading[sizeof(s_reading) - 1] = '\0';
    setStatus("Reading %s ...", s_reading);
    return true;
}

const char* Reading() { return s_reading; }
const char* Opened()  { return s_opened; }

bool SlotValid(int slot)
{
    return s_opened[0] && slot >= 0 && slot < kSlots && s_slotValid[slot];
}

const char* SlotLabel(int slot)
{
    return (s_opened[0] && slot >= 0 && slot < kSlots) ? s_slotLabel[slot] : "";
}

bool LoadSlot(int slot)
{
    if (!s_opened[0])
        return fail("Open a save file first.");
    if (!SlotValid(slot))
        return fail("That slot is empty.");
    if (IsBusy())
        return fail("Still busy with the last file.");

    const u8* image = dSv_saveFileSlot(s_file, s_fileSize, slot);
    if (!image)
        return fail("That slot is missing from the file.");
    dSv_info_loadSlotImage(&s_block, image, slot);

    const dSv_player_return_place_c* rp = &s_block.mSavedata.mPlayer.mReturnPlace;
    char label[Storage::SAVE_PATH_MAX + 32];
    snprintf(label, sizeof(label), "%s slot %d", s_opened, slot + 1);
    if (!SaveStates::LoadBlock(&s_block, rp->mName, (s16)rp->mPoint, rp->mRoomNo,
                               WWHD_STAGE_LAYER_KEEP, nullptr, label)) {
        setStatus("%s", SaveStates::Status());
        return false;
    }
    setStatus("Loading slot %d of %s ...", slot + 1, s_opened);
    Logger::Log("[saveloader] loading %s slot %d: stage=%s room=%d point=%d",
                s_opened, slot + 1, rp->mName, (int)rp->mRoomNo, (int)rp->mPoint);
    return true;
}

bool        IsBusy() { return s_reading[0] != '\0' || SaveStates::IsBusy(); }
const char* Status() { return s_status; }

void Tick()
{
    if (!s_reading[0])
        return;
    bool ok = false;
    u32 size = 0;
    if (!SaveStates::ReadFinished(&ok, &size))
        return;

    if (!ok) {
        s_reading[0] = '\0';
        fail("Could not read the save file.");
        return;
    }
    if (size < dSv_SAVEFILE_MIN_SIZE) {
        s_reading[0] = '\0';
        fail("Not a save file this build understands.");
        return;
    }
    s_fileSize = size;
    strncpy(s_opened, s_reading, sizeof(s_opened));
    s_reading[0] = '\0';
    describeSlots();
    setStatus("Opened %s", s_opened);
    Logger::Log("[saveloader] opened %s (%u bytes)", s_opened, (unsigned)size);
}

void OnApplicationStart()
{
    s_reading[0] = '\0';
    s_opened[0] = '\0';
    s_fileSize = 0;
    s_listValid = false;
    s_count = 0;
    s_status[0] = '\0';
}
}
}
