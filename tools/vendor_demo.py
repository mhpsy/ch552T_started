#!/usr/bin/env python3
import argparse
import sys

import usb.core
import usb.util

REQ_PIN10_FORCE_LOW = 0x01
REQ_PIN11_WRITE = 0x02
REQ_GPIO_STATE = 0x03


def get_dev(vid: int, pid: int):
    dev = usb.core.find(idVendor=vid, idProduct=pid)
    if dev is None:
        raise RuntimeError(f"USB device not found: {vid:04x}:{pid:04x}")

    try:
        dev.set_configuration()
    except usb.core.USBError:
        pass

    return dev


def pin10_force_low(dev):
    dev.ctrl_transfer(0x40, REQ_PIN10_FORCE_LOW, 0, 0, [])


def pin11_write(dev, level: int):
    dev.ctrl_transfer(0x40, REQ_PIN11_WRITE, 1 if level else 0, 0, [])


def gpio_state(dev):
    data = dev.ctrl_transfer(0xC0, REQ_GPIO_STATE, 0, 0, 1)
    value = int(data[0])
    pin10 = 1 if (value & 0x01) else 0
    pin11 = 1 if (value & 0x02) else 0
    return pin10, pin11


def main():
    parser = argparse.ArgumentParser(description="CH552 vendor control demo")
    parser.add_argument("--vid", type=lambda x: int(x, 0), default=0x1234)
    parser.add_argument("--pid", type=lambda x: int(x, 0), default=0x5678)

    sub = parser.add_subparsers(dest="cmd", required=True)
    sub.add_parser("pin10low")

    p_pin11 = sub.add_parser("pin11")
    p_pin11.add_argument("level", type=int, choices=[0, 1])

    sub.add_parser("status")

    args = parser.parse_args()

    try:
        dev = get_dev(args.vid, args.pid)

        if args.cmd == "pin10low":
            pin10_force_low(dev)
            print("ok: pin10 forced low")
        elif args.cmd == "pin11":
            pin11_write(dev, args.level)
            print(f"ok: pin11={args.level}")
        elif args.cmd == "status":
            pin10, pin11 = gpio_state(dev)
            print(f"pin10={pin10} pin11={pin11}")

    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
