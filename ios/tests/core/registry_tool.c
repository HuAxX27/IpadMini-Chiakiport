// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A2.5: Bridge/ck_registry.c from the command line, for check_registry_plist.py.
//   ck-registry-tool write PATH   saves the two made-up consoles below
//   ck-registry-tool dump PATH    loads PATH and prints one line per console

#include "Bridge/ck_registry.h"

#include <stdio.h>
#include <string.h>

static void print_hex(const uint8_t *buf, size_t size)
{
	for(size_t i = 0; i < size; i++)
		printf("%02x", buf[i]);
}

static int write_sample(const char *path)
{
	CKRegistry registry;
	memset(&registry, 0, sizeof(registry));
	ChiakiRegisteredHost host;

	memset(&host, 0, sizeof(host));
	host.target = CHIAKI_TARGET_PS5_1;
	static const uint8_t mac_a[6] = { 0x0a, 0x1b, 0x2c, 0x3d, 0x4e, 0x5f };
	memcpy(host.server_mac, mac_a, sizeof(mac_a));
	snprintf(host.server_nickname, sizeof(host.server_nickname), "%s", "Sala & <PS5> ñ");
	memcpy(host.rp_regist_key, "1a2b3c4d", 8);
	host.rp_key_type = 2;
	memset(host.rp_key, 0xf0, sizeof(host.rp_key));
	if(ck_registry_put(&registry, &host, "192.168.0.10") != CHIAKI_ERR_SUCCESS)
		return 1;

	memset(&host, 0, sizeof(host));
	host.target = CHIAKI_TARGET_PS4_10;
	static const uint8_t mac_b[6] = { 0x0a, 0x1b, 0x2c, 0x3d, 0x4e, 0x60 };
	memcpy(host.server_mac, mac_b, sizeof(mac_b));
	snprintf(host.server_nickname, sizeof(host.server_nickname), "%s", "Cuarto");
	memcpy(host.rp_regist_key, "FFFFFFFF00000000", 16);
	memset(host.rp_key, 0x0f, sizeof(host.rp_key));
	if(ck_registry_put(&registry, &host, NULL) != CHIAKI_ERR_SUCCESS)
		return 1;

	return ck_registry_save(&registry, path) == CHIAKI_ERR_SUCCESS ? 0 : 1;
}

static int dump(const char *path)
{
	CKRegistry registry;
	ChiakiErrorCode err = ck_registry_load(&registry, path);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		fprintf(stderr, "ck-registry-tool: %s\n", chiaki_error_string(err));
		return 1;
	}
	for(size_t i = 0; i < registry.count; i++)
	{
		const CKRegistryEntry *entry = &registry.entries[i];
		const ChiakiRegisteredHost *host = &entry->host;
		char id[13];
		ck_registry_mac_hex(host->server_mac, id);
		printf("%s|%d|%.*s|", id, (int)host->target,
			(int)strnlen(host->server_nickname, sizeof(host->server_nickname)), host->server_nickname);
		print_hex((const uint8_t *)host->rp_regist_key, sizeof(host->rp_regist_key));
		printf("|%u|", (unsigned)host->rp_key_type);
		print_hex(host->rp_key, sizeof(host->rp_key));
		printf("|%s\n", entry->last_host);
	}
	return 0;
}

int main(int argc, char **argv)
{
	if(argc == 3 && !strcmp(argv[1], "write"))
		return write_sample(argv[2]);
	if(argc == 3 && !strcmp(argv[1], "dump"))
		return dump(argv[2]);
	fprintf(stderr, "Uso: ck-registry-tool write|dump RUTA\n");
	return 2;
}
