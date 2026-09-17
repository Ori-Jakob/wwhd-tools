#pragma once

#include <stdint.h>

namespace FrameStats {
static const int HISTORY = 600;

enum GpuState {
    GPU_WAITING = 0,
    GPU_CALIBRATING,
    GPU_READY,
    GPU_UNAVAILABLE,
};

struct Frame {
    float    frameMs;
    float    cpuMs;
    float    gpuCycles;
    float    gpuOverlayCycles;
    float    overlayMs;
    uint32_t draws;
    uint32_t tris;
};

struct Summary {
    int      samples;
    float    fps;
    float    low1Fps;
    float    frameMs;
    float    avgMs, p50Ms, p95Ms, p99Ms, maxMs;
    float    cpuMs;
    float    gpuMs;
    float    gpuOverlayMs;
    float    waitMs;
    float    overlayMs;
    uint32_t draws, tris;
    float    gpuMHz;
    GpuState gpuState;
    bool     drawsSeen;
};

void OnApplicationStart();

void OnPresentationFrame();

void OnExecuteBegin();
void OnExecuteEnd();
void OnDraw(uint32_t primitiveMode, uint32_t count, uint32_t instances);

void TakePackDraws(void* modeStats);
void OnRenderTargetBind();
void OnPresent(bool tv);
void OnOverlayGpuBegin();
void OnOverlayGpuEnd();
void AddOverlayTicks(uint64_t ticks);
void SetIgnore(bool ignore);

const Summary& Current();
int            FrameCount();
const Frame&   FrameAt(int ageFromNewest);
float          GpuMs(float cycles);
}
