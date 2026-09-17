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

void DestroyAll();
}
