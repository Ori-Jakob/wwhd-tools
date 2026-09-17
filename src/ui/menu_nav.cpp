#include "ui/menu_nav.h"

#include "core/input.h"
#include "core/settings.h"
#include "ui/ui_field.h"
#include "ui/ui_window.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <string.h>

namespace Ui {
namespace Nav {
static const int   kMaxCycleWindows = 16;
static const float kMoveSpeed = 40.0f;
static const float kResizeSpeed = 25.0f;
static const float kScrollSpeed = 2000.0f;
static const float kStickDeadzone = 0.05f;
static const float kMinWindowW = 120.0f;
static const float kMinWindowH = 60.0f;

static const char kMenuBarName[] = "##MainMenuBar";

struct ActionInfo {
    const char* name;
    const char* hint;
    const char* key;
    uint32_t    fallback;
};

static const ActionInfo kActions[ACTION_COUNT] = {
    { "Next window",     "Cycle forward through open windows",  "navNextWindow",     Input::BTN_R     },
    { "Previous window", "Cycle back through open windows",     "navPrevWindow",     Input::BTN_L     },
    { "Next tab",        "Next tab in the focused window",      "navNextTab",        Input::BTN_ZR    },
    { "Previous tab",    "Previous tab in the focused window",  "navPrevTab",        Input::BTN_ZL    },
    { "Focus menu bar",  "Jump to the main menu bar",           "navFocusMenuBar",   Input::BTN_Y     },
    { "Move/resize hold","Hold, then use the sticks",           "navAdjustModifier", Input::BTN_MINUS },
};

static uint32_t s_bindings[ACTION_COUNT] = {
    Input::BTN_R, Input::BTN_L, Input::BTN_ZR,
    Input::BTN_ZL, Input::BTN_Y, Input::BTN_MINUS,
};

static bool validAction(Action action)
{
    return action >= 0 && action < ACTION_COUNT;
}

uint32_t Binding(Action action)
{
    return validAction(action) ? s_bindings[action] : 0u;
}

void SetBinding(Action action, uint32_t button)
{
    if (validAction(action))
        s_bindings[action] = button;
}

uint32_t    DefaultBinding(Action action) { return validAction(action) ? kActions[action].fallback : 0u; }
const char* ActionName(Action action)     { return validAction(action) ? kActions[action].name : "?"; }
const char* ActionHint(Action action)     { return validAction(action) ? kActions[action].hint : ""; }
const char* ConfigKey(Action action)      { return validAction(action) ? kActions[action].key : ""; }

void ResetBindings()
{
    for (int i = 0; i < ACTION_COUNT; ++i)
        s_bindings[i] = kActions[i].fallback;
}

static const uint32_t kReservedButtons =
    Input::BTN_A | Input::BTN_B | Input::BTN_X |
    Input::BTN_UP | Input::BTN_DOWN | Input::BTN_LEFT | Input::BTN_RIGHT;

bool Conflicts(Action target, uint32_t buttons, Action other)
{
    if (!validAction(target) || !validAction(other) || target == other)
        return false;
    return buttons == s_bindings[other];
}

const char* Validate(Action target, uint32_t buttons)
{
    if (!validAction(target))
        return "Unknown menu control.";
    if (!buttons)
        return "Press at least one button.";
    if (buttons & kReservedButtons)
        return "A, B, X and the D-pad drive the menu and cannot be rebound.";
    return nullptr;
}

static bool isPressed(Action action, const Input::Snapshot& snap)
{
    const uint32_t combo = Binding(action);
    return combo != 0 && snap.held == combo && (snap.pressed & combo) != 0;
}

static bool isHeld(Action action, const Input::Snapshot& snap)
{
    const uint32_t combo = Binding(action);
    return combo != 0 && (snap.held & combo) == combo;
}

static char    s_cycleNames[kMaxCycleWindows][96];
static ImVec2  s_cyclePos[kMaxCycleWindows];
static ImVec2  s_cycleSize[kMaxCycleWindows];
static int     s_cycleCount = 0;
static bool    s_cycleValid = false;
static int     s_cycleIdx = -1;
static int     s_menuBarKick = 0;

void RequestMenuBarFocus() { s_menuBarKick = 2; }

void ApplyMenuBarFocus(ImGuiIO& io)
{
    if (s_menuBarKick <= 0)
        return;
    ImGui::SetWindowFocus(kMenuBarName);
    io.AddKeyEvent(ImGuiMod_Alt, s_menuBarKick > 1);
    --s_menuBarKick;
}

static void focusMenuBar()
{
    s_cycleIdx = -1;
    Window::SetCycleFocus(nullptr);
    RequestMenuBarFocus();
}

static void invalidateCycle()
{
    s_cycleValid = false;
    s_cycleCount = 0;
    s_cycleIdx = -1;
}

static bool isToastName(const char* name)
{
    static const char kToastPrefix[] = "##wwhd_action_toast_";
    return strncmp(name, kToastPrefix, sizeof(kToastPrefix) - 1) == 0;
}

static bool isManageable(ImGuiWindow* w)
{
    if (!w || !w->Active || w->Hidden)
        return false;
    if (w->Flags & (ImGuiWindowFlags_ChildWindow | ImGuiWindowFlags_Popup |
                    ImGuiWindowFlags_Tooltip))
        return false;
    if (!w->Name)
        return true;
    return strcmp(w->Name, kMenuBarName) != 0 && !isToastName(w->Name);
}

static bool isCycleWindow(ImGuiWindow* w)
{
    if (!w || !ImGui::IsWindowNavFocusable(w) || !w->Name)
        return false;
    return strcmp(w->Name, kMenuBarName) != 0 &&
           strcmp(w->Name, "###NavWindowingList") != 0 &&
           !isToastName(w->Name);
}

static int collectCycleWindows(ImGuiWindow** list, int max)
{
    ImGuiContext* g = ImGui::GetCurrentContext();
    if (!g)
        return 0;
    int n = 0;
    for (int i = 0; i < g->WindowsFocusOrder.Size && n < max; ++i) {
        ImGuiWindow* w = g->WindowsFocusOrder[i];
        if (isCycleWindow(w))
            list[n++] = w;
    }
    return n;
}

static float centerX(const ImGuiWindow* w) { return w->Pos.x + w->Size.x * 0.5f; }

static bool shareColumn(const ImGuiWindow* a, const ImGuiWindow* b)
{
    const float narrow = a->Size.x < b->Size.x ? a->Size.x : b->Size.x;
    if (narrow <= 0.0f)
        return false;
    const float left = a->Pos.x > b->Pos.x ? a->Pos.x : b->Pos.x;
    const float aRight = a->Pos.x + a->Size.x;
    const float bRight = b->Pos.x + b->Size.x;
    const float right = aRight < bRight ? aRight : bRight;
    if (right - left < narrow * 0.5f)
        return false;
    float distance = centerX(a) - centerX(b);
    if (distance < 0.0f)
        distance = -distance;
    return distance <= narrow * 0.6f;
}

static int columnRoot(int* parents, int index)
{
    while (parents[index] != index) {
        parents[index] = parents[parents[index]];
        index = parents[index];
    }
    return index;
}

static int compareInColumn(const ImGuiWindow* a, const ImGuiWindow* b)
{
    if (a->Pos.y != b->Pos.y)
        return a->Pos.y < b->Pos.y ? -1 : 1;
    if (centerX(a) != centerX(b))
        return centerX(a) < centerX(b) ? -1 : 1;
    return strcmp(a->Name, b->Name);
}

static float columnCenter(ImGuiWindow** list, int n, int* parents, int root)
{
    float sum = 0.0f;
    int count = 0;
    for (int i = 0; i < n; ++i) {
        if (columnRoot(parents, i) == root) {
            sum += centerX(list[i]);
            ++count;
        }
    }
    return count > 0 ? sum / (float)count : 0.0f;
}

static void sortSpatial(ImGuiWindow** list, int n)
{
    if (n < 2)
        return;

    int parents[kMaxCycleWindows];
    for (int i = 0; i < n; ++i)
        parents[i] = i;
    for (int i = 0; i < n; ++i) {
        for (int j = i + 1; j < n; ++j) {
            if (!shareColumn(list[i], list[j]))
                continue;
            const int a = columnRoot(parents, i);
            const int b = columnRoot(parents, j);
            if (a != b)
                parents[b] = a;
        }
    }

    int roots[kMaxCycleWindows];
    int columnCount = 0;
    for (int i = 0; i < n; ++i) {
        const int root = columnRoot(parents, i);
        bool known = false;
        for (int j = 0; j < columnCount; ++j)
            known |= roots[j] == root;
        if (!known)
            roots[columnCount++] = root;
    }

    for (int i = 1; i < columnCount; ++i) {
        const int value = roots[i];
        const float valueX = columnCenter(list, n, parents, value);
        int j = i - 1;
        while (j >= 0 && columnCenter(list, n, parents, roots[j]) > valueX) {
            roots[j + 1] = roots[j];
            --j;
        }
        roots[j + 1] = value;
    }

    ImGuiWindow* sorted[kMaxCycleWindows];
    int out = 0;
    for (int column = 0; column < columnCount; ++column) {
        ImGuiWindow* members[kMaxCycleWindows];
        int memberCount = 0;
        for (int i = 0; i < n; ++i) {
            if (columnRoot(parents, i) == roots[column])
                members[memberCount++] = list[i];
        }
        for (int i = 1; i < memberCount; ++i) {
            ImGuiWindow* value = members[i];
            int j = i - 1;
            while (j >= 0 && compareInColumn(members[j], value) > 0) {
                members[j + 1] = members[j];
                --j;
            }
            members[j + 1] = value;
        }
        for (int i = 0; i < memberCount; ++i)
            sorted[out++] = members[i];
    }
    for (int i = 0; i < n; ++i)
        list[i] = sorted[i];
}

static int cacheIndex(const char* name)
{
    if (!name || !name[0])
        return -1;
    for (int i = 0; i < s_cycleCount; ++i)
        if (strcmp(s_cycleNames[i], name) == 0)
            return i;
    return -1;
}

static bool setMatches(ImGuiWindow** list, int n)
{
    if (!s_cycleValid || n != s_cycleCount)
        return false;
    for (int i = 0; i < n; ++i)
        if (cacheIndex(list[i]->Name) < 0)
            return false;
    return true;
}

static bool layoutMatches(ImGuiWindow** list, int n)
{
    if (!s_cycleValid || n != s_cycleCount)
        return false;
    for (int i = 0; i < n; ++i) {
        const int idx = cacheIndex(list[i]->Name);
        if (idx < 0)
            return false;
        if (s_cyclePos[idx].x != list[i]->Pos.x || s_cyclePos[idx].y != list[i]->Pos.y ||
            s_cycleSize[idx].x != list[i]->Size.x || s_cycleSize[idx].y != list[i]->Size.y)
            return false;
    }
    return true;
}

static void storeCycle(ImGuiWindow** list, int n)
{
    s_cycleCount = n;
    for (int i = 0; i < n; ++i) {
        strncpy(s_cycleNames[i], list[i]->Name, sizeof(s_cycleNames[i]) - 1);
        s_cycleNames[i][sizeof(s_cycleNames[i]) - 1] = '\0';
        s_cyclePos[i] = list[i]->Pos;
        s_cycleSize[i] = list[i]->Size;
    }
    s_cycleValid = true;
}

static bool isLayoutBusy(const Input::Snapshot& snap)
{
    if (isHeld(ADJUST_MODIFIER, snap))
        return true;
    ImGuiContext* g = ImGui::GetCurrentContext();
    if (!g)
        return false;
    if (g->MovingWindow)
        return true;
    for (int i = 0; i < g->Windows.Size; ++i)
        if (g->Windows[i] && g->Windows[i]->ResizeBorderHeld >= 0)
            return true;
    return false;
}

static void updateCycle(const Input::Snapshot& snap)
{
    ImGuiWindow* list[kMaxCycleWindows];
    const int n = collectCycleWindows(list, kMaxCycleWindows);
    const bool setChanged = !setMatches(list, n);
    const bool layoutChanged = !layoutMatches(list, n);
    if (!s_cycleValid || setChanged || (layoutChanged && !isLayoutBusy(snap))) {
        sortSpatial(list, n);
        storeCycle(list, n);
    }
}

static int resolveCycle(ImGuiWindow** list, int max)
{
    int n = 0;
    for (int i = 0; i < s_cycleCount && n < max; ++i) {
        ImGuiWindow* w = ImGui::FindWindowByName(s_cycleNames[i]);
        if (isCycleWindow(w))
            list[n++] = w;
    }
    return n;
}

static void cycleWindow(const Input::Snapshot& snap)
{
    if (Field::IsSteeringSlider())
        return;

    const int dir = (isPressed(NEXT_WINDOW, snap) ? 1 : 0) -
                    (isPressed(PREVIOUS_WINDOW, snap) ? 1 : 0);
    if (dir == 0)
        return;

    ImGuiContext* g = ImGui::GetCurrentContext();
    if (!g)
        return;
    if (!s_cycleValid)
        updateCycle(snap);

    ImGuiWindow* list[kMaxCycleWindows];
    int n = resolveCycle(list, kMaxCycleWindows);
    if (n != s_cycleCount) {
        updateCycle(snap);
        n = resolveCycle(list, kMaxCycleWindows);
    }

    ImGuiWindow* cur = g->NavWindow ? g->NavWindow->RootWindow : nullptr;
    bool matched = false;
    for (int i = 0; i < n; ++i) {
        if (list[i] == cur) {
            s_cycleIdx = i;
            Window::SetCycleFocus(list[i]->Name);
            matched = true;
            break;
        }
    }
    if (!matched) {
        const char* hint = Window::CycleFocus();
        s_cycleIdx = -1;
        if (hint && hint[0]) {
            for (int i = 0; i < n; ++i) {
                if (strcmp(list[i]->Name, hint) == 0) {
                    s_cycleIdx = i;
                    break;
                }
            }
        }
    }
    if (s_cycleIdx >= n)
        s_cycleIdx = n - 1;

    if (dir > 0) {
        if (s_cycleIdx < 0)        s_cycleIdx = n > 0 ? 0 : -1;
        else if (s_cycleIdx >= n - 1) s_cycleIdx = -1;
        else                       ++s_cycleIdx;
    } else {
        if (s_cycleIdx < 0)        s_cycleIdx = n > 0 ? n - 1 : -1;
        else if (s_cycleIdx == 0)  s_cycleIdx = -1;
        else                       --s_cycleIdx;
    }

    if (s_cycleIdx < 0) {
        focusMenuBar();
    } else {
        Window::SetCycleFocus(list[s_cycleIdx]->Name);
        ImGui::FocusWindow(list[s_cycleIdx], ImGuiFocusRequestFlags_RestoreFocusedChild);
    }
}

static ImRect scrollableRect(const ImGuiWindow* w)
{
    return ImRect(w->Pos.x, w->Pos.y - w->Scroll.y,
                  w->Pos.x + w->Size.x,
                  w->Pos.y + w->Size.y + (w->ScrollMax.y - w->Scroll.y));
}

static const int kMaxTabBarOwners = 8;

struct TabBarOwner { ImGuiID bar; ImGuiID window; int frame; };

static TabBarOwner s_tabBarOwners[kMaxTabBarOwners];
static int s_tabBarOwnerNext = 0;

void RegisterCurrentTabBar()
{
    ImGuiContext* g = ImGui::GetCurrentContext();
    if (!g || !g->CurrentTabBar || !g->CurrentWindow)
        return;

    TabBarOwner* owner = nullptr;
    for (int i = 0; i < kMaxTabBarOwners; ++i) {
        if (s_tabBarOwners[i].bar == g->CurrentTabBar->ID) {
            owner = &s_tabBarOwners[i];
            break;
        }
    }
    if (!owner) {
        owner = &s_tabBarOwners[s_tabBarOwnerNext];
        s_tabBarOwnerNext = (s_tabBarOwnerNext + 1) % kMaxTabBarOwners;
    }
    owner->bar = g->CurrentTabBar->ID;
    owner->window = g->CurrentWindow->ID;
    owner->frame = g->FrameCount;
}

static ImGuiWindow* barWindow(ImGuiContext* g, const ImGuiTabBar* bar)
{
    for (int i = 0; i < kMaxTabBarOwners; ++i) {
        const TabBarOwner& owner = s_tabBarOwners[i];
        if (owner.bar == bar->ID && owner.frame == g->FrameCount) {
            ImGuiWindow* w = ImGui::FindWindowByID(owner.window);
            if (w && w->Active && !w->Hidden)
                return w;
        }
    }

    ImGuiWindow* best = nullptr;
    float bestArea = 0.0f;
    for (int i = 0; i < g->Windows.Size; ++i) {
        ImGuiWindow* w = g->Windows[i];
        if (!w || !w->Active || w->Hidden || (w->Flags & ImGuiWindowFlags_Tooltip))
            continue;
        if (!scrollableRect(w).Contains(bar->BarRect.Min))
            continue;
        const float area = w->Size.x * w->Size.y;
        if (!best || area < bestArea) {
            best = w;
            bestArea = area;
        }
    }
    return best;
}

static bool submittedThisFrame(const ImGuiContext* g, const ImGuiTabBar* bar)
{
    return bar && bar->CurrFrameVisible == g->FrameCount;
}

static const int kMaxTabScrolls = 32;
static const int kMaxTabBars = 8;

struct TabScroll     { ImGuiID tab; float y; };
struct TabBarState   { ImGuiID bar; ImGuiID selected; };
struct PendingScroll { ImGuiID window; float y; int frames; };

static TabScroll     s_tabScroll[kMaxTabScrolls];
static int           s_tabScrollNext = 0;
static TabBarState   s_tabBar[kMaxTabBars];
static int           s_tabBarNext = 0;
static PendingScroll s_pendingScroll = { 0, 0.0f, 0 };

static float* tabScrollSlot(ImGuiID tab, bool create)
{
    for (int i = 0; i < kMaxTabScrolls; ++i)
        if (s_tabScroll[i].tab == tab)
            return &s_tabScroll[i].y;
    if (!create)
        return nullptr;
    TabScroll* slot = &s_tabScroll[s_tabScrollNext];
    s_tabScrollNext = (s_tabScrollNext + 1) % kMaxTabScrolls;
    slot->tab = tab;
    slot->y = 0.0f;
    return &slot->y;
}

static TabBarState* tabBarSlot(ImGuiID bar)
{
    for (int i = 0; i < kMaxTabBars; ++i)
        if (s_tabBar[i].bar == bar)
            return &s_tabBar[i];
    TabBarState* slot = &s_tabBar[s_tabBarNext];
    s_tabBarNext = (s_tabBarNext + 1) % kMaxTabBars;
    slot->bar = bar;
    slot->selected = 0;
    return slot;
}

static void switchTabScroll(ImGuiWindow* w, ImGuiID from, ImGuiID to, int again)
{
    *tabScrollSlot(from, true) = w->Scroll.y;
    const float* remembered = tabScrollSlot(to, false);
    const float y = remembered ? *remembered : 0.0f;
    ImGui::SetScrollY(w, y);
    s_pendingScroll.window = w->ID;
    s_pendingScroll.y = y;
    s_pendingScroll.frames = again;
}

static void updateTabScroll()
{
    ImGuiContext* g = ImGui::GetCurrentContext();
    if (!g)
        return;
    for (int n = 0; n < g->TabBars.GetMapSize(); ++n) {
        ImGuiTabBar* bar = g->TabBars.TryGetMapData(n);

        if (!submittedThisFrame(g, bar) || bar->NextSelectedTabId != 0)
            continue;
        TabBarState* state = tabBarSlot(bar->ID);
        if (state->selected == bar->SelectedTabId)
            continue;
        ImGuiWindow* home = barWindow(g, bar);
        if (home && state->selected && bar->SelectedTabId)
            switchTabScroll(home, state->selected, bar->SelectedTabId, 0);
        state->selected = bar->SelectedTabId;
    }

    if (s_pendingScroll.frames > 0) {
        --s_pendingScroll.frames;
        ImGuiWindow* w = ImGui::FindWindowByID(s_pendingScroll.window);
        if (w)
            ImGui::SetScrollY(w, s_pendingScroll.y);
    }
}

static void cycleTabs(const Input::Snapshot& snap)
{
    const int dir = (isPressed(NEXT_TAB, snap) ? 1 : 0) -
                    (isPressed(PREVIOUS_TAB, snap) ? 1 : 0);
    if (dir == 0)
        return;

    ImGuiContext* g = ImGui::GetCurrentContext();
    ImGuiWindow* w = (g && g->NavWindow) ? g->NavWindow->RootWindow : nullptr;
    if (!w)
        return;

    ImGuiTabBar* bar = nullptr;
    ImGuiWindow* home = nullptr;
    for (int n = 0; n < g->TabBars.GetMapSize() && !bar; ++n) {
        ImGuiTabBar* candidate = g->TabBars.TryGetMapData(n);
        if (!submittedThisFrame(g, candidate))
            continue;
        ImGuiWindow* candidateHome = barWindow(g, candidate);
        if (candidateHome && candidateHome->RootWindow == w) {
            bar = candidate;
            home = candidateHome;
        }
    }
    if (!bar || bar->Tabs.Size < 2 || bar->SelectedTabId == 0)
        return;

    int cur = 0;
    for (int i = 0; i < bar->Tabs.Size; ++i) {
        if (bar->Tabs[i].ID == bar->SelectedTabId) {
            cur = i;
            break;
        }
    }
    ImGuiTabItem* tab = &bar->Tabs[(cur + dir + bar->Tabs.Size) % bar->Tabs.Size];
    ImGui::TabBarQueueFocus(bar, tab);

    switchTabScroll(home, bar->SelectedTabId, tab->ID, 1);
    tabBarSlot(bar->ID)->selected = tab->ID;

    ImGui::SetNavWindow(w);
    const ImRect abs(ImVec2(bar->BarRect.Min.x + tab->Offset, bar->BarRect.Min.y),
                     ImVec2(bar->BarRect.Min.x + tab->Offset + tab->Width,
                            bar->BarRect.Max.y));
    ImGui::SetNavID(tab->ID, ImGuiNavLayer_Main, 0, ImGui::WindowRectAbsToRel(w, abs));
    g->NavDisableHighlight = false;
    g->NavDisableMouseHover = true;
}

static void adjustFocusedWindow(ImGuiIO& io, const Input::Snapshot& snap)
{
    if (snap.held != Binding(ADJUST_MODIFIER))
        return;

    ImGuiContext* g = ImGui::GetCurrentContext();
    ImGuiWindow* w = (g && g->NavWindow) ? g->NavWindow->RootWindow : nullptr;
    if (!isManageable(w))
        return;

    const float dead = kStickDeadzone * kStickDeadzone;

    if (snap.lx * snap.lx + snap.ly * snap.ly >= dead) {
        ImGui::SetWindowPos(w, ImVec2(w->Pos.x + snap.lx * kMoveSpeed,
                                      w->Pos.y - snap.ly * kMoveSpeed),
                            ImGuiCond_Always);
    }

    if (!(w->Flags & ImGuiWindowFlags_AlwaysAutoResize) &&
        snap.rx * snap.rx + snap.ry * snap.ry >= dead) {
        ImVec2 size(w->Size.x + snap.rx * kResizeSpeed,
                    w->Size.y - snap.ry * kResizeSpeed);
        if (size.x < kMinWindowW) size.x = kMinWindowW;
        if (size.y < kMinWindowH) size.y = kMinWindowH;
        if (size.x > io.DisplaySize.x) size.x = io.DisplaySize.x;
        if (size.y > io.DisplaySize.y) size.y = io.DisplaySize.y;
        ImGui::SetWindowSize(w, size, ImGuiCond_Always);
    }
}

static void scrollFocusedWindow(ImGuiIO& io, const Input::Snapshot& snap)
{
    if (isHeld(ADJUST_MODIFIER, snap))
        return;

    const float magnitude = snap.ry < 0.0f ? -snap.ry : snap.ry;
    if (magnitude <= kStickDeadzone)
        return;

    ImGuiContext* g = ImGui::GetCurrentContext();
    ImGuiWindow* target = g ? g->NavWindow : nullptr;
    while (target) {
        if (target->Active && !target->Hidden && target->ScrollbarY &&
            target->ScrollMax.y > 0.0f)
            break;
        target = target == target->RootWindow ? nullptr : target->ParentWindow;
    }
    if (!target)
        return;

    const float range = 1.0f - kStickDeadzone;
    float amount = range > 0.0f ? (magnitude - kStickDeadzone) / range : magnitude;
    if (snap.ry < 0.0f)
        amount = -amount;

    float next = target->Scroll.y - amount * kScrollSpeed * io.DeltaTime;
    if (next < 0.0f) next = 0.0f;
    if (next > target->ScrollMax.y) next = target->ScrollMax.y;
    ImGui::SetScrollY(target, next);
}

static void clampWindowsToViewport(ImGuiIO& io)
{
    ImGuiContext* g = ImGui::GetCurrentContext();
    if (!g)
        return;
    for (int i = 0; i < g->Windows.Size; ++i) {
        ImGuiWindow* w = g->Windows[i];
        if (!isManageable(w) || w->Size.x <= 0.0f || w->Size.y <= 0.0f)
            continue;
        ImVec2 pos = w->Pos;
        const float maxX = io.DisplaySize.x - w->Size.x;
        const float maxY = io.DisplaySize.y - w->Size.y;
        if (pos.x > maxX) pos.x = maxX;
        if (pos.y > maxY) pos.y = maxY;
        if (pos.x < 0.0f) pos.x = 0.0f;
        if (pos.y < 0.0f) pos.y = 0.0f;
        if (pos.x != w->Pos.x || pos.y != w->Pos.y)
            ImGui::SetWindowPos(w, pos, ImGuiCond_Always);
    }
}

void Update(ImGuiIO& io, const Input::Snapshot& snap, bool menuOpen, bool pageOnly)
{
    if (!menuOpen) {
        invalidateCycle();
        if (pageOnly)
            scrollFocusedWindow(io, snap);
        return;
    }

    updateCycle(snap);
    updateTabScroll();

    if (isPressed(FOCUS_MENU_BAR, snap))
        focusMenuBar();

    cycleWindow(snap);
    cycleTabs(snap);
    adjustFocusedWindow(io, snap);
    scrollFocusedWindow(io, snap);
    clampWindowsToViewport(io);
}
}
}
