#pragma once

#include "core/settings.h"

#include <stddef.h>

namespace Config {
enum FieldType { FIELD_BOOL, FIELD_FLOAT, FIELD_U32 };

struct Field {
    const char* key;
    FieldType   type;
    size_t      offset;
    float       lo, hi;
};

#define WWHD_SETTING(name, type, lo, hi) \
    { #name, type, offsetof(Settings, name), lo, hi }

static const Field kSettingsSchema[] = {
    WWHD_SETTING(drawnScreen,    FIELD_U32,   0.0f,   2.0f),
    WWHD_SETTING(uiScale,        FIELD_FLOAT, 1.0f,   2.0f),
    WWHD_SETTING(hudOnGameScreen, FIELD_BOOL, 0.0f,   0.0f),
    WWHD_SETTING(hudToTv,        FIELD_BOOL,  0.0f,   0.0f),
    WWHD_SETTING(overlayOpacity, FIELD_FLOAT, 0.0f,   1.0f),
    WWHD_SETTING(boldLetters,    FIELD_BOOL,  0.0f,   0.0f),

    WWHD_SETTING(toastsEnabled,  FIELD_BOOL,  0.0f,   0.0f),
    WWHD_SETTING(toastSeconds,   FIELD_FLOAT, 0.5f,  15.0f),

    WWHD_SETTING(flyCamEnabled,  FIELD_BOOL,  0.0f,   0.0f),
    WWHD_SETTING(flyCamSpeed,    FIELD_FLOAT, 5.0f, 500.0f),
    WWHD_SETTING(mssEnabled,     FIELD_BOOL,  0.0f,   0.0f),

    WWHD_SETTING(collisionView,  FIELD_BOOL,  0.0f,   0.0f),
    WWHD_SETTING(collisionAt,    FIELD_BOOL,  0.0f,   0.0f),
    WWHD_SETTING(collisionTg,    FIELD_BOOL,  0.0f,   0.0f),
    WWHD_SETTING(collisionCo,    FIELD_BOOL,  0.0f,   0.0f),
    WWHD_SETTING(collisionMesh,  FIELD_BOOL,  0.0f,   0.0f),
    WWHD_SETTING(collisionMass,  FIELD_BOOL,  0.0f,   0.0f),
    WWHD_SETTING(collisionDepth, FIELD_BOOL,  0.0f,   0.0f),
    WWHD_SETTING(collisionDepthInvert, FIELD_BOOL, 0.0f, 0.0f),
    WWHD_SETTING(collisionRange, FIELD_FLOAT, 0.0f, 50000.0f),
    WWHD_SETTING(collisionMeshRange, FIELD_FLOAT, 100.0f, 20000.0f),

    WWHD_SETTING(showInitToast,  FIELD_BOOL,  0.0f,   0.0f),

    WWHD_SETTING(watermarkEnabled, FIELD_BOOL, 0.0f,  0.0f),
    WWHD_SETTING(watermarkX,     FIELD_FLOAT, 0.0f,   1.0f),
    WWHD_SETTING(watermarkY,     FIELD_FLOAT, 0.0f,   1.0f),
    WWHD_SETTING(watermarkOpacity, FIELD_FLOAT, 0.05f,  1.0f),
    WWHD_SETTING(watermarkSize,  FIELD_FLOAT, 32.0f, 320.0f),
};

#undef WWHD_SETTING

static const unsigned kSettingsSchemaCount =
    sizeof(kSettingsSchema) / sizeof(kSettingsSchema[0]);
}
