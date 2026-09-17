#include "ui/osk.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "core/input.h"
#include "core/logger.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace Ui {
namespace Osk {
namespace {
constexpr int kCols = 11;
constexpr int kCharRows = 4;
constexpr int kMaxText = 128;

const char* const kLower[kCharRows] = {
    "1234567890-",
    "qwertyuiop_",
    "asdfghjkl.,",
    "zxcvbnm()[]",
};
const char* const kUpper[kCharRows] = {
    "!@#$%^&*()+",
    "QWERTYUIOP=",
    "ASDFGHJKL:;",
    "ZXCVBNM<>{}",
};

enum Action { ACT_SHIFT = 0, ACT_SPACE, ACT_BACKSPACE, ACT_CLEAR, ACT_DONE, ACT_CANCEL, ACT_COUNT };
enum Mode { MODE_TEXT = 0, MODE_INT, MODE_FLOAT, MODE_HEX };

struct KeyPlan {
    const char* rows[4];
    int         rowCount;
    const int*  actions;
    int         actionCount;
    float       blockWidth;
};

const int kTextActions[]  = { ACT_SHIFT, ACT_SPACE, ACT_BACKSPACE, ACT_CLEAR,
                              ACT_DONE, ACT_CANCEL };
const int kPadActions[]   = { ACT_BACKSPACE, ACT_CLEAR, ACT_DONE, ACT_CANCEL };

int s_mode = MODE_TEXT;

const char* const kActionLabels[ACT_COUNT] = {
    "Shift", "Space", "Backspace", "Clear", "Done", "Cancel",
};
const float kActionWeights[ACT_COUNT] = { 1.5f, 3.0f, 2.0f, 1.3f, 1.6f, 1.6f };

constexpr int kActionRow = kCharRows;
constexpr int kRows = kCharRows + 1;

enum ShiftState { SHIFT_OFF = 0, SHIFT_ONCE, SHIFT_LOCK };

int  s_row = 1;
int  s_col = 0;
int  s_shift = SHIFT_OFF;
bool s_open = false;
bool s_deactivatePending = false;
char s_restoreWindow[96] = {};
ImGuiID s_restoreNavId = 0;
ImGuiNavLayer s_restoreNavLayer = ImGuiNavLayer_Main;
ImRect s_restoreNavRect;

bool s_wantedLast = false;
bool s_suppressUntilFocusLost = false;

char s_text[kMaxText] = {0};
int  s_len = 0;
int  s_cursor = 0;
int  s_maxChars = 0;
int  s_hardCap = kMaxText - 1;

constexpr int kDoubleTapFrames = 15;
int s_framesSinceShiftTap = kDoubleTapFrames + 1;

uint32_t s_heldDir = 0;
int      s_repeatDelay = 0;
uint32_t s_heldCursorDir = 0;
int      s_cursorRepeatDelay = 0;
constexpr int kRepeatFirstFrames = 9;
constexpr int kRepeatNextFrames  = 3;

bool s_touchWasDown = false;
bool s_swallowTouchUntilRelease = false;

struct FieldEntry { unsigned id; int kind; int maxChars; };
FieldEntry s_fields[64];
unsigned   s_fieldCount = 0;

int lookupField(unsigned id)
{
    for (unsigned i = 0; i < s_fieldCount; ++i)
        if (s_fields[i].id == id)
            return s_fields[i].kind;
    return -1;
}

int lookupMaxChars(unsigned id)
{
    for (unsigned i = 0; i < s_fieldCount; ++i)
        if (s_fields[i].id == id)
            return s_fields[i].maxChars;
    return 0;
}

struct NormRect { float x0, y0, x1, y1; };
struct Layout {
    NormRect panel;
    NormRect field;
    NormRect keys[kRows][kCols];
    float    keyRound;
};

bool shiftActive();

KeyPlan currentPlan()
{
    KeyPlan p = {};
    p.actions = kPadActions;
    p.actionCount = (int)(sizeof(kPadActions) / sizeof(kPadActions[0]));
    p.rowCount = 4;
    p.blockWidth = 0.34f;

    switch (s_mode) {
    case MODE_INT:
        p.rows[0] = "789"; p.rows[1] = "456"; p.rows[2] = "123"; p.rows[3] = "0-";
        break;
    case MODE_FLOAT:
        p.rows[0] = "789"; p.rows[1] = "456"; p.rows[2] = "123"; p.rows[3] = "0.-";
        break;
    case MODE_HEX:
        p.rows[0] = "0123"; p.rows[1] = "4567";
        p.rows[2] = "89AB"; p.rows[3] = "CDEF";
        p.blockWidth = 0.42f;
        break;
    default: {
        const char* const* rows = shiftActive() ? kUpper : kLower;
        p.rows[0] = rows[0];
        p.rows[1] = rows[1];
        p.rows[2] = rows[2];
        p.rows[3] = rows[3];
        p.actions = kTextActions;
        p.actionCount = (int)(sizeof(kTextActions) / sizeof(kTextActions[0]));
        p.blockWidth = 1.0f;
        break;
    }
    }
    return p;
}

int planCols(const KeyPlan& p)
{
    int widest = 0;
    for (int i = 0; i < p.rowCount; ++i) {
        const int n = (int)strlen(p.rows[i]);
        if (n > widest)
            widest = n;
    }
    return widest;
}

Layout computeLayout(const KeyPlan& plan)
{
    Layout out = {};
    const int cols = planCols(plan);

    const float marginX = 0.0f;
    const float padding = 0.0f;
    const float gap     = 0.006f;
    const float keyH    = 0.088f;
    const float fieldH  = 0.105f;
    const float hintH   = 0.036f;

    const float panelH = padding * 2.0f + fieldH + gap * 2.0f +
                         kRows * keyH + (kRows - 1) * gap + hintH;

    out.panel.x0 = marginX;
    out.panel.x1 = 1.0f - marginX;
    out.panel.y1 = 1.0f;
    out.panel.y0 = out.panel.y1 - panelH;
    out.keyRound = keyH * 0.22f;

    const float innerX0 = out.panel.x0 + padding;
    const float innerX1 = out.panel.x1 - padding;
    const float innerW  = innerX1 - innerX0;

    out.field.x0 = innerX0;
    out.field.x1 = innerX1;
    out.field.y0 = out.panel.y0 + padding;
    out.field.y1 = out.field.y0 + fieldH;

    const float blockW = innerW * plan.blockWidth;
    const float blockX0 = innerX0 + (innerW - blockW) * 0.5f;

    const float keysTop = out.field.y1 + gap * 2.0f;
    const float keyW = (blockW - (cols - 1) * gap) / (float)cols;

    for (int row = 0; row < plan.rowCount; ++row) {
        const float y0 = keysTop + row * (keyH + gap);
        const int rowLen = (int)strlen(plan.rows[row]);

        const float rowW = rowLen * keyW + (rowLen - 1) * gap;
        const float x0 = blockX0 + (blockW - rowW) * 0.5f;
        for (int col = 0; col < rowLen; ++col) {
            NormRect& r = out.keys[row][col];
            r.x0 = x0 + col * (keyW + gap);
            r.x1 = r.x0 + keyW;
            r.y0 = y0;
            r.y1 = y0 + keyH;
        }
    }

    float weightTotal = 0.0f;
    for (int i = 0; i < plan.actionCount; ++i)
        weightTotal += kActionWeights[plan.actions[i]];
    const float unit = (blockW - (plan.actionCount - 1) * gap) / weightTotal;

    const float actionY0 = keysTop + plan.rowCount * (keyH + gap);
    float x = blockX0;
    for (int i = 0; i < plan.actionCount; ++i) {
        NormRect& r = out.keys[kActionRow][i];
        r.x0 = x;
        r.x1 = x + kActionWeights[plan.actions[i]] * unit;
        r.y0 = actionY0;
        r.y1 = actionY0 + keyH;
        x = r.x1 + gap;
    }
    return out;
}

bool contains(const NormRect& r, float x, float y)
{
    return x >= r.x0 && x <= r.x1 && y >= r.y0 && y <= r.y1;
}

int colsInRow(const KeyPlan& plan, int row)
{
    if (row == kActionRow)
        return plan.actionCount;
    if (row < 0 || row >= plan.rowCount)
        return 1;
    return (int)strlen(plan.rows[row]);
}

int actionAt(const KeyPlan& plan, int col)
{
    if (col < 0 || col >= plan.actionCount)
        return ACT_CANCEL;
    return plan.actions[col];
}

bool shiftActive()
{
    return s_shift != SHIFT_OFF;
}

char charAt(const KeyPlan& plan, int row, int col)
{
    if (row < 0 || row >= plan.rowCount)
        return 0;
    if (col < 0 || col >= (int)strlen(plan.rows[row]))
        return 0;
    return plan.rows[row][col];
}

void clampSelection(const KeyPlan& plan)
{
    if (s_row < 0) s_row = kRows - 1;
    if (s_row >= kRows) s_row = 0;
    const int cols = colsInRow(plan, s_row);
    if (s_col < 0) s_col = cols - 1;
    if (s_col >= cols) s_col = 0;
}

void clampSelection()
{
    const KeyPlan plan = currentPlan();
    clampSelection(plan);
}

void moveRow(int delta)
{
    const KeyPlan plan = currentPlan();
    const int cols = colsInRow(plan, s_row);
    const float ratio = cols > 1 ? (float)s_col / (float)(cols - 1) : 0.0f;

    const int rows = plan.rowCount;
    s_row += delta;
    if (s_row < 0) s_row = kActionRow;
    if (s_row > kActionRow) s_row = 0;
    if (s_row >= rows && s_row < kActionRow)
        s_row = (delta > 0) ? kActionRow : rows - 1;

    const int newCols = colsInRow(plan, s_row);
    s_col = (int)(ratio * (float)(newCols - 1) + 0.5f);
    clampSelection(plan);
}

int charCapacity()
{
    return s_hardCap > 0 ? s_hardCap : (kMaxText - 1);
}

void clampCursor()
{
    if (s_cursor < 0)
        s_cursor = 0;
    if (s_cursor > s_len)
        s_cursor = s_len;
}

void moveCursor(int delta)
{
    s_cursor += delta;
    clampCursor();
}

void appendChar(char c)
{
    if (!c)
        return;
    if (s_len >= charCapacity())
        return;
    memmove(s_text + s_cursor + 1, s_text + s_cursor,
            (size_t)(s_len - s_cursor + 1));
    s_text[s_cursor++] = c;
    s_len++;
    if (s_shift == SHIFT_ONCE)
        s_shift = SHIFT_OFF;
}

void backspace()
{
    if (s_cursor <= 0)
        return;
    memmove(s_text + s_cursor - 1, s_text + s_cursor,
            (size_t)(s_len - s_cursor + 1));
    s_cursor--;
    s_len--;
}

void tapShift()
{
    if (s_shift == SHIFT_OFF) {
        s_shift = SHIFT_ONCE;
    } else if (s_shift == SHIFT_ONCE &&
               s_framesSinceShiftTap <= kDoubleTapFrames) {
        s_shift = SHIFT_LOCK;
    } else {
        s_shift = SHIFT_OFF;
    }
    s_framesSinceShiftTap = 0;
}

void seedFromField()
{
    s_len = 0;
    s_text[0] = '\0';
    s_cursor = 0;
    s_maxChars = 0;
    s_hardCap = kMaxText - 1;
    s_mode = MODE_TEXT;

    const ImGuiID id = ImGui::GetActiveID();

    const int declared = lookupField((unsigned)id);
    if (declared >= 0) {
        s_mode = declared == FIELD_HEX   ? MODE_HEX
               : declared == FIELD_FLOAT ? MODE_FLOAT
               : declared == FIELD_INT   ? MODE_INT
                                         : MODE_TEXT;
    }

    s_maxChars = lookupMaxChars((unsigned)id);
    s_hardCap = kMaxText - 1;
    if (s_maxChars > 0 && s_maxChars < s_hardCap)
        s_hardCap = s_maxChars;

    ImGuiInputTextState* st = ImGui::GetInputTextState(id);
    if (!st) {
        Logger::Log("[osk] no input-text state for activeID=0x%08x -> TEXT",
                    (unsigned)id);
        return;
    }

    if (declared < 0) {
        if (st->Flags & ImGuiInputTextFlags_CharsHexadecimal)
            s_mode = MODE_HEX;
        else if (st->Flags & ImGuiInputTextFlags_CharsScientific)
            s_mode = MODE_FLOAT;
        else if (st->Flags & ImGuiInputTextFlags_CharsDecimal)
            s_mode = MODE_INT;
    }

    Logger::Log("[osk] activeID=0x%08x flags=0x%08x -> %s "
                "(decimal=%d scientific=%d hex=%d)",
                (unsigned)id, (unsigned)st->Flags,
                s_mode == MODE_HEX   ? "HEX"   :
                s_mode == MODE_FLOAT ? "FLOAT" :
                s_mode == MODE_INT   ? "INT"   : "TEXT",
                (st->Flags & ImGuiInputTextFlags_CharsDecimal) ? 1 : 0,
                (st->Flags & ImGuiInputTextFlags_CharsScientific) ? 1 : 0,
                (st->Flags & ImGuiInputTextFlags_CharsHexadecimal) ? 1 : 0);

    if (st->BufCapacityA > 1 && st->BufCapacityA - 1 < s_hardCap)
        s_hardCap = st->BufCapacityA - 1;
    if (s_maxChars > s_hardCap)
        s_maxChars = s_hardCap;

    for (const ImWchar* w = st->TextW.Data; w && *w && s_len < charCapacity(); ++w) {
        if (*w >= 0x20 && *w < 0x7F)
            s_text[s_len++] = (char)*w;
    }
    s_text[s_len] = '\0';
    s_cursor = s_len;

    if (declared < 0 && s_mode == MODE_TEXT && s_len > 0) {
        bool numeric = true;
        bool sawDot = false;
        bool sawDigit = false;
        for (int i = 0; i < s_len; ++i) {
            const char c = s_text[i];
            if (c >= '0' && c <= '9') { sawDigit = true; continue; }
            if (c == '.') { sawDot = true; continue; }
            if ((c == '-' || c == '+') && i == 0) continue;
            numeric = false;
            break;
        }
        if (numeric && sawDigit) {
            s_mode = sawDot ? MODE_FLOAT : MODE_INT;
            Logger::Log("[osk] no filter flag; contents '%s' -> %s",
                        s_text, sawDot ? "FLOAT" : "INT");
        }
    }
}

void rememberRestoreTarget()
{
    ImGuiContext* g = ImGui::GetCurrentContext();
    s_restoreNavId = ImGui::GetActiveID();
    s_restoreWindow[0] = '\0';
    s_restoreNavLayer = ImGuiNavLayer_Main;
    s_restoreNavRect = ImRect();
    if (!g)
        return;
    ImGuiWindow* w = g->ActiveIdWindow ? g->ActiveIdWindow : g->NavWindow;
    if (w && w->Name) {
        strncpy(s_restoreWindow, w->Name, sizeof(s_restoreWindow) - 1);
        s_restoreWindow[sizeof(s_restoreWindow) - 1] = '\0';
    }
    s_restoreNavLayer = g->NavLayer;
    if (g->NavWindow)
        s_restoreNavRect = g->NavWindow->NavRectRel[g->NavLayer];
}

void restoreNavAfterEdit()
{
    ImGui::ClearActiveID();
    ImGuiContext* g = ImGui::GetCurrentContext();
    ImGuiWindow* w = s_restoreWindow[0] ? ImGui::FindWindowByName(s_restoreWindow)
                                        : nullptr;
    if (!w && g)
        w = g->NavWindow;
    if (w) {
        ImGui::FocusWindow(w);
        if (s_restoreNavId != 0 && g) {
            if (g->NavWindow != w)
                ImGui::SetNavWindow(w);
            ImGui::SetNavID(s_restoreNavId, s_restoreNavLayer, 0, s_restoreNavRect);
            g->NavDisableHighlight = false;
            g->NavDisableMouseHover = true;
        }
    }
    s_restoreNavId = 0;
    s_restoreWindow[0] = '\0';
}

void submitToField()
{
    rememberRestoreTarget();
    ImGuiInputTextState* st = ImGui::GetInputTextState(ImGui::GetActiveID());
    if (st) {
        st->ClearText();
        for (int i = 0; i < s_len; ++i)
            st->OnKeyPressed((int)(unsigned char)s_text[i]);
    } else {
        Logger::LogWarn("[osk] cannot submit: active input-text state is unavailable");
    }
    s_open = false;
    s_deactivatePending = true;
    s_suppressUntilFocusLost = true;
    if (Input::GetTouchPoint(nullptr, nullptr))
        s_swallowTouchUntilRelease = true;
}

void cancel()
{
    rememberRestoreTarget();
    s_open = false;
    s_deactivatePending = true;
    s_suppressUntilFocusLost = true;
    if (Input::GetTouchPoint(nullptr, nullptr))
        s_swallowTouchUntilRelease = true;
}

void activate(int row, int col)
{
    const KeyPlan plan = currentPlan();
    if (row < plan.rowCount) {
        appendChar(charAt(plan, row, col));
        return;
    }
    switch (actionAt(plan, col)) {
    case ACT_SHIFT:     tapShift(); break;
    case ACT_SPACE:     appendChar(' '); break;
    case ACT_BACKSPACE: backspace(); break;
    case ACT_CLEAR:     s_len = 0; s_cursor = 0; s_text[0] = '\0'; break;
    case ACT_DONE:      submitToField(); break;
    case ACT_CANCEL:    cancel(); break;
    default: break;
    }
}
}

bool IsOpen()
{
    return s_open;
}

void RegisterField(unsigned id, int kind, int maxChars)
{
    if (!id)
        return;
    if (maxChars < 0)
        maxChars = 0;
    for (unsigned i = 0; i < s_fieldCount; ++i) {
        if (s_fields[i].id == id) {
            s_fields[i].kind = kind;
            s_fields[i].maxChars = maxChars;
            return;
        }
    }
    if (s_fieldCount < sizeof(s_fields) / sizeof(s_fields[0])) {
        s_fields[s_fieldCount].id = id;
        s_fields[s_fieldCount].kind = kind;
        s_fields[s_fieldCount].maxChars = maxChars;
        ++s_fieldCount;
    }
}

void OnApplicationStart()
{
    s_fieldCount = 0;
    s_open = false;
    s_deactivatePending = false;
    s_shift = SHIFT_OFF;
    s_row = 1;
    s_col = 0;
    s_len = 0;
    s_cursor = 0;
    s_maxChars = 0;
    s_hardCap = kMaxText - 1;
    s_text[0] = '\0';
    s_heldDir = 0;
    s_heldCursorDir = 0;
    s_touchWasDown = false;
    s_swallowTouchUntilRelease = false;
    s_wantedLast = false;
    s_suppressUntilFocusLost = false;
    s_restoreNavId = 0;
    s_restoreWindow[0] = '\0';
}

static bool touchHeld()
{
    return Input::GetTouchPoint(nullptr, nullptr);
}

static bool swallowHeldTouch(ImGuiIO& io)
{
    if (!s_swallowTouchUntilRelease)
        return false;
    if (touchHeld()) {
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, false);
        return true;
    }
    s_swallowTouchUntilRelease = false;
    s_touchWasDown = false;
    return false;
}

bool ProcessInput(ImGuiIO& io)
{
    if (s_deactivatePending) {
        restoreNavAfterEdit();
        s_deactivatePending = false;
        s_wantedLast = io.WantTextInput;
        if (touchHeld())
            s_swallowTouchUntilRelease = true;
        return swallowHeldTouch(io);
    }

    if (swallowHeldTouch(io)) {
        s_wantedLast = io.WantTextInput;
        return true;
    }

    const bool wants = io.WantTextInput;
    if (!wants)
        s_suppressUntilFocusLost = false;

    if (wants && !s_wantedLast && !s_suppressUntilFocusLost && !s_open) {
        s_open = true;
        s_shift = SHIFT_OFF;
        s_row = 0;
        s_col = 0;
        seedFromField();
        clampSelection();
    }
    s_wantedLast = wants;
    if (!s_open)
        return false;

    const bool ownedInputAtFrameStart = s_open;

    if (s_framesSinceShiftTap <= kDoubleTapFrames)
        ++s_framesSinceShiftTap;

    const Input::Snapshot& in = Input::Current();

    const uint32_t dirs = in.held & (Input::BTN_UP | Input::BTN_DOWN | Input::BTN_LEFT | Input::BTN_RIGHT);
    uint32_t step = in.pressed & (Input::BTN_UP | Input::BTN_DOWN | Input::BTN_LEFT | Input::BTN_RIGHT);
    if (dirs && dirs == s_heldDir) {
        if (--s_repeatDelay <= 0) {
            step = dirs;
            s_repeatDelay = kRepeatNextFrames;
        }
    } else {
        s_heldDir = dirs;
        s_repeatDelay = kRepeatFirstFrames;
    }

    if (step & Input::BTN_UP)    moveRow(-1);
    if (step & Input::BTN_DOWN)  moveRow(+1);
    if (step & Input::BTN_LEFT)  { --s_col; clampSelection(); }
    if (step & Input::BTN_RIGHT) { ++s_col; clampSelection(); }

    const uint32_t cursorDirs = in.held & (Input::BTN_L | Input::BTN_R);
    uint32_t cursorStep = in.pressed & (Input::BTN_L | Input::BTN_R);
    if (cursorDirs && cursorDirs == s_heldCursorDir) {
        if (--s_cursorRepeatDelay <= 0) {
            cursorStep = cursorDirs;
            s_cursorRepeatDelay = kRepeatNextFrames;
        }
    } else {
        s_heldCursorDir = cursorDirs;
        s_cursorRepeatDelay = kRepeatFirstFrames;
    }
    if (cursorStep & Input::BTN_L) moveCursor(-1);
    if (cursorStep & Input::BTN_R) moveCursor(+1);

    if (in.pressed & Input::BTN_A)     activate(s_row, s_col);
    if (in.pressed & Input::BTN_B)     backspace();
    if (in.pressed & Input::BTN_X)     appendChar(' ');
    if ((in.pressed & Input::BTN_Y) && s_mode == MODE_TEXT) tapShift();
    if (in.pressed & Input::BTN_PLUS)  submitToField();
    if (in.pressed & Input::BTN_MINUS) cancel();

    float tx = 0.0f, ty = 0.0f;
    const bool touching = Input::GetTouchPoint(&tx, &ty);
    if (touching && !s_touchWasDown && s_open) {
        const KeyPlan plan = currentPlan();
        const Layout layout = computeLayout(plan);
        for (int row = 0; row < kRows && s_open; ++row) {
            const int cols = colsInRow(plan, row);
            for (int col = 0; col < cols; ++col) {
                if (!contains(layout.keys[row][col], tx, ty))
                    continue;
                s_row = row;
                s_col = col;
                activate(row, col);
                break;
            }
        }
    }
    s_touchWasDown = touching;

    return ownedInputAtFrameStart;
}

void Draw()
{
    if (!s_open)
        return;

    ImGuiIO& io = ImGui::GetIO();
    ImDrawList* dl = ImGui::GetForegroundDrawList();
    const KeyPlan plan = currentPlan();
    const Layout n = computeLayout(plan);
    const float W = io.DisplaySize.x;
    const float H = io.DisplaySize.y;

    auto px = [&](const NormRect& r, ImVec2* a, ImVec2* b) {
        *a = ImVec2(r.x0 * W, r.y0 * H);
        *b = ImVec2(r.x1 * W, r.y1 * H);
    };

    const ImU32 colPanel   = IM_COL32(16, 18, 24, 245);
    const ImU32 colBorder  = IM_COL32(92, 148, 255, 200);
    const ImU32 colKey     = IM_COL32(38, 42, 52, 255);
    const ImU32 colKeyEdge = IM_COL32(66, 72, 86, 255);
    const ImU32 colSel     = IM_COL32(92, 148, 255, 255);
    const ImU32 colSelEdge = IM_COL32(178, 208, 255, 255);
    const ImU32 colLock    = IM_COL32(255, 186, 84, 255);
    const ImU32 colText    = IM_COL32(228, 234, 246, 255);
    const ImU32 colTextSel = IM_COL32(10, 14, 22, 255);
    const ImU32 colHint    = IM_COL32(148, 156, 174, 255);
    const ImU32 colField   = IM_COL32(10, 12, 17, 255);

    ImVec2 a, b;
    px(n.panel, &a, &b);
    const float round = n.keyRound * H;

    dl->AddRectFilled(a, b, colPanel);
    dl->AddLine(ImVec2(a.x, a.y), ImVec2(b.x, a.y), colBorder, 2.0f);

    ImVec2 fa, fb;
    px(n.field, &fa, &fb);
    dl->AddRectFilled(fa, fb, colField, round * 0.6f);
    dl->AddRect(fa, fb, colKeyEdge, round * 0.6f, 0, 1.5f);

    const float fieldPad = (fb.y - fa.y) * 0.22f;
    const ImU32 colMax = IM_COL32(220, 72, 72, 255);
    char maxLabel[24] = {};
    ImVec2 maxSize(0.0f, 0.0f);
    float textRight = fb.x - fieldPad;
    if (s_maxChars > 0) {
        snprintf(maxLabel, sizeof(maxLabel), "%d/%d", s_len, s_maxChars);
        maxSize = ImGui::CalcTextSize(maxLabel);
        const bool atMax = s_len >= s_maxChars;
        const ImVec2 maxAt(fb.x - fieldPad - maxSize.x,
                           fa.y + ((fb.y - fa.y) - maxSize.y) * 0.5f);
        dl->AddText(maxAt, atMax ? colMax : colHint, maxLabel);
        textRight = maxAt.x - fieldPad;
    }

    if (s_shift == SHIFT_LOCK && s_maxChars <= 0) {
        const char* badge = "CAPS LOCK";
        const ImVec2 bs = ImGui::CalcTextSize(badge);
        const ImVec2 p0(fb.x - bs.x - fieldPad * 2.2f, fa.y + fieldPad * 0.6f);
        const ImVec2 p1(fb.x - fieldPad * 0.6f, fb.y - fieldPad * 0.6f);
        dl->AddRectFilled(p0, p1, colLock, round * 0.4f);
        dl->AddText(ImVec2(p0.x + (p1.x - p0.x - bs.x) * 0.5f,
                           p0.y + (p1.y - p0.y - bs.y) * 0.5f),
                    colTextSel, badge);
        textRight = p0.x - fieldPad;
    }

    char prefix[kMaxText];
    if (s_cursor < 0)
        s_cursor = 0;
    if (s_cursor > s_len)
        s_cursor = s_len;
    memcpy(prefix, s_text, (size_t)s_cursor);
    prefix[s_cursor] = '\0';
    const ImVec2 prefixSize = ImGui::CalcTextSize(prefix);
    const ImVec2 ts = ImGui::CalcTextSize(s_text);
    const ImVec2 textAt(fa.x + fieldPad, fa.y + ((fb.y - fa.y) - ts.y) * 0.5f);
    dl->PushClipRect(ImVec2(fa.x + fieldPad * 0.4f, fa.y),
                     ImVec2(textRight, fb.y), true);
    dl->AddText(textAt, colText, s_text);

    if (ImGui::GetTime() - (double)(int)ImGui::GetTime() < 0.6) {
        const float cx = textAt.x + prefixSize.x + 2.0f;
        dl->AddRectFilled(ImVec2(cx, fa.y + fieldPad),
                          ImVec2(cx + 2.5f, fb.y - fieldPad), colText);
    }
    dl->PopClipRect();

    for (int row = 0; row < kRows; ++row) {
        if (row < kActionRow && row >= plan.rowCount)
            continue;
        const int cols = colsInRow(plan, row);
        for (int col = 0; col < cols; ++col) {
            const bool sel = (row == s_row && col == s_col);
            const bool shiftKey = (row == kActionRow &&
                                   actionAt(plan, col) == ACT_SHIFT);
            const bool lit = shiftKey && shiftActive();

            ImVec2 k0, k1;
            px(n.keys[row][col], &k0, &k1);

            ImU32 fill = colKey;
            if (sel)
                fill = colSel;
            else if (lit)
                fill = (s_shift == SHIFT_LOCK) ? colLock : colKeyEdge;

            dl->AddRectFilled(k0, k1, fill, round * 0.7f);
            dl->AddRect(k0, k1, sel ? colSelEdge : (lit ? colLock : colKeyEdge),
                        round * 0.7f, 0, sel ? 2.5f : 1.0f);

            char one[2] = {0, 0};
            const char* label;
            if (row < plan.rowCount) {
                one[0] = charAt(plan, row, col);
                label = one;
            } else {
                label = kActionLabels[actionAt(plan, col)];
            }
            if (!label || !label[0])
                continue;

            const ImVec2 ls = ImGui::CalcTextSize(label);
            const bool darkText = sel || (lit && s_shift == SHIFT_LOCK);
            dl->AddText(ImVec2(k0.x + ((k1.x - k0.x) - ls.x) * 0.5f,
                               k0.y + ((k1.y - k0.y) - ls.y) * 0.5f),
                        darkText ? colTextSel : colText, label);
        }
    }

    const char* hint = (s_mode == MODE_TEXT)
        ? "A select   B backspace   X space   Y shift   L/R cursor   "
          "+ done   - cancel   touch to type"
        : "A select   B backspace   L/R cursor   + done   - cancel   touch to type";
    const ImVec2 hs = ImGui::CalcTextSize(hint);
    dl->AddText(ImVec2(a.x + ((b.x - a.x) - hs.x) * 0.5f, b.y - hs.y - 6.0f),
                colHint, hint);
}
}
}
