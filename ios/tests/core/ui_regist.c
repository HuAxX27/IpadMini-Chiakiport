// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A6.3: what the app does when a registration ends (UI/ck_ui_regist.h).
// CKLanConsoleService.m saves through ck_ui_regist_save() and shows the
// sentence of ck_ui_regist_message(), so what passes here is what the app
// does. Each test that touches files works in a folder of its own under the
// system's temporary folder. Every key in here is made up.

// nftw() and mkdtemp(), for the temporary folders.
#define _XOPEN_SOURCE 700

#include <munit.h>

#include "UI/ck_ui_regist.h"

#include <chiaki/session.h>

#include <ftw.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct tmp_dir_t
{
	char path[256];
} TmpDir;

static void *setup_dir(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	TmpDir *dir = malloc(sizeof(*dir));
	munit_assert_not_null(dir);
	const char *tmp = getenv("TMPDIR");
	snprintf(dir->path, sizeof(dir->path), "%s/ck-ui-regist-XXXXXX", tmp && *tmp ? tmp : "/tmp");
	munit_assert_not_null(mkdtemp(dir->path));
	return dir;
}

static int remove_one(const char *path, const struct stat *st, int type, struct FTW *ftw)
{
	(void)st;
	(void)type;
	(void)ftw;
	return remove(path);
}

static void teardown_dir(void *fixture)
{
	TmpDir *dir = fixture;
	nftw(dir->path, remove_one, 16, FTW_DEPTH | FTW_PHYS);
	free(dir);
}

static void dir_file(const TmpDir *dir, const char *name, char *out, size_t size)
{
	munit_assert_int(snprintf(out, size, "%s/%s", dir->path, name), <, (int)size);
}

static void write_file(const char *path, const char *text)
{
	FILE *f = fopen(path, "wb");
	munit_assert_not_null(f);
	size_t len = strlen(text);
	munit_assert_size(fwrite(text, 1, len, f), ==, len);
	munit_assert_int(fclose(f), ==, 0);
}

/** The whole file, NUL-terminated; free() it. */
static char *read_file(const char *path)
{
	FILE *f = fopen(path, "rb");
	munit_assert_not_null(f);
	munit_assert_int(fseek(f, 0, SEEK_END), ==, 0);
	long size = ftell(f);
	munit_assert_long(size, >=, 0);
	rewind(f);
	char *buf = malloc((size_t)size + 1);
	munit_assert_not_null(buf);
	munit_assert_size(fread(buf, 1, (size_t)size, f), ==, (size_t)size);
	buf[size] = '\0';
	fclose(f);
	return buf;
}

/** A registration with made-up keys, told apart by the last byte of its MAC. */
static ChiakiRegisteredHost make_host(uint8_t mac_last, const char *nickname)
{
	ChiakiRegisteredHost host;
	memset(&host, 0, sizeof(host));
	host.target = CHIAKI_TARGET_PS5_1;
	static const uint8_t mac[6] = { 0x0a, 0x1b, 0x2c, 0x3d, 0x4e, 0x00 };
	memcpy(host.server_mac, mac, sizeof(mac));
	host.server_mac[5] = mac_last;
	snprintf(host.server_nickname, sizeof(host.server_nickname), "%s", nickname);
	memcpy(host.rp_regist_key, "1a2b3c4d", 8);
	host.rp_key_type = 2;
	memset(host.rp_key, 0xf0, sizeof(host.rp_key));
	host.rp_key[0] = mac_last;
	return host;
}

/** What ckctl regist would have left in the file. */
static void save_hosts(const char *path, size_t count)
{
	CKRegistry registry;
	memset(&registry, 0, sizeof(registry));
	for(size_t i = 0; i < count; i++)
	{
		char name[16];
		snprintf(name, sizeof(name), "PS5 %u", (unsigned)i);
		ChiakiRegisteredHost host = make_host((uint8_t)(0x10 + i), name);
		munit_assert_int(ck_registry_put(&registry, &host, "192.168.0.30"), ==, CHIAKI_ERR_SUCCESS);
	}
	munit_assert_int(ck_registry_save(&registry, path), ==, CHIAKI_ERR_SUCCESS);
}

static CKRegistry load(const char *path)
{
	CKRegistry registry;
	munit_assert_int(ck_registry_load(&registry, path), ==, CHIAKI_ERR_SUCCESS);
	return registry;
}

// ---------------------------------------------------------------------------
// Saving
// ---------------------------------------------------------------------------

static MunitResult test_save_new_file(const MunitParameter params[], void *fixture)
{
	(void)params;
	TmpDir *dir = fixture;
	char path[512];
	// The folder does not exist yet either: the first registration on a new iPad.
	dir_file(dir, "Chiakiport/consoles.plist", path, sizeof(path));
	ChiakiRegisteredHost host = make_host(0x5f, "PS5 de la sala");
	CKRegistry registry;
	ChiakiErrorCode err = CHIAKI_ERR_SUCCESS;
	munit_assert_int(ck_ui_regist_save(path, &host, "192.168.0.20", &registry, &err), ==, CK_UI_REGIST_SAVED);
	munit_assert_size(registry.count, ==, 1);

	CKRegistry file = load(path);
	munit_assert_size(file.count, ==, 1);
	CKRegistryEntry *entry = ck_registry_find_mac(&file, host.server_mac);
	munit_assert_not_null(entry);
	munit_assert_string_equal(entry->host.server_nickname, "PS5 de la sala");
	munit_assert_memory_equal(sizeof(host.rp_key), entry->host.rp_key, host.rp_key);
	munit_assert_string_equal(entry->last_host, "192.168.0.20");
	munit_assert_memory_equal(sizeof(file), &file, &registry);

	// Without an IP too.
	ChiakiRegisteredHost other = make_host(0x60, "PS5 del cuarto");
	munit_assert_int(ck_ui_regist_save(path, &other, NULL, &registry, NULL), ==, CK_UI_REGIST_SAVED);
	entry = ck_registry_find_mac(&registry, other.server_mac);
	munit_assert_not_null(entry);
	munit_assert_string_equal(entry->last_host, "");
	return MUNIT_OK;
}

static MunitResult test_save_keeps_others(const MunitParameter params[], void *fixture)
{
	(void)params;
	TmpDir *dir = fixture;
	char path[512];
	dir_file(dir, "consoles.plist", path, sizeof(path));
	// ckctl registered two consoles after the app last read the file.
	save_hosts(path, 2);
	ChiakiRegisteredHost host = make_host(0x5f, "PS5 de la sala");
	CKRegistry registry;
	munit_assert_int(ck_ui_regist_save(path, &host, "192.168.0.20", &registry, NULL), ==, CK_UI_REGIST_SAVED);
	CKRegistry file = load(path);
	munit_assert_size(file.count, ==, 3);
	munit_assert_not_null(ck_registry_find_host_id(&file, "0A1B2C3D4E10"));
	munit_assert_not_null(ck_registry_find_host_id(&file, "0A1B2C3D4E11"));
	munit_assert_not_null(ck_registry_find_host_id(&file, "0A1B2C3D4E5F"));
	munit_assert_memory_equal(sizeof(file), &file, &registry);
	return MUNIT_OK;
}

static MunitResult test_save_replaces(const MunitParameter params[], void *fixture)
{
	(void)params;
	TmpDir *dir = fixture;
	char path[512];
	dir_file(dir, "consoles.plist", path, sizeof(path));
	save_hosts(path, 2);
	// Registering the same console again replaces its keys, name and IP.
	ChiakiRegisteredHost host = make_host(0x11, "PS5 nueva");
	memset(host.rp_key, 0x0f, sizeof(host.rp_key));
	CKRegistry registry;
	munit_assert_int(ck_ui_regist_save(path, &host, "192.168.0.44", &registry, NULL), ==, CK_UI_REGIST_SAVED);
	CKRegistry file = load(path);
	munit_assert_size(file.count, ==, 2);
	CKRegistryEntry *entry = ck_registry_find_mac(&file, host.server_mac);
	munit_assert_not_null(entry);
	munit_assert_string_equal(entry->host.server_nickname, "PS5 nueva");
	munit_assert_memory_equal(sizeof(host.rp_key), entry->host.rp_key, host.rp_key);
	munit_assert_string_equal(entry->last_host, "192.168.0.44");
	return MUNIT_OK;
}

static MunitResult test_save_full(const MunitParameter params[], void *fixture)
{
	(void)params;
	TmpDir *dir = fixture;
	char path[512];
	dir_file(dir, "consoles.plist", path, sizeof(path));
	save_hosts(path, CK_REGISTRY_MAX);
	char *before = read_file(path);
	CKRegistry registry;
	memset(&registry, 0x5a, sizeof(registry));
	CKRegistry untouched = registry;

	ChiakiRegisteredHost host = make_host(0x5f, "Una más");
	munit_assert_int(ck_ui_regist_save(path, &host, "192.168.0.20", &registry, NULL), ==, CK_UI_REGIST_FULL);
	char *after = read_file(path);
	munit_assert_string_equal(after, before);
	munit_assert_memory_equal(sizeof(registry), &registry, &untouched);
	free(after);
	free(before);

	// One already registered still fits: it replaces itself.
	host = make_host((uint8_t)(0x10 + CK_REGISTRY_MAX - 1), "La última");
	munit_assert_int(ck_ui_regist_save(path, &host, "192.168.0.21", &registry, NULL), ==, CK_UI_REGIST_SAVED);
	munit_assert_size(load(path).count, ==, CK_REGISTRY_MAX);
	return MUNIT_OK;
}

static MunitResult test_save_unreadable(const MunitParameter params[], void *fixture)
{
	(void)params;
	TmpDir *dir = fixture;
	char path[512];
	dir_file(dir, "consoles.plist", path, sizeof(path));
	// Not a registry: it may hold registrations the app cannot read, so it stays.
	static const char garbage[] = "<?xml version=\"1.0\"?><plist><array><string>no</string></array></plist>";
	write_file(path, garbage);
	ChiakiRegisteredHost host = make_host(0x5f, "PS5 de la sala");
	CKRegistry registry;
	ChiakiErrorCode err = CHIAKI_ERR_SUCCESS;
	munit_assert_int(ck_ui_regist_save(path, &host, "192.168.0.20", &registry, &err), ==, CK_UI_REGIST_UNREADABLE);
	munit_assert_int(err, !=, CHIAKI_ERR_SUCCESS);
	char *after = read_file(path);
	munit_assert_string_equal(after, garbage);
	free(after);
	// err may be NULL.
	munit_assert_int(ck_ui_regist_save(path, &host, NULL, &registry, NULL), ==, CK_UI_REGIST_UNREADABLE);
	return MUNIT_OK;
}

static MunitResult test_save_unwritable(const MunitParameter params[], void *fixture)
{
	(void)params;
	TmpDir *dir = fixture;
	char path[512];
	dir_file(dir, "consoles.plist", path, sizeof(path));
	save_hosts(path, 1);
	char *before = read_file(path);
	// The temporary file's name is taken by a folder, which even root cannot open for writing.
	char tmp[600];
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	munit_assert_int(mkdir(tmp, 0755), ==, 0);
	ChiakiRegisteredHost host = make_host(0x5f, "PS5 de la sala");
	CKRegistry registry;
	ChiakiErrorCode err = CHIAKI_ERR_SUCCESS;
	munit_assert_int(ck_ui_regist_save(path, &host, "192.168.0.20", &registry, &err), ==, CK_UI_REGIST_UNWRITABLE);
	munit_assert_int(err, !=, CHIAKI_ERR_SUCCESS);
	char *after = read_file(path);
	munit_assert_string_equal(after, before);
	free(after);
	free(before);
	munit_assert_int(rmdir(tmp), ==, 0);
	return MUNIT_OK;
}

// ---------------------------------------------------------------------------
// The sentence
// ---------------------------------------------------------------------------

static CKRegistOutcome outcome(CKRegistResult result, int http_code, uint32_t reason)
{
	CKRegistOutcome o;
	memset(&o, 0, sizeof(o));
	o.result = result;
	o.http_code = http_code;
	o.reason = reason;
	return o;
}

static MunitResult test_message(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	char out[CK_UI_REGIST_MESSAGE_SIZE];
	char expected[CK_UI_REGIST_MESSAGE_SIZE];

	CKRegistOutcome o = outcome(CK_REGIST_SUCCESS, 0, 0);
	ck_ui_regist_message(&o, out, sizeof(out));
	munit_assert_string_equal(out, ck_regist_result_string(CK_REGIST_SUCCESS));

	o = outcome(CK_REGIST_WRONG_PIN, 0, CHIAKI_RP_APPLICATION_REASON_REGIST_FAILED);
	ck_ui_regist_message(&o, out, sizeof(out));
	snprintf(expected, sizeof(expected), "%s (razón 0x80108b09)", ck_regist_result_string(CK_REGIST_WRONG_PIN));
	munit_assert_string_equal(out, expected);

	o = outcome(CK_REGIST_REJECTED, 403, 0);
	ck_ui_regist_message(&o, out, sizeof(out));
	snprintf(expected, sizeof(expected), "%s (HTTP 403)", ck_regist_result_string(CK_REGIST_REJECTED));
	munit_assert_string_equal(out, expected);

	o = outcome(CK_REGIST_REJECTED, 403, 0x80108b10);
	ck_ui_regist_message(&o, out, sizeof(out));
	snprintf(expected, sizeof(expected), "%s (HTTP 403, razón 0x80108b10)", ck_regist_result_string(CK_REGIST_REJECTED));
	munit_assert_string_equal(out, expected);

	// Every result, with both codes, fits the room the header promises.
	for(int r = CK_REGIST_SUCCESS; r <= CK_REGIST_FAILED; r++)
	{
		o = outcome((CKRegistResult)r, 503, 0xffffffffu);
		int len = snprintf(expected, sizeof(expected), "%s (HTTP 503, razón 0xffffffff)",
			ck_regist_result_string((CKRegistResult)r));
		munit_assert_int(len, <, (int)sizeof(expected));
		ck_ui_regist_message(&o, out, sizeof(out));
		munit_assert_string_equal(out, expected);
	}
	return MUNIT_OK;
}

/** Whether text is whole UTF-8: no sequence cut short. */
static bool whole_utf8(const char *text)
{
	const unsigned char *p = (const unsigned char *)text;
	while(*p)
	{
		size_t need = *p >= 0xf0 ? 4 : *p >= 0xe0 ? 3 : *p >= 0xc0 ? 2 : 1;
		for(size_t i = 1; i < need; i++)
			if((p[i] & 0xc0) != 0x80)
				return false;
		p += need;
	}
	return true;
}

static MunitResult test_message_cut(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	// "La consola rechazó el PIN...": the ó is two bytes, the 18th and the 19th.
	CKRegistOutcome o = outcome(CK_REGIST_WRONG_PIN, 0, 0);
	const char *full = ck_regist_result_string(CK_REGIST_WRONG_PIN);
	munit_assert_int(strncmp(full, "La consola rechaz\xc3\xb3", 19), ==, 0);
	char out[64];
	for(size_t size = 1; size <= 40; size++)
	{
		memset(out, 'x', sizeof(out));
		ck_ui_regist_message(&o, out, size);
		size_t len = strlen(out);
		munit_assert_size(len, <, size);
		munit_assert_true(whole_utf8(out));
		munit_assert_int(strncmp(out, full, len), ==, 0);
		// Only a cut character is dropped, never more.
		munit_assert_size(len + 2, >=, size);
	}
	// Cutting right inside the ó drops it whole.
	ck_ui_regist_message(&o, out, 19);
	munit_assert_string_equal(out, "La consola rechaz");

	// No room at all writes nothing.
	out[0] = 'x';
	ck_ui_regist_message(&o, out, 0);
	munit_assert_char(out[0], ==, 'x');
	return MUNIT_OK;
}

#define TEST(name, fn) { name, fn, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }
#define DIR_TEST(name, fn) { name, fn, setup_dir, teardown_dir, MUNIT_TEST_OPTION_NONE, NULL }

static MunitTest tests[] = {
	DIR_TEST("/save_new_file", test_save_new_file),
	DIR_TEST("/save_keeps_others", test_save_keeps_others),
	DIR_TEST("/save_replaces", test_save_replaces),
	DIR_TEST("/save_full", test_save_full),
	DIR_TEST("/save_unreadable", test_save_unreadable),
	DIR_TEST("/save_unwritable", test_save_unwritable),
	TEST("/message", test_message),
	TEST("/message_cut", test_message_cut),
	{ NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
};

static const MunitSuite suite = { "/ui_regist", tests, NULL, 1, MUNIT_SUITE_OPTION_NONE };

int main(int argc, char *argv[])
{
	return munit_suite_main(&suite, NULL, argc, argv);
}
