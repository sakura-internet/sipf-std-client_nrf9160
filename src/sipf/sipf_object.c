#include "sipf/sipf_object.h"

/**
 * Byte列をパースして構造体に詰めるよ
 */
int SipfObjectParse(uint8_t *raw_buff, const uint16_t raw_len, SipfObjectObject *obj)
{
  if (raw_buff == NULL) {
    return -1;
  }
  if (obj == NULL) {
    return -1;
  }
  if (obj->value == NULL) {
    return -1;
  }
  if ((raw_len < 4) || (raw_len > 256)) {
    return -1;
  }
  //バッファから構造体へ
  obj->obj_type = raw_buff[0];
  obj->obj_tagid = raw_buff[1];
  obj->value_len = raw_buff[2];
  memcpy(obj->value, &raw_buff[3], obj->value_len);

  return 0;
}