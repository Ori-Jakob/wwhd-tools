#pragma once

#include "libwwhd/libwwhd.h"

namespace Cheats {
namespace Text {
bool AutoAdvanceEnabled();
void SetAutoAdvanceEnabled(bool enabled);

void Tick(bool acceptInput);
void OnBoxInput(dMsgBox_c* box);
void OnFrameEarly();
void ResetToDefaults();
}
}
