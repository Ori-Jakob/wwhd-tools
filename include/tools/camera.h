#pragma once

namespace Tools {
namespace Camera {
void Tick();
void OnCameraRun(void* camera);

// Vertical fovy the game produced last frame and what was shown; 0 until the camera ran.
float GameFov();
float ShownFov();

void OnApplicationStart();
void OnApplicationEnd();
}
}
