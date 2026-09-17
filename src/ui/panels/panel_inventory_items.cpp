#include "ui/panels_inventory.h"

#include "libwwhd/libwwhd.h"

#include "imgui.h"

namespace Ui {
namespace Panels {
namespace Inventory {
static const float kComboWidth = 210.0f;

static const uint8_t kSailIds[] = {
    (uint8_t)WWHD_ITEM_NONE, (uint8_t)dItemNo_SAIL,
    (uint8_t)dItemNo_SWIFT_SAIL,
};
static const char* const kSailNames[] = {
    "None", "Sail", "Swift Sail",
};

static const uint8_t kBowIds[] = {
    (uint8_t)WWHD_ITEM_NONE, (uint8_t)dItemNo_BOW,
    (uint8_t)dItemNo_FIRE_ICE_ARROWS, (uint8_t)dItemNo_LIGHT_ARROWS,
};
static const char* const kBowNames[] = {
    "None", "Bow", "Fire & Ice", "Fire & Ice & Light",
};

static void drawSlotItem(const wwhd_slotItem_t& item, int index)
{
    bool held = dSv_getItem(item.slot) == item.id;
    ImGui::PushID(index);
    if (ImGui::Checkbox(item.name, &held)) {
        if (held)
            dSv_setSlotItem(&item);
        else
            dSv_clearSlot(item.slot);
    }
    ImGui::PopID();
}

static void drawSlotCombo(const char* label, int slot, const uint8_t* ids,
                          const char* const* names, int count)
{
    uint8_t value = dSv_getItem(slot);
    if (!ComboById(label, &value, ids, names, count, kComboWidth))
        return;
    if (value == WWHD_ITEM_NONE)
        dSv_clearSlot(slot);
    else
        dSv_setSlotItem(dSv_findSlotItem(value));
}

static void drawBottles()
{
    uint8_t ids[1 + 10];
    const char* names[1 + 10];
    ids[0] = (uint8_t)WWHD_ITEM_NONE;
    names[0] = "No bottle";
    for (int i = 0; i < 10; ++i) {
        ids[1 + i] = wwhd_bottleContents[i].id;
        names[1 + i] = wwhd_bottleContents[i].name;
    }
    static const char* const kLabels[WWHD_BOTTLE_COUNT] = {
        "Bottle 1", "Bottle 2", "Bottle 3", "Bottle 4"
    };
    for (int b = 0; b < WWHD_BOTTLE_COUNT; ++b) {
        uint8_t value = dSv_getBottle(b);
        if (ComboById(kLabels[b], &value, ids, names, 11, kComboWidth))
            dSv_setBottle(b, value);
    }
}

static void drawEquipment()
{
    uint8_t swordIds[5];
    const char* swordNames[5];
    swordIds[0] = (uint8_t)WWHD_ITEM_NONE;
    swordNames[0] = "None";
    for (int i = 0; i < 4; ++i) {
        swordIds[1 + i] = wwhd_swords[i].id;
        swordNames[1 + i] = wwhd_swords[i].name;
    }
    uint8_t sword = dSv_getEquip(WWHD_EQUIP_SWORD);
    if (ComboById("Sword", &sword, swordIds, swordNames, 5, kComboWidth))
        dSv_setSword(sword);

    uint8_t shieldIds[3];
    const char* shieldNames[3];
    shieldIds[0] = (uint8_t)WWHD_ITEM_NONE;
    shieldNames[0] = "None";
    for (int i = 0; i < 2; ++i) {
        shieldIds[1 + i] = wwhd_shields[i].id;
        shieldNames[1 + i] = wwhd_shields[i].name;
    }
    uint8_t shield = dSv_getEquip(WWHD_EQUIP_SHIELD);
    if (ComboById("Shield", &shield, shieldIds, shieldNames, 3, kComboWidth))
        dSv_setShield(shield);

    bool bracelets = dSv_hasPowerBracelets() != 0;
    if (ImGui::Checkbox("Power Bracelets", &bracelets))
        dSv_setPowerBracelets(bracelets);
    bool pirates = dSv_isCollectBit(WWHD_COLLECT_PIRATES_CHARM, 0) != 0;
    if (ImGui::Checkbox("Pirate's Charm", &pirates))
        dSv_setCollectBit(WWHD_COLLECT_PIRATES_CHARM, 0, pirates);
    bool heros = dSv_isCollectBit(WWHD_COLLECT_HEROS_CHARM, 0) != 0;
    if (ImGui::Checkbox("Hero's Charm", &heros))
        dSv_setCollectBit(WWHD_COLLECT_HEROS_CHARM, 0, heros);
}

typedef int  (*HasBitFn)(int bit);
typedef void (*SetBitFn)(int bit, int on);

static void drawBitBank(const char* id, const wwhd_collectBit_t* bits, int count,
                        HasBitFn has, SetBitFn set)
{
    ImGui::PushID(id);
    if (ImGui::BeginTable(id, 2)) {
        for (int i = 0; i < count; ++i) {
            ImGui::TableNextColumn();
            bool on = has(bits[i].bit) != 0;
            if (ImGui::Checkbox(bits[i].name, &on))
                set(bits[i].bit, on);
        }
        ImGui::EndTable();
    }
    ImGui::PopID();
}

static const wwhd_collectBit_t kShards[8] = {
    { 0x61, 0, "Triforce Shard 1" }, { 0x62, 1, "Triforce Shard 2" },
    { 0x63, 2, "Triforce Shard 3" }, { 0x64, 3, "Triforce Shard 4" },
    { 0x65, 4, "Triforce Shard 5" }, { 0x66, 5, "Triforce Shard 6" },
    { 0x67, 6, "Triforce Shard 7" }, { 0x68, 7, "Triforce Shard 8" },
};

void DrawItemsTab()
{
    ImGui::SeparatorText("Inventory");
    if (ImGui::BeginTable("##slots", 2)) {
        for (int i = 0; i < WWHD_SLOT_ITEM_COUNT; ++i) {
            const wwhd_slotItem_t& item = wwhd_slotItems[i];
            if ((item.slot == dSv_SLOT_SAIL || item.slot == dSv_SLOT_BOW) &&
                item.tier != 0)
                continue;
            ImGui::TableNextColumn();
            if (item.slot == dSv_SLOT_SAIL)
                drawSlotCombo("Sail", item.slot, kSailIds, kSailNames, 3);
            else if (item.slot == dSv_SLOT_BOW)
                drawSlotCombo("Bow", item.slot, kBowIds, kBowNames, 4);
            else
                drawSlotItem(item, i);
        }
        ImGui::EndTable();
    }

    ImGui::SeparatorText("Bottles");
    drawBottles();

    ImGui::SeparatorText("Equipment");
    drawEquipment();

    ImGui::SeparatorText("Songs");
    drawBitBank("##songs", wwhd_songs, 6, dSv_hasSong, dSv_setSong);

    ImGui::SeparatorText("Pearls");
    drawBitBank("##pearls", wwhd_pearls, 3, dSv_hasPearl, dSv_setPearl);

    ImGui::SeparatorText("Triforce Shards");
    drawBitBank("##shards", kShards, 8, dSv_hasShard, dSv_setShard);
}
}
}
}
