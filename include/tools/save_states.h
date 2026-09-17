#pragma once

#include <stdint.h>

#include "core/storage.h"

struct dSv_info_c;

namespace Tools {
namespace SaveStates {
static const int kNameMax = 32;
static const int kListMax = 64;

static const bool kHidden = true;

bool Save(const char* name);

bool Load(const char* name);

bool Delete(const char* name);

bool        IsBusy();
const char* Status();

void        RefreshList();
int         Count();
const char* NameAt(int index);

const char* Selected();
void        SetSelected(const char* name);

typedef Storage::ReadResult (*ReadFn)(const char* name, void* out,
                                      uint32_t capacity, uint32_t* outSize);
bool BeginRead(ReadFn fn, const char* name, void* out, uint32_t capacity);

bool ReadFinished(bool* ok, uint32_t* size);

bool LoadBlock(const dSv_info_c* block, const char* stage, int16_t point,
               int8_t room, int8_t layer, const int8_t* zoneNo, const char* label);

void Tick(bool acceptInput);

void OnApplicationStart();
void OnApplicationEnd();
}
}
