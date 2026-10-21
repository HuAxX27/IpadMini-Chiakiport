// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Glue of the ESP32 firmware: Bluepad32 (bridge_platform.c), Wi-Fi and UDP
// (bridge_net.c), settings in NVS (bridge_config.c) and the serial console
// (bridge_console.c). The portable logic lives in bridge/core and bridge/protocol.

#ifndef BRIDGE_H
#define BRIDGE_H

#include "ck_pad_packet.h"

#include <esp_err.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define BRIDGE_SSID_MAX 32
#define BRIDGE_PASSWORD_MAX 64
#define BRIDGE_ADDR_MAX 15 // dotted IPv4

typedef struct bridge_config_t
{
	char ssid[BRIDGE_SSID_MAX + 1];
	char password[BRIDGE_PASSWORD_MAX + 1];
	char ipad[BRIDGE_ADDR_MAX + 1]; // empty until set
	uint16_t port;
} BridgeConfig;

/** The settings in use: NVS over the Kconfig defaults. Changed only by the console task. */
BridgeConfig *bridge_config(void);
void bridge_config_load(void);
esp_err_t bridge_config_save(void);

typedef enum bridge_net_status_t
{
	BRIDGE_NET_UNCONFIGURED, // no Wi-Fi name or no iPad address
	BRIDGE_NET_CONNECTING,
	BRIDGE_NET_READY
} BridgeNetStatus;

typedef struct bridge_net_stats_t
{
	BridgeNetStatus status;
	char own_ip[BRIDGE_ADDR_MAX + 1];
	uint32_t sent;
	uint32_t errors;
	uint16_t last_seq;
} BridgeNetStats;

void bridge_net_start(const BridgeConfig *config);
/** Points the sender at config->ipad and config->port. Wi-Fi changes need a restart. */
void bridge_net_set_target(const BridgeConfig *config);
/** Latest pad state, or NULL when no pad is connected. Called on the Bluetooth task. */
void bridge_net_set_pad(const CKPadPacket *state);
BridgeNetStatus bridge_net_status(void);
void bridge_net_stats(BridgeNetStats *stats);

struct uni_platform;
struct uni_platform *bridge_platform_get(void);

typedef struct bridge_pad_info_t
{
	bool connected;
	char name[32];
	uint8_t battery; // 0 unknown, 1 empty to 255 full, as Bluepad32 reports it
} BridgePadInfo;

void bridge_platform_pad_info(BridgePadInfo *info);

/** Registers ssid, password, ipad and status. Bluepad32 calls it while it starts its console. */
void bridge_console_register(void);

#endif // BRIDGE_H
