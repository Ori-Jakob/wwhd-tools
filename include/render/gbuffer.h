#pragma once

#include <stdint.h>

struct GX2ColorBuffer;
struct GX2DepthBuffer;

namespace GBuffer {
enum Kind { KIND_COLOR = 0, KIND_DEPTH = 1 };

struct Binding {
    uint8_t     kind;
    uint8_t     slot;
    uint8_t     aa;
    uint8_t     tile;
    uint16_t    width;
    uint16_t    height;
    uint32_t    format;
    const void* image;
};

void OnFrameEnd();
void SetIgnore(bool ignore);

void NoteColor(const GX2ColorBuffer* buffer, int slot);
void NoteDepth(const GX2DepthBuffer* buffer);

int            BindingCount();
const Binding* BindingAt(int index);

const GX2ColorBuffer* SceneColor();
const GX2DepthBuffer* SceneDepth();

const char* Summary();
}
