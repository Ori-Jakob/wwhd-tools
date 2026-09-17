#pragma once

#include <stdint.h>

namespace Config {
enum DrawnScreen {
    DRAWN_SCREEN_BOTH    = 0,
    DRAWN_SCREEN_GAMEPAD = 1,
    DRAWN_SCREEN_TV      = 2,
};

struct Settings {
    uint32_t drawnScreen;
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
