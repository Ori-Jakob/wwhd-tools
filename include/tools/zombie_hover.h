#pragma once

#include <stdint.h>

namespace Tools {
namespace ZombieHover {
enum Grade : uint8_t {
    GRADE_FIRST = 0,
    GRADE_PERFECT,
    GRADE_GOOD,
    GRADE_OK,
    GRADE_BAD,
    // Not graded: B held over from the last press, or a press with no attack.
    GRADE_HELD,
    GRADE_WASTED,
    GRADE_COUNT
};

enum EndReason : uint8_t {
    END_NONE = 0,
    END_GROUND,
    END_WATER,
    END_SHIP,
    END_DIED,
    END_HEALED,
    END_LOST,
    END_RESET,
};

static const int GAP_PERFECT = 2;
static const int GAP_GOOD    = 3;
static const int GAP_OK      = 4;

struct Input {
    uint16_t frame;
    uint8_t  gap;
    uint8_t  grade;
};

static const int MAX_INPUTS = 1024;
static const int HISTORY    = 5;

struct Run {
    bool     valid;
    bool     healed;
    uint8_t  endReason;
    uint32_t frames;
    uint32_t inputs;
    uint32_t presses;
    uint32_t counts[GRADE_COUNT];
    uint32_t gapSum;
    uint32_t maxGap;
    uint32_t perfectStreak;
    float    startY;
    float    gainY;
    float    maxGainY;
    uint32_t recorded;
    Input    samples[MAX_INPUTS];
};

bool IsEnabled();
void SetEnabled(bool enabled);

bool IsHovering();

const Run& Current();

int        HistoryCount();
const Run& HistoryAt(int index);

void ResetStats();
void ClearHistory();

Grade       GradeForGap(uint32_t gap);
const char* GradeName(uint8_t grade);
const char* EndReasonName(uint8_t reason);
float       HeightPerAttack(uint32_t gap);

bool SetupPractice();
bool RestorePractice();
bool CanRestorePractice();

bool NextButtons(uint32_t held, uint32_t* vpadMask);
bool IsSimulating();

void Tick(bool acceptInput);
void OnApplicationStart();
}
}
