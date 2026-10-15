cmake_minimum_required(VERSION 3.16.0)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# Re-run CMake when HEAD moves.
#
# ESP-IDF stamps `git describe` into esp_app_desc_t as PROJECT_VER at
# configure time, and nothing in the ninja graph depends on git state, so an
# incremental build after a commit keeps reporting the previous commit
# (LeekWallet verified this). A binary has to name the source it was built
# from, so the reflog is a configure dependency. Resolved through
# `git rev-parse --git-path` so worktrees work. No git is not an error: a
# source tarball builds, and IDF falls back to version "1".
find_package(Git QUIET)
if(GIT_FOUND)
    foreach(git_file HEAD logs/HEAD)
        execute_process(
            COMMAND ${GIT_EXECUTABLE} rev-parse --git-path ${git_file}
            WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
            OUTPUT_VARIABLE git_path
            OUTPUT_STRIP_TRAILING_WHITESPACE
            ERROR_QUIET)
        if(git_path AND EXISTS "${CMAKE_CURRENT_LIST_DIR}/${git_path}")
            set(git_path "${CMAKE_CURRENT_LIST_DIR}/${git_path}")
        endif()
        if(git_path AND EXISTS "${git_path}")
            set_property(DIRECTORY APPEND PROPERTY
                         CMAKE_CONFIGURE_DEPENDS "${git_path}")
        endif()
    endforeach()
endif()

project(leek-btc-signer)
