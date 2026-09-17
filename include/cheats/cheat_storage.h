#pragma once

namespace Cheats {
namespace Storage {
enum Collision {
    COLLISION_NORMAL = 0,
    COLLISION_CHEST,
    COLLISION_DOOR,
    COLLISION_OTHER,
};

bool Enabled();
void SetEnabled(bool enabled);

bool IsArmed();
bool Arm();
void Clear();

Collision   CurrentCollision();
const char* CollisionName(Collision collision);
void        SetCollision(Collision want);
void        RestoreCollision();
void        ToggleCollision(Collision want);

void Tick(bool acceptInput);
void ResetToDefaults();
}
}
