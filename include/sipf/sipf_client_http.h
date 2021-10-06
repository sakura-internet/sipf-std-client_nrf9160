#ifndef SIPF_CLIENT_HTTP_H
#define SIPF_CLIENT_HTTP_H

#include "sipf/sipf_object.h"

int SipfClientGetAuthInfo(void);
int SipfClientSetAuthInfo(const char *user_name, const char *passwd);
int SipfCLientObjUpRaw(uint8_t *payload_buffer, uint16_t size, SipfObjectOtid *otid);
int SipfClientObjUp(const SipfObjectUp *simp_obj_up, SipfObjectOtid *otid);
int SipfClientObjDown(SipfObjectOtid *otid, uint8_t *remains, uint8_t *objqty, uint8_t **p_objs, uint8_t **p_user_send_datetime, uint8_t **p_recv_datetime);

#endif
