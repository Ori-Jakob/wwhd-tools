#pragma once

#include <stdint.h>

#include <rplloader/rplloader.h>

namespace App {
extern const RplHook  gHooks[];
extern const uint32_t gHookCount;

bool RegisterDynamicHooks(const RplHost* host);

// Adds or removes a one-word code patch now. False under Cemu, where code patches come from the pack.
bool SetCodePatch(const RplHook* hook, bool on);
}
