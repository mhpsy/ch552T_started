#!/bin/bash

PIN=p32
while true; do
    python ./tools/vendor_demo.py write $PIN 1
    sleep 1
    python ./tools/vendor_demo.py write $PIN 0
    sleep 1
done
