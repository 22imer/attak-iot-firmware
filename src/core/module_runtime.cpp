#include "module_runtime.h"

namespace {

bool isTimeoutError(ActionError error) {
    return error == ActionError::ScanTimeout || error == ActionError::ReadTimeout ||
           error == ActionError::CaptureTimeout;
}

} // namespace

const char *moduleName(ModuleId id) {
    switch (id) {
    case ModuleId::Cc1101: return "cc1101";
    case ModuleId::Nrf24: return "nrf24";
    case ModuleId::Pn532: return "pn532";
    case ModuleId::Ir: return "ir";
    case ModuleId::Wifi: return "wifi";
    case ModuleId::Unknown: return "";
    }
    return "";
}

const char *commandErrorName(CommandError error) {
    switch (error) {
    case CommandError::None: return "";
    case CommandError::InvalidCommand: return "invalid_command";
    case CommandError::UnsupportedAction: return "unsupported_action";
    case CommandError::QueueFull: return "queue_full";
    case CommandError::ModuleOff: return "module_off";
    case CommandError::Busy: return "busy";
    case CommandError::HardwareError: return "hardware_error";
    }
    return "invalid_command";
}

ModuleRuntime::ModuleRuntime(const char *name)
    : current_{name, false, false, "off", "", 0, ActionState::Idle, ActionError::None, false, 0, 0} {}

const ModuleStatus &ModuleRuntime::status() const { return current_; }

uint32_t ModuleRuntime::revision() const { return revision_; }

void ModuleRuntime::setEnabled(bool enabled, uint32_t now, bool cleanupRequired) {
    if (enabled) {
        if (current_.enabled) return; // idempotent — no re-init, no action reset
        current_.enabled = true;
        current_.lastUpdateMs = now;
        ++revision_;
        return;
    }

    const bool changed = current_.enabled || (cleanupRequired && !current_.cleanupPending);
    ++epoch_; // invalidate any in-flight ticket
    current_.enabled = false;
    current_.output.clear();
    current_.resultUpdateMs = 0;
    current_.actionState = ActionState::Idle;
    current_.actionError = ActionError::None;
    current_.cleanupPending = current_.cleanupPending || cleanupRequired;
    if (changed) {
        current_.lastUpdateMs = now;
        ++revision_;
    }
}

void ModuleRuntime::setHealth(bool connected, const std::string &detail, uint32_t now) {
    if (current_.connected == connected && current_.detail == detail) return;
    current_.connected = connected;
    current_.detail = detail;
    current_.lastUpdateMs = now;
    ++revision_;
}

CommandError ModuleRuntime::beginAction(uint32_t now, uint32_t deadlineMs, uint32_t &ticket) {
    if (!current_.enabled) return CommandError::ModuleOff;
    if (current_.cleanupPending || current_.actionState == ActionState::Running) return CommandError::Busy;
    if (!current_.connected) return CommandError::HardwareError;

    ticket = ++epoch_;
    startedMs_ = now;
    deadlineMs_ = deadlineMs;
    current_.actionState = ActionState::Running;
    current_.actionError = ActionError::None;
    current_.lastUpdateMs = now;
    ++revision_;
    return CommandError::None;
}

bool ModuleRuntime::completeAction(uint32_t ticket, std::string payload, uint32_t now) {
    if (ticket != epoch_ || current_.actionState != ActionState::Running) return false;

    current_.output = std::move(payload);
    ++current_.resultSequence;
    current_.resultUpdateMs = now;
    current_.lastUpdateMs = now;
    current_.actionState = ActionState::Succeeded;
    current_.actionError = ActionError::None;
    ++revision_;
    return true;
}

bool ModuleRuntime::failAction(uint32_t ticket, ActionError error, uint32_t now, bool cleanupRequired) {
    if (ticket != epoch_ || current_.actionState != ActionState::Running) return false;

    current_.actionState = isTimeoutError(error) ? ActionState::Timeout : ActionState::Error;
    current_.actionError = error;
    current_.cleanupPending = current_.cleanupPending || cleanupRequired;
    ++epoch_; // late completions from this ticket are rejected
    current_.lastUpdateMs = now;
    ++revision_;
    return true;
}

bool ModuleRuntime::expire(uint32_t now, ActionError timeoutError, bool cleanupRequired) {
    if (current_.actionState != ActionState::Running) return false;
    if (static_cast<uint32_t>(now - startedMs_) < deadlineMs_) return false;

    current_.actionState = ActionState::Timeout;
    current_.actionError = timeoutError;
    current_.cleanupPending = current_.cleanupPending || cleanupRequired;
    ++epoch_;
    current_.lastUpdateMs = now;
    ++revision_;
    return true;
}

void ModuleRuntime::finishCleanup(uint32_t now) {
    if (!current_.cleanupPending) return;
    current_.cleanupPending = false;
    current_.lastUpdateMs = now;
    ++revision_;
}
