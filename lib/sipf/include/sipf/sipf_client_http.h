/*
 * Copyright (c) 2021 SAKURA internet Inc.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef SIPF_CLIENT_HTTP_H
#define SIPF_CLIENT_HTTP_H

#include <net/http_client.h>
#include "sipf/sipf_object.h"

#define HTTP_PORT 80
#define HTTPS_PORT 443
#define TLS_SEC_TAG 42

#define BUFF_SZ (1500)

extern uint8_t httpc_req_buff[BUFF_SZ];
extern uint8_t httpc_res_buff[BUFF_SZ];

int SipfClientHttpSetAuthInfo(const char *user_name, const char *passwd);
char *SipfClientHttpGetAuthInfo(void);

int SipfClientHttpRunRequest(const char *hostname, struct http_request *req, uint32_t timeout, struct http_response *http_res, bool tls);

int SipfClientHttpParseURL(char *url, const int url_len, char **protocol, char **host, char **path);
#endif
