#pragma once

#include <stddef.h>
#include <stdint.h>

namespace Ui {
namespace Field {
bool Int(const char* label, int* value, int step = 1, int stepFast = 100);
bool Float(const char* label, float* value, const char* format = "%.3f");
bool U8(const char* label, uint8_t* value);
bool U16(const char* label, uint16_t* value);
bool U32(const char* label, uint32_t* value, const char* format = nullptr);

bool SliderInt(const char* label, int* value, int minValue, int maxValue,
               const char* format = "%d");
bool SliderFloat(const char* label, float* value, float minValue, float maxValue,
                 const char* format = "%.2f", int flags = 0);

bool IsSteeringSlider();

bool Text(const char* label, char* buffer, size_t size);
bool TextWithHint(const char* label, const char* hint, char* buffer, size_t size);
}
}
