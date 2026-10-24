// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A6.1, A6.2: the console list's rows (UI/ck_console_rows.h), built from
// discovered consoles and saved registrations. Every key in here is made up.

#include <munit.h>

#include "UI/ck_console_rows.h"

#include <stdio.h>
#include <string.h>

static CKConsole console(const char *name, const char *addr, const char *host_id, ChiakiDiscoveryHostState state)
{
	CKConsole c;
	memset(&c, 0, sizeof(c));
	strncpy(c.name, name, sizeof(c.name) - 1);
	strncpy(c.addr, addr, sizeof(c.addr) - 1);
	strncpy(c.host_id, host_id, sizeof(c.host_id) - 1);
	c.state = state;
	c.ps5 = true;
	c.target = CHIAKI_TARGET_PS5_1;
	return c;
}

static void put(CKRegistry *registry, const char *nickname, uint8_t last_mac_byte, const char *last_host)
{
	ChiakiRegisteredHost host;
	memset(&host, 0, sizeof(host));
	host.target = CHIAKI_TARGET_PS5_1;
	strncpy(host.server_nickname, nickname, sizeof(host.server_nickname));
	const uint8_t mac[6] = { 0xa0, 0xb1, 0xc2, 0xd3, 0xe4, last_mac_byte };
	memcpy(host.server_mac, mac, sizeof(mac));
	memset(host.rp_key, 0xf0, sizeof(host.rp_key));
	munit_assert_int(ck_registry_put(registry, &host, last_host), ==, CHIAKI_ERR_SUCCESS);
}

static MunitResult test_empty(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	CKConsoleRow rows[CK_CONSOLE_ROWS_MAX];
	munit_assert_size(ck_console_rows_build(NULL, 0, NULL, rows, CK_CONSOLE_ROWS_MAX), ==, 0);
	CKRegistry registry = { 0 };
	munit_assert_size(ck_console_rows_build(NULL, 0, &registry, rows, CK_CONSOLE_ROWS_MAX), ==, 0);
	return MUNIT_OK;
}

static MunitResult test_unregistered(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	CKConsole consoles[] = {
		console("PS5-517", "192.168.0.22", "A0B1C2D3E4F7", CHIAKI_DISCOVERY_HOST_STATE_READY),
		console("PS5-900", "192.168.0.23", "A0B1C2D3E4F9", CHIAKI_DISCOVERY_HOST_STATE_STANDBY),
	};
	CKConsoleRow rows[CK_CONSOLE_ROWS_MAX];
	munit_assert_size(ck_console_rows_build(consoles, 2, NULL, rows, CK_CONSOLE_ROWS_MAX), ==, 2);

	munit_assert_string_equal(rows[0].name, "PS5-517");
	munit_assert_string_equal(rows[0].addr, "192.168.0.22");
	munit_assert_int(rows[0].state, ==, CK_CONSOLE_ROW_READY);
	munit_assert_false(rows[0].registered);
	munit_assert_int(rows[0].action, ==, CK_CONSOLE_ACTION_REGISTER);

	// Registering needs it on; one in rest mode cannot be woken without a registration.
	munit_assert_int(rows[1].state, ==, CK_CONSOLE_ROW_STANDBY);
	munit_assert_int(rows[1].action, ==, CK_CONSOLE_ACTION_NONE);
	return MUNIT_OK;
}

static MunitResult test_registered(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	CKRegistry registry = { 0 };
	put(&registry, "Sala", 0xf5, "192.168.0.20");
	put(&registry, "Cuarto", 0xf6, "192.168.0.21");
	put(&registry, "Oficina", 0xf8, "10.0.0.5");
	put(&registry, "Sin IP", 0xf9, NULL);
	CKConsole consoles[] = {
		console("Sala", "192.168.0.20", "A0B1C2D3E4F5", CHIAKI_DISCOVERY_HOST_STATE_READY),
		console("Cuarto", "192.168.0.21", "A0B1C2D3E4F6", CHIAKI_DISCOVERY_HOST_STATE_STANDBY),
	};
	CKConsoleRow rows[CK_CONSOLE_ROWS_MAX];
	munit_assert_size(ck_console_rows_build(consoles, 2, &registry, rows, CK_CONSOLE_ROWS_MAX), ==, 4);

	// By name: Cuarto, Oficina, Sala, Sin IP.
	munit_assert_string_equal(rows[0].name, "Cuarto");
	munit_assert_int(rows[0].state, ==, CK_CONSOLE_ROW_STANDBY);
	munit_assert_int(rows[0].action, ==, CK_CONSOLE_ACTION_WAKE);

	munit_assert_string_equal(rows[1].name, "Oficina");
	munit_assert_int(rows[1].state, ==, CK_CONSOLE_ROW_MISSING);
	munit_assert_string_equal(rows[1].addr, "10.0.0.5");
	munit_assert_string_equal(rows[1].host_id, "A0B1C2D3E4F8");
	munit_assert_int(rows[1].action, ==, CK_CONSOLE_ACTION_WAKE);

	munit_assert_string_equal(rows[2].name, "Sala");
	munit_assert_int(rows[2].state, ==, CK_CONSOLE_ROW_READY);
	munit_assert_int(rows[2].action, ==, CK_CONSOLE_ACTION_PLAY);

	munit_assert_string_equal(rows[3].name, "Sin IP");
	munit_assert_string_equal(rows[3].addr, "");
	munit_assert_int(rows[3].action, ==, CK_CONSOLE_ACTION_NONE);

	for(size_t i = 0; i < 4; i++)
	{
		munit_assert_true(rows[i].registered);
		munit_assert_true(rows[i].ps5);
	}
	return MUNIT_OK;
}

static MunitResult test_unknown_state(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	CKRegistry registry = { 0 };
	put(&registry, "Sala", 0xf5, "192.168.0.20");
	CKConsole consoles[] = { console("Sala", "192.168.0.20", "A0B1C2D3E4F5", CHIAKI_DISCOVERY_HOST_STATE_UNKNOWN) };
	CKConsoleRow rows[1];
	munit_assert_size(ck_console_rows_build(consoles, 1, &registry, rows, 1), ==, 1);
	munit_assert_int(rows[0].state, ==, CK_CONSOLE_ROW_UNKNOWN);
	munit_assert_int(rows[0].action, ==, CK_CONSOLE_ACTION_NONE);
	return MUNIT_OK;
}

static MunitResult test_order(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	CKRegistry registry = { 0 };
	put(&registry, "zeta", 0x01, "192.168.0.31");
	put(&registry, "Alfa", 0x02, "192.168.0.32");
	CKConsole consoles[] = {
		console("beta", "192.168.0.40", "A0B1C2D3E403", CHIAKI_DISCOVERY_HOST_STATE_READY),
		console("ZETA", "192.168.0.31", "A0B1C2D3E401", CHIAKI_DISCOVERY_HOST_STATE_READY),
		console("Beta", "192.168.0.41", "A0B1C2D3E404", CHIAKI_DISCOVERY_HOST_STATE_READY),
		console("alfa", "192.168.0.32", "A0B1C2D3E402", CHIAKI_DISCOVERY_HOST_STATE_READY),
		console("Alef", "192.168.0.42", "A0B1C2D3E405", CHIAKI_DISCOVERY_HOST_STATE_READY),
	};
	const char *expected[] = { "A0B1C2D3E402", "A0B1C2D3E401", "A0B1C2D3E405", "A0B1C2D3E403", "A0B1C2D3E404" };

	// The same order whatever order discovery reported them in.
	for(size_t shift = 0; shift < 5; shift++)
	{
		CKConsole shuffled[5];
		for(size_t i = 0; i < 5; i++)
			shuffled[i] = consoles[(i + shift) % 5];
		CKConsoleRow rows[CK_CONSOLE_ROWS_MAX];
		munit_assert_size(ck_console_rows_build(shuffled, 5, &registry, rows, CK_CONSOLE_ROWS_MAX), ==, 5);
		for(size_t i = 0; i < 5; i++)
			munit_assert_string_equal(rows[i].host_id, expected[i]);
		// Registered first, then the rest.
		munit_assert_true(rows[0].registered && rows[1].registered);
		munit_assert_false(rows[2].registered || rows[3].registered || rows[4].registered);
	}
	return MUNIT_OK;
}

static MunitResult test_host_id_case(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	CKRegistry registry = { 0 };
	put(&registry, "Sala", 0xf5, "192.168.0.20");
	CKConsole consoles[] = { console("Sala", "192.168.0.99", "a0b1c2d3e4f5", CHIAKI_DISCOVERY_HOST_STATE_READY) };
	CKConsoleRow rows[CK_CONSOLE_ROWS_MAX];
	munit_assert_size(ck_console_rows_build(consoles, 1, &registry, rows, CK_CONSOLE_ROWS_MAX), ==, 1);
	munit_assert_true(rows[0].registered);
	munit_assert_string_equal(rows[0].host_id, "A0B1C2D3E4F5");
	// Where it answers from now, not where it was last seen.
	munit_assert_string_equal(rows[0].addr, "192.168.0.99");
	return MUNIT_OK;
}

static MunitResult test_names_and_target(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	CKRegistry registry = { 0 };
	put(&registry, "Sala", 0xf5, "192.168.0.20");
	// A nickname that fills its 32 bytes, with no NUL, as the core may leave it.
	memcpy(registry.entries[0].host.server_nickname, "0123456789abcdef0123456789ABCDEF", 32);
	registry.entries[0].host.target = CHIAKI_TARGET_PS4_10;
	CKConsole consoles[] = { console("", "192.168.0.20", "A0B1C2D3E4F5", CHIAKI_DISCOVERY_HOST_STATE_READY) };
	CKConsoleRow rows[CK_CONSOLE_ROWS_MAX];
	munit_assert_size(ck_console_rows_build(consoles, 1, &registry, rows, CK_CONSOLE_ROWS_MAX), ==, 1);
	munit_assert_string_equal(rows[0].name, "0123456789abcdef0123456789ABCDEF");
	// The target it registered with.
	munit_assert_int(rows[0].target, ==, CHIAKI_TARGET_PS4_10);
	munit_assert_false(rows[0].ps5);

	munit_assert_size(ck_console_rows_build(NULL, 0, &registry, rows, CK_CONSOLE_ROWS_MAX), ==, 1);
	munit_assert_string_equal(rows[0].name, "0123456789abcdef0123456789ABCDEF");
	munit_assert_int(rows[0].state, ==, CK_CONSOLE_ROW_MISSING);
	return MUNIT_OK;
}

static MunitResult test_running_game(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	CKConsole c = console("Sala", "192.168.0.20", "A0B1C2D3E4F5", CHIAKI_DISCOVERY_HOST_STATE_READY);
	strcpy(c.running_app_name, "Un juego");
	CKConsoleRow rows[1];
	munit_assert_size(ck_console_rows_build(&c, 1, NULL, rows, 1), ==, 1);
	munit_assert_string_equal(rows[0].running_app_name, "Un juego");
	return MUNIT_OK;
}

static MunitResult test_rows_max(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	CKRegistry registry = { 0 };
	for(uint8_t i = 0; i < CK_REGISTRY_MAX; i++)
	{
		char name[16];
		snprintf(name, sizeof(name), "R%u", (unsigned)i);
		put(&registry, name, i, "10.0.0.1");
	}
	CKConsole consoles[CK_CONSOLES_MAX];
	for(size_t i = 0; i < CK_CONSOLES_MAX; i++)
	{
		char name[16], host_id[16];
		snprintf(name, sizeof(name), "D%u", (unsigned)i);
		snprintf(host_id, sizeof(host_id), "B0B1C2D3E4%02X", (unsigned)i);
		consoles[i] = console(name, "192.168.0.50", host_id, CHIAKI_DISCOVERY_HOST_STATE_READY);
	}
	CKConsoleRow rows[CK_CONSOLE_ROWS_MAX + 1];
	munit_assert_size(ck_console_rows_build(consoles, CK_CONSOLES_MAX, &registry, rows, CK_CONSOLE_ROWS_MAX + 1), ==,
		CK_CONSOLE_ROWS_MAX);

	// A short array keeps the first ones found, never writes past its end.
	memset(rows, 0x5a, sizeof(rows));
	munit_assert_size(ck_console_rows_build(consoles, CK_CONSOLES_MAX, &registry, rows, 3), ==, 3);
	munit_assert_uint8(((uint8_t *)&rows[3])[0], ==, 0x5a);
	munit_assert_size(ck_console_rows_build(consoles, CK_CONSOLES_MAX, &registry, rows, 0), ==, 0);
	return MUNIT_OK;
}

static MunitResult test_update_registry(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	CKRegistry registry = { 0 };
	put(&registry, "Sala", 0xf5, "192.168.0.20");
	put(&registry, "Cuarto", 0xf6, NULL);
	CKConsole consoles[] = {
		console("Sala", "192.168.0.20", "A0B1C2D3E4F5", CHIAKI_DISCOVERY_HOST_STATE_READY),
		console("Otra", "192.168.0.30", "C0C0C0C0C0C0", CHIAKI_DISCOVERY_HOST_STATE_READY),
	};
	munit_assert_false(ck_console_rows_update_registry(consoles, 2, &registry));
	munit_assert_string_equal(registry.entries[0].last_host, "192.168.0.20");

	// The router gave it another IP.
	strcpy(consoles[0].addr, "192.168.0.77");
	munit_assert_true(ck_console_rows_update_registry(consoles, 2, &registry));
	munit_assert_string_equal(registry.entries[0].last_host, "192.168.0.77");
	munit_assert_false(ck_console_rows_update_registry(consoles, 2, &registry));

	// One that had no IP gets it, in any case of host_id.
	consoles[1] = console("Cuarto", "192.168.0.21", "a0b1c2d3e4f6", CHIAKI_DISCOVERY_HOST_STATE_STANDBY);
	munit_assert_true(ck_console_rows_update_registry(consoles, 2, &registry));
	munit_assert_string_equal(registry.entries[1].last_host, "192.168.0.21");
	munit_assert_size(registry.count, ==, 2);
	return MUNIT_OK;
}

static MunitResult test_strings(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	munit_assert_string_equal(ck_console_row_state_string(CK_CONSOLE_ROW_READY), "encendida");
	munit_assert_string_equal(ck_console_row_state_string(CK_CONSOLE_ROW_STANDBY), "en reposo");
	munit_assert_string_equal(ck_console_row_state_string(CK_CONSOLE_ROW_MISSING), "no contesta");
	munit_assert_string_equal(ck_console_row_state_string(CK_CONSOLE_ROW_UNKNOWN), "estado desconocido");
	munit_assert_string_equal(ck_console_action_string(CK_CONSOLE_ACTION_PLAY), "Jugar");
	munit_assert_string_equal(ck_console_action_string(CK_CONSOLE_ACTION_WAKE), "Despertar");
	munit_assert_string_equal(ck_console_action_string(CK_CONSOLE_ACTION_REGISTER), "Registrar");
	munit_assert_string_equal(ck_console_action_string(CK_CONSOLE_ACTION_NONE), "");
	return MUNIT_OK;
}

static MunitTest tests[] = {
	{ "/empty", test_empty, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
	{ "/unregistered", test_unregistered, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
	{ "/registered", test_registered, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
	{ "/unknown_state", test_unknown_state, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
	{ "/order", test_order, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
	{ "/host_id_case", test_host_id_case, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
	{ "/names_and_target", test_names_and_target, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
	{ "/running_game", test_running_game, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
	{ "/rows_max", test_rows_max, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
	{ "/update_registry", test_update_registry, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
	{ "/strings", test_strings, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
	{ NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
};

static const MunitSuite suite = { "/console_rows", tests, NULL, 1, MUNIT_SUITE_OPTION_NONE };

int main(int argc, char *argv[])
{
	return munit_suite_main(&suite, NULL, argc, argv);
}
