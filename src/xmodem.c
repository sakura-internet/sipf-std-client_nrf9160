#include <stdint.h>

#include "xmodem.h"
#include "uart_broker.h"

#include <logging/log.h>
LOG_MODULE_DECLARE(sipf);

// static uint8_t xmodem_block[132];

#define XMODEM_BLOCK_BN(b) b[1]
#define XMODEM_BLOCK_BNC(b) b[2]
#define XMODEM_BLOCK_DATA_P(b) &b[3]
#define XMODEM_BLOCK_SUM(b) b[131]

static int xmodem_block_validation(uint8_t *block, uint8_t bn)
{
    LOG_INF("latest bn: %02x, bn: %02x, bnc: %02x", bn, XMODEM_BLOCK_BN(block), XMODEM_BLOCK_BNC(block));
    // BNチェック
    if ((XMODEM_BLOCK_BN(block) + XMODEM_BLOCK_BNC(block)) != 0xff) {
        // BNとBNCが矛盾してる
        LOG_ERR("BN, BNC miss match.");
        return -1;
    }
    if (bn == XMODEM_BLOCK_BN(block)) {
        // BNが重複してる
        LOG_ERR("Dup BN: %d, %d.", bn, XMODEM_BLOCK_BN(block));
        return -2;
    }
    if ((uint8_t)(bn + 1) != XMODEM_BLOCK_BN(block)) {
        // BNが連続してない
        LOG_ERR("Skip BN: %d, %d", bn, XMODEM_BLOCK_BN(block));
        return -1;
    }

    // サムチェック
    uint8_t *d = XMODEM_BLOCK_DATA_P(block);
    uint8_t s = 0;
    for (int i = 0; i < 128; i++) {
        s = s + d[i];
    }
    if (s == XMODEM_BLOCK_SUM(block)) {
        LOG_DBG("BN: %d", XMODEM_BLOCK_BN(block));
        return XMODEM_BLOCK_BN(block);
    } else {
        // サムが一致しない
        LOG_ERR("SUM miss match. s=%02x sum=%02x", s, XMODEM_BLOCK_SUM(block));
        return -1;
    }
}

uint8_t *xmodem_data(uint8_t *block)
{
    return &block[4];
}

static int xmodemSendAck(void)
{
    // ACK送信
    if (UartBrokerPutByte(0x06) != 0) {
        // 失敗しちゃった
        return -1;
    }
    return 0;
}

static int xmodemSendNak(void)
{
    // NAKを送信
    if (UartBrokerPutByte(0x15) != 0) {
        // 失敗しちゃった
        return -1;
    }
    return 0;
}

void XmodemBegin(void)
{
    UartBrokerSetEcho(false);
}

void XmodemEnd(void)
{
    UartBrokerSetEcho(true);
}

/**
 * 受信開始
 */
int XmodemReceiveStart(void)
{
    return xmodemSendNak();
}

/**
 * 次ブロック要求
 */
int XmodemReceiveReqNextBlock(void)
{
    return xmodemSendAck();
}

/**
 * 再送要求
 */
int XmodemReceiveReqCurrentBlock(void)
{
    return xmodemSendNak();
}

/**
 * ブロック受信
 * [in/out]bn:  受信済みブロックのBlock number
 * [out]block:  受信したブロック
 * [in]time_out:受信タイムアウト
 * return:
 */
enum xmodem_recv_ret XmodemReceiveBlock(uint8_t *bn, uint8_t *block, int time_out)
{
    uint8_t b;
    int ret;

    ret = UartBrokerGetByteTm(&b, time_out);
    if (ret == -EAGAIN) {
        LOG_INF("UartBrokerGetByteTm() timeout.");
        return XMODEM_RECV_RET_RETRY;
    } else if (ret != 0) {
        LOG_ERR("UartBrokerGetByteTm() failed: %d", ret);
        return ret;
    }

    uint8_t idx_block = 0;

    switch (b) {
    case 0x01: // SOH
        // ブロック開始
        block[idx_block++] = 0x01;
        break;
    case 0x04: // EOT
        // 転送終了
        // ACK送信
        if (xmodemSendAck() != 0) {
            // 失敗しちゃった
            return XMODEM_RECV_RET_FAILED;
        }
        return XMODEM_RECV_RET_FINISHED;
    case 0x18: // CAN
        // 中断要求
        return XMODEM_RECV_RET_CANCELED;
    }

    // ブロックの残り131Byteを受信する
    for (int i = 0; i < 131; i++) {
        //キャラ間タイムアウト100[ms]で受信
        ret = UartBrokerGetByteTm(&b, 100);
        if (ret == 0) {
            // 受信できた
            block[idx_block++] = b;
        } else {
            // 失敗
            LOG_ERR("UartBrokerGetByteTm() failed: %d", ret);
            return ret;
        }
    }

    // ブロックの正当性チェック
    int bn_recv = xmodem_block_validation(block, *bn);
    if (bn_recv == -1) {
        // チェックサムとか間違ってたから再送要求
        return XMODEM_RECV_RET_RETRY;
    } else if (bn_recv == -2) {
        // 重複してた
        return XMODEM_RECV_RET_DUP;
    }

    // ブロックが正しい
    *bn = bn_recv; // BNを更新
    return XMODEM_RECV_RET_OK;
}

/**
 * 受信をやめる
 */
int XmodemReceiveCancel(void)
{
    // CANを送信
    if (UartBrokerPutByte(0x18) != 0) {
        // 送信失敗
        return -1;
    }
    return 0;
}
