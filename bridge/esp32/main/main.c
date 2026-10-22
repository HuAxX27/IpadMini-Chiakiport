// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// ESP32 bridge: DualSense over Bluetooth (Bluepad32) to the iPad over Wi-Fi,
// with the contract C4 packet. See bridge/esp32/README.md.

#include "bridge.h"

#include <btstack_port_esp32.h>
#include <btstack_run_loop.h>
#include <btstack_stdio_esp32.h>
#include <nvs_flash.h>
#include <uni.h>

#include "sdkconfig.h"

#ifndef CONFIG_BLUEPAD32_PLATFORM_CUSTOM
#error "The bridge is a Bluepad32 custom platform: set CONFIG_BLUEPAD32_PLATFORM_CUSTOM"
#endif

static void init_nvs(void)
{
	esp_err_t err = nvs_flash_init();
	if(err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND)
	{
		ESP_ERROR_CHECK(nvs_flash_erase());
		err = nvs_flash_init();
	}
	ESP_ERROR_CHECK(err);
}

void app_main(void)
{
	init_nvs();
	bridge_config_load();
	bridge_net_start(bridge_config());

	// BTstack's buffered UART would fight with the console.
#if defined(CONFIG_ESP_CONSOLE_UART) && !defined(CONFIG_BLUEPAD32_USB_CONSOLE_ENABLE)
	btstack_stdio_init();
#endif
	btstack_init();
	uni_platform_set_custom(bridge_platform_get());
	uni_init(0, NULL);

	// Runs Bluetooth on this task; never returns.
	btstack_run_loop_execute();
}
