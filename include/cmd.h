/*
 * Copyright (c) 2021 SAKURA internet Inc.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdint.h>
#define CMD_BUFF_SZ 4096

typedef enum { CMD_STATE_WAIT = 0, CMD_STATE_BUFFERING_ASCII, CMD_STATE_BUFFERING_BIN, _CMD_STATE_CNT_ } CmdState;

typedef struct
{
    uint8_t *response;
    uint16_t response_len;
} CmdResponse;

CmdResponse *CmdParse(uint8_t b);
