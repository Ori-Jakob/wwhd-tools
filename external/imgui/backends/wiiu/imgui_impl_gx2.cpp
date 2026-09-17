// dear imgui: Renderer Backend for the Nintendo Wii U using GX2
#include "imgui.h"
#include "imgui_impl_gx2.h"
#include <stdio.h>
#include <stdint.h>     // intptr_t
#include <malloc.h>     // memalign

// GX2 includes
#include <whb/gfx.h>
#include <gx2/registers.h>
#include <gx2/draw.h>
#include <gx2/utils.h>
#include <gx2/mem.h>
#include <gx2r/surface.h>

#include <coreinit/debug.h>     // OSReport (device-object failure diagnostics)

// Include shader data
#include "shaders/shader.h"

// Allocation/resource hooks. The stock backend uses the process heap and GX2R;
// WUPS unity builds can override these before including this file so GPU-facing
// data comes from MemoryMappingModule instead.
#ifndef IMGUI_IMPL_GX2_ALLOC
#define IMGUI_IMPL_GX2_ALLOC(size, alignment) memalign((alignment), (size))
#endif
#ifndef IMGUI_IMPL_GX2_FREE
#define IMGUI_IMPL_GX2_FREE(ptr) free(ptr)
#endif
#ifndef IMGUI_IMPL_GX2_CREATE_SURFACE
#define IMGUI_IMPL_GX2_CREATE_SURFACE(surface, flags) GX2RCreateSurface((surface), (flags))
#endif
#ifndef IMGUI_IMPL_GX2_LOCK_SURFACE
#define IMGUI_IMPL_GX2_LOCK_SURFACE(surface, level, flags) GX2RLockSurfaceEx((surface), (level), (flags))
#endif
#ifndef IMGUI_IMPL_GX2_UNLOCK_SURFACE
#define IMGUI_IMPL_GX2_UNLOCK_SURFACE(surface, level, flags) GX2RUnlockSurfaceEx((surface), (level), (flags))
#endif
#ifndef IMGUI_IMPL_GX2_DESTROY_SURFACE
#define IMGUI_IMPL_GX2_DESTROY_SURFACE(surface, flags) GX2RDestroySurfaceEx((surface), (flags))
#endif
#ifndef IMGUI_IMPL_GX2_LOAD_SHADER_GROUP
#define IMGUI_IMPL_GX2_LOAD_SHADER_GROUP(group, index, file) WHBGfxLoadGFDShaderGroup((group), (index), (file))
#endif
#ifndef IMGUI_IMPL_GX2_INIT_FETCH_SHADER
#define IMGUI_IMPL_GX2_INIT_FETCH_SHADER(group) WHBGfxInitFetchShader(group)
#endif
#ifndef IMGUI_IMPL_GX2_FREE_SHADER_GROUP
#define IMGUI_IMPL_GX2_FREE_SHADER_GROUP(group) WHBGfxFreeShaderGroup(group)
#endif

// GX2 Data
struct ImGui_ImplGX2_Data
{
    uint32_t VertexBufferSize;
    void* VertexBuffer;
    uint32_t IndexBufferSize;
    void* IndexBuffer;

    // RenderDrawData may replay one prepared ImGui frame into both Wii U
    // outputs. The geometry is target-independent, so it only needs to be
    // copied and cache-flushed once per backend frame.
    const ImDrawData* UploadedDrawData;
    uint32_t UploadedVertexSize;
    uint32_t UploadedIndexSize;

    ImGui_ImplGX2_Texture* FontTexture;

    WHBGfxShaderGroup* ShaderGroup;

    ImGui_ImplGX2_Data() { memset(this, 0, sizeof(*this)); }
};

// Backend data stored in io.BackendRendererUserData
static ImGui_ImplGX2_Data* ImGui_ImplGX2_GetBackendData()
{
    return ImGui::GetCurrentContext() ? (ImGui_ImplGX2_Data*)ImGui::GetIO().BackendRendererUserData : NULL;
}

// Functions
bool    ImGui_ImplGX2_Init()
{
    ImGuiIO& io = ImGui::GetIO();
    IM_ASSERT(io.BackendRendererUserData == NULL && "Already initialized a renderer backend!");

    ImGui_ImplGX2_Data* bd = IM_NEW(ImGui_ImplGX2_Data)();
    io.BackendRendererUserData = (void*)bd;
    io.BackendRendererName = "imgui_impl_gx2";

    io.BackendFlags |= ImGuiBackendFlags_RendererHasVtxOffset;  // We can honor the ImDrawCmd::VtxOffset field, allowing for large meshes.

    return true;
}

void    ImGui_ImplGX2_Shutdown()
{
    ImGui_ImplGX2_Data* bd = ImGui_ImplGX2_GetBackendData();
    IM_ASSERT(bd != NULL && "No renderer backend to shutdown, or already shutdown?");
    ImGuiIO& io = ImGui::GetIO();

    ImGui_ImplGX2_DestroyDeviceObjects();
    io.BackendRendererName = NULL;
    io.BackendRendererUserData = NULL;
    IM_DELETE(bd);
}

void    ImGui_ImplGX2_NewFrame()
{
    ImGui_ImplGX2_Data* bd = ImGui_ImplGX2_GetBackendData();
    IM_ASSERT(bd != NULL && "Did you call ImGui_ImplGX2_Init()?");

    bd->UploadedDrawData = NULL;
    bd->UploadedVertexSize = 0;
    bd->UploadedIndexSize = 0;

    if (!bd->ShaderGroup && !ImGui_ImplGX2_CreateDeviceObjects())
    {
        // Without device objects RenderDrawData silently no-ops, which on a
        // console presents as "hooks work but nothing draws" -- say so once.
        static bool logged = false;
        if (!logged)
        {
            logged = true;
            OSReport("[imgui_impl_gx2] CreateDeviceObjects failed -- overlay will not draw\n");
        }
    }
}

bool    ImGui_ImplGX2_DeviceObjectsCreated()
{
    ImGui_ImplGX2_Data* bd = ImGui_ImplGX2_GetBackendData();
    return bd && bd->ShaderGroup != NULL;
}

static void ImGui_ImplGX2_SetupRenderState(ImDrawData* draw_data, int fb_width, int fb_height)
{
    ImGui_ImplGX2_Data* bd = ImGui_ImplGX2_GetBackendData();

    // Setup render state: alpha-blending enabled, no face culling, no depth testing
    GX2SetColorControl(GX2_LOGIC_OP_COPY, 0xFF, FALSE, TRUE);
    GX2SetBlendControl(GX2_RENDER_TARGET_0,
                GX2_BLEND_MODE_SRC_ALPHA,
                GX2_BLEND_MODE_INV_SRC_ALPHA,
                GX2_BLEND_COMBINE_MODE_ADD,
                TRUE,
                GX2_BLEND_MODE_ONE,
                GX2_BLEND_MODE_INV_SRC_ALPHA,
                GX2_BLEND_COMBINE_MODE_ADD);
    GX2SetCullOnlyControl(GX2_FRONT_FACE_CCW, FALSE, FALSE);
    GX2SetDepthOnlyControl(FALSE, FALSE, GX2_COMPARE_FUNC_NEVER);

    // Setup viewport, orthographic projection matrix
    // Our visible imgui space lies from draw_data->DisplayPos (top left) to draw_data->DisplayPos+data_data->DisplaySize (bottom right). DisplayPos is (0,0) for single viewport apps.
    GX2SetViewport(0, 0, (float)fb_width, (float)fb_height, 0.0f, 1.0f);

    GX2SetFetchShader(&bd->ShaderGroup->fetchShader);
    GX2SetVertexShader(bd->ShaderGroup->vertexShader);
    GX2SetPixelShader(bd->ShaderGroup->pixelShader);

    float L = draw_data->DisplayPos.x;
    float R = draw_data->DisplayPos.x + draw_data->DisplaySize.x;
    float T = draw_data->DisplayPos.y;
    float B = draw_data->DisplayPos.y + draw_data->DisplaySize.y;
    const float ortho_projection[4][4] =
    {
        { 2.0f/(R-L),   0.0f,         0.0f,   0.0f },
        { 0.0f,         2.0f/(T-B),   0.0f,   0.0f },
        { 0.0f,         0.0f,        -1.0f,   0.0f },
        { (R+L)/(L-R),  (T+B)/(B-T),  0.0f,   1.0f },
    };

    GX2SetVertexUniformReg(0, sizeof(ortho_projection) / sizeof(float), &ortho_projection[0][0]);
}

void    ImGui_ImplGX2_RenderDrawData(ImDrawData* draw_data, const ImDrawList* const* only_draw_lists, int only_count, const ImDrawList* const* exclude_draw_lists, int exclude_count)
{
    // Avoid rendering when minimized, scale coordinates for retina displays (screen coordinates != framebuffer coordinates)
#ifdef __WUPS__
    int fb_width = (int)(draw_data->DisplaySize.x * draw_data->FramebufferScale.x + 0.5f);
    int fb_height = (int)(draw_data->DisplaySize.y * draw_data->FramebufferScale.y + 0.5f);
#else
    int fb_width = (int)(draw_data->DisplaySize.x * draw_data->FramebufferScale.x);
    int fb_height = (int)(draw_data->DisplaySize.y * draw_data->FramebufferScale.y);
#endif
    if (fb_width <= 0 || fb_height <= 0)
        return;

    ImGui_ImplGX2_Data* bd = ImGui_ImplGX2_GetBackendData();
    if (!bd || !bd->ShaderGroup)
        return;

    ImGui_ImplGX2_SetupRenderState(draw_data, fb_width, fb_height);

    // Will project scissor/clipping rectangles into framebuffer space
    ImVec2 clip_off = draw_data->DisplayPos;         // (0,0) unless using multi-viewports
    ImVec2 clip_scale = draw_data->FramebufferScale; // (1,1) unless using retina display which are often (2,2)

    // Create continuous vertex/index buffers
    uint32_t vtx_buffer_size = (uint32_t)draw_data->TotalVtxCount * (int)sizeof(ImDrawVert);
    uint32_t idx_buffer_size = (uint32_t)draw_data->TotalIdxCount * (int)sizeof(ImDrawIdx);
    if (!vtx_buffer_size || !idx_buffer_size)
        return;

    // Grow buffers if needed
    if (bd->VertexBufferSize < vtx_buffer_size)
    {
        IMGUI_IMPL_GX2_FREE(bd->VertexBuffer);
        bd->VertexBuffer = IMGUI_IMPL_GX2_ALLOC(vtx_buffer_size, GX2_VERTEX_BUFFER_ALIGNMENT);
        bd->VertexBufferSize = bd->VertexBuffer ? vtx_buffer_size : 0;
    }
    if (bd->IndexBufferSize < idx_buffer_size)
    {
        IMGUI_IMPL_GX2_FREE(bd->IndexBuffer);
        bd->IndexBuffer = IMGUI_IMPL_GX2_ALLOC(idx_buffer_size, GX2_INDEX_BUFFER_ALIGNMENT);
        bd->IndexBufferSize = bd->IndexBuffer ? idx_buffer_size : 0;
    }
    if ((vtx_buffer_size && !bd->VertexBuffer) ||
        (idx_buffer_size && !bd->IndexBuffer))
        return;

    const bool needsUpload = bd->UploadedDrawData != draw_data ||
                             bd->UploadedVertexSize != vtx_buffer_size ||
                             bd->UploadedIndexSize != idx_buffer_size;
    if (needsUpload)
    {
        // Copy data into continuous buffers. A second target in the same frame
        // reuses this upload; target-specific projection/scissor/state setup and
        // all draw calls still run below.
        uint8_t* vtx_dst = (uint8_t*)bd->VertexBuffer;
        uint8_t* idx_dst = (uint8_t*)bd->IndexBuffer;
        for (int n = 0; n < draw_data->CmdListsCount; n++)
        {
            const ImDrawList* cmd_list = draw_data->CmdLists[n];

            memcpy(vtx_dst, cmd_list->VtxBuffer.Data, cmd_list->VtxBuffer.Size * sizeof(ImDrawVert));
            vtx_dst += cmd_list->VtxBuffer.Size * sizeof(ImDrawVert);

            memcpy(idx_dst, cmd_list->IdxBuffer.Data, cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx));
            idx_dst += cmd_list->IdxBuffer.Size * sizeof(ImDrawIdx);
        }

        // Flush memory after the CPU upload. The GPU-visible buffers remain
        // valid while the same draw data is replayed into another target.
        GX2Invalidate(GX2_INVALIDATE_MODE_CPU_ATTRIBUTE_BUFFER, bd->VertexBuffer, vtx_buffer_size);
        GX2Invalidate(GX2_INVALIDATE_MODE_CPU, bd->IndexBuffer, idx_buffer_size);

        bd->UploadedDrawData = draw_data;
        bd->UploadedVertexSize = vtx_buffer_size;
        bd->UploadedIndexSize = idx_buffer_size;
    }

    GX2SetAttribBuffer(0, vtx_buffer_size, sizeof(ImDrawVert), bd->VertexBuffer);

    // Render command lists
    // (Because we merged all buffers into a single one, we maintain our own offset into them)
    int global_vtx_offset = 0;
    int global_idx_offset = 0;
    for (int n = 0; n < draw_data->CmdListsCount; n++)
    {
        const ImDrawList* cmd_list = draw_data->CmdLists[n];
        bool excluded = false;
        for (int e = 0; e < exclude_count && !excluded; e++)
            excluded = exclude_draw_lists[e] != NULL && cmd_list == exclude_draw_lists[e];
        bool included = only_count == 0;
        for (int o = 0; o < only_count && !included; o++)
            included = only_draw_lists[o] != NULL && cmd_list == only_draw_lists[o];
        if (!included || excluded)
        {
            // Skipped, but its vertices are still in the shared buffer ahead of
            // the list we do want, so the offsets have to walk past them.
            global_idx_offset += cmd_list->IdxBuffer.Size;
            global_vtx_offset += cmd_list->VtxBuffer.Size;
            continue;
        }

        for (int cmd_i = 0; cmd_i < cmd_list->CmdBuffer.Size; cmd_i++)
        {
            const ImDrawCmd* pcmd = &cmd_list->CmdBuffer[cmd_i];
            if (pcmd->UserCallback != NULL)
            {
                // User callback, registered via ImDrawList::AddCallback()
                // (ImDrawCallback_ResetRenderState is a special callback value used by the user to request the renderer to reset render state.)
                if (pcmd->UserCallback == ImDrawCallback_ResetRenderState)
                    ImGui_ImplGX2_SetupRenderState(draw_data, fb_width, fb_height);
                else
                    pcmd->UserCallback(cmd_list, pcmd);
            }
            else
            {
                // Project scissor/clipping rectangles into framebuffer space
                ImVec2 clip_min((pcmd->ClipRect.x - clip_off.x) * clip_scale.x, (pcmd->ClipRect.y - clip_off.y) * clip_scale.y);
                ImVec2 clip_max((pcmd->ClipRect.z - clip_off.x) * clip_scale.x, (pcmd->ClipRect.w - clip_off.y) * clip_scale.y);
                // Clamp to the framebuffer, then discard only what is degenerate.
                // Rejecting anything that merely reaches the edge is not safe here:
                // when this draw data is scaled to a smaller target, a full-width
                // clip rect can acquire a tiny floating-point overshoot at the
                // edge when 1920x1080 draw data is scaled to 854x480.
                if (clip_min.x < 0.0f) clip_min.x = 0.0f;
                if (clip_min.y < 0.0f) clip_min.y = 0.0f;
                if (clip_max.x > (float)fb_width)  clip_max.x = (float)fb_width;
                if (clip_max.y > (float)fb_height) clip_max.y = (float)fb_height;
                if (clip_max.x <= clip_min.x || clip_max.y <= clip_min.y || !pcmd->ElemCount)
                    continue;

                // Apply scissor/clipping rectangle
                GX2SetScissor((uint32_t)clip_min.x, (uint32_t)clip_min.y, (uint32_t)(clip_max.x - clip_min.x), (uint32_t)(clip_max.y - clip_min.y));

                // Bind texture, Draw
                ImGui_ImplGX2_Texture* tex = (ImGui_ImplGX2_Texture*) pcmd->GetTexID();
                IM_ASSERT(tex && "TextureID cannot be NULL");

                GX2SetPixelTexture(tex->Texture, 0);
                GX2SetPixelSampler(tex->Sampler, 0);

                GX2DrawIndexedEx(GX2_PRIMITIVE_MODE_TRIANGLES, pcmd->ElemCount,
                    sizeof(ImDrawIdx) == 2 ? GX2_INDEX_TYPE_U16 : GX2_INDEX_TYPE_U32,
                    (uint8_t*) bd->IndexBuffer + (pcmd->IdxOffset + global_idx_offset) * sizeof(ImDrawIdx),
                    global_vtx_offset + pcmd->VtxOffset, 1);
            }
        }
        global_idx_offset += cmd_list->IdxBuffer.Size;
        global_vtx_offset += cmd_list->VtxBuffer.Size;
    }
}

bool ImGui_ImplGX2_CreateFontsTexture()
{
    ImGuiIO& io = ImGui::GetIO();
    ImGui_ImplGX2_Data* bd = ImGui_ImplGX2_GetBackendData();

    if (bd->FontTexture)
    {
        return false;
    }

    // Build texture atlas
    unsigned char* src_pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&src_pixels, &width, &height);   // Load as RGBA 32-bit (75% of the memory is wasted, but default font is so small) because it is more likely to be compatible with user's existing shaders.

    bd->FontTexture = IM_NEW(ImGui_ImplGX2_Texture)();

    GX2Texture* tex = IM_NEW(GX2Texture)();
    memset(tex, 0, sizeof(GX2Texture));
    bd->FontTexture->Texture = tex;

    tex->surface.dim = GX2_SURFACE_DIM_TEXTURE_2D;
    tex->surface.use = GX2_SURFACE_USE_TEXTURE;
    tex->surface.width = width;
    tex->surface.height = height;
    tex->surface.depth = 1;
    tex->surface.mipLevels = 1;
    tex->surface.format = GX2_SURFACE_FORMAT_SRGB_R8_G8_B8_A8;
    tex->surface.aa = GX2_AA_MODE1X;
    tex->surface.tileMode = GX2_TILE_MODE_LINEAR_ALIGNED;
    tex->viewNumSlices = 1;
    tex->viewNumMips = 1;
    // swapped for endianness
    tex->compMap = GX2_COMP_MAP(GX2_SQ_SEL_A, GX2_SQ_SEL_B, GX2_SQ_SEL_G, GX2_SQ_SEL_R);

    if (!IMGUI_IMPL_GX2_CREATE_SURFACE(&tex->surface, GX2R_RESOURCE_BIND_TEXTURE | GX2R_RESOURCE_USAGE_CPU_WRITE | GX2R_RESOURCE_USAGE_GPU_READ))
    {
        IM_DELETE(bd->FontTexture->Texture);
        IM_DELETE(bd->FontTexture);
        bd->FontTexture = NULL;
        return false;
    }
    GX2InitTextureRegs(tex);

    unsigned char* dst_pixels = (unsigned char*) IMGUI_IMPL_GX2_LOCK_SURFACE(&tex->surface, 0, GX2R_RESOURCE_BIND_NONE);
    if (!dst_pixels)
    {
        IMGUI_IMPL_GX2_DESTROY_SURFACE(&tex->surface, GX2R_RESOURCE_BIND_NONE);
        IM_DELETE(bd->FontTexture->Texture);
        IM_DELETE(bd->FontTexture);
        bd->FontTexture = NULL;
        return false;
    }

    for (int y = 0; y < height; y++) {
        memcpy(dst_pixels + (y * tex->surface.pitch * 4), src_pixels + (y * width * 4), width * 4);
    }

    // Read the texel every filled rectangle samples back out of the mapping
    // before handing it over. If this comes back white the upload landed and
    // anything still missing on screen is the GPU's view of the memory, not
    // the memory.
    const int white_x = (int)(io.Fonts->TexUvWhitePixel.x * (float)width);
    const int white_y = (int)(io.Fonts->TexUvWhitePixel.y * (float)height);
    const unsigned int white_texel =
        *(const unsigned int*)(dst_pixels + ((size_t)white_y * tex->surface.pitch + white_x) * 4);
    const unsigned int first_texel = *(const unsigned int*)dst_pixels;

    IMGUI_IMPL_GX2_UNLOCK_SURFACE(&tex->surface, 0, GX2R_RESOURCE_BIND_NONE);

    // GX2R is supposed to do this on unlock. Saying it plainly costs one
    // call at startup and removes the question.
    GX2Invalidate((GX2InvalidateMode)(GX2_INVALIDATE_MODE_CPU | GX2_INVALIDATE_MODE_TEXTURE),
                  tex->surface.image, tex->surface.imageSize);

    OSReport("[imgui_impl_gx2] atlas upload: white texel at %d,%d = %08X, first = %08X\n",
             white_x, white_y, white_texel, first_texel);

    bd->FontTexture->Sampler = IM_NEW(GX2Sampler)();
    GX2InitSampler(bd->FontTexture->Sampler, GX2_TEX_CLAMP_MODE_CLAMP, GX2_TEX_XY_FILTER_MODE_LINEAR);

    // Store our identifier
    io.Fonts->SetTexID((ImTextureID) bd->FontTexture);

    return true;
}

void ImGui_ImplGX2_DestroyFontsTexture()
{
    ImGuiIO& io = ImGui::GetIO();
    ImGui_ImplGX2_Data* bd = ImGui_ImplGX2_GetBackendData();
    if (bd->FontTexture)
    {
        if (bd->FontTexture->Texture)
            IMGUI_IMPL_GX2_DESTROY_SURFACE(&bd->FontTexture->Texture->surface, GX2R_RESOURCE_BIND_NONE);
        io.Fonts->SetTexID(0);
        IM_DELETE(bd->FontTexture->Texture);
        IM_DELETE(bd->FontTexture->Sampler);
        IM_DELETE(bd->FontTexture);
        bd->FontTexture = NULL;
    }
}

bool    ImGui_ImplGX2_CreateDeviceObjects()
{
    ImGui_ImplGX2_Data* bd = ImGui_ImplGX2_GetBackendData();
    if (!bd)
        return false;
    if (bd->ShaderGroup)
        return true;

    bd->ShaderGroup = IM_NEW(WHBGfxShaderGroup)();

    if (!IMGUI_IMPL_GX2_LOAD_SHADER_GROUP(bd->ShaderGroup, 0, shader_gsh))
    {
        IM_DELETE(bd->ShaderGroup);
        bd->ShaderGroup = NULL;
        return false;
    }

    WHBGfxInitShaderAttribute(bd->ShaderGroup, "Position", 0, 0, GX2_ATTRIB_FORMAT_FLOAT_32_32);
    WHBGfxInitShaderAttribute(bd->ShaderGroup, "UV", 0, 8, GX2_ATTRIB_FORMAT_FLOAT_32_32);
    WHBGfxInitShaderAttribute(bd->ShaderGroup, "Color", 0, 16, GX2_ATTRIB_TYPE_8_8_8_8);

    if (!IMGUI_IMPL_GX2_INIT_FETCH_SHADER(bd->ShaderGroup))
    {
        IMGUI_IMPL_GX2_FREE_SHADER_GROUP(bd->ShaderGroup);
        IM_DELETE(bd->ShaderGroup);
        bd->ShaderGroup = NULL;
        return false;
    }

    if (!ImGui_ImplGX2_CreateFontsTexture())
    {
        IMGUI_IMPL_GX2_FREE_SHADER_GROUP(bd->ShaderGroup);
        IM_DELETE(bd->ShaderGroup);
        bd->ShaderGroup = NULL;
        return false;
    }

    return true;
}

void    ImGui_ImplGX2_DestroyDeviceObjects()
{
    ImGui_ImplGX2_Data* bd = ImGui_ImplGX2_GetBackendData();
    if (!bd)
        return;

    if (bd->VertexBuffer) {
        IMGUI_IMPL_GX2_FREE(bd->VertexBuffer);
        bd->VertexBuffer = NULL;
    }
    bd->VertexBufferSize = 0;

    if (bd->IndexBuffer) {
        IMGUI_IMPL_GX2_FREE(bd->IndexBuffer);
        bd->IndexBuffer = NULL;
    }
    bd->IndexBufferSize = 0;
    bd->UploadedDrawData = NULL;
    bd->UploadedVertexSize = 0;
    bd->UploadedIndexSize = 0;

    if (bd->ShaderGroup) {
        IMGUI_IMPL_GX2_FREE_SHADER_GROUP(bd->ShaderGroup);
        IM_DELETE(bd->ShaderGroup);
        bd->ShaderGroup = NULL;
    }

    ImGui_ImplGX2_DestroyFontsTexture();
}
