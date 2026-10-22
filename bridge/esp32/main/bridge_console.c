// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Console commands to set the bridge up over the USB serial port, next to the
// ones Bluepad32 brings (help lists them all). Text without accents so any
// serial terminal shows it right.

#include "bridge.h"

#include <argtable3/argtable3.h>
#include <esp_console.h>
#include <esp_system.h>
#include <lwip/inet.h>
#include <lwip/sockets.h>

#include <stdio.h>
#include <string.h>

static struct
{
	struct arg_str *value;
	struct arg_end *end;
} ssid_args, password_args;

static struct
{
	struct arg_str *addr;
	struct arg_int *port;
	struct arg_end *end;
} ipad_args;

static bool parse(int argc, char **argv, void *args, struct arg_end *end)
{
	if(arg_parse(argc, argv, (void **)args) == 0)
		return true;
	arg_print_errors(stdout, end, argv[0]);
	return false;
}

static int cmd_ssid(int argc, char **argv)
{
	if(!parse(argc, argv, &ssid_args, ssid_args.end))
		return 1;
	const char *ssid = ssid_args.value->sval[0];
	size_t len = strlen(ssid);
	if(len == 0 || len > BRIDGE_SSID_MAX)
	{
		printf("El nombre de la red debe tener de 1 a %d caracteres.\n", BRIDGE_SSID_MAX);
		return 1;
	}
	strlcpy(bridge_config()->ssid, ssid, sizeof(bridge_config()->ssid));
	if(bridge_config_save() != ESP_OK)
		return 1;
	printf("Red guardada: \"%s\". Cuando termines, escribe restart para conectarte.\n", ssid);
	return 0;
}

static int cmd_password(int argc, char **argv)
{
	if(!parse(argc, argv, &password_args, password_args.end))
		return 1;
	const char *password = password_args.value->sval[0];
	size_t len = strlen(password);
	if(len != 0 && (len < 8 || len > BRIDGE_PASSWORD_MAX))
	{
		printf("La clave debe tener de 8 a %d caracteres (o \"\" para una red abierta).\n", BRIDGE_PASSWORD_MAX);
		return 1;
	}
	strlcpy(bridge_config()->password, password, sizeof(bridge_config()->password));
	if(bridge_config_save() != ESP_OK)
		return 1;
	printf("Clave guardada (%u caracteres). Cuando termines, escribe restart para conectarte.\n", (unsigned)len);
	return 0;
}

static int cmd_ipad(int argc, char **argv)
{
	if(!parse(argc, argv, &ipad_args, ipad_args.end))
		return 1;
	const char *addr = ipad_args.addr->sval[0];
	struct in_addr in;
	if(strlen(addr) > BRIDGE_ADDR_MAX || inet_pton(AF_INET, addr, &in) != 1)
	{
		printf("\"%s\" no es una direccion IPv4, por ejemplo 192.168.1.50.\n", addr);
		return 1;
	}
	int port = ipad_args.port->count ? ipad_args.port->ival[0] : CK_PAD_PORT;
	if(port < 1 || port > 65535)
	{
		printf("El puerto debe ir de 1 a 65535.\n");
		return 1;
	}
	BridgeConfig *config = bridge_config();
	strlcpy(config->ipad, addr, sizeof(config->ipad));
	config->port = (uint16_t)port;
	if(bridge_config_save() != ESP_OK)
		return 1;
	bridge_net_set_target(config);
	printf("Destino guardado: %s:%d. Ya se usa, sin reiniciar.\n", config->ipad, port);
	return 0;
}

static const char *status_text(BridgeNetStatus status)
{
	switch(status)
	{
		case BRIDGE_NET_UNCONFIGURED:
			return "falta configurar (luz roja)";
		case BRIDGE_NET_CONNECTING:
			return "conectando al Wi-Fi (luz amarilla)";
		case BRIDGE_NET_READY:
			return "mandando al destino (luz verde)";
	}
	return "?";
}

static int cmd_status(int argc, char **argv)
{
	(void)argc;
	(void)argv;
	const BridgeConfig *config = bridge_config();
	BridgeNetStats stats;
	BridgePadInfo pad;
	bridge_net_stats(&stats);
	bridge_platform_pad_info(&pad);

	printf("Estado:   %s\n", status_text(stats.status));
	printf("Wi-Fi:    %s%s%s, %s\n", config->ssid[0] ? "\"" : "", config->ssid[0] ? config->ssid : "sin red",
		config->ssid[0] ? "\"" : "", config->password[0] ? "con clave" : "sin clave");
	printf("IP:       %s\n", stats.own_ip[0] ? stats.own_ip : "sin IP");
	if(config->ipad[0])
		printf("Destino:  %s:%u\n", config->ipad, (unsigned)config->port);
	else
		printf("Destino:  sin configurar\n");
	const char *name = pad.name[0] ? pad.name : "conectado";
	if(pad.connected && pad.battery)
		printf("Control:  %s, bateria %u%%\n", name, (unsigned)((pad.battery - 1) * 100 / 250));
	else if(pad.connected)
		printf("Control:  %s\n", name);
	else
		printf("Control:  ninguno (manten Create + PS en el DualSense para emparejarlo)\n");
	printf("Paquetes: %lu enviados, %lu errores, ultimo seq %u\n", (unsigned long)stats.sent,
		(unsigned long)stats.errors, (unsigned)stats.last_seq);
	printf("Memoria:  %lu KB libres (minimo %lu KB)\n", (unsigned long)(esp_get_free_heap_size() / 1024),
		(unsigned long)(esp_get_minimum_free_heap_size() / 1024));
	return 0;
}

void bridge_console_register(void)
{
	ssid_args.value = arg_str1(NULL, NULL, "<nombre>", "nombre de la red Wi-Fi, entre comillas si tiene espacios");
	ssid_args.end = arg_end(2);
	const esp_console_cmd_t ssid_cmd = {
		.command = "ssid",
		.help = "Guarda el nombre de la red Wi-Fi (se aplica con restart)",
		.func = cmd_ssid,
		.argtable = &ssid_args,
	};
	ESP_ERROR_CHECK(esp_console_cmd_register(&ssid_cmd));

	password_args.value = arg_str1(NULL, NULL, "<clave>", "clave de la red; \"\" si es abierta");
	password_args.end = arg_end(2);
	const esp_console_cmd_t password_cmd = {
		.command = "password",
		.help = "Guarda la clave del Wi-Fi (se aplica con restart)",
		.func = cmd_password,
		.argtable = &password_args,
	};
	ESP_ERROR_CHECK(esp_console_cmd_register(&password_cmd));

	ipad_args.addr = arg_str1(NULL, NULL, "<ip>", "IPv4 del iPad, o de la PC para probar");
	ipad_args.port = arg_int0(NULL, NULL, "<puerto>", "puerto UDP, 9400 si no se da");
	ipad_args.end = arg_end(3);
	const esp_console_cmd_t ipad_cmd = {
		.command = "ipad",
		.help = "Guarda a donde se mandan los paquetes del control",
		.func = cmd_ipad,
		.argtable = &ipad_args,
	};
	ESP_ERROR_CHECK(esp_console_cmd_register(&ipad_cmd));

	const esp_console_cmd_t status_cmd = {
		.command = "status",
		.help = "Muestra Wi-Fi, destino, control y paquetes enviados",
		.func = cmd_status,
	};
	ESP_ERROR_CHECK(esp_console_cmd_register(&status_cmd));
}
