#pragma once

#include <stdint.h>

#include <gx2/context.h>
#include <gx2/enum.h>
#include <gx2/surface.h>

namespace App {
namespace Present {
typedef void (*CopyFn)(const GX2ColorBuffer*, GX2ScanTarget);

void Init();
void Shutdown();

void OnGameFrame();
void OnReleaseForeground();
void OnAcquiredForeground();
void NoteContext(GX2ContextState* state);
void OnCopyToScanBuffer(CopyFn copy, const GX2ColorBuffer* buffer, GX2ScanTarget target);
}
}
