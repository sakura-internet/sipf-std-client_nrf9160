/*
 * Copyright (c) SAKURA internet Inc.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef GNSS_H
#define GNSS_H

#include <nrf_socket.h>

#define GNSS_INIT 0
#define GNSS_START 1
#define GNSS_STOP 2

int gnss_setup_modem();
int gnss_init();
int gnss_start();
int gnss_stop();
void gnss_poll();
bool gnss_get_data(nrf_gnss_data_frame_t *gps_data); // TRUE: fixed, FALSE: not fixed
int gnss_log_dbg_nmea();
int gnss_strcpy_nmea(char *dest);

#endif // GNSS_H
