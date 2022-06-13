/*
 * Copyright (c) 2021 SAKURA internet Inc.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef _SIPF_OBJECT_H_
#define _SIPF_OBJECT_H_

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

#define OBJ_MAX_CNT (255)

#define OBJ_TYPE_UINT8 0x00
#define OBJ_TYPE_INT8 0x01
#define OBJ_TYPE_UINT16 0x02
#define OBJ_TYPE_INT16 0x03
#define OBJ_TYPE_UINT32 0x04
#define OBJ_TYPE_INT32 0x05
#define OBJ_TYPE_UINT64 0x06
#define OBJ_TYPE_INT64 0x07
#define OBJ_TYPE_FLOAT32 0x08
#define OBJ_TYPE_FLOAT64 0x09
#define OBJ_TYPE_BIN_BASE64 0x10
#define OBJ_TYPE_BIN 0x10
#define OBJ_TYPE_STR_UTF8 0x20

typedef enum {
    OBJECTS_UP = 0x00,
    OBJECTS_UP_RETRY = 0x01,
    OBJID_NOTIFICATION = 0x02,

    OBJECTS_DOWN_REQUEST = 0x11,
    OBJECTS_DOWN = 0x12,
    /*
    OBJID_REACH_INQUIRY = 0xff,
    OBJID_REACH_RESULT = 0xff,
    OBJID_REACH_NOTIFICATION = 0xff,
    */
    OBJ_COMMAND_ERR = 0xff,
} SipfObjectCommandType;

typedef struct
{
    SipfObjectCommandType command_type;
    uint64_t command_time;
    uint8_t option_flag;
    uint16_t command_payload_size;
} SipfObjectCommandHeader;

typedef struct
{
    uint8_t obj_type;
    uint8_t obj_tagid;
    uint8_t value_len;
    uint8_t *value;
} SipfObjectObject;

typedef struct
{
    uint8_t obj_qty;
    SipfObjectObject obj;
} SipfObjectUp;

typedef struct
{
    uint8_t value[16];
} SipfObjectOtid;

int SipfObjectParse(uint8_t *raw_buff, const uint16_t raw_len, SipfObjectObject *obj);
int SipfObjectCreateObjUpPayload(uint8_t *raw_buff, uint16_t sz_raw_buff, SipfObjectObject *objs, uint8_t obj_qty);

/* SIPF_OBJクライアント */
int SipfObjClientObjUpRaw(uint8_t *payload_buffer, uint16_t size, SipfObjectOtid *otid);
int SipfObjClientObjUp(const SipfObjectUp *simp_obj_up, SipfObjectOtid *otid);
int SipfObjClientObjDown(SipfObjectOtid *otid, uint8_t *remains, uint8_t *objqty, uint8_t **p_objs, uint8_t **p_user_send_datetime, uint8_t **p_recv_datetime);

#endif