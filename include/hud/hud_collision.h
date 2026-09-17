#pragma once

namespace Hud {
namespace Collision {
void OnBeforeMove(void* ccs);
void OnAfterMove(void* ccs);

void OnMassCheck(void* mng, const void* pos, unsigned result);

void OnCameraRun(void* camera);

void OnFrameEnd();

void Draw(float logicalWidth, float logicalHeight);

int         ShapeCount();
int         MassCount();
const char* DepthInfo();
const char* HiddenReason();
}
}
