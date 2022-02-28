/*
 * Copyright (c) 2021 SAKURA internet Inc.
 *
 * SPDX-License-Identifier: MIT
 */
#include <logging/log.h>

LOG_MODULE_DECLARE(sipf);

#include "sipf/sipf_auth.h"
#include "sipf/sipf_client_http.h"

/** SIPF_AUTHクライアント  **/

/**
 * HTTPレスポンスコールバック
 */
static void http_response_cb(struct http_response *resp, enum http_final_call final_data, void *user_data)
{
    if (resp->data_len > 0) {
        LOG_INF("HTTP response has come");
        memcpy(user_data, resp, sizeof(struct http_response));
    }
}

int run_get_session_key_http_request(struct http_response *http_res)
{
    struct http_request req;
    memset(&req, 0, sizeof(req));
    const char *headers[] = {"Connection: Close\r\n", "Content-Type: text/plain\r\n", "Accept: text/plain\r\n", NULL};

    req.method = HTTP_POST;
    req.url = CONFIG_SIPF_AUTH_PATH;
    req.host = CONFIG_SIPF_AUTH_HOST;
    req.protocol = "HTTP/1.1";
    req.payload = NULL;
    req.payload_len = 0;
    req.header_fields = headers;
    req.response = http_response_cb;
    req.recv_buf = httpc_res_buff;
    req.recv_buf_len = sizeof(httpc_res_buff);

#ifndef CONFIG_SIPF_AUTH_DISABLE_SSL
    bool ssl = true;
#else
    bool ssl = false;
#endif
    return SipfClientHttpRunRequest(CONFIG_SIPF_AUTH_HOST, &req, 3 * MSEC_PER_SEC, http_res, ssl);
}

/**
 * IPアドレス(SIM)認証リクエスト
 */
int SipfAuthRequest(char *user_name, uint8_t sz_user_name, char *password, uint8_t sz_password)
{
    int ret;
    static struct http_response http_res;
    ret = run_get_session_key_http_request(&http_res);

    LOG_INF("run_get_session_key_http_request(): %d", ret);
    if (ret < 0) {
        return ret;
    }

    LOG_INF("Response status %s", http_res.http_status);
    LOG_INF("content-length: %d", http_res.content_length);
    // FIXME: CONFIG_SIPF_LOG_LEVEL をつかっていい感じに抑制する
    for (int i = 0; i < http_res.content_length; i++) {
        LOG_DBG("0x%02x ", http_res.body_start[i]);
    }
    // FIXME: ここまで

    if (strcmp(http_res.http_status, "OK") != 0) {
        // 200 OK以外のレスポンスが返ってきた
        return -1;
    }

    // レスポンスのフォーマットは USERNAME\nPASSWORD\n
    // FIXME: 例外処理もしっかりやる
    char *u = (char *)&http_res.body_start[0];
    char *p = NULL;
    for (int i = 0; i < http_res.content_length; i++) {
        if (http_res.body_start[i] == '\n') {
            http_res.body_start[i] = 0;
            if (p == NULL) {
                p = (char *)&(http_res.body_start[i + 1]);
            }
        }
    }
    if (p != NULL) {
        memset(user_name, 0, sz_user_name);
        strncpy(user_name, u, sz_user_name);
        if (user_name[sz_user_name - 1] != 0x00) {
            // NULL終端されてない=バッファより長かった
            LOG_ERR("user_name is too long");
            return -1; // NG
        }

        memset(password, 0, sz_password);
        strncpy(password, p, sz_password);
        if (password[sz_password - 1] != 0x00) {
            LOG_ERR("password is too long");
            return -1; // NG
        }

        LOG_INF("user_name=%s passwd=%s", user_name, password);
        return 0;
    }
    return -1;
}
