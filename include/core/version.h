#pragma once

#if __has_include("wwhd_version.h")
#include "wwhd_version.h"
#endif

#ifndef WWHD_TOOLS_VERSION_BASE
#define WWHD_TOOLS_VERSION_BASE "dev"
#endif

#ifdef WWHD_TOOLS_DEBUG
#define WWHD_TOOLS_VERSION WWHD_TOOLS_VERSION_BASE "-debug"
#else
#define WWHD_TOOLS_VERSION WWHD_TOOLS_VERSION_BASE
#endif

#define WWHD_TOOLS_AUTHOR "n0ted"
