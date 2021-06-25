/*
 * Copyright (c) 2021 Sakura Internet Inc.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef FOTA_HTTP_H
#define FOTA_HTTP_H

#include <dfu/dfu_target_mcuboot.h>
#include <dfu/mcuboot.h>
#include <net/fota_download.h>

int FotaHttpRun(void);

#endif