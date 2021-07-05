/*
 * Copyright (c) 2021 Sakura Internet Inc.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include <net/http_client.h>
#include <net/net_ip.h>
#include <net/socket.h>
#include <net/tls_credentials.h>
#include <sys/base64.h>
#include <logging/log.h>

LOG_MODULE_DECLARE(sipf);

#include "registers.h"
#include "sipf/sipf_object.h"

#define HTTP_BASIC_AUTH_HEADER_PREFIX "Authorization: Basic "
#define NEWLINE_STRING "\r\n"

#define HTTP_PORT 80
#define HTTPS_PORT 443
#define TLS_SEC_TAG 42

#define BUFF_SZ (1024)

static uint8_t req_buff[BUFF_SZ];
static uint8_t res_buff[BUFF_SZ];

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

static void http_response_cb(struct http_response *resp, enum http_final_call final_data, void *user_data)
{
  if (resp->data_len > 0) {
    LOG_DBG("HTTP response has come");
    memcpy(user_data, resp, sizeof(struct http_response));
  }
}

static int createAuthInfoFromRegister(void)
{
  char tmp[162];
  int len, olen;

  if (*REG_00_UN_LEN == 0) {
    return -1;
  }
  if (*REG_00_PW_LEN == 0) {
    return -1;
  }

  memcpy(tmp, REG_00_USER_NAME, *REG_00_UN_LEN);
  tmp[*REG_00_UN_LEN] = ':';
  memcpy(&tmp[*REG_00_UN_LEN + 1], REG_00_PASSWORD, *REG_00_PW_LEN);
  len = *REG_00_UN_LEN + *REG_00_PW_LEN + 1;

  strncpy(req_auth_header, HTTP_BASIC_AUTH_HEADER_PREFIX, strlen(HTTP_BASIC_AUTH_HEADER_PREFIX));
  if (base64_encode(&req_auth_header[21], sizeof(req_auth_header) - 22, &olen, tmp, len) < 0) {
    return -1;
  }
  len = strlen(HTTP_BASIC_AUTH_HEADER_PREFIX) + olen;

  strncpy(&req_auth_header[len], NEWLINE_STRING, strlen(NEWLINE_STRING));
  len += strlen(NEWLINE_STRING);

  req_auth_header[len] = '\0'; // strncpy does not terminate
  return len;
}

static int run_http_request(const char *hostname, struct http_request *req, uint32_t timeout, struct http_response *http_res)
{
  int sock;
  int ret;
  struct addrinfo *res;
  struct addrinfo hints = {
      .ai_family = AF_INET,
      .ai_socktype = SOCK_STREAM,
  };
  // 接続先をセットアップするよ
  ret = getaddrinfo(hostname, NULL, &hints, &res);
  if (ret) {
    LOG_ERR("getaddrinfo failed: ret=%d errno=%d", ret, errno);
    return -errno;
  }
  ((struct sockaddr_in *)res->ai_addr)->sin_port = htons(HTTPS_PORT);

  sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TLS_1_2);
  if (sock < 0) {
    LOG_ERR("socket() failed: ret=%d errno=%d", ret, errno);
    freeaddrinfo(res);
    return -errno;
  }
  // TLSを設定
  ret = tls_setup(sock, hostname);
  if (ret != 0) {
    LOG_ERR("tls_setup() failed: ret=%d", ret);
    freeaddrinfo(res);
    (void)close(sock);
    return -errno;
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

static int run_connector_http_request(const uint8_t *payload, const int payload_len, struct http_response *http_res)
{
  // リクエストを組み立てるよ
  if (*REG_00_MODE == 0x00) {
    //パスワード認証モードの場合
    createAuthInfoFromRegister(); //レジスタから認証情報を生成する
  }
  LOG_DBG("auth: %s", req_auth_header);

  struct http_request req;
  memset(&req, 0, sizeof(req));
  const char *headers[] = {"Connection: Close\r\n", "Content-Type: application/octet-stream\r\n", req_auth_header, NULL};

  req.method = HTTP_POST;
  req.url = CONFIG_SIPF_CONNECTOR_PATH;
  req.host = CONFIG_SIPF_CONNECTOR_HTTP_HOST;
  req.protocol = "HTTP/1.1";
  req.payload = payload;
  req.payload_len = payload_len;
  req.header_fields = headers;
  req.response = http_response_cb;
  req.recv_buf = res_buff;
  req.recv_buf_len = sizeof(res_buff);

  return run_http_request(CONFIG_SIPF_CONNECTOR_HTTP_HOST, &req, 3 * MSEC_PER_SEC, http_res);
}

int run_get_session_key_http_request(struct http_response *http_res)
{
  struct http_request req;
  memset(&req, 0, sizeof(req));
  const char *headers[] = {"Connection: Close\r\n", "Content-Type: text/plain\r\n", "Accept: text/plain\r\n", NULL};

  req.method = HTTP_POST;
  req.url = CONFIG_SIPF_AUTH_PATH;
  req.host = CONFIG_SIPF_AUTH_HOST;
  req.protocol = "HTTP/1.1";
  req.payload = NULL;
  req.payload_len = 0;
  req.header_fields = headers;
  req.response = http_response_cb;
  req.recv_buf = res_buff;
  req.recv_buf_len = sizeof(res_buff);

  return run_http_request(CONFIG_SIPF_AUTH_HOST, &req, 3 * MSEC_PER_SEC, http_res);
}

int SipfClientSetAuthInfo(const char *user_name, const char *passwd)
{
  char tmp1[128], tmp2[128];

  int ilen, olen;

  ilen = sprintf(tmp1, "%s:%s", user_name, passwd);
  if (base64_encode(tmp2, sizeof(tmp2), &olen, tmp1, ilen) < 0) {
    return -1;
  }
  return sprintf(req_auth_header, "Authorization: BASIC %s\r\n", tmp2);
}

int SipfClientGetAuthInfo(void)
{
  int ret;
  static struct http_response http_res;
  ret = run_get_session_key_http_request(&http_res);

  LOG_INF("run_get_session_key_http_request(): %d", ret);
  if (ret < 0) {
    return ret;
  }

  // FIXME: CONFIG_SIPF_LOG_LEVEL をつかっていい感じに抑制する
  LOG_DBG("Response status %s", http_res.http_status);
  LOG_DBG("content-length: %d", http_res.content_length);
  for (int i = 0; i < http_res.content_length; i++) {
    LOG_DBG("0x%02x ", http_res.body_start[i]);
  }
  // FIXME: ここまで

  if (strcmp(http_res.http_status, "OK") != 0) {
    // 200 OK以外のレスポンスが返ってきた
    return -1;
  }

  // レスポンスのフォーマットは USERNAME\nPASSWORD\n
  // FIXME: 例外処理もしっかりやる
  char *user_name = (char *)&http_res.body_start[0];
  char *passwd = NULL;
  for (int i = 0; i < http_res.content_length; i++) {
    if (http_res.body_start[i] == '\n') {
      http_res.body_start[i] = 0;
      if (passwd == NULL) {
        passwd = (char *)&(http_res.body_start[i + 1]);
      }
    }
  }
  LOG_DBG("user_name=%s passwd=%s", user_name, passwd);
  if (passwd != NULL) {
    return SipfClientSetAuthInfo(user_name, passwd);
  }
  return -1;
}

int SipfClientObjUp(const SipfObjectUp *simp_obj_up, SipfObjectOtid *otid)
{
  int ret;
  uint16_t sz;

  if (simp_obj_up->obj.value == NULL) {
    return -1;
  }

  uint16_t sz_packet = simp_obj_up->obj.value_len + 12 + 3;

  // COMMAND_TYPE
  req_buff[0] = (uint8_t)OBJECTS_UP;
  // COMMAND_TIME
  req_buff[1] = 0x00;
  req_buff[2] = 0x00;
  req_buff[3] = 0x00;
  req_buff[4] = 0x00;
  req_buff[5] = 0x00;
  req_buff[6] = 0x00;
  req_buff[7] = 0x00;
  req_buff[8] = 0x00;
  // OPTION_FLAG
  req_buff[9] = 0x00;
  // PAYLOAD_SIZE(BigEndian)
  sz = simp_obj_up->obj.value_len + 3;
  req_buff[10] = sz >> 8;
  req_buff[11] = sz & 0xff;

  // payload: OBJECTS_UP
  uint8_t *payload = &req_buff[12];
  // OBJ
  //  OBJ_TYPEID
  payload[0] = simp_obj_up->obj.obj_type;
  //  OBJ_TAGID
  payload[1] = simp_obj_up->obj.obj_tagid;
  //  OBJ_LENGTH
  payload[2] = simp_obj_up->obj.value_len;
  //  OBJ_VALUE
  memcpy(&payload[3], simp_obj_up->obj.value, simp_obj_up->obj.value_len);

  static struct http_response http_res;
  ret = run_connector_http_request(req_buff, sz_packet, &http_res);

  LOG_INF("run_connector_http_request(): %d", ret);
  if (ret < 0) {
    return ret;
  }

  LOG_DBG("Response status %s", http_res.http_status);
  LOG_DBG("content-length: %d", http_res.content_length);
  for (int i = 0; i < http_res.content_length; i++) {
    LOG_DBG("0x%02x ", http_res.body_start[i]);
  }

  // OBJID_NOTIFICATIONをパース
  uint8_t *sipf_obj_head = &http_res.body_start[0];
  uint8_t *sipf_obj_payload = &http_res.body_start[12];
  if (strcmp(http_res.http_status, "OK") != 0) {
    if (strcmp(http_res.http_status, "Unauthorized") == 0) {
      return -401;
    }
    // OK以外
    return -1;
  }

  if (http_res.content_length != 30) {
    // ペイロード長が合わない
    return -1;
  }

  if (sipf_obj_head[0] != 0x02) {
    // OBJID_NOTIFICATIONじゃない
    return -1;
  }

  if (sipf_obj_payload[0] != 0x00) {
    // ResultがOKじゃない
    return -1;
  }

  // OTIDを取得
  memcpy(&otid->value, &sipf_obj_payload[2], sizeof(otid->value));

  return 0;
}
