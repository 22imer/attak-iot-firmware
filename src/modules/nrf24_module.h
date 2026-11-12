#pragma once

#include "core/module_status.h"

namespace nrf24 {
    void begin();

    // v2 toggle model: enabling starts the 2.4GHz jammer (constant carrier
    // hopping across channels 0..125); disabling stops the carrier and
    // powers the radio down. Safe to call repeatedly.
    void setEnabled(bool enabled);

    void poll();
    ModuleStatus status();
}
