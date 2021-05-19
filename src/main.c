/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <zephyr.h>
#include <stdlib.h>
#include <string.h>

#include <net/socket.h>
#include <modem/nrf_modem_lib.h>
//#include <modem/bsdlib.h>
#include <modem/lte_lc.h>
#include <modem/at_cmd.h>
#include <modem/at_notif.h>
#include <modem/modem_key_mgmt.h>
#include <modem/modem_info.h>

#include <drivers/gpio.h>

#include "uart_broker.h"
#include "debug_print.h"
#include "sipf/sipf_client_http.h"

#define LED_PORT DT_GPIO_LABEL(DT_ALIAS(led1), gpios)
#define LED1_PIN (DT_GPIO_PIN(DT_ALIAS(led1), gpios))
#define LED1_FLAGS (GPIO_OUTPUT_ACTIVE | DT_GPIO_FLAGS(DT_ALIAS(led1), gpios))

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

static int led_init(void)
{
	const struct device *dev;

	dev = device_get_binding(LED_PORT);
	if (dev == 0) {
		printk("Nordic nRF GPIO driver was not found!\n");
		return 1;
	}
  int ret;
  ret = gpio_pin_configure(dev, LED1_PIN, LED1_FLAGS);
  printk("gpio_pin_configure(): %d\r\n", ret);
  ret = gpio_pin_set(dev, LED1_PIN, 1);
  printk("gpio_pin_set(): %d\r\n", ret);
  return 0;
}

static int led_toggle(void)
{
	const struct device *dev;
  static int val = 0;

	dev = device_get_binding(LED_PORT);
	if (dev == 0) {
		printk("Nordic nRF GPIO driver was not found!\n");
		return 1;
	}
  gpio_pin_set(dev, LED1_PIN, val);
  val = (val == 0) ? 1 : 0;
  return 0;
}

void main(void) {
  int err;

  int64_t time_stamp;
  int64_t time_delta;

  time_stamp = k_uptime_get();

  led_init();

  /* Initialize UartBroker */ 
  uart_dev = device_get_binding(UART_LABEL);
  UartBrokerInit(uart_dev);
  UartBrokerPuts("*** SIPF Client v.0.1.0 ***\r\n");

	err = nrf_modem_lib_init(NORMAL_MODE);
	if (err) {
		DebugPrint("Failed to initialize modem library!");
		return;
	}

  /* Initialize AT comms in order to provision the certificate */
  err = at_comms_init();
  if (err) {
    DebugPrint("Faild to at_comms_init(): %d\r\n", err);
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

  uint8_t b;
  uint32_t val = 0;
  SipfObjectUp objup;
  SipfObjectOtid otid;
  err = SipfClientSetAuthInfo("user2", "pass2");
  DebugPrint(DBG "SipfClientSetAuthInfo(): %d\r\n", err);
  UartBrokerPuts("+++ Ready +++\r\n");
  for (;;) {
    while (UartBrokerGetByte(&b) == 0) {
      UartBrokerPutByte(b);
      led_toggle();
      if (b == 't') {
        val++;
        objup.obj_qty = 1;
        objup.obj.obj_type = 0x02; //uint32
        objup.obj.obj_tagid = 0x00;
        objup.obj.value = (uint8_t *)&val;
        objup.obj.value_len = sizeof(val);
        err = SipfClientObjUp(&objup, &otid);
        if (err == 0) {
          DebugPrint("OTID:");
          for (int i = 0; i < sizeof(otid.value); i++) {
            DebugPrint("%02x", otid.value[i]);
          }
          DebugPrint("\r\n");
        } else {
          DebugPrint("SipfClientObjUp() failed: %d\r\n", err);
        }
      }
    }
    k_sleep(K_MSEC(1));
  }
  time_delta = k_uptime_delta(&time_stamp);

  DebugPrint("time delta=%lld\r\n", time_delta);
}
