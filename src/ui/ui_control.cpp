#include "ui/ui_control.h"

#include "core/logger.h"

#include "imgui.h"

#include <string.h>

namespace Ui {
namespace Control {
static Descriptor s_controls[MAX_CONTROLS];
static int        s_count = 0;

bool Register(const Descriptor& descriptor)
{
    if (!descriptor.id || !descriptor.id[0] || !descriptor.primary)
        return false;
    if (Find(descriptor.id)) {
        Logger::LogWarn("[ui] duplicate control id '%s'", descriptor.id);
        return false;
    }
    if (s_count >= MAX_CONTROLS) {
        Logger::LogError("[ui] control registry full (%d)", MAX_CONTROLS);
        return false;
    }
    s_controls[s_count++] = descriptor;
    return true;
}

int Count() { return s_count; }

const Descriptor* At(int index)
{
    return index >= 0 && index < s_count ? &s_controls[index] : nullptr;
}

const Descriptor* Find(const char* id)
{
    if (!id)
        return nullptr;
    for (int i = 0; i < s_count; ++i)
        if (strcmp(s_controls[i].id, id) == 0)
            return &s_controls[i];
    return nullptr;
}

bool Draw(const Descriptor* descriptor, Surface surface)
{
    if (!descriptor || !descriptor->primary)
        return false;

    ImGui::PushID((int)surface);
    ImGui::PushID(descriptor->id);
    bool changed = descriptor->primary(descriptor, surface);
    if (descriptor->companion)
        changed |= descriptor->companion(descriptor, surface);
    if (changed && descriptor->immediateApply)
        descriptor->immediateApply(descriptor);
    ImGui::PopID();
    ImGui::PopID();
    return changed;
}

bool Draw(const char* id, Surface surface)
{
    return Draw(Find(id), surface);
}
}
}
