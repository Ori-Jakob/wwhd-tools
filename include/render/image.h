#pragma once

#include "imgui.h"

#include <stddef.h>
#include <stdint.h>

namespace Image {
struct Texture {
    ImTextureID id;
    float       width;
    float       height;
};

const Texture* Load(const uint8_t* blob, size_t size);

const Texture* LoadIndexed(const void* key, uint32_t width, uint32_t height,
                           const uint32_t* palette, uint32_t paletteCount,
                           const uint8_t* indices, uint32_t stride);

bool UpdateIndexed(const Texture* texture, const uint32_t* palette, uint32_t paletteCount,
                   const uint8_t* indices, uint32_t stride);

void DestroyAll();
}
