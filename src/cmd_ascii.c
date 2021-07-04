/*
 * Copyright (c) 2021 Sakura Internet Inc.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <zephyr.h>

#include "cmd_ascii.h"
#include "debug_print.h"
#include "registers.h"
#include "fota/fota_http.h"
#include "sipf/sipf_client_http.h"
#include "gnss/gnss.h"

static uint8_t buff_work[256];
/**/
typedef int (*ascii_cmd_func)(uint8_t *in_buff, uint16_t in_len, uint8_t *out_buff, uint16_t out_buff_len);
typedef struct
{
  char *cmd_name;
  ascii_cmd_func cmd_func;
} CmdAsciiCmd;
/**/

static bool is_unlocked = false;

/**
 * 2桁の16進文字列をUINT8に変換する
 */
static uint8_t hexToUint8(uint8_t *pchars, uint8_t *err)
{
  uint8_t ret = 0;
  if (err) {
    *err = 0;
  }

  for (int i = 0; i < 2; i++) {
    if ((pchars[i] >= (uint8_t)'0') && (pchars[i] <= (uint8_t)'9')) {
      ret |= (pchars[i] - (uint8_t)'0') << (i == 0 ? 4 : 0);
    } else if ((pchars[i] >= (uint8_t)'a') && (pchars[i] <= (uint8_t)'f')) {
      ret |= (pchars[i] - (uint8_t)'a' + 10) << (i == 0 ? 4 : 0);
    } else if ((pchars[i] >= (uint8_t)'A') && (pchars[i] <= (uint8_t)'F')) {
      ret |= (pchars[i] - (uint8_t)'A' + 10) << (i == 0 ? 4 : 0);
    } else {
      if (err) {
        ret = 0;
        *err = 0xff;
      }
    }
  }
  return ret;
}

static int cmdCreateResIllParam(uint8_t *out_buff, uint16_t out_buff_len) { return snprintf(out_buff, out_buff_len, "ILLIGAL PARAMETER\r\nNG\r\n"); }

static int cmdCreateResOk(uint8_t *out_buff, uint16_t out_buff_len) { return snprintf(out_buff, out_buff_len, "OK\r\n"); }

static int cmdCreateResNg(uint8_t *out_buff, uint16_t out_buff_len) { return snprintf(out_buff, out_buff_len, "NG\r\n"); }

static int cmdAsciiCmdW(uint8_t *in_buff, uint16_t in_len, uint8_t *out_buff, uint16_t out_buff_len)
{
  is_unlocked = false;

  if (in_len != 6) {
    // lengthが合わない
    return cmdCreateResIllParam(out_buff, out_buff_len);
  }

  if (in_buff[3] != 0x20) {
    // ADDRとVALUEの区切りがスペースじゃない
    return cmdCreateResIllParam(out_buff, out_buff_len);
  }

  if (in_buff[0] != 0x20) {
    // 先頭がスペースじゃない
    return CMD_RES_ILLPARM;
  }

  //文字列として処理できるように区切りをNull文字にする
  in_buff[3] = 0x00;
  in_buff[6] = 0x00;

  char *endptr;
  uint8_t addr, val;

  addr = strtol((char *)&in_buff[1], &endptr, 16);
  if (*endptr != '\0') {
    // Null文字以外で変換が終わってる
    return cmdCreateResIllParam(out_buff, out_buff_len);
  }

  val = strtol((char *)&in_buff[4], &endptr, 16);
  if (*endptr != '\0') {
    // Null文字以外で変換が終わってる
    return cmdCreateResIllParam(out_buff, out_buff_len);
  }

  if (RegistersWrite(addr, val) < 0) {
    // 書き込みエラー
    return cmdCreateResNg(out_buff, out_buff_len);
  }

  return cmdCreateResOk(out_buff, out_buff_len);
}

static int cmdAsciiCmdR(uint8_t *in_buff, uint16_t in_len, uint8_t *out_buff, uint16_t out_buff_len)
{
  is_unlocked = false;
  if (in_len != 3) {
    // lengthが合わない
    return cmdCreateResIllParam(out_buff, out_buff_len);
  }
  if (in_buff[0] != 0x20) {
    // 先頭がスペースじゃない
    return cmdCreateResIllParam(out_buff, out_buff_len);
  }
  //文字列として処理できるように区切りをNull文字にする
  in_buff[3] = 0x00;

  char *endptr;
  uint8_t addr, val;

  addr = strtol((char *)&in_buff[1], &endptr, 16);
  if (*endptr != '\0') {
    // Null文字以外で変換が終わってる
    return cmdCreateResIllParam(out_buff, out_buff_len);
  }

  if (RegistersRead(addr, &val) < 0) {
    // 読み出しエラー
    return cmdCreateResIllParam(out_buff, out_buff_len);
  }

  return sprintf(out_buff, "%02x\r\nOK\r\n", val);
}

static int cmdAsciiCmdTx(uint8_t *in_buff, uint16_t in_len, uint8_t *out_buff, uint16_t out_buff_len)
{
  is_unlocked = false;

  if (in_len < 8) {
    // valueまでの長さが無い
    return cmdCreateResIllParam(out_buff, out_buff_len);
  }

  if (in_buff[0] != 0x20) {
    // 先頭がスペースじゃない
    return cmdCreateResIllParam(out_buff, out_buff_len);
  }

  if (in_buff[3] != 0x20) {
    // TAG_IDとTYPEの区切り位置がスペースじゃない
    return cmdCreateResIllParam(out_buff, out_buff_len);
  }

  if (in_buff[6] != 0x20) {
    // TYPEとVALUEの区切り位置がスペースじゃない
    return cmdCreateResIllParam(out_buff, out_buff_len);
  }

  // 文字列として処理できるように区切りのスペースをNull文字にする
  in_buff[3] = 0x00;
  in_buff[6] = 0x00;
  in_buff[in_len] = 0x00;

  char *endptr;
  uint8_t tag_id, type_id;
  uint8_t *top_value;

  tag_id = strtol((char *)&in_buff[1], &endptr, 16);
  if (*endptr != '\0') {
    // Null文字以外で変換が終わってる
    return cmdCreateResIllParam(out_buff, out_buff_len);
  }
  type_id = strtol((char *)&in_buff[4], &endptr, 16);
  if (*endptr != '\0') {
    // Null文字以外で変換が終わってる
    return cmdCreateResIllParam(out_buff, out_buff_len);
  }

  top_value = &in_buff[7];
  int val_str_len = strlen(top_value);
  SipfObjectUp objup;
  objup.obj.obj_tagid = tag_id;
  objup.obj.obj_type = type_id;
  DebugPrint("tag_id: 0x%02x, type: 0x%02x, val=%s\r\n", tag_id, type_id, top_value);
  switch (type_id) {
  case OBJ_TYPE_UINT8:
  case OBJ_TYPE_INT8:
    if (val_str_len != 2) {
      return cmdCreateResIllParam(out_buff, out_buff_len);
    }
    break;
  case OBJ_TYPE_UINT16:
  case OBJ_TYPE_INT16:
    if (val_str_len != 4) {
      return cmdCreateResIllParam(out_buff, out_buff_len);
    }
    break;
  case OBJ_TYPE_UINT32:
  case OBJ_TYPE_INT32:
  case OBJ_TYPE_FLOAT32:
    if (val_str_len != 8) {
      return cmdCreateResIllParam(out_buff, out_buff_len);
    }
    break;
  case OBJ_TYPE_UINT64:
  case OBJ_TYPE_INT64:
  case OBJ_TYPE_FLOAT64:
    if (val_str_len != 16) {
      return cmdCreateResIllParam(out_buff, out_buff_len);
    }
    break;
  case OBJ_TYPE_BIN_BASE64:
  case OBJ_TYPE_STR_UTF8:
    if ((val_str_len % 2) != 0) {
      // 奇数文字数はエラー
      return cmdCreateResIllParam(out_buff, out_buff_len);
    }
    break;
  default:
    // TYPEが範囲外
    return cmdCreateResIllParam(out_buff, out_buff_len);
  }

  uint8_t err;
  for (int i = 0; i < (val_str_len / 2); i++) {
    buff_work[i] = hexToUint8(&top_value[i * 2], &err);
    if (err != 0) {
      // 変換に失敗
      return cmdCreateResIllParam(out_buff, out_buff_len);
    }
    DebugPrint("0x%02x\r\n", buff_work[i]);
  }

  objup.obj.value_len = val_str_len / 2;
  objup.obj.value = buff_work;

  SipfObjectOtid otid;
  err = SipfClientObjUp(&objup, &otid);
  int len = 0;
  if (err == 0) {
    for (int i = 0; i < sizeof(otid.value); i++) {
      len += sprintf(&out_buff[i * 2], "%02X", otid.value[i]);
    }
    len += sprintf(&out_buff[len], "\r\nOK\r\n");
  } else {
    DebugPrint("SipfClientObjUp() failed: %d\r\n", err);
    return cmdCreateResNg(out_buff, out_buff_len);
  }
  return len;
}

/**
 * $$UNLOCKコマンド
 * in_buff: コマンド名より後ろを格納してるバッファ
 */
static int cmdAsciiCmdUnlock(uint8_t *in_buff, uint16_t in_len, uint8_t *out_buff, uint16_t out_buff_len)
{
  if (in_buff[0] != 0x20) {
    // 先頭がスペースじゃない
    return cmdCreateResIllParam(out_buff, out_buff_len);
  }
  if (in_len != 7) {
    // パラメータ長が違う
    return cmdCreateResIllParam(out_buff, out_buff_len);
  }

  int res_len = 0;
  if (memcmp(" UNLOCK", in_buff, 7) == 0) {
    is_unlocked = true;
    res_len = cmdCreateResOk(out_buff, out_buff_len);
  } else {
    // パラメータが”UNLOCK”じゃない
    is_unlocked = false;
    res_len = cmdCreateResIllParam(out_buff, out_buff_len);
  }
  return res_len;
}

/**
 * $$UPDATEコマンド
 * in_buff: コマンド名より後ろを格納してるバッファ
 */
static int cmdAsciiCmdUpdate(uint8_t *in_buff, uint16_t in_len, uint8_t *out_buff, uint16_t out_buff_len)
{
  if (is_unlocked == false) {
    // UNLOCKされてない
    return cmdCreateResNg(out_buff, out_buff_len);
  }
  if (in_len != 7) {
    // パラメータ長が違う
    return cmdCreateResIllParam(out_buff, out_buff_len);
  }
  if (memcmp(" UPDATE", in_buff, 7) == 0) {
    if (FotaHttpRun() != 0) {
      // FOTA失敗した
      return cmdCreateResNg(out_buff, out_buff_len);
    }
    // FOTA成功ならリセットかかるからここには来ないはず
    return cmdCreateResOk(out_buff, out_buff_len);
  } else {
    // パラメータが”UPDATEじゃない”
    return cmdCreateResIllParam(out_buff, out_buff_len);
  }
}

/**
 * $$GNSSEN コマンド
 * in_buff: コマンド名より後ろを格納してるバッファ
 */
static int cmdAsciiCmdGnssEnable(uint8_t *in_buff, uint16_t in_len, uint8_t *out_buff, uint16_t out_buff_len)
{
  if (in_buff[0] != 0x20) {
    // 先頭がスペースじゃない
    return cmdCreateResIllParam(out_buff, out_buff_len);
  }
  if (in_len != 2) {
    // パラメータ長が違う
    return cmdCreateResIllParam(out_buff, out_buff_len);
  }

  if (in_buff[1] == '0') {
    // GPS無効
    if (gnss_stop() != 0) {
      return cmdCreateResNg(out_buff, out_buff_len);
    }
  } else if (in_buff[1] == '1') {
    // GPS有効
    if (gnss_start() != 0) {
      return cmdCreateResNg(out_buff, out_buff_len);
    }
  } else {
    return cmdCreateResIllParam(out_buff, out_buff_len);
  }

  return cmdCreateResOk(out_buff, out_buff_len);
}

/**
 * $$GNSSSTAT コマンド
 * in_buff: コマンド名より後ろを格納してるバッファ
 */
static int cmdAsciiCmdGnssStatus(uint8_t *in_buff, uint16_t in_len, uint8_t *out_buff, uint16_t out_buff_len)
{
  if (in_len != 0) {
    // パラメータ長が違う
    return cmdCreateResIllParam(out_buff, out_buff_len);
  }

  nrf_gnss_data_frame_t gps_data;
  gnss_get_data(&gps_data);

  uint8_t *buff = out_buff;

  buff += sprintf(buff, "Fix valid: %s\n", (gps_data.pvt.flags & NRF_GNSS_PVT_FLAG_FIX_VALID_BIT) == NRF_GNSS_PVT_FLAG_FIX_VALID_BIT ? "true" : "false");
  buff += sprintf(buff, "Leap second valid: %s\n", (gps_data.pvt.flags & NRF_GNSS_PVT_FLAG_LEAP_SECOND_VALID) == NRF_GNSS_PVT_FLAG_LEAP_SECOND_VALID ? "true" : "false");
  buff += sprintf(buff, "Sleep between PVT: %s\n", (gps_data.pvt.flags & NRF_GNSS_PVT_FLAG_SLEEP_BETWEEN_PVT) == NRF_GNSS_PVT_FLAG_SLEEP_BETWEEN_PVT ? "true" : "false");
  buff += sprintf(buff, "Deadline missed: %s\n", (gps_data.pvt.flags & NRF_GNSS_PVT_FLAG_DEADLINE_MISSED) == NRF_GNSS_PVT_FLAG_DEADLINE_MISSED ? "true" : "false");
  buff += sprintf(buff, "Insuf. time window: %s\n", (gps_data.pvt.flags & NRF_GNSS_PVT_FLAG_NOT_ENOUGH_WINDOW_TIME) == NRF_GNSS_PVT_FLAG_NOT_ENOUGH_WINDOW_TIME ? "true" : "false");

  buff += sprintf(buff, "OK\n");
  return (int)(buff - out_buff);
}

/**
 * $$GNSSLOC コマンド
 * in_buff: コマンド名より後ろを格納してるバッファ
 */
static int cmdAsciiCmdGnssLocation(uint8_t *in_buff, uint16_t in_len, uint8_t *out_buff, uint16_t out_buff_len)
{
  if (in_len != 0) {
    // パラメータ長が違う
    return cmdCreateResIllParam(out_buff, out_buff_len);
  }

  bool got_fix;
  nrf_gnss_data_frame_t gps_data;
  got_fix = gnss_get_data(&gps_data);

  uint8_t *buff = out_buff;

  if (!got_fix) {
    // NOTFIXED
    buff += sprintf(buff, "V,");
  } else {
    // FIXED
    buff += sprintf(buff, "A,");
  }
  buff += sprintf(buff, "%.6f,%.6f,%f,%f,%f,%04u-%02u-%02uT%02u:%02u:%02uZ", gps_data.pvt.longitude, gps_data.pvt.latitude, gps_data.pvt.altitude, gps_data.pvt.speed, gps_data.pvt.heading, gps_data.pvt.datetime.year, gps_data.pvt.datetime.month, gps_data.pvt.datetime.day, gps_data.pvt.datetime.hour, gps_data.pvt.datetime.minute, gps_data.pvt.datetime.seconds);
  buff += sprintf(buff, "\r\nOK\r\n");
  return (int)(buff - out_buff);
}

/**
 * $$GNSSNMEA コマンド
 * in_buff: コマンド名より後ろを格納してるバッファ
 */
static int cmdAsciiCmdGnssNmea(uint8_t *in_buff, uint16_t in_len, uint8_t *out_buff, uint16_t out_buff_len)
{
  if (in_len != 0) {
    // パラメータ長が違う
    return cmdCreateResIllParam(out_buff, out_buff_len);
  }
  uint8_t *buff = out_buff;
  buff += gnss_strcpy_nmea((char *)out_buff);
  buff += sprintf(buff, "\r\nOK\r\n");
  return (int)(buff - out_buff);
}

static CmdAsciiCmd cmdfunc[] = {{CMD_REG_W, cmdAsciiCmdW}, {CMD_REG_R, cmdAsciiCmdR}, {CMD_TX, cmdAsciiCmdTx}, {CMD_UNLOCK, cmdAsciiCmdUnlock}, {CMD_UPDATE, cmdAsciiCmdUpdate}, {CMD_GNSS_ENABLE, cmdAsciiCmdGnssEnable}, {CMD_GNSS_GET_LOCATION, cmdAsciiCmdGnssLocation}, {CMD_GNSS_GET_NMEA, cmdAsciiCmdGnssNmea}, {CMD_GNSS_GET_STATUS, cmdAsciiCmdGnssStatus}, {NULL, NULL}};

int CmdAsciiParse(uint8_t *in_buff, uint16_t in_len, uint8_t *out_buff, uint16_t out_buff_len)
{
  int idx = 0;
  // コマンドリストを探す
  while (cmdfunc[idx].cmd_func) {
    //コマンド名に対応する関数を呼ぶ
    int name_len = strlen(cmdfunc[idx].cmd_name);
    if (memcmp(in_buff, cmdfunc[idx].cmd_name, name_len) == 0) {
      //コマンド名の次から末尾までのバッファを渡す(先頭はスペースのハズ)
      return cmdfunc[idx].cmd_func(&in_buff[name_len], in_len - name_len, out_buff, out_buff_len);
    }
    idx++;
  }

  // 未定義のコマンド
  return cmdCreateResNg(out_buff, out_buff_len);
}
