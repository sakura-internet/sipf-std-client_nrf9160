/*
 * Copyright (c) 2021 SAKURA internet Inc.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef _REGISTERS_H_
#define _REGISTERS_H_

#include "version.h"
#include <stdint.h>
#include <string.h>

extern uint8_t bank00[240];
#define REG_00_MODE (uint8_t *)&bank00[0x00]
#define REG_00_UN_LEN (uint8_t *)&bank00[0x10]
#define REG_00_USER_NAME (char *)&bank00[0x20]
#define REG_00_PW_LEN (uint8_t *)&bank00[0x80]
#define REG_00_PASSWORD (char *)&bank00[0x90]

extern uint8_t reg_common[16];
#define REG_CMN_FW_TYPE (uint8_t *)&reg_common[0x0]
#define REG_CMN_VER_MJR (uint8_t *)&reg_common[0x1]
#define REG_CMN_VER_MNR (uint8_t *)&reg_common[0x2]
#define REG_CMN_VER_REL (uint16_t *)&reg_common[0x3]
#define REG_CMN_BANK (uint8_t *)&reg_common[0xf]

int RegistersReset(void);
int RegistersWrite(const uint8_t addr, const uint8_t value);
int RegistersRead(const uint8_t addr, uint8_t *value);

#endif