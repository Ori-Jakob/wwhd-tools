#include "render/scene_depth.h"

#include "core/logger.h"
#include "render/gbuffer.h"

#include <gx2/enum.h>
#include <gx2/mem.h>
#include <gx2/sampler.h>
#include <gx2/shaders.h>
#include <gx2/surface.h>
#include <gx2/texture.h>
#include <gx2/utils.h>
#include <whb/gfx.h>

#include <stdio.h>
#include <string.h>

#include "collision_gsh_bin.h"

extern "C" void GX2ExpandDepthBuffer(GX2DepthBuffer* buffer);

namespace SceneDepth {
static const uint32_t kDbKillEnable = 1u << 6;

static WHBGfxShaderGroup s_group;
static bool              s_groupReady;
static bool              s_groupFailed;
static GX2DepthBuffer    s_depth;
static GX2Texture        s_tex;
static GX2Sampler        s_sampler;
static bool              s_texValid;
static bool              s_available;
static bool              s_expandLogged;
static char              s_summary[160] = "scene depth: nothing captured yet";

static bool loadShaders()
{
    if (s_groupReady)
        return true;
    if (s_groupFailed)
        return false;

    memset(&s_group, 0, sizeof(s_group));
    if (!WHBGfxLoadGFDShaderGroup(&s_group, 0, collision_gsh_bin)) {
        s_groupFailed = true;
        Logger::LogError("scene depth: collision shader group failed to load");
        return false;
    }
    WHBGfxInitShaderAttribute(&s_group, "Position", 0, 0, GX2_ATTRIB_FORMAT_FLOAT_32_32);
    WHBGfxInitShaderAttribute(&s_group, "UV", 0, 8, GX2_ATTRIB_FORMAT_FLOAT_32_32);
    WHBGfxInitShaderAttribute(&s_group, "Color", 0, 16, GX2_ATTRIB_FORMAT_UNORM_8_8_8_8);
    if (!WHBGfxInitFetchShader(&s_group)) {
        WHBGfxFreeShaderGroup(&s_group);
        s_groupFailed = true;
        Logger::LogError("scene depth: fetch shader failed");
        return false;
    }

    const uint32_t before = s_group.pixelShader->regs.db_shader_control;
    s_group.pixelShader->regs.db_shader_control = before | kDbKillEnable;
    Logger::Log("scene depth: shaders ready, db_shader_control %08x -> %08x",
                (unsigned)before, (unsigned)s_group.pixelShader->regs.db_shader_control);
    s_groupReady = true;
    return true;
}

static const char* rejectReason(const GX2DepthBuffer* db)
{
    if (!db)
        return "no scene depth captured yet";
    if (db->surface.aa != GX2_AA_MODE1X)
        return "scene depth is multisampled";
    switch ((uint32_t)db->surface.format) {
    case GX2_SURFACE_FORMAT_UNORM_R24_X8:
    case GX2_SURFACE_FORMAT_FLOAT_D24_S8:
    case GX2_SURFACE_FORMAT_FLOAT_R32:
        return nullptr;
    default:
        return "scene depth format unsupported";
    }
}

static bool sameSurface(const GX2Surface& a, const GX2Surface& b)
{
    return a.image == b.image && a.width == b.width && a.height == b.height &&
           a.format == b.format && a.tileMode == b.tileMode && a.swizzle == b.swizzle &&
           a.pitch == b.pitch && a.aa == b.aa;
}

static void buildTexture(const GX2DepthBuffer* db)
{
    memset(&s_tex, 0, sizeof(s_tex));
    s_tex.surface = db->surface;
    s_tex.surface.use = GX2_SURFACE_USE_TEXTURE;
    s_tex.surface.mipLevels = 1;
    s_tex.surface.mipmaps = nullptr;
    s_tex.surface.mipmapSize = 0;
    s_tex.viewFirstMip = 0;
    s_tex.viewNumMips = 1;
    s_tex.viewFirstSlice = 0;
    s_tex.viewNumSlices = 1;
    s_tex.compMap = GX2_COMP_MAP(GX2_SQ_SEL_R, GX2_SQ_SEL_R, GX2_SQ_SEL_R, GX2_SQ_SEL_1);
    GX2InitTextureRegs(&s_tex);
    GX2InitSampler(&s_sampler, GX2_TEX_CLAMP_MODE_CLAMP, GX2_TEX_XY_FILTER_MODE_POINT);
    s_texValid = true;
    Logger::Log("scene depth: texture over %ux%u fmt=0x%x tile=%u swizzle=%08x pitch=%u "
                "size=%u hiz=%p img=%p",
                (unsigned)db->surface.width, (unsigned)db->surface.height,
                (unsigned)db->surface.format, (unsigned)db->surface.tileMode,
                (unsigned)db->surface.swizzle, (unsigned)db->surface.pitch,
                (unsigned)db->surface.imageSize, db->hiZPtr, db->surface.image);
}

bool Prepare(bool wanted)
{
    s_available = false;
    if (!wanted) {
        snprintf(s_summary, sizeof(s_summary), "scene depth: depth test off");
        return false;
    }
    const GX2DepthBuffer* db = GBuffer::SceneDepth();
    if (const char* why = rejectReason(db)) {
        snprintf(s_summary, sizeof(s_summary), "scene depth: %s", why);
        return false;
    }
    if (!loadShaders()) {
        snprintf(s_summary, sizeof(s_summary), "scene depth: shader unavailable");
        return false;
    }
    if (!s_texValid || !sameSurface(s_depth.surface, db->surface))
        buildTexture(db);
    s_depth = *db;

    if (s_depth.hiZPtr) {
        GX2ExpandDepthBuffer(&s_depth);
        if (!s_expandLogged) {
            s_expandLogged = true;
            Logger::Log("scene depth: expanding HiZ each frame");
        }
    }
    GX2Invalidate((GX2InvalidateMode)(GX2_INVALIDATE_MODE_DEPTH_BUFFER |
                                      GX2_INVALIDATE_MODE_TEXTURE),
                  s_depth.surface.image, s_depth.surface.imageSize);

    s_available = true;
    snprintf(s_summary, sizeof(s_summary), "scene depth: %ux%u fmt 0x%x bound as texture%s",
             (unsigned)s_depth.surface.width, (unsigned)s_depth.surface.height,
             (unsigned)s_depth.surface.format, s_depth.hiZPtr ? ", HiZ expanded" : "");
    return true;
}

bool Available()
{
    return s_available;
}

void Bind(float sign, float bias)
{
    if (!s_available || !s_groupReady)
        return;
    GX2SetFetchShader(&s_group.fetchShader);
    GX2SetVertexShader(s_group.vertexShader);
    GX2SetPixelShader(s_group.pixelShader);

    float params[4] = { sign, -bias, 0.0f, 0.0f };
    uint32_t words[4];
    memcpy(words, params, sizeof(words));
    GX2SetPixelUniformReg(0, 4, words);

    GX2SetPixelTexture(&s_tex, 1);
    GX2SetPixelSampler(&s_sampler, 1);
}

void Reset()
{
    if (s_groupReady)
        WHBGfxFreeShaderGroup(&s_group);
    s_groupReady = false;
    s_groupFailed = false;
    s_texValid = false;
    s_available = false;
}

const char* Summary()
{
    return s_summary;
}
}
