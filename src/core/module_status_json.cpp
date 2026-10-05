// Pure JSON encoding for ModuleStatus — no Arduino/network dependency, so
// this file compiles on both the ESP32 build and the native test env.
#include "module_status.h"

#include <ArduinoJson.h>

const char *actionStateName(ActionState state) {
    switch (state) {
    case ActionState::Idle: return "idle";
    case ActionState::Running: return "running";
    case ActionState::Succeeded: return "succeeded";
    case ActionState::Timeout: return "timeout";
    case ActionState::Error: return "error";
    }
    return "idle";
}

const char *actionErrorName(ActionError error) {
    switch (error) {
    case ActionError::None: return "";
    case ActionError::ScanFailed: return "scan_failed";
    case ActionError::ScanTimeout: return "scan_timeout";
    case ActionError::ReadTimeout: return "read_timeout";
    case ActionError::CaptureTimeout: return "capture_timeout";
    case ActionError::CaptureTooLong: return "capture_too_long";
    case ActionError::HardwareError: return "hardware_error";
    }
    return "";
}

std::string statusToJson(const ModuleStatus &status) {
    JsonDocument doc;
    doc["module"] = status.name;
    doc["enabled"] = status.enabled;
    doc["connected"] = status.connected;
    doc["detail"] = status.detail;
    doc["output"] = status.output;
    doc["lastUpdateMs"] = status.lastUpdateMs;
    doc["actionState"] = actionStateName(status.actionState);
    doc["actionError"] = actionErrorName(status.actionError);
    doc["cleanupPending"] = status.cleanupPending;
    doc["resultSequence"] = status.resultSequence;
    doc["resultUpdateMs"] = status.resultUpdateMs;

    std::string out;
    serializeJson(doc, out);
    return out;
}
