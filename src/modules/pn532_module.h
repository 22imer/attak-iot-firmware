#pragma once

#include "core/module_status.h"

namespace pn532 {
    void begin();
    void poll();
    ModuleStatus status();
}
