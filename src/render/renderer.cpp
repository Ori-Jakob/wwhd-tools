#include "render/renderer.h"

#include <stdint.h>
#include <coreinit/debug.h>
#include <gx2/event.h>
#include <gx2/registers.h>
#include <gx2/surface.h>
#include <gx2/texture.h>
#include <gx2/shaders.h>

#include "render/image.h"
#include "render/scene_depth.h"
#include "imgui.h"
#include "imgui_impl_gx2.h"
#include "core/logger.h"

#include <math.h>

namespace Renderer {
static bool s_ready = false;
static const ImDrawList* s_gamePadOnly = nullptr;
static bool s_deviceFailureLogged = false;
static ImFont* s_font = nullptr;
static ImFont* s_fontBold = nullptr;
static ImFont* s_fontShadow = nullptr;

static const float kAtlasFontSize = 26.0f;
static const float kBoldWeight = 1.7f;
static const float kShadowWeight = 2.0f;

bool IsReady()
{
    return s_ready;
}

ImFont* Font()
{
    return s_font;
}

ImFont* BoldFont()
{
    return s_fontBold ? s_fontBold : s_font;
}

ImFont* ShadowFont()
{
    return s_fontShadow ? s_fontShadow : BoldFont();
}

static uint8_t s_toLinear[256];

static void buildLinearTable()
{
    for (int i = 0; i < 256; ++i) {
        const float c = (float)i / 255.0f;
        const float l = c <= 0.04045f ? c / 12.92f
                                      : powf((c + 0.055f) / 1.055f, 2.4f);
        s_toLinear[i] = (uint8_t)(l * 255.0f + 0.5f);
    }
}

static void linearizeDrawData(ImDrawData* drawData)
{
    for (int n = 0; n < drawData->CmdListsCount; ++n) {
        ImDrawVert* v = drawData->CmdLists[n]->VtxBuffer.Data;
        const int count = drawData->CmdLists[n]->VtxBuffer.Size;
        for (int i = 0; i < count; ++i) {
            const ImU32 c = v[i].col;
            v[i].col = (c & IM_COL32_A_MASK)
                     | ((ImU32)s_toLinear[(c >> IM_COL32_R_SHIFT) & 0xFFu] << IM_COL32_R_SHIFT)
                     | ((ImU32)s_toLinear[(c >> IM_COL32_G_SHIFT) & 0xFFu] << IM_COL32_G_SHIFT)
                     | ((ImU32)s_toLinear[(c >> IM_COL32_B_SHIFT) & 0xFFu] << IM_COL32_B_SHIFT);
        }
    }
}

float BackdropAlpha(float displayAlpha)
{
    if (displayAlpha <= 0.0f)
        return 0.0f;
    if (displayAlpha >= 1.0f)
        return 1.0f;
    return 1.0f - powf(1.0f - displayAlpha, 2.2f);
}

static float s_uiScale = 1.0f;
static float s_wantScale = 1.0f;

// Rasterised scale times larger, laid out at 1x: sharp where a logical pixel is scale pixels.
static void buildFonts(float scale)
{
    ImGuiIO& io = ImGui::GetIO();
    io.Fonts->Clear();

    ImFontConfig cfg;
    cfg.SizePixels  = kAtlasFontSize * scale;
    cfg.OversampleH = 1;
    cfg.OversampleV = 1;
    cfg.PixelSnapH  = true;
    s_font = io.Fonts->AddFontDefault(&cfg);

    ImFontConfig boldCfg = cfg;
    boldCfg.RasterizerMultiply = kBoldWeight;
    s_fontBold = io.Fonts->AddFontDefault(&boldCfg);

    ImFontConfig shadowCfg = cfg;
    shadowCfg.RasterizerMultiply = kShadowWeight;
    s_fontShadow = io.Fonts->AddFontDefault(&shadowCfg);

    io.FontDefault = s_font;
    io.FontGlobalScale = 1.0f / scale;
}

void SetUiScale(float scale)
{
    s_wantScale = scale < 1.0f ? 1.0f : scale;
}

float UiScale()
{
    return s_uiScale;
}

void Init(float logicalWidth, float logicalHeight)
{
    if (s_ready)
        return;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.BackendFlags |= ImGuiBackendFlags_HasGamepad;
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.DisplaySize = ImVec2(logicalWidth, logicalHeight);

    s_uiScale = s_wantScale;
    buildFonts(s_uiScale);

    ImGui::StyleColorsDark();
    buildLinearTable();

    ImGuiStyle& style = ImGui::GetStyle();
    style.Colors[ImGuiCol_WindowBg].w = BackdropAlpha(style.Colors[ImGuiCol_WindowBg].w);
    style.Colors[ImGuiCol_PopupBg].w  = BackdropAlpha(style.Colors[ImGuiCol_PopupBg].w);
    style.ScaleAllSizes(2.0f);

    ImGui_ImplGX2_Init();
    s_ready = true;
    OSReport("[wwhd_tools_rpl] ImGui initialized (%ux%u), font atlas %dx%d\n",
             (unsigned)logicalWidth, (unsigned)logicalHeight,
             io.Fonts->TexWidth, io.Fonts->TexHeight);
}

void NewFrame(float logicalWidth, float logicalHeight, float deltaTime)
{
    ImGuiIO& io = ImGui::GetIO();
    io.DisplaySize = ImVec2(logicalWidth, logicalHeight);
    io.DisplayFramebufferScale = ImVec2(1.0f, 1.0f);
    io.DeltaTime = deltaTime;

    if (s_wantScale != s_uiScale) {
        s_uiScale = s_wantScale;
        // GX2DrawDone first: the last frame's draws may still sample the old atlas.
        const bool live = ImGui_ImplGX2_DeviceObjectsCreated();
        if (live) {
            GX2DrawDone();
            ImGui_ImplGX2_DestroyFontsTexture();
        }
        buildFonts(s_uiScale);
        if (live)
            ImGui_ImplGX2_CreateFontsTexture();
        Logger::Log("UI scale %.2f: atlas rebuilt at %.0f px, %dx%d", s_uiScale,
                    kAtlasFontSize * s_uiScale, io.Fonts->TexWidth, io.Fonts->TexHeight);
    }

    ImGui_ImplGX2_NewFrame();
    if (!ImGui_ImplGX2_DeviceObjectsCreated() && !s_deviceFailureLogged) {
        s_deviceFailureLogged = true;
        Logger::LogError("[wwhd_tools] GX2 device-object creation failed");
    }

    static bool s_atlasLogged = false;
    if (!s_atlasLogged && ImGui_ImplGX2_DeviceObjectsCreated()) {
        s_atlasLogged = true;
        const ImGui_ImplGX2_Texture* ft =
            (const ImGui_ImplGX2_Texture*)io.Fonts->TexID;
        const GX2Surface* fs = (ft && ft->Texture) ? &ft->Texture->surface : 0;
        Logger::Log("atlas %dx%d id=%p tex=%p image=%p pitch=%u size=%u fmt=%u "
                    "tile=%u",
                    io.Fonts->TexWidth, io.Fonts->TexHeight, (void*)io.Fonts->TexID,
                    (void*)(ft ? ft->Texture : 0), fs ? fs->image : 0,
                    fs ? (unsigned)fs->pitch : 0u,
                    fs ? (unsigned)fs->imageSize : 0u,
                    fs ? (unsigned)fs->format : 0u,
                    fs ? (unsigned)fs->tileMode : 0u);
    }
    ImGui::NewFrame();
}

static bool s_topHasContent = false;
static bool s_padHasContent = false;
static bool s_worldHasContent = false;
static bool s_hudHasContent = false;
static const ImDrawList* s_world = nullptr;

static const int kMaxGameScreenLists = 8;
static const ImDrawList* s_gameScreenLists[kMaxGameScreenLists];
static int s_gameScreenCount = 0;
static int s_gameScreenFrame = -1;

static int gameScreenListCount()
{
    return s_gameScreenFrame == ImGui::GetFrameCount() ? s_gameScreenCount : 0;
}

bool HasTopLayerContent()       { return s_topHasContent; }
bool HasGamePadOnlyContent()    { return s_padHasContent; }
bool HasWorldContent()          { return s_worldHasContent; }
bool HasGameScreenListContent() { return s_hudHasContent; }

void FinishFrame()
{
    const ImDrawList* top = ImGui::GetForegroundDrawList();
    s_topHasContent = top && top->VtxBuffer.Size > 0;
    s_padHasContent = s_gamePadOnly && s_gamePadOnly->VtxBuffer.Size > 0;
    s_world = ImGui::GetBackgroundDrawList();
    s_worldHasContent = s_world && s_world->VtxBuffer.Size > 0;
    s_hudHasContent = false;
    for (int i = 0; i < gameScreenListCount() && !s_hudHasContent; ++i)
        s_hudHasContent = s_gameScreenLists[i]->VtxBuffer.Size > 0;

    ImGui::Render();
    ImDrawData* drawData = ImGui::GetDrawData();
    if (!drawData)
        return;
    linearizeDrawData(drawData);

    static int s_frames = 0;
    static int s_peakVtx = 0;
    ++s_frames;
    const bool peak = drawData->TotalVtxCount > s_peakVtx + 64;
    if (peak)
        s_peakVtx = drawData->TotalVtxCount;
    if (s_frames <= 3 || peak || (s_frames % 600) == 0) {
        Logger::Log("frame lists=%d vtx=%d idx=%d gamePadOnly=%p",
                    drawData->CmdListsCount, drawData->TotalVtxCount,
                    drawData->TotalIdxCount, (const void*)s_gamePadOnly);
    }
}

static ImDrawData* bindTarget(GX2ColorBuffer* target)
{
    if (!target)
        return nullptr;

    ImDrawData* drawData = ImGui::GetDrawData();
    if (!drawData || drawData->DisplaySize.x <= 0.0f ||
        drawData->DisplaySize.y <= 0.0f)
        return nullptr;

    drawData->FramebufferScale =
        ImVec2((float)target->surface.width / drawData->DisplaySize.x,
               (float)target->surface.height / drawData->DisplaySize.y);

    GX2SetColorBuffer(target, GX2_RENDER_TARGET_0);
    GX2SetViewport(0.0f, 0.0f,
                   (float)target->surface.width,
                   (float)target->surface.height, 0.0f, 1.0f);
    GX2SetScissor(0, 0, target->surface.width, target->surface.height);

    GX2SetShaderMode(GX2_SHADER_MODE_UNIFORM_REGISTER);
    GX2SetDepthOnlyControl(GX2_FALSE, GX2_FALSE, GX2_COMPARE_FUNC_NEVER);
    GX2SetAlphaTest(GX2_TRUE, GX2_COMPARE_FUNC_GREATER, 0.0f);
    GX2SetColorControl(GX2_LOGIC_OP_COPY, GX2_ENABLE, GX2_DISABLE, GX2_ENABLE);

    return drawData;
}

void SetGamePadOnlyList(const void* drawList)
{
    s_gamePadOnly = (const ImDrawList*)drawList;
}

void MarkGameScreenOnly(const void* drawList)
{
    const int frame = ImGui::GetFrameCount();
    if (frame != s_gameScreenFrame) {
        s_gameScreenFrame = frame;
        s_gameScreenCount = 0;
    }
    if (drawList && s_gameScreenCount < kMaxGameScreenLists)
        s_gameScreenLists[s_gameScreenCount++] = (const ImDrawList*)drawList;
}

// The background list is the world layer; marked HUD lists are dropped off the non-game screen.
void DrawPrepared(GX2ColorBuffer* target, bool withWorldLayer, bool withHud)
{
    ImDrawData* drawData = bindTarget(target);
    static int s_logged = 0;
    if (s_logged < 4) {
        ++s_logged;
        Logger::Log("DrawPrepared target=%p %ux%u bound=%d world=%d hud=%d", (void*)target,
                    target ? (unsigned)target->surface.width : 0u,
                    target ? (unsigned)target->surface.height : 0u,
                    (int)(drawData != nullptr), (int)withWorldLayer, (int)withHud);
    }
    if (!drawData)
        return;
    const ImDrawList* exclude[2 + kMaxGameScreenLists];
    int count = 0;
    exclude[count++] = s_gamePadOnly;
    if (!withWorldLayer)
        exclude[count++] = s_world;
    if (!withHud)
        for (int i = 0; i < gameScreenListCount(); ++i)
            exclude[count++] = s_gameScreenLists[i];
    ImGui_ImplGX2_RenderDrawData(drawData, nullptr, 0, exclude, count);
}

void DrawGameLayers(GX2ColorBuffer* target, bool withHud)
{
    ImDrawData* drawData = bindTarget(target);
    if (!drawData)
        return;
    const ImDrawList* only[1 + kMaxGameScreenLists];
    int count = 0;
    only[count++] = s_world;
    if (withHud)
        for (int i = 0; i < gameScreenListCount(); ++i)
            only[count++] = s_gameScreenLists[i];
    ImGui_ImplGX2_RenderDrawData(drawData, only, count, nullptr, 0);
}

void DrawPreparedTopLayer(GX2ColorBuffer* target)
{
    ImDrawData* drawData = bindTarget(target);
    if (!drawData)
        return;
    const ImDrawList* only[1] = { ImGui::GetForegroundDrawList() };
    ImGui_ImplGX2_RenderDrawData(drawData, only, 1, nullptr, 0);
}

void DrawGamePadOnlyLayer(GX2ColorBuffer* target)
{
    if (!s_gamePadOnly)
        return;
    ImDrawData* drawData = bindTarget(target);
    if (!drawData)
        return;
    const ImDrawList* only[1] = { s_gamePadOnly };
    ImGui_ImplGX2_RenderDrawData(drawData, only, 1, nullptr, 0);
}

void ResetDeviceObjects()
{
    s_deviceFailureLogged = false;
    Image::DestroyAll();
    SceneDepth::Reset();
    if (!s_ready || !ImGui::GetCurrentContext())
        return;
    ImGui_ImplGX2_DestroyDeviceObjects();
}
}
