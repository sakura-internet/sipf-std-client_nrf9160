/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <string.h>
#include <zephyr.h>
#include <stdlib.h>
#include <net/socket.h>
#include <modem/bsdlib.h>
#include <modem/lte_lc.h>
#include <modem/at_cmd.h>
#include <modem/at_notif.h>
#include <modem/modem_key_mgmt.h>
#include <modem/modem_info.h>

#include "uart_broker.h"
#include "debug_print.h"

static const struct device *uart_dev;

/* Initialize AT communications */
int at_comms_init(void) {
  int err;

  err = at_cmd_init();
  if (err) {
    DebugPrint("Failed to initialize AT commands, err %d\r\n", err);
    return err;
  }

  err = at_notif_init();
  if (err) {
    DebugPrint("Failed to initialize AT notifications, err %d\r\n", err);
    return err;
  }

  return 0;
}

void main(void) {
  int err;

  int64_t time_stamp;
  int64_t time_delta;

  time_stamp = k_uptime_get();

  /* Initialize UartBroker */ 
  uart_dev = device_get_binding(UART_LABEL);
  UartBrokerInit(uart_dev);
  UartBrokerPrintf("*** SIPF Client v.0.1.0 ***\r\n");

  err = bsdlib_init();
  if (err) {
    DebugPrint("Failed to initialize bsdlib!\r\n");
    return;
  }

  /* Initialize AT comms in order to provision the certificate */
  err = at_comms_init();
  if (err) {
    return;
  }

  DebugPrint("Setting APN.. ");
  err = lte_lc_pdp_context_set(LTE_LC_PDP_TYPE_IP, "sakura", 0, 0, 0);
  if (err) {
    DebugPrint("Failed to configure to the LTE network, err %d\r\n", err);
    return;
  }
  DebugPrint("OK\r\n");

  DebugPrint("Lock PLMN.. ");
  err = at_cmd_write("AT+COPS=1,2,\"44020\"", NULL, 0, NULL);
  if (err != 0) {
    DebugPrint("Failed to lock PLMN, err %d\r\n", err);
    return;
  }
  DebugPrint("OK\r\n");

  DebugPrint("Initialize LTE.. ");
  err = lte_lc_init();
  if (err) {
    DebugPrint("Failed to initializes the modem, err %d\r\n", err);
    return;
  }
  DebugPrint("OK\r\n");

  DebugPrint("Waiting for network.. ");
  err = lte_lc_connect();
  if (err) {
    DebugPrint("Failed to connect to the LTE network, err %d\r\n", err);
    return;
  }
  DebugPrint("OK\r\n");

  UartBrokerPrintf("+++ Ready +++\r\n");
  uint8_t b;
  for (;;) {
    while (UartBrokerGetByte(&b) == 0) {
      UartBrokerPutByte(b);
    }
    k_sleep(K_MSEC(1));
  }
  time_delta = k_uptime_delta(&time_stamp);

  DebugPrint("time delta=%lld\r\n", time_delta);
}
