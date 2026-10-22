// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Bluepad32 "custom platform": takes one pad, maps its state to the C4 packet
// and hands it to bridge_net. Every callback here runs on the Bluetooth task.

#include "bridge.h"
#include "ck_bridge_map.h"
#include "ck_pad_codec.h"

#include <btstack_run_loop.h>
#include <freertos/FreeRTOS.h>
#include <uni.h>

#include <string.h>

_Static_assert(CK_BP32_DPAD_UP == DPAD_UP && CK_BP32_DPAD_DOWN == DPAD_DOWN
	&& CK_BP32_DPAD_RIGHT == DPAD_RIGHT && CK_BP32_DPAD_LEFT == DPAD_LEFT, "Bluepad32 d-pad bits changed");
_Static_assert(CK_BP32_BUTTON_A == BUTTON_A && CK_BP32_BUTTON_B == BUTTON_B
	&& CK_BP32_BUTTON_X == BUTTON_X && CK_BP32_BUTTON_Y == BUTTON_Y, "Bluepad32 face button bits changed");
_Static_assert(CK_BP32_BUTTON_SHOULDER_L == BUTTON_SHOULDER_L && CK_BP32_BUTTON_SHOULDER_R == BUTTON_SHOULDER_R
	&& CK_BP32_BUTTON_TRIGGER_L == BUTTON_TRIGGER_L && CK_BP32_BUTTON_TRIGGER_R == BUTTON_TRIGGER_R
	&& CK_BP32_BUTTON_THUMB_L == BUTTON_THUMB_L && CK_BP32_BUTTON_THUMB_R == BUTTON_THUMB_R,
	"Bluepad32 shoulder, trigger or thumb bits changed");
_Static_assert(CK_BP32_MISC_SYSTEM == MISC_BUTTON_SYSTEM && CK_BP32_MISC_SELECT == MISC_BUTTON_SELECT
	&& CK_BP32_MISC_START == MISC_BUTTON_START && CK_BP32_MISC_CAPTURE == MISC_BUTTON_CAPTURE,
	"Bluepad32 misc button bits changed");

#define STATUS_TICK_MS 250

static uni_hid_device_t *pad; // the one pad the bridge forwards
static uni_gamepad_t last_gamepad;
static bool have_gamepad;
static bool touchpad_click;
static CKPadPacket last_state;
static bool have_state;

static btstack_timer_source_t status_timer;
static int shown_status = -1;

// Copy for the console task.
static portMUX_TYPE info_lock = portMUX_INITIALIZER_UNLOCKED;
static BridgePadInfo info;

static void set_info(bool connected, const char *name, uint8_t battery)
{
	taskENTER_CRITICAL(&info_lock);
	info.connected = connected;
	strlcpy(info.name, name ? name : "", sizeof(info.name));
	info.battery = battery;
	taskEXIT_CRITICAL(&info_lock);
}

void bridge_platform_pad_info(BridgePadInfo *out)
{
	taskENTER_CRITICAL(&info_lock);
	*out = info;
	taskEXIT_CRITICAL(&info_lock);
}

// Light bar: red = settings missing, yellow = joining the Wi-Fi, green = sending to the iPad.
static void show_status(uni_hid_device_t *d, BridgeNetStatus status)
{
	if(!d->report_parser.set_lightbar_color)
		return;
	switch(status)
	{
		case BRIDGE_NET_UNCONFIGURED:
			d->report_parser.set_lightbar_color(d, 255, 0, 0);
			break;
		case BRIDGE_NET_CONNECTING:
			d->report_parser.set_lightbar_color(d, 255, 160, 0);
			break;
		case BRIDGE_NET_READY:
			d->report_parser.set_lightbar_color(d, 0, 255, 0);
			break;
	}
}

static void status_tick(btstack_timer_source_t *ts)
{
	BridgeNetStatus status = bridge_net_status();
	if(pad && (int)status != shown_status)
	{
		show_status(pad, status);
		shown_status = (int)status;
	}
	btstack_run_loop_set_timer(ts, STATUS_TICK_MS);
	btstack_run_loop_add_timer(ts);
}

static void push_state(void)
{
	CKBridgeGamepad in = {
		.dpad = last_gamepad.dpad,
		.buttons = last_gamepad.buttons,
		.misc_buttons = last_gamepad.misc_buttons,
		.axis_x = last_gamepad.axis_x,
		.axis_y = last_gamepad.axis_y,
		.axis_rx = last_gamepad.axis_rx,
		.axis_ry = last_gamepad.axis_ry,
		.brake = last_gamepad.brake,
		.throttle = last_gamepad.throttle,
		.touchpad_click = touchpad_click,
	};
	CKPadPacket state;
	ck_pad_packet_init(&state);
	ck_bridge_map(&in, &state);
	// The DualSense reports about every 4 ms even when nothing moves.
	if(have_state && ck_pad_packet_same_state(&state, &last_state))
		return;
	last_state = state;
	have_state = true;
	bridge_net_set_pad(&state);
}

static void forget_pad(void)
{
	pad = NULL;
	have_gamepad = false;
	touchpad_click = false;
	have_state = false;
	shown_status = -1;
	set_info(false, NULL, 0);
	bridge_net_set_pad(NULL);
}

static void platform_init(int argc, const char **argv)
{
	ARG_UNUSED(argc);
	ARG_UNUSED(argv);
}

static void platform_on_init_complete(void)
{
	// The DualSense touchpad click only reaches the platform through Bluepad32's virtual mouse.
	if(!uni_virtual_device_is_enabled())
		uni_virtual_device_set_enabled(true);

	// Keep the stored Bluetooth keys: a paired pad comes back by pressing PS.
	uni_bt_allow_incoming_connections(true);
	uni_bt_start_scanning_and_autoconnect_unsafe();

	btstack_run_loop_set_timer_handler(&status_timer, status_tick);
	btstack_run_loop_set_timer(&status_timer, STATUS_TICK_MS);
	btstack_run_loop_add_timer(&status_timer);
	logi("bridge: listo. Para emparejar, manten Create + PS en el DualSense hasta que parpadee.\n");
}

static uni_error_t platform_on_device_discovered(bd_addr_t addr, const char *name, uint16_t cod, uint8_t rssi)
{
	(void)addr; // ARG_UNUSED takes sizeof, which warns on an array parameter
	ARG_UNUSED(name);
	ARG_UNUSED(rssi);
	if((cod & UNI_BT_COD_MINOR_KEYBOARD_AND_MICE) != 0)
		return UNI_ERROR_IGNORE_DEVICE;
	return UNI_ERROR_SUCCESS;
}

static void platform_on_device_connected(uni_hid_device_t *d)
{
	logi("bridge: conectado %s\n", bd_addr_to_str(d->conn.btaddr));
}

static void platform_on_device_disconnected(uni_hid_device_t *d)
{
	if(d != pad)
		return;
	logi("bridge: el control se desconecto\n");
	forget_pad();
	uni_bt_start_scanning_and_autoconnect_unsafe();
}

static uni_error_t platform_on_device_ready(uni_hid_device_t *d)
{
	if(d->parent)
		return d->parent == pad ? UNI_ERROR_SUCCESS : UNI_ERROR_IGNORE_DEVICE;
	if(pad)
	{
		logi("bridge: ya hay un control; se ignora %s\n", d->name);
		return UNI_ERROR_IGNORE_DEVICE;
	}

	pad = d;
	have_gamepad = false;
	touchpad_click = false;
	have_state = false;
	shown_status = -1;
	set_info(true, d->name, 0);
	logi("bridge: control listo: %s\n", d->name);

	// Scanning shares the radio with the Wi-Fi; a paired pad reconnects on its own.
	uni_bt_stop_scanning_unsafe();

	if(d->report_parser.set_player_leds)
		d->report_parser.set_player_leds(d, 1);
	if(d->report_parser.play_dual_rumble)
		d->report_parser.play_dual_rumble(d, 0, 150, 128, 40);
	return UNI_ERROR_SUCCESS;
}

static void platform_on_controller_data(uni_hid_device_t *d, uni_controller_t *ctl)
{
	if(!pad)
		return;

	if(d == pad && ctl->klass == UNI_CONTROLLER_CLASS_GAMEPAD)
	{
		last_gamepad = ctl->gamepad;
		have_gamepad = true;
		if(ctl->battery != info.battery)
			set_info(true, d->name, ctl->battery);
		push_state();
	}
	else if(d->parent == pad && ctl->klass == UNI_CONTROLLER_CLASS_MOUSE)
	{
		// The virtual mouse is reported right before the gamepad of the same report.
		bool click = (ctl->mouse.buttons & (UNI_MOUSE_BUTTON_LEFT | UNI_MOUSE_BUTTON_RIGHT)) != 0;
		if(click != touchpad_click)
		{
			touchpad_click = click;
			if(have_gamepad)
				push_state();
		}
	}
}

static const uni_property_t *platform_get_property(uni_property_idx_t idx)
{
	ARG_UNUSED(idx);
	return NULL;
}

static void platform_on_oob_event(uni_platform_oob_event_t event, void *data)
{
	// The PS button already goes to the iPad as a button; nothing else to do.
	ARG_UNUSED(event);
	ARG_UNUSED(data);
}

struct uni_platform *bridge_platform_get(void)
{
	static struct uni_platform platform = {
		.name = "ck-bridge",
		.init = platform_init,
		.on_init_complete = platform_on_init_complete,
		.on_device_discovered = platform_on_device_discovered,
		.on_device_connected = platform_on_device_connected,
		.on_device_disconnected = platform_on_device_disconnected,
		.on_device_ready = platform_on_device_ready,
		.on_controller_data = platform_on_controller_data,
		.get_property = platform_get_property,
		.on_oob_event = platform_on_oob_event,
		.register_console_cmds = bridge_console_register,
	};
	return &platform;
}
