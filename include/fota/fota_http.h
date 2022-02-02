/*
 * Copyright (c) 2021 SAKURA internet Inc.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FOTA_HTTP_H
#define FOTA_HTTP_H

#include <dfu/dfu_target_mcuboot.h>
#include <dfu/mcuboot.h>
#include <net/fota_download.h>

#define FOTA_BUFF_SZ (512)
#define IMAGE_URL_LEN (256)

#ifdef CONFIG_SIPF_FOTA_TLS
#define FOTA_SEC_TAG 42
#else
#define FOTA_SEC_TAG -1
#endif

int FotaHttpRun(char *file_name_suffix);

#endif