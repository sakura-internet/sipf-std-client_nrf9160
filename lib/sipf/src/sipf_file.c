/**
 * Copyright (c) 2022 SAKURA internet Inc.
 *
 * SPDX-License-Identifier: MIT
 */
#include <logging/log.h>

LOG_MODULE_DECLARE(sipf);

#include <stdio.h>
#include <string.h>
#include <zephyr.h>

#include "sipf/sipf_client_http.h"
#include "sipf/sipf_file.h"

uint8_t *dl_buff;
static char path_endpoint[128];
/**
 * HTTPレスポンスコールバック
 */
static void http_request_cb(struct http_response *resp, enum http_final_call final_data, void *user_data)
{
    LOG_DBG("resp->http_status: %s", resp->http_status);
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

static int sipfFileRequestURL(enum req_url_type req_type, const char *file_id, char *url, int sz_url)
{
    int ret;
    /* リクエストを組み立てるよ */
    struct http_request req;

    char *req_auth_header = SipfClientHttpGetAuthInfo();

    memset(&req, 0, sizeof(req));
    const char *headers[] = {"Connection: Close\r\n", /*"Authorization: BASIC dTYxbGc2VFNmRTJCOnFINVpMb0d4VGRYNQ==\r\n"*/ req_auth_header, NULL};

    switch (req_type) {
    case REQ_URL_DOWNLOAD:
        req.method = HTTP_GET;
        break;
    case REQ_URL_UPLOAD:
        req.method = HTTP_PUT;
        break;
    default:
        LOG_ERR("Invalid request type.");
        return -1;
    }

    if ((strlen(CONFIG_SIPF_FILE_REQ_URL_PATH) + strlen(file_id)) > sizeof(path_endpoint)) {
        // PATHがバッファに入り切らない
        LOG_ERR("Request URL buffer full.");
        return -1;
    }
    ret = sprintf(path_endpoint, CONFIG_SIPF_FILE_REQ_URL_PATH, file_id);
    LOG_DBG("REQEST URL: %s", path_endpoint);

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
    ret = SipfClientHttpRunRequest(CONFIG_SIPF_FILE_REQ_URL_HOST, &req, 3 * MSEC_PER_SEC, &http_res, false /*true*/);
    LOG_DBG("SipfClientHttpRunRequest(): %d", ret);
    if (ret < 0) {
        LOG_ERR("SipfClientHttpRunRequest() failed.");
        return ret;
    }
    /* レスポンスを解釈するよ */
    if (strcmp(http_res.http_status, "OK") != 0) {
        // 200 OK以外のレスポンスが返ってきた
        LOG_ERR("Invalid HTTP respons: %s", http_res.http_status);
        return -1;
    }

    if (sz_url < http_res.content_length) {
        // URLのバッファよりレスポンスボディが大きい
        LOG_ERR("Response buffer full: sz_buff=%d content_length=%d", sz_url, http_res.content_length);
        return -1;
    }

    strncpy(url, http_res.body_start, http_res.content_length);
    LOG_HEXDUMP_INF(url, http_res.content_length, "url:");
    return http_res.content_length;
}

/**
 * ダウンロードURL要求
 */
int SipfFileRequestDownloadURL(const char *file_id, char *url, int sz_url)
{
    return sipfFileRequestURL(REQ_URL_DOWNLOAD, file_id, url, sz_url);
}

/**
 * アップロードURL要求
 */
int SipfFileRequestUploadURL(const char *file_id, char *url, int sz_url)
{
    return sipfFileRequestURL(REQ_URL_UPLOAD, file_id, url, sz_url);
}

/**
 * アップロード完了通知
 */
int SipfFileUploadComplete(const char *file_id)
{
    int ret;
    /* リクエストを組み立てるよ */
    struct http_request req;

    char *req_auth_header = SipfClientHttpGetAuthInfo();

    memset(&req, 0, sizeof(req));
    const char *headers[] = {"Connection: Close\r\n", "Content-Type: text/plain\r\n", req_auth_header, NULL};

    req.method = HTTP_PUT;

    int path_len = strlen(CONFIG_SIPF_FILE_REQ_URL_PATH) + sizeof(file_id);
    if (path_len > sizeof(path_endpoint)) {
        // PATHがバッファに入り切らない
        LOG_ERR("%s() Path buffer full. path_len=%d", __func__, path_len);
        return -1;
    }
    ret = sprintf(path_endpoint, CONFIG_SIPF_FILE_REQ_URL_PATH "complete/", file_id);

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
        LOG_ERR("%s(): SipfClientHttpRunRequest() failed: ret=%d", __func__, ret);
        return ret;
    }

    /* レスポンスを解釈するよ */
    if (strcmp(http_res.http_status, "OK") != 0) {
        // 200 OK以外のレスポンスが返ってきた
        LOG_ERR("Invalid HTTP respons: %s", http_res.http_status);
        return -1;
    }

    return 0;
}

/** Upload **/
static int sipfFileCallbackUploadRequest(char *host, char *file_path, http_payload_cb_t cb, int content_length, bool tls)
{
    int ret;

    /* */
    struct http_request req;

    memset(&req, 0, sizeof(req));
    char header_content_length[30];

    sprintf(header_content_length, "Content-Length: %d\r\n", content_length);

    const char *headers[] = {"Content-Type: application/octet-stream\r\n", header_content_length, NULL};

    req.method = HTTP_PUT;
    req.url = file_path;
    req.host = host;
    req.protocol = "HTTP/1.1";
    req.payload_cb = cb;
    req.payload_len = 0;
    req.header_fields = headers;
    req.response = http_request_cb;
    req.recv_buf = httpc_res_buff;
    req.recv_buf_len = sizeof(httpc_res_buff);

    /* リクエストするよ */
    static struct http_response http_res;
    ret = SipfClientHttpRunRequest(host, &req, 3 * MSEC_PER_SEC, &http_res, tls);
    if (ret < 0) {
        LOG_ERR("SipfClientHttpRunRequest failed: ret=%d", ret);
        return ret;
    }
    /* ステータスをチェック */
    if (strcmp(http_res.http_status, "OK") != 0) {
        // 200 OK以外のレスポンスが返ってきた
        LOG_ERR("Upload failed: Status=%s", http_res.http_status);
        return -1;
    }

    return ret;
}

static int sipfFileUploadRequest(char *host, char *file_path, uint8_t *buff, int sz_buff, bool tls)
{
    int ret;

    /* リクエストを組み立てるよ */
    struct http_request req;

    memset(&req, 0, sizeof(req));
    const char *headers[] = {"Connection: Close\r\n", "Content-Type: application/octet-stream\r\n", NULL};

    req.method = HTTP_PUT;
    req.url = file_path;
    req.host = host;
    req.protocol = "HTTP/1.1";
    req.payload = buff;
    req.payload_len = sz_buff;
    req.header_fields = headers;
    req.response = http_request_cb;
    req.recv_buf = httpc_res_buff;
    req.recv_buf_len = sizeof(httpc_res_buff);

    /* リクエストするよ */
    static struct http_response http_res;
    ret = SipfClientHttpRunRequest(host, &req, 3 * MSEC_PER_SEC, &http_res, tls);
    if (ret < 0) {
        LOG_ERR("SipfClientHttpRunRequest failed: ret=%d", ret);
        return ret;
    }
    /* ステータスをチェック */
    if (strcmp(http_res.http_status, "OK") != 0) {
        // 200 OK以外のレスポンスが返ってきた
        LOG_ERR("Upload failed: Status=%s", http_res.http_status);
        return -1;
    }

    return ret;
}

static char image_url[400];
int SipfFileUpload(char *file_id, uint8_t *buff, http_payload_cb_t cb, int sz_payload)
{
    int ret;
    // アップロードURL取得
    ret = SipfFileRequestUploadURL(file_id, image_url, sizeof(image_url));
    if (ret < 0) {
        LOG_ERR("SipfFileRequestUploadURL(): failed ret=%d", ret);
        return ret;
    }
    // URLを分割
    char *prot = NULL;
    char *host = NULL;
    char *path = NULL;
    ret = SipfClientHttpParseURL(image_url, ret, &prot, &host, &path);
    if (ret < 0) {
        // 分割失敗
        LOG_ERR("SipfClientHttpParseURL(): failed ret=%d", ret);
        return -1;
    }
    if ((prot == NULL) || (host == NULL) || (path == NULL)) {
        // 分割うまくいってなさそう
        LOG_ERR("SipfClientHttpParseURL(): invalid result.");
        return -1;
    }
    bool tls = false;
    if (strcmp(prot, "https") == 0) {
        tls = true;
    } else if (strcmp(prot, "http") == 0) {
        tls = false;
    } else {
        // 未対応なプロトコル
        LOG_ERR("Invalid protocol.");
        return -1;
    }

    for (int i = strlen(path); i >= 0; i--) {
        path[i + 1] = path[i];
    }
    path[0] = '/';

    // アップロード
    if (cb == NULL) {
        ret = sipfFileUploadRequest(host, path, buff, sz_payload, tls);
    } else {
        ret = sipfFileCallbackUploadRequest(host, path, cb, sz_payload, tls);
    }
    if (ret < 0) {
        // アップロード失敗
        LOG_ERR("sipfFileUploadRequest(): failed %d", ret);
        return ret;
    }

    //アップロード完了通知
    ret = SipfFileUploadComplete(file_id);
    if (ret != 0) {
        LOG_ERR("SipfFileUploadComplete() failed: ret=%d", ret);
        return ret;
    }

    return ret;
}

/**
 * Download
 */
static K_SEM_DEFINE(sem_dl_finish, 0, 1);
static sipfFileDownload_cb_t dl_cb = NULL;
static int dl_cb_err = 0;
int download_client_callback(const struct download_client_evt *event)
{
    int ret;

    if (event == NULL) {
        return -EINVAL;
    }

    switch (event->id) {
    case DOWNLOAD_CLIENT_EVT_FRAGMENT:
        LOG_INF("DOWNLOAD_CLIENT_EVT_FRAGMENT");
        if (dl_cb) {
            ret = dl_cb((uint8_t *)event->fragment.buf, event->fragment.len);
            if (ret < 0) {
                dl_cb_err = ret;
                k_sem_give(&sem_dl_finish);
                return ret;
            }
        }
        break;
    case DOWNLOAD_CLIENT_EVT_DONE:
        LOG_INF("DOWNLOAD_CLIENT_EVT_DONE");
        dl_cb_err = 0;
        k_sem_give(&sem_dl_finish);
        break;
    case DOWNLOAD_CLIENT_EVT_ERROR:
        LOG_ERR("DOWNLOAD_CLIENT_EVT_ERR");
        dl_cb_err = -1;
        k_sem_give(&sem_dl_finish);
        return -1;
    default:
        break;
    }
    return 0;
}

int SipfFileDownload(const char *file_id, uint8_t *buff, size_t sz_download, sipfFileDownload_cb_t cb)
{
    int ret;
    dl_buff = buff;

    //ダウンロードURL取得
    ret = SipfFileRequestDownloadURL(file_id, image_url, sizeof(image_url));
    if (ret < 0) {
        LOG_ERR("SipfFileRequestDownloadURL() failed: %d", ret);
        return ret;
    }
    // URLを分割
    char *prot = NULL;
    char *host = NULL;
    char *path = NULL;
    ret = SipfClientHttpParseURL(image_url, ret, &prot, &host, &path);
    if (ret < 0) {
        // 分割失敗
        LOG_ERR("SipfClientHttpParseURL() failed: %d", ret);
        return ret;
    }
    if ((prot == NULL) || (host == NULL) || (path == NULL)) {
        // 分割うまくいってなさそう
        LOG_ERR("SipfClientHttpParseURL(): invalid result.");
        return -1;
    }
    int sec_tag;
    if (strcmp(prot, "https") == 0) {
        sec_tag = TLS_SEC_TAG;
    } else if (strcmp(prot, "http") == 0) {
        sec_tag = -1;
    } else {
        // 未対応なプロトコル
        LOG_ERR("Invalid protocol.");
        return -1;
    }

    // Download Clientの設定
    struct download_client_cfg config = {
        .sec_tag = sec_tag, .apn = NULL, .frag_size_override = sz_download, .set_tls_hostname = (sec_tag != -1),
    };

    // Download Client初期化
    struct download_client dc;
    dl_cb = cb; // FLAGMENTダウンロードイベントで呼ぶコールバック関数を設定
    ret = download_client_init(&dc, download_client_callback);
    if (ret != 0) {
        LOG_ERR("download_client_init() failed: %d", ret);
        return ret;
    }
    //接続
    ret = download_client_connect(&dc, host, &config);
    if (ret != 0) {
        LOG_ERR("download_client_connect() failed: %d", ret);
        return ret;
    }
    //ダウンロード開始
    ret = download_client_start(&dc, path, 0);
    if (ret != 0) {
        LOG_ERR("download_client_start() failed: %d", ret);
        download_client_disconnect(&dc);
        return ret;
    }

    //ダウンロード終了まち
    ret = k_sem_take(&sem_dl_finish, K_FOREVER /*K_MSEC(10000)*/);
    if (ret == -EAGAIN) {
        // タイムアウト
        LOG_ERR("k_sem_take(sem_dl_finish): timeout.");
        download_client_disconnect(&dc);
        return ret;
    } else if (ret != 0) {
        // タイムアウト以外のエラー
        LOG_ERR("k_sem_take(sem_dl_finish): err=%d", ret);
        download_client_disconnect(&dc);
        return ret;
    }

    // コールバック関数のエラーチェック
    if (dl_cb_err == 0) {
        LOG_DBG("file_size: %d", dc.file_size);
        ret = dc.file_size;
    } else {
        LOG_ERR("Download callback error: %d", dl_cb_err);
        ret = dl_cb_err;
    }
    download_client_disconnect(&dc);
    return ret;
}