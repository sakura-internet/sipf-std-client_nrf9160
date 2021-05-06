#include <stdint.h>

#include <zephyr.h>
#include <drivers/uart.h>
#include "uart_broker.h"

#define PRIORITY (7)
#define STACK_UB_SZ (1024)

static uint8_t tx_buff[UART_TX_BUF_SZ];
static uint8_t rx_buff[UART_RX_BUF_SZ];

static struct k_msgq msgq_tx, msgq_rx;

K_THREAD_STACK_DEFINE(stack_ub, STACK_UB_SZ);
static struct k_thread thread_ub;
static k_tid_t tid_ub;

static void uart_broker_thread(void *dev, void *arg2, void *arg3)
{
    const struct device *uart = (struct device*)dev;
    int ret;
    uint8_t b;

    for (;;) {
        //RX
        while(uart_poll_in(uart, &b) == 0) {
            // UARTでなにか受けたら受信キューに突っ込む
            k_msgq_put(&msgq_rx, &b, K_NO_WAIT);    //TODO: キューに突っ込めなぁ�きにちも�としなぁ�な
        }
        //TX
        while(k_msgq_get(&msgq_tx, &b, K_MSEC(1)) == 0) {
            // 送信キューになにか入ってた
            uart_poll_out(uart, b);
        }
    }
}


/** Interface **/
int UartBrokerPutByte(uint8_t byte)
{
    return k_msgq_put(&msgq_tx, &byte, K_NO_WAIT);;
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

int UartBrokerPrintf(const char *fmt, ...)
{
    static char msg[128];
    va_list va;
    
    va_start(va, fmt);
    int len = sprintf(msg, fmt, va);
    va_end(va);
    return UartBrokerPut((uint8_t*)msg, len);
}

int UartBrokerGetByte(uint8_t *byte)
{
    return k_msgq_get(&msgq_rx, byte, K_MSEC(1));
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
    tid_ub = k_thread_create(
        &thread_ub, stack_ub, 
        STACK_UB_SZ, uart_broker_thread, (void*)uart, NULL, NULL, 
        PRIORITY, 0, K_NO_WAIT
    );
    k_thread_name_set(tid_ub, "uart broker");
    k_thread_start(&thread_ub);

    return 0;
}

int UartBrokerTerm(void)
{
    return 0;
}

