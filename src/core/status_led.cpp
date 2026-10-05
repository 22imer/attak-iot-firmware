#include "status_led.h"

#include <FastLED.h>

#include "board_pins.h"

namespace statusLed {

namespace {

CRGB led[1];
HealthLevel current = HealthLevel::Off;
bool started = false;

} // namespace

void begin() {
    FastLED.addLeds<NEOPIXEL, static_cast<uint8_t>(PIN_STATUS_RGB_LED)>(led, 1);
    led[0] = CRGB::Black;
    FastLED.show();
    current = HealthLevel::Off;
    started = true;
}

void update(HealthLevel level) {
    if (!started || level == current) return; // only write on aggregate change
    current = level;
    switch (level) {
    case HealthLevel::Off: led[0] = CRGB::Black; break;
    case HealthLevel::Healthy: led[0] = CRGB::Green; break;
    case HealthLevel::Degraded: led[0] = CRGB::Yellow; break;
    }
    FastLED.show();
}

} // namespace statusLed
