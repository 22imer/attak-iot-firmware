#pragma once

#include "core/module_status.h"

namespace nrf24 {
    void begin();
    void poll();
    ModuleStatus status();
}
