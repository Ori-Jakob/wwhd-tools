#include "render/image.h"

#include "core/logger.h"

#include <gx2/mem.h>
#include <gx2/sampler.h>
#include <gx2/surface.h>
#include <gx2/texture.h>
#include <gx2/utils.h>
#include <gx2r/surface.h>

#include <string.h>

#include "imgui_impl_gx2.h"

namespace Image {
static bool createSurface(GX2Surface* surface)
{
    return GX2RCreateSurface(surface,
                             (GX2RResourceFlags)(GX2R_RESOURCE_BIND_TEXTURE |
                                                 GX2R_RESOURCE_USAGE_CPU_WRITE |
                                                 GX2R_RESOURCE_USAGE_GPU_READ)) != FALSE;
}

static void* lockSurface(GX2Surface* surface)
{
    return GX2RLockSurfaceEx(surface, 0, GX2R_RESOURCE_BIND_NONE);
}

static void unlockSurface(GX2Surface* surface)
{
    GX2RUnlockSurfaceEx(surface, 0, GX2R_RESOURCE_BIND_NONE);
}

static void destroySurface(GX2Surface* surface)
{
    GX2RDestroySurfaceEx(surface, GX2R_RESOURCE_BIND_NONE);
}

static const int kMaxTextures = 8;

struct Entry {
    const void*           key;
    bool                  failed;
    Texture               texture;
    GX2Texture            gx2;
    GX2Sampler            sampler;
    ImGui_ImplGX2_Texture binding;
};

static Entry s_entries[kMaxTextures];
static int   s_count = 0;

static bool createTexture(Entry& entry, uint32_t width, uint32_t height)
{
    GX2Texture* tex = &entry.gx2;
    memset(tex, 0, sizeof(*tex));
    tex->surface.dim       = GX2_SURFACE_DIM_TEXTURE_2D;
    tex->surface.use       = GX2_SURFACE_USE_TEXTURE;
    tex->surface.width     = width;
    tex->surface.height    = height;
    tex->surface.depth     = 1;
    tex->surface.mipLevels = 1;
    tex->surface.format    = GX2_SURFACE_FORMAT_SRGB_R8_G8_B8_A8;
    tex->surface.aa        = GX2_AA_MODE1X;
    tex->surface.tileMode  = GX2_TILE_MODE_LINEAR_ALIGNED;
    tex->viewNumSlices     = 1;
    tex->viewNumMips       = 1;
    tex->compMap = GX2_COMP_MAP(GX2_SQ_SEL_A, GX2_SQ_SEL_B, GX2_SQ_SEL_G, GX2_SQ_SEL_R);

    if (!createSurface(&tex->surface))
        return false;
    GX2InitTextureRegs(tex);

    GX2InitSampler(&entry.sampler, GX2_TEX_CLAMP_MODE_CLAMP,
                   GX2_TEX_XY_FILTER_MODE_LINEAR);

    entry.binding.Texture = tex;
    entry.binding.Sampler = &entry.sampler;
    entry.texture.id      = (ImTextureID)&entry.binding;
    entry.texture.width   = (float)width;
    entry.texture.height  = (float)height;
    return true;
}

static bool fill(Entry& entry, const uint32_t* palette, uint32_t paletteCount,
                 const uint8_t* indices, uint32_t stride)
{
    GX2Texture* tex = &entry.gx2;
    uint8_t* dst = (uint8_t*)lockSurface(&tex->surface);
    if (!dst)
        return false;

    const uint32_t width = tex->surface.width;
    const uint32_t height = tex->surface.height;
    for (uint32_t y = 0; y < height; ++y) {
        uint32_t*      row = (uint32_t*)(dst + y * tex->surface.pitch * 4);
        const uint8_t* src = indices + y * stride;
        for (uint32_t x = 0; x < width; ++x) {
            const uint8_t i = src[x];
            row[x] = i < paletteCount ? palette[i] : 0u;
        }
    }
    unlockSurface(&tex->surface);
    GX2Invalidate((GX2InvalidateMode)(GX2_INVALIDATE_MODE_CPU | GX2_INVALIDATE_MODE_TEXTURE),
                  tex->surface.image, tex->surface.imageSize);
    return true;
}

static Entry* findEntry(const void* key)
{
    for (int i = 0; i < s_count; ++i)
        if (s_entries[i].key == key)
            return &s_entries[i];
    return nullptr;
}

static const Texture* create(const void* key, uint32_t width, uint32_t height,
                             const uint32_t* palette, uint32_t paletteCount,
                             const uint8_t* indices, uint32_t stride)
{
    if (!key || !ImGui_ImplGX2_DeviceObjectsCreated())
        return nullptr;

    if (Entry* existing = findEntry(key))
        return existing->failed ? nullptr : &existing->texture;

    if (s_count >= kMaxTextures) {
        Logger::LogError("[wwhd_tools] image cache full (%d textures)", kMaxTextures);
        return nullptr;
    }

    Entry& entry = s_entries[s_count++];
    entry.key = key;
    if (!palette || !indices || width == 0 || height == 0 || paletteCount == 0 ||
        paletteCount > 256 || !createTexture(entry, width, height)) {
        entry.failed = true;
        Logger::LogError("[wwhd_tools] image create failed (%ux%u)", (unsigned)width,
                         (unsigned)height);
        return nullptr;
    }
    if (!fill(entry, palette, paletteCount, indices, stride)) {
        destroySurface(&entry.gx2.surface);
        entry.failed = true;
        Logger::LogError("[wwhd_tools] image upload failed (%ux%u)", (unsigned)width,
                         (unsigned)height);
        return nullptr;
    }
    return &entry.texture;
}

const Texture* Load(const uint8_t* blob, size_t size)
{
    if (!blob || size < 8)
        return nullptr;

    const uint32_t width   = ((uint32_t)blob[0] << 8) | blob[1];
    const uint32_t height  = ((uint32_t)blob[2] << 8) | blob[3];
    const uint32_t palSize = ((uint32_t)blob[4] << 8) | blob[5];
    if (size < 8 + (size_t)palSize * 4 + (size_t)width * height)
        return nullptr;

    const uint32_t* palette = (const uint32_t*)(blob + 8);
    const uint8_t*  indices = blob + 8 + palSize * 4;
    return create(blob, width, height, palette, palSize, indices, width);
}

const Texture* LoadIndexed(const void* key, uint32_t width, uint32_t height,
                           const uint32_t* palette, uint32_t paletteCount,
                           const uint8_t* indices, uint32_t stride)
{
    return create(key, width, height, palette, paletteCount, indices, stride);
}

bool UpdateIndexed(const Texture* texture, const uint32_t* palette, uint32_t paletteCount,
                   const uint8_t* indices, uint32_t stride)
{
    if (!texture || !palette || !indices)
        return false;
    for (int i = 0; i < s_count; ++i) {
        Entry& entry = s_entries[i];
        if (&entry.texture != texture || entry.failed)
            continue;
        return fill(entry, palette, paletteCount, indices, stride);
    }
    return false;
}

void DestroyAll()
{
    for (int i = 0; i < s_count; ++i) {
        Entry& entry = s_entries[i];
        if (!entry.failed)
            destroySurface(&entry.gx2.surface);

        entry.key     = nullptr;
        entry.failed  = false;
        entry.texture = Texture();
        entry.binding = ImGui_ImplGX2_Texture();
    }
    s_count = 0;
}
}
