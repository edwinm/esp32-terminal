# Top-level ESP-IDF project file. PlatformIO's espidf framework still requires this.
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# LovyanGFX 1.1.12 predates GCC 14, which promotes several benign warnings to
# errors under IDF's global -Werror=all. Downgrade just those (appended after
# -Werror=all, so they win) so the third-party build succeeds.
idf_build_set_property(COMPILE_OPTIONS
    "-Wno-error=array-bounds;-Wno-error=stringop-overflow;-Wno-error=stringop-overread;-Wno-error=maybe-uninitialized"
    APPEND)

project(esp32_terminal)
