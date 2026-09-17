#include "core/config.h"

#include "cheats/cheat_movement.h"
#include "cheats/cheat_sailing.h"
#include "cheats/cheat_status.h"
#include "cheats/cheat_storage.h"
#include "cheats/cheat_text.h"
#include "core/config_schema.h"
#include "core/hotkeys.h"
#include "core/logger.h"
#include "core/settings.h"
#include "core/storage.h"
#include "hud/hud_frame_stats.h"
#include "hud/hud_game_info.h"
#include "hud/hud_input_viewer.h"
#include "ui/menu_nav.h"
#include "ui/quick_access.h"
#include "ui/window_state.h"

#include "cJSON.h"

#include <coreinit/time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace Config {
static const uint32_t kMaxConfigBytes = 64 * 1024;
static const int64_t  kSaveCoalesceMs = 1000;
static const float    kMaxWindowCoord = 4096.0f;

static bool    s_loaded = false;
static bool    s_dirty = false;
static int64_t s_dirtySinceMs = 0;
static char    s_error[128] = "";

static int64_t nowMs()
{
    return (int64_t)OSTicksToMilliseconds(OSGetTime());
}

bool IsLoaded()          { return s_loaded; }
const char* LastError()  { return s_error; }

void OnApplicationStart()
{
    ResetToDefaults();
    s_loaded = false;
    s_dirty = false;
    s_dirtySinceMs = 0;
    s_error[0] = '\0';
}

static void readBool(cJSON* root, const char* key, bool* out)
{
    cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsBool(item))
        *out = cJSON_IsTrue(item) != 0;
}

static void readFloat(cJSON* root, const char* key, float* out, float lo, float hi)
{
    cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (!cJSON_IsNumber(item))
        return;
    float v = (float)item->valuedouble;
    if (v < lo) v = lo;
    if (v > hi) v = hi;
    *out = v;
}

static void readU32(cJSON* root, const char* key, uint32_t* out)
{
    cJSON* item = cJSON_GetObjectItemCaseSensitive(root, key);
    if (cJSON_IsNumber(item) && item->valuedouble >= 0.0)
        *out = (uint32_t)item->valuedouble;
}

static void* fieldPtr(const Field& f)
{
    return (char*)&g_settings + f.offset;
}

static void loadSettings(cJSON* root)
{
    for (unsigned i = 0; i < kSettingsSchemaCount; ++i) {
        const Field& f = kSettingsSchema[i];
        switch (f.type) {
        case FIELD_BOOL:  readBool (root, f.key, (bool*)fieldPtr(f)); break;
        case FIELD_FLOAT: readFloat(root, f.key, (float*)fieldPtr(f), f.lo, f.hi); break;
        case FIELD_U32:   readU32  (root, f.key, (uint32_t*)fieldPtr(f)); break;
        }
    }
}

static void saveSettings(cJSON* root)
{
    for (unsigned i = 0; i < kSettingsSchemaCount; ++i) {
        const Field& f = kSettingsSchema[i];
        switch (f.type) {
        case FIELD_BOOL:
            cJSON_AddBoolToObject(root, f.key, *(bool*)fieldPtr(f));
            break;
        case FIELD_FLOAT:
            cJSON_AddNumberToObject(root, f.key, (double)*(float*)fieldPtr(f));
            break;
        case FIELD_U32:
            cJSON_AddNumberToObject(root, f.key, (double)*(uint32_t*)fieldPtr(f));
            break;
        }
    }
}

static void keyFor(char* out, size_t size, const char* prefix, const char* suffix)
{
    snprintf(out, size, "%s%s", prefix, suffix);
}

static void loadWindow(cJSON* root, const char* prefix, Ui::WindowState& st,
                       float minW, float maxW)
{
    char key[64];
    keyFor(key, sizeof(key), prefix, "Enabled");
    readBool(root, key, &st.enabled);
    keyFor(key, sizeof(key), prefix, "X");
    readFloat(root, key, &st.x, -kMaxWindowCoord, kMaxWindowCoord);
    keyFor(key, sizeof(key), prefix, "Y");
    readFloat(root, key, &st.y, -kMaxWindowCoord, kMaxWindowCoord);
    keyFor(key, sizeof(key), prefix, "W");
    readFloat(root, key, &st.w, minW, maxW);
}

static void saveWindow(cJSON* root, const char* prefix, const Ui::WindowState& st)
{
    char key[64];
    keyFor(key, sizeof(key), prefix, "Enabled");
    cJSON_AddBoolToObject(root, key, st.enabled);
    keyFor(key, sizeof(key), prefix, "X");
    cJSON_AddNumberToObject(root, key, (double)st.x);
    keyFor(key, sizeof(key), prefix, "Y");
    cJSON_AddNumberToObject(root, key, (double)st.y);
    keyFor(key, sizeof(key), prefix, "W");
    cJSON_AddNumberToObject(root, key, (double)st.w);
}

static void loadHotkeys(cJSON* root)
{
    for (int i = 0; i < Hotkeys::HOTKEY_COUNT; ++i) {
        const Hotkeys::Id id = (Hotkeys::Id)i;
        uint32_t combo = Hotkeys::Get(id);
        readU32(root, Hotkeys::ConfigKey(id), &combo);
        Hotkeys::Set(id, combo);
    }
}

static void saveHotkeys(cJSON* root)
{
    for (int i = 0; i < Hotkeys::HOTKEY_COUNT; ++i) {
        const Hotkeys::Id id = (Hotkeys::Id)i;
        cJSON_AddNumberToObject(root, Hotkeys::ConfigKey(id), (double)Hotkeys::Get(id));
    }
}

static void loadNavBindings(cJSON* root)
{
    for (int i = 0; i < Ui::Nav::ACTION_COUNT; ++i) {
        const Ui::Nav::Action action = (Ui::Nav::Action)i;
        uint32_t button = Ui::Nav::Binding(action);
        readU32(root, Ui::Nav::ConfigKey(action), &button);

        if (button == 0 || !Ui::Nav::Validate(action, button))
            Ui::Nav::SetBinding(action, button);
    }
}

static void saveNavBindings(cJSON* root)
{
    for (int i = 0; i < Ui::Nav::ACTION_COUNT; ++i) {
        const Ui::Nav::Action action = (Ui::Nav::Action)i;
        cJSON_AddNumberToObject(root, Ui::Nav::ConfigKey(action),
                                (double)Ui::Nav::Binding(action));
    }
}

static void loadCheats(cJSON* root)
{
    using namespace Cheats;
    for (int i = 0; i < Status::PIN_COUNT; ++i) {
        const Status::Pin pin = (Status::Pin)i;
        bool on = Status::IsEnabled(pin);
        readBool(root, Status::ConfigKey(pin), &on);
        Status::SetEnabled(pin, on);
    }

    bool moonJump = Movement::MoonJumpEnabled();
    readBool(root, "cheatMoonJump", &moonJump);
    Movement::SetMoonJumpEnabled(moonJump);

    bool speedMods = Movement::SpeedModifiersEnabled();
    readBool(root, "cheatSpeedModifiers", &speedMods);
    Movement::SetSpeedModifiersEnabled(speedMods);

    bool swimSpeedControls = Movement::SwimSpeedControlsEnabled();
    readBool(root, "cheatSwimSpeedControls", &swimSpeedControls);
    Movement::SetSwimSpeedControlsEnabled(swimSpeedControls);

    float swimMul = (float)Movement::SwimMultiplier();
    readFloat(root, "cheatSwimMultiplier", &swimMul,
              (float)Movement::SPEED_MULTIPLIER_MIN,
              (float)Movement::SPEED_MULTIPLIER_MAX);
    Movement::SetSwimMultiplier((int)swimMul);

    float crawlMul = (float)Movement::CrawlMultiplier();
    readFloat(root, "cheatCrawlMultiplier", &crawlMul,
              (float)Movement::CRAWL_MULTIPLIER_MIN,
              (float)Movement::CRAWL_MULTIPLIER_MAX);
    Movement::SetCrawlMultiplier((int)crawlMul);

    float moveMul = (float)Movement::MoveMultiplier();
    readFloat(root, "cheatMoveMultiplier", &moveMul,
              (float)Movement::MOVE_MULTIPLIER_MIN,
              (float)Movement::MOVE_MULTIPLIER_MAX);
    Movement::SetMoveMultiplier((int)moveMul);

    bool launch = Movement::LaunchEnabled();
    readBool(root, "cheatLaunch", &launch);
    Movement::SetLaunchEnabled(launch);

    float launchVelocity = (float)Movement::LaunchVelocity();
    readFloat(root, "cheatLaunchVelocity", &launchVelocity,
              (float)Movement::LAUNCH_VELOCITY_MIN,
              (float)Movement::LAUNCH_VELOCITY_MAX);
    Movement::SetLaunchVelocity((int)launchVelocity);

    float launchRamp = Movement::LaunchRampSeconds();
    readFloat(root, "cheatLaunchRamp", &launchRamp,
              Movement::LAUNCH_RAMP_MIN_SECONDS, Movement::LAUNCH_RAMP_MAX_SECONDS);
    Movement::SetLaunchRampSeconds(launchRamp);

    bool launchReversed = Movement::LaunchReversed();
    readBool(root, "cheatLaunchReverse", &launchReversed);
    Movement::SetLaunchReversed(launchReversed);

    bool storage = Cheats::Storage::Enabled();
    readBool(root, "cheatStorage", &storage);
    Cheats::Storage::SetEnabled(storage);

    bool boatJump = Sailing::MoonJumpEnabled();
    readBool(root, "cheatBoatMoonJump", &boatJump);
    Sailing::SetMoonJumpEnabled(boatJump);

    bool autoWind = Sailing::AutoWindEnabled();
    readBool(root, "cheatAutoWind", &autoWind);
    Sailing::SetAutoWindEnabled(autoWind);

    float windDir = (float)Sailing::WindDirection();
    readFloat(root, "cheatWindDirection", &windDir, 0.0f,
              (float)(Sailing::WIND_DIR_COUNT - 1));
    Sailing::SetWindDirection((int)windDir);

    bool boost = Sailing::BoostEnabled();
    readBool(root, "cheatBoatBoost", &boost);
    Sailing::SetBoostEnabled(boost);

    float boatMul = (float)Sailing::BoostMultiplier();
    readFloat(root, "cheatBoatMultiplier", &boatMul,
              (float)Sailing::BOOST_MULTIPLIER_MIN,
              (float)Sailing::BOOST_MULTIPLIER_MAX);
    Sailing::SetBoostMultiplier((int)boatMul);

    bool textAdvance = Text::AutoAdvanceEnabled();
    readBool(root, "cheatTextAutoAdvance", &textAdvance);
    Text::SetAutoAdvanceEnabled(textAdvance);
}

static void saveCheats(cJSON* root)
{
    using namespace Cheats;
    for (int i = 0; i < Status::PIN_COUNT; ++i) {
        const Status::Pin pin = (Status::Pin)i;
        cJSON_AddBoolToObject(root, Status::ConfigKey(pin), Status::IsEnabled(pin));
    }
    cJSON_AddBoolToObject  (root, "cheatMoonJump",       Movement::MoonJumpEnabled());
    cJSON_AddBoolToObject  (root, "cheatSpeedModifiers", Movement::SpeedModifiersEnabled());
    cJSON_AddBoolToObject  (root, "cheatSwimSpeedControls", Movement::SwimSpeedControlsEnabled());
    cJSON_AddNumberToObject(root, "cheatSwimMultiplier", Movement::SwimMultiplier());
    cJSON_AddNumberToObject(root, "cheatMoveMultiplier", Movement::MoveMultiplier());
    cJSON_AddNumberToObject(root, "cheatCrawlMultiplier", Movement::CrawlMultiplier());
    cJSON_AddBoolToObject  (root, "cheatLaunch",         Movement::LaunchEnabled());
    cJSON_AddNumberToObject(root, "cheatLaunchVelocity", Movement::LaunchVelocity());
    cJSON_AddBoolToObject  (root, "cheatLaunchReverse",  Movement::LaunchReversed());
    cJSON_AddNumberToObject(root, "cheatLaunchRamp",     (double)Movement::LaunchRampSeconds());
    cJSON_AddBoolToObject  (root, "cheatStorage",        Cheats::Storage::Enabled());
    cJSON_AddBoolToObject  (root, "cheatBoatMoonJump",   Sailing::MoonJumpEnabled());
    cJSON_AddBoolToObject  (root, "cheatAutoWind",       Sailing::AutoWindEnabled());
    cJSON_AddNumberToObject(root, "cheatWindDirection", Sailing::WindDirection());
    cJSON_AddBoolToObject  (root, "cheatBoatBoost",      Sailing::BoostEnabled());
    cJSON_AddNumberToObject(root, "cheatBoatMultiplier", Sailing::BoostMultiplier());
    cJSON_AddBoolToObject  (root, "cheatTextAutoAdvance", Text::AutoAdvanceEnabled());
}

static void loadGameInfoRows(cJSON* root)
{
    uint32_t rows = Hud::GameInfo::GetVisibleRows();
    readU32(root, "gameInfoRows", &rows);

    cJSON* order = cJSON_GetObjectItemCaseSensitive(root, "gameInfoOrder");
    const int savedCount = cJSON_IsArray(order) ? cJSON_GetArraySize(order) : 0;
    if (savedCount > 0 && savedCount < (int)Hud::GameInfo::ROW_COUNT)
        rows |= Hud::GameInfo::ALL_VISIBLE_ROWS & ~((1u << savedCount) - 1u);
    Hud::GameInfo::SetVisibleRows(rows);

    bool procNames = Hud::GameInfo::GetShowProcNames();
    readBool(root, "gameInfoProcNames", &procNames);
    Hud::GameInfo::SetShowProcNames(procNames);

    bool boatValues = Hud::GameInfo::GetBoatValues();
    readBool(root, "gameInfoBoatValues", &boatValues);
    Hud::GameInfo::SetBoatValues(boatValues);

    if (savedCount != (int)Hud::GameInfo::ROW_COUNT)
        return;

    uint32_t parsed[Hud::GameInfo::ROW_COUNT];
    for (unsigned i = 0; i < Hud::GameInfo::ROW_COUNT; ++i) {
        cJSON* item = cJSON_GetArrayItem(order, (int)i);
        if (!cJSON_IsNumber(item) || item->valuedouble < 0.0)
            return;
        parsed[i] = (uint32_t)item->valuedouble;
    }
    Hud::GameInfo::SetRowOrder(parsed, Hud::GameInfo::ROW_COUNT);
}

static void loadFrameStatsRows(cJSON* root)
{
    uint32_t rows = Hud::FrameStats::GetVisibleRows();
    readU32(root, "frameStatsRows", &rows);
    Hud::FrameStats::SetVisibleRows(rows);

    uint32_t series = Hud::FrameStats::GetGraphSeries();
    readU32(root, "frameStatsGraph", &series);
    Hud::FrameStats::SetGraphSeries(series);

    cJSON* order = cJSON_GetObjectItemCaseSensitive(root, "frameStatsOrder");
    if (!cJSON_IsArray(order) || cJSON_GetArraySize(order) != (int)Hud::FrameStats::ROW_COUNT)
        return;
    uint32_t parsed[Hud::FrameStats::ROW_COUNT];
    for (unsigned i = 0; i < Hud::FrameStats::ROW_COUNT; ++i) {
        cJSON* item = cJSON_GetArrayItem(order, (int)i);
        if (!cJSON_IsNumber(item) || item->valuedouble < 0.0)
            return;
        parsed[i] = (uint32_t)item->valuedouble;
    }
    Hud::FrameStats::SetRowOrder(parsed, Hud::FrameStats::ROW_COUNT);
}

static void loadQuickAccess(cJSON* root)
{
    cJSON* items = cJSON_GetObjectItemCaseSensitive(root, "quickAccessItems");
    if (!cJSON_IsArray(items))
        return;
    Ui::QuickAccess::ClearItems();
    cJSON* item = nullptr;
    cJSON_ArrayForEach(item, items) {
        if (cJSON_IsString(item) && item->valuestring)
            Ui::QuickAccess::AddItem(item->valuestring);
    }
}

static void saveQuickAccess(cJSON* root)
{
    cJSON* items = cJSON_AddArrayToObject(root, "quickAccessItems");
    if (!items)
        return;
    for (int i = 0; i < Ui::QuickAccess::ItemCount(); ++i)
        cJSON_AddItemToArray(items,
                             cJSON_CreateString(Ui::QuickAccess::ItemId(i)));
}

static void applyJson(const char* text)
{
    cJSON* root = cJSON_Parse(text);
    if (!root) {
        const char* err = cJSON_GetErrorPtr();
        snprintf(s_error, sizeof(s_error), "parse error near '%.24s'", err ? err : "?");
        Logger::LogWarn("[config] %s -- keeping defaults", s_error);
        return;
    }

    loadSettings(root);
    loadHotkeys(root);
    loadNavBindings(root);
    loadCheats(root);
    loadQuickAccess(root);

    loadWindow(root, "gameInfo", Hud::GameInfo::State(),
               Hud::GameInfo::MIN_WIDTH, Hud::GameInfo::MAX_WIDTH);
    loadGameInfoRows(root);
    Hud::GameInfo::ApplyState();

    loadWindow(root, "inputViewer", Hud::InputViewer::State(),
               Hud::InputViewer::MIN_WIDTH, Hud::InputViewer::MAX_WIDTH);
    float inputOpacity = Hud::InputViewer::GetOpacity();
    readFloat(root, "inputViewerOpacity", &inputOpacity, 0.0f, 1.0f);
    Hud::InputViewer::SetOpacity(inputOpacity);
    bool inputInts = Hud::InputViewer::GetIntReadout();
    readBool(root, "inputViewerInts", &inputInts);
    Hud::InputViewer::SetIntReadout(inputInts);
    Hud::InputViewer::ApplyState();

    loadWindow(root, "frameStats", Hud::FrameStats::State(),
               Hud::FrameStats::MIN_WIDTH, Hud::FrameStats::MAX_WIDTH);
    loadFrameStatsRows(root);
    Hud::FrameStats::ApplyState();

    cJSON_Delete(root);
}

void Poll()
{
    if (!s_loaded) {
        char* text = nullptr;
        Storage::ReadResult r = Storage::LoadConfig(&text, kMaxConfigBytes);

        if (r == Storage::READ_RETRY)
            return;

        if (r == Storage::READ_OK && text) {
            applyJson(text);
            Logger::Log("[config] loaded from %s", Storage::RootPath());
        } else if (r == Storage::READ_MISSING) {
            Logger::Log("[config] no config.json yet -- using defaults");
            s_dirty = true;
            s_dirtySinceMs = nowMs();
        } else {
            snprintf(s_error, sizeof(s_error), "read failed (%d)", (int)r);
            Logger::LogWarn("[config] %s", s_error);
        }

        free(text);
        s_loaded = true;
        return;
    }

    if (s_dirty && (nowMs() - s_dirtySinceMs) >= kSaveCoalesceMs)
        Flush();
}

void MarkDirty()
{
    if (!s_dirty) {
        s_dirty = true;
        s_dirtySinceMs = nowMs();
    }
}

void Flush()
{
    if (!s_dirty)
        return;
    s_dirty = false;

    cJSON* root = cJSON_CreateObject();
    if (!root) {
        snprintf(s_error, sizeof(s_error), "out of memory building JSON");
        return;
    }

    saveSettings(root);
    saveHotkeys(root);
    saveNavBindings(root);
    saveCheats(root);
    saveQuickAccess(root);

    saveWindow(root, "gameInfo", Hud::GameInfo::State());
    cJSON_AddNumberToObject(root, "gameInfoRows", Hud::GameInfo::GetVisibleRows());
    cJSON_AddBoolToObject(root, "gameInfoProcNames", Hud::GameInfo::GetShowProcNames());
    cJSON_AddBoolToObject(root, "gameInfoBoatValues", Hud::GameInfo::GetBoatValues());
    uint32_t order[Hud::GameInfo::ROW_COUNT];
    Hud::GameInfo::GetRowOrder(order, Hud::GameInfo::ROW_COUNT);
    if (cJSON* orderJson = cJSON_AddArrayToObject(root, "gameInfoOrder"))
        for (unsigned i = 0; i < Hud::GameInfo::ROW_COUNT; ++i)
            cJSON_AddItemToArray(orderJson, cJSON_CreateNumber((double)order[i]));

    saveWindow(root, "inputViewer", Hud::InputViewer::State());
    cJSON_AddNumberToObject(root, "inputViewerOpacity", Hud::InputViewer::GetOpacity());
    cJSON_AddBoolToObject(root, "inputViewerInts", Hud::InputViewer::GetIntReadout());

    saveWindow(root, "frameStats", Hud::FrameStats::State());
    cJSON_AddNumberToObject(root, "frameStatsRows", Hud::FrameStats::GetVisibleRows());
    cJSON_AddNumberToObject(root, "frameStatsGraph", Hud::FrameStats::GetGraphSeries());
    uint32_t statsOrder[Hud::FrameStats::ROW_COUNT];
    Hud::FrameStats::GetRowOrder(statsOrder, Hud::FrameStats::ROW_COUNT);
    if (cJSON* statsJson = cJSON_AddArrayToObject(root, "frameStatsOrder"))
        for (unsigned i = 0; i < Hud::FrameStats::ROW_COUNT; ++i)
            cJSON_AddItemToArray(statsJson, cJSON_CreateNumber((double)statsOrder[i]));

    char* text = cJSON_Print(root);
    cJSON_Delete(root);
    if (!text) {
        snprintf(s_error, sizeof(s_error), "out of memory printing JSON");
        return;
    }

    if (!Storage::SaveConfig(text)) {
        snprintf(s_error, sizeof(s_error), "write failed");
        Logger::LogWarn("[config] %s", s_error);
    } else {
        s_error[0] = '\0';
    }

    free(text);
}
}
