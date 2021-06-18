/*
 * Copyright (c) 2021 Sakura Internet Inc.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef DEBUG_PRINT_H
#define DEBUG_PRINT_H

//#define DEBUG_PRINT

#ifdef DEBUG_PRINT
#include "uart_broker.h"
#define DBG "DBG:"
#define INFO "INFO:"
#define WARN "WARN:"
#define ERR "ERR:"

//#define DebugPrint(...) PRINT(__VA_ARGS__)
#define DebugPrint(...)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                \
  {                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                    \
    char dp_msg[256];                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  \
    int dp_len = sprintf(dp_msg, __VA_ARGS__);                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                         \
    UartBrokerPut((uint8_t *)dp_msg, dp_len);                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          \
  }
#else
#define DebugPrint(...)
#define DBG
#define INFO
#define WARN
#define ERR
#endif

#endif