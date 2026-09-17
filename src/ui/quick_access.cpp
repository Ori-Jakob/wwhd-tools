#include "ui/quick_access.h"

#include "core/config.h"
#include "core/input.h"
#include "render/renderer.h"
#include "ui/ui_control.h"
#include "ui/ui_field.h"
#include "ui/ui_window.h"

#include "imgui.h"

#include <string.h>

namespace Ui {
namespace QuickAccess {
const char FULL_WINDOW_NAME[]       = "Quick Access";
const char STANDALONE_WINDOW_NAME[] = "Quick Access##standalone";

enum Mode { MODE_NORMAL = 0, MODE_EDIT, MODE_ADD };

static const Control::Descriptor* s_items[MAX_ITEMS];
static int  s_count = 0;
static bool s_open = false;
static bool s_pageFocused = false;
static Mode s_mode = MODE_NORMAL;
static char s_filter[64] = "";
static char s_addFilter[64] = "";

bool IsOpen() { return s_open; }
int  ItemCount() { return s_count; }

const char* ItemId(int index)
{
    return index >= 0 && index < s_count ? s_items[index]->id : nullptr;
}

void SetOpen(bool open)
{
    if (s_open == open)
        return;
    s_open = open;
    if (!open)
        s_mode = MODE_NORMAL;
}

bool IsPageFocused() { return s_pageFocused; }

void OnHotkey(bool menuOpen)
{
    if (menuOpen) {
        SetOpen(!s_open);
        if (s_open)
            Window::RequestFocus(FULL_WINDOW_NAME);
        return;
    }
    s_pageFocused = !s_pageFocused;
    if (s_pageFocused)
        Window::RequestFocus(STANDALONE_WINDOW_NAME);
}

bool OnMenuOpened()
{
    if (!s_pageFocused)
        return false;
    s_pageFocused = false;
    SetOpen(true);
    Window::RequestFocus(FULL_WINDOW_NAME);
    return true;
}

void ClearItems() { s_count = 0; }

bool AddItem(const char* id)
{
    const Control::Descriptor* descriptor = Control::Find(id);
    if (!descriptor || s_count >= MAX_ITEMS)
        return false;
    for (int i = 0; i < s_count; ++i)
        if (s_items[i] == descriptor)
            return false;
    s_items[s_count++] = descriptor;
    return true;
}

static char fold(char c)
{
    return (c >= 'A' && c <= 'Z') ? (char)(c - 'A' + 'a') : c;
}

static bool matches(const char* text, const char* needle)
{
    if (!needle || !needle[0])
        return true;
    if (!text)
        return false;
    for (const char* start = text; *start; ++start) {
        const char* a = start;
        const char* b = needle;
        while (*a && *b && fold(*a) == fold(*b)) { ++a; ++b; }
        if (!*b)
            return true;
    }
    return false;
}

static void sortByName()
{
    for (int i = 1; i < s_count; ++i) {
        const Control::Descriptor* value = s_items[i];
        int j = i - 1;
        while (j >= 0) {
            const char* a = s_items[j]->name;
            const char* b = value->name;
            while (*a && *b && fold(*a) == fold(*b)) { ++a; ++b; }
            if (fold(*a) <= fold(*b))
                break;
            s_items[j + 1] = s_items[j];
            --j;
        }
        s_items[j + 1] = value;
    }
    Config::MarkDirty();
}

static void drawPinned(const char* filter, Control::Surface surface)
{
    int visible = 0;
    for (int i = 0; i < s_count; ++i) {
        if (!matches(s_items[i]->name, filter))
            continue;
        if (visible++ > 0)
            ImGui::Spacing();
        Control::Draw(s_items[i], surface);
    }
    if (visible)
        return;
    ImGui::TextDisabled("%s", s_count == 0 ? "Nothing pinned yet."
                                           : "Nothing matches the search.");
}

static void drawNormal()
{
    ImGui::SetNextItemWidth(230.0f);
    Field::TextWithHint("##qa_filter", "Search pinned", s_filter, sizeof(s_filter));
    ImGui::SameLine();
    if (ImGui::Button("Sort A-Z"))
        sortByName();
    ImGui::SameLine();
    if (ImGui::Button("Edit"))
        s_mode = MODE_EDIT;
    ImGui::SameLine();
    if (ImGui::Button("Add"))
        s_mode = MODE_ADD;
    ImGui::Separator();
    drawPinned(s_filter, Control::SURFACE_QUICK_ACCESS);
}

static void drawEdit()
{
    if (ImGui::Button("Done"))
        s_mode = MODE_NORMAL;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(230.0f);
    Field::TextWithHint("##qa_edit_filter", "Search pinned", s_filter, sizeof(s_filter));
    ImGui::SameLine();
    if (ImGui::Button("Sort A-Z"))
        sortByName();
    ImGui::Separator();

    int moveFrom = -1, moveTo = -1, removeAt = -1, visible = 0;

    const bool filtered = s_filter[0] != '\0';

    if (ImGui::BeginTable("##qa_edit", 4,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Control", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
        ImGui::TableHeadersRow();
        for (int i = 0; i < s_count; ++i) {
            if (!matches(s_items[i]->name, s_filter))
                continue;
            ++visible;
            ImGui::TableNextRow();
            ImGui::PushID(s_items[i]->id);
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(s_items[i]->name);
            ImGui::TableSetColumnIndex(1);
            ImGui::BeginDisabled(filtered || i == 0);
            if (ImGui::ArrowButton("##up", ImGuiDir_Up)) { moveFrom = i; moveTo = i - 1; }
            ImGui::EndDisabled();
            ImGui::TableSetColumnIndex(2);
            ImGui::BeginDisabled(filtered || i + 1 == s_count);
            if (ImGui::ArrowButton("##down", ImGuiDir_Down)) { moveFrom = i; moveTo = i + 1; }
            ImGui::EndDisabled();
            ImGui::TableSetColumnIndex(3);
            if (ImGui::Button("Remove"))
                removeAt = i;
            ImGui::PopID();
        }
        ImGui::EndTable();
    }

    if (moveFrom >= 0 && moveTo >= 0) {
        const Control::Descriptor* moved = s_items[moveFrom];
        s_items[moveFrom] = s_items[moveTo];
        s_items[moveTo] = moved;
        Config::MarkDirty();
    }
    if (removeAt >= 0) {
        for (int i = removeAt; i + 1 < s_count; ++i)
            s_items[i] = s_items[i + 1];
        --s_count;
        Config::MarkDirty();
    }

    if (s_count == 0)
        ImGui::TextDisabled("Nothing pinned yet.");
    else if (!visible)
        ImGui::TextDisabled("Nothing matches the search.");
}

static bool isPinned(const Control::Descriptor* descriptor)
{
    for (int i = 0; i < s_count; ++i)
        if (s_items[i] == descriptor)
            return true;
    return false;
}

static void drawAdd()
{
    if (ImGui::Button("Back"))
        s_mode = MODE_NORMAL;
    ImGui::SameLine();
    ImGui::SetNextItemWidth(300.0f);
    Field::TextWithHint("##qa_add_filter", "Search all controls",
                        s_addFilter, sizeof(s_addFilter));
    ImGui::Separator();

    int visible = 0;
    if (ImGui::BeginTable("##qa_catalog", 2,
                          ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                          ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Control", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed);
        for (int i = 0; i < Control::Count(); ++i) {
            const Control::Descriptor* descriptor = Control::At(i);

            if (!matches(descriptor->name, s_addFilter) &&
                !matches(descriptor->path, s_addFilter))
                continue;
            ++visible;
            ImGui::TableNextRow();
            ImGui::PushID(descriptor->id);
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(descriptor->name);
            ImGui::TextDisabled("%s", descriptor->path);
            ImGui::TableSetColumnIndex(1);
            const bool pinned = isPinned(descriptor);
            ImGui::BeginDisabled(pinned || s_count >= MAX_ITEMS);
            if (ImGui::Button(pinned ? "Pinned" : "Pin", ImVec2(72.0f, 0.0f))) {
                AddItem(descriptor->id);
                Config::MarkDirty();
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    if (!visible)
        ImGui::TextDisabled("Nothing matches the search.");
    if (s_count >= MAX_ITEMS)
        ImGui::TextDisabled("The page is full (%d).", MAX_ITEMS);
}

void DrawMenuItem()
{
    bool open = s_open;
    if (Window::Checkbox("Quick Access", &open, FULL_WINDOW_NAME))
        SetOpen(open);
}

void DrawFullWindow()
{
    if (!s_open)
        return;
    ImGui::SetNextWindowSize(ImVec2(600.0f, 520.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(100.0f, 100.0f), ImGuiCond_FirstUseEver);
    bool open = s_open;
    if (ImGui::Begin(FULL_WINDOW_NAME, &open,
                     ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoFocusOnAppearing)) {
        if (s_mode == MODE_EDIT)      drawEdit();
        else if (s_mode == MODE_ADD)  drawAdd();
        else                          drawNormal();
    }
    ImGui::End();
    SetOpen(open);
}

void DrawPageWindow(bool menuOpen)
{
    if (menuOpen || !s_pageFocused)
        return;

    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse |
                                   ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoFocusOnAppearing;

    ImGui::SetNextWindowPos(ImVec2(48.0f, 96.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSizeConstraints(ImVec2(320.0f, 0.0f), ImVec2(520.0f, 620.0f));
    ImGui::SetNextWindowBgAlpha(Renderer::BackdropAlpha(0.75f));

    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.0f, 0.0f, 0.0f, 1.0f));
    if (ImGui::Begin(STANDALONE_WINDOW_NAME, nullptr, flags))
        drawPinned(nullptr, Control::SURFACE_QUICK_ACCESS);
    ImGui::End();
    ImGui::PopStyleColor();
}

bool HandleBack()
{
    if (!(Input::Current().pressed & Input::BTN_B))
        return false;

    if (s_pageFocused) {
        s_pageFocused = false;
        return true;
    }
    if (s_open && s_mode != MODE_NORMAL) {
        s_mode = MODE_NORMAL;
        return true;
    }
    return false;
}

void ResetToDefaults()
{
    ClearItems();
    s_open = false;
    s_pageFocused = false;
    s_mode = MODE_NORMAL;
    s_filter[0] = '\0';
    s_addFilter[0] = '\0';
}

void OnApplicationStart()
{
    s_pageFocused = false;
    s_mode = MODE_NORMAL;
    s_filter[0] = '\0';
    s_addFilter[0] = '\0';
}
}
}
