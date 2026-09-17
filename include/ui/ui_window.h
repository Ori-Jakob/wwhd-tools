#pragma once

namespace Ui {
namespace Window {
void RequestFocus(const char* windowName);
void ResolveFocusRequest();
void ClearFocusRequest();
bool HasPendingFocus();

void SetCycleFocus(const char* windowName);
const char* CycleFocus();

bool Checkbox(const char* label, bool* open, const char* windowName);
bool Button(const char* label, bool* open, const char* windowName);
}
}
