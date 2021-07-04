/*
 * Copyright (c) 2021 Sakura Internet Inc.
 *
 * SPDX-License-Identifier: MIT
 */
#include <power/reboot.h>
#include <zephyr.h>
#include <logging/log.h>

#include "fota/fota_http.h"

LOG_MODULE_REGISTER(fota, CONFIG_FOTA_LOG_LEVEL);

static uint8_t fota_buf[512];

static void fota_dl_event_handler(const struct fota_download_evt *evt)
{
  switch (evt->id) {
  case FOTA_DOWNLOAD_EVT_ERROR:
    LOG_ERR("fota_download failed");
    break;
  case FOTA_DOWNLOAD_EVT_FINISHED:
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

  return 0;
}