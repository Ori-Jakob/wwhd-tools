#pragma once

#include "core/rebind.h"

#include <stdint.h>

namespace Ui {
namespace RebindUi {
void DrawBindingCell(Rebind::Domain domain, int index, uint32_t bound);
void DrawRebindButton(Rebind::Domain domain, int index);
void DrawError(Rebind::Domain domain);
void DrawConflictPopup(Rebind::Domain domain, const char* title);
}
}
