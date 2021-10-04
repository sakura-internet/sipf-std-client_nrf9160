#ifndef SIPF_CLIENT_HTTP_H
#define SIPF_CLIENT_HTTP_H

#include "sipf/sipf_object.h"

int SipfClientGetAuthInfo(void);
int SipfClientSetAuthInfo(const char *user_name, const char *passwd);
int SipfCLientObjUpRaw(uint8_t *payload_buffer, uint16_t size, SipfObjectOtid *otid);
int SipfClientObjUp(const SipfObjectUp *simp_obj_up, SipfObjectOtid *otid);

#endif
