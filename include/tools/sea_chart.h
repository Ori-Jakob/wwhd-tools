#pragma once

#include <stdint.h>

#include "libwwhd/libwwhd.h"

namespace Tools {
namespace SeaChart {
static const int   GRID      = 7;
static const float SQUARE    = 100000.0f;
static const float SEA_HALF  = 350000.0f;

enum View { VIEW_CHART = 0, VIEW_SQUARE };

struct State {
    View  view;
    int   col, row;
    float cu, cv;
    s16   facing;
    bool  focusMap;
};

State& Ui();

void NoteWindow(bool windowFocused, bool mapFocused);

enum Action {
    ACTION_TELEPORT_LINK = 0,
    ACTION_TELEPORT_BOAT,
    ACTION_TELEPORT_BOTH,
    ACTION_LINK_TO_BOAT,
    ACTION_BOAT_TO_LINK,
    ACTION_COUNT
};

const char* Blocker(Action action);
bool        Run(Action action);

bool OnGreatSea();
bool IsBusy();

bool LinkOnChart(float* u, float* v, s16* facing);
bool BoatOnChart(float* u, float* v, s16* facing);

void CursorWorld(float* x, float* z);
int  RoomAt(float x, float z);
void SquareName(int col, int row, char* out, int cap);

void ZoomIn();
void ZoomOut();

void Tick(bool menuOpen);
void OnApplicationStart();
}
}
