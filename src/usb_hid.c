/*
 * USB HID device implementation for CH552/CH554.
 *
 * Presents a generic vendor-page HID device with 8-byte IN and OUT reports
 * so the host recognises it without any extra driver on Windows / Linux / macOS.
 *
 * Protocol:
 *   OUT report (host -> device), 8 bytes:
 *     byte[0]  command:
 *              0x01 = force Pin10 (P3.0/RXD) LOW
 *              0x02 = write Pin11 (P3.1/TXD), byte[1]=level (0 or 1)
 *              0x03 = request GPIO state (next IN report will contain it)
 *     byte[1]  parameter (depends on command)
 *     byte[2..7] reserved (0)
 *
 *   IN report (device -> host), 8 bytes:
 *     byte[0]  GPIO state: bit0 = Pin10, bit1 = Pin11
 *     byte[1..7] reserved (0)
 */

#include <stdint.h>
#include "ch554.h"
#include "ch554_usb.h"
#include "debug.h"
#include "usb_hid.h"

/* ---- GPIO helpers (same pins as the old vendor demo) ---- */

#define PIN10_MASK bRXD   /* P3.0 */
#define PIN11_MASK bTXD   /* P3.1 */

static void pin10_force_low(void)
{
    P3 &= (uint8_t)~PIN10_MASK;
}

static void pin11_write(uint8_t level)
{
    if (level)
        P3 |= PIN11_MASK;
    else
        P3 &= (uint8_t)~PIN11_MASK;
}

void gpio_hid_init(void)
{
    P3_MOD_OC &= (uint8_t)~(PIN10_MASK | PIN11_MASK);
    P3_DIR_PU |=  (PIN10_MASK | PIN11_MASK);
    pin10_force_low();
    pin11_write(0);
}

/* ---- Buffers ---- */

/*
 * EP0 buffer:        0x0000 .. 0x003F  (64 bytes)
 * EP1 OUT (RX) buf:  0x0040 .. 0x007F  (64 bytes)
 * EP1 IN  (TX) buf:  0x0080 .. 0x00BF  (64 bytes)
 *
 * When bUEP1_TX_EN=1, bUEP1_RX_EN=1, bUEP1_BUF_MOD=0 (single buffer):
 *   OUT data -> DMA base
 *   IN  data <- DMA base + 64
 */
static __xdata __at(0x0000) uint8_t ep0_buffer[MAX_PACKET_SIZE];
static __xdata __at(0x0040) uint8_t ep1_out_buffer[MAX_PACKET_SIZE];
static __xdata __at(0x0080) uint8_t ep1_in_buffer[MAX_PACKET_SIZE];

__xdata uint8_t hid_out_report[HID_REPORT_SIZE];

/* ---- Descriptors ---- */

static USB_SETUP_REQ setup_req;
static uint8_t  pending_address;
static uint8_t  usb_config_value;
static uint16_t ep0_data_remaining;
static const uint8_t __code *ep0_data_ptr;

static volatile uint8_t ep1_in_busy;   /* 1 while waiting for IN ACK */
static volatile uint8_t new_out_report; /* 1 when a fresh OUT report arrived */

static uint8_t hid_idle_rate;           /* SET_IDLE value (units of 4 ms) */

/* ---------- HID Report Descriptor ---------- */

static const uint8_t __code hid_report_desc[] = {
    0x06, 0x00, 0xFF,  /* Usage Page (Vendor Defined 0xFF00) */
    0x09, 0x01,        /* Usage (Vendor Usage 1)             */
    0xA1, 0x01,        /* Collection (Application)           */

    /* --- Input report: device -> host --- */
    0x09, 0x01,        /*   Usage (Vendor Usage 1)           */
    0x15, 0x00,        /*   Logical Minimum (0)              */
    0x26, 0xFF, 0x00,  /*   Logical Maximum (255)            */
    0x75, 0x08,        /*   Report Size (8 bits)             */
    0x95, HID_REPORT_SIZE, /*   Report Count                 */
    0x81, 0x02,        /*   Input (Data, Var, Abs)           */

    /* --- Output report: host -> device --- */
    0x09, 0x01,        /*   Usage (Vendor Usage 1)           */
    0x15, 0x00,        /*   Logical Minimum (0)              */
    0x26, 0xFF, 0x00,  /*   Logical Maximum (255)            */
    0x75, 0x08,        /*   Report Size (8 bits)             */
    0x95, HID_REPORT_SIZE, /*   Report Count                 */
    0x91, 0x02,        /*   Output (Data, Var, Abs)          */

    0xC0               /* End Collection                     */
};

#define HID_REPORT_DESC_SIZE  (sizeof(hid_report_desc))

/* ---------- Device Descriptor ---------- */

static const uint8_t __code dev_desc[] = {
    0x12,                      /* bLength            */
    USB_DESCR_TYP_DEVICE,     /* bDescriptorType    */
    0x10, 0x01,                /* bcdUSB  1.10       */
    0x00,                      /* bDeviceClass       */
    0x00,                      /* bDeviceSubClass    */
    0x00,                      /* bDeviceProtocol    */
    DEFAULT_ENDP0_SIZE,        /* bMaxPacketSize0    */
    0x34, 0x12,                /* idVendor  0x1234   */
    0x01, 0x00,                /* idProduct 0x0001   */
    0x00, 0x01,                /* bcdDevice 1.00     */
    0x01,                      /* iManufacturer      */
    0x02,                      /* iProduct           */
    0x03,                      /* iSerialNumber      */
    0x01                       /* bNumConfigurations */
};

/* ---------- Configuration + Interface + HID + EP Descriptors ---------- */

static const uint8_t __code cfg_desc[] = {
    /* Configuration Descriptor  (9 bytes) */
    0x09,
    USB_DESCR_TYP_CONFIG,
    0x29, 0x00,                /* wTotalLength = 41  */
    0x01,                      /* bNumInterfaces     */
    0x01,                      /* bConfigurationValue*/
    0x00,                      /* iConfiguration     */
    0x80,                      /* bmAttributes: bus-powered */
    0x32,                      /* bMaxPower: 100 mA  */

    /* Interface Descriptor  (9 bytes) */
    0x09,
    USB_DESCR_TYP_INTERF,
    0x00,                      /* bInterfaceNumber   */
    0x00,                      /* bAlternateSetting  */
    0x02,                      /* bNumEndpoints      */
    USB_DEV_CLASS_HID,         /* bInterfaceClass    */
    0x00,                      /* bInterfaceSubClass (no boot) */
    0x00,                      /* bInterfaceProtocol (none)    */
    0x00,                      /* iInterface         */

    /* HID Descriptor  (9 bytes) */
    0x09,
    USB_DESCR_TYP_HID,        /* 0x21 */
    0x11, 0x01,                /* bcdHID 1.11        */
    0x00,                      /* bCountryCode       */
    0x01,                      /* bNumDescriptors    */
    USB_DESCR_TYP_REPORT,     /* bDescriptorType (report) */
    (uint8_t)(HID_REPORT_DESC_SIZE),
    (uint8_t)(HID_REPORT_DESC_SIZE >> 8),

    /* Endpoint 1 IN  (7 bytes) */
    0x07,
    USB_DESCR_TYP_ENDP,
    0x81,                      /* bEndpointAddress: EP1 IN */
    USB_ENDP_TYPE_INTER,       /* bmAttributes: Interrupt  */
    HID_REPORT_SIZE, 0x00,     /* wMaxPacketSize           */
    0x0A,                      /* bInterval: 10 ms         */

    /* Endpoint 1 OUT (7 bytes) */
    0x07,
    USB_DESCR_TYP_ENDP,
    0x01,                      /* bEndpointAddress: EP1 OUT */
    USB_ENDP_TYPE_INTER,       /* bmAttributes: Interrupt   */
    HID_REPORT_SIZE, 0x00,     /* wMaxPacketSize            */
    0x0A                       /* bInterval: 10 ms          */
};

/* Offset of the HID descriptor inside cfg_desc (after config + interface) */
#define HID_DESC_OFFSET  18
#define HID_DESC_SIZE    9

/* ---------- String Descriptors ---------- */

static const uint8_t __code str_lang[] = {
    0x04, USB_DESCR_TYP_STRING, 0x09, 0x04
};

static const uint8_t __code str_mfr[] = {
    0x12, USB_DESCR_TYP_STRING,
    'M',0, 'H',0, 'P',0, 'S',0, 'Y',0, '-',0, 'L',0, 'A',0 };

static const uint8_t __code str_prod[] = {
    0x1E, USB_DESCR_TYP_STRING,
    'C',0, 'H',0, '5',0, '5',0, '2',0, ' ',0,
    'H',0, 'I',0, 'D',0, ' ',0, 'G',0, 'P',0, 'I',0, 'O',0
};

static const uint8_t __code str_serial[] = {
    0x0E, USB_DESCR_TYP_STRING,
    '0',0, '0',0, '0',0, '0',0, '0',0, '1',0
};

/* ---- Low-level EP0 helpers ---- */

static void ep0_set_tx_ack(uint8_t len)
{
    UEP0_T_LEN = len;
    UEP0_CTRL = (UEP0_CTRL & (uint8_t)~MASK_UEP_T_RES) | UEP_T_RES_ACK;
}

static void ep0_send_next_data(void)
{
    uint8_t i, send_len;

    send_len = (ep0_data_remaining > DEFAULT_ENDP0_SIZE)
             ? DEFAULT_ENDP0_SIZE
             : (uint8_t)ep0_data_remaining;

    for (i = 0; i < send_len; ++i)
        ep0_buffer[i] = ep0_data_ptr[i];

    ep0_data_ptr       += send_len;
    ep0_data_remaining -= send_len;
    ep0_set_tx_ack(send_len);
}

static void ep0_prepare_code_data(const uint8_t __code *src,
                                  uint16_t total_len,
                                  uint16_t req_len)
{
    ep0_data_ptr = src;
    ep0_data_remaining = (req_len < total_len) ? req_len : total_len;
    ep0_send_next_data();
}

static void ep0_stall(void)
{
    UEP0_CTRL = (UEP0_CTRL & (uint8_t)~(MASK_UEP_R_RES | MASK_UEP_T_RES))
              | UEP_R_RES_STALL | UEP_T_RES_STALL;
}

static void ep0_send_zlp(void)
{
    ep0_data_remaining = 0;
    ep0_set_tx_ack(0);
}

/* ---- Process an OUT report (from host) ---- */

static void process_out_report(const __xdata uint8_t *buf, uint8_t len)
{
    uint8_t i;
    for (i = 0; i < HID_REPORT_SIZE; ++i)
        hid_out_report[i] = (i < len) ? buf[i] : 0;

    switch (hid_out_report[0]) {
    case 0x01:
        pin10_force_low();
        break;
    case 0x02:
        pin11_write(hid_out_report[1] ? 1 : 0);
        break;
    default:
        break;
    }

    new_out_report = 1;
}

/* ---- Build an IN report (to host) ---- */

static void build_in_report(__xdata uint8_t *buf)
{
    uint8_t i;
    buf[0] = 0;
    if (P3 & PIN10_MASK) buf[0] |= 0x01;
    if (P3 & PIN11_MASK) buf[0] |= 0x02;
    for (i = 1; i < HID_REPORT_SIZE; ++i)
        buf[i] = 0;
}

/* ---- Standard request handler ---- */

static void handle_standard_request(void)
{
    const uint8_t __code *dsc = 0;
    uint16_t dsc_len = 0;
    uint16_t req_len = (uint16_t)setup_req.wLengthL
                     | ((uint16_t)setup_req.wLengthH << 8);

    switch (setup_req.bRequest) {

    case USB_GET_DESCRIPTOR:
        if (!(setup_req.bRequestType & USB_REQ_TYP_IN)) {
            ep0_stall(); return;
        }
        switch (setup_req.wValueH) {
        case USB_DESCR_TYP_DEVICE:
            dsc = dev_desc;  dsc_len = sizeof(dev_desc);
            break;
        case USB_DESCR_TYP_CONFIG:
            dsc = cfg_desc;  dsc_len = sizeof(cfg_desc);
            break;
        case USB_DESCR_TYP_STRING:
            if      (setup_req.wValueL == 0) { dsc = str_lang;   dsc_len = sizeof(str_lang);   }
            else if (setup_req.wValueL == 1) { dsc = str_mfr;    dsc_len = sizeof(str_mfr);    }
            else if (setup_req.wValueL == 2) { dsc = str_prod;   dsc_len = sizeof(str_prod);   }
            else if (setup_req.wValueL == 3) { dsc = str_serial; dsc_len = sizeof(str_serial); }
            break;
        case USB_DESCR_TYP_HID:
            /* Host may request the HID descriptor standalone */
            dsc     = cfg_desc + HID_DESC_OFFSET;
            dsc_len = HID_DESC_SIZE;
            break;
        case USB_DESCR_TYP_REPORT:
            dsc     = hid_report_desc;
            dsc_len = HID_REPORT_DESC_SIZE;
            break;
        default:
            break;
        }

        if (dsc == 0) { ep0_stall(); return; }
        ep0_prepare_code_data(dsc, dsc_len, req_len);
        break;

    case USB_SET_ADDRESS:
        pending_address = setup_req.wValueL & MASK_USB_ADDR;
        ep0_send_zlp();
        break;

    case USB_SET_CONFIGURATION:
        usb_config_value = setup_req.wValueL;
        /* Arm EP1 OUT to receive the first output report */
        UEP1_CTRL = bUEP_AUTO_TOG | UEP_R_RES_ACK | UEP_T_RES_NAK;
        UEP1_T_LEN = 0;
        ep1_in_busy = 0;
        ep0_send_zlp();
        break;

    case USB_GET_CONFIGURATION:
        ep0_buffer[0] = usb_config_value;
        ep0_set_tx_ack(1);
        break;

    case USB_GET_STATUS:
        ep0_buffer[0] = 0;
        ep0_buffer[1] = 0;
        ep0_set_tx_ack(2);
        break;

    default:
        ep0_stall();
        break;
    }
}

/* ---- HID class request handler ---- */

static void handle_class_request(void)
{
    uint16_t req_len = (uint16_t)setup_req.wLengthL
                     | ((uint16_t)setup_req.wLengthH << 8);

    switch (setup_req.bRequest) {

    case HID_GET_REPORT:
        /* Return current GPIO state as an input report via EP0 */
        build_in_report(ep0_buffer);
        {
            uint8_t l = (req_len < HID_REPORT_SIZE)
                      ? (uint8_t)req_len : HID_REPORT_SIZE;
            ep0_set_tx_ack(l);
        }
        break;

    case HID_SET_REPORT:
        /* Host will send the report in the DATA stage (OUT on EP0).
         * We accept the ZLP status from our side and handle data in
         * the OUT token handler for EP0.  For simplicity, just ACK
         * and process in the OUT phase. */
        ep0_data_remaining = req_len;   /* remember how much to expect */
        UEP0_CTRL = (UEP0_CTRL & (uint8_t)~MASK_UEP_R_RES) | UEP_R_RES_ACK;
        break;

    case HID_GET_IDLE:
        ep0_buffer[0] = hid_idle_rate;
        ep0_set_tx_ack(1);
        break;

    case HID_SET_IDLE:
        hid_idle_rate = setup_req.wValueH;
        ep0_send_zlp();
        break;

    case HID_GET_PROTOCOL:
        ep0_buffer[0] = 1;  /* report protocol */
        ep0_set_tx_ack(1);
        break;

    case HID_SET_PROTOCOL:
        ep0_send_zlp();
        break;

    default:
        ep0_stall();
        break;
    }
}

/* ---- SETUP handler ---- */

static void handle_setup_packet(void)
{
    setup_req.bRequestType = ep0_buffer[0];
    setup_req.bRequest     = ep0_buffer[1];
    setup_req.wValueL      = ep0_buffer[2];
    setup_req.wValueH      = ep0_buffer[3];
    setup_req.wIndexL      = ep0_buffer[4];
    setup_req.wIndexH      = ep0_buffer[5];
    setup_req.wLengthL     = ep0_buffer[6];
    setup_req.wLengthH     = ep0_buffer[7];

    ep0_data_remaining = 0;
    UEP0_CTRL = bUEP_R_TOG | bUEP_T_TOG | UEP_R_RES_ACK | UEP_T_RES_NAK;

    switch (setup_req.bRequestType & USB_REQ_TYP_MASK) {
    case USB_REQ_TYP_STANDARD:
        handle_standard_request();
        break;
    case USB_REQ_TYP_CLASS:
        handle_class_request();
        break;
    default:
        ep0_stall();
        break;
    }
}

/* ---- Public API ---- */

void usb_hid_init(void)
{
    /* Enable USB I/O pins */
    PIN_FUNC |= bUSB_IO_EN;

    /* Reset USB SIE */
    USB_CTRL = bUC_RESET_SIE | bUC_CLR_ALL;
    mDelayuS(100);
    USB_CTRL = 0x00;
    mDelayuS(100);

    /* Enable EP1 TX (IN) + RX (OUT), single buffer mode */
    UEP4_1_MOD = bUEP1_TX_EN | bUEP1_RX_EN;
    UEP2_3_MOD = 0x00;

    /* EP0 DMA */
    UEP0_DMA_L = 0x00;
    UEP0_DMA_H = 0x00;
    UEP0_T_LEN = 0;
    UEP0_CTRL  = UEP_R_RES_ACK | UEP_T_RES_NAK;

    /* EP1 DMA — base at 0x0040; OUT at +0, IN at +64 */
    UEP1_DMA_L = 0x40;
    UEP1_DMA_H = 0x00;
    UEP1_T_LEN = 0;
    UEP1_CTRL  = bUEP_AUTO_TOG | UEP_R_RES_ACK | UEP_T_RES_NAK;

    ep1_in_busy    = 0;
    new_out_report = 0;

    /* Reset state */
    USB_DEV_AD     = 0x00;
    pending_address = 0;
    usb_config_value = 0;
    hid_idle_rate  = 0;

    /* Enable USB port */
    UDEV_CTRL = bUD_PD_DIS | bUD_PORT_EN;
    USB_INT_FG = 0xFF;
    USB_INT_EN = bUIE_SUSPEND | bUIE_TRANSFER | bUIE_BUS_RST;
    IE_USB = 1;
    EA = 1;

    /* Enable DMA, then pull-up (host sees device) */
    USB_CTRL = bUC_DMA_EN | bUC_INT_BUSY;
    mDelaymS(1);
    USB_CTRL = bUC_DEV_PU_EN | bUC_INT_BUSY | bUC_DMA_EN;
}

uint8_t usb_hid_process(void)
{
    uint8_t got = 0;

    /* Send an IN report when EP1 IN is idle and the device is configured */
    if (usb_config_value && !ep1_in_busy) {
        build_in_report(ep1_in_buffer);
        UEP1_T_LEN = HID_REPORT_SIZE;
        UEP1_CTRL  = (UEP1_CTRL & (uint8_t)~MASK_UEP_T_RES) | UEP_T_RES_ACK;
        ep1_in_busy = 1;
    }

    if (new_out_report) {
        new_out_report = 0;
        got = 1;
    }

    return got;
}

/* ---- USB ISR ---- */

void USBInterrupt(void) __interrupt(INT_NO_USB)
{
    uint8_t int_st;

    if (UIF_BUS_RST) {
        USB_DEV_AD     = 0x00;
        pending_address = 0;
        usb_config_value = 0;
        ep0_data_remaining = 0;
        ep1_in_busy    = 0;
        new_out_report = 0;

        UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
        UEP0_T_LEN = 0;
        UEP1_CTRL  = bUEP_AUTO_TOG | UEP_R_RES_ACK | UEP_T_RES_NAK;
        UEP1_T_LEN = 0;
        USB_INT_FG = 0xFF;
        return;
    }

    if (UIF_TRANSFER) {
        int_st = USB_INT_ST;

        switch (int_st & (MASK_UIS_ENDP | MASK_UIS_TOKEN)) {

        /* ---- EP0 ---- */
        case 0x00 | UIS_TOKEN_SETUP:
            handle_setup_packet();
            break;

        case 0x00 | UIS_TOKEN_IN:
            UEP0_CTRL ^= bUEP_T_TOG;
            if (pending_address) {
                USB_DEV_AD = (USB_DEV_AD & bUDA_GP_BIT) | pending_address;
                pending_address = 0;
            }
            if (ep0_data_remaining)
                ep0_send_next_data();
            else {
                UEP0_T_LEN = 0;
                UEP0_CTRL = (UEP0_CTRL & (uint8_t)~MASK_UEP_T_RES) | UEP_T_RES_NAK;
            }
            break;

        case 0x00 | UIS_TOKEN_OUT:
            /* Handle SET_REPORT data stage */
            if (USB_RX_LEN > 0) {
                process_out_report(ep0_buffer, USB_RX_LEN);
            }
            /* Status stage — send ZLP */
            ep0_send_zlp();
            break;

        /* ---- EP1 IN ---- */
        case 0x01 | UIS_TOKEN_IN:
            UEP1_T_LEN = 0;
            UEP1_CTRL = (UEP1_CTRL & (uint8_t)~MASK_UEP_T_RES) | UEP_T_RES_NAK;
            ep1_in_busy = 0;
            break;

        /* ---- EP1 OUT ---- */
        case 0x01 | UIS_TOKEN_OUT:
            if (USB_RX_LEN > 0) {
                process_out_report(ep1_out_buffer, USB_RX_LEN);
            }
            UEP1_CTRL = (UEP1_CTRL & (uint8_t)~MASK_UEP_R_RES) | UEP_R_RES_ACK;
            break;

        default:
            break;
        }

        UIF_TRANSFER = 0;
    }

    if (UIF_SUSPEND) {
        UIF_SUSPEND = 0;
    }
}
