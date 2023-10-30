/*
 * Copyright (c) SAKURA internet Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>
#include <stdint.h>
#include <stdio.h>

#include <zephyr/kernel.h>
#include <nrf_modem_gnss.h>
#include <nrf_modem_at.h>
#include <modem/nrf_modem_lib.h>
#include <modem/lte_lc.h>
#include <zephyr/logging/log.h>

#include "gnss/gnss.h"

LOG_MODULE_REGISTER(gnss, CONFIG_SIPF_LOG_LEVEL);

static char nmea_strings[10][NRF_MODEM_GNSS_NMEA_MAX_LEN];
static uint32_t nmea_string_cnt;

static struct nrf_modem_gnss_pvt_data_frame last_gps_pvt;

static void on_gnss_evt_nmea(void)
{
	struct nrf_modem_gnss_nmea_data_frame nmea;

	if (nrf_modem_gnss_read((void *)&nmea, sizeof(nmea), NRF_MODEM_GNSS_DATA_NMEA) == 0) {
		LOG_DBG("%s", nmea.nmea_str);
        if (nmea_string_cnt < 10) {
            memcpy(nmea_strings[nmea_string_cnt++], nmea.nmea_str, strlen(nmea.nmea_str)+1);
        }
	}
}

static void on_gnss_evt_pvt(void)
{
	int err;
	err = nrf_modem_gnss_read((void *)&last_gps_pvt, sizeof(last_gps_pvt), NRF_MODEM_GNSS_DATA_PVT);
	if (err) {
		LOG_ERR("Failed to read GNSS PVT data, error %d", err);
		return;
	}
	for (int i = 0; i < NRF_MODEM_GNSS_MAX_SATELLITES; ++i) {
		if (last_gps_pvt.sv[i].sv) { /* SV number 0 indicates no satellite */
			LOG_DBG("SV:%3d sig: %d c/n0:%4d el:%3d az:%3d in-fix: %d unhealthy: %d",
				last_gps_pvt.sv[i].sv, last_gps_pvt.sv[i].signal, last_gps_pvt.sv[i].cn0,
				last_gps_pvt.sv[i].elevation, last_gps_pvt.sv[i].azimuth,
				(last_gps_pvt.sv[i].flags & NRF_MODEM_GNSS_SV_FLAG_USED_IN_FIX) ? 1 : 0,
				(last_gps_pvt.sv[i].flags & NRF_MODEM_GNSS_SV_FLAG_UNHEALTHY) ? 1 : 0);
		}
	}
}

static void gnss_event_handler(int event)
{
	switch (event) {
	case NRF_MODEM_GNSS_EVT_PVT:
        nmea_string_cnt = 0;
        on_gnss_evt_pvt();
		break;
	case NRF_MODEM_GNSS_EVT_NMEA:
        on_gnss_evt_nmea();
		break;
	case NRF_MODEM_GNSS_EVT_FIX:
		LOG_INF("GNSS_EVT_FIX");
		break;
	default:
		break;
	}
}

static int gnss_ctrl(uint32_t ctrl)
{
    int retval;

    uint16_t fix_retry = 0;
    uint16_t fix_interval = 1;
    uint16_t nmea_mask = NRF_MODEM_GNSS_NMEA_GSV_MASK | NRF_MODEM_GNSS_NMEA_GSA_MASK | NRF_MODEM_GNSS_NMEA_GLL_MASK | NRF_MODEM_GNSS_NMEA_GGA_MASK | NRF_MODEM_GNSS_NMEA_RMC_MASK;

    if (ctrl == GNSS_INIT) {

        /* Configure GNSS. */
        retval = nrf_modem_gnss_event_handler_set(gnss_event_handler);
        if (retval != 0) {
            LOG_ERR("Failed to set GNSS event handler (err: %d)", retval);
            return -1;
        }

        retval = nrf_modem_gnss_fix_retry_set(fix_retry);
        if (retval != 0) {
            LOG_ERR("Failed to set fix retry value (err: %d)", retval);
            return -1;
        }

        retval = nrf_modem_gnss_fix_interval_set(fix_interval);
        if (retval != 0) {
            LOG_ERR("Failed to set fix interval value (err: %d)", retval);
            return -1;
        }

        retval = nrf_modem_gnss_nmea_mask_set(nmea_mask);
        if (retval != 0) {
            LOG_ERR("Failed to set nmea mask (err: %d)", retval);
            return -1;
        }
    }

    if (ctrl == GNSS_START) {
        retval = nrf_modem_gnss_start();
        if (retval != 0) {
            LOG_ERR("Failed to start GPS (err: %d)", retval);
            return -1;
        }
    }

    if (ctrl == GNSS_STOP) {
        retval = nrf_modem_gnss_stop();
        if (retval != 0) {
            LOG_ERR("Failed to stop GPS (err: %d)", retval);
            return -1;
        }
    }

    return 0;
}

int gnss_init()
{
    return gnss_ctrl(GNSS_INIT);
}

int gnss_start()
{
    return gnss_ctrl(GNSS_START);
}

int gnss_stop()
{
    return gnss_ctrl(GNSS_STOP);
}


bool gnss_get_data(struct nrf_modem_gnss_pvt_data_frame *gps_data)
{
    memcpy(gps_data, &last_gps_pvt, sizeof(last_gps_pvt));
    return (last_gps_pvt.flags & NRF_MODEM_GNSS_PVT_FLAG_FIX_VALID);
}

int gnss_log_dbg_nmea()
{
    for (int i = 0; i < nmea_string_cnt; ++i) {
        LOG_DBG("%s", nmea_strings[i]);
    }
    return nmea_string_cnt;
}

int gnss_strcpy_nmea(char *dest)
{
    int ret = 0;
    for (int i = 0; i < nmea_string_cnt; ++i) {
        int s = sprintf(dest, "%s", nmea_strings[i]);
        dest += s;
        ret += s;
    }
    return ret;
}
