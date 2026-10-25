#pragma once

// Profiler zones in the client's main loop: WXL_CORE_ZONE("World.Update") at the head of a
// function makes the rest of its body a zone on the profiler's timeline, beside the runtime's
// zones for events and mods. The profiler is WarcraftXL.dll's (wxl/core/profile.h); the macro is
// empty on Win32, which carries no runtime, and when the runtime is built with WXL_PROFILE off.

#if defined(WOWDECOMP_ARCH_64)
#include "wxl/core/profile.h"
#else
#define WXL_CORE_ZONE(name) static_cast<void>(0)
#endif
