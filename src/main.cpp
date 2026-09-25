#include <Arduino.h>

#include "core/storage.h"
#include "core/web_dashboard.h"
#include "core/wifi_ap.h"
#include "modules/cc1101_module.h"
#include "modules/ir_module.h"
#include "modules/nrf24_module.h"
#include "modules/pn532_module.h"

namespace {
DeviceConfig config;
}

void setup() {
    Serial.begin(115200);

    storage::begin();
    config = storage::load();

    wifiAp::begin(config);
    webDashboard::begin();

    cc1101::begin();
    nrf24::begin();
    pn532::begin();
    ir::begin();
}

void loop() {
    cc1101::poll();
    nrf24::poll();
    pn532::poll();
    ir::poll();

    webDashboard::publishStatus(cc1101::status());
    webDashboard::publishStatus(nrf24::status());
    webDashboard::publishStatus(pn532::status());
    webDashboard::publishStatus(ir::status());

    webDashboard::loop();
}
