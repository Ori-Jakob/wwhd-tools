#include "core/settings.h"

#include "cheats/cheats.h"
#include "core/hotkeys.h"
#include "core/rebind.h"
#include "hud/hud_frame_stats.h"
#include "hud/hud_game_info.h"
#include "hud/hud_input_viewer.h"
#include "ui/menu_nav.h"
#include "ui/quick_access.h"

namespace Config {
Settings g_settings;

void ResetToDefaults()
{
    Settings& s = g_settings;
    s.drawnScreen    = DRAWN_SCREEN_BOTH;
    s.uiScale        = 1.0f;
    s.hudOnGameScreen = true;
    s.hudToTv        = false;
    s.overlayOpacity = 1.0f;
    s.boldLetters    = false;

    s.toastsEnabled  = true;
    s.toastSeconds   = 3.0f;

    s.flyCamEnabled  = true;
    s.flyCamSpeed    = 20.0f;
    s.mssEnabled     = false;

    s.collisionView  = false;
    s.collisionAt    = true;
    s.collisionTg    = true;
    s.collisionCo    = true;
    s.collisionMesh  = false;
    s.collisionMass  = true;
    s.collisionDepth = false;
    s.collisionDepthInvert = false;
    s.collisionRange = 3000.0f;
    s.collisionMeshRange = 3000.0f;

    s.showInitToast  = true;

    s.watermarkEnabled = true;
    s.watermarkX     = 0.012f;
    s.watermarkY     = 0.020f;
    s.watermarkOpacity = 0.35f;
    s.watermarkSize    = 128.0f;

    Rebind::Cancel();
    Hotkeys::ResetToDefaults();
    Ui::Nav::ResetBindings();
    Cheats::ResetToDefaults();
    Ui::QuickAccess::ResetToDefaults();
    Hud::InputViewer::ResetToDefaults();
    Hud::GameInfo::ResetToDefaults();
    Hud::FrameStats::ResetToDefaults();
}
}
