# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

# The fse_dcu_2612 shield lives in this repository under boards/shields/.
# Zephyr only searches BOARD_ROOT plus ZEPHYR_BASE, so add ourselves here —
# otherwise every build would need -DBOARD_ROOT=... on the command line.
# Must precede find_package(Zephyr), which is where the roots are evaluated.
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

# Out-of-tree drivers (HX8357 display) and their devicetree bindings.  Keeping
# them here rather than patched into the Zephyr tree is what makes a fresh
# `west init` workspace build without manual file copying.
list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/drivers)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(fse_dcu_2612)

# ── Vehicle identification ────────────────────────────────────────────────────
#
# The public version number is 2612.<minor>.<patch>, where 2612 identifies the
# vehicle generation: year 26, twelfth car built.
#
# That number cannot live in VERSION_MAJOR — Zephyr's version.cmake and the
# MCUboot image header both store it in 8 bits and reject anything above 255.
# VERSION therefore carries the vehicle index (12) and the year in a field of
# its own; Zephyr extracts only its known keys by regex and ignores the rest.
# The public number is derived from both, so the index stays single-sourced.
file(READ ${APPLICATION_SOURCE_DIR}/VERSION dcu_version_file)
string(REGEX MATCH "DCU_VEHICLE_YEAR = ([0-9]+)" _ "${dcu_version_file}")
set(DCU_VEHICLE_YEAR "${CMAKE_MATCH_1}")

if(NOT DCU_VEHICLE_YEAR)
  message(FATAL_ERROR "DCU_VEHICLE_YEAR is missing from ${APPLICATION_SOURCE_DIR}/VERSION")
endif()

# Zephyr already tracks VERSION, but declare our own dependency on it so a
# changed year triggers a reconfigure regardless of what version.cmake does.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
             ${APPLICATION_SOURCE_DIR}/VERSION)

math(EXPR DCU_VEHICLE_ID "${DCU_VEHICLE_YEAR} * 100 + ${APP_VERSION_MAJOR}")
message(STATUS "DCU vehicle ${DCU_VEHICLE_ID}, "
               "firmware ${DCU_VEHICLE_ID}.${APP_VERSION_MINOR}.${APP_PATCHLEVEL}")

target_compile_definitions(app PRIVATE DCU_VEHICLE_ID=${DCU_VEHICLE_ID})

# Collect all .c files recursively under src/
FILE(GLOB_RECURSE app_sources src/*.c)
target_sources(app PRIVATE ${app_sources})

# Add src/ as include root so headers can be included as:
#   #include "services/event_bus/event_bus.h"
#   #include "modules/can/can.h"
target_include_directories(app PRIVATE src)
