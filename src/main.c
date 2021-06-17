/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <stdlib.h>
#include <string.h>
#include <zephyr.h>

#include <modem/at_cmd.h>
#include <modem/at_notif.h>
#include <modem/lte_lc.h>
#include <modem/modem_info.h>
#include <modem/modem_key_mgmt.h>
#include <modem/nrf_modem_lib.h>
#include <net/socket.h>

#include <drivers/gpio.h>
#include <power/reboot.h>

#include "cmd.h"
#include "debug_print.h"
#include "fota/fota_http.h"
#include "sipf/sipf_client_http.h"
#include "uart_broker.h"

#include "registers.h"
#include "version.h"

#define LED_HEARTBEAT_MS (500)

#define LED_PORT DT_GPIO_LABEL(DT_ALIAS(led1), gpios)
#define LED1_PIN (DT_GPIO_PIN(DT_ALIAS(led1), gpios))
#define LED1_FLAGS (GPIO_OUTPUT_ACTIVE | DT_GPIO_FLAGS(DT_ALIAS(led1), gpios))

#define WAKE_IN_PORT DT_GPIO_LABEL(DT_ALIAS(sw2), gpios)
#define WAKE_IN_PIN (DT_GPIO_PIN(DT_ALIAS(sw2), gpios))
#define WAKE_IN_FLAGS (GPIO_INPUT | DT_GPIO_FLAGS(DT_ALIAS(sw2), gpios))

static const struct device *uart_dev;
static struct gpio_callback gpio_cb;

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

void wake_in_assert(const struct device *gpiob, struct gpio_callback *cb, uint32_t pins) {
  //リブート
  UartBrokerPrint("RESET_REQ_DETECT\r\n");
  sys_reboot(SYS_REBOOT_COLD);
}

static int wake_in_init(void) {
  const struct device *dev;
  dev = device_get_binding(WAKE_IN_PORT);
  if (dev == 0) {
    DebugPrint("Nordic nRF GPIO driver was not found!\n");
    return 1;
  }
  int ret;
  ret = gpio_pin_configure(dev, WAKE_IN_PIN, WAKE_IN_FLAGS);
  if (ret == 0) {
    gpio_init_callback(&gpio_cb, wake_in_assert, BIT(WAKE_IN_PIN));
    ret = gpio_add_callback(dev, &gpio_cb);
    if (ret == 0) {
      gpio_pin_interrupt_configure(dev, WAKE_IN_PIN, GPIO_INT_EDGE_TO_ACTIVE);
    }
  }

  return 0;
}

static int led_init(void) {
  const struct device *dev;

  dev = device_get_binding(LED_PORT);
  if (dev == 0) {
    DebugPrint("Nordic nRF GPIO driver was not found!\n");
    return 1;
  }
  int ret;
  ret = gpio_pin_configure(dev, LED1_PIN, LED1_FLAGS);
  DebugPrint("gpio_pin_configure(): %d\r\n", ret);
  ret = gpio_pin_set(dev, LED1_PIN, 1);
  DebugPrint("gpio_pin_set(): %d\r\n", ret);
  return 0;
}

static int led_on(void) {
  const struct device *dev = device_get_binding(LED_PORT);
  if (dev == 0) {
    DebugPrint("Nordic nRF GPIO driver was not found!\n");
    return 1;
  }
  gpio_pin_set(dev, LED1_PIN, 1);
  return 0;
}

static int led_off(void) {
  const struct device *dev = device_get_binding(LED_PORT);
  if (dev == 0) {
    DebugPrint("Nordic nRF GPIO driver was not found!\n");
    return 1;
  }
  gpio_pin_set(dev, LED1_PIN, 0);
  return 0;
}

static int led_toggle(void) {
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

static int init_modem_and_lte(void) {
  int err = 0;

  err = nrf_modem_lib_init(NORMAL_MODE);
  if (err) {
    DebugPrint("Failed to initialize modem library!");
    return err;
  }

  /* Initialize AT comms in order to provision the certificate */
  err = at_comms_init();
  if (err) {
    DebugPrint("Faild to at_comms_init(): %d\r\n", err);
    return err;
  }

  DebugPrint("Setting APN.. ");
  err = lte_lc_pdp_context_set(LTE_LC_PDP_TYPE_IP, "sakura", 0, 0, 0);
  if (err) {
    DebugPrint("Failed to configure to the LTE network, err %d\r\n", err);
    return err;
  }
  DebugPrint("OK\r\n");

  DebugPrint("Lock PLMN.. ");
  err = at_cmd_write("AT+COPS=1,2,\"44020\"", NULL, 0, NULL);
  if (err != 0) {
    DebugPrint("Failed to lock PLMN, err %d\r\n", err);
    return err;
  }
  DebugPrint("OK\r\n");

  DebugPrint("Initialize LTE.. ");
  err = lte_lc_init();
  if (err) {
    DebugPrint("Failed to initializes the modem, err %d\r\n", err);
    return err;
  }
  DebugPrint("OK\r\n");

  DebugPrint("Waiting for network.. ");
  err = lte_lc_connect();
  if (err) {
    DebugPrint("Failed to connect to the LTE network, err %d\r\n", err);
    return err;
  }
  DebugPrint("OK\r\n");

  return err;
}

void main(void) {
  int err;

  int64_t ms_now, ms_timeout;
  led_on();

  // 対ユーザーMUCのレジスタ初期化
  RegistersReset();

  // UartBrokerの初期化(以降、Debug系の出力も可能)
  uart_dev = device_get_binding(UART_LABEL);
  UartBrokerInit(uart_dev);
  UartBrokerPrint("*** SIPF Client(Type%02x) v.%d.%d.%d ***\r\n", *REG_CMN_FW_TYPE, *REG_CMN_VER_MJR, *REG_CMN_VER_MNR, *REG_CMN_VER_REL);

  // LEDの初期化
  led_init();

  // WAKE_INの初期化
  wake_in_init();

  //モデムの初期化&LTE接続
  err = init_modem_and_lte();
  if (err) {
    led_off();
    return;
  }

  // LTEつながるならOKなFWよね
  boot_write_img_confirmed();

  uint8_t b;
  err = SipfClientGetAuthInfo();
  DebugPrint(DBG "SipfClientGetAuthInfo(): %d\r\n", err);
  /*
    err = SipfClientSetAuthInfo("user2", "pass2");
    DebugPrint(DBG "SipfClientSetAuthInfo(): %d\r\n", err);
  */
  UartBrokerPuts("+++ Ready +++\r\n");
  ms_timeout = k_uptime_get() + LED_HEARTBEAT_MS;
  for (;;) {
    while (UartBrokerGetByte(&b) == 0) {
      CmdResponse *cr = CmdParse(b);
      if (cr != NULL) {
        // UARTにレスポンスを返す
        UartBrokerPut(cr->response, cr->response_len);
      }
    }

    // Heart Beat
    ms_now = k_uptime_get();
    if ((ms_timeout - ms_now) < 0) {
      ms_timeout = ms_now + LED_HEARTBEAT_MS;
      led_toggle();
    }

    k_sleep(K_MSEC(1));
  }
}
