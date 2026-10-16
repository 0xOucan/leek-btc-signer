# libwally-core + secp256k1-zkp as one ESP-IDF component (T12), built from
# upstream's single-file amalgamation, the way Blockstream's Jade builds it.
#
# Sources: upstream/, vendored by scripts/vendor-libwally.sh and pinned to a
# commit. Settings: config/config.h, and the definitions below, which must
# match config/wally-defs.mk (used by the host build in sim/).

idf_component_register(
    SRCS "upstream/src/amalgamation/combined.c"
    INCLUDE_DIRS "upstream/include"
    PRIV_INCLUDE_DIRS
        "config"
        "upstream"
        "upstream/src"
        "upstream/src/ccan"
        "upstream/src/secp256k1"
        "upstream/src/secp256k1/include"
)

# WALLY_ABI_NO_ELEMENTS changes the public headers, so callers see it too.
target_compile_definitions(${COMPONENT_LIB}
    PUBLIC WALLY_ABI_NO_ELEMENTS=1
    PRIVATE ECMULT_WINDOW_SIZE=8 ECMULT_GEN_KB=22)

# Upstream code, upstream's warnings: not ours to fix, and -Werror on them
# would make a toolchain bump break the build for no security reason.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -O2 -Wno-error -Wno-unused-function -Wno-unused-variable
    -Wno-unused-but-set-variable -Wno-maybe-uninitialized)
