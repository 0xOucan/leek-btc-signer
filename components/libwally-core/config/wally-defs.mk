# Compile definitions shared by the host build (sim/Makefile) and the
# ESP-IDF component (CMakeLists.txt). Keep the two in step.
#
# Bitcoin only: no Liquid/Elements in the ABI or the build.
# ECMULT_WINDOW_SIZE 8: verification tables of ~32 KB instead of ~1 MB at
#   the default 15; the device verifies rarely and signs often.
# ECMULT_GEN_KB 22: secp256k1's mid-size signing table, about 22 KB.
# BUILD_MINIMAL: English BIP-39 wordlist only. The others are ~56 KB of
#   internal RAM in any image that calls bip39_get_wordlist (the self-test);
#   the firmware uses only English.
WALLY_DEFS := -DWALLY_ABI_NO_ELEMENTS=1 -DECMULT_WINDOW_SIZE=8 -DECMULT_GEN_KB=22 -DBUILD_MINIMAL=1
