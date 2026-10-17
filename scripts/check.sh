#!/usr/bin/env bash
# Everything checkable without a board (T11). CI runs exactly this.
set -euo pipefail
cd "$(dirname "$0")/.."

step() { echo; echo "==> $*"; }

step "Host suites, every display profile"
make -C sim test

step "Image checks fail when they should"
./scripts/test-checks.sh

step "Build every image"
for env in s3cam s3cam-dev s3cam-qemu s3cam-qemu-selftest; do
    pio run -e "${env}" >/dev/null
    echo "built ${env}"
done

step "No radio stack in any image"
./scripts/check-radios.sh s3cam s3cam-dev s3cam-qemu

step "No QEMU harness in a board image"
./scripts/check-release.sh

step "QEMU: the vector suites on the Xtensa build"
SELFTEST_TIMEOUT=${SELFTEST_TIMEOUT:-2400} ./scripts/qemu.sh --env s3cam-qemu-selftest --selftest

step "QEMU: boot and flows on the real image"
./scripts/qemu.sh --smoke
./scripts/qemu-flow.py

echo
echo "ALL CHECKS PASSED"
