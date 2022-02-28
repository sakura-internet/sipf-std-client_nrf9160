/*
 * Copyright (c) 2021 SAKURA internet Inc.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef _UART_BROKER_H_
#define _UART_BROKER_H_

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

#include <drivers/uart.h>

#define UART_LABEL DT_LABEL(DT_NODELABEL(uart0))

#define UART_TX_BUF_SZ (256)
#define UART_RX_BUF_SZ (256)

int UartBrokerInit(const struct device *uart);
int UartBrokerTerm(void);
bool UartBrokerSetEcho(bool echo);

int UartBrokerPutByte(uint8_t byte);
int UartBrokerPut(uint8_t *data, int len);
int UartBrokerGetByte(uint8_t *byte);
int UartBrokerGetByteTm(uint8_t *byte, int timeout_ms);
int UartBrokerGet(uint8_t *data, int len);

int UartBrokerPuts(const char *msg);

void UartBrokerClearRecveiveQueue(void);

#define UartBrokerPrint(...)                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           \
    {                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                  \
        char msg[140];                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                 \
        int len = sprintf(msg, __VA_ARGS__);                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                           \
        UartBrokerPut((uint8_t *)msg, len);                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                            \
    }

#endif