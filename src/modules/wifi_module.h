#pragma once

#include "core/command_types.h"
#include "core/module_status.h"

namespace wifiModule {
void begin();
CommandError setEnabled(bool enabled);
CommandError handleAction(ActionId action);
void poll();
const ModuleStatus &status();
uint32_t revision();
} // namespace wifiModule
