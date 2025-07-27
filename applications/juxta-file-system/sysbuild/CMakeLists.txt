cmake_minimum_required(VERSION 3.21)

# Set CMake policies
cmake_policy(SET CMP0057 NEW)

# Generic sysbuild CMakeLists.txt file used as sysbuild entry point for multi-image builds.
#
# Applications which requires custom handling when built using sysbuild may copy this
# file to the folder `<app-dir>/sysbuild` and use as a template and extend as needed.

find_package(Sysbuild REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(sysbuild LANGUAGES) 