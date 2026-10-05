// Onboard addressable RGB LED (GPIO48) driven from the enabled-only aggregate.
#pragma once

#include "core/status_health.h"

namespace statusLed {
void begin();
void update(HealthLevel level);
} // namespace statusLed
