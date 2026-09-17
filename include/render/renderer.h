#pragma once

struct GX2ColorBuffer;
struct ImFont;

namespace Renderer {
bool IsReady();
void Init(float logicalWidth, float logicalHeight);
void NewFrame(float logicalWidth, float logicalHeight, float deltaTime);
void FinishFrame();

// The caller shrinks the logical size by this; the atlas is rasterised larger to match.
void SetUiScale(float scale);
float UiScale();

void DrawPrepared(GX2ColorBuffer* target, bool withWorldLayer, bool withHud);
void DrawPreparedTopLayer(GX2ColorBuffer* target);
void DrawGamePadOnlyLayer(GX2ColorBuffer* target);

void SetGamePadOnlyList(const void* drawList);

// Call between Begin and End; keeps that window off screens the game is not on.
void MarkGameScreenOnly(const void* drawList);

bool HasTopLayerContent();
bool HasGamePadOnlyContent();
bool HasWorldContent();
bool HasGameScreenListContent();
// The world layer and, with withHud, the marked HUD lists: a screen showing the game but not the menu.
void DrawGameLayers(GX2ColorBuffer* target, bool withHud);

ImFont* Font();
ImFont* BoldFont();
ImFont* ShadowFont();

void ResetDeviceObjects();
float BackdropAlpha(float displayAlpha);
}
