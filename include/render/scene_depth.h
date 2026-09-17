#pragma once

namespace SceneDepth {
bool Prepare(bool wanted);
bool Available();

void Bind(float sign, float bias);

void Reset();
const char* Summary();
}
