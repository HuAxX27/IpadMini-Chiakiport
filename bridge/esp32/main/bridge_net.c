// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Wi-Fi station and the UDP sender. The Bluetooth task only stores the latest
// pad state and wakes the sender task, which asks ck_bridge_pace when to send
// and does the sendto(), so a slow Wi-Fi never holds up Bluetooth.

#include "bridge.h"
#include "ck_bridge_pace.h"
#include "ck_pad_codec.h"

#include <esp_event.h>
#include <esp_log.h>
#include <esp_netif.h>
#include <esp_timer.h>
#include <esp_wifi.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/inet.h>
#include <lwip/sockets.h>

#include <errno.h>
#include <string.h>

#define SENDER_STACK 4096
#define SENDER_PRIORITY 10
#define SENDER_CORE 1 // Bluetooth and Wi-Fi run on core 0

static const char *TAG = "bridge_net";

static portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
// Guarded by lock.
static CKPadPacket pad_state;
static bool pad_present;
static struct sockaddr_in target;
static bool target_set;
static bool wifi_set;
static bool have_ip;
static esp_ip4_addr_t own_ip;
static uint32_t stat_sent;
static uint32_t stat_errors;
static uint16_t stat_last_seq;

static TaskHandle_t sender;
static int sock = -1;
static char applied_ssid[BRIDGE_SSID_MAX + 1];

static void wake_sender(void)
{
	if(sender)
		xTaskNotifyGive(sender);
}

static uint32_t now_ms(void)
{
	return (uint32_t)(esp_timer_get_time() / 1000);
}

static void sender_task(void *arg)
{
	(void)arg;
	CKBridgePaceConfig pace_config;
	ck_bridge_pace_default_config(&pace_config);
	CKBridgePace pace;
	ck_bridge_pace_init(&pace, &pace_config);
	uint32_t wait_ms = CK_BRIDGE_PACE_WAIT_FOREVER;
	bool failing = false;

	for(;;)
	{
		TickType_t ticks = wait_ms == CK_BRIDGE_PACE_WAIT_FOREVER ? portMAX_DELAY : pdMS_TO_TICKS(wait_ms);
		ulTaskNotifyTake(pdTRUE, ticks ? ticks : 1);

		CKPadPacket state;
		bool present, ready;
		struct sockaddr_in to;
		taskENTER_CRITICAL(&lock);
		state = pad_state;
		present = pad_present;
		ready = have_ip && target_set;
		to = target;
		taskEXIT_CRITICAL(&lock);

		if(!ready)
		{
			wait_ms = CK_BRIDGE_PACE_WAIT_FOREVER;
			continue;
		}

		CKPadPacket out;
		if(!ck_bridge_pace_poll(&pace, present ? &state : NULL, now_ms(), &out, &wait_ms))
			continue;

		uint8_t buf[CK_PAD_PACKET_SIZE];
		ck_pad_packet_write(&out, buf);
		bool ok = sendto(sock, buf, sizeof(buf), MSG_DONTWAIT, (struct sockaddr *)&to, sizeof(to)) == (int)sizeof(buf);
		if(!ok && !failing)
			ESP_LOGW(TAG, "sendto fallo: errno %d", errno);
		failing = !ok;

		taskENTER_CRITICAL(&lock);
		if(ok)
			stat_sent++;
		else
			stat_errors++;
		stat_last_seq = out.seq;
		taskEXIT_CRITICAL(&lock);
	}
}

static void wifi_connect_if_set(void)
{
	if(applied_ssid[0])
		esp_wifi_connect();
}

static void on_event(void *arg, esp_event_base_t base, int32_t id, void *data)
{
	(void)arg;
	if(base == WIFI_EVENT && id == WIFI_EVENT_STA_START)
	{
		wifi_connect_if_set();
	}
	else if(base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED)
	{
		const wifi_event_sta_disconnected_t *event = data;
		taskENTER_CRITICAL(&lock);
		have_ip = false;
		taskEXIT_CRITICAL(&lock);
		ESP_LOGI(TAG, "Wi-Fi desconectado (razon %d), reintentando", event->reason);
		wifi_connect_if_set();
	}
	else if(base == IP_EVENT && id == IP_EVENT_STA_GOT_IP)
	{
		const ip_event_got_ip_t *event = data;
		taskENTER_CRITICAL(&lock);
		have_ip = true;
		own_ip = event->ip_info.ip;
		taskEXIT_CRITICAL(&lock);
		ESP_LOGI(TAG, "Wi-Fi conectado, IP del puente " IPSTR, IP2STR(&event->ip_info.ip));
		wake_sender();
	}
	else if(base == IP_EVENT && id == IP_EVENT_STA_LOST_IP)
	{
		taskENTER_CRITICAL(&lock);
		have_ip = false;
		taskEXIT_CRITICAL(&lock);
	}
}

static void apply_target(const BridgeConfig *config)
{
	struct sockaddr_in to;
	memset(&to, 0, sizeof(to));
	to.sin_family = AF_INET;
	to.sin_port = htons(config->port);
	bool ok = config->ipad[0] && inet_pton(AF_INET, config->ipad, &to.sin_addr) == 1;

	taskENTER_CRITICAL(&lock);
	target = to;
	target_set = ok;
	taskEXIT_CRITICAL(&lock);
	wake_sender();
}

// Wi-Fi settings are applied once at boot; the console asks for a restart after changing them.
static void apply_wifi(const BridgeConfig *config)
{
	strlcpy(applied_ssid, config->ssid, sizeof(applied_ssid));

	wifi_config_t wc;
	memset(&wc, 0, sizeof(wc));
	// Both fields may be full length with no terminator.
	memcpy(wc.sta.ssid, config->ssid, strnlen(config->ssid, sizeof(wc.sta.ssid)));
	memcpy(wc.sta.password, config->password, strnlen(config->password, sizeof(wc.sta.password)));
	wc.sta.threshold.authmode = config->password[0] ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
	wc.sta.pmf_cfg.capable = true;
	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wc));

	taskENTER_CRITICAL(&lock);
	wifi_set = applied_ssid[0] != '\0';
	taskEXIT_CRITICAL(&lock);
}

void bridge_net_start(const BridgeConfig *config)
{
	ESP_ERROR_CHECK(esp_netif_init());
	esp_err_t err = esp_event_loop_create_default();
	if(err != ESP_ERR_INVALID_STATE)
		ESP_ERROR_CHECK(err);
	esp_netif_create_default_wifi_sta();

	wifi_init_config_t init = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&init));
	ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
	ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, on_event, NULL));
	ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, ESP_EVENT_ANY_ID, on_event, NULL));
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	apply_wifi(config);
	apply_target(config);
	// Power save stays at the default modem sleep: ESP-IDF requires it when Bluetooth shares the
	// radio, and it only delays what the bridge receives, never what it sends.
	ESP_ERROR_CHECK(esp_wifi_start());

	sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
	if(sock < 0)
	{
		ESP_LOGE(TAG, "no se pudo abrir el socket UDP: errno %d", errno);
		return;
	}
	int on = 1;
	// Lets a broadcast address be used as the target when testing on a PC.
	setsockopt(sock, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));

	xTaskCreatePinnedToCore(sender_task, "ck_sender", SENDER_STACK, NULL, SENDER_PRIORITY, &sender, SENDER_CORE);

	if(!config->ssid[0] || !config->ipad[0])
		ESP_LOGW(TAG, "Falta configurar: escribe ssid, password e ipad en la consola (ver bridge/esp32/README.md)");
}

void bridge_net_set_target(const BridgeConfig *config)
{
	apply_target(config);
}

void bridge_net_set_pad(const CKPadPacket *state)
{
	taskENTER_CRITICAL(&lock);
	if(state)
		pad_state = *state;
	pad_present = state != NULL;
	taskEXIT_CRITICAL(&lock);
	wake_sender();
}

BridgeNetStatus bridge_net_status(void)
{
	BridgeNetStatus status;
	taskENTER_CRITICAL(&lock);
	if(!wifi_set || !target_set)
		status = BRIDGE_NET_UNCONFIGURED;
	else if(!have_ip)
		status = BRIDGE_NET_CONNECTING;
	else
		status = BRIDGE_NET_READY;
	taskEXIT_CRITICAL(&lock);
	return status;
}

void bridge_net_stats(BridgeNetStats *stats)
{
	esp_ip4_addr_t ip;
	bool connected;
	stats->status = bridge_net_status();
	taskENTER_CRITICAL(&lock);
	stats->sent = stat_sent;
	stats->errors = stat_errors;
	stats->last_seq = stat_last_seq;
	ip = own_ip;
	connected = have_ip;
	taskEXIT_CRITICAL(&lock);
	if(connected)
		snprintf(stats->own_ip, sizeof(stats->own_ip), IPSTR, IP2STR(&ip));
	else
		stats->own_ip[0] = '\0';
}
