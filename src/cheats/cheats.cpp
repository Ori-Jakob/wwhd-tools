#include "cheats/cheats.h"

#include "cheats/cheat_equipment.h"
#include "cheats/cheat_movement.h"
#include "cheats/cheat_sailing.h"
#include "cheats/cheat_status.h"
#include "cheats/cheat_storage.h"
#include "cheats/cheat_text.h"

namespace Cheats {
void Tick(bool acceptInput)
{
    Status::Tick();
    Equipment::Tick(acceptInput);
    Movement::Tick(acceptInput);
    Sailing::Tick(acceptInput);
    Storage::Tick(acceptInput);
    Text::Tick(acceptInput);
}

void ResetToDefaults()
{
    Status::ResetToDefaults();
    Equipment::ResetToDefaults();
    Movement::ResetToDefaults();
    Sailing::ResetToDefaults();
    Storage::ResetToDefaults();
    Text::ResetToDefaults();
}
}
