#!/usr/bin/env bash
set -uo pipefail

HEX_PATH="${1:-.pio/build/ch552_raw/firmware.hex}"
TIMEOUT_SEC="${2:-30}"

if [[ ! -f "$HEX_PATH" ]]; then
  echo "hex not found: $HEX_PATH"
  echo "run: platformio run -e ch552_raw"
  exit 1
fi

echo "waiting up to ${TIMEOUT_SEC}s for CH55x ISP device (4348:55e0 / 1a86:55e0)..."
attempt=0
end=$((SECONDS + TIMEOUT_SEC))
while (( SECONDS < end )); do
  if lsusb | grep -Eiq '4348:55e0|1a86:55e0'; then
    info_out="$(wchisp -u info 2>&1 || true)"
    if echo "$info_out" | grep -q "no enough permission"; then
      echo "detected CH55x ISP device but permission denied."
      echo "install udev rule, then replug device:"
      echo "  sudo cp ~/.platformio/packages/tool-vnproch55x/99-ch55xbl.rules /etc/udev/rules.d/"
      echo "  sudo udevadm control --reload-rules"
      echo "  sudo udevadm trigger"
      exit 3
    fi

    if wchisp -u info >/dev/null 2>&1; then
      attempt=$((attempt + 1))
      echo "device detected, flashing..."
      if wchisp -u flash "$HEX_PATH" -V -R; then
        echo "flash done"
        exit 0
      fi
      echo "flash attempt ${attempt} failed, keep waiting..."
    fi
  fi
  sleep 0.2
done

echo "timeout: ISP device not detected"
exit 2
