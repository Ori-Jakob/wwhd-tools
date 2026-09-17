#include "ui/panels.h"
#include "ui/panels_inventory.h"

#include "libwwhd/libwwhd.h"
#include "ui/menu_nav.h"
#include "ui/ui_field.h"
#include "ui/ui_window.h"

#include "imgui.h"

#include <stdio.h>

namespace Ui {
namespace Panels {
static bool s_open = false;
static const char kWindowName[] = "Inventory Editor";

void DrawInventoryItem()
{
    Window::Checkbox("Inventory Editor", &s_open, kWindowName);
}

void DrawInventoryWindow()
{
    if (!s_open)
        return;

    ImGui::SetNextWindowSize(ImVec2(660.0f, 580.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowPos(ImVec2(110.0f, 90.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin(kWindowName, &s_open,
                     ImGuiWindowFlags_NoCollapse |
                     ImGuiWindowFlags_NoFocusOnAppearing)) {
        if (!dComIfGs_getPlayerSave()) {
            ImGui::TextDisabled("No save data is loaded. Start or load a file first.");
        } else if (ImGui::BeginTabBar("##inventory")) {
            Nav::RegisterCurrentTabBar();
            if (ImGui::BeginTabItem("Status")) {
                Inventory::DrawStatusTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Items")) {
                Inventory::DrawItemsTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Bags")) {
                Inventory::DrawBagsTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Charts & Sea")) {
                Inventory::DrawChartsTab();
                ImGui::EndTabItem();
            }
            if (ImGui::BeginTabItem("Gallery")) {
                Inventory::DrawGalleryTab();
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }
    }
    ImGui::End();
}

namespace Inventory {
static const float kWidth = 230.0f;

bool ComboById(const char* label, uint8_t* value, const uint8_t* ids,
               const char* const* names, int count, float width)
{
    int current = -1;
    for (int i = 0; i < count; ++i)
        if (ids[i] == *value)
            current = i;

    char unknown[24];
    const char* preview = names[0];
    if (current >= 0) {
        preview = names[current];
    } else {
        snprintf(unknown, sizeof(unknown), "Unknown (0x%02X)", (unsigned)*value);
        preview = unknown;
    }
    ImGui::SetNextItemWidth(width);
    bool changed = false;
    if (ImGui::BeginCombo(label, preview)) {
        for (int i = 0; i < count; ++i) {
            const bool selected = i == current;
            if (ImGui::Selectable(names[i], selected) && !selected) {
                *value = ids[i];
                changed = true;
            }
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}

void DrawStatusTab()
{
    ImGui::SeparatorText("Hearts");
    int containers = dSv_getMaxLife() / WWHD_HEART_QUARTERS;
    ImGui::SetNextItemWidth(kWidth);
    if (Field::SliderInt("Heart containers", &containers, 1, 20)) {
        dComIfGp_clearItemDeltas();
        dSv_setMaxLife((u16)(containers * WWHD_HEART_QUARTERS));
    }
    int life = dSv_getLife();
    const int maxLife = dSv_getMaxLife();
    ImGui::SetNextItemWidth(kWidth);
    if (Field::SliderInt("Current life", &life, 0, maxLife, "%d quarters")) {
        dComIfGp_clearItemDeltas();
        dSv_setLife((u16)life);
    }
    ImGui::SameLine();
    ImGui::TextDisabled("%d.%02d hearts", life / WWHD_HEART_QUARTERS,
                        (life % WWHD_HEART_QUARTERS) * 25);
    if (ImGui::Button("Full heal"))
        dSv_healFull();

    ImGui::SeparatorText("Magic");
    static const char* const kMeters[] = { "None", "Half", "Full" };
    const u8 maxMagic = dSv_getMaxMagic();
    int meter = maxMagic >= dSv_MAGIC_MAX ? 2 : maxMagic > 0 ? 1 : 0;
    ImGui::SetNextItemWidth(kWidth);
    if (ImGui::Combo("Magic meter", &meter, kMeters, 3)) {
        const u8 cap = meter == 2 ? (u8)dSv_MAGIC_MAX
                     : meter == 1 ? (u8)(dSv_MAGIC_MAX / 2) : (u8)0;
        dSv_setMaxMagic(cap);
        dSv_setMagic(cap);
    }
    if (maxMagic > 0) {
        int magic = dSv_getMagic();
        ImGui::SetNextItemWidth(kWidth);
        if (Field::SliderInt("Current magic", &magic, 0, maxMagic))
            dSv_setMagic((u8)magic);
    }

    ImGui::SeparatorText("Rupees");
    static const char* const kWallets[] = { "500", "1000", "5000" };
    int wallet = dSv_getWalletSize();
    if (wallet >= WWHD_WALLET_SIZES)
        wallet = WWHD_WALLET_SIZES - 1;
    ImGui::SetNextItemWidth(kWidth);
    if (ImGui::Combo("Wallet", &wallet, kWallets, WWHD_WALLET_SIZES)) {
        dComIfGp_clearItemDeltas();
        dSv_setWalletSize((u8)wallet);
        dMeter_setRupeeDisplay(dMeter_searchByProc(), dSv_getRupee());
    }
    int rupees = dSv_getRupee();
    const int cap = dSv_getMaxRupee();
    ImGui::SetNextItemWidth(kWidth);
    if (Field::SliderInt("Rupees", &rupees, 0, cap)) {
        dSv_setRupeeExact((u16)rupees);
        dMeter_setRupeeDisplay(dMeter_searchByProc(), (u16)rupees);
    }
    ImGui::SameLine();
    if (ImGui::Button("Fill")) {
        dSv_refillRupees();
        dMeter_setRupeeDisplay(dMeter_searchByProc(), dSv_getRupee());
    }
}
}
}
}
