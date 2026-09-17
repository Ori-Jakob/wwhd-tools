#pragma once

namespace Config {
void Poll();
void MarkDirty();

void Flush();

bool IsLoaded();
const char* LastError();

void OnApplicationStart();
}
