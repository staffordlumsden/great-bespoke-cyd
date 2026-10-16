cmake_minimum_required(VERSION 3.16)
set(PROJECT_VER "0.1.0")
set(CYD_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../..")
set(EXTRA_COMPONENT_DIRS
    "${CYD_ROOT}/projects/launcher/firmware/components/cyd_hal")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(LEDMicro)
