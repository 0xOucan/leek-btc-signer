#!/usr/bin/env bash
# Prove the image checks fail when they should (T1, T7). A check that has
# never failed has not been shown to check anything.
set -euo pipefail
cd "$(dirname "$0")/.."

tmp=$(mktemp -d)
trap 'rm -rf "${tmp}"' EXIT

# Host-built objects stand in for firmware with the forbidden symbols; the
# host nm reads them.
cat > "${tmp}/radio.c" <<'C'
void esp_wifi_init(void) {}
void i2c_master_write_to_device(void) {}
C
cat > "${tmp}/harness.c" <<'C'
void qemu_harness_start(void) {}
void i2c_master_write_to_device(void) {}
C
cat > "${tmp}/clean.c" <<'C'
void i2c_master_write_to_device(void) {}
C
for f in radio harness clean; do cc -c -o "${tmp}/${f}.o" "${tmp}/${f}.c"; done

expect() {   # expect <pass|fail> <command...>
    local want=$1; shift
    if "$@" >/dev/null 2>&1; then got=pass; else got=fail; fi
    if [[ "${got}" != "${want}" ]]; then
        echo "FAIL: expected ${want}: $*" >&2
        exit 1
    fi
}

export NM=nm
expect fail ./scripts/check-radios.sh --elf "${tmp}/radio.o"
expect pass ./scripts/check-radios.sh --elf "${tmp}/clean.o"
expect fail ./scripts/check-release.sh --elf "${tmp}/harness.o"
expect pass ./scripts/check-release.sh --elf "${tmp}/clean.o"
expect fail ./scripts/check-release.sh --elf "${tmp}/missing.o"
echo "PASS image checks fail on a radio, a harness, and a missing image"
