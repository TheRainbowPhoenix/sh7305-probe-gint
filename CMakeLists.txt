# SPDX-FileCopyrightText: 2026 Artem Novak
# SPDX-License-Identifier: BSD-3-Clause

cmake_minimum_required(VERSION 3.15)
project(sh7305-probe LANGUAGES C)

include(GenerateHH3)
find_package(Gint 2.9 REQUIRED)

add_executable(sh7305_probe Addin.c)
target_link_libraries(sh7305_probe Gint::Gint)

if("${FXSDK_PLATFORM_LONG}" STREQUAL "fxCP")
    generate_hh3(TARGET sh7305_probe OUTPUT "SH7305Probe.hh3")
else()
    message(FATAL_ERROR "This probe requires fxsdk build-cp (fxCP/HH3)")
endif()

# The 1.5 MiB blob is generated as a separate file, never linked to HH3.
find_package(Python3 COMPONENTS Interpreter REQUIRED)
add_custom_command(
    OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/romprobe.bin"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_romprobe.py"
            "${CMAKE_CURRENT_BINARY_DIR}/romprobe.bin"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tools/make_romprobe.py"
    COMMENT "Generating external 1.5 MiB romprobe.bin"
    VERBATIM
)
add_custom_target(romprobe ALL
    DEPENDS "${CMAKE_CURRENT_BINARY_DIR}/romprobe.bin")
