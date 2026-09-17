#include "ui/ui_field.h"

#include "core/input.h"
#include "ui/osk.h"

#include "imgui.h"
#include "imgui_internal.h"

namespace Ui {
namespace Field {
static const int kRepeatFirstFrames = 9;

struct RepeatStage {
    int repeatsBefore;
    int intervalFrames;
    int multiplier;
};
static const RepeatStage kRepeatStages[] = {
    {  0, 4, 1 },
    {  6, 3, 1 },
    { 14, 2, 2 },
    { 26, 2, 4 },
};
static const int kRepeatStageCount =
    (int)(sizeof(kRepeatStages) / sizeof(kRepeatStages[0]));

static const RepeatStage& stageFor(int repeats)
{
    int i = kRepeatStageCount - 1;
    while (i > 0 && repeats < kRepeatStages[i].repeatsBefore)
        --i;
    return kRepeatStages[i];
}

static ImGuiID  s_repeatItem = 0;
static uint32_t s_repeatDir = 0;
static int      s_repeatDelay = 0;
static int      s_repeatCount = 0;

static const int kDoubleTapFrames = 12;
static ImGuiID s_lastTapItem = 0;
static int     s_lastTapFrame = -1000;

static ImGuiID s_navSliderItem = 0;
static int     s_navSliderFrame = -1000;

bool IsSteeringSlider()
{
    if (!s_navSliderItem)
        return false;
    if (ImGui::GetFrameCount() - s_navSliderFrame > 1)
        return false;
    return (Input::Current().held & Input::BTN_A) != 0;
}

static void cancelDoubleTap(ImGuiID id)
{
    if (s_lastTapItem == id)
        s_lastTapItem = 0;
}

static bool takeDoubleTap(ImGuiID id)
{
    ImGuiContext* g = ImGui::GetCurrentContext();
    if (!g || !id || g->NavId != id)
        return false;
    if (!(Input::Current().pressed & Input::BTN_A))
        return false;

    const int frame = ImGui::GetFrameCount();
    const bool isDouble = s_lastTapItem == id &&
                          (frame - s_lastTapFrame) <= kDoubleTapFrames;
    s_lastTapItem = id;
    s_lastTapFrame = frame;

    if (isDouble)
        s_lastTapItem = 0;
    return isDouble;
}

static void noteNavSlider(ImGuiID id)
{
    ImGuiContext* g = ImGui::GetCurrentContext();
    if (g && id && g->NavId == id) {
        s_navSliderItem = id;
        s_navSliderFrame = ImGui::GetFrameCount();
    } else if (s_navSliderItem == id) {
        s_navSliderItem = 0;
    }
}

static void registerField(int kind, int maxChars = 0)
{
    const ImGuiID id = ImGui::GetItemID();
    if (id)
        Osk::RegisterField((unsigned)id, kind, maxChars);
}

static void releaseNavActivation()
{
    ImGuiContext* g = ImGui::GetCurrentContext();
    if (g && g->ActiveId == ImGui::GetItemID() &&
        g->ActiveIdSource != ImGuiInputSource_Mouse)
        ImGui::ClearActiveID();
}

static int sliderStep()
{
    ImGuiContext* g = ImGui::GetCurrentContext();
    const ImGuiID id = ImGui::GetItemID();
    if (!g || !id || g->NavId != id)
        return 0;

    const Input::Snapshot& in = Input::Current();
    const uint32_t negative = in.held & (Input::BTN_LEFT | Input::BTN_L);
    const uint32_t positive = in.held & (Input::BTN_RIGHT | Input::BTN_R);
    const uint32_t dirs = negative | positive;

    const bool steering = (in.held & Input::BTN_A) &&
                          (negative != 0) != (positive != 0);
    if (!steering) {
        if (s_repeatItem == id) {
            s_repeatItem = 0;
            s_repeatDir = 0;
            s_repeatCount = 0;
        }
        return 0;
    }

    int units;
    if (s_repeatItem == id && s_repeatDir == dirs) {
        if (--s_repeatDelay > 0)
            return 0;
        const RepeatStage& stage = stageFor(s_repeatCount);
        s_repeatDelay = stage.intervalFrames;
        units = (dirs & (Input::BTN_L | Input::BTN_R)) ? 10 : stage.multiplier;
        ++s_repeatCount;
    } else {
        s_repeatItem = id;
        s_repeatDir = dirs;
        s_repeatDelay = kRepeatFirstFrames;
        s_repeatCount = 0;
        units = (dirs & (Input::BTN_L | Input::BTN_R)) ? 10 : 1;
    }
    return positive ? units : -units;
}

bool Int(const char* label, int* value, int step, int stepFast)
{
    const bool changed = ImGui::InputInt(label, value, step, stepFast);
    registerField(Osk::FIELD_INT);
    return changed;
}

bool Float(const char* label, float* value, const char* format)
{
    const bool changed = ImGui::InputFloat(label, value, 0.0f, 0.0f, format);
    registerField(Osk::FIELD_FLOAT);
    return changed;
}

bool U8(const char* label, uint8_t* value)
{
    const bool changed = ImGui::InputScalar(label, ImGuiDataType_U8, value);
    registerField(Osk::FIELD_INT);
    return changed;
}

bool U16(const char* label, uint16_t* value)
{
    const bool changed = ImGui::InputScalar(label, ImGuiDataType_U16, value);
    registerField(Osk::FIELD_INT);
    return changed;
}

bool U32(const char* label, uint32_t* value, const char* format)
{
    const bool hex = format && format[0];
    const bool changed = ImGui::InputScalar(label, ImGuiDataType_U32, value,
                                            nullptr, nullptr, format);
    registerField(hex ? Osk::FIELD_HEX : Osk::FIELD_INT);
    return changed;
}

bool SliderInt(const char* label, int* value, int minValue, int maxValue,
               const char* format)
{
    const ImGuiID id = ImGui::GetID(label);
    const bool toText = takeDoubleTap(id);

    bool changed = ImGui::SliderInt(label, value, minValue, maxValue, format,
                                    toText ? 0 : ImGuiSliderFlags_NoInput);
    registerField(Osk::FIELD_INT);
    noteNavSlider(id);
    if (!toText)
        releaseNavActivation();

    const int step = ImGui::TempInputIsActive(id) ? 0 : sliderStep();
    if (step) {
        cancelDoubleTap(id);
        int next = *value + step;
        if (next < minValue) next = minValue;
        if (next > maxValue) next = maxValue;
        if (next != *value) {
            *value = next;
            changed = true;
        }
    }
    return changed;
}

bool SliderFloat(const char* label, float* value, float minValue, float maxValue,
                 const char* format, int flags)
{
    const ImGuiID id = ImGui::GetID(label);
    const bool toText = takeDoubleTap(id);

    ImGuiSliderFlags sliderFlags = (ImGuiSliderFlags)flags;
    if (!toText)
        sliderFlags |= ImGuiSliderFlags_NoInput;

    bool changed = ImGui::SliderFloat(label, value, minValue, maxValue, format,
                                      sliderFlags);
    registerField(Osk::FIELD_FLOAT);
    noteNavSlider(id);
    if (!toText)
        releaseNavActivation();

    const int step = ImGui::TempInputIsActive(id) ? 0 : sliderStep();
    if (step) {
        cancelDoubleTap(id);

        const float delta = (maxValue - minValue) * 0.01f;
        float next = *value + (float)step * delta;
        if (next < minValue) next = minValue;
        if (next > maxValue) next = maxValue;
        if (next != *value) {
            *value = next;
            changed = true;
        }
    }
    return changed;
}

bool Text(const char* label, char* buffer, size_t size)
{
    const bool changed = ImGui::InputText(label, buffer, size);
    registerField(Osk::FIELD_TEXT, (int)size - 1);
    return changed;
}

bool TextWithHint(const char* label, const char* hint, char* buffer, size_t size)
{
    const bool changed = ImGui::InputTextWithHint(label, hint, buffer, size);
    registerField(Osk::FIELD_TEXT, (int)size - 1);
    return changed;
}
}
}
