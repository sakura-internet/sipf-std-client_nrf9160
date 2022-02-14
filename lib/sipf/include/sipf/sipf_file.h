/**
 * Copyright (c) 2022 SAKURA internet Inc.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef SIPF_FILE_H
#define SIPF_FILE_H

#include <stdint.h>
#include <stdbool.h>
#include <net/socket.h>
#include <net/download_client.h>
#include <net/http_client.h>

int SipfFileRequestDownloadURL(const char *file_id, char *url, int sz_url);
int SipfFileRequestUploadURL(const char *file_id, char *url, int sz_url);
int SipfFileUploadComplete(const char *file_id);

int SipfFileUpload(char *file_id, uint8_t *buff, http_payload_cb_t cb, int sz_payload);

typedef int (*sipfFileDownload_cb_t)(uint8_t *buff, size_t len);
int SipfFileDownload(const char *file_id, uint8_t *buff, size_t sz_download, sipfFileDownload_cb_t cb);

#endif
