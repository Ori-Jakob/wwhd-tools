#pragma once

namespace Ui {
namespace Panels {
void RegisterControls();
void RegisterCheatControls();

void DrawTools();
void DrawMods();
void DrawSettings();
void DrawResetConfirm();

void DrawHotkeysButton();
void DrawHotkeysWindow();

void DrawControlsButton();
void DrawControlsWindow();

void DrawInventoryItem();
void DrawInventoryWindow();

void DrawSaveStatesItem();
void DrawSaveStatesWindow();

void DrawSaveLoaderItem();
void DrawSaveLoaderWindow();

#ifdef WWHD_TOOLS_DEBUG
void DrawDiagnostics();
#endif
}
}
