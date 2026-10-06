/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 * Copyright (c) 2024 Hardy Griech
 * Copyright (c) 2020 Jacob Berg Potter
 * Copyright (c) 2020 Peter Lawrence
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 * ---------------------------------------------------------------------------
 * LOCAL ADAPTATION / PROVENANCE
 * ---------------------------------------------------------------------------
 * The NCM wire structures and the extra NCM tuning knobs below are taken from
 * TinyUSB v0.17.0 src/class/net/ncm.h (MIT, see above):
 *   https://github.com/hathach/tinyusb/blob/0.17.0/src/class/net/ncm.h
 * They are duplicated here (rather than shipped by the Arduino-ESP32 2.0.17
 * TinyUSB 0.16 headers, which only carry the class request codes) so the NCM
 * application class driver can be compiled against the 0.16 core unchanged.
 * Names already provided by the 0.16 <class/net/net_device.h> are guarded with
 * #ifndef so both headers can be included together.
 * ---------------------------------------------------------------------------
 */

#ifndef ATTK_TUSB_NCM_NTB_H_
#define ATTK_TUSB_NCM_NTB_H_

#include "common/tusb_common.h"

#ifdef __cplusplus
extern "C" {
#endif

// NTB buffer size for the receive side (host -> device), must be >= MTU.
#ifndef CFG_TUD_NCM_OUT_NTB_MAX_SIZE
  #define CFG_TUD_NCM_OUT_NTB_MAX_SIZE 3200
#endif

// NTB buffer size for the transmit side (device -> host), must be >= MTU.
#ifndef CFG_TUD_NCM_IN_NTB_MAX_SIZE
  #define CFG_TUD_NCM_IN_NTB_MAX_SIZE 3200
#endif

// Number of receive NTB buffers.
#ifndef CFG_TUD_NCM_OUT_NTB_N
  #define CFG_TUD_NCM_OUT_NTB_N 2
#endif

// Number of transmit NTB buffers.
#ifndef CFG_TUD_NCM_IN_NTB_N
  #define CFG_TUD_NCM_IN_NTB_N 2
#endif

// Datagrams the device may pack into one outgoing NTB.
#ifndef CFG_TUD_NCM_IN_MAX_DATAGRAMS_PER_NTB
  #define CFG_TUD_NCM_IN_MAX_DATAGRAMS_PER_NTB 8
#endif

// Datagrams we advertise the host may pack into one incoming NTB.
#ifndef CFG_TUD_NCM_OUT_MAX_DATAGRAMS_PER_NTB
  #define CFG_TUD_NCM_OUT_MAX_DATAGRAMS_PER_NTB 6
#endif

#ifndef CFG_TUD_NCM_ALIGNMENT
  #define CFG_TUD_NCM_ALIGNMENT 4
#endif

#define NTH16_SIGNATURE     0x484D434E
#define NDP16_SIGNATURE_NCM0 0x304D434E
#define NDP16_SIGNATURE_NCM1 0x314D434E

typedef struct TU_ATTR_PACKED {
  uint16_t wLength;
  uint16_t bmNtbFormatsSupported;
  uint32_t dwNtbInMaxSize;
  uint16_t wNdbInDivisor;
  uint16_t wNdbInPayloadRemainder;
  uint16_t wNdbInAlignment;
  uint16_t wReserved;
  uint32_t dwNtbOutMaxSize;
  uint16_t wNdbOutDivisor;
  uint16_t wNdbOutPayloadRemainder;
  uint16_t wNdbOutAlignment;
  uint16_t wNtbOutMaxDatagrams;
} ntb_parameters_t;

typedef struct TU_ATTR_PACKED {
  uint32_t dwSignature;
  uint16_t wHeaderLength;
  uint16_t wSequence;
  uint16_t wBlockLength;
  uint16_t wNdpIndex;
} nth16_t;

typedef struct TU_ATTR_PACKED {
  uint16_t wDatagramIndex;
  uint16_t wDatagramLength;
} ndp16_datagram_t;

typedef struct TU_ATTR_PACKED {
  uint32_t dwSignature;
  uint16_t wLength;
  uint16_t wNextNdpIndex;
  // ndp16_datagram_t datagram[];
} ndp16_t;

typedef struct TU_ATTR_PACKED {
  uint32_t dwNtbInMaxSize;
  uint16_t wNtbInMaxDatagrams;
  uint16_t wReserved;
} ncm_ntb_input_size_t;

typedef union TU_ATTR_PACKED {
  struct {
    nth16_t nth;
    ndp16_t ndp;
    ndp16_datagram_t ndp_datagram[CFG_TUD_NCM_IN_MAX_DATAGRAMS_PER_NTB + 1];
  };
  uint8_t data[CFG_TUD_NCM_IN_NTB_MAX_SIZE];
} xmit_ntb_t;

typedef union TU_ATTR_PACKED {
  struct {
    nth16_t nth;
    // only the header is at a guaranteed position
  };
  uint8_t data[CFG_TUD_NCM_OUT_NTB_MAX_SIZE];
} recv_ntb_t;

struct ncm_notify_t {
  tusb_control_request_t header;
  uint32_t downlink, uplink;
};

//--------------------------------------------------------------------+
// EXTRA APPLICATION HOOK (not part of TinyUSB v0.17.0)
//--------------------------------------------------------------------+
// Called from the TinyUSB task once an IN transfer finished and its NTB buffer
// was recycled, so the glue can drain a bounded TX queue. Weak: applications
// may override it (implementation lives in ncm_device.c).
void tud_network_xmit_ready_cb(void);

#ifdef __cplusplus
}
#endif

#endif /* ATTK_TUSB_NCM_NTB_H_ */
