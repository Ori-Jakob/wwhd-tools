#include "core/storage.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

namespace Storage {
static const char kRoot[] = "fs:/vol/external01/wwhd_tools";
static const char kConfigPath[] = "fs:/vol/external01/wwhd_tools/config.json";
static const char kLogPath[] = "fs:/vol/external01/wwhd_tools/log.txt";
static const char kLogOldPath[] = "fs:/vol/external01/wwhd_tools/log.txt.old";
static const char kStateDir[] = "fs:/vol/external01/wwhd_tools/savestates";
static const char kStateExt[] = ".bin";
static const char kSaveDir[] = "fs:/vol/external01/wwhd_tools/saves";
static const int  kSaveScanDepth = 6;

static bool s_ready = false;

const char* RootPath()
{
    return kRoot;
}

bool EnsureReady()
{
    if (s_ready)
        return true;

    struct stat st;
    if (stat(kRoot, &st) == 0) {
        s_ready = true;
        return true;
    }
    if (mkdir(kRoot, 0777) == 0) {
        s_ready = true;
        return true;
    }
    return stat(kRoot, &st) == 0 && (s_ready = true);
}

ReadResult LoadConfig(char** outText, uint32_t maxBytes)
{
    if (!outText)
        return READ_ERROR;
    *outText = nullptr;

    if (!EnsureReady())
        return READ_RETRY;

    FILE* f = fopen(kConfigPath, "rb");
    if (!f)
        return READ_MISSING;

    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return READ_ERROR;
    }
    long size = ftell(f);
    if (size < 0 || (uint32_t)size > maxBytes) {
        fclose(f);
        return READ_ERROR;
    }
    rewind(f);

    char* text = static_cast<char*>(malloc((size_t)size + 1));
    if (!text) {
        fclose(f);
        return READ_ERROR;
    }

    size_t read = fread(text, 1, (size_t)size, f);
    fclose(f);
    text[read] = '\0';
    *outText = text;
    return READ_OK;
}

bool SaveConfig(const char* text)
{
    if (!text || !EnsureReady())
        return false;

    FILE* f = fopen(kConfigPath, "wb");
    if (!f)
        return false;

    const size_t len = strlen(text);
    const bool ok = fwrite(text, 1, len, f) == len;
    fclose(f);
    return ok;
}

bool PrepareLog()
{
    if (!EnsureReady())
        return false;

    remove(kLogOldPath);
    rename(kLogPath, kLogOldPath);

    FILE* f = fopen(kLogPath, "wb");
    if (!f)
        return false;
    fclose(f);
    return true;
}

bool AppendLog(const char* text, uint32_t size)
{
    if (!text || !s_ready)
        return false;

    FILE* f = fopen(kLogPath, "ab");
    if (!f)
        return false;

    const bool ok = fwrite(text, 1, size, f) == size;
    fflush(f);
    fclose(f);
    return ok;
}

static bool validStateName(const char* name)
{
    if (!name || !name[0])
        return false;
    int n = 0;
    for (const char* p = name; *p; ++p, ++n) {
        const char c = *p;
        const bool ok = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
                        (c >= '0' && c <= '9') || c == '-' || c == '_';
        if (!ok || n >= STATE_NAME_MAX - 1)
            return false;
    }
    return true;
}

static bool statePath(const char* name, char* out, size_t cap)
{
    if (!validStateName(name))
        return false;
    const int n = snprintf(out, cap, "%s/%s%s", kStateDir, name, kStateExt);
    return n > 0 && (size_t)n < cap;
}

static bool ensureStateDir()
{
    if (!EnsureReady())
        return false;
    struct stat st;
    if (stat(kStateDir, &st) == 0)
        return true;
    return mkdir(kStateDir, 0777) == 0 || stat(kStateDir, &st) == 0;
}

bool WriteStateFile(const char* name, const void* data, uint32_t size)
{
    char path[128];
    if (!data || !size || !statePath(name, path, sizeof(path)) || !ensureStateDir())
        return false;

    FILE* f = fopen(path, "wb");
    if (!f)
        return false;
    const bool ok = fwrite(data, 1, size, f) == size;
    fclose(f);
    return ok;
}

ReadResult ReadStateFile(const char* name, void* out, uint32_t capacity,
                         uint32_t* outSize)
{
    if (outSize)
        *outSize = 0;
    char path[128];
    if (!out || !capacity || !outSize || !statePath(name, path, sizeof(path)))
        return READ_ERROR;
    if (!EnsureReady())
        return READ_RETRY;

    FILE* f = fopen(path, "rb");
    if (!f)
        return READ_MISSING;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return READ_ERROR;
    }
    const long size = ftell(f);
    if (size <= 0 || (uint32_t)size > capacity) {
        fclose(f);
        return READ_ERROR;
    }
    rewind(f);
    const size_t read = fread(out, 1, (size_t)size, f);
    fclose(f);
    if (read != (size_t)size)
        return READ_ERROR;
    *outSize = (uint32_t)read;
    return READ_OK;
}

bool DeleteStateFile(const char* name)
{
    char path[128];
    if (!statePath(name, path, sizeof(path)))
        return false;
    return remove(path) == 0;
}

static bool endsWithSav(const char* name)
{
    const size_t len = strlen(name);
    if (len < 5)
        return false;
    const char* e = name + len - 4;
    return e[0] == '.' && (e[1] == 's' || e[1] == 'S') &&
           (e[2] == 'a' || e[2] == 'A') && (e[3] == 'v' || e[3] == 'V');
}

static bool validSavePath(const char* rel)
{
    if (!rel || !rel[0] || rel[0] == '/' || strlen(rel) >= (size_t)SAVE_PATH_MAX)
        return false;
    for (const char* p = rel; *p; ++p) {
        const unsigned char c = (unsigned char)*p;
        if (c == '\\' || c < 0x20 || c > 0x7E)
            return false;
        if (p[0] == '.' && p[1] == '.')
            return false;
    }
    return true;
}

struct SaveScan {
    SavePathEntry* out;
    int            max;
    int            count;
};

static void insertSave(SaveScan* s, const char* rel)
{
    int at = s->count;
    while (at > 0 && strcmp(s->out[at - 1].path, rel) > 0)
        --at;
    if (at >= s->max)
        return;
    if (s->count < s->max)
        ++s->count;
    for (int i = s->count - 1; i > at; --i)
        s->out[i] = s->out[i - 1];
    snprintf(s->out[at].path, SAVE_PATH_MAX, "%s", rel);
}

static void scanSaveDir(const char* absDir, const char* relDir, SaveScan* s, int depth)
{
    DIR* dir = opendir(absDir);
    if (!dir)
        return;
    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        char abs[256];
        int n = snprintf(abs, sizeof(abs), "%s/%s", absDir, ent->d_name);
        if (n <= 0 || n >= (int)sizeof(abs))
            continue;
        char rel[SAVE_PATH_MAX];
        n = relDir[0] ? snprintf(rel, sizeof(rel), "%s/%s", relDir, ent->d_name)
                      : snprintf(rel, sizeof(rel), "%s", ent->d_name);
        if (n <= 0 || n >= (int)sizeof(rel))
            continue;
        struct stat st;
        if (stat(abs, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode)) {
            if (depth < kSaveScanDepth)
                scanSaveDir(abs, rel, s, depth + 1);
        } else if (S_ISREG(st.st_mode) && endsWithSav(ent->d_name) && validSavePath(rel)) {
            insertSave(s, rel);
        }
    }
    closedir(dir);
}

int ListSaveFiles(SavePathEntry* out, int maxEntries)
{
    if (!out || maxEntries <= 0 || !EnsureReady())
        return 0;

    struct stat st;
    if (stat(kSaveDir, &st) != 0)
        mkdir(kSaveDir, 0777);
    SaveScan scan = { out, maxEntries, 0 };
    scanSaveDir(kSaveDir, "", &scan, 0);
    return scan.count;
}

ReadResult ReadSaveFile(const char* relPath, void* out, uint32_t capacity,
                        uint32_t* outSize)
{
    if (outSize)
        *outSize = 0;
    if (!out || !capacity || !outSize || !validSavePath(relPath))
        return READ_ERROR;
    if (!EnsureReady())
        return READ_RETRY;

    char path[256];
    const int n = snprintf(path, sizeof(path), "%s/%s", kSaveDir, relPath);
    if (n <= 0 || n >= (int)sizeof(path))
        return READ_ERROR;

    FILE* f = fopen(path, "rb");
    if (!f)
        return READ_MISSING;
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return READ_ERROR;
    }
    const long size = ftell(f);
    if (size <= 0 || (uint32_t)size > capacity) {
        fclose(f);
        return READ_ERROR;
    }
    rewind(f);
    const size_t read = fread(out, 1, (size_t)size, f);
    fclose(f);
    if (read != (size_t)size)
        return READ_ERROR;
    *outSize = (uint32_t)read;
    return READ_OK;
}

int ListStateFiles(StateEntry* out, int maxEntries)
{
    if (!out || maxEntries <= 0 || !EnsureReady())
        return 0;

    DIR* dir = opendir(kStateDir);
    if (!dir)
        return 0;

    const size_t extLen = strlen(kStateExt);
    int count = 0;
    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        const char* file = ent->d_name;
        const size_t len = strlen(file);
        if (len <= extLen || strcmp(file + len - extLen, kStateExt) != 0)
            continue;
        const size_t stemLen = len - extLen;
        if (stemLen >= (size_t)STATE_NAME_MAX)
            continue;

        char stem[STATE_NAME_MAX];
        memcpy(stem, file, stemLen);
        stem[stemLen] = '\0';
        if (!validStateName(stem))
            continue;

        int at = count;
        while (at > 0 && strcmp(out[at - 1].name, stem) > 0)
            --at;
        if (at >= maxEntries)
            continue;
        if (count < maxEntries)
            ++count;
        for (int i = count - 1; i > at; --i)
            out[i] = out[i - 1];
        memcpy(out[at].name, stem, stemLen + 1);
    }
    closedir(dir);
    return count;
}
}
