#ifndef _SIPF_OBJECT_H_
#define _SIPF_OBJECT_H_

#include <stdint.h>

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