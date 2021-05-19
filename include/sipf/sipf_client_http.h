#ifndef SIPF_CLIENT_HTTP_H
#define SIPF_CLIENT_HTTP_H

#include "sipf/sipf_object.h"

int SipfClientSetAuthInfo(const char *user_name, const char *passwd);
int SipfClientObjUp(const SipfObjectUp *simp_obj_up, SipfObjectOtid *otid);

#endif