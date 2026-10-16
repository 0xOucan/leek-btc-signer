#!/usr/bin/env bash
# Fail if any radio stack is linked into a firmware image (T1).
#
#   ./scripts/check-radios.sh [env ...]      default: every built s3cam env
#   ./scripts/check-radios.sh --elf FILE     one ELF (used by test-checks.sh)
#
# Reads the ELF, not the sdkconfig: LeekWallet T17 is a build where the config
# said Wi-Fi was off and the image still carried it. Symbols of type A are
# addresses in the chip's mask ROM, present in every S3 image whatever is
# built, so they are excluded; anything else matching is linked code or data.
set -euo pipefail
cd "$(dirname "$0")/.."

NM=${NM:-$(ls "${HOME}"/.platformio/packages/toolchain-xtensa-esp-elf/bin/*-elf-nm | head -1)}
PATTERN=' (esp_wifi|wifi_|ieee80211|esp_netif|lwip_|esp_bt|ble_|nimble|btdm|bluedroid|esp_phy|phy_init|register_chipv7_phy|tud_|tusb_)'

elfs=()
envs=()
if [[ "${1:-}" == "--elf" ]]; then
    elfs=("$2")
    envs=("$2")
else
    envs=("$@")
fi
if (( ${#envs[@]} == 0 )); then
    for d in .pio/build/s3cam*/; do envs+=("$(basename "$d")"); done
fi

fail=0
for env in "${envs[@]}"; do
    if (( ${#elfs[@]} )); then elf="${env}"; else elf=".pio/build/${env}/firmware.elf"; fi
    if [[ ! -f "${elf}" ]]; then
        echo "FAIL ${env}: ${elf} not built" >&2
        fail=1
        continue
    fi
    hits=$("${NM}" "${elf}" | grep -v ' A ' | grep -E "${PATTERN}" || true)
    if [[ -n "${hits}" ]]; then
        echo "FAIL ${env}: radio or USB stack symbols linked:" >&2
        echo "${hits}" | head -20 >&2
        fail=1
    else
        echo "PASS ${env}: no radio or USB stack linked"
    fi
done
exit "${fail}"
