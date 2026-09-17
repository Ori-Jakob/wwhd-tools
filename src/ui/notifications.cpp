#include "ui/notifications.h"

#include "imgui.h"
#include "core/settings.h"
#include "render/renderer.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

namespace Notifications {
static const int kMaxVisible = 4;
static const int kKeySize = 32;
static const int kTitleSize = 48;
static const int kMessageSize = 192;
static const float kFadeInSeconds = 0.12f;
static const float kFadeOutSeconds = 0.45f;
static const float kStackGap = 6.0f;
static const float kBorderW = 2.0f;
static const float kRounding = 6.0f;

static const char kWindowPrefix[] = "##wwhd_action_toast_";

struct Slot {
    char  key[kKeySize];
    char  title[kTitleSize];
    char  text[kMessageSize];
    Kind  kind;
    bool  sticky;
    float remaining;
    float age;
};

static Slot s_slots[kMaxVisible] = {};
static int  s_count = 0;

void Clear()
{
    s_count = 0;
    memset(s_slots, 0, sizeof(s_slots));
}

static ImVec4 kindColor(Kind kind)
{
    switch (kind) {
    case Success: return ImVec4(64.0f / 255.0f, 207.0f / 255.0f, 142.0f / 255.0f, 1.0f);
    case Warning: return ImVec4(255.0f / 255.0f, 196.0f / 255.0f, 64.0f / 255.0f, 1.0f);
    case Error:   return ImVec4(255.0f / 255.0f, 104.0f / 255.0f, 96.0f / 255.0f, 1.0f);
    case Info:
    default:      return ImVec4(96.0f / 255.0f, 168.0f / 255.0f, 255.0f / 255.0f, 1.0f);
    }
}

static float holdSeconds(Kind kind)
{
    float hold = Config::g_settings.toastSeconds;
    if (hold < 0.5f)
        hold = 0.5f;
    return (kind == Error || kind == Warning) ? hold * 1.5f : hold;
}

static int findKey(const char* key)
{
    for (int i = 0; i < s_count; ++i)
        if (strcmp(s_slots[i].key, key) == 0)
            return i;
    return -1;
}

static void showInternal(const char* key, Kind kind, const char* title,
                         const char* message, bool sticky)
{
    if (!Config::g_settings.toastsEnabled || !message || !message[0])
        return;

    char resolvedKey[kKeySize];
    snprintf(resolvedKey, sizeof(resolvedKey), "%s", (key && key[0]) ? key : message);

    const int found = findKey(resolvedKey);

    Slot incoming;
    snprintf(incoming.key, sizeof(incoming.key), "%s", resolvedKey);
    snprintf(incoming.title, sizeof(incoming.title), "%s", title ? title : "");
    snprintf(incoming.text, sizeof(incoming.text), "%s", message);
    incoming.kind = kind;
    incoming.sticky = sticky;
    incoming.remaining = holdSeconds(kind) + kFadeOutSeconds;

    incoming.age = found >= 0 ? kFadeInSeconds : 0.0f;

    if (found >= 0) {
        for (int i = found; i > 0; --i)
            s_slots[i] = s_slots[i - 1];
        s_slots[0] = incoming;
        return;
    }

    if (s_count >= kMaxVisible) {
        int drop = s_count - 1;
        for (int i = s_count - 1; i >= 0; --i) {
            if (!s_slots[i].sticky) {
                drop = i;
                break;
            }
        }
        for (int i = drop; i < s_count - 1; ++i)
            s_slots[i] = s_slots[i + 1];
        --s_count;
    }
    for (int i = s_count; i > 0; --i)
        s_slots[i] = s_slots[i - 1];
    s_slots[0] = incoming;
    ++s_count;
}

static void showfv(const char* key, Kind kind, const char* title, const char* format,
                   va_list args, bool sticky)
{
    if (!Config::g_settings.toastsEnabled || !format)
        return;
    char message[kMessageSize];
    vsnprintf(message, sizeof(message), format, args);
    message[sizeof(message) - 1] = '\0';
    showInternal(key, kind, title, message, sticky);
}

void Show(const char* message)                    { showInternal(nullptr, Info, nullptr, message, false); }
void Show(Kind kind, const char* message)         { showInternal(nullptr, kind, nullptr, message, false); }
void Show(Kind kind, const char* title, const char* message)
{
    showInternal(nullptr, kind, title, message, false);
}
void ShowKeyed(const char* key, Kind kind, const char* message)
{
    showInternal(key, kind, nullptr, message, false);
}
void ShowKeyed(const char* key, Kind kind, const char* title, const char* message)
{
    showInternal(key, kind, title, message, false);
}

void Showf(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    showfv(nullptr, Info, nullptr, format, args, false);
    va_end(args);
}

void Showf(Kind kind, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    showfv(nullptr, kind, nullptr, format, args, false);
    va_end(args);
}

void ShowTitledf(Kind kind, const char* title, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    showfv(nullptr, kind, title, format, args, false);
    va_end(args);
}

void ShowKeyedf(const char* key, Kind kind, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    showfv(key, kind, nullptr, format, args, false);
    va_end(args);
}

void ShowKeyedTitledf(const char* key, Kind kind, const char* title, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    showfv(key, kind, title, format, args, false);
    va_end(args);
}

void ShowSticky(const char* key, Kind kind, const char* title, const char* message)
{
    showInternal(key, kind, title, message, true);
}

void ShowStickyf(const char* key, Kind kind, const char* title, const char* format, ...)
{
    va_list args;
    va_start(args, format);
    showfv(key, kind, title, format, args, true);
    va_end(args);
}

void Dismiss(const char* key)
{
    if (!key || !key[0])
        return;
    const int i = findKey(key);
    if (i < 0)
        return;

    s_slots[i].sticky = false;
    if (s_slots[i].remaining > kFadeOutSeconds)
        s_slots[i].remaining = kFadeOutSeconds;
}

static float slotAlpha(const Slot& slot)
{
    float alpha = 1.0f;
    if (!slot.sticky && slot.remaining < kFadeOutSeconds)
        alpha = slot.remaining / kFadeOutSeconds;
    if (slot.age < kFadeInSeconds) {
        const float in = slot.age / kFadeInSeconds;
        if (in < alpha)
            alpha = in;
    }
    if (alpha < 0.0f)
        alpha = 0.0f;
    if (alpha > 1.0f)
        alpha = 1.0f;
    return alpha;
}

void Draw(ImGuiIO& io)
{
    if (!Config::g_settings.toastsEnabled) {
        Clear();
        return;
    }
    if (s_count <= 0)
        return;

    const float k = 1.0f / Renderer::UiScale();
    const ImGuiStyle& style = ImGui::GetStyle();
    const ImVec2 padding(style.WindowPadding.x * k, style.WindowPadding.y * k);
    const ImVec2 spacing(style.ItemSpacing.x * k, style.ItemSpacing.y * k);
    const float maxWindowWidth = io.DisplaySize.x * 0.34f;
    float stackY = io.DisplaySize.y * 0.075f;
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_NoNav |
        ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_AlwaysAutoResize;

    for (int i = 0; i < s_count; ++i) {
        Slot& slot = s_slots[i];
        slot.age += io.DeltaTime;

        const float alpha = slotAlpha(slot);
        const ImVec4 accent = kindColor(slot.kind);
        const float textW = ImGui::CalcTextSize(slot.text).x * k;
        const float titleW = slot.title[0] ? ImGui::CalcTextSize(slot.title).x * k : 0.0f;
        float windowWidth = (textW > titleW ? textW : titleW) + padding.x * 2.0f + 4.0f * k;
        if (windowWidth > maxWindowWidth)
            windowWidth = maxWindowWidth;
        if (windowWidth < 96.0f * k)
            windowWidth = 96.0f * k;

        char name[32];
        snprintf(name, sizeof(name), "%s%d", kWindowPrefix, i);

        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, kRounding * k);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, kBorderW * k);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, padding);
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, spacing);
        ImGui::PushStyleColor(ImGuiCol_Border, accent);
        ImGui::SetNextWindowBgAlpha(Renderer::BackdropAlpha(0.86f * alpha));
        ImGui::SetNextWindowPos(ImVec2(io.DisplaySize.x - 24.0f * k, stackY),
                                ImGuiCond_Always, ImVec2(1.0f, 0.0f));
        ImGui::SetNextWindowSizeConstraints(ImVec2(windowWidth, 0.0f),
                                            ImVec2(windowWidth, 10000.0f));
        if (ImGui::Begin(name, nullptr, flags)) {
            ImGui::SetWindowFontScale(k);
            if (slot.title[0]) {
                ImGui::PushStyleColor(ImGuiCol_Text, accent);
                ImGui::TextUnformatted(slot.title);
                ImGui::PopStyleColor();
            }
            ImGui::PushTextWrapPos(0.0f);
            ImGui::TextWrapped("%s", slot.text);
            ImGui::PopTextWrapPos();
            stackY += ImGui::GetWindowSize().y + kStackGap * k;
        }
        ImGui::End();
        ImGui::PopStyleColor();
        ImGui::PopStyleVar(5);

        if (!slot.sticky)
            slot.remaining -= io.DeltaTime;
    }

    int n = 0;
    for (int i = 0; i < s_count; ++i) {
        if (s_slots[i].sticky || s_slots[i].remaining > 0.0f) {
            if (n != i)
                s_slots[n] = s_slots[i];
            ++n;
        }
    }
    s_count = n;
}
}
