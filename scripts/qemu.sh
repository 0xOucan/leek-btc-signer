#!/usr/bin/env bash
# Build the s3cam-qemu image and boot it under Espressif's QEMU fork.
#
#   ./scripts/qemu.sh                 interactive, Ctrl-A X quits
#   ./scripts/qemu.sh --smoke         boot, wait for LBS-READY, exit 0 or 1
#   ./scripts/qemu.sh --env s3cam-qemu-selftest --selftest
#                                     run the vector suites on the device
#
# Extra arguments go to QEMU, so `-s -S` waits for GDB on :1234.
# What QEMU does and does not emulate is in docs/TESTING.md.
set -euo pipefail
cd "$(dirname "$0")/.."

ENV="s3cam-qemu"
if [[ "${1:-}" == "--env" ]]; then ENV=$2; shift 2; fi
BUILD_DIR=".pio/build/${ENV}"
IMAGE="${BUILD_DIR}/qemu-flash.bin"
# Must match partitions.csv and sdkconfig: a smaller image makes the
# bootloader reject the partition table.
FLASH_SIZE="16MB"
SMOKE_TIMEOUT=60

if ! command -v qemu-system-xtensa >/dev/null 2>&1; then
    echo "error: qemu-system-xtensa not found; install Espressif's fork:" >&2
    echo "  python -m espressif.idf_tools install qemu-xtensa" >&2
    exit 1
fi

smoke=0
selftest=0
if [[ "${1:-}" == "--smoke" ]]; then smoke=1; shift; fi
if [[ "${1:-}" == "--selftest" ]]; then selftest=1; shift; fi

echo "==> Building ${ENV}"
pio run -e "${ENV}"

echo "==> Merging flash image"
# The S3 boots from offset 0x0, unlike the original ESP32 at 0x1000.
# PlatformIO's esptool, pinned with the platform, rather than whatever the
# system Python happens to have.
ESPTOOL="${HOME}/.platformio/packages/tool-esptoolpy/esptool.py"
python3 "${ESPTOOL}" --chip esp32s3 merge_bin \
    -o "${IMAGE}" \
    --flash_mode dio --flash_freq 80m --flash_size "${FLASH_SIZE}" \
    --fill-flash-size "${FLASH_SIZE}" \
    0x0     "${BUILD_DIR}/bootloader.bin" \
    0x8000  "${BUILD_DIR}/partitions.bin" \
    0x10000 "${BUILD_DIR}/firmware.bin" >/dev/null

qemu=(qemu-system-xtensa -nographic -machine esp32s3
      -drive "file=${IMAGE},if=mtd,format=raw")

if (( selftest )); then
    echo "==> Self-test boot (waiting up to ${SELFTEST_TIMEOUT:-300}s)"
    log="${BUILD_DIR}/qemu-selftest.log"
    timeout "${SELFTEST_TIMEOUT:-300}" "${qemu[@]}" "$@" </dev/null >"${log}" 2>&1 &
    pid=$!
    while kill -0 "${pid}" 2>/dev/null; do
        if grep -q "SELFTEST-DONE" "${log}"; then break; fi
        sleep 1
    done
    kill "${pid}" 2>/dev/null || true
    grep -E "^SELFTEST|FAIL" "${log}" || true
    if grep -q "SELFTEST-DONE 0" "${log}"; then echo "PASS"; exit 0; fi
    echo "FAIL: log ${log}" >&2
    tail -30 "${log}" >&2
    exit 1
fi

if (( smoke )); then
    echo "==> Smoke boot (waiting up to ${SMOKE_TIMEOUT}s for LBS-READY)"
    log="${BUILD_DIR}/qemu-smoke.log"
    timeout "${SMOKE_TIMEOUT}" "${qemu[@]}" "$@" </dev/null >"${log}" 2>&1 &
    pid=$!
    for _ in $(seq "${SMOKE_TIMEOUT}"); do
        if grep -q "LBS-READY" "${log}"; then
            kill "${pid}" 2>/dev/null || true
            grep -E "Leek BTC Signer|board=" "${log}" || true
            echo "PASS"
            exit 0
        fi
        kill -0 "${pid}" 2>/dev/null || break
        sleep 1
    done
    kill "${pid}" 2>/dev/null || true
    echo "FAIL: no LBS-READY. Log: ${log}" >&2
    tail -20 "${log}" >&2
    exit 1
fi

echo "==> Booting (Ctrl-A X to quit)"
exec "${qemu[@]}" "$@"
