// Copyright 2025 ATTAK IoT firmware contributors
//
// SPDX-License-Identifier: MIT
//
// USB NCM (Network Control Model) network backend.
//
// Presents the ESP32-S3 native USB-C port as a USB Ethernet adapter so the host
// (Windows 11 inbox UsbNcm.sys, Linux cdc_ncm, macOS) can reach the on-device
// dashboard over a point-to-point link, without the WiFi AP.
//
// Contract:
//   * The device owns 192.168.7.1/24 and runs a DHCP server that hands the
//     peer 192.168.7.2 (no router/DNS options are advertised).
//   * The existing AsyncTCP dashboard keeps listening on all interfaces; no
//     change is required there.
//   * UART0 (CH343) stays available as `Serial`.
//
// All TinyUSB network APIs are driven from the TinyUSB device task; the lwIP
// side only ever touches a bounded copy queue.

#pragma once

#include <stdbool.h>

namespace usbNetwork {

// Bring up the NCM interface, its esp_netif/lwIP interface and the DHCP server,
// then start TinyUSB. Safe to call more than once; returns false on failure.
// Must be called before connected()/address() are meaningful.
bool begin();

// True while the host has selected the NCM data interface (link up) and the
// device is configured. False before begin(), while unplugged and on bus reset.
bool connected();

// Fixed device-side IPv4 address as a NUL-terminated string, e.g. "192.168.7.1".
// Returns "" (empty string) before begin() has succeeded. Never returns NULL.
const char *address();

}  // namespace usbNetwork
