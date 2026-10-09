#pragma once

// Keep native platform detection here; use #if PLATFORM_* everywhere else.
// Match fury3d's convention: the active platform is 1, all others are 0.
#if defined(_WIN32)
#define PLATFORM_WINDOWS 1
#elif defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_OSX
#define PLATFORM_MACOS 1
#else
#error "Unsupported Apple platform"
#endif
#elif defined(__linux__)
#define PLATFORM_LINUX 1
#else
#error "Unsupported platform: add a PLATFORM_* definition in platform.h"
#endif

#ifndef PLATFORM_WINDOWS
#define PLATFORM_WINDOWS 0
#endif
#ifndef PLATFORM_MACOS
#define PLATFORM_MACOS 0
#endif
#ifndef PLATFORM_LINUX
#define PLATFORM_LINUX 0
#endif
