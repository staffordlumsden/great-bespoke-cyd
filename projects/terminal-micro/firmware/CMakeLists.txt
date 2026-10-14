cmake_minimum_required(VERSION 3.16)
set(PROJECT_VER "0.2.3-headroom")
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../launcher/firmware/components/cyd_hal"
    "${CMAKE_CURRENT_LIST_DIR}/../../launcher/firmware/components/cyd_clock"
    "${CMAKE_CURRENT_LIST_DIR}/components/cyd_keyboard"
    "${CMAKE_CURRENT_LIST_DIR}/../../../common/libssh2_esp")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(TerminalMicro)
