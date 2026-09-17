#include "core/logger.h"

#include <rplloader/rplloader.h>

#include <coreinit/debug.h>

#include <stdarg.h>
#include <stdio.h>

namespace Logger {
static const RplHost* s_host = nullptr;

void SetHost(const RplHost* host)
{
    s_host = host;
}

static int hostLevel(Level level)
{
    switch (level) {
    case LL_ERROR: return RPL_LOG_ERROR;
    case LL_WARN:  return RPL_LOG_WARN;
    case LL_INFO:
    default:       return RPL_LOG_INFO;
    }
}

static void emit(Level level, const char* fmt, va_list args)
{
    char line[512];
    vsnprintf(line, sizeof(line), fmt, args);
    if (s_host)
        s_host->log(s_host, hostLevel(level), "%s", line);
    else
        OSReport("%s\n", line);
}

void Init() {}
void OnApplicationStart() {}
void StartNewLog() {}

void LogAt(Level level, const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    emit(level, fmt, args);
    va_end(args);
}

void Log(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    emit(LL_INFO, fmt, args);
    va_end(args);
}

void LogWarn(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    emit(LL_WARN, fmt, args);
    va_end(args);
}

void LogError(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    emit(LL_ERROR, fmt, args);
    va_end(args);
}

#ifdef WWHD_TOOLS_DEBUG
void Breadcrumb(const char* fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    emit(LL_INFO, fmt, args);
    va_end(args);
}
#endif
}
