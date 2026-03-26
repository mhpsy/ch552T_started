#pragma once

#include <stdint.h>

/* HID report size (bytes) for both IN and OUT */
#define HID_REPORT_SIZE  8

/**
 * Initialise GPIO pins used by the demo (P3.0 / P3.1).
 */
void gpio_hid_init(void);

/**
 * Initialise the USB peripheral as an HID device (no driver needed).
 */
void usb_hid_init(void);

/**
 * Call from main loop.  Sends an IN report when the endpoint is idle.
 * Returns 1 if a new OUT report was received since the last call.
 */
uint8_t usb_hid_process(void);

/**
 * Latest OUT report received from the host (HID_REPORT_SIZE bytes).
 */
extern __xdata uint8_t hid_out_report[HID_REPORT_SIZE];
