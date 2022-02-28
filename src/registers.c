
/*
 * Copyright (c) 2021 SAKURA internet Inc.
 *
 * SPDX-License-Identifier: MIT
 */
#include "registers.h"

typedef int (*reset_reg)();
typedef int (*write_reg)(const uint8_t addr, const uint8_t value);
typedef int (*read_reg)(const uint8_t addr, uint8_t *value);
typedef struct
{
    reset_reg reset;
    write_reg write;
    read_reg read;
} RegistersBankFuncs;

uint8_t reg_common[] = {
    FW_TYPE, VER_MJR, VER_MNR, VER_REL & 0xff, VER_REL >> 8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00 // REG_BANK
};

/* BANK00 */
uint8_t bank00[240];
static int bank00_reset(void)
{
    memset(bank00, 0, sizeof(bank00));
    return 0;
}
static int bank00_write(const uint8_t addr, const uint8_t value)
{
    if (addr >= 0xf0) {
        //共通レジスタに書こうとした
        return -1;
    }
    bank00[addr] = value;
    return value;
}
static int bank00_read(const uint8_t addr, uint8_t *value)
{
    if (addr >= 0xf0) {
        //共通レジスタを読もうとした
        return -1;
    }
    *value = bank00[addr];
    return *value;
}
/**/

/**
 * バンクリスト
 */
static RegistersBankFuncs bank_func[] = {{bank00_reset, bank00_write, bank00_read}, {NULL, NULL, NULL}};

/* 共通レジスタ */
/**
 * 共通レジスタ書き込み
 */
static int reg_common_write(const uint8_t addr, const uint8_t value)
{
    if (addr != 0xf0) {
        // バンク選択以外は読み取り専用
        return -1;
    }

    // バンク選択
    int banks = sizeof(bank_func) / sizeof(RegistersBankFuncs);
    if ((value + 1) > banks) {
        // 選択不能なバンクを選択しようとした
        return -1;
    }
    if (bank_func[value].read == NULL) {
        // 選択不能なバンクを選択しようとした
        return -1;
    }
    *REG_CMN_BANK = value;
    return value;
}
/**
 * 共通レジスタ読み出し
 */
static int reg_common_read(const uint8_t addr, uint8_t *value)
{
    if (addr < 0xf0) {
        return -1;
    }
    *value = reg_common[addr - 0xf0];
    return *value;
}
/**/

/**
 * レジスタの初期化
 */
int RegistersReset(void)
{
    for (int i = 0; i < (sizeof(bank_func) / sizeof(RegistersBankFuncs)); i++) {
        if (bank_func[i].reset) {
            bank_func[i].reset();
        }
    }
    return 0;
}

/**
 * レジスタ書き込み
 */
int RegistersWrite(const uint8_t addr, const uint8_t value)
{
    if (addr >= 0xf0) {
        return reg_common_write(addr, value);
    } else {
        return bank_func[*REG_CMN_BANK].write(addr, value);
    }
}

/**
 * レジスタ読み込み
 */
int RegistersRead(const uint8_t addr, uint8_t *value)
{
    if (addr >= 0xf0) {
        return reg_common_read(addr, value);
    } else {
        return bank_func[*REG_CMN_BANK].read(addr, value);
    }
}
