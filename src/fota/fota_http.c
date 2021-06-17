/*
 * Copyright (c) 2021 Sakura Internet Inc.
 *
 * SPDX-License-Identifier: MIT
 */
#include <power/reboot.h>
#include <zephyr.h>

#include "debug_print.h"
#include "fota/fota_http.h"

static uint8_t fota_buf[512];

static void fota_dl_event_handler(const struct fota_download_evt *evt) {
  switch (evt->id) {
  case FOTA_DOWNLOAD_EVT_ERROR:
    DebugPrint(ERR "fota_download failed...\r\n");
    break;
  case FOTA_DOWNLOAD_EVT_FINISHED:
    DebugPrint(INFO "fota_download finished.\r\n");
    sys_reboot(SYS_REBOOT_COLD);
    break;
  default:
    break;
  }
}

int FotaHttpRun(void) {
  int ret;
  // DFUライブラリにバッファを設定
  ret = dfu_target_mcuboot_set_buf(fota_buf, sizeof(fota_buf));
  if (ret != 0) {
    DebugPrint(ERR "dfu_target_mcuboot_set_buf() failed: %d\r\n", ret);
    return ret;
  }
  // FOTA Downloadライブラリを初期化
  ret = fota_download_init(fota_dl_event_handler);
  if (ret != 0) {
    DebugPrint(ERR "fota_download_init() failed: %d\r\n", ret);
    return ret;
  }
  // FOTA開始(HTTPで)
  ret = fota_download_start(DOWNLOAD_HOST, DOWNLOAD_FILE, -1, NULL, 0);
  if (ret != 0) {
    DebugPrint(ERR "fota_download_start() failed: %d\r\n", ret);
    return ret;
  }

  return 0;
}