/*
 * Copyright (c) 2021 Sakura Internet Inc.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include <net/net_ip.h>
#include <net/socket.h>
#include <net/tls_credentials.h>
#include <net/http_client.h>
#include <sys/base64.h>

#include "registers.h"
#include "sipf/sipf_object.h"
#include "debug_print.h"

#define HTTP_HOST   "133.242.234.182"
#define HTTP_PORT   8080
#define BUFF_SZ (1024)

static uint8_t req_buff[BUFF_SZ];
static uint8_t res_buff[BUFF_SZ];

static char req_auth_header[256];

static void response_cb(struct http_response *resp, enum http_final_call final_data, void *user_data)
{
    if (resp->data_len > 0) {
        DebugPrint("HTTP response has come\r\n");
        memcpy(user_data, resp, sizeof(struct http_response));
    }
}

static int createAuthInfoFromRegister(void)
{
    char tmp[162];
    int len, olen;

    if (*REG_00_UN_LEN == 0) {
        return -1;
    }
    if (*REG_00_PW_LEN == 0) {
        return -1;
    }

    memcpy(tmp, REG_00_USER_NAME, *REG_00_UN_LEN);
    tmp[*REG_00_UN_LEN] = ':';
    memcpy(&tmp[*REG_00_UN_LEN + 1], REG_00_PASSWORD, *REG_00_PW_LEN);
    len = *REG_00_UN_LEN + *REG_00_PW_LEN + 1;

    strncpy(req_auth_header, "Authorization: BASIC ", 21);
    if (base64_encode(&req_auth_header[21], 210, &olen, tmp, len) < 0) {
        return -1;
    }
    len = 21 + olen;
    strncpy(&req_auth_header[len], "\r\n", 2);
    return len + 2;
}

static int run_http_request(const uint8_t *payload, const int payload_len, struct http_response *http_res)
{
    int sock;
    int ret;
    struct addrinfo *res;
    struct addrinfo hints = {
        .ai_family   = AF_INET,
        .ai_socktype = SOCK_STREAM,
    };
    // 接続先をセットアップするよ
    ret = getaddrinfo(HTTP_HOST, NULL, &hints, &res);
    if (ret) {
        DebugPrint(ERR "getaddrinfo failed: ret=%d errno=%d\r\n", ret, errno);
        return -errno;
    }
    ((struct sockaddr_in *)res->ai_addr)->sin_port = htons(HTTP_PORT);

    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock <  0) {
        DebugPrint(ERR "socket() failed: ret=%d errno=%d\r\n", ret, errno);
        freeaddrinfo(res);
        return -errno;
    }
    // 接続するよ
    DebugPrint(INFO "Connect to " HTTP_HOST ":%d\r\n", HTTP_PORT);
    ret = connect(sock, res->ai_addr, sizeof(struct sockaddr_in));
    if (ret) {
        DebugPrint(ERR "connect() failed: ret=%d errno=%d\r\n", ret, errno);
        freeaddrinfo(res);
        (void)close(sock);
        return -errno;
    }

    // リクエストを組み立てるよ
    createAuthInfoFromRegister(); //レジスタから認証情報を生成する
    DebugPrint("auth: %s\r\n", req_auth_header);

    struct http_request req;
    memset(&req, 0, sizeof(req));
    const char *headers[] = {
        "Connection: Close\r\n",
        "Content-Type: application/octet-stream\r\n",
        req_auth_header,
        NULL
    };

    req.method = HTTP_POST;
    req.url = "/";
    req.host = HTTP_HOST;
    req.protocol = "HTTP/1.1";
    req.payload = payload;
    req.payload_len = payload_len;
    req.header_fields = headers;
    req.response = response_cb;
    req.recv_buf = res_buff;
    req.recv_buf_len = sizeof(res_buff);

    ret = http_client_req(sock, &req, 3 * MSEC_PER_SEC, (void *)http_res);
    freeaddrinfo(res);
    close(sock);

    return ret;
}

int SipfClientSetAuthInfo(const char *user_name, const char *passwd)
{
    char tmp1[128], tmp2[128];

    int ilen, olen;
    
    ilen = sprintf(tmp1, "%s:%s", user_name, passwd);
    if (base64_encode(tmp2, sizeof(tmp2), &olen, tmp1, ilen) < 0) {
        return -1;
    }
    return sprintf(req_auth_header, "Authorization: BASIC %s\r\n", tmp2);
}

int SipfClientObjUp(const SipfObjectUp *simp_obj_up, SipfObjectOtid *otid)
{
    int ret;
    uint16_t sz;

    if (simp_obj_up->obj.value == NULL) {
        return -1;
    }

    uint16_t sz_packet = simp_obj_up->obj.value_len + 12 + 5;

    //COMMAND_TYPE
    req_buff[0] = (uint8_t)OBJECTS_UP;
    //COMMAND_TIME
    req_buff[1] = 0x00;
    req_buff[2] = 0x00;
    req_buff[3] = 0x00;
    req_buff[4] = 0x00;
    req_buff[5] = 0x00;
    req_buff[6] = 0x00;
    req_buff[7] = 0x00;
    req_buff[8] = 0x00;
    //OPTION_FLAG
    req_buff[9] = 0x00;
    //PAYLOAD_SIZE(BigEndian)
    sz = simp_obj_up->obj.value_len + 5;
    req_buff[10] = sz >> 8;
    req_buff[11] = sz & 0xff;

    //payload: OBJECTS_UP
    uint8_t *payload = &req_buff[12];
    // OBJ_QTY 
    payload[0] = 0x01;
    // OBJ
    //  OBJ_LENGTH(BigEndian)
    sz = simp_obj_up->obj.value_len + 4;
    payload[1] = sz >> 8;
    payload[2] = sz & 0xff;
    //  OBJ_TYPEID
    payload[3] = simp_obj_up->obj.obj_type;
    //  OBJ_TAGID
    payload[4] = simp_obj_up->obj.obj_tagid;
    //  OBJ_VALUE
    memcpy(&payload[5], simp_obj_up->obj.value, simp_obj_up->obj.value_len);

    static struct http_response http_res;
    ret = run_http_request(req_buff, sz_packet, &http_res);

    DebugPrint(INFO "run_http_request(): %d\r\n", ret);
    DebugPrint(DBG "Response status %s\r\n", http_res.http_status);
    DebugPrint("content-length: %d\r\n", http_res.content_length);
    for (int i = 0; i < http_res.content_length; i++) {
        DebugPrint("0x%02x ", http_res.body_start[i]);
    }
    DebugPrint("\r\n");

    // OBJID_NOTIFICATIONをパース
    uint8_t *sipf_obj_head = &http_res.body_start[0];
    uint8_t *sipf_obj_payload = &http_res.body_start[12]; 
    if (strcmp(http_res.http_status, "OK") != 0) {
        if (strcmp(http_res.http_status, "Unauthorized") == 0) {
            return -401;
        }
        // OK以外
        return -1;
    }

    if (http_res.content_length != 30) {
        // ペイロード長が合わない
        return -1;
    }

    if (sipf_obj_head[0] != 0x02) {
        // OBJID_NOTIFICATIONじゃない
        return -1;
    }

    if (sipf_obj_payload[0] != 0x00) {
        // ResultがOKじゃない
        return -1;
    }

    //OTIDを取得
    memcpy(&otid->value, &sipf_obj_payload[2], sizeof(otid->value));

    return 0;
}
