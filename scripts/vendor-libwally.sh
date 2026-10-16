#!/usr/bin/env bash
# Vendor libwally-core (with its secp256k1-zkp submodule) into
# components/libwally-core/upstream (T12).
#
# Pinned to a tag AND a commit: a tag can be moved, a commit cannot. Builds
# never fetch anything; this script is the only thing that touches the
# network, and its output is committed and reviewable.
#
#   ./scripts/vendor-libwally.sh
set -euo pipefail
cd "$(dirname "$0")/.."

TAG=release_1.5.6
COMMIT=0c41f38fb1c201786e9c3ac9eae4f5f80c051399
SECP_COMMIT=45f6f0f158c5ae80a2c8a53398ea4adbf19af6dc
DEST=components/libwally-core/upstream

tmp=$(mktemp -d)
trap 'rm -rf "${tmp}"' EXIT

git clone -q --depth 1 --branch "${TAG}" --recurse-submodules --shallow-submodules \
    https://github.com/ElementsProject/libwally-core.git "${tmp}/w"
got=$(git -C "${tmp}/w" rev-parse HEAD)
secp=$(git -C "${tmp}/w/src/secp256k1" rev-parse HEAD)
[[ "${got}" == "${COMMIT}" ]] || { echo "tag ${TAG} is ${got}, expected ${COMMIT}" >&2; exit 1; }
[[ "${secp}" == "${SECP_COMMIT}" ]] || { echo "secp256k1 is ${secp}, expected ${SECP_COMMIT}" >&2; exit 1; }

rm -rf "${DEST}"
mkdir -p "${DEST}/src"
cp -r "${tmp}/w/include" "${tmp}/w/LICENSE" "${tmp}/w/README.md" "${DEST}/"
# Library sources only: no bindings, fuzzers, tests or build systems.
(cd "${tmp}/w/src" && find . -maxdepth 1 -type f \( -name '*.c' -o -name '*.h' -o -name '*.inl' \) \
    -exec cp {} "${OLDPWD}/${DEST}/src/" \;)
cp -r "${tmp}/w/src/amalgamation" "${tmp}/w/src/ccan" "${tmp}/w/src/ctaes" \
      "${tmp}/w/src/scrypt" "${tmp}/w/src/data" "${DEST}/src/"
# data/ is kept: its wordlists and test vectors (psbt.json, bip341) feed the
# host suites.
mkdir -p "${DEST}/src/secp256k1"
cp -r "${tmp}/w/src/secp256k1/include" "${tmp}/w/src/secp256k1/src" \
      "${tmp}/w/src/secp256k1/COPYING" "${DEST}/src/secp256k1/"
# secp256k1's own benchmarks and tests are not part of the library.
rm -rf "${DEST}/src/secp256k1/src/wasm" "${DEST}/src/secp256k1/src/ctime_tests.c" \
       "${DEST}/src/secp256k1/src/bench"*.c "${DEST}/src/secp256k1/src/tests"*.c \
       "${DEST}/src/amalgamation/windows_config"

cat > "${DEST}/VENDORED" <<INFO
libwally-core ${TAG}
commit ${COMMIT}
secp256k1-zkp ${SECP_COMMIT}
fetched by scripts/vendor-libwally.sh
INFO
du -sh "${DEST}"
