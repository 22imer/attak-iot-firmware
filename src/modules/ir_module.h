#pragma once

#include "core/module_status.h"

namespace ir {
    void begin();
    void poll();
    ModuleStatus status();
}
