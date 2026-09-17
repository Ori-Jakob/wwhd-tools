#include "ui/overlay.h"

#include "cheats/cheats.h"
#include "core/config.h"
#include "core/frame_stats.h"
#include "core/hotkeys.h"
#include "core/input.h"
#include "core/logger.h"
#include "core/rebind.h"
#include "core/settings.h"
#include "hud/hud_collision.h"
#include "hud/hud_frame_stats.h"
#include "hud/hud_game_info.h"
#include "hud/hud_input_viewer.h"
#include "render/renderer.h"
#include "tools/flycam.h"
#include "tools/mss.h"
#include "tools/save_loader.h"
#include "tools/save_states.h"
#include "tools/stage_control.h"
#include "ui/init_toast.h"
#include "ui/menu.h"
#include "ui/menu_nav.h"
#include "ui/notifications.h"
#include "ui/osk.h"
#include "ui/quick_access.h"
#include "ui/ui_field.h"
#include "ui/ui_window.h"
#include "ui/watermark.h"

#include "libwupatch/wupatch.h"
#include "libwwhd/libwwhd.h"

#include "imgui.h"

#include <gx2/surface.h>

namespace Ui {
namespace Overlay {
static bool s_menuOpen = false;
static bool s_started = false;
static bool s_initToastShown = false;

bool IsMenuOpen() { return s_menuOpen; }

static bool overlayOwnsInput()
{
    return s_menuOpen || QuickAccess::IsPageFocused();
}

void SetMenuOpen(bool open)
{
    if (s_menuOpen == open)
        return;
    s_menuOpen = open;
    Logger::Log("menu %s (drawn screen %u, display mode %u)",
                open ? "open" : "closed",
                (unsigned)Config::g_settings.drawnScreen,
                (unsigned)wwhd_getDisplayMode());

    if (!open)
        Rebind::Cancel();
    if (open && !QuickAccess::OnMenuOpened())
        Menu::OnOpened();
    Input::SetBlockGameInput(overlayOwnsInput());
}

void OnApplicationStart()
{
    s_started = true;
    s_initToastShown = false;
    s_menuOpen = false;

    Input::OnApplicationStart();
    Hotkeys::OnApplicationStart();
    Rebind::OnApplicationStart();
    Config::OnApplicationStart();
    Menu::OnApplicationStart();
    Osk::OnApplicationStart();
    Tools::FlyCam::OnApplicationStart();
    Tools::Mss::OnApplicationStart();
    Tools::SaveStates::OnApplicationStart();
    Tools::SaveLoader::OnApplicationStart();
    Hud::GameInfo::OnApplicationStart();
    ::FrameStats::OnApplicationStart();
    Notifications::Clear();
}

void OnApplicationEnd()
{
    if (s_started)
        Config::Flush();
    s_started = false;
    s_menuOpen = false;
    Tools::FlyCam::OnApplicationEnd();
    Tools::SaveStates::OnApplicationEnd();
    Renderer::ResetDeviceObjects();
    Notifications::Clear();
}

static bool blockWanted(uint32_t held, bool touchOnWidget)
{
    return overlayOwnsInput() || Watermark::IsInteracting() || touchOnWidget ||
           Hotkeys::OverlayComboHeld(held) || Tools::FlyCam::IsActive();
}

void OnPadSampled()
{
    uint32_t held = 0;
    float tx = -1.0f, ty = -1.0f;
    if (!Input::PeekLive(&held, &tx, &ty))
        return;
    const bool onWidget = tx >= 0.0f && Watermark::HitTest(tx, ty);
    Input::SetBlockGameInput(blockWanted(held, onWidget));
}

static bool touchReachesMenu()
{
    return Config::g_settings.drawnScreen != Config::DRAWN_SCREEN_TV ||
           Osk::IsOpen();
}

// Both and TV fall back to the GamePad in off-TV play; Gamepad hands the TV the game layers only.
ScreenContent ContentForScreen(bool isTv)
{
    const bool tvShowing = wwhd_isTvShowingGame() != 0;

    bool full;
    switch (Config::g_settings.drawnScreen) {
    case Config::DRAWN_SCREEN_GAMEPAD: full = !isTv; break;
    case Config::DRAWN_SCREEN_TV:      full = tvShowing ? isTv : !isTv; break;
    default:                           full = isTv ? tvShowing : true; break;
    }

    if (full)
        return SCREEN_ALL;
    if (isTv)
        return Config::g_settings.drawnScreen == Config::DRAWN_SCREEN_GAMEPAD && tvShowing
                   ? SCREEN_GAME : SCREEN_NOTHING;
    return SCREEN_TOP;
}

bool HasContentFor(ScreenContent content, bool isTv)
{
    if (content == SCREEN_NOTHING)
        return false;
    if (content == SCREEN_ALL)
        return true;
    if (content == SCREEN_GAME)
        return Renderer::HasWorldContent() ||
               (Config::g_settings.hudToTv && Renderer::HasGameScreenListContent());
    return Renderer::HasTopLayerContent() ||
           (!isTv && Renderer::HasGamePadOnlyContent());
}

void Tick()
{
    Input::Sample();
    Input::BeginFrame();
    Config::Poll();

    if (Input::HotkeyToggled())
        SetMenuOpen(!s_menuOpen);
    if (Watermark::Tick(!Osk::IsOpen()))
        SetMenuOpen(!s_menuOpen);
    if (Input::QuickAccessToggled())
        QuickAccess::OnHotkey(s_menuOpen);

    if (!Rebind::BlocksMenuInput() && QuickAccess::HandleBack())
        Input::DrainHeld();
    const bool ownsInput = overlayOwnsInput();
    Input::SetBlockGameInput(blockWanted(Input::Current().held, false));
    Hotkeys::Tick();
    Rebind::Tick();

    const bool toolsActive = !ownsInput && !Rebind::IsActive();
    Tools::FlyCam::Tick(toolsActive);
    const bool gameTools = toolsActive && !Tools::FlyCam::IsActive();
    Cheats::Tick(gameTools);
    Tools::SaveStates::Tick(gameTools);
    Tools::SaveLoader::Tick();
    Tools::StageControl::Tick(gameTools);

    static int s_gameOnTv = -1;
    const int gameOnTv = wwhd_isTvShowingGame() != 0;
    if (gameOnTv != s_gameOnTv) {
        s_gameOnTv = gameOnTv;
        Logger::Log("game is on the %s (display mode %u); the world layer follows it",
                    gameOnTv ? "TV" : "GamePad", (unsigned)wwhd_getDisplayMode());
    }

    WuPatch::Tick();
}

bool PrepareFrame(float logicalWidth, float logicalHeight)
{
    const bool ownsInput = overlayOwnsInput();

    const float uiScale =
        Config::g_settings.uiScale < 1.0f ? 1.0f : Config::g_settings.uiScale;
    const float lw = logicalWidth / uiScale;
    const float lh = logicalHeight / uiScale;
    Renderer::SetUiScale(uiScale);

    if (!Renderer::IsReady()) {
        Renderer::Init(lw, lh);
        if (!Renderer::IsReady())
            return false;
    }

    const float dt = 1.0f / 60.0f;
    Renderer::NewFrame(lw, lh, dt);

    const bool oskOwnsInput = Osk::ProcessInput(ImGui::GetIO());
    uint32_t feedFlags =
        Field::IsSteeringSlider() ? Input::FEED_NO_HORIZONTAL : 0u;
    if (!touchReachesMenu() || Watermark::IsInteracting())
        feedFlags |= Input::FEED_NO_TOUCH;
    if (Rebind::BlocksMenuInput())
        feedFlags |= Input::FEED_NO_BUTTONS;
    Input::FeedMenu(ImGui::GetIO(), lw, lh, ownsInput && !oskOwnsInput, feedFlags);

    if (!s_initToastShown) {
        s_initToastShown = true;
        if (Config::g_settings.showInitToast)
            InitToast::Arm();
    }

    if (s_menuOpen)
        Menu::Draw(ImGui::GetIO());
    Watermark::Draw();

    if (!InitToast::IsActive() || ownsInput) {
        Hud::Collision::Draw(lw, lh);
        Hud::GameInfo::DrawWindow(s_menuOpen);
        Hud::InputViewer::DrawWindow(s_menuOpen);
        Hud::FrameStats::DrawWindow(s_menuOpen);
        QuickAccess::DrawPageWindow(s_menuOpen);
    }

    InitToast::Draw(ImGui::GetIO());
    Osk::Draw();

    Window::ResolveFocusRequest();
    Nav::Update(ImGui::GetIO(), Input::Current(),
                s_menuOpen && !oskOwnsInput && !Rebind::BlocksMenuInput());

    if (Config::g_settings.toastsEnabled)
        Notifications::Draw(ImGui::GetIO());

    Renderer::FinishFrame();
    return true;
}

// The world layer and the HUD readouts follow the game screen, not drawnScreen.
static bool isGameScreen(bool isTv)
{
    return isTv == (wwhd_isTvShowingGame() != 0);
}

void DrawPrepared(GX2ColorBuffer* target, ScreenContent content, bool isTv)
{
    if (!target || content == SCREEN_NOTHING)
        return;
    ::FrameStats::OnOverlayGpuBegin();
    if (content == SCREEN_TOP) {
        Renderer::DrawPreparedTopLayer(target);
    } else if (content == SCREEN_GAME) {
        Renderer::DrawGameLayers(target, Config::g_settings.hudToTv);
    } else {
        const bool gameScreen = isGameScreen(isTv);
        const ScreenContent other = ContentForScreen(!isTv);
        const bool hudElsewhere = !gameScreen && Config::g_settings.hudOnGameScreen &&
                                  (other == SCREEN_ALL ||
                                   (other == SCREEN_GAME && Config::g_settings.hudToTv));
        Renderer::DrawPrepared(target, gameScreen, !hudElsewhere);
    }
    if (!isTv)
        Renderer::DrawGamePadOnlyLayer(target);
}
}
}
