/**
 * Copyright (c) 2022 SAKURA internet Inc.
 *
 * SPDX-License-Identifier: MIT
 */
#include <logging/log.h>

LOG_MODULE_DECLARE(sipf);

#include <stdio.h>
#include <string.h>
#include "sipf/sipf_client_http.h"
#include "sipf/sipf_file.h"

uint8_t *dl_buff;
char path_endpoint[128];
/**
 * HTTPレスポンスコールバック
 */
static void http_request_cb(struct http_response *resp, enum http_final_call final_data, void *user_data)
{
    if (resp->data_len > 0) {
        LOG_DBG("HTTP response has come");
        memcpy(user_data, resp, sizeof(struct http_response));
    }
}

/**
 * URL要求リクエスト
 */
enum req_url_type
{
    REQ_URL_DOWNLOAD,
    REQ_URL_UPLOAD
};

static int sipfFileRequestDownloadURL(enum req_url_type req_type, const char *user_name, const char *password, const char *file_id, char *url)
{
    int ret;
    /* リクエストを組み立てるよ */
    struct http_request req;

    char *req_auth_header = SipfClientHttpGetAuthInfo();

    memset(&req, 0, sizeof(req));
    const char *headers[] = {"Connection: Close\r\n", "Content-Type: text/plain\r\n", req_auth_header, NULL};

    switch (req_type) {
    case REQ_URL_DOWNLOAD:
        req.method = HTTP_GET;
    case REQ_URL_UPLOAD:
        req.method = HTTP_PUT;
    default:
        return -1;
    }

    if ((strlen(CONFIG_SIPF_FILE_REQ_URL_PATH) + strlen(file_id)) > strlen(path_endpoint)) {
        // PATHがバッファに入り切らない
        return -1;
    }
    ret = sprintf(path_endpoint, CONFIG_SIPF_FILE_REQ_URL_PATH, file_id);

    req.url = path_endpoint;
    req.host = CONFIG_SIPF_FILE_REQ_URL_HOST;
    req.protocol = "HTTP/1.1";
    req.payload = NULL;
    req.payload_len = 0;
    req.header_fields = headers;
    req.response = http_request_cb;
    req.recv_buf = httpc_res_buff;
    req.recv_buf_len = sizeof(httpc_res_buff);

    /* リクエストするよ */
    static struct http_response http_res;
    ret = SipfClientHttpRunRequest(CONFIG_SIPF_FILE_REQ_URL_HOST, &req, 3 * MSEC_PER_SEC, &http_res, true);
    if (ret < 0) {
        return ret;
    }

    /* レスポンスを解釈するよ */
    if (strcmp(http_res.http_status, "OK") != 0) {
        // 200 OK以外のレスポンスが返ってきた
        return -1;
    }

    strncpy(url, http_res.body_start, http_res.content_length);
    return 0;
}

/**
 * ダウンロードURL要求
 */
int SipfFileRequestDownloadURL(const char *user_name, const char *password, const char *file_id, char *url)
{
    return sipfFileRequestDownloadURL(REQ_URL_DOWNLOAD, user_name, password, file_id, url);
}

/**
 * アップロードURL要求
 */
int SipfFileRequestUploadURL(const char *user_name, const char *password, const char *file_id, char *url)
{
    return sipfFileRequestDownloadURL(REQ_URL_UPLOAD, user_name, password, file_id, url);
}

#if 0
//セマフォ

int download_client_callback(const struct download_client_evt *event)
{
	static size_t file_size;
	size_t offset;
	int err;

	if (event == NULL) {
		return -EINVAL;
	}

	switch (event->id) {
	case DOWNLOAD_CLIENT_EVT_FRAGMENT:
        memcpy(dl_buff, event->fragment.buf, event->fragment.len);
        break;
    case DOWNLOAD_CLIENT_EVT_DONE:
        break;
    case DOWNLOAD_CLIENT_EVT_ERROR:
        break;
    default:
        break;
    }
}

int SipfFileDownload(const char *file_name, uint8_t *buff, size_t sz_buff)
{

    dl_buff = buff;

    struct download_client_cfg config = {
        .frag_size_override = sz_buff,
    };

    //ダウンロード開始

    //ダウンロード終了まち
}
#endif