// Copyright 2025 ATTAK IoT firmware contributors
//
// SPDX-License-Identifier: MIT
//
// USB NCM network backend for the ESP32-S3 native USB port.
//
// Topology:  host  <--USB NCM-->  ESP32-S3 (192.168.7.1/24, DHCP server .2)
//            existing AsyncTCP dashboard keeps listening on all interfaces.
//
// Threading model
// ---------------
//   * TinyUSB runs in its own task (created by esp32-hal-tinyusb.c). Every
//     tud_network_* call happens there:
//       - RX: tud_network_recv_cb() hands the borrowed NTB datagram to
//             esp_netif_receive(), whose input callback copies it once into a
//             lwIP PBUF_RAM before returning. DHCP client requests are answered
//             by the local usbDhcp codec before lwIP sees them.
//             tud_network_recv_renew() is called (driver-side re-entrancy guard).
//       - TX: the glue keeps a bounded ring of frame copies filled by lwIP, and
//             drains it from the USB task via usbd_defer_func() and the driver's
//             tud_network_xmit_ready_cb() hook.
//   * The lwIP tcpip thread only ever enqueues into the bounded ring (never
//     blocks on the USB task), so there is no lock inversion between the two.
//   * Link up/down is handled by a small dedicated task: esp_netif's lifecycle
//     helpers block on the tcpip thread, which must never happen from a USB
//     callback.

#include "usb_network.h"
#include "usb_dhcp.h"

#include "sdkconfig.h"

#if CONFIG_TINYUSB_ENABLED

#include <atomic>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include "esp_efuse.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "lwip/etharp.h"
#include "lwip/netif.h"
#include "lwip/pbuf.h"
#if LWIP_IPV6
#include "lwip/ethip6.h"
#endif

// The application NCM class driver must be visible to tusb.h so that
// <class/net/net_device.h> (tud_network_* app API, descriptor protocol) is
// pulled in. Defining this here makes the file independent of the global build
// flag; the driver's own translation units force it the same way.
#ifndef CFG_TUD_NCM
#define CFG_TUD_NCM 1
#endif

// Brings in tusb.h/tusb_config.h and the esp32-hal-tinyusb.h registration API.
#include "esp32-hal-tinyusb.h"

// Extra hook provided by the locally adapted NCM driver (see
// src/third_party/tinyusb_ncm/ncm_ntb.h). Declared here to avoid a private
// header dependency in this translation unit.
extern "C" void tud_network_xmit_ready_cb(void);

// Private TinyUSB device API: runs a callback in the TinyUSB task. Declared
// here (rather than including device/usbd_pvt.h) to keep this translation unit
// free of private headers.
extern "C" void usbd_defer_func(osal_task_func_t func, void *param, bool in_isr);

// Arduino core entry point (WiFiGeneric.cpp): esp_netif_init() plus the shared
// Arduino network event plumbing, guarded so WiFi and this backend use exactly
// one init path. Calling esp_netif_init() directly would make WiFi's later
// tcpipInit() skip its event-task setup.
bool tcpipInit();

static const char *TAG = "usb_net";

namespace {

// ---------------------------------------------------------------------------
// Fixed network parameters
// ---------------------------------------------------------------------------
constexpr const char *kDeviceAddress = "192.168.7.1";
constexpr const char *kPeerAddress = "192.168.7.2";

constexpr size_t kTxRingCount = 4;      // bounded lwIP -> USB queue depth
constexpr size_t kTxFrameMax = 1600;    // MTU 1500 + Ethernet header, with slack

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------
esp_netif_t *s_netif = nullptr;
std::atomic<bool> s_started{false};
std::atomic<bool> s_link_up{false};

// Latest requested link state + one binary semaphore. The semaphore coalesces
// rapid transitions: the task always applies the newest requested state, so a
// down->up (replug) sequence cannot be lost to a full queue.
std::atomic<bool> s_link_target{false};
SemaphoreHandle_t s_link_sem = nullptr;

uint8_t s_mac[6];
char s_mac_str[2 * sizeof(s_mac) + 1];  // "AABBCCDDEEFF" for iMACAddress
uint8_t s_dhcp_reply[usbDhcp::kReplyCapacity];  // USB-task only, no heap

// Endpoint addresses chosen before tinyusb_init() and baked into the descriptor.
uint8_t s_ep_notif = 0;
uint8_t s_ep_in = 0;
uint8_t s_ep_out = 0;

// Bounded TX ring: slots are frame copies, indices flow free<->ready.
uint8_t s_tx_buf[kTxRingCount][kTxFrameMax];
uint16_t s_tx_len[kTxRingCount];
QueueHandle_t s_tx_free_q = nullptr;
QueueHandle_t s_tx_ready_q = nullptr;

TaskHandle_t s_link_task = nullptr;

esp_netif_ip_info_t s_ip_info;
esp_netif_inherent_config_t s_base_cfg;
esp_netif_driver_ifconfig_t s_driver_cfg;
esp_netif_driver_base_t s_driver_base;  // non-NULL handle sentinel for sanity check

// ---------------------------------------------------------------------------
// esp_netif <-> lwIP glue
// ---------------------------------------------------------------------------
//
// The public esp_netif header exposes esp_netif_netstack_config_t as opaque; the
// lwIP variant is { init_fn, input_fn } (esp_netif_lwip_internal.h, IDF v4.4.7).
// We provide our own lwIP netif init/input so the USB link does not need an
// esp_eth handle (the stock ethernetif_init() would call esp_eth_ioctl()).
struct NcmNetstackConfig {
  err_t (*init_fn)(struct netif *);
  void (*input_fn)(void *netif, void *buffer, size_t len, void *eb);
};

void ncmNetifInput(void *h, void *buffer, size_t len, void *eb);
err_t ncmNetifInit(struct netif *netif);
err_t ncmNetifOutput(struct netif *netif, struct pbuf *p);
esp_err_t netifTransmit(void *h, void *buffer, size_t len);

const NcmNetstackConfig s_netstack = {ncmNetifInit, ncmNetifInput};

err_t ncmNetifInit(struct netif *netif) {
  netif->name[0] = 'u';
  netif->name[1] = 'n';
  netif->hwaddr_len = 6;
  memcpy(netif->hwaddr, s_mac, sizeof(s_mac));
  netif->mtu = 1500;
  netif->output = etharp_output;
#if LWIP_IPV6
  netif->output_ip6 = ethip6_output;
#endif
  netif->linkoutput = ncmNetifOutput;
  netif->flags = NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP;
#if LWIP_IGMP
  netif->flags |= NETIF_FLAG_IGMP;
#endif
#if LWIP_IPV6 && LWIP_IPV6_MLD
  netif->flags |= NETIF_FLAG_MLD6;
#endif
  return ERR_OK;
}

// Called synchronously from the TinyUSB task via esp_netif_receive() (the IDF
// implementation invokes lwip_input_fn inline). `buffer` is borrowed from the
// NCM driver's NTB and only valid for this call, so copy it into a lwIP
// PBUF_RAM once and hand the pbuf to the tcpip thread.
void ncmNetifInput(void *h, void *buffer, size_t len, void *eb) {
  (void)eb;
  struct netif *netif = (struct netif *)h;

  if (buffer == nullptr || len == 0 || !netif_is_up(netif)) {
    return;
  }

  // Answer DHCP client requests for the USB link before lwIP sees them: the
  // IDF DHCP server shares singleton state with the WiFi AP's server, so the
  // USB link uses its own codec (usb_dhcp.h). Runs synchronously while the
  // borrowed RX buffer is still valid; a handled request is never forwarded.
  const usbDhcp::Reply dhcp =
      usbDhcp::respond((const uint8_t *)buffer, len, s_mac, s_dhcp_reply, sizeof(s_dhcp_reply));
  if (dhcp.handled) {
    if (dhcp.size > 0) {
      netifTransmit(nullptr, s_dhcp_reply, dhcp.size);
    }
    return;
  }

  struct pbuf *p = pbuf_alloc(PBUF_RAW, (u16_t)len, PBUF_RAM);
  if (p == nullptr) {
    return;
  }
  if (pbuf_take(p, buffer, (u16_t)len) != ERR_OK) {
    pbuf_free(p);
    return;
  }
  if (netif->input(p, netif) != ERR_OK) {
    pbuf_free(p);  // tcpip mailbox full / input refused
  }
}

// ---------------------------------------------------------------------------
// lwIP -> USB transmit
// ---------------------------------------------------------------------------
err_t ncmNetifOutput(struct netif *netif, struct pbuf *p);

void usbNetDrainTx();
void usbNetScheduleDrain();
void usbNetFlushTx();

// Reserve a ring slot and copy exactly one frame into it (pbuf chains are
// handled by pbuf_copy_partial). Called from the lwIP tcpip thread; never
// blocks on the USB task.
err_t ncmEnqueuePbuf(struct pbuf *p) {
  if (!s_started.load() || !s_link_up.load() || s_tx_free_q == nullptr || p == nullptr) {
    return ERR_IF;
  }
  const uint16_t len = p->tot_len;
  if (len == 0 || len > kTxFrameMax) {
    return ERR_IF;
  }

  uint8_t idx;
  if (xQueueReceive(s_tx_free_q, &idx, 0) != pdTRUE) {
    return ERR_MEM;  // bounded queue full: lwIP retries this pbuf later
  }

  pbuf_copy_partial(p, s_tx_buf[idx], len, 0);
  s_tx_len[idx] = len;
  if (xQueueSend(s_tx_ready_q, &idx, 0) != pdTRUE) {
    xQueueSend(s_tx_free_q, &idx, 0);
    return ERR_MEM;
  }

  usbNetScheduleDrain();
  return ERR_OK;
}

err_t ncmNetifOutput(struct netif *netif, struct pbuf *p) {
  (void)netif;
  return ncmEnqueuePbuf(p);
}

// esp_netif driver transmit entry (contiguous raw frame). The NCM netif uses
// linkoutput/ncmNetifOutput, so chained pbufs are never linearized; this exists
// for the esp_netif driver contract and copies once.
esp_err_t netifTransmit(void *h, void *buffer, size_t len) {
  (void)h;
  if (!s_started.load() || !s_link_up.load() || s_tx_free_q == nullptr || buffer == nullptr ||
      len == 0 || len > kTxFrameMax) {
    return ESP_ERR_INVALID_ARG;
  }
  uint8_t idx;
  if (xQueueReceive(s_tx_free_q, &idx, 0) != pdTRUE) {
    return ESP_ERR_NO_MEM;
  }
  memcpy(s_tx_buf[idx], buffer, len);
  s_tx_len[idx] = (uint16_t)len;
  if (xQueueSend(s_tx_ready_q, &idx, 0) != pdTRUE) {
    xQueueSend(s_tx_free_q, &idx, 0);
    return ESP_ERR_NO_MEM;
  }
  usbNetScheduleDrain();
  return ESP_OK;
}

// Runs in the TinyUSB task. Sends as many queued frames as the NCM driver can
// currently accept; the rest are drained from tud_network_xmit_ready_cb().
void usbNetDrainTx() {
  if (s_tx_ready_q == nullptr) {
    return;
  }
  uint8_t idx;
  while (xQueuePeek(s_tx_ready_q, &idx, 0) == pdTRUE) {
    const uint16_t len = s_tx_len[idx];
    if (!tud_network_can_xmit(len)) {
      break;
    }
    tud_network_xmit(s_tx_buf[idx], len);
    xQueueReceive(s_tx_ready_q, &idx, 0);
    xQueueSend(s_tx_free_q, &idx, 0);
  }
}

// Drop every queued frame (unplug / bus reset). Runs in the TinyUSB task, so it
// cannot race with usbNetDrainTx().
void usbNetFlushTx() {
  if (s_tx_ready_q == nullptr || s_tx_free_q == nullptr) {
    return;
  }
  uint8_t idx;
  while (xQueueReceive(s_tx_ready_q, &idx, 0) == pdTRUE) {
    xQueueSend(s_tx_free_q, &idx, 0);
  }
}

// Trampoline so the drain runs in the TinyUSB task even when triggered by lwIP.
void usbNetDrainTxDeferred(void *arg) {
  (void)arg;
  usbNetDrainTx();
}

void usbNetScheduleDrain() {
  if (s_started.load()) {
    usbd_defer_func(usbNetDrainTxDeferred, nullptr, false);
  }
}

// ---------------------------------------------------------------------------
// Link state
// ---------------------------------------------------------------------------
void linkTask(void *arg) {
  (void)arg;
  for (;;) {
    if (xSemaphoreTake(s_link_sem, portMAX_DELAY) == pdTRUE && s_netif != nullptr) {
      // Coalesce: apply the newest requested state, not every transition.
      if (s_link_target.load()) {
        esp_netif_action_connected(s_netif, nullptr, 0, nullptr);
        ESP_LOGI(TAG, "NCM link up");
      } else {
        esp_netif_action_disconnected(s_netif, nullptr, 0, nullptr);
        ESP_LOGI(TAG, "NCM link down");
      }
    }
  }
}

// ---------------------------------------------------------------------------
// USB descriptors
// ---------------------------------------------------------------------------
uint16_t ncmLoadDescriptor(uint8_t *dst, uint8_t *itf) {
  const uint8_t str_itf = tinyusb_add_string_descriptor("ATTAK USB Network");
  const uint8_t str_mac = tinyusb_add_string_descriptor(s_mac_str);

  // Two interfaces (NCM control + data) with an IAD, so Windows' inbox
  // UsbNcm.sys binds the function automatically.
  uint8_t descriptor[TUD_CDC_NCM_DESC_LEN] = {
      TUD_CDC_NCM_DESCRIPTOR(*itf, str_itf, str_mac, s_ep_notif, 64, s_ep_out, s_ep_in,
                             CFG_TUD_NET_ENDPOINT_SIZE, CFG_TUD_NET_MTU)};

  memcpy(dst, descriptor, sizeof(descriptor));
  *itf += 2;
  return sizeof(descriptor);
}

// ---------------------------------------------------------------------------
// TinyUSB network callbacks (all run in the TinyUSB task)
// ---------------------------------------------------------------------------
}  // namespace

extern "C" bool tud_network_recv_cb(const uint8_t *src, uint16_t size) {
  // esp_netif_receive() invokes our input callback synchronously and that
  // callback copies `src` into a lwIP pbuf before returning, so the borrowed
  // NTB buffer is safe here (no extra copy in this function).
  if (s_netif != nullptr && src != nullptr && size >= 14) {
    esp_netif_receive(s_netif, (void *)src, size, nullptr);
  }

  // Keep draining datagrams of this NTB (driver has a re-entrancy guard).
  tud_network_recv_renew();
  return true;
}

extern "C" uint16_t tud_network_xmit_cb(uint8_t *dst, void *ref, uint16_t arg) {
  // `ref` points into the bounded TX ring and stays valid until xmit returns.
  memcpy(dst, ref, arg);
  return arg;
}

extern "C" void tud_network_link_state_cb(bool state) {
  if (!state) {
    // Unplug / bus reset: discard frames queued for the previous session.
    usbNetFlushTx();
  }
  s_link_up.store(state);
  s_link_target.store(state);
  if (s_link_sem != nullptr) {
    xSemaphoreGive(s_link_sem);
  }
}

extern "C" void tud_network_xmit_ready_cb(void) {
  // Called from netd_xfer_cb() (TinyUSB task) after an NTB buffer is recycled.
  usbNetDrainTx();
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------
namespace usbNetwork {

bool begin() {
  if (s_started.load()) {
    return true;
  }

  // --- TCP/IP stack -------------------------------------------------------
  if (!tcpipInit()) {
    ESP_LOGE(TAG, "tcpipInit failed");
    return false;
  }

  // --- stable, unique identity -------------------------------------------
  if (esp_efuse_mac_get_default(s_mac) != ESP_OK) {
    ESP_LOGE(TAG, "efuse MAC unavailable");
    return false;
  }
  snprintf(s_mac_str, sizeof(s_mac_str), "%02X%02X%02X%02X%02X%02X", s_mac[0], s_mac[1], s_mac[2],
           s_mac[3], s_mac[4], s_mac[5]);

  // --- bounded queues -----------------------------------------------------
  s_tx_free_q = xQueueCreate(kTxRingCount, sizeof(uint8_t));
  s_tx_ready_q = xQueueCreate(kTxRingCount, sizeof(uint8_t));
  s_link_sem = xSemaphoreCreateBinary();
  if (s_tx_free_q == nullptr || s_tx_ready_q == nullptr || s_link_sem == nullptr) {
    ESP_LOGE(TAG, "queue allocation failed");
    return false;
  }
  for (uint8_t i = 0; i < kTxRingCount; ++i) {
    xQueueSend(s_tx_free_q, &i, 0);
  }
  if (xTaskCreate(linkTask, "usbnet_link", 3072, nullptr, 4, &s_link_task) != pdPASS) {
    ESP_LOGE(TAG, "link task create failed");
    return false;
  }

  // --- esp_netif ----------------------------------------------------------
  IP4_ADDR(&s_ip_info.ip, 192, 168, 7, 1);
  IP4_ADDR(&s_ip_info.netmask, 255, 255, 255, 0);
  IP4_ADDR(&s_ip_info.gw, 0, 0, 0, 0);

  memset(&s_base_cfg, 0, sizeof(s_base_cfg));
  // Plain static-IP interface: the USB DHCP server is our own codec (usb_dhcp.h)
  // because ESP_NETIF_DHCP_SERVER would share singleton DHCP state with the
  // WiFi AP's server.
  s_base_cfg.flags = (esp_netif_flags_t)ESP_NETIF_FLAG_AUTOUP;
  memcpy(s_base_cfg.mac, s_mac, sizeof(s_mac));
  s_base_cfg.ip_info = &s_ip_info;
  s_base_cfg.get_ip_event = 0;
  s_base_cfg.lost_ip_event = 0;
  s_base_cfg.if_key = "USB_NCM";
  s_base_cfg.if_desc = "usb_ncm";
  s_base_cfg.route_prio = 5;  // below WiFi AP(10)/STA(100): never steals default route

  memset(&s_driver_cfg, 0, sizeof(s_driver_cfg));
  s_driver_cfg.handle = &s_driver_base;  // non-NULL: esp_netif sanity check
  s_driver_cfg.transmit = netifTransmit;
  s_driver_cfg.transmit_wrap = nullptr;
  s_driver_cfg.driver_free_rx_buffer = nullptr;  // input path frees via l2 notify

  esp_netif_config_t cfg = {};
  cfg.base = &s_base_cfg;
  cfg.driver = &s_driver_cfg;
  cfg.stack = reinterpret_cast<const esp_netif_netstack_config_t *>(&s_netstack);

  s_netif = esp_netif_new(&cfg);
  if (s_netif == nullptr) {
    ESP_LOGE(TAG, "esp_netif_new failed");
    return false;
  }

  // Bring the interface up, then park it link-down until the host actually
  // configures the NCM data interface.
  esp_netif_action_start(s_netif, nullptr, 0, nullptr);
  esp_netif_action_disconnected(s_netif, nullptr, 0, nullptr);

  // --- TinyUSB NCM class --------------------------------------------------
  const uint8_t notif_num = tinyusb_get_free_in_endpoint();
  const uint8_t out_num = tinyusb_get_free_out_endpoint();
  const uint8_t in_num = tinyusb_get_free_in_endpoint();
  if (notif_num == 0 || out_num == 0 || in_num == 0) {
    ESP_LOGE(TAG, "no free USB endpoints");
    return false;
  }
  s_ep_notif = (uint8_t)(0x80 | notif_num);
  s_ep_in = (uint8_t)(0x80 | in_num);
  s_ep_out = out_num;

  if (tinyusb_enable_interface(USB_INTERFACE_CUSTOM, TUD_CDC_NCM_DESC_LEN, ncmLoadDescriptor) !=
      ESP_OK) {
    ESP_LOGE(TAG, "NCM interface registration failed");
    return false;
  }

  // Build the device config explicitly: TINYUSB_CONFIG_DEFAULT() references
  // CONFIG_TINYUSB_DESC_* Kconfig symbols that are not present in this SDK
  // configuration (Arduino's own USB class builds its config the same way).
  tinyusb_device_config_t usb_cfg = {
      .vid = USB_ESPRESSIF_VID,
      .pid = 0x0002,
      .product_name = "ATTAK IoT Dashboard",
      .manufacturer_name = "ATTAK",
      .serial_number = nullptr,  // derive stable serial from efuse MAC
      .fw_version = 0x0100,
      .usb_version = 0x0200,
      .usb_class = TUSB_CLASS_MISC,
      .usb_subclass = MISC_SUBCLASS_COMMON,
      .usb_protocol = MISC_PROTOCOL_IAD,
      .usb_attributes = TUSB_DESC_CONFIG_ATT_SELF_POWERED,
      .usb_power_ma = 500,
      .webusb_enabled = false,
      .webusb_url = "",
  };

  if (tinyusb_init(&usb_cfg) != ESP_OK) {
    ESP_LOGE(TAG, "tinyusb_init failed");
    return false;
  }

  s_started.store(true);
  ESP_LOGI(TAG, "USB NCM ready: device %s, MAC %s, host lease %s", kDeviceAddress, s_mac_str,
           kPeerAddress);
  return true;
}

bool connected() {
  return s_started.load() && s_link_up.load();
}

const char *address() {
  return s_started.load() ? kDeviceAddress : "";
}

}  // namespace usbNetwork

#else
#error "usb_network.cpp requires CONFIG_TINYUSB_ENABLED (ESP32-S3 native USB target)"
#endif  // CONFIG_TINYUSB_ENABLED
