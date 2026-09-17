#include "core/rebind.h"

#include "core/config.h"
#include "core/hotkeys.h"
#include "core/input.h"
#include "ui/menu_nav.h"

#include <stdio.h>

namespace Rebind {
static const int kListenTimeoutFrames = 180;
static const int kFramesPerSecond = 30;

struct Ops {
    int         (*count)();
    uint32_t    (*get)(int index);
    void        (*set)(int index, uint32_t buttons);
    const char* (*name)(int index);
    bool        (*conflicts)(int target, uint32_t buttons, int other);
    const char* (*validate)(int target, uint32_t buttons);
};

static int         hotkeyCount()                { return Hotkeys::HOTKEY_COUNT; }
static uint32_t    hotkeyGet(int i)             { return Hotkeys::Get((Hotkeys::Id)i); }
static void        hotkeySet(int i, uint32_t b) { Hotkeys::Set((Hotkeys::Id)i, b); }
static const char* hotkeyName(int i)            { return Hotkeys::Name((Hotkeys::Id)i); }
static bool        hotkeyConflicts(int t, uint32_t b, int o)
{
    return Hotkeys::Conflicts((Hotkeys::Id)t, b, (Hotkeys::Id)o);
}
static const char* hotkeyValidate(int t, uint32_t b)
{
    return Hotkeys::Validate((Hotkeys::Id)t, b);
}

static int         navCount()                { return Ui::Nav::ACTION_COUNT; }
static uint32_t    navGet(int i)             { return Ui::Nav::Binding((Ui::Nav::Action)i); }
static void        navSet(int i, uint32_t b) { Ui::Nav::SetBinding((Ui::Nav::Action)i, b); }
static const char* navName(int i)            { return Ui::Nav::ActionName((Ui::Nav::Action)i); }
static bool        navConflicts(int t, uint32_t b, int o)
{
    return Ui::Nav::Conflicts((Ui::Nav::Action)t, b, (Ui::Nav::Action)o);
}
static const char* navValidate(int t, uint32_t b)
{
    return Ui::Nav::Validate((Ui::Nav::Action)t, b);
}

static const Ops kOps[DOMAIN_COUNT] = {
    { hotkeyCount, hotkeyGet, hotkeySet, hotkeyName, hotkeyConflicts, hotkeyValidate },
    { navCount,    navGet,    navSet,    navName,    navConflicts,    navValidate    },
};

static Phase       s_phase = PHASE_IDLE;
static Domain      s_domain = DOMAIN_HOTKEYS;
static int         s_index = -1;
static uint32_t    s_mask = 0;
static uint32_t    s_held = 0;
static int         s_idleFrames = 0;
static const char* s_error = nullptr;
static Domain      s_errorDomain = DOMAIN_HOTKEYS;

static bool validDomain(Domain domain)
{
    return domain >= 0 && domain < DOMAIN_COUNT;
}

static void reset()
{
    s_phase = PHASE_IDLE;
    s_index = -1;
    s_mask = 0;
    s_held = 0;
    s_idleFrames = 0;
}

static void setError(Domain domain, const char* text)
{
    s_errorDomain = domain;
    s_error = text;
}

void Begin(Domain domain, int index)
{
    if (!validDomain(domain) || index < 0 || index >= kOps[domain].count())
        return;
    reset();
    s_phase = PHASE_WAIT_RELEASE;
    s_domain = domain;
    s_index = index;
    ClearError(domain);
}

void Cancel() { reset(); }

Phase  CurrentPhase()  { return s_phase; }
Domain CurrentDomain() { return s_domain; }
int    CurrentIndex()  { return s_index; }
bool   IsActive()      { return s_phase != PHASE_IDLE; }

bool BlocksMenuInput()
{
    return s_phase == PHASE_WAIT_RELEASE || s_phase == PHASE_LISTENING;
}

bool IsCapturing(Domain domain, int index)
{
    return s_phase != PHASE_IDLE && s_domain == domain && s_index == index;
}

uint32_t LiveMask() { return s_mask; }
uint32_t LiveHeld() { return s_held; }

int SecondsLeft()
{
    const int left = kListenTimeoutFrames - s_idleFrames;
    return left > 0 ? (left + kFramesPerSecond - 1) / kFramesPerSecond : 0;
}

const char* Error(Domain domain)
{
    return s_errorDomain == domain ? s_error : nullptr;
}

void ClearError(Domain domain)
{
    if (s_errorDomain == domain)
        s_error = nullptr;
}

static bool conflictsWith(int other)
{
    return other != s_index && kOps[s_domain].conflicts(s_index, s_mask, other);
}

static void commit()
{
    const Ops& ops = kOps[s_domain];
    if (const char* error = ops.validate(s_index, s_mask)) {
        setError(s_domain, error);
        reset();
        return;
    }
    for (int i = 0; i < ops.count(); ++i) {
        if (conflictsWith(i)) {
            s_phase = PHASE_CONFLICT;
            return;
        }
    }
    ops.set(s_index, s_mask);
    Config::MarkDirty();
    reset();
}

void Tick()
{
    if (s_phase == PHASE_IDLE || s_phase == PHASE_CONFLICT)
        return;

    const uint32_t held = Input::Current().held;
    s_held = held;

    if (s_phase == PHASE_WAIT_RELEASE) {
        if (held == 0) {
            s_phase = PHASE_LISTENING;
            s_idleFrames = 0;
        }
        return;
    }

    if (held) {
        s_mask |= held;
        return;
    }
    if (!s_mask) {
        if (++s_idleFrames >= kListenTimeoutFrames)
            reset();
        return;
    }
    commit();
}

bool IsConflictPending() { return s_phase == PHASE_CONFLICT; }

uint32_t PendingMask()
{
    return s_phase == PHASE_CONFLICT ? s_mask : 0u;
}

void ConflictNames(char* out, size_t size)
{
    if (!out || !size)
        return;
    out[0] = '\0';
    if (s_phase != PHASE_CONFLICT)
        return;
    const Ops& ops = kOps[s_domain];
    size_t used = 0;
    for (int i = 0; i < ops.count(); ++i) {
        if (!conflictsWith(i))
            continue;
        const int n = snprintf(out + used, size - used, "%s%s",
                               used ? ", " : "", ops.name(i));
        if (n < 0 || (size_t)n >= size - used)
            break;
        used += (size_t)n;
    }
}

void ConfirmConflict()
{
    if (s_phase != PHASE_CONFLICT)
        return;
    const Ops& ops = kOps[s_domain];
    for (int i = 0; i < ops.count(); ++i)
        if (conflictsWith(i))
            ops.set(i, 0u);
    ops.set(s_index, s_mask);
    Config::MarkDirty();
    reset();
}

void CancelConflict() { reset(); }

void OnApplicationStart()
{
    reset();
    s_error = nullptr;
}
}
