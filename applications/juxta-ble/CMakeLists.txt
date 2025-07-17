#-------------------------------------------------------------------------------
# JUXTA BLE Application
#
# Copyright (c) 2024 NeurotechHub
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(juxta_ble LANGUAGES C)

target_sources(app PRIVATE 
    src/main.c
    src/ble_service.c
) 