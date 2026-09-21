#pragma once

#include <stdint.h>

#include "libwwhd/libwwhd.h"

namespace Tools {
namespace Coordinates {
static const int SLOT_COUNT = 10;
static const int NAME_MAX   = 24;

struct Slot {
    bool  valid;
    char  name[NAME_MAX];
    char  stage[WWHD_STAGE_NAME_MAX];
    s8    room;
    s8    layer;
    cXyz  pos;
    s16   angle;
    bool  hasCamera;
    cXyz  camEye;
    cXyz  camCenter;
};

bool SaveSlot(int index);
bool LoadSlot(int index);
void ClearSlot(int index);

int  Selected();
void SetSelected(int index);

const Slot& At(int index);
void        Set(int index, const Slot& slot);

bool GoToInStage(s8 room, const cXyz& pos, bool setFacing, s16 angle,
                 const char* label);

void SetCamera(const cXyz& eye, const cXyz& center);

bool        IsBusy();
const char* Status();

void Describe(int index, char* out, int cap);

void Tick(bool acceptInput);
void OnCameraRun(void* camera);
void OnApplicationStart();
}
}
