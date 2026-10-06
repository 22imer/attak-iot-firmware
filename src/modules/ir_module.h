#pragma once

#include "core/command_types.h"
#include "core/module_status.h"
#include "core/action_params.h"

struct ActionOutput;

namespace ir {
void begin();
CommandError setEnabled(bool enabled);
CommandError handleAction(ActionId action, const ActionParams &params);
void poll();
const ModuleStatus &status();
uint32_t revision();
bool takeActionOutput(ActionOutput &output);
} // namespace ir
