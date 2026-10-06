/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
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
 * TinyUSB's device stack resolves application class drivers through the weak
 * symbol `usbd_app_driver_get_cb()` (declared in device/usbd_pvt.h, referenced
 * from the pre-compiled usbd.c). Because the Arduino-ESP32 2.0.17 TinyUSB
 * archive was built without CFG_TUD_NCM, this application-provided hook is the
 * only way to attach an NCM class driver to the 0.16 stack.
 *
 * This tiny translation unit exists solely so the definition is *strong*:
 * GCC propagates the `weak` attribute from the declaration in
 * device/usbd_pvt.h onto a definition in the same TU, so the definition cannot
 * live in ncm_device.c (which needs usbd_pvt.h for usbd_edpt_* and the
 * usbd_class_driver_t layout). Here we deliberately avoid usbd_pvt.h and treat
 * the returned driver as an opaque, ABI-identical pointer.
 * ---------------------------------------------------------------------------
 */

#include <stdint.h>

#ifndef CFG_TUD_NCM
#define CFG_TUD_NCM 1
#endif

#include "tusb_option.h"

#if (CFG_TUD_ENABLED && CFG_TUD_NCM)

/* ABI-identical stand-in for usbd_class_driver_t (opaque here). */
typedef struct usbd_class_driver_t_attk usbd_class_driver_t_attk;

/* Provided by ncm_device.c; returns the real usbd_class_driver_t*. */
extern usbd_class_driver_t_attk const *usb_ncm_get_class_driver(void);

/* Strong override of the weak symbol in the pre-built TinyUSB stack. */
usbd_class_driver_t_attk const *usbd_app_driver_get_cb(uint8_t *driver_count) {
  if (driver_count) {
    *driver_count = 1;
  }
  return usb_ncm_get_class_driver();
}

#endif /* CFG_TUD_ENABLED && CFG_TUD_NCM */
