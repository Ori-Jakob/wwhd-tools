#include "ui/panels.h"

#include "core/input.h"
#include "render/image.h"
#include "tools/sea_chart.h"
#include "ui/ui_window.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <math.h>
#include <stdio.h>

#include "sea_boat_icon_bin.h"
#include "sea_chart_bin.h"
#include "sea_link_icon_bin.h"

namespace Ui {
namespace Panels {
namespace Sea = Tools::SeaChart;

static bool s_open = false;
static bool s_wasOpen = false;
static const char kWindowName[] = "Sea Chart";

static const float kMapSide   = 440.0f;
static const float kMargin    = 16.0f;
static const float kIconChart = 13.0f;
static const float kIconSquare = 20.0f;
static const float kPi        = 3.14159265f;

static const ImU32 kColSelectFill = IM_COL32(255, 230, 120, 55);
static const ImU32 kColSelectLine = IM_COL32(255, 196, 40, 255);
static const ImU32 kColConeFill   = IM_COL32(255, 214, 40, 85);
static const ImU32 kColConeLine   = IM_COL32(255, 214, 40, 215);
static const ImU32 kColInk        = IM_COL32(20, 16, 10, 235);
static const ImU32 kColWhite      = IM_COL32(255, 255, 255, 255);
static const ImU32 kColAxis       = IM_COL32(205, 190, 150, 255);

struct ChartData {
    bool            ok;
    uint32_t        tile, grid, overview, paletteCount;
    const uint32_t* palette;
    const uint8_t*  overviewIdx;
    const uint8_t*  tiles;
};

static uint32_t be16(const uint8_t* p) { return ((uint32_t)p[0] << 8) | p[1]; }

static const ChartData& chart()
{
    static ChartData d;
    static bool parsed = false;
    if (parsed)
        return d;
    parsed = true;
    const uint8_t* b = sea_chart_bin;
    const size_t size = sea_chart_bin_size;
    if (size < 12 || b[0] != 'W' || b[1] != 'S' || b[2] != 'C' || b[3] != '1')
        return d;
    d.tile = be16(b + 4);
    d.grid = be16(b + 6);
    d.overview = be16(b + 8);
    d.paletteCount = be16(b + 10);
    const size_t need = 12 + (size_t)d.paletteCount * 4 + (size_t)d.overview * d.overview +
                        (size_t)d.grid * d.grid * d.tile * d.tile;
    if (d.grid != (uint32_t)Sea::GRID || d.paletteCount == 0 || d.paletteCount > 256 ||
        size < need)
        return d;
    d.palette = (const uint32_t*)(b + 12);
    d.overviewIdx = b + 12 + d.paletteCount * 4;
    d.tiles = d.overviewIdx + d.overview * d.overview;
    d.ok = true;
    return d;
}

static const Image::Texture* overviewTexture(const ChartData& d)
{
    return Image::LoadIndexed(d.overviewIdx, d.overview, d.overview, d.palette,
                              d.paletteCount, d.overviewIdx, d.overview);
}

static int                   s_tileShown = -1;
static const Image::Texture* s_tileTex = nullptr;

static const Image::Texture* tileTexture(const ChartData& d, int index)
{
    const uint8_t* idx = d.tiles + (size_t)index * d.tile * d.tile;
    const Image::Texture* t =
        Image::LoadIndexed(&s_tileShown, d.tile, d.tile, d.palette, d.paletteCount, idx, d.tile);
    if (!t)
        return nullptr;
    if (t != s_tileTex) {
        s_tileTex = t;
        s_tileShown = index;
    } else if (s_tileShown != index) {
        Image::UpdateIndexed(t, d.palette, d.paletteCount, idx, d.tile);
        s_tileShown = index;
    }
    return t;
}

static ImVec2 screenDir(s16 angle)
{
    const float a = (float)angle * (kPi / 32768.0f);
    return ImVec2(sinf(a), cosf(a));
}

static const char* compass(s16 angle)
{
    static const char* const kNames[8] = { "S", "SE", "E", "NE", "N", "NW", "W", "SW" };
    return kNames[((uint16_t)(angle + 0x1000)) >> 13];
}

static void drawIcon(ImDrawList* dl, const Image::Texture* tex, ImVec2 c, float r, s16 facing,
                     ImU32 ring)
{
    const ImVec2 d = screenDir(facing);
    const ImVec2 n(-d.y, d.x);
    const float w = r * 0.55f;
    const ImVec2 tip(c.x + d.x * (r + 10.0f), c.y + d.y * (r + 10.0f));
    const ImVec2 b1(c.x + d.x * (r - 2.0f) + n.x * w, c.y + d.y * (r - 2.0f) + n.y * w);
    const ImVec2 b2(c.x + d.x * (r - 2.0f) - n.x * w, c.y + d.y * (r - 2.0f) - n.y * w);
    dl->AddTriangleFilled(tip, b1, b2, ring);
    dl->AddTriangle(tip, b1, b2, kColInk, 1.5f);

    dl->AddCircleFilled(c, r + 2.0f, IM_COL32(0, 0, 0, 120));
    dl->AddCircle(c, r + 1.5f, ring, 0, 2.5f);
    if (tex)
        dl->AddImage(tex->id, ImVec2(c.x - r, c.y - r), ImVec2(c.x + r, c.y + r));
    else
        dl->AddCircleFilled(c, r, ring);
}

static void drawCursor(ImDrawList* dl, ImVec2 c, s16 facing, float coneRadius)
{
    const ImVec2 d = screenDir(facing);
    const float t = atan2f(d.y, d.x);
    const float half = 0.5f;
    dl->PathClear();
    dl->PathLineTo(c);
    dl->PathArcTo(c, coneRadius, t - half, t + half, 24);
    dl->PathFillConvex(kColConeFill);
    dl->PathClear();
    dl->PathLineTo(c);
    dl->PathArcTo(c, coneRadius, t - half, t + half, 24);
    dl->PathStroke(kColConeLine, ImDrawFlags_Closed, 2.0f);

    static const ImVec2 kArms[4] = { ImVec2(1, 0), ImVec2(-1, 0), ImVec2(0, 1), ImVec2(0, -1) };
    for (int pass = 0; pass < 2; ++pass) {
        const ImU32 col = pass == 0 ? kColInk : kColWhite;
        const float thick = pass == 0 ? 4.5f : 2.0f;
        for (int i = 0; i < 4; ++i) {
            const ImVec2 a(c.x + kArms[i].x * 8.0f, c.y + kArms[i].y * 8.0f);
            const ImVec2 b(c.x + kArms[i].x * 16.0f, c.y + kArms[i].y * 16.0f);
            dl->AddLine(a, b, col, thick);
        }
        dl->AddCircle(c, 5.5f, col, 0, thick);
    }
    dl->AddCircleFilled(c, 1.8f, kColWhite);
}

static void centeredText(ImDrawList* dl, ImVec2 center, ImU32 col, const char* text)
{
    const ImVec2 size = ImGui::CalcTextSize(text);
    dl->AddText(ImVec2(center.x - size.x * 0.5f, center.y - size.y * 0.5f), col, text);
}

static void actionButton(const char* label, Sea::Action action)
{
    const char* why = Sea::Blocker(action);
    ImGui::BeginDisabled(why != nullptr);
    if (ImGui::Button(label))
        Sea::Run(action);
    ImGui::EndDisabled();
    if (why && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", why);
}

void DrawSeaChartItem()
{
    Window::Checkbox("Sea Chart", &s_open, kWindowName);
}

struct Mapper {
    ImVec2 origin;
    float  side;
    bool   square;
    int    col, row;

    bool map(float u, float v, ImVec2* out) const
    {
        if (square) {
            u = u * (float)Sea::GRID - (float)col;
            v = v * (float)Sea::GRID - (float)row;
            if (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f)
                return false;
        }
        *out = ImVec2(origin.x + u * side, origin.y + v * side);
        return true;
    }
};

static void drawActors(ImDrawList* dl, const Mapper& m, float iconRadius)
{
    const Image::Texture* linkTex = Image::Load(sea_link_icon_bin, sea_link_icon_bin_size);
    const Image::Texture* boatTex = Image::Load(sea_boat_icon_bin, sea_boat_icon_bin_size);
    float u = 0.0f, v = 0.0f;
    s16 facing = 0;
    ImVec2 at;
    if (Sea::BoatOnChart(&u, &v, &facing) && m.map(u, v, &at))
        drawIcon(dl, boatTex, at, iconRadius, facing, IM_COL32(236, 96, 52, 255));
    if (Sea::LinkOnChart(&u, &v, &facing) && m.map(u, v, &at))
        drawIcon(dl, linkTex, at, iconRadius, facing, IM_COL32(96, 196, 88, 255));
}

void DrawSeaChartWindow()
{
    if (!s_open) {
        s_wasOpen = false;
        return;
    }
    Sea::State& st = Sea::Ui();
    if (!s_wasOpen) {
        s_wasOpen = true;
        st.focusMap = true;
    }

    ImGui::SetNextWindowPos(ImVec2(220.0f, 40.0f), ImGuiCond_FirstUseEver);
    if (!ImGui::Begin(kWindowName, &s_open,
                      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoFocusOnAppearing |
                      ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        return;
    }

    const ChartData& d = chart();
    const bool squareView = st.view == Sea::VIEW_SQUARE;
    const bool onSea = Sea::OnGreatSea();

    char square[48];
    Sea::SquareName(st.col, st.row, square, sizeof(square));
    if (squareView) {
        float x = 0.0f, z = 0.0f;
        Sea::CursorWorld(&x, &z);
        ImGui::Text("%s   x %.0f  z %.0f  facing %s", square, (double)x, (double)z,
                    compass(st.facing));
    } else {
        ImGui::Text("Selected: %s", square);
    }

    const ImVec2 p0 = ImGui::GetCursorScreenPos();
    const ImVec2 origin(p0.x + kMargin, p0.y + kMargin);
    const float side = kMapSide;
    const float cell = side / (float)Sea::GRID;

    ImGui::InvisibleButton("##seamap", ImVec2(side + kMargin, side + kMargin));
    if (st.focusMap) {
        ImGui::FocusItem();
        st.focusMap = false;
    }
    const bool mapFocused = ImGui::IsItemFocused();
    const bool clicked = ImGui::IsItemClicked(ImGuiMouseButton_Left);
    const bool dragging = ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left);
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const float mu = (mouse.x - origin.x) / side;
    const float mv = (mouse.y - origin.y) / side;

    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p1(origin.x + side, origin.y + side);
    dl->AddRectFilled(origin, p1, IM_COL32(40, 34, 24, 255));

    if (!squareView) {
        if (d.ok) {
            if (const Image::Texture* tex = overviewTexture(d))
                dl->AddImage(tex->id, origin, p1);
        }
        for (int i = 0; i < Sea::GRID; ++i) {
            char label[12];
            snprintf(label, sizeof(label), "%c", 'A' + i);
            centeredText(dl, ImVec2(origin.x + cell * ((float)i + 0.5f), p0.y + kMargin * 0.5f),
                         kColAxis, label);
            snprintf(label, sizeof(label), "%d", i + 1);
            centeredText(dl, ImVec2(p0.x + kMargin * 0.5f, origin.y + cell * ((float)i + 0.5f)),
                         kColAxis, label);
        }
        const ImVec2 s0(origin.x + cell * (float)st.col, origin.y + cell * (float)st.row);
        const ImVec2 s1(s0.x + cell, s0.y + cell);
        dl->AddRectFilled(s0, s1, kColSelectFill);
        dl->AddRect(s0, s1, kColInk, 0.0f, 0, 5.0f);
        dl->AddRect(s0, s1, kColSelectLine, 0.0f, 0, 3.0f);

        Mapper m = { origin, side, false, st.col, st.row };
        drawActors(dl, m, kIconChart);

        if (clicked && mu >= 0.0f && mu < 1.0f && mv >= 0.0f && mv < 1.0f) {
            const int col = (int)(mu * Sea::GRID), row = (int)(mv * Sea::GRID);
            if (col == st.col && row == st.row) {
                Sea::ZoomIn();
            } else {
                st.col = col;
                st.row = row;
            }
        }
    } else {
        if (d.ok) {
            if (const Image::Texture* tex = tileTexture(d, st.row * Sea::GRID + st.col))
                dl->AddImage(tex->id, origin, p1);
        }
        char label[12];
        snprintf(label, sizeof(label), "%c", 'A' + st.col);
        centeredText(dl, ImVec2(origin.x + side * 0.5f, p0.y + kMargin * 0.5f), kColAxis, label);
        snprintf(label, sizeof(label), "%d", st.row + 1);
        centeredText(dl, ImVec2(p0.x + kMargin * 0.5f, origin.y + side * 0.5f), kColAxis, label);

        if ((clicked || dragging) && mu >= 0.0f && mu <= 1.0f && mv >= 0.0f && mv <= 1.0f) {
            st.cu = mu;
            st.cv = mv;
        }

        Mapper m = { origin, side, true, st.col, st.row };
        drawActors(dl, m, kIconSquare);
        drawCursor(dl, ImVec2(origin.x + st.cu * side, origin.y + st.cv * side), st.facing,
                   side * 0.2f);
    }

    dl->AddRect(origin, p1, kColInk, 0.0f, 0, 2.0f);
    if (!onSea) {
        dl->AddRectFilled(origin, p1, IM_COL32(0, 0, 0, 120));
        centeredText(dl, ImVec2(origin.x + side * 0.5f, origin.y + side * 0.5f), kColWhite,
                     "Only on the Great Sea");
    } else if (Sea::IsBusy()) {
        centeredText(dl, ImVec2(origin.x + side * 0.5f, p1.y - 14.0f), kColWhite, "Moving ...");
    }

    const bool windowFocused = ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows);
    if (!squareView && mapFocused) {
        uint32_t claim = Input::BTN_A | Input::BTN_X | Input::BTN_Y;
        if (st.col > 0) claim |= Input::BTN_LEFT;
        if (st.col < Sea::GRID - 1) claim |= Input::BTN_RIGHT;
        if (st.row > 0) claim |= Input::BTN_UP;
        if (st.row < Sea::GRID - 1) claim |= Input::BTN_DOWN;
        Input::ClaimMenuButtons(claim);
        Input::ClaimMenuSticks();
    } else if (squareView && windowFocused) {
        uint32_t claim = Input::BTN_B;
        if (mapFocused)
            claim |= Input::BTN_A | Input::BTN_X | Input::BTN_Y;
        Input::ClaimMenuButtons(claim);
        Input::ClaimMenuSticks();
    }
    Sea::NoteWindow(windowFocused, mapFocused);

    if (!squareView) {
        if (ImGui::Button("Zoom in (A)"))
            Sea::ZoomIn();
        ImGui::SameLine();
        actionButton("Link to boat (X)", Sea::ACTION_LINK_TO_BOAT);
        ImGui::SameLine();
        actionButton("Boat to Link (Y)", Sea::ACTION_BOAT_TO_LINK);
        ImGui::TextDisabled("D-pad or left stick picks a square; tap it twice to zoom in.");
    } else {
        actionButton("Teleport Link (A)", Sea::ACTION_TELEPORT_LINK);
        ImGui::SameLine();
        actionButton("Teleport boat (X)", Sea::ACTION_TELEPORT_BOAT);
        ImGui::SameLine();
        actionButton("Link & boat (Y)", Sea::ACTION_TELEPORT_BOTH);
        ImGui::SameLine();
        if (ImGui::Button("Back (B)"))
            Sea::ZoomOut();
        ImGui::TextDisabled("Left stick or touch moves the cursor; right stick sets the facing.");
    }
    ImGui::End();
}
}
}
