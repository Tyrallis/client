#include "WarcraftXL/Init.h"

#include "WarcraftXL/Binding.h"
#include "WarcraftXL/Points.h"
#include "wxl/core/init.h"

namespace WarcraftXL {
    void Init() {
        // The runtime is a DLL and reads the client only through what it is handed here: every
        // row of the binding table, in table order, each the function Binding.cpp defines for it,
        // and the rows themselves, which the runtime checks against its own.
        wxl_core_init init = {};
        init.struct_size = sizeof(init);
        init.row_count = WXL_CORE_INIT_ROW_COUNT;
        init.rows = wxl_core_init_rows;
#define WXL_BINDING(name, ret, params, args, absent) init.name = &WarcraftXL::Binding::name;
#include "wxl/core/binding.def"
#undef WXL_BINDING

        // The points' bytes, which the head of each listed function tests; handed over before the
        // mods load, so one may attach as it loads.
        static const char* const pointRows[] = WXL_POINT_ROWS;
        wxl_core_points points = {};
        points.struct_size = sizeof(points);
        points.count = WXL_POINT_COUNT;
        points.rows = pointRows;
        points.armed = wxl::point::g_armed;
        WarcraftXLPoints(&points);

        WarcraftXLInit(&init);
    }
}
