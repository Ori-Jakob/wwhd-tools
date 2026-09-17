#pragma once

namespace Ui {
namespace Control {
enum Surface {
    SURFACE_MENU = 0,
    SURFACE_QUICK_ACCESS,
};

struct Descriptor;

typedef bool (*DrawCallback)(const Descriptor* descriptor, Surface surface);
typedef void (*ApplyCallback)(const Descriptor* descriptor);

struct Descriptor {
    const char*   id;
    const char*   name;
    const char*   path;
    DrawCallback  primary;
    DrawCallback  companion;
    ApplyCallback immediateApply;
    int           token;
};

static const int MAX_CONTROLS = 64;

bool Register(const Descriptor& descriptor);

int Count();
const Descriptor* At(int index);
const Descriptor* Find(const char* id);

bool Draw(const char* id, Surface surface);
bool Draw(const Descriptor* descriptor, Surface surface);
}
}
