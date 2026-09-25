#pragma once

#include "core/module_status.h"

namespace cc1101 {
    void begin();
    void poll();
    ModuleStatus status();
}
