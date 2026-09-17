#pragma once

#include <stdint.h>

namespace Ui {
namespace Panels {
namespace Inventory {
void DrawStatusTab();
void DrawItemsTab();
void DrawBagsTab();
void DrawChartsTab();
void DrawGalleryTab();

bool ComboById(const char* label, uint8_t* value, const uint8_t* ids,
               const char* const* names, int count, float width);
}
}
}
