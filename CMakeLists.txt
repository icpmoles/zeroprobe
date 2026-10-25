# Copyright (c) 2023 Nordic Semiconductor ASA
# SPDX-License-Identifier: Apache-2.0
set(BOARD "rp2040_zero/rp2040")

cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(dap)

#include(${ZEPHYR_BASE}/samples/subsys/usb/common/common.cmake)
include_directories(include)
FILE(GLOB app_sources src/*.c)
target_sources(app PRIVATE ${app_sources})
# Hook USB transfer completions to drive the activity LED
target_link_options(app PUBLIC -Wl,--wrap=usbd_class_handle_xfer)
