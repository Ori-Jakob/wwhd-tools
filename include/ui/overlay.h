#pragma once

struct GX2ColorBuffer;

namespace Ui {
namespace Overlay {
enum ScreenContent {
    SCREEN_NOTHING = 0,
    SCREEN_ALL,
    SCREEN_TOP,
    SCREEN_GAME,
};

void Tick();
void OnPadSampled();
bool PrepareFrame(float logicalWidth, float logicalHeight);
void DrawPrepared(GX2ColorBuffer* target, ScreenContent content, bool isTv);
ScreenContent ContentForScreen(bool isTv);

bool HasContentFor(ScreenContent content, bool isTv);

bool IsMenuOpen();
void SetMenuOpen(bool open);

void OnApplicationStart();
void OnApplicationEnd();
}
}
