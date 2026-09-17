#include "tools/mss.h"

#include "core/hotkeys.h"
#include "core/input.h"
#include "core/logger.h"
#include "core/settings.h"
#include "libwwhd/libwwhd.h"
#include "tools/flycam.h"

namespace Tools {
namespace Mss {
static bool s_up = true;

bool IsEnabled()
{
    return Config::g_settings.mssEnabled;
}

void SetEnabled(bool enabled)
{
    Config::g_settings.mssEnabled = enabled;
}

// Alternates the stick each frame while swimming; every change of the decision is logged.
bool NextStick(uint32_t held, float* x, float* y)
{
    if (!x || !y)
        return false;

    const uint32_t combo = Hotkeys::Get(Hotkeys::HOTKEY_MSS);
    const bool comboHeld = combo != 0 && (held & combo) == combo;
    const bool enabled = IsEnabled();
    const bool flyCam = FlyCam::IsActive();
    const int32_t proc = comboHeld ? daPy_getCurProc() : (int32_t)-1;
    const bool swimming = proc == (int32_t)daPyProc_SWIM_WAIT_e ||
                          proc == (int32_t)daPyProc_SWIM_MOVE_e;
    const bool run = comboHeld && enabled && !flyCam && swimming;

    static uint32_t s_lastState = 0;
    const uint32_t state = !comboHeld ? 0u
                         : 1u | (enabled ? 2u : 0u) | (flyCam ? 4u : 0u) |
                           (swimming ? 8u : 0u);
    if (comboHeld && state != s_lastState)
        Logger::Log("mss: %s held, %s (enabled=%d flycam=%d proc=%d)",
                    Input::DescribeCombo(combo), run ? "running" : "not running",
                    (int)enabled, (int)flyCam, (int)proc);
    s_lastState = state;

    if (!run)
        return false;

    *x = 0.0f;
    *y = s_up ? 1.0f : -1.0f;
    s_up = !s_up;
    return true;
}

void OnApplicationStart()
{
    s_up = true;
}
}
}
