/*
 * Copyright (c) 2022 SAKURA internet Inc.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef _XMODEM_H_
#define _XMODEM_H_

#define XMODEM_SZ_BLOCK (128)

typedef enum xmodem_recv_ret {
    XMODEM_RECV_RET_OK,
    XMODEM_RECV_RET_FINISHED,
    XMODEM_RECV_RET_CANCELED,
    XMODEM_RECV_RET_RETRY,
    XMODEM_RECV_RET_DUP,
    XMODEM_RECV_RET_FAILED = -1,
    XMODEM_RECV_RET_TIMEOUT = -2,
} XmodemRecvRet;

typedef enum xmodem_send_ret {
    XMODEM_SEND_RET_OK,
    XMODEM_SEND_RET_RETRY,
    XMODEM_SEND_RET_CANCELED,
    XMODEM_SEND_RET_FAILED = -1,
    XMODEM_SEND_RET_TIMEOUT = -2,
} XmodemSendRet;

void XmodemBegin(void);
void XmodemEnd(void);

int XmodemTransmitCancel(void);

int XmodemReceiveStart(void);
XmodemRecvRet XmodemReceiveBlock(uint8_t *bn, uint8_t *block, int time_out);
int XmodemReceiveReqNextBlock(void);
int XmodemReceiveReqCurrentBlock(void);

XmodemSendRet XmodemSendWaitRequest(int time_out);
XmodemSendRet XmodemSendEnd(int time_out);
XmodemSendRet XmodemSendBlock(uint8_t *bn, uint8_t *payload, int sz_payload, int time_out);
#endif