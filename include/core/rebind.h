#pragma once

#include <stddef.h>
#include <stdint.h>

namespace Rebind {
enum Domain {
    DOMAIN_HOTKEYS = 0,
    DOMAIN_MENU,
    DOMAIN_COUNT
};

enum Phase {
    PHASE_IDLE = 0,
    PHASE_WAIT_RELEASE,
    PHASE_LISTENING,
    PHASE_CONFLICT,
};

void Begin(Domain domain, int index);
void Cancel();

Phase  CurrentPhase();
Domain CurrentDomain();
int    CurrentIndex();
bool   IsActive();
bool   BlocksMenuInput();
bool   IsCapturing(Domain domain, int index);

uint32_t LiveMask();
uint32_t LiveHeld();
int      SecondsLeft();

const char* Error(Domain domain);
void        ClearError(Domain domain);

bool     IsConflictPending();
uint32_t PendingMask();
void     ConflictNames(char* out, size_t size);
void     ConfirmConflict();
void     CancelConflict();

void Tick();
void OnApplicationStart();
}
