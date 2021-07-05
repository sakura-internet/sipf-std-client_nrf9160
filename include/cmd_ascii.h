/*
 * Copyright (c) 2021 Sakura Internet Inc.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef CMD_ASCII_H
#define CMD_ASCII_H

#include <stdint.h>

#define CMD_REG_W "W"
#define CMD_REG_R "R"
#define CMD_RADIX "RADIX"
#define CMD_REG_MW "MW"
#define CMD_REG_MR "MR"

#define CMD_TX "$TX"
#define CMD_RX "$RX"
#define CMD_PUSH "$PUSH"
#define CMD_PULL "$PULL"
#define CMD_BCLR "$BCLR"
#define CMD_MTX "$MTX"
#define CMD_MRX "$MRX"
#define CMD_MPUSH "$MPUSH"
#define CMD_MPULL "$MPULL"
#define CMD_EXRADIX "$EXRDIX"
#define CMD_UNLOCK "$UNLOCK"
#define CMD_UPDATE "$UPDATE"

#define CMD_GNSS_ENABLE "$GNSSEN"
#define CMD_GNSS_GET_STATUS "$GNSSSTAT"
#define CMD_GNSS_GET_LOCATION "$GNSSLOC"
#define CMD_GNSS_GET_NMEA "$GNSSNMEA"

#define CMD_RES_OK (0)
#define CMD_RES_ILLPARM (-1)
#define CMD_RES_CMDFAIL (-2)
#define CMD_RES_LOCKED (-3)

int CmdAsciiParse(uint8_t *in_buff, uint16_t in_buff_len, uint8_t *out_buff, uint16_t out_buff_len);

#endif