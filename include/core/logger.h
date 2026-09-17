#pragma once

struct RplHost;

namespace Logger {
void SetHost(const RplHost* host);

enum Level {
    LL_INFO  = 0,
    LL_WARN  = 1,
    LL_ERROR = 2,
};

void Init();

void OnApplicationStart();

void StartNewLog();

void LogAt(Level level, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

void Log(const char* fmt, ...)      __attribute__((format(printf, 1, 2)));
void LogWarn(const char* fmt, ...)  __attribute__((format(printf, 1, 2)));
void LogError(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

#ifdef WWHD_TOOLS_DEBUG
void Breadcrumb(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
#endif
}

#ifdef WWHD_TOOLS_DEBUG

#define WWHD_BREADCRUMB(...) Logger::Breadcrumb(__VA_ARGS__)

#define WWHD_BREADCRUMB_EVERY(n, ...)                                       \
    do {                                                                    \
        static unsigned wwhdBreadcrumbTick_ = 0;                            \
        const unsigned wwhdBreadcrumbEvery_ = (unsigned)(n);                \
        if (wwhdBreadcrumbEvery_ == 0 ||                                    \
            (wwhdBreadcrumbTick_++ % wwhdBreadcrumbEvery_) == 0)            \
            Logger::Log(__VA_ARGS__);                                       \
    } while (0)

#define WWHD_BREADCRUMB_ONCE(...)                                           \
    do {                                                                    \
        static bool wwhdBreadcrumbSeen_ = false;                            \
        if (!wwhdBreadcrumbSeen_) {                                         \
            wwhdBreadcrumbSeen_ = true;                                     \
            Logger::Log(__VA_ARGS__);                                       \
        }                                                                   \
    } while (0)

#else

#define WWHD_BREADCRUMB(...)             ((void)0)
#define WWHD_BREADCRUMB_EVERY(n, ...)    ((void)0)
#define WWHD_BREADCRUMB_ONCE(...)        ((void)0)

#endif
