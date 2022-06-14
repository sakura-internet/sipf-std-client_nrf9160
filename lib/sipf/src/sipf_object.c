/*
 * Copyright (c) 2021 SAKURA internet Inc.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include <logging/log.h>

LOG_MODULE_DECLARE(sipf);

#include "sipf/sipf_object.h"
#include "sipf/sipf_client_http.h"

/**
 * Byte列をパースして構造体に詰めるよ
 */
int SipfObjectParse(uint8_t *raw_buff, const uint16_t raw_len, SipfObjectObject *obj)
{
    if (raw_buff == NULL) {
        return -1;
    }
    if (obj == NULL) {
        return -1;
    }
    if (obj->value == NULL) {
        return -1;
    }
    if ((raw_len < 4) || (raw_len > 256)) {
        return -1;
    }
    //バッファから構造体へ
    obj->obj_type = raw_buff[0];
    obj->obj_tagid = raw_buff[1];
    obj->value_len = raw_buff[2];
    memcpy(obj->value, &raw_buff[3], obj->value_len);

    return 0;
}

int SipfObjectCreateObjUpPayload(uint8_t *raw_buff, uint16_t sz_raw_buff, SipfObjectObject *objs, uint8_t obj_qty)
{
    static uint8_t work[220];
    if (raw_buff == NULL) {
        return -1;
    }

    memset(raw_buff, 0, sz_raw_buff);
    int idx_raw_buff = 0;
    for (int i = 0; i < obj_qty; i++) {
        LOG_DBG("objs[%d]", i);
        if (objs[i].value_len > 220) {
            return -1;
        }
        raw_buff[idx_raw_buff++] = objs[i].obj_type;
        raw_buff[idx_raw_buff++] = objs[i].obj_tagid;
        raw_buff[idx_raw_buff++] = objs[i].value_len;

        switch (objs[i].obj_type) {
        case OBJ_TYPE_UINT8:
        case OBJ_TYPE_INT8:
        case OBJ_TYPE_BIN:
        case OBJ_TYPE_STR_UTF8:
            // バイトスワップ必要なし
            memcpy(&raw_buff[idx_raw_buff], objs[i].value, objs[i].value_len);
            idx_raw_buff += objs[i].value_len;
            break;
        default:
            // バイトスワップする(リトリエンディアン->ビッグエンディアン)
            if (objs[i].value_len % 2) {
                // データ長が偶数じゃない
                return -1;
            }
            for (int j = 0; j < objs[i].value_len; j++) {
                work[j] = objs[i].value[objs[i].value_len - j - 1];
            }
            memcpy(&raw_buff[idx_raw_buff], work, objs[i].value_len);
            idx_raw_buff += objs[i].value_len;
            break;
        }
    }
    return idx_raw_buff; // バッファに書いたデータ長を返す
}

/** SIPF_OBJクライアント  **/

/**
 * HTTPレスポンスコールバック
 */
static void http_response_cb(struct http_response *resp, enum http_final_call final_data, void *user_data)
{
    struct http_response *ur = (struct http_response*)user_data;
    
    if (resp->data_len > 0) {
        if (ur->http_status_code == 0) {
            memcpy(user_data, resp, sizeof(struct http_response));
        } else {
            ur->data_len += resp->data_len;
        }
        LOG_INF("HTTP response has come(content-length: %d, data_len: %d)", resp->content_length, resp->data_len);
        
    }
}

static int run_connector_http_request(const uint8_t *payload, const int payload_len, struct http_response *http_res)
{
    char *req_auth_header = SipfClientHttpGetAuthInfo();
    // リクエストを組み立てるよ
    LOG_INF("auth: %s", req_auth_header);

    struct http_request req;
    memset(&req, 0, sizeof(req));
    const char *headers[] = {"Connection: Close\r\n", "Content-Type: application/octet-stream\r\n", req_auth_header, NULL};

    memset(httpc_res_buff, 0, sizeof(httpc_res_buff));

    req.method = HTTP_POST;
    req.url = CONFIG_SIPF_CONNECTOR_PATH;
    req.host = CONFIG_SIPF_CONNECTOR_HTTP_HOST;
    req.protocol = "HTTP/1.1";
    req.payload = payload;
    req.payload_len = payload_len;
    req.header_fields = headers;
    req.response = http_response_cb;
    req.recv_buf = httpc_res_buff;
    req.recv_buf_len = sizeof(httpc_res_buff);

#ifndef CONFIG_SIPF_CONNECTOR_DISABLE_SSL
    bool ssl = true;
#else
    bool ssl = false;
#endif
    return SipfClientHttpRunRequest(CONFIG_SIPF_CONNECTOR_HTTP_HOST, &req, 3 * MSEC_PER_SEC, http_res, ssl);
}

int SipfObjClientObjUpRaw(uint8_t *payload_buffer, uint16_t size, SipfObjectOtid *otid)
{
    uint16_t sz_packet = 12 + size; // HEADER 12 + Size

    if (sz_packet >= sizeof(httpc_req_buff)) {
        LOG_ERR("Size is too big %d", size);
        return -1;
    }

    // COMMAND_TYPE
    httpc_req_buff[0] = (uint8_t)OBJECTS_UP;
    // COMMAND_TIME
    httpc_req_buff[1] = 0x00;
    httpc_req_buff[2] = 0x00;
    httpc_req_buff[3] = 0x00;
    httpc_req_buff[4] = 0x00;
    httpc_req_buff[5] = 0x00;
    httpc_req_buff[6] = 0x00;
    httpc_req_buff[7] = 0x00;
    httpc_req_buff[8] = 0x00;
    // OPTION_FLAG
    httpc_req_buff[9] = 0x00;

    // PAYLOAD_SIZE(BigEndian)
    LOG_INF("payload_size: %d", size);
    httpc_req_buff[10] = size >> 8;
    httpc_req_buff[11] = size & 0xff;

    memcpy(&httpc_req_buff[12], payload_buffer, size);
    LOG_HEXDUMP_DBG(httpc_req_buff, sz_packet, "request:");

    static struct http_response http_res;
    int ret = run_connector_http_request(httpc_req_buff, sz_packet, &http_res);

    LOG_INF("run_connector_http_request(): %d", ret);
    if (ret < 0) {
        return ret;
    }

    LOG_DBG("Response status %s", http_res.http_status);
    LOG_DBG("content-length: %d", http_res.content_length);
    LOG_HEXDUMP_DBG(http_res.body_start, http_res.content_length, "response:");

    // OBJID_NOTIFICATIONをパース
    uint8_t *sipf_obj_head = &http_res.body_start[0];
    uint8_t *sipf_obj_payload = &http_res.body_start[12];
    if (strcmp(http_res.http_status, "OK") != 0) {
        LOG_WRN("Invalid HTTP stauts %s", http_res.http_status);
        if (strcmp(http_res.http_status, "Unauthorized") == 0) {
            return -401;
        }
        // OK以外
        return -1;
    }

    if (http_res.content_length != 30) {
        // ペイロード長が合わない
        LOG_WRN("Invalid HTTP response size %d", http_res.content_length);
        return -1;
    }

    if (sipf_obj_head[0] != OBJID_NOTIFICATION) {
        // OBJID_NOTIFICATIONじゃない
        LOG_WRN("Invalid header type 0x%02x", sipf_obj_head[0]);
        return -1;
    }

    if (sipf_obj_payload[0] != 0x00) {
        // ResultがOKじゃない
        LOG_WRN("Invalid result 0x%02x", sipf_obj_payload[0]);
        return -1;
    }

    // OTIDを取得
    memcpy(&otid->value, &sipf_obj_payload[2], sizeof(otid->value));
    return 0;
}

int SipfObjClientObjUp(const SipfObjectUp *simp_obj_up, SipfObjectOtid *otid)
{
    if (simp_obj_up->obj.value == NULL) {
        return -1;
    }

    uint16_t size = 3 + simp_obj_up->obj.value_len;
    uint8_t payload[32];
    if (size > sizeof(payload)) {
        LOG_ERR("obj.value is too big %d", size);
        return -1;
    }

    // OBJ
    //  OBJ_TYPEID
    payload[0] = simp_obj_up->obj.obj_type;
    //  OBJ_TAGID
    payload[1] = simp_obj_up->obj.obj_tagid;
    //  OBJ_LENGTH
    payload[2] = simp_obj_up->obj.value_len;
    //  OBJ_VALUE
    memcpy(&payload[3], simp_obj_up->obj.value, simp_obj_up->obj.value_len);

    return SipfObjClientObjUpRaw(payload, size, otid);
}

int SipfObjClientObjDown(SipfObjectOtid *otid, uint8_t *remains, uint8_t *objqty, uint8_t **p_objs, uint8_t **p_user_send_datetime, uint8_t **p_recv_datetime)
{
    int ret;

    if (otid == NULL) {
        return -1;
    }
    if (objqty == NULL) {
        return -1;
    }
    if (p_user_send_datetime == NULL) {
        return -1;
    }
    if (p_recv_datetime == NULL) {
        return -1;
    }

    memset(httpc_req_buff, 0x00, BUFF_SZ);

    // COMMAND_TYPE
    httpc_req_buff[0] = (uint8_t)OBJECTS_DOWN_REQUEST;
    // COMMAND_TIME
    httpc_req_buff[1] = 0x00;
    httpc_req_buff[2] = 0x00;
    httpc_req_buff[3] = 0x00;
    httpc_req_buff[4] = 0x00;
    httpc_req_buff[5] = 0x00;
    httpc_req_buff[6] = 0x00;
    httpc_req_buff[7] = 0x00;
    httpc_req_buff[8] = 0x00;
    // OPTION_FLAG
    httpc_req_buff[9] = 0x00;
    // PAYLOAD_SIZE(BigEndian)
    httpc_req_buff[10] = 0x00;
    httpc_req_buff[11] = 0x01;

    // OBJECTS_DOWN
    httpc_req_buff[12] = 0x00; // RESERVED

    /* リクエスト送信 */
    static struct http_response http_res;
    memset(&http_res, 0, sizeof(struct http_response));
    ret = run_connector_http_request(httpc_req_buff, 13, &http_res);

    LOG_INF("run_connector_http_request(): %d", ret);
    if (ret < 0) {
        return ret;
    }

    LOG_INF("Response status %s(%d)", http_res.http_status, http_res.http_status_code);
    LOG_INF("data_len: %d content-length: %d", http_res.data_len, http_res.content_length);
    /* レスポンスを処理 */
    uint8_t *sipf_obj_head = &http_res.body_start[0];
    uint8_t *sipf_obj_payload = &http_res.body_start[12];
    if (strcmp(http_res.http_status, "OK") != 0) {
        if (strcmp(http_res.http_status, "Unauthorized") == 0) {
            return -401;
        }
        // OK以外
        return -1;
    }

    // OBJECTS_DOWNをパース
    if ((http_res.content_length < 12) || (http_res.content_length >= 12 + BUFF_SZ)) {
        // ザイズが仕様の範囲外
        LOG_ERR("Invalid content_length: %d", http_res.content_length);
        return -1;
    }
    if (sipf_obj_head[0] != OBJECTS_DOWN) {
        // OBJECTS_DOWNじゃない
        LOG_ERR("Invalid command type: %d", sipf_obj_head[0]);
        return -1;
    }

    if (sipf_obj_payload[0] != 0x00) {
        // REQEST_RESULTがOK(0x00)じゃない
        LOG_ERR("REQUEST_RESULT: %d", sipf_obj_payload[0]);
        return -1;
    }

    // OTIDをコピー
    memcpy(&otid->value, &sipf_obj_payload[1], sizeof(otid->value));
    // USER_SEND_DATETIME_MSへのポインタをコピー
    *p_user_send_datetime = &sipf_obj_payload[17];
    // RECEIVED_DATETIME_MSへのポインタをコピー
    *p_recv_datetime = &sipf_obj_payload[25];
    // REMAINS
    *remains = sipf_obj_payload[33];
    // (RESERVED)

    // obj
    *objqty = 0;
    if (http_res.content_length == 12 + 35) {
        // objなし
        LOG_INF("empty");
        return 0;
    }
    int idx = 35; // objの先頭
    for (;;) {
        //オブジェクトの先頭ポインタをリストに追加
        p_objs[(*objqty)++] = &sipf_obj_payload[idx];
        //オブジェクト数の上限に達してたら中断
        if (*objqty >= OBJ_MAX_CNT) {
            break;
        }
        //インデックスを次のオブジェクトの先頭に設定
        idx += (3 + sipf_obj_payload[idx + 2]);
        if (idx >= http_res.content_length - 12) {
            //インデックスが受信データの終端を超えた
            break;
        }
    }

    return 0;
}