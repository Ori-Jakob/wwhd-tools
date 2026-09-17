#pragma once

struct ImGuiIO;

namespace Ui {
namespace Osk {
enum FieldKind { FIELD_TEXT = 0, FIELD_INT, FIELD_FLOAT, FIELD_HEX };

void RegisterField(unsigned id, int kind, int maxChars = 0);

bool IsOpen();
bool ProcessInput(ImGuiIO& io);

void Draw();

void OnApplicationStart();
}
}
