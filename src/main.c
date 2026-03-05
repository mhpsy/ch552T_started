#include <stdint.h>

#include "ch554.h"
#include "debug.h"
#include "usb_vendor.h"

void main(void)
{
    CfgFsys();
    mDelaymS(5);

    gpio_demo_init();
    usb_vendor_init();

    while (1) {
    }
}
