/*
 * Copyright (c) 2021 SAKURA internet Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <zephyr.h>
#include <drivers/uart.h>

#include "uart_broker.h"

#define PRIORITY (7)
#define STACK_UB_SZ (1024)

static uint8_t tx_buff[UART_TX_BUF_SZ];
static uint8_t rx_buff[UART_RX_BUF_SZ];

static bool is_echo = true;
K_MUTEX_DEFINE(mutex_is_echo);

static struct k_msgq msgq_tx, msgq_rx;

K_THREAD_STACK_DEFINE(stack_ub, STACK_UB_SZ);
static struct k_thread thread_ub;
static k_tid_t tid_ub;

static void uart_broker_thread(void *dev, void *arg2, void *arg3)
{
    const struct device *uart = (struct device *)dev;
    uint8_t b;
    bool e;
    for (;;) {
        // RX
        while (uart_poll_in(uart, &b) == 0) {
            // UARTでなにか受けたら受信キューに突っ込む
            k_msgq_put(&msgq_rx, &b, K_NO_WAIT); // TODO: キューに突っ込めないときどうするか

            k_mutex_lock(&mutex_is_echo, K_FOREVER);
            e = is_echo;
            k_mutex_unlock(&mutex_is_echo);
            if (e) {
                uart_poll_out(uart, b); // ECHO BACK
            }
        }
        // TX
        while (k_msgq_get(&msgq_tx, &b, K_USEC(10)) == 0) {
            // 送信キューになにか入ってた
            uart_poll_out(uart, b);
        }
    }
}

/** Interface **/
int UartBrokerPutByte(uint8_t byte)
{
    return k_msgq_put(&msgq_tx, &byte, K_MSEC(10));
}

int UartBrokerPut(uint8_t *data, int len)
{
    int cnt = 0;
    int ret;
    for (int i = 0; i < len; i++) {
        ret = UartBrokerPutByte(data[i]);
        if (ret != 0) {
            break;
        }
        cnt++;
    }
    return cnt;
}

int UartBrokerPuts(const char *msg)
{
    return UartBrokerPut((uint8_t *)msg, strlen(msg));
}

int UartBrokerGetByte(uint8_t *byte)
{
    return k_msgq_get(&msgq_rx, byte, K_MSEC(1));
}

int UartBrokerGetByteTm(uint8_t *byte, int timeout_ms)
{
    return k_msgq_get(&msgq_rx, byte, K_MSEC(timeout_ms));
}

int UartBrokerGet(uint8_t *data, int len)
{
    int cnt = 0;
    int ret;
    for (int i = 0; i < len; i++) {
        ret = UartBrokerGetByte(&data[i]);
        if (ret != 0) {
            break;
        }
        cnt++;
    }
    return cnt;
}

int UartBrokerInit(const struct device *uart)
{
    // 送受信キュー作成
    k_msgq_init(&msgq_tx, tx_buff, 1, UART_TX_BUF_SZ);
    k_msgq_init(&msgq_rx, rx_buff, 1, UART_RX_BUF_SZ);

    // スレッド作成
    tid_ub = k_thread_create(&thread_ub, stack_ub, STACK_UB_SZ, uart_broker_thread, (void *)uart, NULL, NULL, PRIORITY, 0, K_NO_WAIT);
    k_thread_name_set(tid_ub, "uart broker");
    k_thread_start(&thread_ub);

    return 0;
}

int UartBrokerTerm(void)
{
    return 0;
}

bool UartBrokerSetEcho(bool echo)
{
    k_mutex_lock(&mutex_is_echo, K_FOREVER);
    is_echo = echo;
    k_mutex_unlock(&mutex_is_echo);
    return echo;
}
