#pragma once

namespace Tools {
namespace SaveLoader {
static const int kSlots = 3;

void        RefreshList();
int         Count();
const char* PathAt(int index);

bool        Open(const char* relPath);
const char* Reading();
const char* Opened();

bool        SlotValid(int slot);
const char* SlotLabel(int slot);

bool        LoadSlot(int slot);

bool        IsBusy();
const char* Status();

void Tick();
void OnApplicationStart();
}
}
