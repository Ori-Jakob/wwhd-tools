// dear imgui: Renderer Backend for the Nintendo Wii U using GX2
#pragma once
#include "imgui.h"      // IMGUI_IMPL_API

// GX2 Texture / contains a texture and sampler
// Can be used as a ImTextureID with the GX2 backend
struct ImGui_ImplGX2_Texture
{
    struct GX2Texture* Texture;
    struct GX2Sampler* Sampler;

    ImGui_ImplGX2_Texture() { memset(this, 0, sizeof(*this)); }
};

// Backend API
IMGUI_IMPL_API bool     ImGui_ImplGX2_Init();
IMGUI_IMPL_API void     ImGui_ImplGX2_Shutdown();
IMGUI_IMPL_API void     ImGui_ImplGX2_NewFrame();
// only_draw_lists limits a pass to those lists and exclude_draw_lists drops those from a full
// pass (NULL entries ignored); every pass reads the one shared upload, offsets walk every list.
IMGUI_IMPL_API void     ImGui_ImplGX2_RenderDrawData(ImDrawData* draw_data, const ImDrawList* const* only_draw_lists = NULL, int only_count = 0, const ImDrawList* const* exclude_draw_lists = NULL, int exclude_count = 0);

// (Optional) Called by Init/NewFrame/Shutdown
IMGUI_IMPL_API bool     ImGui_ImplGX2_CreateFontsTexture();
IMGUI_IMPL_API void     ImGui_ImplGX2_DestroyFontsTexture();
IMGUI_IMPL_API bool     ImGui_ImplGX2_CreateDeviceObjects();
IMGUI_IMPL_API void     ImGui_ImplGX2_DestroyDeviceObjects();

// True once CreateDeviceObjects has succeeded (shaders + font texture live).
// RenderDrawData silently no-ops without them; callers can surface that state.
IMGUI_IMPL_API bool     ImGui_ImplGX2_DeviceObjectsCreated();
