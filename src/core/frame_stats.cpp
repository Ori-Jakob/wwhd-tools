#include "core/frame_stats.h"

#include "core/logger.h"

#include <coreinit/cache.h>
#include <coreinit/time.h>
#include <gx2/enum.h>

#include <algorithm>
#include <malloc.h>
#include <string.h>

extern "C" void GX2SampleTopGPUCycle(uint64_t* result);
extern "C" void GX2SampleBottomGPUCycle(uint64_t* result);

namespace FrameStats {
static const int   kGpuSlots     = 8;
static const int   kGpuMinLag    = 2;
static const int   kGpuMaxLag    = 6;
static const int   kGpuGiveUp    = 240;
static const int   kGpuLogs      = 6;
static const int   kSummaryEvery = 10;
static const float kMaxMs        = 5000.0f;
static const int   kCalibrateSeconds = 2;
static const uint32_t kLogEvery  = 1800;

struct GpuSlot {
    uint64_t top;
    uint64_t bottom;
    uint64_t overlayTop;
    uint64_t overlayBottom;
};

static GpuSlot* s_gpu;
static uint32_t s_slotFrame[kGpuSlots];
static OSTime   s_slotTime[kGpuSlots];
static bool     s_slotArmed[kGpuSlots];

static int      s_gpuDropped;
static int      s_gpuValid;
static int      s_gpuLogged;
static bool     s_gpuOff;
static uint64_t s_lastTop, s_lastBottom;
static bool     s_calLogged;

static const uint64_t kNotASample = ~(uint64_t)0;

static Frame    s_frames[HISTORY];
static int      s_head, s_count;
static Summary  s_summary;
static float    s_sorted[HISTORY];

static bool     s_ignore;
static OSTime   s_lastPresent;

static OSTime   s_prevPresent;
static int      s_prevTarget = -1;
static float    s_afterMs[2];
static OSTime   s_lastSeen[2];
static int      s_boundary;
static int      s_sinceChoose;

static OSTime   s_execStart;
static float    s_execMs = -1.0f;
static uint64_t s_overlayTicks;
static uint32_t s_draws, s_tris;
static bool     s_topSampled;
static bool     s_overlayTopSampled;
static bool     s_overlayClosed;
static int      s_overlaySlot;
static bool     s_drawsSeen;
static uint32_t s_frameIndex;
static int      s_sinceSummary;

static uint64_t s_calBase;
static OSTime   s_calTime;
static bool     s_calHave;
static double   s_cyclesPerTick;

static float msOf(uint64_t ticks)
{
    return (float)((double)ticks * 1000.0 / (double)OSTimerClockSpeed);
}

static Frame& frameAt(int age)
{
    return s_frames[(s_head - 1 - age + 2 * HISTORY) % HISTORY];
}

static void clearSummary()
{
    memset(&s_summary, 0, sizeof(s_summary));
    s_summary.cpuMs = s_summary.gpuMs = s_summary.waitMs = s_summary.overlayMs = -1.0f;
    s_summary.gpuOverlayMs = -1.0f;
    s_summary.gpuState = GPU_WAITING;
}

static void clearSlot(int i)
{
    memset(&s_gpu[i], 0, sizeof(GpuSlot));
    DCFlushRange(&s_gpu[i], sizeof(GpuSlot));
    s_slotArmed[i] = false;
}

void OnApplicationStart()
{
    if (!s_gpu) {
        s_gpu = (GpuSlot*)memalign(32, sizeof(GpuSlot) * kGpuSlots);
        if (s_gpu)
            Logger::Log("frame stats: GPU sample buffer at %p (%u bytes)", (void*)s_gpu,
                        (unsigned)(sizeof(GpuSlot) * kGpuSlots));
        else
            Logger::LogWarn("frame stats: no memory for the GPU sample buffer; GPU time off");
    }
    if (s_gpu) {
        memset(s_gpu, 0, sizeof(GpuSlot) * kGpuSlots);
        DCFlushRange(s_gpu, sizeof(GpuSlot) * kGpuSlots);
    }
    memset(s_slotFrame, 0, sizeof(s_slotFrame));
    memset(s_slotTime, 0, sizeof(s_slotTime));
    memset(s_slotArmed, 0, sizeof(s_slotArmed));
    s_gpuDropped = s_gpuValid = s_gpuLogged = 0;
    s_calLogged = false;
    s_gpuOff = s_gpu == nullptr;
    s_lastTop = s_lastBottom = 0;

    memset(s_frames, 0, sizeof(s_frames));
    s_head = s_count = 0;
    clearSummary();
    s_ignore = false;
    s_lastPresent = 0;
    s_prevPresent = 0;
    s_prevTarget = -1;
    s_afterMs[0] = s_afterMs[1] = 0.0f;
    s_lastSeen[0] = s_lastSeen[1] = 0;
    s_boundary = 0;
    s_sinceChoose = 0;
    s_execStart = 0;
    s_execMs = -1.0f;
    s_overlayTicks = 0;
    s_draws = s_tris = 0;
    s_topSampled = false;
    s_overlayTopSampled = false;
    s_overlayClosed = false;
    s_overlaySlot = 0;
    s_drawsSeen = false;
    s_frameIndex = 0;
    s_sinceSummary = 0;
    s_calHave = false;
    s_cyclesPerTick = 0.0;
}

void SetIgnore(bool ignore) { s_ignore = ignore; }

void OnPresentationFrame()
{
    s_overlaySlot = (int)(s_frameIndex % kGpuSlots);
    s_overlayTopSampled = false;
    s_overlayClosed = false;
}

void OnExecuteBegin()
{
    s_execStart = OSGetSystemTime();
}

void OnExecuteEnd()
{
    if (!s_execStart)
        return;
    const float ms = msOf((uint64_t)(OSGetSystemTime() - s_execStart));
    s_execMs = s_execMs < 0.0f ? ms : s_execMs + ms;
    s_execStart = 0;
}

static uint32_t primitives(uint32_t mode, uint32_t count)
{
    switch (mode) {
    case GX2_PRIMITIVE_MODE_POINTS:                   return count;
    case GX2_PRIMITIVE_MODE_LINES:                    return count / 2;
    case GX2_PRIMITIVE_MODE_LINE_STRIP:               return count > 1 ? count - 1 : 0;
    case GX2_PRIMITIVE_MODE_LINE_LOOP:                return count;
    case GX2_PRIMITIVE_MODE_TRIANGLES:                return count / 3;
    case GX2_PRIMITIVE_MODE_TRIANGLE_FAN:
    case GX2_PRIMITIVE_MODE_TRIANGLE_STRIP:           return count > 2 ? count - 2 : 0;
    case GX2_PRIMITIVE_MODE_LINES_ADJACENCY:          return count / 4;
    case GX2_PRIMITIVE_MODE_LINE_STRIP_ADJACENCY:     return count > 3 ? count - 3 : 0;
    case GX2_PRIMITIVE_MODE_TRIANGLES_ADJACENCY:      return count / 6;
    case GX2_PRIMITIVE_MODE_TRIANGLE_STRIP_ADJACENCY: return count > 5 ? (count - 4) / 2 : 0;
    case GX2_PRIMITIVE_MODE_RECTS:                    return count / 3 * 2;
    case GX2_PRIMITIVE_MODE_QUADS:                    return count / 4 * 2;
    case GX2_PRIMITIVE_MODE_QUAD_STRIP:               return count > 3 ? (count - 2) : 0;
    default:                                          return count / 3;
    }
}

static void openGpuSpan()
{
    if (s_topSampled || s_gpuOff)
        return;
    s_topSampled = true;
    GX2SampleTopGPUCycle(&s_gpu[s_frameIndex % kGpuSlots].top);
}

void OnRenderTargetBind()
{
    if (!s_ignore)
        openGpuSpan();
}

void OnDraw(uint32_t primitiveMode, uint32_t count, uint32_t instances)
{
    if (s_ignore)
        return;
    s_drawsSeen = true;
    ++s_draws;
    uint32_t prims = primitives(primitiveMode, count);
    if (instances > 1)
        prims *= instances;
    s_tris += prims;
    openGpuSpan();
}

static uint32_t primitivesBatch(uint32_t mode, uint32_t count, uint32_t draws)
{
    switch (mode) {
    case GX2_PRIMITIVE_MODE_LINE_STRIP:
        return count > draws ? count - draws : 0;
    case GX2_PRIMITIVE_MODE_TRIANGLE_FAN:
    case GX2_PRIMITIVE_MODE_TRIANGLE_STRIP:
    case GX2_PRIMITIVE_MODE_QUAD_STRIP:
        return count > 2 * draws ? count - 2 * draws : 0;
    case GX2_PRIMITIVE_MODE_LINE_STRIP_ADJACENCY:
        return count > 3 * draws ? count - 3 * draws : 0;
    case GX2_PRIMITIVE_MODE_TRIANGLE_STRIP_ADJACENCY:
        return count > 4 * draws ? (count - 4 * draws) / 2 : 0;
    default:
        return primitives(mode, count);
    }
}

void TakePackDraws(void* modeStats)
{
    static const uint32_t kModes = 24;
    if (!modeStats)
        return;
    uint32_t* stats = (uint32_t*)modeStats;
    bool any = false;
    for (uint32_t mode = 0; mode < kModes; ++mode) {
        const uint32_t count = stats[mode * 2];
        const uint32_t draws = stats[mode * 2 + 1];
        if (!draws)
            continue;
        stats[mode * 2] = 0;
        stats[mode * 2 + 1] = 0;
        s_draws += draws;
        s_tris += primitivesBatch(mode, count, draws);
        any = true;
    }
    if (any) {
        s_drawsSeen = true;
        openGpuSpan();
    }
}

void AddOverlayTicks(uint64_t ticks)
{
    s_overlayTicks += ticks;
}

void OnOverlayGpuBegin()
{
    if (s_gpuOff || !s_frameIndex || s_overlayTopSampled)
        return;
    s_overlayTopSampled = true;
    GX2SampleBottomGPUCycle(&s_gpu[s_overlaySlot].overlayTop);
}

void OnOverlayGpuEnd()
{
    if (s_gpuOff || !s_frameIndex || !s_overlayTopSampled || s_overlayClosed)
        return;
    s_overlayClosed = true;
    GX2SampleBottomGPUCycle(&s_gpu[s_overlaySlot].overlayBottom);
}

static void calibrate(uint64_t bottom, OSTime presentTime)
{
    if (!s_calHave || bottom < s_calBase) {
        s_calBase = bottom;
        s_calTime = presentTime;
        s_calHave = true;
        return;
    }
    const uint64_t dt = (uint64_t)(presentTime - s_calTime);
    if (dt < (uint64_t)kCalibrateSeconds * (uint64_t)OSTimerClockSpeed)
        return;

    const double rate = (double)(bottom - s_calBase) / (double)dt;
    if (rate <= 0.0) {
        if (!s_calLogged) {
            s_calLogged = true;
            Logger::LogWarn("frame stats: the GPU cycle counter does not advance "
                            "(%llu over %.1f s); GPU time is off",
                            (unsigned long long)bottom,
                            (double)dt / (double)OSTimerClockSpeed);
        }
        s_gpuOff = true;
        return;
    }

    s_cyclesPerTick = rate;
    if (!s_calLogged) {
        s_calLogged = true;
        Logger::Log("frame stats: GPU cycle counter %.2f MHz, calibrated over %.1f s",
                    s_cyclesPerTick * (double)OSTimerClockSpeed / 1.0e6,
                    (double)dt / (double)OSTimerClockSpeed);
    }
}

static void pollGpu(uint32_t frameNow)
{
    for (int i = 0; i < kGpuSlots; ++i) {
        if (!s_slotArmed[i])
            continue;
        const uint32_t age = frameNow - s_slotFrame[i];
        if (age < (uint32_t)kGpuMinLag)
            continue;

        DCInvalidateRange(&s_gpu[i], sizeof(GpuSlot));
        const uint64_t top = s_gpu[i].top;
        const uint64_t bottom = s_gpu[i].bottom;
        const uint64_t oTop = s_gpu[i].overlayTop;
        const uint64_t oBottom = s_gpu[i].overlayBottom;
        const bool good = top && bottom && bottom > top &&
                          top != kNotASample && bottom != kNotASample;
        if (!good && age < (uint32_t)kGpuMaxLag)
            continue;

        if (s_gpuLogged < kGpuLogs) {
            ++s_gpuLogged;
            Logger::Log("frame stats: gpu slot %d age %u top=%llu bottom=%llu -> %s", i,
                        (unsigned)age, (unsigned long long)top, (unsigned long long)bottom,
                        good ? "ok" : "dropped");
        }
        if (top || bottom) {
            s_lastTop = top;
            s_lastBottom = bottom;
        }
        clearSlot(i);

        if (!good) {
            ++s_gpuDropped;
            continue;
        }
        ++s_gpuValid;
        if (age < (uint32_t)s_count) {
            frameAt((int)age).gpuCycles = (float)(bottom - top);
            if (oTop && oBottom && oBottom > oTop)
                frameAt((int)age).gpuOverlayCycles = (float)(oBottom - oTop);
        }
        calibrate(bottom, s_slotTime[i]);
    }

    if (!s_gpuOff && !s_gpuValid && s_gpuDropped >= kGpuGiveUp) {
        s_gpuOff = true;
        Logger::LogWarn("frame stats: the GPU cycle counter gave nothing usable in %d "
                        "frames (last top=%llu bottom=%llu); GPU time is off",
                        s_gpuDropped, (unsigned long long)s_lastTop,
                        (unsigned long long)s_lastBottom);
    }
}

float GpuMs(float cycles)
{
    if (s_cyclesPerTick <= 0.0 || cycles <= 0.0f)
        return -1.0f;
    return (float)((double)cycles / s_cyclesPerTick * 1000.0 / (double)OSTimerClockSpeed);
}

static GpuState gpuState()
{
    if (s_gpuOff)
        return GPU_UNAVAILABLE;
    if (s_cyclesPerTick > 0.0)
        return GPU_READY;
    return s_gpuValid ? GPU_CALIBRATING : GPU_WAITING;
}

static void updateSummary()
{
    const int n = s_count;
    if (!n)
        return;
    const Frame& newest = frameAt(0);
    s_summary.samples = n;
    s_summary.frameMs = newest.frameMs;
    s_summary.cpuMs = newest.cpuMs;
    s_summary.overlayMs = newest.overlayMs;
    s_summary.draws = newest.draws;
    s_summary.tris = newest.tris;
    s_summary.drawsSeen = s_drawsSeen;
    s_summary.gpuMHz = (float)(s_cyclesPerTick * (double)OSTimerClockSpeed / 1.0e6);
    s_summary.gpuState = gpuState();

    s_summary.waitMs = -1.0f;
    if (newest.cpuMs >= 0.0f) {
        const float wait = newest.frameMs - newest.cpuMs;
        s_summary.waitMs = wait > 0.0f ? wait : 0.0f;
    }

    s_summary.gpuMs = -1.0f;
    s_summary.gpuOverlayMs = -1.0f;
    for (int age = 0; age < n && age <= kGpuMaxLag + 1; ++age) {
        const float ms = GpuMs(frameAt(age).gpuCycles);
        if (ms >= 0.0f) {
            s_summary.gpuMs = ms;
            s_summary.gpuOverlayMs = GpuMs(frameAt(age).gpuOverlayCycles);
            break;
        }
    }

    if (++s_sinceSummary < kSummaryEvery && n > kSummaryEvery)
        return;
    s_sinceSummary = 0;

    double sum = 0.0;
    for (int i = 0; i < n; ++i) {
        s_sorted[i] = s_frames[i].frameMs;
        sum += s_frames[i].frameMs;
    }
    std::sort(s_sorted, s_sorted + n);
    s_summary.avgMs = (float)(sum / n);
    s_summary.p50Ms = s_sorted[(n - 1) / 2];
    s_summary.p95Ms = s_sorted[(int)((n - 1) * 0.95f)];
    s_summary.p99Ms = s_sorted[(int)((n - 1) * 0.99f)];
    s_summary.maxMs = s_sorted[n - 1];
    s_summary.fps = s_summary.avgMs > 0.0f ? 1000.0f / s_summary.avgMs : 0.0f;
    s_summary.low1Fps = s_summary.p99Ms > 0.0f ? 1000.0f / s_summary.p99Ms : 0.0f;
}

static const int   kChooseEvery = 60;
static const float kIntervalBlend = 0.05f;

static bool isBoundary(int target, OSTime now)
{
    if (s_prevTarget >= 0) {
        const float dt = msOf((uint64_t)(now - s_prevPresent));
        float& avg = s_afterMs[s_prevTarget];
        avg = avg > 0.0f ? avg + (dt - avg) * kIntervalBlend : dt;
    }
    s_prevTarget = target;
    s_prevPresent = now;
    s_lastSeen[target] = now;

    const int other = 1 - s_boundary;
    const bool otherAlive = s_lastSeen[other] &&
                            (uint64_t)(now - s_lastSeen[other]) < (uint64_t)OSTimerClockSpeed;
    const bool mineAlive = s_lastSeen[s_boundary] &&
                           (uint64_t)(now - s_lastSeen[s_boundary]) < (uint64_t)OSTimerClockSpeed;
    const int before = s_boundary;
    if (!mineAlive && otherAlive) {
        s_boundary = other;
        s_sinceChoose = 0;
    } else if (++s_sinceChoose >= kChooseEvery) {
        s_sinceChoose = 0;
        if (otherAlive && s_afterMs[other] > s_afterMs[s_boundary] * 1.5f)
            s_boundary = other;
    }
    if (s_boundary != before)
        Logger::Log("frame stats: frames now close at the %s present (after TV %.1f ms, "
                    "after DRC %.1f ms)", s_boundary ? "DRC" : "TV",
                    (double)s_afterMs[0], (double)s_afterMs[1]);
    return target == s_boundary;
}

void OnPresent(bool tv)
{
    const OSTime now = OSGetSystemTime();
    if (!isBoundary(tv ? 0 : 1, now))
        return;

    if (s_lastPresent) {
        Frame& f = s_frames[s_head];
        float ms = msOf((uint64_t)(now - s_lastPresent));
        if (ms > kMaxMs) ms = kMaxMs;
        f.frameMs = ms;
        f.cpuMs = s_execMs;
        f.gpuCycles = 0.0f;
        f.gpuOverlayCycles = 0.0f;
        f.overlayMs = msOf(s_overlayTicks);
        f.draws = s_draws;
        f.tris = s_tris;
        s_head = (s_head + 1) % HISTORY;
        if (s_count < HISTORY)
            ++s_count;
    }

    const int slot = (int)(s_frameIndex % kGpuSlots);
    if (!s_gpuOff) {
        if (s_slotArmed[slot])
            clearSlot(slot);
        if (s_topSampled) {
            GX2SampleBottomGPUCycle(&s_gpu[slot].bottom);
            s_slotFrame[slot] = s_frameIndex;
            s_slotTime[slot] = now;
            s_slotArmed[slot] = true;
        }
        pollGpu(s_frameIndex);
    }

    if (s_count) {
        updateSummary();
        if (s_frameIndex && (s_frameIndex % kLogEvery) == 0)
            Logger::Log("frame stats: %.1f fps, frame %.1f ms (p99 %.1f), cpu %.1f, gpu %.1f, "
                        "wait %.1f, overlay %.1f cpu %.1f gpu, %u draws, %u tris, boundary %s",
                        (double)s_summary.fps, (double)s_summary.avgMs, (double)s_summary.p99Ms,
                        (double)s_summary.cpuMs, (double)s_summary.gpuMs,
                        (double)s_summary.waitMs, (double)s_summary.overlayMs,
                        (double)s_summary.gpuOverlayMs,
                        (unsigned)s_summary.draws, (unsigned)s_summary.tris,
                        s_boundary ? "DRC" : "TV");
    }

    ++s_frameIndex;
    s_lastPresent = now;
    s_execMs = -1.0f;
    s_overlayTicks = 0;
    s_draws = s_tris = 0;
    s_topSampled = false;
}

const Summary& Current() { return s_summary; }

int FrameCount() { return s_count; }

const Frame& FrameAt(int age)
{
    if (age < 0) age = 0;
    if (age >= s_count) age = s_count ? s_count - 1 : 0;
    return frameAt(age);
}
}
