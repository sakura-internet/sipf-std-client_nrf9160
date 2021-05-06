#ifndef DEBUG_PRINT_H
#define DEBUG_PRINT_H

#define DEBUG_PRINT

#ifdef DEBUG_PRINT
#include "uart_broker.h"
#define PRINT UartBrokerPrintf
#define DBG "DBG:"
#define WARN "WARN:"
#define ERR "ERR:"

#define DebugPrint(...) PRINT(__VA_ARGS__)
#define DBG_FUNCNAME() PRINT(DBG "Run: %s()\r\n", __func__)
#else
#define DebugPrint(...)
#define DBG_FUNCNAME()
#endif

#endif