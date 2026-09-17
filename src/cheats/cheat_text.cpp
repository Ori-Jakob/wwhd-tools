#include "cheats/cheat_text.h"

#include "core/hotkeys.h"

#include <string.h>
#include "core/logger.h"
#include "libwwhd/libwwhd.h"

namespace Cheats {
namespace Text {
static bool s_autoAdvance = false;
static bool s_holding = false;
static bool stateIs(const char* name, const char* suffix)
{
    return name && suffix && strcmp(name, suffix) == 0;
}

static bool stateCommitsChoice(const char* name)
{
    return stateIs(name, "::StateID_Select2") ||
           stateIs(name, "::StateID_Select3") ||
           stateIs(name, "::StateID_SelectYoko") ||
           stateIs(name, "::StateID_Input");
}

static bool advanceWanted(u8 status)
{
    return status == fopMsgStts_STOP_e ||
           status == fopMsgStts_CLOSE_WAIT_e ||
           status == fopMsgStts_MSG_DISPLAYED_e ||
           status == fopMsgStts_MSG_CONTINUES_e ||
           status == fopMsgStts_MSG_ENDS_e;
}

#ifdef WWHD_TOOLS_DEBUG
static u8       s_lastStatus = 0xFF;
static bool     s_lastHolding = false;
static unsigned s_driveHits = 0;
static unsigned s_hookCalls = 0;
static unsigned s_hookMismatch = 0;
static u8       s_lastDriveStatus = 0xFF;
static const char* s_lastDriveState = 0;
static unsigned s_frameCalls = 0;
static unsigned s_recHits = 0;
static u8       s_lastRecStatus = 0xFF;
static const char* s_lastRecState = 0;

static const char* statusName(u8 s)
{
    switch (s) {
    case fopMsgStts_NONE_e:          return "none";
    case fopMsgStts_MSG_PREPARING_e: return "preparing";
    case fopMsgStts_BOX_OPENING_e:   return "boxOpening";
    case fopMsgStts_MSG_TYPING_e:    return "typing/OutNow";
    case fopMsgStts_STOP_e:          return "pageWait/Stop";
    case fopMsgStts_SELECT_2_e:      return "select2";
    case fopMsgStts_SELECT_3_e:      return "select3";
    case fopMsgStts_CLOSE_WAIT_e:    return "closeWait";
    case fopMsgStts_MSG_DISPLAYED_e: return "answerWait";
    case fopMsgStts_MSG_CONTINUES_e: return "continues";
    case fopMsgStts_MSG_ENDS_e:      return "ends";
    case fopMsgStts_BOX_CLOSING_e:   return "boxClosing";
    case fopMsgStts_BOX_CLOSED_e:    return "boxClosed";
    case fopMsgStts_MSG_DESTROYED_e: return "destroyed";
    case fopMsgStts_SELECT_YOKO_e:   return "selectYoko";
    case fopMsgStts_INPUT_e:         return "input";
    case fopMsgStts_TACT_e:          return "tact";
    case fopMsgStts_DEMO_e:          return "demo";
    default:                         return "?";
    }
}
#endif

bool AutoAdvanceEnabled() { return s_autoAdvance; }

void SetAutoAdvanceEnabled(bool enabled)
{
    s_autoAdvance = enabled;
    if (!enabled)
        s_holding = false;
    WWHD_BREADCRUMB("text: auto-advance %s", enabled ? "on" : "off");
}

void OnFrameEarly()
{
#ifdef WWHD_TOOLS_DEBUG
    ++s_frameCalls;
#endif
    if (!s_autoAdvance || !s_holding || !dMsg_isBoxUp())
        return;

    dMsgBox_c* box = dMsg_getActiveBox();
    if (!box)
        return;

    u32* rec = dMsg_getInputRecord();
    if (!rec)
        return;

    dMsg_armAutoSend(dMsg_getActiveBoxData());

    const u8 status = dMsg_getBoxStatus();
    const char* state = dMsgBox_getStateName(box);
    if (stateCommitsChoice(state))
        return;

    const bool typing = status == fopMsgStts_MSG_TYPING_e;
    if (typing && dMsg_activeBoxHasPrintFlags())
        rec[0x43] |= WWHD_MSGBOX_PRINT_SKIP;

    const bool advance = advanceWanted(status) ||
                         stateIs(state, "::StateID_SelectStartWait");
    if (advance)
        rec[0] |= WWHD_MSGBOX_DECIDE;

#ifdef WWHD_TOOLS_DEBUG
    ++s_recHits;
    if (status != s_lastRecStatus || state != s_lastRecState) {
        WWHD_BREADCRUMB("text: rec box %08X kind %u st %02X %s state '%s' "
                        "rec=%08X w0=%08X adv=%d bgm=%d ext=%d (frames %u "
                        "hits %u)",
                        (unsigned)(uintptr_t)box, (unsigned)dMsg_getBoxKind(),
                        (unsigned)status, statusName(status),
                        state ? state : "?",
                        (unsigned)(uintptr_t)rec, (unsigned)rec[0],
                        (int)advance, dMsg_isBgmHolding(),
                        dMsgBox_isExternallyDriven(box),
                        s_frameCalls, s_recHits);
        s_lastRecStatus = status;
        s_lastRecState = state;
    }
#endif
}

void OnBoxInput(dMsgBox_c* box)
{
#ifdef WWHD_TOOLS_DEBUG
    ++s_hookCalls;
#endif
    if (!s_autoAdvance || !s_holding || !box)
        return;
    if (box != dMsg_getActiveBox()) {
#ifdef WWHD_TOOLS_DEBUG
        ++s_hookMismatch;
#endif
        return;
    }

    dMsgData_c* data = dMsg_getActiveBoxData();
    dMsg_armAutoSend(data);

    const u8 status = dMsg_getBoxStatus();
#ifdef WWHD_TOOLS_DEBUG
    const u32 flagsBefore = box->mInputFlags;
#endif
    const bool typing = status == fopMsgStts_MSG_TYPING_e;
    if (typing && dMsg_activeBoxHasPrintFlags())
        dMsgBox_skipTyping(box);

    const char* state = dMsgBox_getStateName(box);
    const bool advance = !stateCommitsChoice(state) &&
                         (advanceWanted(status) ||
                          stateIs(state, "::StateID_SelectStartWait"));
    if (advance)
        dMsgBox_pressDecide(box);

#ifdef WWHD_TOOLS_DEBUG
    ++s_driveHits;
    if (status != s_lastDriveStatus || state != s_lastDriveState) {
        WWHD_BREADCRUMB("text: drive box %08X kind %u st %02X %s state '%s' "
                        "in54=%08X->%08X adv=%d bgm=%d ext=%d auto=%d wait=%d%s "
                        "(hits %u)",
                        (unsigned)(uintptr_t)box, (unsigned)dMsg_getBoxKind(),
                        (unsigned)status, statusName(status),
                        state ? state : "?",
                        (unsigned)flagsBefore, (unsigned)box->mInputFlags,
                        (int)advance, dMsg_isBgmHolding(),
                        dMsgBox_isExternallyDriven(box),
                        data ? (int)data->mAutoSendFlag : -1,
                        data ? (int)data->mWaitTimer : -1,
                        typing ? " skip" : "", s_driveHits);
        s_lastDriveStatus = status;
        s_lastDriveState = state;
    }
#endif
}

void Tick(bool acceptInput)
{
    const bool held = Hotkeys::Held(Hotkeys::HOTKEY_TEXT_ADVANCE);
    s_holding = s_autoAdvance && acceptInput && held;

#ifdef WWHD_TOOLS_DEBUG
    const u8 status = dComIfGp_getMesgStatus();
    if (status != s_lastStatus) {
        WWHD_BREADCRUMB("text: status %02X %s -> %02X %s "
                        "(on=%d accept=%d held=%d box=%08X kind=%u "
                        "hook=%u miss=%u)",
                        (unsigned)s_lastStatus, statusName(s_lastStatus),
                        (unsigned)status, statusName(status),
                        (int)s_autoAdvance, (int)acceptInput, (int)held,
                        (unsigned)(uintptr_t)dMsg_getActiveBox(),
                        (unsigned)dMsg_getBoxKind(),
                        s_hookCalls, s_hookMismatch);
        s_lastStatus = status;
    }
    if (held != s_lastHolding) {
        WWHD_BREADCRUMB("text: hotkey %s (status=%02X %s)",
                        held ? "down" : "up", (unsigned)status,
                        statusName(status));
        s_lastHolding = held;
    }
#endif
}

void ResetToDefaults()
{
    s_autoAdvance = false;
    s_holding = false;
}
}
}
