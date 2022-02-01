/*
 * Copyright (c) SAKURA internet Inc.
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>
#include <stdint.h>
#include <stdio.h>

#include <zephyr.h>
#include <modem/at_cmd.h>
#include <modem/nrf_modem_lib.h>
#include <modem/lte_lc.h>
#include <logging/log.h>

#include "gnss/gnss.h"

LOG_MODULE_REGISTER(gnss, CONFIG_SIPF_LOG_LEVEL);

static int gnss_fd;
static char nmea_strings[10][NRF_GNSS_NMEA_MAX_LEN];
static uint32_t nmea_string_cnt;

static bool got_fix;
static uint64_t fix_timestamp;
static nrf_gnss_data_frame_t tmp_gps_pvt;
static nrf_gnss_data_frame_t last_gps_pvt;

static int gnss_ctrl(uint32_t ctrl)
{
    int retval;

    nrf_gnss_fix_retry_t fix_retry = 0;
    nrf_gnss_fix_interval_t fix_interval = 1;
    nrf_gnss_delete_mask_t delete_mask = 0;
    nrf_gnss_nmea_mask_t nmea_mask = NRF_GNSS_NMEA_GSV_MASK | NRF_GNSS_NMEA_GSA_MASK | NRF_GNSS_NMEA_GLL_MASK | NRF_GNSS_NMEA_GGA_MASK | NRF_GNSS_NMEA_RMC_MASK;

    if (ctrl == GNSS_INIT) {
        gnss_fd = nrf_socket(NRF_AF_LOCAL, NRF_SOCK_DGRAM, NRF_PROTO_GNSS);

        if (gnss_fd >= 0) {
            LOG_DBG("GNSS Socket created (%d)", gnss_fd);
        } else {
            LOG_ERR("Could not init socket (err: %d)", gnss_fd);
            return -1;
        }

        retval = nrf_setsockopt(gnss_fd, NRF_SOL_GNSS, NRF_SO_GNSS_FIX_RETRY, &fix_retry, sizeof(fix_retry));
        if (retval != 0) {
            LOG_ERR("Failed to set fix retry value (err: %d)", retval);
            return -1;
        }

        retval = nrf_setsockopt(gnss_fd, NRF_SOL_GNSS, NRF_SO_GNSS_FIX_INTERVAL, &fix_interval, sizeof(fix_interval));
        if (retval != 0) {
            LOG_ERR("Failed to set fix interval value (err: %d)", retval);
            return -1;
        }

        retval = nrf_setsockopt(gnss_fd, NRF_SOL_GNSS, NRF_SO_GNSS_NMEA_MASK, &nmea_mask, sizeof(nmea_mask));
        if (retval != 0) {
            LOG_ERR("Failed to set nmea mask (err: %d)", retval);
            return -1;
        }
    }

    if (ctrl == GNSS_START) {
        retval = nrf_setsockopt(gnss_fd, NRF_SOL_GNSS, NRF_SO_GNSS_START, &delete_mask, sizeof(delete_mask));
        if (retval != 0) {
            LOG_ERR("Failed to start GPS (err: %d)", retval);
            return -1;
        }
    }

    if (ctrl == GNSS_STOP) {
        retval = nrf_setsockopt(gnss_fd, NRF_SOL_GNSS, NRF_SO_GNSS_STOP, &delete_mask, sizeof(delete_mask));
        if (retval != 0) {
            LOG_ERR("Failed to stop GPS (err: %d)", retval);
            return -1;
        }
    }

    return 0;
}

static int process_gps_data(nrf_gnss_data_frame_t *gps_data)
{
    int retval;

    retval = nrf_recv(gnss_fd, gps_data, sizeof(nrf_gnss_data_frame_t), NRF_MSG_DONTWAIT);

    if (retval > 0) {

        switch (gps_data->data_id) {
        case NRF_GNSS_PVT_DATA_ID:
            memcpy(&last_gps_pvt, gps_data, sizeof(nrf_gnss_data_frame_t));
            nmea_string_cnt = 0;
            got_fix = false;

            if (gps_data->pvt.flags & NRF_GNSS_PVT_FLAG_FIX_VALID_BIT) {

                got_fix = true;
                fix_timestamp = k_uptime_get();
            }
            break;

        case NRF_GNSS_NMEA_DATA_ID:
            if (nmea_string_cnt < 10) {
                memcpy(nmea_strings[nmea_string_cnt++], gps_data->nmea, retval);
            }
            break;

        case NRF_GNSS_AGPS_DATA_ID:
            // TODO: SUPL対応
            break;

        default:
            break;
        }
    }

    return retval;
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

void gnss_poll()
{
    do {
        /* Loop until we don't have more
         * data to read
         */
    } while (process_gps_data(&tmp_gps_pvt) > 0);
}

bool gnss_get_data(nrf_gnss_data_frame_t *gps_data)
{
    *gps_data = last_gps_pvt;
    return got_fix;
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
