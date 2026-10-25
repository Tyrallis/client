#pragma once

// The points of wxl.point on the recompiled client: what the WXL_POINT_<n>(...) line at the head
// of each listed function calls (see Points.gen.h, generated, and core/include/wxl/points/
// points.txt, the list). Win32 carries no runtime: every line compiles to nothing there.

#include <cstdint>

#if defined(WOWDECOMP_ARCH_64)
#include "wxl/core/points.h"
#define WXL_POINT_LIVE 1
#endif

#include "WarcraftXL/Points.gen.h"

#if defined(WXL_POINT_LIVE)

#if defined(__clang__) || defined(__GNUC__)
#define WXL_POINT_UNLIKELY(x) __builtin_expect(!!(x), 0)
// The attached path of a point line, out of line and out of the hot code.
#define WXL_POINT_COLD __attribute__((noinline, cold))
#else
#define WXL_POINT_UNLIKELY(x) (x)
#define WXL_POINT_COLD
#endif

namespace wxl::point {
    /// One byte per point, set by the runtime while a mod is attached (WarcraftXLPoints).
    inline volatile uint8_t g_armed[WXL_POINT_COUNT] = {};

    /// Set while a point's original runs, so the head of its function lets that one call through.
    inline thread_local uint8_t t_bypass[WXL_POINT_COUNT] = {};

    /// Whether this call of point @p n goes to the runtime: armed, and not the original's own call.
    inline bool Enters(uint32_t n) {
        if (!WXL_POINT_UNLIKELY(g_armed[n])) {
            return false;
        }
        if (t_bypass[n]) {
            t_bypass[n] = 0;
            return false;
        }
        return true;
    }

    /// Lets the next call of point @p n on this thread through, for the time the original runs.
    struct Bypass {
        explicit Bypass(uint32_t n) : n(n) {
            t_bypass[n] = 1;
        }
        ~Bypass() {
            t_bypass[n] = 0;
        }
        Bypass(const Bypass&) = delete;
        Bypass& operator=(const Bypass&) = delete;
        uint32_t n;
    };

    inline void Enter(uint32_t n, void* const* args, void* result, wxl_core_point_original original) {
        wxl_core_point_enter(n, args, result, original);
    }
}

#endif
