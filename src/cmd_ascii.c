/*
 * Copyright (c) 2021 SAKURA internet Inc.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include <zephyr.h>
#include <logging/log.h>

LOG_MODULE_DECLARE(sipf);

#include "cmd_ascii.h"
#include "registers.h"
#include "xmodem.h"
#include "fota/fota_http.h"
#include "sipf/sipf_client_http.h"
#include "sipf/sipf_file.h"
#include "gnss/gnss.h"

static uint8_t buff_work[256];
/**/
typedef int (*ascii_cmd_func)(uint8_t *in_buff, uint16_t in_len, uint8_t *out_buff, uint16_t out_buff_len);
typedef struct
{
    char *cmd_name;
    ascii_cmd_func cmd_func;
} CmdAsciiCmd;
/**/

static bool is_unlocked = false;

/**
 * 2桁の16進文字列をUINT8に変換する
 */
static uint8_t hexToUint8(uint8_t *pchars, uint8_t *err)
{
    uint8_t ret = 0;
    if (err) {
        *err = 0;
    }

    for (int i = 0; i < 2; i++) {
        if ((pchars[i] >= (uint8_t)'0') && (pchars[i] <= (uint8_t)'9')) {
            ret |= (pchars[i] - (uint8_t)'0') << (i == 0 ? 4 : 0);
        } else if ((pchars[i] >= (uint8_t)'a') && (pchars[i] <= (uint8_t)'f')) {
            ret |= (pchars[i] - (uint8_t)'a' + 10) << (i == 0 ? 4 : 0);
        } else if ((pchars[i] >= (uint8_t)'A') && (pchars[i] <= (uint8_t)'F')) {
            ret |= (pchars[i] - (uint8_t)'A' + 10) << (i == 0 ? 4 : 0);
        } else {
            if (err) {
                ret = 0;
                *err = 0xff;
            }
        }
    }
    return ret;
}

static int cmdCreateResIllParam(uint8_t *out_buff, uint16_t out_buff_len)
{
    return snprintf(out_buff, out_buff_len, "ILLIGAL PARAMETER\r\nNG\r\n");
}

static int cmdCreateResOk(uint8_t *out_buff, uint16_t out_buff_len)
{
    return snprintf(out_buff, out_buff_len, "OK\r\n");
}

static int cmdCreateResNg(uint8_t *out_buff, uint16_t out_buff_len)
{
    return snprintf(out_buff, out_buff_len, "NG\r\n");
}

static int cmdAsciiCmdW(uint8_t *in_buff, uint16_t in_len, uint8_t *out_buff, uint16_t out_buff_len)
{
    is_unlocked = false;

    if (in_len != 6) {
        // lengthが合わない
        return cmdCreateResIllParam(out_buff, out_buff_len);
    }

    if (in_buff[3] != 0x20) {
        // ADDRとVALUEの区切りがスペースじゃない
        return cmdCreateResIllParam(out_buff, out_buff_len);
    }

    if (in_buff[0] != 0x20) {
        // 先頭がスペースじゃない
        return CMD_RES_ILLPARM;
    }

    //文字列として処理できるように区切りをNull文字にする
    in_buff[3] = 0x00;
    in_buff[6] = 0x00;

    char *endptr;
    uint8_t addr, val;

    addr = strtol((char *)&in_buff[1], &endptr, 16);
    if (*endptr != '\0') {
        // Null文字以外で変換が終わってる
        return cmdCreateResIllParam(out_buff, out_buff_len);
    }

    val = strtol((char *)&in_buff[4], &endptr, 16);
    if (*endptr != '\0') {
        // Null文字以外で変換が終わってる
        return cmdCreateResIllParam(out_buff, out_buff_len);
    }

    if (RegistersWrite(addr, val) < 0) {
        // 書き込みエラー
        return cmdCreateResNg(out_buff, out_buff_len);
    }

    return cmdCreateResOk(out_buff, out_buff_len);
}

static int cmdAsciiCmdR(uint8_t *in_buff, uint16_t in_len, uint8_t *out_buff, uint16_t out_buff_len)
{
    is_unlocked = false;
    if (in_len != 3) {
        // lengthが合わない
        return cmdCreateResIllParam(out_buff, out_buff_len);
    }
    if (in_buff[0] != 0x20) {
        // 先頭がスペースじゃない
        return cmdCreateResIllParam(out_buff, out_buff_len);
    }
    //文字列として処理できるように区切りをNull文字にする
    in_buff[3] = 0x00;

    char *endptr;
    uint8_t addr, val;

    addr = strtol((char *)&in_buff[1], &endptr, 16);
    if (*endptr != '\0') {
        // Null文字以外で変換が終わってる
        return cmdCreateResIllParam(out_buff, out_buff_len);
    }

    if (RegistersRead(addr, &val) < 0) {
        // 読み出しエラー
        return cmdCreateResIllParam(out_buff, out_buff_len);
    }

    return sprintf(out_buff, "%02X\r\nOK\r\n", val);
}

static int checkTypeLen(uint8_t type_id, uint16_t len)
{
    switch (type_id) {
    case OBJ_TYPE_UINT8:
    case OBJ_TYPE_INT8:
        if (len != 1) {
            return false;
        }
        break;
    case OBJ_TYPE_UINT16:
    case OBJ_TYPE_INT16:
        if (len != 2) {
            return false;
        }
        break;
    case OBJ_TYPE_UINT32:
    case OBJ_TYPE_INT32:
    case OBJ_TYPE_FLOAT32:
        if (len != 4) {
            return false;
        }
        break;
    case OBJ_TYPE_UINT64:
    case OBJ_TYPE_INT64:
    case OBJ_TYPE_FLOAT64:
        if (len != 8) {
            return false;
        }
        break;
    case OBJ_TYPE_BIN_BASE64:
    case OBJ_TYPE_STR_UTF8:
        if (len > 1000) {
            return false;
        }
        break;
    default:
        // TYPEが範囲外
        return false;
    }
    return true;
}

/**
 * OBJECT送信
 */
static uint8_t tx_buff[1024];
static int cmdAsciiCmdTx(uint8_t *in_buff, uint16_t in_len, uint8_t *out_buff, uint16_t out_buff_len)
{
    enum
    {
        ST_BEGIN,
        ST_TAG_ID,
        ST_TYPE,
        ST_VALUE,
        ST_END
    } parse_state = ST_BEGIN;

    uint16_t idx_in_buff = 0;
    uint16_t idx_tx_buff = 0;

    if (in_len < 8) {
        // valueまでの長さが無い
        return cmdCreateResIllParam(out_buff, out_buff_len);
    }

    memset(tx_buff, 0, sizeof(tx_buff));

    uint8_t tag_id, type_id;
    uint8_t value_len = 0;
    uint16_t idx_value_len;
    uint8_t err;
    SipfObjectOtid otid;
    int len = 0;
    for (;;) {
        switch (parse_state) {
        case ST_BEGIN:
            if (in_buff[idx_in_buff] == ' ') {
                idx_in_buff++;
                parse_state = ST_TAG_ID; // TAG_IDのパースへ遷移
            } else {
                LOG_ERR("BEGIN: Invalid separater");
                return cmdCreateResIllParam(out_buff, out_buff_len);
            }
            break;
        case ST_TAG_ID: // TAG_IDのパース
            if (in_buff[idx_in_buff + 2] == ' ') {
                in_buff[idx_in_buff + 2] = '\0';
                // TAG_IDを送信バッファにセット
                tag_id = hexToUint8(&in_buff[idx_in_buff], &err);
                LOG_INF("TAG_ID: 0x%02x", tag_id);
                if (err != 0) {
                    // 変換失敗
                    LOG_ERR("TAG_ID: parse failed...");
                    return cmdCreateResIllParam(out_buff, out_buff_len);
                }
                idx_in_buff += 3;      // 入力バッファのインデックスを進める
                parse_state = ST_TYPE; // TYPEのパースへ遷移
            } else {
                LOG_ERR("TAG_ID: Invalid separater");
                return cmdCreateResIllParam(out_buff, out_buff_len);
            }
            break;
        case ST_TYPE: // TYPEのパース
            if (in_buff[idx_in_buff + 2] == ' ') {
                in_buff[idx_in_buff + 2] = '\0';
                // TYPE_IDを送信バッファにセット
                type_id = hexToUint8(&in_buff[idx_in_buff], &err);
                LOG_INF("TYPE: 0x%02x", type_id);
                if (err != 0) {
                    // 変換失敗
                    LOG_ERR("TYPE: parse failed...");
                    return cmdCreateResIllParam(out_buff, out_buff_len);
                }
                idx_in_buff += 3; // 入力バッファのインデックスを進める
                // 送信バッファに積んでVALUEのパースへ
                tx_buff[idx_tx_buff++] = type_id;
                tx_buff[idx_tx_buff++] = tag_id;
                idx_value_len = idx_tx_buff++;
                value_len = 0;
                parse_state = ST_VALUE; // VALUEのパースへ遷移
            } else {
                LOG_ERR("TYPE: Invalid separater");
                return cmdCreateResIllParam(out_buff, out_buff_len);
            }
            break;
        case ST_VALUE:                   // Valueのパース
            if (idx_in_buff >= in_len) { // 入力の末尾へ到達
                // TYPEとデータ長が矛盾してたらエラー
                if (checkTypeLen(type_id, value_len) == false) {
                    LOG_ERR("VALUE: Value length missmatch...");
                    return cmdCreateResIllParam(out_buff, out_buff_len);
                }
                // パース終了
                LOG_INF("VALUE_LEN: %d", value_len);
                tx_buff[idx_value_len] = value_len;
                parse_state = ST_END;
                break;
            }
            if (in_buff[idx_in_buff] == ' ') { // スペースだったら
                // TYPEとデータ長が矛盾してたらエラー
                if (checkTypeLen(type_id, value_len) == false) {
                    LOG_ERR("VALUE: Value length missmatch...");
                    return cmdCreateResIllParam(out_buff, out_buff_len);
                }
                // 次のオブジェクトのパースへ
                LOG_INF("VALUE_LEN: %d", value_len);
                tx_buff[idx_value_len] = value_len;
                idx_in_buff++;
                parse_state = ST_TAG_ID;
                break;
            }
            // 2文字ずつ読んでByteに変換
            tx_buff[idx_tx_buff++] = hexToUint8(&in_buff[idx_in_buff], &err);
            if (err != 0) {
                LOG_ERR("VALUE: parse failed...");
                return cmdCreateResIllParam(out_buff, out_buff_len);
            }
            value_len++;
            idx_in_buff += 2;
            break;
        case ST_END:
            goto parse_end;
            break;
        default:
            // 想定外のステート
            return cmdCreateResNg(out_buff, out_buff_len);
        }
    }

parse_end:

    // SIPF_OBJ_UP送信
    err = SipfObjClientObjUpRaw(tx_buff, idx_tx_buff, &otid);
    len = 0;
    if (err == 0) {
        // OTID取得
        for (int i = 0; i < sizeof(otid.value); i++) {
            len += sprintf(&out_buff[i * 2], "%02X", otid.value[i]);
        }
        len += sprintf(&out_buff[len], "\r\nOK\r\n");
    } else {
        LOG_ERR("SipfClientObjUpRaw() failed: %d", err);
        return cmdCreateResNg(out_buff, out_buff_len);
    }
    return len;
}

static int cmdAsciiCmdTxRaw(uint8_t *in_buff, uint16_t in_len, uint8_t *out_buff, uint16_t out_buff_len)
{
    // $TXRAW XX YYYYYYYYYY..
    is_unlocked = false;

    if (in_len < 5) {
        // valueまでの長さが無い
        return cmdCreateResIllParam(out_buff, out_buff_len);
    }

    if (in_buff[0] != 0x20 || in_buff[3] != 0x20) {
        // 先頭がスペースじゃない or SizeとValueの間がスペースじゃない
        return cmdCreateResIllParam(out_buff, out_buff_len);
    }

    in_buff[0] = '\0';
    in_buff[3] = '\0';

    uint8_t size;
    uint8_t buffer[256];
    char *endptr;

    size = strtol((char *)&in_buff[1], &endptr, 16);
    if (*endptr != '\0') {
        // Null文字以外で変換が終わってる
        return cmdCreateResIllParam(out_buff, out_buff_len);
    }

    LOG_DBG("TXRAW Size:%d", size);

    uint8_t *head = &in_buff[4];
    int i;
    for (i = 0; i < size; i++) {
        if (!isxdigit(*head) || !isxdigit(*(head + 1))) {
            LOG_WRN("Invalid charctor");
            return cmdCreateResIllParam(out_buff, out_buff_len);
        }

        uint8_t hexBuff[3] = {*head, *(head + 1), 0x00};
        uint8_t value = strtol((char *)hexBuff, &endptr, 16);
        if (*endptr != '\0') {
            // Null文字以外で変換が終わってる
            LOG_WRN("Convert error");
            return cmdCreateResIllParam(out_buff, out_buff_len);
        }
        buffer[i] = value;
        head += 2;
    }

    if (*head != '\0' && *head != '\r' && *head != '\n') {
        // 文字があまっている
        LOG_WRN("Invalid length");
        return cmdCreateResIllParam(out_buff, out_buff_len);
    }

    SipfObjectOtid otid;
    uint8_t err = SipfObjClientObjUpRaw(buffer, size, &otid);
    int len = 0;
    if (err == 0) {
        for (int i = 0; i < sizeof(otid.value); i++) {
            len += sprintf(&out_buff[i * 2], "%02X", otid.value[i]);
        }
        len += sprintf(&out_buff[len], "\r\nOK\r\n");
    } else {
        LOG_ERR("SipfClientObjUpRaw() failed: %d", err);
        return cmdCreateResNg(out_buff, out_buff_len);
    }
    return len;
}

/**
 * $$RXコマンド
 * パラメータなし
 */
static int cmdAsciiCmdRx(uint8_t *in_buff, uint16_t in_len, uint8_t *out_buff, uint16_t out_buff_len)
{
    if (in_len != 0) {
        // なにかパラメータが指定されてる。
        return cmdCreateResIllParam(out_buff, out_buff_len);
    }

    uint8_t err;
    SipfObjectOtid otid;
    uint8_t remains, objqty;
    uint8_t *p_snd_datetime, *p_rcv_datetime;
    static uint8_t *p_objs[OBJ_MAX_CNT];

    err = SipfObjClientObjDown(&otid, &remains, &objqty, p_objs, &p_snd_datetime, &p_rcv_datetime);

    if (err != 0) {
        LOG_ERR("SipfObjClientObjDown():%d", err);
        return cmdCreateResNg(out_buff, out_buff_len);
    }

    LOG_INF("remains=%d, objqty=%d", remains, objqty);

    if (objqty == 0) {
        LOG_INF("EMPTY");
        return cmdCreateResOk(out_buff, out_buff_len);
    }

    int idx = 0;
    // OTID
    for (int i = 0; i < 16; i++) {
        idx += sprintf(&out_buff[idx], "%02X", otid.value[i]);
    }
    idx += sprintf(&out_buff[idx], "\r\n");
    // USER_SEND_DATETIME_MS
    for (int i = 0; i < 8; i++) {
        idx += sprintf(&out_buff[idx], "%02X", p_snd_datetime[i]);
    }
    idx += sprintf(&out_buff[idx], "\r\n");
    // RECEIVE_DATETIME_MS
    for (int i = 0; i < 8; i++) {
        idx += sprintf(&out_buff[idx], "%02X", p_rcv_datetime[i]);
    }
    idx += sprintf(&out_buff[idx], "\r\n");
    // REMAINS
    idx += sprintf(&out_buff[idx], "%02X\r\n", remains);
    // OBJQTY
    idx += sprintf(&out_buff[idx], "%02X\r\n", objqty);
    // OBJECTS
    for (int i = 0; i < objqty; i++) {
        SipfObjectObject obj;
        obj.value = buff_work;
        if (SipfObjectParse(p_objs[i], sizeof(buff_work), &obj) != 0) {
            LOG_ERR("SipfObjectParse() failed...");
            return cmdCreateResNg(out_buff, out_buff_len);
        }
        idx += sprintf(&out_buff[idx], "%02X %02X %02X ", obj.obj_tagid, obj.obj_type, obj.value_len);
        for (int j = 0; j < obj.value_len; j++) {
            idx += sprintf(&out_buff[idx], "%02X", obj.value[j]);
        }
        idx += sprintf(&out_buff[idx], "\r\n");
    }
    idx += sprintf(&out_buff[idx], "OK\r\n");

    return idx;
}

/*** ファイル送受信コマンド ***/
static int cmdFputOkRes(int file_size, uint8_t *out_buff, int out_buff_len)
{
    return sprintf(out_buff, "%08X\r\nOK\r\n", file_size);
}

static int cmdFgetOkRes(int file_size, uint8_t *out_buff, int out_buff_len)
{
    return sprintf(out_buff, "\r\n%08X\r\nOK\r\n", file_size);
}

static int cmdFgetNgRes(uint8_t *out_buff, int out_buff_len)
{
    int len = sprintf(out_buff, "\r\nNG\r\n");
    return len;
}

/**
 * $$FPUTコマンド
 */
static uint8_t xmodem_block[132];
static uint32_t sz_fput_file;

static int sendChunkedData(int sock, uint8_t *chunk)
{
    int len = 0, ret;

    LOG_HEXDUMP_INF(chunk, 128, "chunk:");

    ret = send(sock, &xmodem_block[3], 128, 0);
    if (ret < 0) {
        LOG_ERR("send[1]() failed: %d", errno);
        return -errno;
    }
    len += ret;
    return len;
}

static int sendChunkedEnd(int sock)
{
    return 0;
}

static int cmdFputSendCb(int sock, struct http_request *req, void *user_data)
{
    int ret;
    int total_sent = 0;
    uint8_t bn = 1;

    //最初のブロックを送る
    ret = sendChunkedData(sock, &xmodem_block[3]);
    if (ret < 0) {
        LOG_ERR("sendChunkedData() failed: %d", ret);
        return -ret;
    }
    total_sent += ret;
    XmodemReceiveReqNextBlock();

    enum xmodem_recv_ret xret;
    uint8_t *chunk;
    int cnt_retry = 0;
    for (;;) {
        xret = XmodemReceiveBlock(&bn, xmodem_block, 1000);
        if (xret == XMODEM_RECV_RET_OK) {
            chunk = &xmodem_block[3];
            // 送信するよ
            ret = sendChunkedData(sock, chunk);
            if (ret < 0) {
                LOG_ERR("sendChunkedData() failed: %d", ret);
                XmodemTransmitCancel();
                return ret;
            }
            total_sent += ret;
            // 次ブロック要求
            cnt_retry = 0;
            XmodemReceiveReqNextBlock();
        } else if (xret == XMODEM_RECV_RET_FINISHED) {
            LOG_INF("XmodemReceiveBlock() finished.");
            break;
        } else if (xret == XMODEM_RECV_RET_RETRY) {
            if (cnt_retry++ > 10) {
                LOG_ERR("XmodemReceiveBlock() retry over.");
                XmodemTransmitCancel();
                return -1;
            }
            LOG_INF("XmodemReceiveBlock() retry.");
            // 再送要求
            XmodemReceiveReqCurrentBlock();
        } else if (xret == XMODEM_RECV_RET_CANCELED) {
            LOG_INF("XmodemReceiveBlock() canceled.");
            break;
        }
    }
    ret = sendChunkedEnd(sock);
    if (ret < 0) {
        LOG_ERR("sendChunkedEnd() failed: %d", ret);
        return ret;
    }
    total_sent += ret;
    LOG_DBG("content-length: %d, total_sent: %d", sz_fput_file, total_sent);
    if (sz_fput_file > total_sent) {
        // 送信予定のファイルサイズに満たなかった
        (void)close(sock);
    }

    return total_sent;
}

static int cmdAsciiCmdFput(uint8_t *in_buff, uint16_t in_len, uint8_t *out_buff, uint16_t out_buff_len)
{
    int ret;

    if (in_buff[0] != 0x20) {
        // 先頭がスペースじゃない
        return cmdCreateResIllParam(out_buff, out_buff_len);
    }
    if (in_len < 2) {
        // file_idが空
        return cmdCreateResIllParam(out_buff, out_buff_len);
    }

    // file_idとfile_sizeの区切りを探す
    char *str_file_size = NULL;
    for (int i = 1; i < in_len; i++) {
        if (in_buff[i] == 0x20) {
            in_buff[i] = 0x00;
            str_file_size = (char *)&in_buff[i + 1];
        }
    }
    if (str_file_size == NULL) {
        // 区切りが見つからなかった
        return cmdCreateResIllParam(out_buff, out_buff_len);
    }

    // file_size
    uint8_t err;
    uint32_t file_size;
    if (strlen(str_file_size) != 8) {
        // 32bit(=4Byte)じゃない
        return cmdCreateResIllParam(out_buff, out_buff_len);
    }
    file_size = hexToUint8(&str_file_size[0], &err) << 24;
    if (err) {
        // 変換失敗
        return cmdCreateResIllParam(out_buff, out_buff_len);
    }
    file_size += hexToUint8(&str_file_size[2], &err) << 16;
    if (err) {
        // 変換失敗
        return cmdCreateResIllParam(out_buff, out_buff_len);
    }
    file_size += hexToUint8(&str_file_size[4], &err) << 8;
    if (err) {
        // 変換失敗
        return cmdCreateResIllParam(out_buff, out_buff_len);
    }
    file_size += hexToUint8(&str_file_size[6], &err);
    if (err) {
        // 変換失敗
        return cmdCreateResIllParam(out_buff, out_buff_len);
    }

    // file_id
    char *file_id = (char *)&in_buff[1];

    k_msleep(10);

    // XMODEM開始
    XmodemBegin();
    // 受信開始
    ret = XmodemReceiveStart();
    if (ret < 0) {
        LOG_ERR("XmodemReceiveStart() failed: %d", ret);
        return ret;
    }
    // 最初のレコードを受信
    enum xmodem_recv_ret xret;
    int cnt_retry = 0;
    uint8_t bn = 0;
    for (;;) {
        xret = XmodemReceiveBlock(&bn, xmodem_block, 3000);
        if (xret == XMODEM_RECV_RET_OK) {
            break;
        } else if (xret == XMODEM_RECV_RET_RETRY) {
            if (cnt_retry++ < 10) {
                LOG_INF("Retry");
                XmodemReceiveReqCurrentBlock();
            } else {
                LOG_ERR("Retry over.");
                XmodemTransmitCancel();
                XmodemEnd();
                return cmdCreateResNg(out_buff, out_buff_len);
            }
        } else {
            LOG_ERR("XmodemReceiveBlock() failed: %d", xret);
            ret = cmdCreateResNg(out_buff, out_buff_len);
            goto fput_end;
        }
    }
    sz_fput_file = file_size; // コールバック関数でfile_sizeを参照したい
    ret = SipfFileUpload(file_id, NULL, cmdFputSendCb, file_size);
    if (ret < 0) {
        LOG_ERR("SipfFileUpload() failed: %d", ret);
        ret = cmdCreateResNg(out_buff, out_buff_len);
    } else {
        ret = cmdFputOkRes(file_size, out_buff, out_buff_len);
    }
fput_end:
    XmodemEnd();

    k_msleep(10);

    return ret;
}

/**
 * $$FGETコマンド
 */
static uint8_t fget_bn;
#define FPUT_BLOCK_SEND_RETRY (3)
static int cmdFgetCb(uint8_t *buff, size_t len)
{
    XmodemSendRet xret;
    int payload_len;
    for (int idx = 0; idx < len; idx += XMODEM_SZ_BLOCK) {
        for (int i = 0; i < FPUT_BLOCK_SEND_RETRY; i++) {
            if ((len - idx) < XMODEM_SZ_BLOCK) {
                payload_len = len % XMODEM_SZ_BLOCK;
            } else {
                payload_len = XMODEM_SZ_BLOCK;
            }
            xret = XmodemSendBlock(&fget_bn, &buff[idx], payload_len, 500);
            if (xret == XMODEM_SEND_RET_FAILED) {
                LOG_ERR("XmodemSendBlock() failed.");
                return -1;
            }
            if (xret == XMODEM_SEND_RET_TIMEOUT) {
                LOG_ERR("XmodemSendBlock() timeout.");
                continue;
            }
            if (xret == XMODEM_SEND_RET_RETRY) {
                LOG_ERR("XmodemSendBlock() retry.");
                continue;
            }
            if (xret == XMODEM_SEND_RET_CANCELED) {
                LOG_INF("XmodemSendBlock() canceled.");
                return -2;
            }
            if (xret == XMODEM_SEND_RET_OK) {
                fget_bn++; // ブロック番号をインクリメント
                break;
            }
        }
    }
    return 0;
}

static int cmdAsciiCmdFget(uint8_t *in_buff, uint16_t in_len, uint8_t *out_buff, uint16_t out_buff_len)
{
    int ret;

    if (in_buff[0] != 0x20) {
        // 先頭がスペースじゃない
        return cmdCreateResIllParam(out_buff, out_buff_len);
    }
    if (in_len < 2) {
        // file_idが空
        return cmdCreateResIllParam(out_buff, out_buff_len);
    }
    // file_id
    char *file_id = (char *)&in_buff[1];

    k_msleep(10);

    // XMODEM開始
    XmodemBegin();
    // XMODEM:受信要求を待つ
    XmodemSendRet xret = XmodemSendWaitRequest(30000);
    if (xret != XMODEM_SEND_RET_OK) {
        LOG_ERR("XmodemSendWaitRequest() failed: %d", xret);
        XmodemTransmitCancel();
        ret = cmdFgetNgRes(out_buff, out_buff_len);
        goto fget_end;
    }
    fget_bn = 1; // ブロック番号を初期化

    // ファイルのダウンロードを開始
    ret = SipfFileDownload(file_id, NULL, XMODEM_SZ_BLOCK * 8, cmdFgetCb);
    if (ret < 0) {
        LOG_ERR("SipfFileDownload() failed: %d", ret);
        XmodemTransmitCancel();
        ret = cmdFgetNgRes(out_buff, out_buff_len);
        goto fget_end;
    }
    LOG_INF("SipfFileDownload: ret=%d", ret);
    // XMODEM: 送信終了
    xret = XmodemSendEnd(500);
    if (xret == XMODEM_SEND_RET_TIMEOUT) {
        LOG_ERR("XmodemSendEnd() TIMEOUT.");
        XmodemTransmitCancel();
        ret = cmdFgetNgRes(out_buff, out_buff_len);
        goto fget_end;
    }
    if (xret == XMODEM_SEND_RET_FAILED) {
        LOG_ERR("XmodemSendEnd() FAILED.");
        XmodemTransmitCancel();
        ret = cmdFgetNgRes(out_buff, out_buff_len);
        goto fget_end;
    }
    ret = cmdFgetOkRes(ret, out_buff, out_buff_len);

fget_end:
    // XMODEM終了
    XmodemEnd();
    k_msleep(10);
    return ret;
}

/*** 管理コマンド ***/

/**
 * $$UNLOCKコマンド
 * in_buff: コマンド名より後ろを格納してるバッファ
 */
static int cmdAsciiCmdUnlock(uint8_t *in_buff, uint16_t in_len, uint8_t *out_buff, uint16_t out_buff_len)
{
    if (in_buff[0] != 0x20) {
        // 先頭がスペースじゃない
        return cmdCreateResIllParam(out_buff, out_buff_len);
    }
    if (in_len != 7) {
        // パラメータ長が違う
        return cmdCreateResIllParam(out_buff, out_buff_len);
    }

    int res_len = 0;
    if (memcmp(" UNLOCK", in_buff, 7) == 0) {
        is_unlocked = true;
        res_len = cmdCreateResOk(out_buff, out_buff_len);
    } else {
        // パラメータが”UNLOCK”じゃない
        is_unlocked = false;
        res_len = cmdCreateResIllParam(out_buff, out_buff_len);
    }
    return res_len;
}

/**
 * $$UPDATEコマンド
 * in_buff: コマンド名より後ろを格納してるバッファ
 */
static int cmdAsciiCmdUpdate(uint8_t *in_buff, uint16_t in_len, uint8_t *out_buff, uint16_t out_buff_len)
{
    if (is_unlocked == false) {
        // UNLOCKされてない
        return cmdCreateResNg(out_buff, out_buff_len);
    }
    if (in_len < 7) {
        // パラメータ長が違う
        return cmdCreateResIllParam(out_buff, out_buff_len);
    }
    if (memcmp(" UPDATE", in_buff, 7) == 0) {
        if (in_len != 7) {
            return cmdCreateResIllParam(out_buff, out_buff_len);
        }
        if (FotaHttpRun("app_update.bin") != 0) {
            // FOTA失敗した
            return cmdCreateResNg(out_buff, out_buff_len);
        }
        // FOTA成功ならリセットかかるからここには来ないはず
        return cmdCreateResOk(out_buff, out_buff_len);
    } else if (memcmp(" VERSION ", in_buff, 9) == 0) {
        // パラメータが"VERSION"だった
        if (in_len < 10) {
            // サフィックスがしていされていない
            return cmdCreateResIllParam(out_buff, out_buff_len);
        }
        in_buff[in_len] = 0x00; //文字列として扱うために末尾をNULL文字にする
        if (FotaHttpRun(&in_buff[9]) != 0) {
            // FOTA失敗した
            return cmdCreateResNg(out_buff, out_buff_len);
        }
        // FOTA成功ならリセットかかるからここには来ないはず
        return cmdCreateResOk(out_buff, out_buff_len);
    } else {
        return cmdCreateResIllParam(out_buff, out_buff_len);
    }
}

/*** GNSSコマンド ***/

/**
 * $$GNSSEN コマンド
 * in_buff: コマンド名より後ろを格納してるバッファ
 */
static int cmdAsciiCmdGnssEnable(uint8_t *in_buff, uint16_t in_len, uint8_t *out_buff, uint16_t out_buff_len)
{
    if (in_buff[0] != 0x20) {
        // 先頭がスペースじゃない
        return cmdCreateResIllParam(out_buff, out_buff_len);
    }
    if (in_len != 2) {
        // パラメータ長が違う
        return cmdCreateResIllParam(out_buff, out_buff_len);
    }

    if (in_buff[1] == '0') {
        // GPS無効
        if (gnss_stop() != 0) {
            return cmdCreateResNg(out_buff, out_buff_len);
        }
    } else if (in_buff[1] == '1') {
        // GPS有効
        if (gnss_start() != 0) {
            return cmdCreateResNg(out_buff, out_buff_len);
        }
    } else {
        return cmdCreateResIllParam(out_buff, out_buff_len);
    }

    return cmdCreateResOk(out_buff, out_buff_len);
}

/**
 * $$GNSSSTAT コマンド
 * in_buff: コマンド名より後ろを格納してるバッファ
 */
static int cmdAsciiCmdGnssStatus(uint8_t *in_buff, uint16_t in_len, uint8_t *out_buff, uint16_t out_buff_len)
{
    if (in_len != 0) {
        // パラメータ長が違う
        return cmdCreateResIllParam(out_buff, out_buff_len);
    }

    nrf_gnss_data_frame_t gps_data;
    gnss_get_data(&gps_data);

    uint8_t *buff = out_buff;

    buff += sprintf(buff, "Fix valid: %s\n", (gps_data.pvt.flags & NRF_GNSS_PVT_FLAG_FIX_VALID_BIT) == NRF_GNSS_PVT_FLAG_FIX_VALID_BIT ? "true" : "false");
    buff += sprintf(buff, "Leap second valid: %s\n", (gps_data.pvt.flags & NRF_GNSS_PVT_FLAG_LEAP_SECOND_VALID) == NRF_GNSS_PVT_FLAG_LEAP_SECOND_VALID ? "true" : "false");
    buff += sprintf(buff, "Sleep between PVT: %s\n", (gps_data.pvt.flags & NRF_GNSS_PVT_FLAG_SLEEP_BETWEEN_PVT) == NRF_GNSS_PVT_FLAG_SLEEP_BETWEEN_PVT ? "true" : "false");
    buff += sprintf(buff, "Deadline missed: %s\n", (gps_data.pvt.flags & NRF_GNSS_PVT_FLAG_DEADLINE_MISSED) == NRF_GNSS_PVT_FLAG_DEADLINE_MISSED ? "true" : "false");
    buff += sprintf(buff, "Insuf. time window: %s\n", (gps_data.pvt.flags & NRF_GNSS_PVT_FLAG_NOT_ENOUGH_WINDOW_TIME) == NRF_GNSS_PVT_FLAG_NOT_ENOUGH_WINDOW_TIME ? "true" : "false");

    buff += sprintf(buff, "OK\n");
    return (int)(buff - out_buff);
}

/**
 * $$GNSSLOC コマンド
 * in_buff: コマンド名より後ろを格納してるバッファ
 */
static int cmdAsciiCmdGnssLocation(uint8_t *in_buff, uint16_t in_len, uint8_t *out_buff, uint16_t out_buff_len)
{
    if (in_len != 0) {
        // パラメータ長が違う
        return cmdCreateResIllParam(out_buff, out_buff_len);
    }

    bool got_fix;
    nrf_gnss_data_frame_t gps_data;
    got_fix = gnss_get_data(&gps_data);

    uint8_t *buff = out_buff;

    if (!got_fix) {
        // NOTFIXED
        buff += sprintf(buff, "V,");
    } else {
        // FIXED
        buff += sprintf(buff, "A,");
    }
    buff += sprintf(buff, "%.6f,%.6f,%f,%f,%f,%04u-%02u-%02uT%02u:%02u:%02uZ", gps_data.pvt.longitude, gps_data.pvt.latitude, gps_data.pvt.altitude, gps_data.pvt.speed, gps_data.pvt.heading, gps_data.pvt.datetime.year, gps_data.pvt.datetime.month, gps_data.pvt.datetime.day, gps_data.pvt.datetime.hour, gps_data.pvt.datetime.minute, gps_data.pvt.datetime.seconds);
    buff += sprintf(buff, "\r\nOK\r\n");
    return (int)(buff - out_buff);
}

/**
 * $$GNSSNMEA コマンド
 * in_buff: コマンド名より後ろを格納してるバッファ
 */
static int cmdAsciiCmdGnssNmea(uint8_t *in_buff, uint16_t in_len, uint8_t *out_buff, uint16_t out_buff_len)
{
    if (in_len != 0) {
        // パラメータ長が違う
        return cmdCreateResIllParam(out_buff, out_buff_len);
    }
    uint8_t *buff = out_buff;
    buff += gnss_strcpy_nmea((char *)out_buff);
    buff += sprintf(buff, "\r\nOK\r\n");
    return (int)(buff - out_buff);
}

static CmdAsciiCmd cmdfunc[] = {{CMD_REG_W, cmdAsciiCmdW}, {CMD_REG_R, cmdAsciiCmdR}, {CMD_TXRAW, cmdAsciiCmdTxRaw}, {CMD_TX, cmdAsciiCmdTx}, {CMD_RX, cmdAsciiCmdRx}, {CMD_FPUT, cmdAsciiCmdFput}, {CMD_FGET, cmdAsciiCmdFget}, {CMD_UNLOCK, cmdAsciiCmdUnlock}, {CMD_UPDATE, cmdAsciiCmdUpdate}, {CMD_GNSS_ENABLE, cmdAsciiCmdGnssEnable}, {CMD_GNSS_GET_LOCATION, cmdAsciiCmdGnssLocation}, {CMD_GNSS_GET_NMEA, cmdAsciiCmdGnssNmea}, {CMD_GNSS_GET_STATUS, cmdAsciiCmdGnssStatus}, {NULL, NULL}};

int CmdAsciiParse(uint8_t *in_buff, uint16_t in_len, uint8_t *out_buff, uint16_t out_buff_len)
{
    int idx = 0;
    // コマンドリストを探す
    while (cmdfunc[idx].cmd_func) {
        //コマンド名に対応する関数を呼ぶ
        int name_len = strlen(cmdfunc[idx].cmd_name);
        if (memcmp(in_buff, cmdfunc[idx].cmd_name, name_len) == 0) {
            //コマンド名の後は区切り文字(スペース)か終端？
            if ((in_len == name_len) || (in_buff[name_len] == ' ')) {
                //コマンド名の次から末尾までのバッファを渡す
                return cmdfunc[idx].cmd_func(&in_buff[name_len], in_len - name_len, out_buff, out_buff_len);
            }
        }
        idx++;
    }

    // 未定義のコマンド
    return cmdCreateResNg(out_buff, out_buff_len);
}
