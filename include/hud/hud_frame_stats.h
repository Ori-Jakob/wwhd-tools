#pragma once

#include "ui/window_state.h"

#include <stdint.h>

namespace Hud {
namespace FrameStats {
enum VisibleRow : uint32_t {
    ROW_FPS     = 1u << 0,
    ROW_FRAME   = 1u << 1,
    ROW_AVG     = 1u << 2,
    ROW_P50     = 1u << 3,
    ROW_P95     = 1u << 4,
    ROW_P99     = 1u << 5,
    ROW_LOW1    = 1u << 6,
    ROW_MAX     = 1u << 7,
    ROW_CPU     = 1u << 8,
    ROW_GPU     = 1u << 9,
    ROW_OVERLAY = 1u << 10,
    ROW_DRAWS   = 1u << 11,
    ROW_TRIS    = 1u << 12,
    ROW_GRAPH   = 1u << 13,
    ROW_WAIT    = 1u << 14,
    ROW_GPU_OVL = 1u << 15,
};

static const unsigned ROW_COUNT = 16u;
static const uint32_t ALL_VISIBLE_ROWS = (1u << ROW_COUNT) - 1u;
static const uint32_t DEFAULT_VISIBLE_ROWS =
    ROW_FPS | ROW_FRAME | ROW_P99 | ROW_LOW1 | ROW_CPU | ROW_GPU | ROW_WAIT |
    ROW_DRAWS | ROW_GRAPH;

enum GraphSeries : uint32_t {
    GRAPH_FRAME = 1u << 0,
    GRAPH_CPU   = 1u << 1,
    GRAPH_GPU   = 1u << 2,
};
static const uint32_t ALL_GRAPH_SERIES = GRAPH_FRAME | GRAPH_CPU | GRAPH_GPU;
static const uint32_t DEFAULT_GRAPH_SERIES = ALL_GRAPH_SERIES;

static const float MIN_WIDTH = 130.0f;
static const float MAX_WIDTH = 420.0f;

void DrawSettingsButton();
void DrawSettingsWindow();
void DrawWindow(bool menuActive);

Ui::WindowState& State();
void ApplyState();

uint32_t GetVisibleRows();
void SetVisibleRows(uint32_t rows);

uint32_t GetGraphSeries();
void SetGraphSeries(uint32_t series);

void GetRowOrder(uint32_t* rows, unsigned count);
bool SetRowOrder(const uint32_t* rows, unsigned count);
void ResetRowOrder();

void ResetToDefaults();
}
}
