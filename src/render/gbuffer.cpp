#include "render/gbuffer.h"

#include "core/frame_stats.h"
#include "core/logger.h"

#include <gx2/surface.h>
#include <stdio.h>
#include <string.h>

namespace GBuffer {
static const int kMaxBindings = 48;
static const int kMaxLogs     = 10;

static Binding  s_bindings[kMaxBindings];
static int      s_count;
static bool     s_ignore;
static bool     s_overflow;

static GX2DepthBuffer s_curDepth;
static bool           s_curDepthValid;

static GX2ColorBuffer s_frameColor;
static GX2DepthBuffer s_frameDepth;
static bool           s_frameValid;
static uint32_t       s_frameArea;

static GX2ColorBuffer s_sceneColor;
static GX2DepthBuffer s_sceneDepth;
static bool           s_sceneValid;

static uint32_t s_lastSignature;
static int      s_logs;
static uint32_t s_frames;
static char     s_summary[128] = "g-buffer: nothing captured yet";

static void push(uint8_t kind, uint8_t slot, const GX2Surface& s)
{
    if (s_count >= kMaxBindings) {
        s_overflow = true;
        return;
    }
    Binding& b = s_bindings[s_count++];
    b.kind = kind;
    b.slot = slot;
    b.aa = (uint8_t)s.aa;
    b.tile = (uint8_t)s.tileMode;
    b.width = (uint16_t)s.width;
    b.height = (uint16_t)s.height;
    b.format = (uint32_t)s.format;
    b.image = s.image;
}

void SetIgnore(bool ignore) { s_ignore = ignore; }

void NoteColor(const GX2ColorBuffer* buffer, int slot)
{
    if (s_ignore || !buffer || !buffer->surface.image)
        return;
    FrameStats::OnRenderTargetBind();
    push(KIND_COLOR, (uint8_t)slot, buffer->surface);
    if (slot != 0 || !s_curDepthValid)
        return;
    if (s_curDepth.surface.width != buffer->surface.width ||
        s_curDepth.surface.height != buffer->surface.height)
        return;
    const uint32_t a = buffer->surface.width * buffer->surface.height;
    if (a < s_frameArea)
        return;
    s_frameArea = a;
    s_frameColor = *buffer;
    s_frameDepth = s_curDepth;
    s_frameValid = true;
}

void NoteDepth(const GX2DepthBuffer* buffer)
{
    if (s_ignore || !buffer || !buffer->surface.image)
        return;
    push(KIND_DEPTH, 0, buffer->surface);
    s_curDepth = *buffer;
    s_curDepthValid = true;
}

int BindingCount() { return s_count; }

const Binding* BindingAt(int index)
{
    return (index >= 0 && index < s_count) ? &s_bindings[index] : nullptr;
}

const GX2ColorBuffer* SceneColor() { return s_sceneValid ? &s_sceneColor : nullptr; }
const GX2DepthBuffer* SceneDepth() { return s_sceneValid ? &s_sceneDepth : nullptr; }

const char* Summary() { return s_summary; }

static uint32_t signature()
{
    uint32_t h = 2166136261u;
    for (int i = 0; i < s_count; ++i) {
        const Binding& b = s_bindings[i];
        const uint32_t v = ((uint32_t)b.kind << 28) ^ ((uint32_t)b.slot << 24) ^
                           ((uint32_t)b.width << 12) ^ (uint32_t)b.height ^ (b.format << 4);
        h = (h ^ v) * 16777619u;
    }
    return h;
}

static void logFrame()
{
    char line[400];
    int n = snprintf(line, sizeof(line), "g-buffer frame %u: %d binds%s:", s_frames, s_count,
                     s_overflow ? "+" : "");
    for (int i = 0; i < s_count && n < (int)sizeof(line) - 40; ++i) {
        const Binding& b = s_bindings[i];
        n += snprintf(line + n, sizeof(line) - (size_t)n, " %c%u:%ux%u/%x",
                      b.kind == KIND_COLOR ? 'C' : 'D', (unsigned)b.slot,
                      (unsigned)b.width, (unsigned)b.height, (unsigned)b.format);
    }
    Logger::Log("%s", line);
    if (s_frameValid)
        Logger::Log("g-buffer scene: colour %ux%u fmt=0x%x img=%p | depth fmt=0x%x aa=%u tile=%u "
                    "img=%p hiz=%p",
                    (unsigned)s_frameColor.surface.width, (unsigned)s_frameColor.surface.height,
                    (unsigned)s_frameColor.surface.format, s_frameColor.surface.image,
                    (unsigned)s_frameDepth.surface.format, (unsigned)s_frameDepth.surface.aa,
                    (unsigned)s_frameDepth.surface.tileMode, s_frameDepth.surface.image,
                    s_frameDepth.hiZPtr);
    else
        Logger::Log("g-buffer scene: no colour target had a depth buffer of its own size bound");
}

void OnFrameEnd()
{
    ++s_frames;
    int colours = 0, depths = 0;
    for (int i = 0; i < s_count; ++i)
        (s_bindings[i].kind == KIND_COLOR ? colours : depths)++;

    if (s_frameValid) {
        s_sceneColor = s_frameColor;
        s_sceneDepth = s_frameDepth;
        s_sceneValid = true;
    }
    if (s_sceneValid)
        snprintf(s_summary, sizeof(s_summary),
                 "g-buffer: %d colour, %d depth binds; scene %ux%u, depth %s",
                 colours, depths, (unsigned)s_sceneColor.surface.width,
                 (unsigned)s_sceneColor.surface.height,
                 s_sceneDepth.hiZPtr ? "with HiZ" : "plain");
    else
        snprintf(s_summary, sizeof(s_summary),
                 "g-buffer: %d colour, %d depth binds; no scene pair found", colours, depths);

    const uint32_t sig = signature();
    if (s_count && sig != s_lastSignature && s_logs < kMaxLogs) {
        s_lastSignature = sig;
        ++s_logs;
        logFrame();
    }

    s_count = 0;
    s_overflow = false;
    s_curDepthValid = false;
    s_frameValid = false;
    s_frameArea = 0;
}
}
