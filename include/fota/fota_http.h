/*
 * Copyright (c) 2021 Sakura Internet Inc.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FOTA_HTTP_H
#define FOTA_HTTP_H

#include <dfu/mcuboot.h>
#include <dfu/dfu_target_mcuboot.h>
#include <net/fota_download.h>

#define DOWNLOAD_HOST   "sipf.iot.sakura.ad.jp"
#define DOWNLOAD_FILE   "/ota/sipf-std-client_latest.bin"
//#define DOWNLOAD_HOST   "153.127.217.173"
//#define DOWNLOAD_FILE   "app_update.bin"

int FotaHttpRun(void);

#endif