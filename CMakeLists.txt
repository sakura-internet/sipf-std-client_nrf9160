#
# Copyright (c) 2020 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
#

cmake_minimum_required(VERSION 3.13.1)

set(BOARD_ROOT ${CMAKE_SOURCE_DIR})
set(DTC_OVERLAY_FILE "${CMAKE_CURRENT_SOURCE_DIR}/dts.overlay")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(sipf-nrf91-std)

target_sources(app PRIVATE
    src/main.c
    src/cmd.c
    src/cmd_ascii.c
    src/registers.c
    src/uart_broker.c
    src/sipf/sipf_client_http.c
    src/fota/fota_http.c
    src/gnss/gnss.c
)

target_include_directories(app PRIVATE
    include/
)
