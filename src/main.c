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
#include <modem/pdn.h>
#include <net/socket.h>
#include <net/tls_credentials.h>
#include <logging/log.h>

#include <drivers/gpio.h>
#include <power/reboot.h>

#include "cmd.h"
#include "fota/fota_http.h"
#include "sipf/sipf_client_http.h"
#include "sipf/sipf_auth.h"
#include "gnss/gnss.h"
#include "uart_broker.h"

#include "registers.h"
#include "version.h"

LOG_MODULE_REGISTER(sipf, CONFIG_SIPF_LOG_LEVEL);

/** peripheral **/
#define LED_HEARTBEAT_MS (500)

#define LED_PORT DT_GPIO_LABEL(DT_ALIAS(led1), gpios)
#define LED1_PIN (DT_GPIO_PIN(DT_ALIAS(led1), gpios))
#define LED1_FLAGS (GPIO_OUTPUT_ACTIVE | DT_GPIO_FLAGS(DT_ALIAS(led1), gpios))

#define LED2_PIN (DT_GPIO_PIN(DT_ALIAS(led2), gpios))
#define LED2_FLAGS (GPIO_OUTPUT_ACTIVE | DT_GPIO_FLAGS(DT_ALIAS(led2), gpios))

#define LED3_PIN (DT_GPIO_PIN(DT_ALIAS(led3), gpios))
#define LED3_FLAGS (GPIO_OUTPUT_ACTIVE | DT_GPIO_FLAGS(DT_ALIAS(led3), gpios))

#define WAKE_IN_PORT DT_GPIO_LABEL(DT_ALIAS(sw2), gpios)
#define WAKE_IN_PIN (DT_GPIO_PIN(DT_ALIAS(sw2), gpios))
#define WAKE_IN_FLAGS (GPIO_INPUT | DT_GPIO_FLAGS(DT_ALIAS(sw2), gpios))
/**********/

/** TLS **/
#define TLS_SEC_TAG 42
static const char cert[] = {
#include "../cert/sipf.iot.sakura.ad.jp"
};
BUILD_ASSERT(sizeof(cert) < KB(4), "Certificate too large");
/*********/

static K_SEM_DEFINE(lte_connected, 0, 1);
static K_SEM_DEFINE(reset_request, 0, 1);
static const struct device *uart_dev;

/* Auth info */
#define SZ_USER_NAME  (255)
#define SZ_PASSWORD   (255)
static char user_name[SZ_USER_NAME];
static char password[SZ_PASSWORD];

/* Initialize AT communications */
int at_comms_init(void)
{
  int err;

  err = at_cmd_init();
  if (err) {
    LOG_ERR("Failed to initialize AT commands, err %d", err);
    return err;
  }

  err = at_notif_init();
  if (err) {
    LOG_ERR("Failed to initialize AT notifications, err %d", err);
    return err;
  }

  return 0;
}

#ifdef CONFIG_BOARD_SCM_LTEM1NRF_NRF9160NS
static struct gpio_callback gpio_cb;

void wake_in_assert(const struct device *gpiob, struct gpio_callback *cb, uint32_t pins)
{
  //リブート要求
  UartBrokerPrint("RESET_REQ_DETECT\n");
  k_sem_give(&reset_request);
}

static int wake_in_init(void)
{
  const struct device *dev;
  dev = device_get_binding(WAKE_IN_PORT);
  if (dev == 0) {
    LOG_ERR("Nordic nRF GPIO driver was not found!");
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
#else
static int wake_in_init(void)
{
  return 0;
}
#endif

/** LED **/
static int led_init(void)
{
  const struct device *dev;

  dev = device_get_binding(LED_PORT);
  if (dev == 0) {
    LOG_ERR("Nordic nRF GPIO driver was not found!");
    return 1;
  }
  int ret;
  /* Initialize LED1  */
  ret = gpio_pin_configure(dev, LED1_PIN, LED1_FLAGS);
  LOG_DBG("gpio_pin_configure(%d): %d", LED1_PIN, ret);
  ret = gpio_pin_set(dev, LED1_PIN, 0);
  LOG_DBG("gpio_pin_set(%d): %d", LED1_PIN, ret);

  /* Initialize LED2  */
  ret = gpio_pin_configure(dev, LED2_PIN, LED2_FLAGS);
  LOG_DBG("gpio_pin_configure(%d): %d", LED2_PIN, ret);
  ret = gpio_pin_set(dev, LED2_PIN, 0);
  LOG_DBG("gpio_pin_set(%d): %d", LED2_PIN, ret);

  /* Initialize LED3  */
  ret = gpio_pin_configure(dev, LED3_PIN, LED3_FLAGS);
  LOG_DBG("gpio_pin_configure(%d): %d", LED3_PIN, ret);
  ret = gpio_pin_set(dev, LED3_PIN, 0);
  LOG_DBG("gpio_pin_set(%d): %d", LED3_PIN, ret);
  return 0;
}

static int led_on(gpio_pin_t pin)
{
  const struct device *dev = device_get_binding(LED_PORT);
  if (dev == 0) {
    LOG_ERR("Nordic nRF GPIO driver was not found!");
    return 1;
  }
  gpio_pin_set(dev, pin, 1);
  return 0;
}

static int led_off(gpio_pin_t pin)
{
  const struct device *dev = device_get_binding(LED_PORT);
  if (dev == 0) {
    LOG_ERR("Nordic nRF GPIO driver was not found!");
    return 1;
  }
  gpio_pin_set(dev, pin, 0);
  return 0;
}

static int led1_toggle(void)
{
  const struct device *dev;
  static int val = 0;

  dev = device_get_binding(LED_PORT);
  if (dev == 0) {
    LOG_ERR("Nordic nRF GPIO driver was not found!");
    return 1;
  }
  gpio_pin_set(dev, LED1_PIN, val);
  val = (val == 0) ? 1 : 0;
  return 0;
}
/***********/

/** MODEM **/
#define REGISTER_TIMEOUT_MS (120000)
#define REGISTER_TRY (3)

static int cert_provision(void)
{
  int err;
  bool exists;
  uint8_t unused;

  err = modem_key_mgmt_exists(TLS_SEC_TAG, MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN, &exists, &unused);
  if (err) {
    LOG_ERR("Failed to check for certificates err %d", err);
    return err;
  }

  if (exists) {
    /* For the sake of simplicity we delete what is provisioned
     * with our security tag and reprovision our certificate.
     */
    err = modem_key_mgmt_delete(TLS_SEC_TAG, MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN);
    if (err) {
      LOG_ERR("Failed to delete existing certificate, err %d", err);
    }
  }

  LOG_DBG("Provisioning certificate");

  /*  Provision certificate to the modem */
  err = modem_key_mgmt_write(TLS_SEC_TAG, MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN, cert, sizeof(cert) - 1);
  if (err) {
    LOG_ERR("Failed to provision certificate, err %d", err);
    return err;
  }

  return 0;
}

static void lte_handler(const struct lte_lc_evt *const evt)
{
  LOG_DBG("evt->type=%d", evt->type);
  switch (evt->type) {
  case LTE_LC_EVT_NW_REG_STATUS:
    LOG_DBG("- evt->nw_reg_status=%d\n", evt->nw_reg_status);
    if (evt->nw_reg_status == LTE_LC_NW_REG_SEARCHING) {
      UartBrokerPrint("SEARCHING\n");
      break;
    }
    if ((evt->nw_reg_status == LTE_LC_NW_REG_REGISTERED_HOME) || (evt->nw_reg_status == LTE_LC_NW_REG_REGISTERED_ROAMING)) {
      UartBrokerPrint("REGISTERD\n");
      k_sem_give(&lte_connected);
      break;
    }
    break;
  case LTE_LC_EVT_CELL_UPDATE:
    LOG_DBG("- mcc=%d, mnc=%d", evt->cell.mcc, evt->cell.mnc);
    break;
  case LTE_LC_EVT_LTE_MODE_UPDATE:
    LOG_DBG("- evt->lte_mode=%d", evt->lte_mode);
    break;
  case LTE_LC_EVT_MODEM_EVENT:
    LOG_DBG("- evt->modem_evt=%d", evt->modem_evt);
    break;
  default:
    break;
  }
}

static int init_modem_and_lte(void)
{
  static char at_ret[128];
  int err = 0;

  err = nrf_modem_lib_init(NORMAL_MODE);
  if (err) {
    LOG_ERR("Failed to initialize modem library!");
    return err;
  }

  /* Initialize AT comms in order to provision the certificate */
  err = at_comms_init();
  if (err) {
    LOG_ERR("Faild to at_comms_init(): %d", err);
    return err;
  }

  /* Provision certificates before connecting to the LTE network */
  err = cert_provision();
  if (err) {
    LOG_ERR("Faild to cert_provision(): %d", err);
    return err;
  }

  err = lte_lc_system_mode_set(LTE_LC_SYSTEM_MODE_LTEM_GPS, LTE_LC_SYSTEM_MODE_PREFER_AUTO);
  if (err) {
    LOG_ERR("Failed to System Mode set.");
    return err;
  }
  LOG_DBG("Setting system mode OK");

  err = at_cmd_write("AT\%XMAGPIO=1,0,0,1,1,1574,1577", NULL, 0, NULL);
  if (err != 0) {
    LOG_ERR("Failed to set XMAGPIO, err %d", err);
    return err;
  }
  LOG_DBG("Configure MAGPIO OK");

  err = at_cmd_write("AT\%XCOEX0=1,1,1565,1586", NULL, 0, NULL);
  if (err != 0) {
    LOG_ERR("Failed to set XCOEX0, err %d", err);
    return err;
  }
  LOG_DBG("Configure pin OK");

  /* PDN */
  uint8_t cid;
  err = pdn_init();
  if (err != 0) {
    LOG_ERR("Failed to pdn_init()");
    return err;
  }
  err = pdn_ctx_create(&cid, NULL);
  if (err != 0) {
    LOG_ERR("Failed to pdn_ctx_create(), err %d", err);
    return err;
  }
  // set APN
  err = pdn_ctx_configure(cid, "sakura", PDN_FAM_IPV4, NULL);
  if (err != 0) {
    LOG_ERR("Failed to pdn_ctx_configure(), err %d", err);
    return err;
  }
  LOG_DBG("Setting APN OK");

  /* CONNECT */
  enum at_cmd_state at_state;
  for (int i = 0; i < REGISTER_TRY; i++) {
    LOG_DBG("Initialize LTE");
    err = lte_lc_init();
    if (err) {
      LOG_ERR("Failed to initializes the modem, err %d", err);
      return err;
    }
    LOG_DBG("Initialize LTE OK");

    lte_lc_modem_events_enable();
/*
    err = lte_lc_neighbor_cell_measurement();
    if (err == 0) {
      LOG_DBG("lte_lc_neighbor_cell_measurement() success.");
    }
*/

    LOG_INF("[%d] Trying to attach to LTE network (TIMEOUT: %d ms)", i, REGISTER_TIMEOUT_MS);
    UartBrokerPrint("Trying to attach to LTE network (TIMEOUT: %d ms)\r\n", REGISTER_TIMEOUT_MS);
    err = lte_lc_connect_async(lte_handler);
    if (err) {
      LOG_ERR("Failed to attatch to the LTE network, err %d", err);
      return err;
    }
    err = k_sem_take(&lte_connected, K_MSEC(REGISTER_TIMEOUT_MS));
    if (err == -EAGAIN) {
      UartBrokerPrint("TIMEOUT\n");
      lte_lc_offline();
      lte_lc_deinit();
      continue;
    } else if (err == 0) {
      // connected

      // PSMの設定
      err = lte_lc_psm_req(true);
      if (err) {
        LOG_ERR("PSM request failed, error: %d", err);
      } else {
        LOG_DBG("PSM is enabled");
      }

      // ICCIDの取得
      err = at_cmd_write("AT%XICCID", at_ret, sizeof(at_ret), &at_state);
      if (err) {
        LOG_ERR("Failed to get ICCID, err %d", err);
        return err;
      }
      if (at_state == AT_CMD_OK) {
        char *iccid_top = &at_ret[9]; // ICCIDの先頭
        for (int i = 0; i < 20; i++) {
          if (iccid_top[i] == 'F') {
            iccid_top[i] = 0x00;
          }
        }
        UartBrokerPrint("ICCID: %s\n", iccid_top);
      }
      return 0;
    } else {
      //
      return err;
    }
  }

  LOG_ERR("Faild to attach to LTE Network");
  return -1;
}
/**********/

void main(void)
{
  int err;

  int64_t ms_now, ms_timeout;

  // 対ユーザーMUCのレジスタ初期化
  RegistersReset();

  // UartBrokerの初期化(以降、Debug系の出力も可能)
  uart_dev = device_get_binding(UART_LABEL);
  UartBrokerInit(uart_dev);
  UartBrokerPrint("*** SIPF Client(Type%02x) v.%d.%d.%d ***\n", *REG_CMN_FW_TYPE, *REG_CMN_VER_MJR, *REG_CMN_VER_MNR, *REG_CMN_VER_REL);
#ifdef CONFIG_LTE_LOCK_PLMN
  UartBrokerPuts("* PLMN: " CONFIG_LTE_LOCK_PLMN_STRING "\n");
#endif
#ifdef CONFIG_SIPF_AUTH_DISABLE_SSL
  UartBrokerPuts("* Disable SSL, AUTH endpoint.\n");
#endif
#ifdef CONFIG_SIPF_CONNECTOR_DISABLE_SSL
  UartBrokerPuts("* Disable SSL, CONNECTOR endpoint.\n");
#endif
  // LEDの初期化
  led_init();
  led_on(LED2_PIN);

  // WAKE_INの初期化
  wake_in_init();

  //モデムの初期化&LTE接続
  err = init_modem_and_lte();
  if (err) {
    led_off(LED2_PIN);
    return;
  }

  // GNSSの初期化
  if (gnss_init() != 0) {
    UartBrokerPuts("Failed to initialize GNSS peripheral\n");
  }

  // LTEつながるならOKなFWよね
  boot_write_img_confirmed();

  uint8_t b, prev_auth_mode = 0x00;
  UartBrokerPuts("+++ Ready +++\n");
  led_on(LED3_PIN);
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
      led1_toggle();
    }

    if ((*REG_00_MODE == 0x01) && (prev_auth_mode == 0x00)) {
      // 認証モードがIPアドレス認証に切り替えられた
      err = SipfAuthRequest(user_name, sizeof(user_name), password, sizeof(user_name));
      LOG_DBG("SipfAuthRequest(): %d", err);
      if (err < 0) {
        // IPアドレス認証に失敗した
        *REG_00_MODE = 0x00; // モードが切り替えられなかった
      }

      err = SipfClientHttpSetAuthInfo(user_name, password);
      if (err < 0) {
        // 認証情報の設定に失敗した
        *REG_00_MODE = 0x00; // モードが切り替えられなかった
      }
    }
    prev_auth_mode = *REG_00_MODE;

    // GNSSイベントの処理
    gnss_poll();

    if (k_sem_take(&reset_request, K_NO_WAIT)== 0) {
        // リセット要求来た
        lte_lc_offline();
        sys_reboot(SYS_REBOOT_COLD);
    }

    k_sleep(K_MSEC(1));
  }
}
