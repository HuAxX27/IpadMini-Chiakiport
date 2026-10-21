// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Settings kept in NVS, written from the console. Kconfig (idf.py menuconfig)
// gives the defaults for whoever builds the firmware with their values baked in.

#include "bridge.h"

#include <esp_log.h>
#include <nvs.h>

#include <string.h>

#define NAMESPACE "ckbridge"

static const char *TAG = "bridge_config";
static BridgeConfig config;

BridgeConfig *bridge_config(void)
{
	return &config;
}

static void load_str(nvs_handle_t nvs, const char *key, char *out, size_t size)
{
	size_t len = size;
	char buf[BRIDGE_PASSWORD_MAX + 1];
	if(size > sizeof(buf))
		return;
	if(nvs_get_str(nvs, key, buf, &len) == ESP_OK)
		strlcpy(out, buf, size);
}

void bridge_config_load(void)
{
	memset(&config, 0, sizeof(config));
	strlcpy(config.ssid, CONFIG_CKBRIDGE_WIFI_SSID, sizeof(config.ssid));
	strlcpy(config.password, CONFIG_CKBRIDGE_WIFI_PASSWORD, sizeof(config.password));
	strlcpy(config.ipad, CONFIG_CKBRIDGE_IPAD_ADDR, sizeof(config.ipad));
	config.port = CK_PAD_PORT;

	nvs_handle_t nvs;
	if(nvs_open(NAMESPACE, NVS_READONLY, &nvs) != ESP_OK)
		return; // nothing saved yet
	load_str(nvs, "ssid", config.ssid, sizeof(config.ssid));
	load_str(nvs, "password", config.password, sizeof(config.password));
	load_str(nvs, "ipad", config.ipad, sizeof(config.ipad));
	uint16_t port;
	if(nvs_get_u16(nvs, "port", &port) == ESP_OK && port)
		config.port = port;
	nvs_close(nvs);
}

esp_err_t bridge_config_save(void)
{
	nvs_handle_t nvs;
	esp_err_t err = nvs_open(NAMESPACE, NVS_READWRITE, &nvs);
	if(err != ESP_OK)
		return err;
	if((err = nvs_set_str(nvs, "ssid", config.ssid)) == ESP_OK
		&& (err = nvs_set_str(nvs, "password", config.password)) == ESP_OK
		&& (err = nvs_set_str(nvs, "ipad", config.ipad)) == ESP_OK
		&& (err = nvs_set_u16(nvs, "port", config.port)) == ESP_OK)
		err = nvs_commit(nvs);
	nvs_close(nvs);
	if(err != ESP_OK)
		ESP_LOGE(TAG, "no se pudo guardar la configuracion: %s", esp_err_to_name(err));
	return err;
}
