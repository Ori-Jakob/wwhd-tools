#pragma once

#include "ui/window_state.h"

#include <stdint.h>

namespace Hud {
namespace GameInfo {
enum VisibleRow : uint32_t {
    ROW_TIME_OF_DAY     = 1u << 0,
    ROW_CURRENT_SESSION = 1u << 1,
    ROW_ANGLE           = 1u << 2,
    ROW_Y_ANGLE         = 1u << 3,
    ROW_SPEED           = 1u << 4,
    ROW_X               = 1u << 5,
    ROW_Y               = 1u << 6,
    ROW_Z               = 1u << 7,
    ROW_ACTION          = 1u << 8,
    ROW_FRAME           = 1u << 9,
    ROW_DATE            = 1u << 10,
    ROW_CLOCK           = 1u << 11,
    ROW_REGION          = 1u << 12,
    ROW_DEMO            = 1u << 13,
    ROW_POTENTIAL_SPEED = 1u << 14,
    ROW_SPEED_ANGLE     = 1u << 15,
    ROW_STORAGE         = 1u << 16,
};

static const unsigned ROW_COUNT = 17u;
static const uint32_t ALL_VISIBLE_ROWS = (1u << ROW_COUNT) - 1u;
static const uint32_t DEFAULT_VISIBLE_ROWS =
    ALL_VISIBLE_ROWS & ~(ROW_DATE | ROW_CLOCK);

static const float MIN_WIDTH = 130.0f;
static const float MAX_WIDTH = 360.0f;

void DrawSettingsButton();
void DrawSettingsWindow();
void DrawWindow(bool menuActive);

Ui::WindowState& State();
void ApplyState();

uint32_t GetVisibleRows();

void SetVisibleRows(uint32_t rows);

bool GetShowProcNames();
void SetShowProcNames(bool show);

bool GetBoatValues();
void SetBoatValues(bool show);

void GetRowOrder(uint32_t* rows, unsigned count);
bool SetRowOrder(const uint32_t* rows, unsigned count);
void ResetRowOrder();

void ResetToDefaults();
void OnApplicationStart();
}
}
