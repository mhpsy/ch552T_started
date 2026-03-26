#include <stdint.h>

#include "ch554.h"
#include "ch554_usb.h"
#include "debug.h"

#include "usb_vendor.h"

/*
 * Pin ID encoding: high nibble = port number, low nibble = bit number
 * CH552T TSSOP-20 available GPIO pins:
 *
 *   Port 1 (8 pins)              Port 3 (6 pins, P3.6/P3.7 = USB)
 *   P1.0  pin 2   T2/CAP1        P3.0  pin 15  RXD/PWM1_
 *   P1.1  pin 3   T2EX/CAP2/AIN0 P3.1  pin 16  TXD/PWM2_
 *   P1.2  pin 4   RXD_            P3.2  pin 17  INT0/TXD1_/AIN3
 *   P1.3  pin 5   TXD_            P3.3  pin 18  INT1
 *   P1.4  pin 6   SCS/AIN1        P3.4  pin 19  PWM2/RXD1_/T0
 *   P1.5  pin 7   MOSI/PWM1/AIN2  P3.5  pin 20  T1
 *   P1.6  pin 8   MISO/RXD1
 *   P1.7  pin 9   SCK/TXD1
 */
#define PIN_P10  0x10
#define PIN_P11  0x11
#define PIN_P12  0x12
#define PIN_P13  0x13
#define PIN_P14  0x14
#define PIN_P15  0x15
#define PIN_P16  0x16
#define PIN_P17  0x17
#define PIN_P30  0x30
#define PIN_P31  0x31
#define PIN_P32  0x32
#define PIN_P33  0x33
#define PIN_P34  0x34
#define PIN_P35  0x35

#define VND_REQ_GPIO_WRITE    0x01  /* wValueL = pin_id, wIndexL = level */
#define VND_REQ_GPIO_READ     0x02  /* wValueL = pin_id, returns 1 byte  */
#define VND_REQ_GPIO_READ_ALL 0x03  /* returns 2 bytes: [P1, P3]         */

static __xdata __at (0x0000) uint8_t ep0_buffer[MAX_PACKET_SIZE];
static USB_SETUP_REQ setup_req;

static uint8_t pending_address;
static uint8_t usb_config_value;
static uint16_t ep0_data_remaining;
static const uint8_t __code *ep0_data_ptr;

static const uint8_t __code dev_desc[] = {
    0x12,
    USB_DESCR_TYP_DEVICE,
    0x10, 0x01,
    0xFF,
    0x00,
    0x00,
    DEFAULT_ENDP0_SIZE,
    0x34, 0x12,
    0x78, 0x56,
    0x00, 0x01,
    0x01,
    0x02,
    0x03,
    0x01
};

static const uint8_t __code cfg_desc[] = {
    0x09,
    USB_DESCR_TYP_CONFIG,
    0x12, 0x00,
    0x01,
    0x01,
    0x00,
    0x80,
    0x32,

    0x09,
    USB_DESCR_TYP_INTERF,
    0x00,
    0x00,
    0x00,
    0xFF,
    0x00,
    0x00,
    0x00
};

static const uint8_t __code str_lang[] = {
    0x04,
    USB_DESCR_TYP_STRING,
    0x09, 0x04
};

static const uint8_t __code str_mfr[] = {
    0x12,
    USB_DESCR_TYP_STRING,
    'M', 0x00,
    'H', 0x00,
    'P', 0x00,
    'S', 0x00,
    'Y', 0x00,
    '-', 0x00,
    'L', 0x00,
    'A', 0x00
};

static const uint8_t __code str_prod[] = {
    0x24,
    USB_DESCR_TYP_STRING,
    'C', 0x00,
    'H', 0x00,
    '5', 0x00,
    '5', 0x00,
    '2', 0x00,
    ' ', 0x00,
    'V', 0x00,
    'e', 0x00,
    'n', 0x00,
    'd', 0x00,
    'o', 0x00,
    'r', 0x00,
    ' ', 0x00,
    'G', 0x00,
    'P', 0x00,
    'I', 0x00,
    'O', 0x00
};

static const uint8_t __code str_serial[] = {
    0x0E,
    USB_DESCR_TYP_STRING,
    '0', 0x00,
    '0', 0x00,
    '0', 0x00,
    '0', 0x00,
    '0', 0x00,
    '1', 0x00
};

static uint8_t gpio_write_pin(uint8_t pin_id, uint8_t level)
{
    uint8_t port = pin_id >> 4;
    uint8_t bit  = pin_id & 0x0F;
    uint8_t mask;

    if (bit > 7) return 0;
    mask = (uint8_t)(1 << bit);

    if (port == 1) {
        if (level) P1 |= mask;
        else       P1 &= (uint8_t)~mask;
        return 1;
    }
    if (port == 3 && bit <= 5) {
        if (level) P3 |= mask;
        else       P3 &= (uint8_t)~mask;
        return 1;
    }
    return 0;
}

static uint8_t gpio_read_pin(uint8_t pin_id, uint8_t *out)
{
    uint8_t port = pin_id >> 4;
    uint8_t bit  = pin_id & 0x0F;
    uint8_t mask;

    if (bit > 7) return 0;
    mask = (uint8_t)(1 << bit);

    if (port == 1) {
        *out = (P1 & mask) ? 1 : 0;
        return 1;
    }
    if (port == 3) {
        *out = (P3 & mask) ? 1 : 0;
        return 1;
    }
    return 0;
}

void gpio_init_all_low(void)
{
    /* P1.0~P1.7: push-pull output, all low */
    P1_MOD_OC = 0x00;
    P1_DIR_PU = 0xFF;
    P1 = 0x00;

    /* P3.0~P3.5: push-pull output, all low (P3.6/P3.7 = USB, untouched) */
    P3_MOD_OC &= 0xC0;
    P3_DIR_PU |= 0x3F;
    P3 &= 0xC0;
}

static void ep0_set_tx_ack(uint8_t len)
{
    UEP0_T_LEN = len;
    UEP0_CTRL = (UEP0_CTRL & (uint8_t)~MASK_UEP_T_RES) | UEP_T_RES_ACK;
}

static void ep0_send_next_data(void)
{
    uint8_t i;
    uint8_t send_len;

    if (ep0_data_remaining > DEFAULT_ENDP0_SIZE) {
        send_len = DEFAULT_ENDP0_SIZE;
    } else {
        send_len = (uint8_t)ep0_data_remaining;
    }

    for (i = 0; i < send_len; ++i) {
        ep0_buffer[i] = ep0_data_ptr[i];
    }

    ep0_data_ptr += send_len;
    ep0_data_remaining -= send_len;

    ep0_set_tx_ack(send_len);
}

static void ep0_prepare_code_data(const uint8_t __code *src, uint16_t total_len, uint16_t req_len)
{
    ep0_data_ptr = src;
    if (req_len < total_len) {
        ep0_data_remaining = req_len;
    } else {
        ep0_data_remaining = total_len;
    }

    ep0_send_next_data();
}

static void ep0_stall(void)
{
    UEP0_CTRL = (UEP0_CTRL & (uint8_t)~(MASK_UEP_R_RES | MASK_UEP_T_RES)) | UEP_R_RES_STALL | UEP_T_RES_STALL;
}

static void ep0_send_zlp(void)
{
    ep0_data_remaining = 0;
    ep0_set_tx_ack(0);
}

static void handle_standard_request(void)
{
    const uint8_t __code *desc = 0;
    uint16_t desc_len = 0;

    switch (setup_req.bRequest) {
    case USB_GET_DESCRIPTOR:
        if ((setup_req.bRequestType & USB_REQ_TYP_IN) == 0) {
            ep0_stall();
            return;
        }

        switch (setup_req.wValueH) {
        case USB_DESCR_TYP_DEVICE:
            desc = dev_desc;
            desc_len = sizeof(dev_desc);
            break;
        case USB_DESCR_TYP_CONFIG:
            desc = cfg_desc;
            desc_len = sizeof(cfg_desc);
            break;
        case USB_DESCR_TYP_STRING:
            if (setup_req.wValueL == 0) {
                desc = str_lang;
                desc_len = sizeof(str_lang);
            } else if (setup_req.wValueL == 1) {
                desc = str_mfr;
                desc_len = sizeof(str_mfr);
            } else if (setup_req.wValueL == 2) {
                desc = str_prod;
                desc_len = sizeof(str_prod);
            } else if (setup_req.wValueL == 3) {
                desc = str_serial;
                desc_len = sizeof(str_serial);
            }
            break;
        default:
            break;
        }

        if (desc == 0) {
            ep0_stall();
            return;
        }

        ep0_prepare_code_data(desc, desc_len, (uint16_t)setup_req.wLengthL | ((uint16_t)setup_req.wLengthH << 8));
        break;

    case USB_SET_ADDRESS:
        pending_address = setup_req.wValueL & MASK_USB_ADDR;
        ep0_send_zlp();
        break;

    case USB_SET_CONFIGURATION:
        usb_config_value = setup_req.wValueL;
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

static void handle_vendor_request(void)
{
    uint8_t pin_val;

    if ((setup_req.bRequestType & USB_REQ_TYP_IN) == USB_REQ_TYP_IN) {
        if (setup_req.bRequest == VND_REQ_GPIO_READ) {
            if (gpio_read_pin(setup_req.wValueL, &pin_val)) {
                ep0_buffer[0] = pin_val;
                ep0_set_tx_ack(1);
            } else {
                ep0_stall();
            }
            return;
        }
        if (setup_req.bRequest == VND_REQ_GPIO_READ_ALL) {
            ep0_buffer[0] = P1;
            ep0_buffer[1] = P3;
            ep0_set_tx_ack(2);
            return;
        }
        ep0_stall();
        return;
    }

    if (setup_req.bRequest == VND_REQ_GPIO_WRITE) {
        if (gpio_write_pin(setup_req.wValueL, setup_req.wIndexL ? 1 : 0)) {
            ep0_send_zlp();
        } else {
            ep0_stall();
        }
        return;
    }

    ep0_stall();
}

static void handle_setup_packet(void)
{
    setup_req.bRequestType = ep0_buffer[0];
    setup_req.bRequest = ep0_buffer[1];
    setup_req.wValueL = ep0_buffer[2];
    setup_req.wValueH = ep0_buffer[3];
    setup_req.wIndexL = ep0_buffer[4];
    setup_req.wIndexH = ep0_buffer[5];
    setup_req.wLengthL = ep0_buffer[6];
    setup_req.wLengthH = ep0_buffer[7];

    ep0_data_remaining = 0;
    /* After SETUP token, USB spec requires DATA1 for both next TX and RX */
    UEP0_CTRL = bUEP_R_TOG | bUEP_T_TOG | UEP_R_RES_ACK | UEP_T_RES_NAK;

    if ((setup_req.bRequestType & USB_REQ_TYP_MASK) == USB_REQ_TYP_STANDARD) {
        handle_standard_request();
        return;
    }

    if ((setup_req.bRequestType & USB_REQ_TYP_MASK) == USB_REQ_TYP_VENDOR) {
        if ((setup_req.bRequestType & USB_REQ_RECIP_MASK) != USB_REQ_RECIP_DEVICE) {
            ep0_stall();
            return;
        }
        handle_vendor_request();
        return;
    }

    ep0_stall();
}

void usb_vendor_init(void)
{
    /* 1. Enable USB I/O pins */
    PIN_FUNC |= bUSB_IO_EN;

    /* 2. Reset USB SIE — hold reset long enough for hardware */
    USB_CTRL = bUC_RESET_SIE | bUC_CLR_ALL;
    mDelayuS(100);
    USB_CTRL = 0x00;
    mDelayuS(100);

    /* 3. Disable all endpoints except EP0 */
    UEP4_1_MOD = 0x00;
    UEP2_3_MOD = 0x00;

    /* 4. Configure EP0 DMA buffer and default response */
    UEP0_DMA_L = 0x00;
    UEP0_DMA_H = 0x00;
    UEP0_T_LEN = 0;
    UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;

    /* 5. Reset device address */
    USB_DEV_AD = 0x00;
    pending_address = 0;
    usb_config_value = 0;

    /* 6. Enable USB port (no pull-up yet — host must not see us) */
    UDEV_CTRL = bUD_PD_DIS | bUD_PORT_EN;

    /* 7. Clear ALL interrupt flags before enabling interrupts */
    USB_INT_FG = 0xFF;

    /* 8. Enable USB interrupts */
    USB_INT_EN = bUIE_SUSPEND | bUIE_TRANSFER | bUIE_BUS_RST;
    IE_USB = 1;
    EA = 1;

    /* 9. Enable DMA first, then enable pull-up — host sees device NOW */
    USB_CTRL = bUC_DMA_EN | bUC_INT_BUSY;
    mDelaymS(1);
    USB_CTRL = bUC_DEV_PU_EN | bUC_INT_BUSY | bUC_DMA_EN;
}

void USBInterrupt(void) __interrupt(INT_NO_USB)
{
    uint8_t int_st;

    if (UIF_BUS_RST) {
        USB_DEV_AD = 0x00;
        pending_address = 0;
        usb_config_value = 0;
        ep0_data_remaining = 0;
        UEP0_CTRL = UEP_R_RES_ACK | UEP_T_RES_NAK;
        UEP0_T_LEN = 0;
        USB_INT_FG = 0xFF;  /* clear all flags at once on bus reset */
        return;
    }

    if (UIF_TRANSFER) {
        int_st = USB_INT_ST;

        if ((int_st & MASK_UIS_ENDP) == 0) {
            switch (int_st & MASK_UIS_TOKEN) {
            case UIS_TOKEN_SETUP:
                handle_setup_packet();
                break;

            case UIS_TOKEN_IN:
                /* Toggle TX data toggle for next IN packet (EP0 has no auto-toggle) */
                UEP0_CTRL ^= bUEP_T_TOG;

                if (pending_address != 0) {
                    USB_DEV_AD = (USB_DEV_AD & bUDA_GP_BIT) | pending_address;
                    pending_address = 0;
                }

                if (ep0_data_remaining) {
                    ep0_send_next_data();
                } else {
                    UEP0_T_LEN = 0;
                    UEP0_CTRL = (UEP0_CTRL & (uint8_t)~MASK_UEP_T_RES) | UEP_T_RES_NAK;
                }
                break;

            case UIS_TOKEN_OUT:
                UEP0_CTRL = (UEP0_CTRL & (uint8_t)~MASK_UEP_R_RES) | UEP_R_RES_ACK;
                break;

            default:
                break;
            }
        }

        UIF_TRANSFER = 0;
    }

    if (UIF_SUSPEND) {
        UIF_SUSPEND = 0;
        if (USB_MIS_ST & bUMS_SUSPEND) {
            /* Enter suspend — clear wakeup flag */
        }
    }
}
