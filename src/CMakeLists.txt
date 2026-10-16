# Leek BTC Signer main component.
#
# core/ is pure C and also builds on the host (sim/). hal/ is ESP-IDF only;
# each hal file compiles to nothing unless its board, profile or image
# matches, so the glob takes them all.
FILE(GLOB app_sources
    ${CMAKE_SOURCE_DIR}/src/*.c
    ${CMAKE_SOURCE_DIR}/src/core/*.c
    ${CMAKE_SOURCE_DIR}/src/hal/*.c)

idf_component_register(
    SRCS ${app_sources}
    INCLUDE_DIRS "." "core" "hal"
)
