#ifndef _SIPF_OBJECT_H_
#define _SIPF_OBJECT_H_

#include <stdint.h>

#define OBJ_TYPE_UINT8      0x00
#define OBJ_TYPE_INT8       0x01
#define OBJ_TYPE_UINT16     0x02
#define OBJ_TYPE_INT16      0x03
#define OBJ_TYPE_UINT32     0x04
#define OBJ_TYPE_INT32      0x05
#define OBJ_TYPE_UINT64     0x06
#define OBJ_TYPE_INT64      0x07
#define OBJ_TYPE_FLOAT32    0x08
#define OBJ_TYPE_FLOAT64    0x09
#define OBJ_TYPE_BIN_BASE64 0x10
#define OBJ_TYPE_STR_UTF8   0x20

typedef enum {
	OBJECTS_UP               = 0x00,
	OBJECTS_UP_RETRY         = 0x01,
	OBJID_NOTIFICATION       = 0x02,
	OBJID_REACH_INQUIRY      = 0x03,
	OBJID_REACH_RESULT       = 0x04,
	OBJECTS_DOWN             = 0x05,
	OBJID_REACH_NOTIFICATION = 0x06,
	OBJ_COMMAND_ERR          = 0xff,
}   SipfObjectCommandType;

typedef struct {
    SipfObjectCommandType   command_type;
    uint64_t                command_time;
    uint8_t                 option_flag;
    uint16_t                command_payload_size;
}   SipfObjectCommandHeader;


typedef struct {
    uint8_t     obj_type;
    uint8_t     obj_tagid;
    uint8_t     *value;
    uint16_t    value_len;
}   SipfObjectObject;

typedef struct {
    uint8_t             obj_qty;
    SipfObjectObject    obj;
}   SipfObjectUp;

typedef struct {
    uint8_t value[16];
}   SipfObjectOtid;
#endif