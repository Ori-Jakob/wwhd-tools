#include "ui/panels_inventory.h"

#include "libwwhd/libwwhd.h"
#include "ui/ui_field.h"

#include "imgui.h"

namespace Ui {
namespace Panels {
namespace Inventory {
static const float kSliderWidth = 150.0f;
static const float kComboWidth = 120.0f;

static const uint8_t kAmmoCaps[4] = {
    WWHD_AMMO_CAP_NONE, WWHD_AMMO_CAP_30, WWHD_AMMO_CAP_60, WWHD_AMMO_CAP_99
};
static const char* const kAmmoCapNames[4] = { "None", "30", "60", "99" };

static void drawAmmo(const char* capLabel, const char* countLabel, uint8_t cap,
                     uint8_t count, void (*setCap)(u8), void (*setCount)(u8))
{
    uint8_t newCap = cap;
    if (ComboById(capLabel, &newCap, kAmmoCaps, kAmmoCapNames, 4, kComboWidth))
        setCap(newCap);
    if (cap == 0)
        return;
    ImGui::SameLine();
    int value = count;
    ImGui::SetNextItemWidth(kSliderWidth);
    if (Field::SliderInt(countLabel, &value, 0, cap))
        setCount((u8)value);
}

static void drawCountedBag(const char* id, const wwhd_bagItem_t* items, int count,
                           int (*has)(int), void (*set)(int, int),
                           u8 (*getNum)(int), void (*setNum)(int, u8))
{
    ImGui::PushID(id);
    if (ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Item", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("Count", ImGuiTableColumnFlags_WidthFixed, 170.0f);
        for (int i = 0; i < count; ++i) {
            const int index = items[i].index;
            ImGui::PushID(i);
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            bool owned = has(index) != 0;
            if (ImGui::Checkbox(items[i].name, &owned))
                set(index, owned);
            ImGui::TableSetColumnIndex(1);
            if (owned) {
                int n = getNum(index);
                ImGui::SetNextItemWidth(kSliderWidth);
                if (Field::SliderInt("##count", &n, 0, 99))
                    setNum(index, (u8)n);
            }
            ImGui::PopID();
        }
        ImGui::EndTable();
    }
    ImGui::PopID();
}

static void drawDelivery()
{
    if (ImGui::BeginTable("##delivery", 2)) {
        for (int i = 0; i < 21; ++i) {
            ImGui::TableNextColumn();
            bool owned = dSv_hasDeliveryItem(&wwhd_deliveryItems[i]) != 0;
            if (ImGui::Checkbox(wwhd_deliveryItems[i].name, &owned))
                dSv_setDeliveryItem(&wwhd_deliveryItems[i], owned);
        }
        ImGui::EndTable();
    }
    ImGui::TextDisabled("Up to eight show, in pickup order.");
}

static void drawDungeonItems()
{
    static const struct { const char* name; int bit; } kItems[3] = {
        { "Dungeon Map", WWHD_DUNGEON_ITEM_MAP },
        { "Compass",     WWHD_DUNGEON_ITEM_COMPASS },
        { "Big Key",     WWHD_DUNGEON_ITEM_BIG_KEY },
    };
    if (!dSv_getCurrentMemBit()) {
        ImGui::TextDisabled("No stage memory is available.");
        return;
    }
    for (int i = 0; i < 3; ++i) {
        bool on = dSv_isDungeonItem(kItems[i].bit) != 0;
        if (ImGui::Checkbox(kItems[i].name, &on))
            dSv_setDungeonItem(kItems[i].bit, on);
        if (i + 1 < 3)
            ImGui::SameLine();
    }
    ImGui::TextDisabled("For the dungeon Link is standing in.");
}

void DrawBagsTab()
{
    ImGui::SeparatorText("Quiver and Bomb Bag");
    drawAmmo("Quiver", "Arrows", dSv_getMaxArrowNum(), dSv_getArrowNum(),
             dSv_setMaxArrowNum, dSv_setArrowNum);
    drawAmmo("Bomb Bag", "Bombs", dSv_getMaxBombNum(), dSv_getBombNum(),
             dSv_setMaxBombNum, dSv_setBombNum);

    ImGui::SeparatorText("Spoils Bag");
    drawCountedBag("##spoils", wwhd_spoils, 8, dSv_hasSpoil, dSv_setSpoil,
                   dSv_getSpoilNum, dSv_setSpoilNum);

    ImGui::SeparatorText("Bait Bag");
    drawCountedBag("##bait", wwhd_baits, 2, dSv_hasBait, dSv_setBait,
                   dSv_getBaitNum, dSv_setBaitNum);

    ImGui::SeparatorText("Delivery Bag");
    drawDelivery();

    ImGui::SeparatorText("Dungeon Items");
    drawDungeonItems();
}
}
}
}
