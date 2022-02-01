/*
 * Copyright (c) 2021 SAKURA internet Inc.
 *
 * SPDX-License-Identifier: MIT
 */
#include <power/reboot.h>
#include <zephyr.h>
#include <logging/log.h>

#include "fota/fota_http.h"
#include "uart_broker.h"

LOG_MODULE_REGISTER(fota, CONFIG_FOTA_LOG_LEVEL);

static K_SEM_DEFINE(sem_download_failed, 0, 1);

static char image_fullpath[IMAGE_PATH_LEN];
// static uint8_t fota_buf[FOTA_BUFF_SZ];

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

int FotaHttpRun(char *file_name_suffix)
{
    int ret;
    // FOTA Downloadライブラリを初期化
    ret = fota_download_init(fota_dl_event_handler);
    if (ret != 0) {
        LOG_ERR("fota_download_init() failed: %d", ret);
        return ret;
    }
    // FOTA開始(HTTPで)
    if (strlen(CONFIG_SIPF_FOTA_PATH) + strlen(file_name_suffix) > IMAGE_PATH_LEN) {
        // イメージファイルのパス長が長すぎる
        LOG_ERR("%s(): Image file path length too long.", __func__);
        return -1;
    }
    sprintf(image_fullpath, CONFIG_SIPF_FOTA_PATH, file_name_suffix);
    UartBrokerPuts("DOWNLOAD FROM ");
    UartBrokerPuts(CONFIG_SIPF_FOTA_HOST "/");
    UartBrokerPuts(image_fullpath);
    UartBrokerPuts("\r\n");
    ret = fota_download_start(CONFIG_SIPF_FOTA_HOST, image_fullpath, FOTA_SEC_TAG, NULL, 0);
    if (ret != 0) {
        LOG_ERR("fota_download_start() failed: %d", ret);
        return ret;
    }
    UartBrokerPuts("FOTA DOWNLOAD START\r\n");
    k_sem_take(&sem_download_failed, K_FOREVER);
    //成功したらソフトウェアリセットがかかるのでここにはこない。失敗の場合は関数を抜けてNGを出力したい
    return -1;
}
