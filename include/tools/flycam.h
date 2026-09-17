#pragma once

namespace Tools {
namespace FlyCam {
bool IsEnabled();
void SetEnabled(bool enabled);

bool IsActive();

void Tick(bool acceptInput);
void OnFrameEarly();

void OnApplicationStart();
void OnApplicationEnd();
}
}
