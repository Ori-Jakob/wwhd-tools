#pragma once

#include <stdint.h>

namespace Ui {
namespace QuickAccess {
static const int MAX_ITEMS = 32;

extern const char FULL_WINDOW_NAME[];
extern const char STANDALONE_WINDOW_NAME[];

bool IsOpen();
void SetOpen(bool open);

bool IsPageFocused();
void OnHotkey(bool menuOpen);
bool OnMenuOpened();

int         ItemCount();
const char* ItemId(int index);
void        ClearItems();
bool        AddItem(const char* id);

void DrawMenuItem();
void DrawFullWindow();
void DrawPageWindow(bool menuOpen);

bool HandleBack();

void ResetToDefaults();
void OnApplicationStart();
}
}
