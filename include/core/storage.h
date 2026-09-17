#pragma once

#include <stdint.h>

namespace Storage {
enum ReadResult {
    READ_OK = 0,
    READ_MISSING,
    READ_RETRY,
    READ_ERROR,
};

bool EnsureReady();

ReadResult LoadConfig(char** outText, uint32_t maxBytes);
bool SaveConfig(const char* text);
bool PrepareLog();
bool AppendLog(const char* text, uint32_t size);

const char* RootPath();

static const int STATE_NAME_MAX = 32;

struct StateEntry {
    char name[STATE_NAME_MAX];
};

bool       WriteStateFile(const char* name, const void* data, uint32_t size);
ReadResult ReadStateFile(const char* name, void* out, uint32_t capacity,
                         uint32_t* outSize);
bool       DeleteStateFile(const char* name);

int        ListStateFiles(StateEntry* out, int maxEntries);

static const int SAVE_PATH_MAX = 128;

struct SavePathEntry {
    char path[SAVE_PATH_MAX];
};

int        ListSaveFiles(SavePathEntry* out, int maxEntries);
ReadResult ReadSaveFile(const char* relPath, void* out, uint32_t capacity,
                        uint32_t* outSize);
}
