/*
 * Copyright (c) 2020 Nordic Semiconductor ASA
 *
 * SPDX-License-Identifier: LicenseRef-BSD-5-Clause-Nordic
 */

#include <stdlib.h>
#include <string.h>
#include <zephyr/kernel.h>

#include <nrf_modem_at.h>
#include <modem/at_monitor.h>
#include <modem/lte_lc.h>
#include <modem/modem_info.h>
#include <modem/modem_key_mgmt.h>
#include <modem/nrf_modem_lib.h>
#include <modem/pdn.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/tls_credentials.h>
#include <zephyr/logging/log.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/reboot.h>

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

static const struct gpio_dt_spec LED1 = GPIO_DT_SPEC_GET(DT_NODELABEL(led1), gpios);
static const struct gpio_dt_spec LED2 = GPIO_DT_SPEC_GET(DT_NODELABEL(led2), gpios);
static const struct gpio_dt_spec LED3 = GPIO_DT_SPEC_GET(DT_NODELABEL(led3), gpios);
static const struct gpio_dt_spec WAKE_IN = GPIO_DT_SPEC_GET(DT_NODELABEL(button0), gpios);

/**********/

/** TLS **/
#define TLS_SEC_TAG 42
static const char cert[] = {
#include "sipf/cert/sipf.iot.sakura.ad.jp"
};
BUILD_ASSERT(sizeof(cert) < KB(4), "Certificate too large");
/*********/

static K_SEM_DEFINE(lte_connected, 0, 1);
static const struct device *uart_dev;

/* Auth info */
#define SZ_USER_NAME (255)
#define SZ_PASSWORD (255)
static char user_name[SZ_USER_NAME];
static char password[SZ_PASSWORD];

/* Initialize AT communications */
int at_comms_init(void)
{
    return 0;
}

#ifdef CONFIG_BOARD_SCM_LTEM1NRF_NRF9160_NS
static struct gpio_callback gpio_cb;

void wake_in_assert(const struct device *gpiob, struct gpio_callback *cb, uint32_t pins)
{
    //リブート要求
    UartBrokerPrint("RESET_REQ_DETECT\r\n");
    sys_reboot(SYS_REBOOT_COLD);
}

static int wake_in_init(void)
{
    int ret;
    ret = gpio_pin_configure_dt(&WAKE_IN, GPIO_INPUT);
    if (ret == 0) {
        gpio_init_callback(&gpio_cb, wake_in_assert, BIT(WAKE_IN.pin));
        ret = gpio_add_callback(WAKE_IN.port, &gpio_cb);
        if (ret == 0) {
            gpio_pin_interrupt_configure_dt(&WAKE_IN, GPIO_INT_EDGE_TO_ACTIVE);
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
    int ret;

    /* Initialize LED1  */
	if (!gpio_is_ready_dt(&LED1)) {
		LOG_ERR("LED1 gpio is not ready");
	}
    ret = gpio_pin_configure_dt(&LED1, GPIO_OUTPUT_ACTIVE);
    LOG_DBG("gpio_pin_configure(%d): %d", 1, ret);
    ret = gpio_pin_set_dt(&LED1, 0x00);
    LOG_DBG("gpio_pin_set(%d): %d", 1, ret);

    /* Initialize LED2  */
	if (!gpio_is_ready_dt(&LED2)) {
		LOG_ERR("LED1 gpio is not ready");
	}
    ret = gpio_pin_configure_dt(&LED2, GPIO_OUTPUT_ACTIVE);
    LOG_DBG("gpio_pin_configure(%d): %d", 2, ret);
    ret = gpio_pin_set_dt(&LED2, 0x00);
    LOG_DBG("gpio_pin_set(%d): %d", 2, ret);

    /* Initialize LED3  */
	if (!gpio_is_ready_dt(&LED3)) {
		LOG_ERR("LED1 gpio is not ready");
	}
    ret = gpio_pin_configure_dt(&LED3, GPIO_OUTPUT_ACTIVE);
    LOG_DBG("gpio_pin_configure(%d): %d", 3, ret);
    ret = gpio_pin_set_dt(&LED3, 0x00);
    LOG_DBG("gpio_pin_set(%d): %d", 3, ret);

    return 0;
}

static int led_on(gpio_pin_t pin)
{
    switch(pin) {
        case 1:
            gpio_pin_set_dt(&LED1, 0x01);
            break;
        case 2:
            gpio_pin_set_dt(&LED2, 0x01);
            break;
        case 3:
            gpio_pin_set_dt(&LED3, 0x01);
            break;
        default:
            LOG_ERR("Invald LED %d", pin);
            return 1;
            break;
    }
    return 0;
}

static int led_off(gpio_pin_t pin)
{
    switch(pin) {
        case 1:
            gpio_pin_set_dt(&LED1, 0x00);
            break;
        case 2:
            gpio_pin_set_dt(&LED2, 0x00);
            break;
        case 3:
            gpio_pin_set_dt(&LED3, 0x00);
            break;
        break;
            LOG_ERR("Invald LED %d", pin);
            return 1;
            break;
    }
    return 0;
}

static int led1_toggle(void)
{
    static int val = 0;
    gpio_pin_set_dt(&LED1, val);
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

    err = modem_key_mgmt_exists(TLS_SEC_TAG, MODEM_KEY_MGMT_CRED_TYPE_CA_CHAIN, &exists);
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
            UartBrokerPrint("SEARCHING\r\n");
            break;
        }
        if ((evt->nw_reg_status == LTE_LC_NW_REG_REGISTERED_HOME) || (evt->nw_reg_status == LTE_LC_NW_REG_REGISTERED_ROAMING)) {
            UartBrokerPrint("REGISTERD\r\n");
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

    err = nrf_modem_lib_init();
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

    err = nrf_modem_at_printf("AT%%XMAGPIO=1,0,0,1,1,1574,1577");
    if (err != 0) {
        LOG_ERR("Failed to set XMAGPIO, err %d", err);
        return err;
    }
    LOG_DBG("Configure MAGPIO OK");

    err = nrf_modem_at_printf("AT%%XCOEX0=1,1,1565,1586");
    if (err != 0) {
        LOG_ERR("Failed to set XCOEX0, err %d", err);
        return err;
    }
    LOG_DBG("Configure pin OK");

    /* PDN */
    uint8_t cid;
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
    for (int i = 0; i < REGISTER_TRY; i++) {
        LOG_DBG("Initialize LTE");
        err = lte_lc_init();
        if (err) {
            LOG_ERR("Failed to initializes the modem, err %d", err);
            return err;
        }
        LOG_DBG("Initialize LTE OK");

        lte_lc_modem_events_enable();

        LOG_INF("[%d] Trying to attach to LTE network (TIMEOUT: %d ms)", i, REGISTER_TIMEOUT_MS);
        UartBrokerPrint("Trying to attach to LTE network (TIMEOUT: %d ms)\r\n", REGISTER_TIMEOUT_MS);
        err = lte_lc_connect_async(lte_handler);
        if (err) {
            LOG_ERR("Failed to attatch to the LTE network, err %d", err);
            return err;
        }
        err = k_sem_take(&lte_connected, K_MSEC(REGISTER_TIMEOUT_MS));
        if (err == -EAGAIN) {
            UartBrokerPrint("TIMEOUT\r\n");
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

            err = nrf_modem_at_scanf("AT%XICCID", "%%XICCID:  %120[ ,-\"a-zA-Z0-9]", at_ret);
            if (err) {
                LOG_ERR("Failed to get ICCID, err %d", err);
                return err;
            }
            char *iccid_top = &at_ret[9]; // ICCIDの先頭
            for (int i = 0; i < 20; i++) {
                if (iccid_top[i] == 'F') {
                    iccid_top[i] = 0x00;
                }
            }
            UartBrokerPrint("ICCID: %s\r\n", iccid_top);
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

int main(void)
{
    int err;

    int64_t ms_now, ms_timeout;

    // 対ユーザーMUCのレジスタ初期化
    RegistersReset();

    // UartBrokerの初期化(以降、Debug系の出力も可能)
    uart_dev =  DEVICE_DT_GET(DT_NODELABEL(uart0));
    UartBrokerInit(uart_dev);
    UartBrokerPrint("*** SIPF Client(Type%02x) v.%d.%d.%d ***\r\n", *REG_CMN_FW_TYPE, *REG_CMN_VER_MJR, *REG_CMN_VER_MNR, *REG_CMN_VER_REL);
#ifdef CONFIG_LTE_LOCK_PLMN
    UartBrokerPuts("* PLMN: " CONFIG_LTE_LOCK_PLMN_STRING "\r\n");
#endif
#ifdef CONFIG_SIPF_AUTH_DISABLE_SSL
    UartBrokerPuts("* Disable SSL, AUTH endpoint.\r\n");
#endif
#ifdef CONFIG_SIPF_CONNECTOR_DISABLE_SSL
    UartBrokerPuts("* Disable SSL, CONNECTOR endpoint.\r\n");
#endif
    // LEDの初期化
    led_init();
    led_on(2);

    // WAKE_INの初期化
    wake_in_init();

    //モデムの初期化&LTE接続
    err = init_modem_and_lte();
    if (err) {
        led_off(2);
        return -1;
    }

    // GNSSの初期化
    if (gnss_init() != 0) {
        UartBrokerPuts("Failed to initialize GNSS peripheral\r\n");
    }

#if CONFIG_DFU_TARGET_MCUBOOT
    // LTEつながるならOKなFWよね
    boot_write_img_confirmed();
#endif

    // 認証モードをSIM認証にする
    uint8_t b, prev_auth_mode = 0x01;
    *REG_00_MODE = 0x01;

    for (;;) {
        err = SipfAuthRequest(user_name, sizeof(user_name), password, sizeof(user_name));
        LOG_DBG("SipfAuthRequest(): %d", err);
        if (err < 0) {
            // IPアドレス認証に失敗した
            UartBrokerPuts("Set AuthMode to `SIM Auth' faild...(Retry after 10s)\r\n");
            *REG_00_MODE = 0x00; // モードが切り替えられなかった
            k_sleep(K_MSEC(10000));
            continue;
        }
        break;
    }
    err = SipfClientHttpSetAuthInfo(user_name, password);
    if (err < 0) {
        // 認証情報の設定に失敗した
        *REG_00_MODE = 0x00; // モードが切り替えられなかった
    }

    UartBrokerPuts("+++ Ready +++\r\n");
    led_on(3);
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

        k_sleep(K_MSEC(1));
    }
}
