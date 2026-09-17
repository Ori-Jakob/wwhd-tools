#include "ui/panels_inventory.h"

#include "libwwhd/libwwhd.h"

#include "imgui.h"

#include <stdio.h>

namespace Ui {
namespace Panels {
namespace Inventory {
static const char* const kIsleNames[WWHD_SEA_GRID_MAX] = {
    "Forsaken Fortress", "Star Island", "Northern Fairy Island", "Gale Isle",
    "Crescent Moon Island", "Seven-Star Isles", "Overlook Island",
    "Four-Eye Reef", "Mother and Child Isles", "Spectacle Island",
    "Windfall Island", "Pawprint Isle", "Dragon Roost Island",
    "Flight Control Platform", "Western Fairy Island", "Rock Spire Isle",
    "Tingle Island", "Northern Triangle Island", "Eastern Fairy Island",
    "Fire Mountain", "Star Belt Archipelago", "Three-Eye Reef",
    "Greatfish Isle", "Cyclops Reef", "Six-Eye Reef", "Tower of the Gods",
    "Eastern Triangle Island", "Thorned Fairy Island", "Needle Rock Isle",
    "Islet of Steel", "Stone Watcher Island", "Southern Triangle Island",
    "Private Oasis", "Bomb Island", "Bird's Peak Rock", "Diamond Steppe Island",
    "Five-Eye Reef", "Shark Island", "Southern Fairy Island", "Ice Ring Isle",
    "Forest Haven", "Cliff Plateau Isles", "Horseshoe Island", "Outset Island",
    "Headstone Island", "Two-Eye Reef", "Angular Isles", "Boating Course",
    "Five-Star Isles",
};

static void drawTreasureCharts()
{
    dSv_player_map_c* map = dComIfGs_getMap();
    if (!map)
        return;

    if (ImGui::Button("Own all")) {
        for (int id = 1; id <= dSv_CHART_MAX; ++id)
            if (!dComIfGs_isGetCollectMap(id))
                dComIfGs_onGetCollectMap(id);
    }
    ImGui::SameLine();
    if (ImGui::Button("Open all")) {
        for (int id = 1; id <= dSv_CHART_MAX; ++id)
            if (dComIfGs_isGetCollectMap(id))
                dComIfGs_onOpenCollectMap(id);
    }
    ImGui::SameLine();
    if (ImGui::Button("Forget all")) {
        for (int id = 1; id <= dSv_CHART_MAX; ++id) {
            dSv_map_offGetMap(map, id - 1);
            dSv_map_offOpenMap(map, id - 1);
            dSv_map_offCompleteMap(map, id - 1);
        }
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%d owned", dComIfGs_getCollectMapNum());

    const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_ScrollY;
    if (ImGui::BeginTable("##charts", 5, flags, ImVec2(0.0f, 260.0f))) {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Chart", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Owned", ImGuiTableColumnFlags_WidthFixed, 64.0f);
        ImGui::TableSetupColumn("Opened", ImGuiTableColumnFlags_WidthFixed, 64.0f);
        ImGui::TableSetupColumn("Salvaged", ImGuiTableColumnFlags_WidthFixed, 74.0f);
        ImGui::TableSetupColumn("Deciphered", ImGuiTableColumnFlags_WidthFixed, 86.0f);
        ImGui::TableHeadersRow();
        for (int id = 1; id <= dSv_CHART_MAX; ++id) {
            ImGui::PushID(id);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%s (%d)", wwhd_chartName(id), id);
            ImGui::TableSetColumnIndex(1);
            bool owned = dComIfGs_isGetCollectMap(id) != 0;
            if (ImGui::Checkbox("##own", &owned)) {
                if (owned)
                    dComIfGs_onGetCollectMap(id);
                else
                    dSv_map_offGetMap(map, id - 1);
            }
            ImGui::TableSetColumnIndex(2);
            bool opened = dComIfGs_isOpenCollectMap(id) != 0;
            if (ImGui::Checkbox("##open", &opened)) {
                if (opened)
                    dComIfGs_onOpenCollectMap(id);
                else
                    dSv_map_offOpenMap(map, id - 1);
            }
            ImGui::TableSetColumnIndex(3);
            if (dSv_map_chartIsSalvage(id)) {
                bool salvaged = dComIfGs_isCompleteCollectMap(id) != 0;
                if (ImGui::Checkbox("##done", &salvaged)) {
                    if (salvaged)
                        dComIfGs_onCompleteCollectMap(id);
                    else
                        dSv_map_offCompleteMap(map, id - 1);
                }
            }
            ImGui::TableSetColumnIndex(4);
            if (dSv_map_chartIsTriforce(id)) {
                bool done = dComIfGs_isCollectMapTriforce(id) != 0;
                if (ImGui::Checkbox("##deciphered", &done)) {
                    if (done)
                        dComIfGs_onCollectMapTriforce(id);
                    else
                        dSv_map_offTriforce(map, id - 1);
                }
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}

enum SectorState { SECTOR_HIDDEN = 0, SECTOR_SAILED, SECTOR_DRAWN };

static SectorState sectorState(int grid)
{
    if (dComIfGs_isSaveArriveGrid(grid))
        return SECTOR_DRAWN;
    return dComIfGs_isVisitGrid(grid) ? SECTOR_SAILED : SECTOR_HIDDEN;
}

static void cycleSector(dSv_player_map_c* map, int grid)
{
    switch (sectorState(grid)) {
    case SECTOR_HIDDEN:
        dSv_map_onVisitGrid(map, grid);
        break;
    case SECTOR_SAILED:
        dSv_map_onArriveGrid(map, grid);
        break;
    case SECTOR_DRAWN:
        dSv_map_offArriveGrid(map, grid);
        dSv_map_offVisitGrid(map, grid);
        break;
    }
}

static const ImVec4 kSectorColour[3] = {
    ImVec4(0.36f, 0.36f, 0.38f, 1.0f),
    ImVec4(0.20f, 0.45f, 0.85f, 1.0f),
    ImVec4(0.18f, 0.62f, 0.30f, 1.0f),
};
static const char* const kSectorStateName[3] = {
    "hidden", "sailed into", "drawn on the chart"
};

static void drawSeaChart()
{
    dSv_player_map_c* map = dComIfGs_getMap();
    if (!map)
        return;

    if (ImGui::Button("Set all sailed into"))
        dComIfGs_visitAllGrids();
    ImGui::SameLine();
    if (ImGui::Button("Reveal all sectors"))
        dComIfGs_revealAllGrids();
    ImGui::SameLine();
    ImGui::TextDisabled("%d sailed into, %d drawn", dComIfGs_getVisitGridNum(),
                        dComIfGs_getArriveGridNum());

    for (int s = 0; s < 3; ++s) {
        if (s)
            ImGui::SameLine();
        ImGui::ColorButton(kSectorStateName[s], kSectorColour[s],
                           ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoPicker,
                           ImVec2(14.0f, 14.0f));
        ImGui::SameLine(0.0f, 4.0f);
        ImGui::TextDisabled("%s", kSectorStateName[s]);
    }

    const float cell = 40.0f;
    const float gap = 4.0f;
    const float labelGap = 12.0f;
    const float boardX = ImGui::GetCursorPosX() + ImGui::CalcTextSize("7").x + labelGap;

    for (int col = 0; col < WWHD_SEA_GRID_W; ++col) {
        const char letter[2] = { (char)('A' + col), '\0' };
        if (col)
            ImGui::SameLine();
        ImGui::SetCursorPosX(boardX + col * (cell + gap) +
                             (cell - ImGui::CalcTextSize(letter).x) * 0.5f);
        ImGui::TextDisabled("%s", letter);
    }

    for (int row = 0; row < WWHD_SEA_GRID_W; ++row) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextDisabled("%d", row + 1);
        for (int col = 0; col < WWHD_SEA_GRID_W; ++col) {
            ImGui::SameLine();
            ImGui::SetCursorPosX(boardX + col * (cell + gap));
            const int grid = dMap_gridPos2GridNo(col - WWHD_SEA_GRID_HALF,
                                                 row - WWHD_SEA_GRID_HALF);
            const SectorState state = sectorState(grid);
            const ImVec4 base = kSectorColour[state];
            ImGui::PushID(grid);
            ImGui::PushStyleColor(ImGuiCol_Button, base);
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                  ImVec4(base.x + 0.12f, base.y + 0.12f, base.z + 0.12f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive,
                                  ImVec4(base.x - 0.08f, base.y - 0.08f, base.z - 0.08f, 1.0f));
            char label[4];
            snprintf(label, sizeof(label), "%c%d", 'A' + col, row + 1);
            if (ImGui::Button(label, ImVec2(cell, 0.0f)))
                cycleSector(map, grid);
            ImGui::PopStyleColor(3);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s\n%s", kIsleNames[grid], kSectorStateName[state]);
            ImGui::PopID();
        }
    }
}

void DrawChartsTab()
{
    ImGui::SeparatorText("Charts");
    drawTreasureCharts();
    ImGui::SeparatorText("Sea Chart");
    drawSeaChart();
}

void DrawGalleryTab()
{
    if (ImGui::Button("Own all figurines")) {
        for (int no = 0; no < WWHD_FIGURE_MAX; ++no)
            dSv_setFigure(no, 1);
    }
    ImGui::SameLine();
    if (ImGui::Button("Clear all")) {
        for (int no = 0; no < WWHD_FIGURE_MAX; ++no)
            dSv_setFigure(no, 0);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%d of %d owned", dSv_getFigureNum(), WWHD_FIGURE_MAX);

    const ImGuiTableFlags flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg |
                                  ImGuiTableFlags_ScrollY;
    if (ImGui::BeginTable("##figurines", 4, flags, ImVec2(0.0f, 380.0f))) {
        for (int no = 0; no < WWHD_FIGURE_MAX; ++no) {
            ImGui::TableNextColumn();
            ImGui::PushID(no);
            bool owned = dSv_isFigure(no) != 0;
            char label[20];
            snprintf(label, sizeof(label), "Figurine %d", no + 1);
            if (ImGui::Checkbox(label, &owned))
                dSv_setFigure(no, owned);
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
}
}
}
}
