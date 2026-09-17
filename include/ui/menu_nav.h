#pragma once

#include <stdint.h>

struct ImGuiIO;
namespace Input { struct Snapshot; }

namespace Ui {
namespace Nav {
enum Action {
    NEXT_WINDOW = 0,
    PREVIOUS_WINDOW,
    NEXT_TAB,
    PREVIOUS_TAB,
    FOCUS_MENU_BAR,
    ADJUST_MODIFIER,
    ACTION_COUNT,
};

uint32_t    Binding(Action action);
void        SetBinding(Action action, uint32_t button);
uint32_t    DefaultBinding(Action action);
const char* ActionName(Action action);
const char* ActionHint(Action action);
const char* ConfigKey(Action action);
void        ResetBindings();

bool        Conflicts(Action target, uint32_t buttons, Action other);
const char* Validate(Action target, uint32_t buttons);

// pageOnly: the quick access page has focus without the menu; only stick scrolling runs.
void Update(ImGuiIO& io, const Input::Snapshot& snapshot, bool menuOpen, bool pageOnly);
void RegisterCurrentTabBar();

void RequestMenuBarFocus();
void ApplyMenuBarFocus(ImGuiIO& io);
}
}
