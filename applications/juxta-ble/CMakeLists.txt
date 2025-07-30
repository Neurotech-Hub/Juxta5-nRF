#-------------------------------------------------------------------------------
# JUXTA BLE Application
#
# Copyright (c) 2024 NeurotechHub
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(juxta_ble LANGUAGES C)

target_sources(app PRIVATE
    src/main.c
    src/ble_service.c
)

# Add include directories for our libraries
target_include_directories(app PRIVATE 
    ../../lib/juxta_vitals_nrf52/include
)

# Add library source files directly
target_sources(app PRIVATE 
    ../../lib/juxta_vitals_nrf52/src/vitals.c
) 