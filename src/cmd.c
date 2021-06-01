/*
 * Copyright (c) 2021 Sakura Internet Inc.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>
#include "cmd.h"
#include "cmd_ascii.h"
#include "debug_print.h"

static CmdState state = CMD_STATE_WAIT;
static uint8_t in_buff[CMD_BUFF_SZ];
static int in_buff_idx;
static uint8_t out_buff[CMD_BUFF_SZ];
static int out_len;

static CmdResponse cmdres;

typedef CmdResponse* (*state_func)(uint8_t b);

static CmdResponse* stateWait(uint8_t b)
{
    if (b == (uint8_t)'$') {
        memset(in_buff, 0, CMD_BUFF_SZ);
        in_buff_idx = 0;
        state = CMD_STATE_BUFFERING_ASCII;
    }
    return NULL;
}

static CmdResponse* stateBufferingAscii(uint8_t b)
{
    if ((b == 0x0a) || (b == 0x0d)) {
        state = CMD_STATE_WAIT;
        // 改行がきたらコマンド終端
        out_len = CmdAsciiParse(in_buff, in_buff_idx, out_buff, sizeof(out_buff));
        if (out_len > 0) {
            cmdres.response = out_buff;
            cmdres.response_len = out_len;
            return &cmdres; // コマンドの応答を返す
        } else {
            // out_lenが1byteもない=返すべき応答が無い
            return NULL;
        }
    } else if (b == 0x08/*BS*/) {
        in_buff_idx--;
    } else {
        in_buff[in_buff_idx++] = b;
    }
    return NULL;
}


static state_func bufunc[] = {
    stateWait,              /* STATE_WAIT */
    stateBufferingAscii,    /* STATE_BUFFERING_ASCII */
    NULL,                   /* STATE_BUFFERING_BIN */
    NULL,                   /* (terminator) */
};

CmdResponse* CmdParse(uint8_t b)
{
    CmdResponse *cr = NULL;
    //DebugPrint("b: 0x%02x, state:%d\r\n", b, state);
    if (bufunc[state] != NULL) {
        cr = bufunc[state](b);
    }
    return cr;
}