// Enabled-only RGB health aggregate. Pure (no FastLED) so the rules are
// native-testable; status_led.cpp wraps the LED driver.
#pragma once

#include <cstddef>
#include <cstdint>

#include "core/module_status.h"

enum class HealthLevel : uint8_t { Off, Healthy, Degraded };

// Off when no category is enabled. Healthy when every enabled category is
// connected. Degraded when at least one enabled category is disconnected.
// Disabled categories (even if their stale connected flag is true) are ignored,
// and an action timeout/error never changes chip health.
HealthLevel aggregateHealth(const ModuleStatus *const *modules, size_t count);
