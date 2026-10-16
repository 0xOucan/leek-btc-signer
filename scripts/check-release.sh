#!/usr/bin/env bash
# Fail if a board image carries anything that only belongs in QEMU (T7).
#
#   ./scripts/check-release.sh [env ...]     default: s3cam s3cam-dev
#   ./scripts/check-release.sh --elf FILE    one ELF (used by test-checks.sh)
#
# The QEMU harness reads input from and dumps the framebuffer to UART0 -
# including any seed words on screen. It is compiled only with
# LBS_QEMU_HARNESS; this proves from the ELF that it stayed out, and that
# the image drives a real panel rather than the harness stand-in.
set -euo pipefail
cd "$(dirname "$0")/.."

NM=${NM:-$(ls "${HOME}"/.platformio/packages/toolchain-xtensa-esp-elf/bin/*-elf-nm | head -1)}

targets=()
if [[ "${1:-}" == "--elf" ]]; then
    targets=("$2")
else
    for env in "${@:-s3cam s3cam-dev}"; do
        for e in ${env}; do targets+=(".pio/build/${e}/firmware.elf"); done
    done
fi

fail=0
for elf in "${targets[@]}"; do
    if [[ ! -f "${elf}" ]]; then
        echo "FAIL ${elf}: not built" >&2
        fail=1
        continue
    fi
    syms=$("${NM}" "${elf}")
    if grep -qE ' qemu_harness_' <<<"${syms}"; then
        echo "FAIL ${elf}: QEMU harness linked into a board image" >&2
        fail=1
    elif ! grep -qE ' i2c_master_write_to_device$' <<<"${syms}"; then
        echo "FAIL ${elf}: no panel driver linked" >&2
        fail=1
    else
        echo "PASS ${elf}: no harness, real panel driver"
    fi
done
exit "${fail}"
