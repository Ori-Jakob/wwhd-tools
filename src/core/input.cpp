#include "core/input.h"

#include "core/hotkeys.h"
#include "core/rebind.h"
#include "core/logger.h"
#include "core/settings.h"
#include "tools/mss.h"

#include <padscore/wpad.h>
#include <vpad/input.h>

#include "imgui.h"
#include "imgui_internal.h"

#include <stdio.h>
#include <string.h>

namespace Input {
static const RplHost* s_host = nullptr;

static Snapshot s_snapshot = {};
static uint32_t s_accumHeld = 0;
static uint32_t s_accumSources = 0;
static uint32_t s_prevHeld = 0;
static float    s_lx = 0, s_ly = 0, s_rx = 0, s_ry = 0;
static bool     s_sawInput = false;
static bool     s_block = false;
static bool     s_blockPushed = false;
static bool     s_togglePending = false;
static bool     s_hotkeyFired = false;
static bool     s_quickAccessPending = false;
static bool     s_quickAccessFired = false;
static bool     s_drainHeld = false;
static RplTouch s_lastTouch = {};
static bool     s_haveTouch = false;
static bool     s_wasTouched = false;
static bool     s_stickPushed = false;

static const float kStickDeadzone = 0.06f;

static float applyDeadzone(float v)
{
    if (v > -kStickDeadzone && v < kStickDeadzone)
        return 0.0f;
    return v;
}

void SetHost(const RplHost* host)
{
    s_host = host;
}

void OnApplicationStart()
{
    memset(&s_snapshot, 0, sizeof(s_snapshot));
    s_accumHeld = 0;
    s_accumSources = 0;
    s_prevHeld = 0;
    s_lx = s_ly = s_rx = s_ry = 0.0f;
    s_sawInput = false;
    s_block = false;
    s_blockPushed = false;
    s_togglePending = false;
    s_hotkeyFired = false;
    s_quickAccessPending = false;
    s_quickAccessFired = false;
    s_drainHeld = false;
    memset(&s_lastTouch, 0, sizeof(s_lastTouch));
    s_haveTouch = false;
    s_wasTouched = false;
    s_stickPushed = false;
}

void OnApplicationEnd()
{
    if (s_host) {
        s_host->setInputMode(s_host, RPL_INPUT_PASS);
        s_host->setStick(s_host, nullptr);
    }
    s_block = false;
    s_blockPushed = false;
    s_stickPushed = false;
}

static void pushBlockState()
{
    const bool want = s_block || s_drainHeld;
    if (!s_host || want == s_blockPushed)
        return;
    s_host->setInputMode(s_host, want ? RPL_INPUT_BLOCK : RPL_INPUT_PASS);
    s_blockPushed = want;
}

void SetBlockGameInput(bool block)
{
    s_block = block;
    pushBlockState();
}

bool IsBlockingGameInput() { return s_block || s_drainHeld; }

void DrainHeld()
{
    s_drainHeld = true;
    pushBlockState();
}

const Snapshot& Current() { return s_snapshot; }

// Only the menu and quick-access combos block; gameplay hotkeys pass through.
static void detectOverlayHotkeys(uint32_t held)
{
    if (Rebind::IsActive())
        return;
    const uint32_t menu = Hotkeys::Get(Hotkeys::HOTKEY_MENU);
    const uint32_t quick = Hotkeys::Get(Hotkeys::HOTKEY_QUICK_ACCESS);

    if (menu != 0 && held == menu && s_prevHeld != menu) {
        s_togglePending = true;
        s_drainHeld = true;
    } else if (quick != 0 && held == quick && s_prevHeld != quick) {
        s_quickAccessPending = true;
        s_drainHeld = true;
    }
}

static uint32_t vpadToPro(uint32_t v)
{
    uint32_t out = 0;
    if (v & VPAD_BUTTON_A)       out |= BTN_A;
    if (v & VPAD_BUTTON_B)       out |= BTN_B;
    if (v & VPAD_BUTTON_X)       out |= BTN_X;
    if (v & VPAD_BUTTON_Y)       out |= BTN_Y;
    if (v & VPAD_BUTTON_L)       out |= BTN_L;
    if (v & VPAD_BUTTON_R)       out |= BTN_R;
    if (v & VPAD_BUTTON_ZL)      out |= BTN_ZL;
    if (v & VPAD_BUTTON_ZR)      out |= BTN_ZR;
    if (v & VPAD_BUTTON_STICK_L) out |= BTN_L3;
    if (v & VPAD_BUTTON_STICK_R) out |= BTN_R3;
    if (v & VPAD_BUTTON_PLUS)    out |= BTN_PLUS;
    if (v & VPAD_BUTTON_MINUS)   out |= BTN_MINUS;
    if (v & VPAD_BUTTON_UP)      out |= BTN_UP;
    if (v & VPAD_BUTTON_DOWN)    out |= BTN_DOWN;
    if (v & VPAD_BUTTON_LEFT)    out |= BTN_LEFT;
    if (v & VPAD_BUTTON_RIGHT)   out |= BTN_RIGHT;
    return out;
}

static uint32_t proToPro(uint32_t v)
{
    uint32_t out = 0;
    if (v & WPAD_PRO_BUTTON_A)       out |= BTN_A;
    if (v & WPAD_PRO_BUTTON_B)       out |= BTN_B;
    if (v & WPAD_PRO_BUTTON_X)       out |= BTN_X;
    if (v & WPAD_PRO_BUTTON_Y)       out |= BTN_Y;
    if (v & WPAD_PRO_TRIGGER_L)      out |= BTN_L;
    if (v & WPAD_PRO_TRIGGER_R)      out |= BTN_R;
    if (v & WPAD_PRO_TRIGGER_ZL)     out |= BTN_ZL;
    if (v & WPAD_PRO_TRIGGER_ZR)     out |= BTN_ZR;
    if (v & WPAD_PRO_BUTTON_STICK_L) out |= BTN_L3;
    if (v & WPAD_PRO_BUTTON_STICK_R) out |= BTN_R3;
    if (v & WPAD_PRO_BUTTON_PLUS)    out |= BTN_PLUS;
    if (v & WPAD_PRO_BUTTON_MINUS)   out |= BTN_MINUS;
    if (v & WPAD_PRO_BUTTON_UP)      out |= BTN_UP;
    if (v & WPAD_PRO_BUTTON_DOWN)    out |= BTN_DOWN;
    if (v & WPAD_PRO_BUTTON_LEFT)    out |= BTN_LEFT;
    if (v & WPAD_PRO_BUTTON_RIGHT)   out |= BTN_RIGHT;
    return out;
}

static uint32_t classicToPro(uint32_t v)
{
    uint32_t out = 0;
    if (v & WPAD_CLASSIC_BUTTON_A)     out |= BTN_A;
    if (v & WPAD_CLASSIC_BUTTON_B)     out |= BTN_B;
    if (v & WPAD_CLASSIC_BUTTON_X)     out |= BTN_X;
    if (v & WPAD_CLASSIC_BUTTON_Y)     out |= BTN_Y;
    if (v & WPAD_CLASSIC_BUTTON_L)     out |= BTN_L;
    if (v & WPAD_CLASSIC_BUTTON_R)     out |= BTN_R;
    if (v & WPAD_CLASSIC_BUTTON_ZL)    out |= BTN_ZL;
    if (v & WPAD_CLASSIC_BUTTON_ZR)    out |= BTN_ZR;
    if (v & WPAD_CLASSIC_BUTTON_PLUS)  out |= BTN_PLUS;
    if (v & WPAD_CLASSIC_BUTTON_MINUS) out |= BTN_MINUS;
    if (v & WPAD_CLASSIC_BUTTON_UP)    out |= BTN_UP;
    if (v & WPAD_CLASSIC_BUTTON_DOWN)  out |= BTN_DOWN;
    if (v & WPAD_CLASSIC_BUTTON_LEFT)  out |= BTN_LEFT;
    if (v & WPAD_CLASSIC_BUTTON_RIGHT) out |= BTN_RIGHT;
    return out;
}

static void pushStick(uint32_t held)
{
    if (!s_host)
        return;

    float xy[2] = { 0.0f, 0.0f };
    const bool wanted = Tools::Mss::NextStick(held, &xy[0], &xy[1]);
    const bool gated = s_block || s_drainHeld;
    static bool s_gatedLogged = false;
    if (wanted && !gated) {
        s_host->setStick(s_host, xy);
        if (!s_stickPushed)
            Logger::Log("stick macro on, held=%08X", held);
        s_stickPushed = true;
    } else if (s_stickPushed) {
        s_host->setStick(s_host, nullptr);
        s_stickPushed = false;
        Logger::Log("stick macro off, wanted=%d block=%d drain=%d", (int)wanted,
                    (int)s_block, (int)s_drainHeld);
    } else if (wanted && !s_gatedLogged) {
        s_gatedLogged = true;
        Logger::Log("stick macro wanted but held back: block=%d drain=%d",
                    (int)s_block, (int)s_drainHeld);
    }
    if (!wanted)
        s_gatedLogged = false;
}

void Sample()
{
    if (!s_host)
        return;

    RplPad pad;
    if (s_host->pad(s_host, &pad)) {
        s_accumHeld |= vpadToPro(pad.hold);
        s_accumSources |= SOURCE_GAMEPAD;
        s_sawInput = true;
        s_lx = pad.leftX;
        s_ly = pad.leftY;
        s_rx = pad.rightX;
        s_ry = pad.rightY;
        s_lastTouch = pad.touch;
        s_haveTouch = true;
    }

    for (uint32_t chan = 0; chan < RPL_KPAD_CHANNELS; ++chan) {
        RplKpad kpad;
        if (!s_host->kpad(s_host, chan, &kpad))
            continue;
        if (kpad.extension == WPAD_EXT_PRO_CONTROLLER) {
            s_accumHeld |= proToPro(kpad.hold);
            s_accumSources |= SOURCE_PRO;
        } else if (kpad.extension == WPAD_EXT_CLASSIC ||
                   kpad.extension == WPAD_EXT_MPLUS_CLASSIC) {
            s_accumHeld |= classicToPro(kpad.hold);
            s_accumSources |= SOURCE_CLASSIC;
        } else {
            continue;
        }
        s_sawInput = true;
        s_lx = kpad.leftX;
        s_ly = kpad.leftY;
        s_rx = kpad.rightX;
        s_ry = kpad.rightY;
    }

    detectOverlayHotkeys(s_accumHeld);
    pushBlockState();
    pushStick(s_accumHeld);
}

void BeginFrame()
{
    const uint32_t held = s_accumHeld;

    s_snapshot.sourceMask = s_accumSources;
    s_snapshot.held     = held;
    s_snapshot.pressed  = held & ~s_prevHeld;
    s_snapshot.released = ~held & s_prevHeld;
    s_snapshot.lx = applyDeadzone(s_lx);
    s_snapshot.ly = applyDeadzone(s_ly);
    s_snapshot.rx = applyDeadzone(s_rx);
    s_snapshot.ry = applyDeadzone(s_ry);
    s_snapshot.valid = s_sawInput;

    s_hotkeyFired = s_togglePending;
    s_togglePending = false;
    s_quickAccessFired = s_quickAccessPending;
    s_quickAccessPending = false;

    if (s_drainHeld && held == 0) {
        s_drainHeld = false;
        pushBlockState();
    }

    s_prevHeld = held;

    s_accumHeld = 0;
    s_accumSources = 0;
    s_sawInput = false;
}

bool HotkeyToggled()      { return s_hotkeyFired; }
bool QuickAccessToggled() { return s_quickAccessFired; }

static bool cancelHasLocalTarget(ImGuiContext* context)
{
    if (!context)
        return false;
    if (context->ActiveId != 0 || context->NavLayer != ImGuiNavLayer_Main)
        return true;
    ImGuiWindow* nav = context->NavWindow;
    if (nav && nav != nav->RootWindow &&
        !(nav->Flags & ImGuiWindowFlags_Popup) && nav->ParentWindow) {
        return true;
    }
    return context->OpenPopupStack.Size > 0 &&
           context->OpenPopupStack.back().Window &&
           !(context->OpenPopupStack.back().Window->Flags & ImGuiWindowFlags_Modal);
}

static bool calibratedTouch(VPADTouchData* out)
{
    if (!s_haveTouch)
        return false;
    VPADTouchData raw;
    memcpy(&raw, &s_lastTouch, sizeof(raw));
    VPADGetTPCalibratedPoint(VPAD_CHAN_0, out, &raw);
    return out->touched != 0;
}

void FeedMenu(ImGuiIO& io, float displayWidth, float displayHeight,
              bool menuActive, uint32_t flags)
{
    uint32_t held = s_snapshot.held;
    if (!menuActive || s_drainHeld || (flags & FEED_NO_BUTTONS))
        held = 0;
    if (flags & FEED_NO_HORIZONTAL)
        held &= ~(uint32_t)(BTN_LEFT | BTN_RIGHT);

    ImGuiContext* context = ImGui::GetCurrentContext();
    const bool windowing = context && context->NavWindowingTarget != nullptr;
    const bool up = (held & BTN_UP) != 0;
    const bool down = (held & BTN_DOWN) != 0;
    const bool activate = (held & BTN_A) != 0;
    const bool cancel = (held & BTN_B) != 0 && cancelHasLocalTarget(context);

    io.AddKeyEvent(ImGuiKey_GamepadDpadUp, !windowing && up);
    io.AddKeyEvent(ImGuiKey_GamepadDpadDown, !windowing && down);
    io.AddKeyEvent(ImGuiKey_GamepadDpadLeft, (held & BTN_LEFT) != 0);
    io.AddKeyEvent(ImGuiKey_GamepadDpadRight, (held & BTN_RIGHT) != 0);
    io.AddKeyEvent(ImGuiKey_GamepadFaceDown, activate);
    io.AddKeyEvent(ImGuiKey_GamepadFaceRight, cancel);
    io.AddKeyEvent(ImGuiKey_GamepadFaceLeft, (held & BTN_X) != 0);
    io.AddKeyEvent(ImGuiKey_GamepadFaceUp, activate);
    io.AddKeyEvent(ImGuiKey_GamepadL1, windowing && up);
    io.AddKeyEvent(ImGuiKey_GamepadR1, windowing && down);

    if (!menuActive || (flags & FEED_NO_TOUCH)) {
        if (s_wasTouched)
            io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        s_wasTouched = false;
        return;
    }

    VPADTouchData touch = {};
    const bool touched = calibratedTouch(&touch);
    if (touched) {
        const float scaleX = displayWidth / 1280.0f;
        const float scaleY = displayHeight / 720.0f;
        io.AddMousePosEvent((float)touch.x * scaleX, (float)touch.y * scaleY);
    }
    if (s_haveTouch && touched != s_wasTouched) {
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, touched);
        s_wasTouched = touched;
    }
}

static const ButtonLabel kButtonLabels[] = {
    { BTN_ZL, "ZL" }, { BTN_ZR, "ZR" }, { BTN_L, "L" }, { BTN_R, "R" },
    { BTN_L3, "L3" }, { BTN_R3, "R3" }, { BTN_A, "A" }, { BTN_B, "B" },
    { BTN_X, "X" }, { BTN_Y, "Y" }, { BTN_PLUS, "Plus" }, { BTN_MINUS, "Minus" },
    { BTN_UP, "Up" }, { BTN_DOWN, "Down" },
    { BTN_LEFT, "Left" }, { BTN_RIGHT, "Right" },
};
static const int kButtonLabelCount =
    (int)(sizeof(kButtonLabels) / sizeof(kButtonLabels[0]));

const ButtonLabel* GetButtonLabels(int* count)
{
    if (count)
        *count = kButtonLabelCount;
    return kButtonLabels;
}

// Read by OnPadSampled on the input path, VPAD and KPAD folded together.
bool PeekLive(uint32_t* held, float* touchX, float* touchY)
{
    if (!s_host)
        return false;

    RplPad pad;
    const bool havePad = s_host->pad(s_host, &pad) != 0;
    uint32_t buttons = havePad ? vpadToPro(pad.hold) : 0u;
    bool any = havePad;
    for (uint32_t chan = 0; chan < RPL_KPAD_CHANNELS; ++chan) {
        RplKpad kpad;
        if (!s_host->kpad(s_host, chan, &kpad))
            continue;
        if (kpad.extension == WPAD_EXT_PRO_CONTROLLER)
            buttons |= proToPro(kpad.hold);
        else if (kpad.extension == WPAD_EXT_CLASSIC ||
                 kpad.extension == WPAD_EXT_MPLUS_CLASSIC)
            buttons |= classicToPro(kpad.hold);
        else
            continue;
        any = true;
    }
    if (!any)
        return false;

    if (held)
        *held = buttons;

    if (touchX && touchY) {
        *touchX = -1.0f;
        *touchY = -1.0f;
        if (havePad) {
            VPADTouchData raw, point;
            memcpy(&raw, &pad.touch, sizeof(raw));
            VPADGetTPCalibratedPoint(VPAD_CHAN_0, &point, &raw);
            if (point.touched) {
                *touchX = (float)point.x / 1280.0f;
                *touchY = (float)point.y / 720.0f;
            }
        }
    }
    return true;
}

bool GetTouchPoint(float* outX, float* outY)
{
    VPADTouchData touch = {};
    if (!calibratedTouch(&touch))
        return false;
    if (outX) *outX = (float)touch.x / 1280.0f;
    if (outY) *outY = (float)touch.y / 720.0f;
    return true;
}

const char* DescribeCombo(uint32_t buttons)
{
    static char buf[96];
    buf[0] = '\0';
    for (int i = 0; i < kButtonLabelCount; ++i) {
        if (!(buttons & kButtonLabels[i].bit))
            continue;
        if (buf[0])
            strncat(buf, "+", sizeof(buf) - strlen(buf) - 1);
        strncat(buf, kButtonLabels[i].name, sizeof(buf) - strlen(buf) - 1);
    }
    if (!buf[0])
        snprintf(buf, sizeof(buf), "(unbound)");
    return buf;
}
}
