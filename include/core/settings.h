#pragma once

#include <stdint.h>

namespace Config {
enum DrawnScreen {
    DRAWN_SCREEN_BOTH    = 0,
    DRAWN_SCREEN_GAMEPAD = 1,
    DRAWN_SCREEN_TV      = 2,
};

enum LoadController {
    LOAD_CONTROLLER_AUTO    = 0,
    LOAD_CONTROLLER_GAMEPAD = 1,
    LOAD_CONTROLLER_PRO     = 2,
};

struct Settings {
    uint32_t drawnScreen;
    uint32_t loadController;
    float    uiScale;
    bool     hudOnGameScreen;
    bool     hudToTv;
    float    overlayOpacity;
    bool     boldLetters;

    bool     toastsEnabled;
    float    toastSeconds;

    bool     flyCamEnabled;
    float    flyCamSpeed;
    bool     mssEnabled;

    bool     modernCam;
    bool     modernCamSailing;
    float    camSensX;
    float    camSensY;
    float    cameraFov;
    bool     cameraFovAll;

    bool     zombieHoverEnabled;
    bool     zombieHoverHeal;
    bool     zombieHoverSimPerfect;

    bool     collisionView;
    bool     collisionAt;
    bool     collisionTg;
    bool     collisionCo;
    bool     collisionMesh;
    bool     collisionMass;
    bool     collisionDepth;
    bool     collisionDepthInvert;
    float    collisionRange;
    float    collisionMeshRange;

    bool     showInitToast;

    bool     watermarkEnabled;
    float    watermarkX;
    float    watermarkY;
    float    watermarkOpacity;
    float    watermarkSize;
};

extern Settings g_settings;

void ResetToDefaults();
}
