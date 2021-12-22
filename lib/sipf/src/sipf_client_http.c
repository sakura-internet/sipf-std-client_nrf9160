/*
 * Copyright (c) 2021 SAKURA internet Inc.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include <net/net_ip.h>
#include <net/socket.h>
#include <net/tls_credentials.h>
#include <sys/base64.h>
#include <logging/log.h>

LOG_MODULE_DECLARE(sipf);

#include "sipf/sipf_client_http.h"
#include "sipf/sipf_object.h"

#define HTTP_BASIC_AUTH_HEADER_PREFIX "Authorization: Basic "
#define NEWLINE_STRING "\r\n"

#define HTTP_PORT 80
#define HTTPS_PORT 443
#define TLS_SEC_TAG 42

uint8_t httpc_req_buff[BUFF_SZ];
uint8_t httpc_res_buff[BUFF_SZ];

static char req_auth_header[256];

/* Setup TLS options on a given socket */
static int tls_setup(int fd, const char *host_name)
{
  int err;
  int verify;

  /* Security tag that we have provisioned the certificate with */
  const sec_tag_t tls_sec_tag[] = {
      TLS_SEC_TAG,
  };

  /* Set up TLS peer verification */
  enum
  {
    NONE = 0,
    OPTIONAL = 1,
    REQUIRED = 2,
  };

  verify = REQUIRED;

  err = setsockopt(fd, SOL_TLS, TLS_PEER_VERIFY, &verify, sizeof(verify));
  if (err) {
    LOG_ERR("Failed to setup peer verification, err %d", errno);
    return err;
  }

  /* Associate the socket with the security tag
   * we have provisioned the certificate with.
   */
  err = setsockopt(fd, SOL_TLS, TLS_SEC_TAG_LIST, tls_sec_tag, sizeof(tls_sec_tag));
  if (err) {
    LOG_ERR("Failed to setup TLS sec tag, err %d", errno);
    return err;
  }

  err = setsockopt(fd, SOL_TLS, TLS_HOSTNAME, host_name, strlen(host_name));
  if (err) {
    LOG_ERR("Failed to Set TLS Hostname, err %d", errno);
    return err;
  }

  return 0;
}

/** HTTP Client **/

int SipfClientHttpRunRequest(const char *hostname, struct http_request *req, uint32_t timeout, struct http_response *http_res, bool tls)
{
  int sock;
  int ret;
  struct addrinfo *res;
  struct addrinfo hints = {
      .ai_family = AF_INET, .ai_socktype = SOCK_STREAM,
  };
  // 接続先をセットアップするよ
  ret = getaddrinfo(hostname, NULL, &hints, &res);
  if (ret) {
    LOG_ERR("getaddrinfo failed: ret=%d errno=%d", ret, errno);
    return -errno;
  }
  if (tls) {
    ((struct sockaddr_in *)res->ai_addr)->sin_port = htons(HTTPS_PORT);
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TLS_1_2);
  } else {
    ((struct sockaddr_in *)res->ai_addr)->sin_port = htons(HTTP_PORT);
    sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);

  }
  if (sock < 0) {
    LOG_ERR("socket() failed: ret=%d errno=%d", ret, errno);
    freeaddrinfo(res);
    return -errno;
  }
  if (tls) {
    // TLSを設定
    ret = tls_setup(sock, hostname);
    if (ret != 0) {
      LOG_ERR("tls_setup() failed: ret=%d", ret);
      freeaddrinfo(res);
      (void)close(sock);
      return -errno;
    }
  }
  // 接続するよ
  LOG_INF("Connect to %s:%d", hostname, HTTPS_PORT);
  ret = connect(sock, res->ai_addr, sizeof(struct sockaddr_in));
  if (ret) {
    LOG_ERR("connect() failed: ret=%d errno=%d", ret, errno);
    freeaddrinfo(res);
    (void)close(sock);
    return -errno;
  }

  ret = http_client_req(sock, req, 3 * MSEC_PER_SEC, http_res);
  freeaddrinfo(res);
  close(sock);
  return ret;
}

/**
 * Authorizationヘッダの文字列バッファへのポインタを返す
 */
char *SipfClientHttpGetAuthInfo(void)
{
  return req_auth_header;
}

/**
 * user_nameとpasswdからAuthorizationヘッダ文字列を生成してバッファに置く
 */
int SipfClientHttpSetAuthInfo(const char *user_name, const char *passwd)
{
  char tmp1[128], tmp2[128];

  int ilen, olen;

  ilen = sprintf(tmp1, "%s:%s", user_name, passwd);
  LOG_DBG("%s", tmp1);
  if (base64_encode(tmp2, sizeof(tmp2), &olen, tmp1, ilen) < 0) {
    return -1;
  }
  return sprintf(req_auth_header, "Authorization: BASIC %s\r\n", tmp2);
}
