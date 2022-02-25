/*
 * Copyright (c) 2021 SAKURA internet Inc.
 *
 * SPDX-License-Identifier: MIT
 */
#include <power/reboot.h>
#include <zephyr.h>
#include <logging/log.h>

#include "fota/fota_http.h"
#include "sipf/sipf_client_http.h"
#include "sipf/sipf_file.h"
#include "uart_broker.h"

LOG_MODULE_REGISTER(fota, CONFIG_FOTA_LOG_LEVEL);

static K_SEM_DEFINE(sem_download_failed, 0, 1);

static char image_url[IMAGE_URL_LEN];

static void fota_dl_event_handler(const struct fota_download_evt *evt)
{
    switch (evt->id) {
    case FOTA_DOWNLOAD_EVT_ERROR:
        UartBrokerPuts("FOTA DOWNLOAD FAILED\r\n");
        LOG_ERR("fota_download failed");
        k_sem_give(&sem_download_failed);
        break;
    case FOTA_DOWNLOAD_EVT_PROGRESS:
        UartBrokerPrint("%3d%% DOWNLOADED\r\n", evt->progress);
        break;
    case FOTA_DOWNLOAD_EVT_FINISHED:
        UartBrokerPuts("FOTA DOWNLOAD FINISHED\r\n");
        UartBrokerPuts("REBOOT & RUN UPDATE\r\n");
        LOG_INF("fota_download finished");
        sys_reboot(SYS_REBOOT_COLD);
        break;
    default:
        break;
    }
}

/**
 * FOTA実行
 *
 * 実行前にSipfClientHttpSetAuthInfo()で認証情報を設定しておくこと
 */
int FotaHttpRun(char *file_name_suffix)
{
    int ret;
    int sec_tag = -1; // DISABLE TLS

    // ダウンロードURLを取得
    ret = SipfFileRequestDownloadURL(file_name_suffix, image_url, IMAGE_URL_LEN);
    if (ret < 0) {
        // URL取得失敗
        LOG_ERR("SipfFileRequestDownloadURL() faild: %d", ret);
        return -1;
    }
    // URLをホスト名とパスに分解(URLのバッファ内のポインタを返すよ)
    char *protocol = NULL;
    char *host = NULL;
    char *path = NULL;
    if (SipfClientHttpParseURL(image_url, ret, &protocol, &host, &path) != 0) {
        // 分解できなかった
        LOG_ERR("SipfClientHttpParseURL() failed.");
        return -1;
    }

    if ((protocol == NULL) || (host == NULL) || (path == NULL)) {
        LOG_ERR("SipfClientHttpParseURL() Invalid result.");
        return -1;
    }

    if (strcmp(protocol, "https") == 0) {
        sec_tag = 42; // ENABLE TLS
    } else if (strcmp(protocol, "http") != 0) {
        // httpでもhttpsでもない
        LOG_ERR("Imvalid download protocol: %s", protocol);
        return -1;
    }

    // FOTA Downloadライブラリを初期化
    ret = fota_download_init(fota_dl_event_handler);
    if (ret != 0) {
        LOG_ERR("fota_download_init() failed: %d", ret);
        return ret;
    }
    // FOTA開始(HTTPで)
    UartBrokerPuts("DOWNLOAD FILE: ");
    UartBrokerPuts(file_name_suffix);
    UartBrokerPuts("\r\n");
    ret = fota_download_start(host, path, sec_tag, NULL, 1024);
    if (ret != 0) {
        LOG_ERR("fota_download_start() failed: %d", ret);
        return ret;
    }
    UartBrokerPuts("FOTA DOWNLOAD START\r\n");
    k_sem_take(&sem_download_failed, K_FOREVER);
    //成功したらソフトウェアリセットがかかるのでここにはこない。失敗の場合は関数を抜けてNGを出力したい
    return -1;
}
