#pragma once

#include <stdint.h>

#include <rplloader/rplloader.h>

struct ImGuiIO;

namespace Input {
enum Button {
    BTN_A     = 1u << 0,
    BTN_B     = 1u << 1,
    BTN_X     = 1u << 2,
    BTN_Y     = 1u << 3,
    BTN_L     = 1u << 4,
    BTN_R     = 1u << 5,
    BTN_ZL    = 1u << 6,
    BTN_ZR    = 1u << 7,
    BTN_L3    = 1u << 8,
    BTN_R3    = 1u << 9,
    BTN_PLUS  = 1u << 10,
    BTN_MINUS = 1u << 11,
    BTN_UP    = 1u << 12,
    BTN_DOWN  = 1u << 13,
    BTN_LEFT  = 1u << 14,
    BTN_RIGHT = 1u << 15,
};

enum Source {
    SOURCE_GAMEPAD = 1u << 0,
    SOURCE_PRO     = 1u << 1,
    SOURCE_CLASSIC = 1u << 2,
};

struct Snapshot {
    uint32_t sourceMask;
    uint32_t held;
    uint32_t pressed;
    uint32_t released;
    float    lx, ly;
    float    rx, ry;
    bool     valid;
};

void SetHost(const RplHost* host);
void Sample();

void BeginFrame();
bool HotkeyToggled();
bool QuickAccessToggled();

enum FeedFlags {
    FEED_NO_HORIZONTAL = 1u << 0,
    FEED_NO_TOUCH      = 1u << 1,
    FEED_NO_BUTTONS    = 1u << 2,
};
void FeedMenu(ImGuiIO& io, float displayWidth, float displayHeight,
              bool menuActive, uint32_t flags = 0);

const Snapshot& Current();

bool GetTouchPoint(float* outX, float* outY);

bool PeekLive(uint32_t* held, float* touchX, float* touchY);

void SetBlockGameInput(bool block);
bool IsBlockingGameInput();

void DrainHeld();

void OnApplicationStart();
void OnApplicationEnd();

const char* DescribeCombo(uint32_t buttons);

struct ButtonLabel {
    uint32_t    bit;
    const char* name;
};
const ButtonLabel* GetButtonLabels(int* count);
}
