#pragma once

#include <stdint.h>

namespace Tools {
namespace Mss {
bool IsEnabled();
void SetEnabled(bool enabled);

bool NextStick(uint32_t held, float* x, float* y);

void OnApplicationStart();
}
}
