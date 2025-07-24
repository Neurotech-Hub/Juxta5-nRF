#-------------------------------------------------------------------------------
# JUXTA File System Test Application
#
# Copyright (c) 2024 NeurotechHub
# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.13.1)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(juxta_file_system LANGUAGES C)

target_sources(app PRIVATE 
    src/main.c
    src/fram_test.c
    src/framfs_test.c
)

# Add include directories for our libraries
target_include_directories(app PRIVATE 
    ../../lib/juxta_fram/include
    ../../lib/juxta_framfs/include
)

# Add library source files directly
target_sources(app PRIVATE 
    ../../lib/juxta_fram/src/fram.c
    ../../lib/juxta_framfs/src/framfs.c
) 