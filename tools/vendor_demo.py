#!/usr/bin/env python3
import argparse
import sys

import usb.core
import usb.util

REQ_GPIO_WRITE    = 0x01
REQ_GPIO_READ     = 0x02
REQ_GPIO_READ_ALL = 0x03

# Pin ID: high nibble = port, low nibble = bit
PINS = {
    "p10": 0x10, "p11": 0x11, "p12": 0x12, "p13": 0x13,
    "p14": 0x14, "p15": 0x15, "p16": 0x16, "p17": 0x17,
    "p30": 0x30, "p31": 0x31, "p32": 0x32, "p33": 0x33,
    "p34": 0x34, "p35": 0x35,
}


def get_dev(vid: int, pid: int):
    dev = usb.core.find(idVendor=vid, idProduct=pid)
    if dev is None:
        raise RuntimeError(f"USB device not found: {vid:04x}:{pid:04x}")
    try:
        dev.set_configuration()
    except usb.core.USBError:
        pass
    return dev


def gpio_write(dev, pin_id: int, level: int):
    dev.ctrl_transfer(0x40, REQ_GPIO_WRITE, pin_id, 1 if level else 0, [])


def gpio_read(dev, pin_id: int) -> int:
    data = dev.ctrl_transfer(0xC0, REQ_GPIO_READ, pin_id, 0, 1)
    return int(data[0])


def gpio_read_all(dev):
    data = dev.ctrl_transfer(0xC0, REQ_GPIO_READ_ALL, 0, 0, 2)
    return int(data[0]), int(data[1])


def main():
    parser = argparse.ArgumentParser(description="CH552 vendor GPIO control")
    parser.add_argument("--vid", type=lambda x: int(x, 0), default=0x1234)
    parser.add_argument("--pid", type=lambda x: int(x, 0), default=0x5678)

    sub = parser.add_subparsers(dest="cmd", required=True)

    p_write = sub.add_parser("write", help="Write pin level")
    p_write.add_argument("pin", choices=sorted(PINS.keys()))
    p_write.add_argument("level", type=int, choices=[0, 1])

    p_read = sub.add_parser("read", help="Read single pin")
    p_read.add_argument("pin", choices=sorted(PINS.keys()))

    sub.add_parser("status", help="Read all port states")

    args = parser.parse_args()

    try:
        dev = get_dev(args.vid, args.pid)

        if args.cmd == "write":
            pin_id = PINS[args.pin]
            gpio_write(dev, pin_id, args.level)
            print(f"ok: {args.pin}={args.level}")
        elif args.cmd == "read":
            pin_id = PINS[args.pin]
            val = gpio_read(dev, pin_id)
            print(f"{args.pin}={val}")
        elif args.cmd == "status":
            p1, p3 = gpio_read_all(dev)
            print(f"P1=0x{p1:02X}  P3=0x{p3:02X}")
            for name, pid in sorted(PINS.items()):
                port = pid >> 4
                bit = pid & 0x0F
                val = (p1 >> bit) & 1 if port == 1 else (p3 >> bit) & 1
                print(f"  {name}={val}")

    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
