#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>
#include "Engine/Source/Tempest/C3Vector.h"

#if defined(WOWDECOMP_ARCH_64)

#include "wxl/seams/seams.h"

#else

// Win32 carries no runtime: every seam call compiles to nothing. Before and After are function
// objects that take anything, so a call site reads the same on both architectures.
#include "wxl/abi.h"
#include "wxl/events.h"

namespace wxl::seam {
    // Nobody listens on Win32: the guarded seam calls are skipped like the others.
    inline constexpr bool Listening(wxl_event) {
        return false;
    }

    namespace detail {
        struct Nop {
            template <class... T>
            void operator()(T&&...) const {}
        };

        // A gated seam's Gate never takes the call, so a Win32 call site reads the same and
        // behaves as if no seam were there.
        struct NopGate {
            template <class... T>
            bool operator()(T...) const {
                return false;
            }
        };
    }

#define WXL_SEAM(domain, name, symbol, ret, conv, params) \
    namespace domain::name {                              \
        inline constexpr ::wxl::seam::detail::Nop Before{}; \
        inline constexpr ::wxl::seam::detail::Nop After{};  \
    }
#define WXL_SEAM_BEFORE(domain, name, symbol, ret, conv, params) \
    namespace domain::name {                                     \
        inline constexpr ::wxl::seam::detail::Nop Before{};      \
    }
#define WXL_SEAM_AFTER(domain, name, symbol, ret, conv, params) \
    namespace domain::name {                                    \
        inline constexpr ::wxl::seam::detail::Nop After{};      \
    }
#define WXL_SEAM_GATE(domain, name, symbol, ret, conv, params) \
    namespace domain::name {                                   \
        inline constexpr ::wxl::seam::detail::NopGate Gate{};  \
    }
#include "wxl/seams/seams.def"
#undef WXL_SEAM_GATE
#undef WXL_SEAM_AFTER
#undef WXL_SEAM_BEFORE
#undef WXL_SEAM
}

#endif

namespace WarcraftXL {
    static_assert(sizeof(C3Vector) == sizeof(wxl_vec3) && offsetof(C3Vector, x) == offsetof(wxl_vec3, x)
                      && offsetof(C3Vector, y) == offsetof(wxl_vec3, y) && offsetof(C3Vector, z) == offsetof(wxl_vec3, z),
                  "C3Vector and wxl_vec3 are the same three floats, or the seams cannot pass one as the other");

    // A small client value passed by value as the dword it is, for a seam row that declares it
    // as one (CGxTexFlags, a struct of bit fields).
    template <class T>
    inline uint32_t Bits(const T& value) {
        static_assert(sizeof(T) == sizeof(uint32_t), "a dword-sized value");
        uint32_t bits;
        std::memcpy(&bits, &value, sizeof bits);
        return bits;
    }

    // A client position as the seams take it.
    inline const wxl_vec3* Vec3(const C3Vector& v) {
        return reinterpret_cast<const wxl_vec3*>(&v);
    }
}
