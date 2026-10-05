#include "status_health.h"

HealthLevel aggregateHealth(const ModuleStatus *const *modules, size_t count) {
    bool anyEnabled = false;
    bool anyDisconnected = false;
    for (size_t i = 0; i < count; ++i) {
        const ModuleStatus *module = modules[i];
        if (module == nullptr || !module->enabled) continue;
        anyEnabled = true;
        if (!module->connected) anyDisconnected = true;
    }
    if (!anyEnabled) return HealthLevel::Off;
    return anyDisconnected ? HealthLevel::Degraded : HealthLevel::Healthy;
}
