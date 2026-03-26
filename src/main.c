#include <stdint.h>

#include "ch554.h"
#include "debug.h"
#include "usb_vendor.h"
// #include "usb_hid.h"

/*
 * SDCC 4.1.0 does not auto-generate the interrupt vector LJMP for
 * __interrupt(8) on MCS-51.  We define it manually here so that
 * the USB ISR at 0x0043 actually points to our handler.
 */
void _usb_isr_vector(void) __naked
{
    __asm
    .area CABS    (ABS,CODE)
    .org  0x0043
    ljmp  _USBInterrupt
    __endasm;
}

void main(void)
{
    CfgFsys();
    mDelaymS(20);  /* longer delay for PLL/oscillator stabilization */

    gpio_demo_init();
    usb_vendor_init();


    // gpio_hid_init();
    // usb_hid_init();
    // while (1) {usb_hid_process();}

    while (1) {}
}
