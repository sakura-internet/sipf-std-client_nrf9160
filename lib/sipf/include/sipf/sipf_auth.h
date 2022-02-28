/*
 * Copyright (c) 2021 SAKURA internet Inc.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef _SIPF_AUTH_H_
#define _SIPF_AUTH_H_

#include <stdlib.h>
#include <stdint.h>
#include <string.h>

int SipfAuthRequest(char *user_name, uint8_t sz_user_name, char *password, uint8_t sz_password);

#endif