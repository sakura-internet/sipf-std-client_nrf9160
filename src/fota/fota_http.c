/*
 * Copyright (c) 2021 Sakura Internet Inc.
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

static uint8_t fota_buf[512];

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

int FotaHttpRun(void)
{
  int ret;
  // DFUライブラリにバッファを設定
  ret = dfu_target_mcuboot_set_buf(fota_buf, sizeof(fota_buf));
  if (ret != 0) {
    LOG_ERR("dfu_target_mcuboot_set_buf() failed: %d", ret);
    return ret;
  }
  // FOTA Downloadライブラリを初期化
  ret = fota_download_init(fota_dl_event_handler);
  if (ret != 0) {
    LOG_ERR("fota_download_init() failed: %d", ret);
    return ret;
  }
  // FOTA開始(HTTPで)
  ret = fota_download_start(CONFIG_SIPF_FOTA_HOST, CONFIG_SIPF_FOTA_PATH, -1, NULL, 0);
  if (ret != 0) {
    LOG_ERR("fota_download_start() failed: %d", ret);
    return ret;
  }
  UartBrokerPuts("FOTA DOWNLOAD START\r\n");
  k_sem_take(&sem_download_failed, K_FOREVER);
  //成功したらソフトウェアリセットがかかるのでここにはこない。失敗の場合は関数を抜けてNGを出力したい
  return -1;
}