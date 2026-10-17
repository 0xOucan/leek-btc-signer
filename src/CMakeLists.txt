# Leek BTC Signer main component.
#
# core/ is pure C and also builds on the host (sim/). hal/ is ESP-IDF only;
# each hal file compiles to nothing unless its board, profile or image
# matches, so the glob takes them all.
FILE(GLOB app_sources
    ${CMAKE_SOURCE_DIR}/src/*.c
    ${CMAKE_SOURCE_DIR}/src/core/*.c
    ${CMAKE_SOURCE_DIR}/src/hal/*.c)

# The self-test image (T21) also compiles the host's vector suites, each
# with its main() renamed <suite>_main for src/selftest.c to call. The flag
# comes through cmake_extra_args because sources are chosen at configure
# time, before any compiler flag is read.
set(SELFTESTS test_wally test_btc test_psbt test_cbor test_ur test_ur_fountain
    test_ur_decoder test_ur_encoder test_qrcode test_qr_decode test_qr_encode test_scan
    test_widgets test_text_entry)
# test_qr_view runs on the host for every profile but not here yet: in QEMU
# quirc misses some of its static codes (ROADMAP.md, Phase 3).
set(app_include "." "core" "hal")
if(LBS_SELFTEST)
    foreach(t ${SELFTESTS})
        set(f ${CMAKE_SOURCE_DIR}/sim/${t}.c)
        list(APPEND app_sources ${f})
        set_source_files_properties(${f} PROPERTIES COMPILE_DEFINITIONS "main=${t}_main")
    endforeach()
    list(APPEND app_include "../sim")
endif()

# REQUIRES is explicit so libwally-core's PUBLIC definitions reach these
# sources. Without it they compiled without WALLY_ABI_NO_ELEMENTS while the
# library had it, the PSBT structs differed in size between the two, and the
# self-test image crashed walking a PSBT. btc.h now refuses to build that way.
idf_component_register(
    SRCS ${app_sources}
    INCLUDE_DIRS ${app_include}
    REQUIRES libwally-core tinf quirc driver esp_timer esp_driver_uart esp_app_format
)
