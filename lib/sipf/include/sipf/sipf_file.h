/**
 * Copyright (c) 2022 SAKURA internet Inc.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef SIPF_FILE_H
#define SIPF_FILE_H

#include <stdint.h>
#include <stdbool.h>
#include <net/download_client.h>
#include <net/http_client.h>

int SipfFileRequestDownloadURL(const char *file_id, char *url, int sz_url);
int SipfFileRequestUploadURL(const char *file_id, char *url, int sz_url);
int SipfFileUploadComplete(const char *file_id);

int SipfFileUpload(char *file_id, uint8_t *buff, int sz_buff);
int SipfFileDownload(const char *file_id, uint8_t *buff, size_t sz_buff);
#if 0
struct sipf_file_upload_evt (
    _SIPF_FILE_UPLOAD_EVT_CNT_
);

typedef int (*sipf_file_upload_callback_t)(
	const struct sipf_file_upload_evt *event);


int SipfFileSetAuth(const char *username, const char *password);

/**
 * Simple Upload/Download
 */
int SipfFileUpload(uint8_t *buff, size_t sz_buff);
int SipfFileDownload(uint8_t *buff, size_t sz_buff);

/**
 * 
 */
int SipfFileUploadBegin(size_t sz_file, );
int SipfFileUploadPut(uint8_t *buff, size_t sz_buff);
int SipfFileUploadEnd(void);

int SipfFileDownloadBegin(download_client_callback_t callback);
#endif

#endif