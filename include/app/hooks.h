#pragma once

#include <stdint.h>

#include <rplloader/rplloader.h>

namespace App {
extern const RplHook  gHooks[];
extern const uint32_t gHookCount;

bool RegisterDynamicHooks(const RplHost* host);
}
