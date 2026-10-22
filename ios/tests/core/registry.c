// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A2.5: the saved registrations of Bridge/ck_registry.h. Each test works in a
// folder of its own under the system's temporary folder. Every key in here is
// made up. check_registry_plist.py checks the same files with Python's plistlib.

// nftw() and mkdtemp(), for the temporary folders.
#define _XOPEN_SOURCE 700

#include <munit.h>

#include "Bridge/ck_registry.h"

#include <chiaki/base64.h>

#include <errno.h>
#include <ftw.h>
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
	snprintf(dir->path, sizeof(dir->path), "%s/ck-registry-XXXXXX", tmp && *tmp ? tmp : "/tmp");
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

static void write_file(const char *path, const char *text, size_t len)
{
	FILE *f = fopen(path, "wb");
	munit_assert_not_null(f);
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

/** A registration as the core hands it out, with fields C5 does not keep set too. */
static ChiakiRegisteredHost make_host(uint8_t mac_last, const char *nickname, const char *regist_key)
{
	ChiakiRegisteredHost host;
	memset(&host, 0, sizeof(host));
	host.target = CHIAKI_TARGET_PS5_1;
	static const uint8_t mac[6] = { 0x0a, 0x1b, 0x2c, 0x3d, 0x4e, 0x00 };
	memcpy(host.server_mac, mac, sizeof(mac));
	host.server_mac[5] = mac_last;
	snprintf(host.server_nickname, sizeof(host.server_nickname), "%s", nickname);
	memcpy(host.rp_regist_key, regist_key, strlen(regist_key));
	host.rp_key_type = 2;
	memset(host.rp_key, 0xf0, sizeof(host.rp_key));
	host.rp_key[0] = mac_last;
	snprintf(host.ap_ssid, sizeof(host.ap_ssid), "PS5-AP");
	host.console_pin = 1234;
	return host;
}

static void assert_entries_equal(const CKRegistryEntry *a, const CKRegistryEntry *b)
{
	const ChiakiRegisteredHost *x = &a->host;
	const ChiakiRegisteredHost *y = &b->host;
	munit_assert_int(x->target, ==, y->target);
	munit_assert_memory_equal(sizeof(x->server_mac), x->server_mac, y->server_mac);
	munit_assert_memory_equal(sizeof(x->server_nickname), x->server_nickname, y->server_nickname);
	munit_assert_memory_equal(sizeof(x->rp_regist_key), x->rp_regist_key, y->rp_regist_key);
	munit_assert_uint32(x->rp_key_type, ==, y->rp_key_type);
	munit_assert_memory_equal(sizeof(x->rp_key), x->rp_key, y->rp_key);
	munit_assert_string_equal(a->last_host, b->last_host);
	// Nothing else survives a save, so nothing else may be set before one either.
	munit_assert_memory_equal(sizeof(*a), a, b);
}

// ---------------------------------------------------------------------------
// In memory
// ---------------------------------------------------------------------------

static MunitResult test_mac_hex(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	char hex[13];
	static const uint8_t mac[6] = { 0x0a, 0x1b, 0x2c, 0x3d, 0x4e, 0x5f };
	ck_registry_mac_hex(mac, hex);
	munit_assert_string_equal(hex, "0A1B2C3D4E5F");
	static const uint8_t edges[6] = { 0x00, 0xff, 0x09, 0x90, 0xa0, 0x0f };
	ck_registry_mac_hex(edges, hex);
	munit_assert_string_equal(hex, "00FF0990A00F");
	return MUNIT_OK;
}

static MunitResult test_put_find_remove(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	CKRegistry registry;
	memset(&registry, 0, sizeof(registry));
	ChiakiRegisteredHost sala = make_host(0x5f, "Sala", "1a2b3c4d");
	ChiakiRegisteredHost cuarto = make_host(0x60, "Cuarto", "00ff00ff");
	munit_assert_int(ck_registry_put(&registry, &sala, "192.168.0.10"), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_int(ck_registry_put(&registry, &cuarto, NULL), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_size(registry.count, ==, 2);

	// What C5 keeps, and nothing more.
	CKRegistryEntry *entry = &registry.entries[0];
	munit_assert_string_equal(entry->host.server_nickname, "Sala");
	munit_assert_string_equal(entry->last_host, "192.168.0.10");
	munit_assert_char(entry->host.ap_ssid[0], ==, '\0');
	munit_assert_uint32(entry->host.console_pin, ==, 0);
	munit_assert_string_equal(registry.entries[1].last_host, "");

	munit_assert_ptr_equal(ck_registry_find_mac(&registry, sala.server_mac), &registry.entries[0]);
	munit_assert_ptr_equal(ck_registry_find_host_id(&registry, "0A1B2C3D4E5F"), &registry.entries[0]);
	munit_assert_ptr_equal(ck_registry_find_host_id(&registry, "0a1b2c3d4e60"), &registry.entries[1]);
	munit_assert_null(ck_registry_find_host_id(&registry, "0A1B2C3D4E61"));
	munit_assert_null(ck_registry_find_host_id(&registry, "0A1B2C3D4E5"));
	munit_assert_null(ck_registry_find_host_id(&registry, "0A1B2C3D4E5FF"));
	munit_assert_null(ck_registry_find_host_id(&registry, "0A1B2C3D4EZZ"));
	munit_assert_null(ck_registry_find_host_id(&registry, NULL));

	munit_assert_ptr_equal(ck_registry_find(&registry, "0a1b2c3d4e5f"), &registry.entries[0]);
	munit_assert_ptr_equal(ck_registry_find(&registry, "192.168.0.10"), &registry.entries[0]);
	munit_assert_ptr_equal(ck_registry_find(&registry, "Cuarto"), &registry.entries[1]);
	munit_assert_null(ck_registry_find(&registry, "cuarto"));
	munit_assert_null(ck_registry_find(&registry, "Cuart"));
	munit_assert_null(ck_registry_find(&registry, ""));
	munit_assert_null(ck_registry_find(&registry, NULL));

	// The same console again replaces its entry; a NULL address keeps the old one.
	ChiakiRegisteredHost again = make_host(0x5f, "Sala nueva", "99999999");
	munit_assert_int(ck_registry_put(&registry, &again, NULL), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_size(registry.count, ==, 2);
	munit_assert_string_equal(registry.entries[0].host.server_nickname, "Sala nueva");
	munit_assert_memory_equal(8, registry.entries[0].host.rp_regist_key, "99999999");
	munit_assert_string_equal(registry.entries[0].last_host, "192.168.0.10");
	munit_assert_int(ck_registry_put(&registry, &again, "192.168.0.20"), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_string_equal(registry.entries[0].last_host, "192.168.0.20");

	// Full.
	for(uint8_t i = 0; registry.count < CK_REGISTRY_MAX; i++)
	{
		ChiakiRegisteredHost more = make_host((uint8_t)(0x70 + i), "Otra", "12345678");
		munit_assert_int(ck_registry_put(&registry, &more, NULL), ==, CHIAKI_ERR_SUCCESS);
	}
	ChiakiRegisteredHost extra = make_host(0x99, "Extra", "12345678");
	munit_assert_int(ck_registry_put(&registry, &extra, NULL), ==, CHIAKI_ERR_OVERFLOW);
	munit_assert_size(registry.count, ==, CK_REGISTRY_MAX);
	// Replacing still works when full.
	munit_assert_int(ck_registry_put(&registry, &cuarto, "192.168.0.30"), ==, CHIAKI_ERR_SUCCESS);

	// Removing keeps the order of the rest.
	munit_assert_true(ck_registry_remove(&registry, &registry.entries[0]));
	munit_assert_size(registry.count, ==, CK_REGISTRY_MAX - 1);
	munit_assert_string_equal(registry.entries[0].host.server_nickname, "Cuarto");
	munit_assert_string_equal(registry.entries[0].last_host, "192.168.0.30");
	munit_assert_false(ck_registry_remove(&registry, NULL));
	munit_assert_false(ck_registry_remove(&registry, &registry.entries[CK_REGISTRY_MAX - 1]));
	CKRegistryEntry outside;
	munit_assert_false(ck_registry_remove(&registry, &outside));
	while(registry.count)
		munit_assert_true(ck_registry_remove(&registry, &registry.entries[registry.count - 1]));
	munit_assert_null(ck_registry_find(&registry, "Cuarto"));
	return MUNIT_OK;
}

/** A nickname that fills the field has no NUL: it is cut whole characters at a time. */
static MunitResult test_put_long_nickname(const MunitParameter params[], void *user)
{
	(void)params;
	(void)user;
	ChiakiRegisteredHost host = make_host(0x5f, "", "1a2b3c4d");
	// 30 ASCII bytes and the first byte of a 2-byte "ñ", then the second.
	memset(host.server_nickname, 'a', 30);
	host.server_nickname[30] = '\xc3';
	host.server_nickname[31] = '\xb1';
	CKRegistry registry;
	memset(&registry, 0, sizeof(registry));
	munit_assert_int(ck_registry_put(&registry, &host, NULL), ==, CHIAKI_ERR_SUCCESS);
	const char *nickname = registry.entries[0].host.server_nickname;
	munit_assert_size(strlen(nickname), ==, 30);
	munit_assert_char(nickname[29], ==, 'a');

	// A full key has no NUL either, and stays whole.
	memcpy(host.rp_regist_key, "FFFFFFFF00000000", sizeof(host.rp_regist_key));
	munit_assert_int(ck_registry_put(&registry, &host, NULL), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_memory_equal(sizeof(host.rp_regist_key), registry.entries[0].host.rp_regist_key, "FFFFFFFF00000000");
	return MUNIT_OK;
}

// ---------------------------------------------------------------------------
// On disk
// ---------------------------------------------------------------------------

static MunitResult test_round_trip(const MunitParameter params[], void *fixture)
{
	(void)params;
	TmpDir *dir = fixture;
	char path[512];
	// Folders that do not exist yet, as on a fresh iPad.
	dir_file(dir, "Library/Chiakiport/consoles.plist", path, sizeof(path));

	CKRegistry registry;
	memset(&registry, 0, sizeof(registry));
	ChiakiRegisteredHost a = make_host(0x5f, "Sala & <PS5> \"ñ\"", "1a2b3c4d");
	ChiakiRegisteredHost b = make_host(0x60, "Cuarto", "FFFFFFFF00000000");
	b.target = CHIAKI_TARGET_PS4_10;
	b.rp_key_type = 0;
	ChiakiRegisteredHost c = make_host(0x61, "", "1");
	munit_assert_int(ck_registry_put(&registry, &a, "192.168.0.10"), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_int(ck_registry_put(&registry, &b, NULL), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_int(ck_registry_put(&registry, &c, "fe80::1%en0"), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_int(ck_registry_save(&registry, path), ==, CHIAKI_ERR_SUCCESS);

	struct stat st;
	munit_assert_int(stat(path, &st), ==, 0);
	munit_assert_int(st.st_mode & 0777, ==, 0600);
	char tmp[600];
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	munit_assert_int(access(tmp, F_OK), !=, 0);

	char *text = read_file(path);
	munit_assert_not_null(strstr(text, "<key>0A1B2C3D4E5F</key>"));
	munit_assert_not_null(strstr(text, "<string>Sala &amp; &lt;PS5&gt; \"ñ\"</string>"));
	munit_assert_null(strstr(text, "1a2b3c4d"));
	munit_assert_null(strstr(text, "0a1b2c3d4e5f"));
	free(text);

	CKRegistry loaded;
	memset(&loaded, 0xaa, sizeof(loaded));
	munit_assert_int(ck_registry_load(&loaded, path), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_size(loaded.count, ==, 3);
	for(size_t i = 0; i < 3; i++)
		assert_entries_equal(&loaded.entries[i], &registry.entries[i]);

	// Saving over it replaces it.
	munit_assert_true(ck_registry_remove(&loaded, &loaded.entries[1]));
	munit_assert_int(ck_registry_save(&loaded, path), ==, CHIAKI_ERR_SUCCESS);
	CKRegistry again;
	munit_assert_int(ck_registry_load(&again, path), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_size(again.count, ==, 2);
	assert_entries_equal(&again.entries[0], &registry.entries[0]);
	assert_entries_equal(&again.entries[1], &registry.entries[2]);
	return MUNIT_OK;
}

static MunitResult test_empty_and_missing(const MunitParameter params[], void *fixture)
{
	(void)params;
	TmpDir *dir = fixture;
	char path[512];
	dir_file(dir, "consoles.plist", path, sizeof(path));

	CKRegistry registry;
	memset(&registry, 0xaa, sizeof(registry));
	munit_assert_int(ck_registry_load(&registry, path), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_size(registry.count, ==, 0);

	munit_assert_int(ck_registry_save(&registry, path), ==, CHIAKI_ERR_SUCCESS);
	char *text = read_file(path);
	munit_assert_not_null(strstr(text, "<plist version=\"1.0\">\n<dict/>\n</plist>\n"));
	free(text);
	registry.count = 5;
	munit_assert_int(ck_registry_load(&registry, path), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_size(registry.count, ==, 0);

	// A file in the current folder: no folder to create.
	char cwd[512];
	munit_assert_not_null(getcwd(cwd, sizeof(cwd)));
	munit_assert_int(chdir(dir->path), ==, 0);
	ChiakiErrorCode err = ck_registry_save(&registry, "here.plist");
	int access_r = access("here.plist", R_OK);
	munit_assert_int(chdir(cwd), ==, 0);
	munit_assert_int(err, ==, CHIAKI_ERR_SUCCESS);
	munit_assert_int(access_r, ==, 0);
	return MUNIT_OK;
}

/**
 * What NSPropertyListSerialization writes for the same dictionary, with keys
 * the app may add next to the consoles and inside them, comments, entities,
 * line-wrapped data and rp_regist_key typed in by hand as a string.
 */
static const char foundation_plist[] =
	"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
	"<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
	"<plist version=\"1.0\">\n"
	"<dict>\n"
	"\t<!-- the app's own settings -->\n"
	"\t<key>0A1B2C3D4E5F</key>\n"
	"\t<dict>\n"
	"\t\t<key>favorite</key>\n"
	"\t\t<true/>\n"
	"\t\t<key>host</key>\n"
	"\t\t<string>192.168.0.10</string>\n"
	"\t\t<key>last_seen</key>\n"
	"\t\t<date>2026-10-22T12:00:00Z</date>\n"
	"\t\t<key>rp_key</key>\n"
	"\t\t<data>\n"
	"\t\t8PDw8PDw8PDw8PDw8PDw8A==\n"
	"\t\t</data>\n"
	"\t\t<key>rp_key_type</key>\n"
	"\t\t<integer>2</integer>\n"
	"\t\t<key>rp_regist_key</key>\n"
	"\t\t<string>1a2b3c4d</string>\n"
	"\t\t<key>server_mac</key>\n"
	"\t\t<data>\n"
	"\t\tChssPU5f\n"
	"\t\t</data>\n"
	"\t\t<key>server_nickname</key>\n"
	"\t\t<string>Sala &amp; tele &#38; m&#xe1;s</string>\n"
	"\t\t<key>streams</key>\n"
	"\t\t<array>\n"
	"\t\t\t<dict>\n"
	"\t\t\t\t<key>fps</key>\n"
	"\t\t\t\t<integer>30</integer>\n"
	"\t\t\t</dict>\n"
	"\t\t\t<array/>\n"
	"\t\t\t<string>x</string>\n"
	"\t\t</array>\n"
	"\t\t<key>target</key>\n"
	"\t\t<integer>1000100</integer>\n"
	"\t</dict>\n"
	"\t<key>order</key>\n"
	"\t<array>\n"
	"\t\t<string>0A1B2C3D4E5F</string>\n"
	"\t</array>\n"
	"\t<key>version</key>\n"
	"\t<integer>1</integer>\n"
	"\t<key>0A1B2C3D4E60</key>\n"
	"\t<dict>\n"
	"\t\t<key>host</key>\n"
	"\t\t<string/>\n"
	"\t\t<key>server_mac</key>\n"
	"\t\t<data>ChssPU5g</data>\n"
	"\t</dict>\n"
	"</dict>\n"
	"</plist>\n";

static MunitResult test_foundation_plist(const MunitParameter params[], void *fixture)
{
	(void)params;
	TmpDir *dir = fixture;
	char path[512];
	dir_file(dir, "consoles.plist", path, sizeof(path));
	write_file(path, foundation_plist, strlen(foundation_plist));

	CKRegistry registry;
	munit_assert_int(ck_registry_load(&registry, path), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_size(registry.count, ==, 2);
	const CKRegistryEntry *entry = &registry.entries[0];
	static const uint8_t mac[6] = { 0x0a, 0x1b, 0x2c, 0x3d, 0x4e, 0x5f };
	munit_assert_memory_equal(6, entry->host.server_mac, mac);
	munit_assert_int(entry->host.target, ==, CHIAKI_TARGET_PS5_1);
	// Characters past ASCII as numeric entities come out as '?': nicknames are written as UTF-8.
	munit_assert_string_equal(entry->host.server_nickname, "Sala & tele & m?s");
	munit_assert_memory_equal(8, entry->host.rp_regist_key, "1a2b3c4d");
	munit_assert_char(entry->host.rp_regist_key[8], ==, '\0');
	munit_assert_uint32(entry->host.rp_key_type, ==, 2);
	uint8_t rp_key[16];
	memset(rp_key, 0xf0, sizeof(rp_key));
	munit_assert_memory_equal(16, entry->host.rp_key, rp_key);
	munit_assert_string_equal(entry->last_host, "192.168.0.10");

	// Only the MAC is required.
	entry = &registry.entries[1];
	munit_assert_uint8(entry->host.server_mac[5], ==, 0x60);
	munit_assert_string_equal(entry->last_host, "");
	munit_assert_char(entry->host.rp_regist_key[0], ==, '\0');
	return MUNIT_OK;
}

#define PLIST_HEAD "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<plist version=\"1.0\">\n"
#define ENTRY_OPEN "<dict><key>0A1B2C3D4E5F</key><dict>"
#define MAC "<key>server_mac</key><data>ChssPU5f</data>"
#define ENTRY_CLOSE "</dict></dict>\n</plist>\n"

static MunitResult test_corrupt_files(const MunitParameter params[], void *fixture)
{
	(void)params;
	TmpDir *dir = fixture;
	char path[512];
	dir_file(dir, "consoles.plist", path, sizeof(path));

	// Each one must fail, so ckctl leaves it alone instead of saving over it.
	static const char *const bad[] = {
		"",
		"no soy un plist",
		"bplist00\xd1\x01\x02",
		PLIST_HEAD,
		PLIST_HEAD "<dict>",
		PLIST_HEAD "<dict></dict>\n",
		PLIST_HEAD "<array></array>\n</plist>\n",
		PLIST_HEAD "<dict><string>x</string></dict>\n</plist>\n",
		PLIST_HEAD ENTRY_OPEN "<key>host</key><string>1.2.3.4</string>" ENTRY_CLOSE,
		PLIST_HEAD ENTRY_OPEN "<key>server_mac</key><data>ChssPU4=</data>" ENTRY_CLOSE,
		PLIST_HEAD ENTRY_OPEN "<key>server_mac</key><data>ChssPU5fAA==</data>" ENTRY_CLOSE,
		PLIST_HEAD ENTRY_OPEN "<key>server_mac</key><data>@@@@@@@@</data>" ENTRY_CLOSE,
		PLIST_HEAD ENTRY_OPEN "<key>server_mac</key><data>Chss\xc3\xb1PU5f</data>" ENTRY_CLOSE,
		PLIST_HEAD ENTRY_OPEN MAC "<key>rp_key</key><data>8PDw8PDw8PDw8PDw8PDw8PA=</data>" ENTRY_CLOSE,
		PLIST_HEAD ENTRY_OPEN MAC "<key>rp_regist_key</key><string>12345678901234567</string>" ENTRY_CLOSE,
		PLIST_HEAD ENTRY_OPEN MAC "<key>rp_key_type</key><integer>-1</integer>" ENTRY_CLOSE,
		PLIST_HEAD ENTRY_OPEN MAC "<key>rp_key_type</key><integer>4294967296</integer>" ENTRY_CLOSE,
		PLIST_HEAD ENTRY_OPEN MAC "<key>target</key><integer>12abc</integer>" ENTRY_CLOSE,
		PLIST_HEAD ENTRY_OPEN MAC "<key>target</key><integer></integer>" ENTRY_CLOSE,
		PLIST_HEAD ENTRY_OPEN MAC "<key>server_nickname</key><string>a &bogus; b</string>" ENTRY_CLOSE,
		PLIST_HEAD ENTRY_OPEN MAC "<key>server_nickname</key><string>a <b>c</b></string>" ENTRY_CLOSE,
		PLIST_HEAD ENTRY_OPEN MAC "<key>streams</key><array><dict></array>" ENTRY_CLOSE,
		PLIST_HEAD ENTRY_OPEN MAC "<key>a_very_long_tag_name</key><averyveryverylongtag>1</averyveryverylongtag>" ENTRY_CLOSE,
		PLIST_HEAD ENTRY_OPEN MAC "</dict></dict>\n",
		PLIST_HEAD ENTRY_OPEN MAC "<!-- never closed ",
	};
	for(size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++)
	{
		write_file(path, bad[i], strlen(bad[i]));
		CKRegistry registry;
		memset(&registry, 0xaa, sizeof(registry));
		ChiakiErrorCode err = ck_registry_load(&registry, path);
		if(err == CHIAKI_ERR_SUCCESS)
			munit_errorf("loaded bad file %zu:\n%s", i, bad[i]);
		munit_assert_int(err, ==, CHIAKI_ERR_INVALID_DATA);
		munit_assert_size(registry.count, ==, 0);
	}

	// A good file is still good after all that.
	static const char good[] = PLIST_HEAD ENTRY_OPEN MAC ENTRY_CLOSE;
	write_file(path, good, strlen(good));
	CKRegistry registry;
	munit_assert_int(ck_registry_load(&registry, path), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_size(registry.count, ==, 1);
	return MUNIT_OK;
}

static MunitResult test_too_many_or_too_big(const MunitParameter params[], void *fixture)
{
	(void)params;
	TmpDir *dir = fixture;
	char path[512];
	dir_file(dir, "consoles.plist", path, sizeof(path));

	// More consoles than a registry holds: the file is someone else's to trim.
	char text[4096];
	size_t len = (size_t)snprintf(text, sizeof(text), PLIST_HEAD "<dict>\n");
	for(int i = 0; i <= CK_REGISTRY_MAX; i++)
	{
		uint8_t mac[6] = { 0x0a, 0x1b, 0x2c, 0x3d, 0x4e, (uint8_t)i };
		char b64[16];
		munit_assert_int(chiaki_base64_encode(mac, sizeof(mac), b64, sizeof(b64)), ==, CHIAKI_ERR_SUCCESS);
		len += (size_t)snprintf(text + len, sizeof(text) - len,
			"<key>0A1B2C3D4E%02X</key><dict><key>server_mac</key><data>%s</data></dict>\n", i, b64);
		munit_assert_size(len, <, sizeof(text));
	}
	len += (size_t)snprintf(text + len, sizeof(text) - len, "</dict>\n</plist>\n");
	write_file(path, text, len);
	CKRegistry registry;
	munit_assert_int(ck_registry_load(&registry, path), ==, CHIAKI_ERR_OVERFLOW);
	munit_assert_size(registry.count, ==, 0);

	// A valid registry padded past 256 KB is not read at all.
	static const char good[] = PLIST_HEAD ENTRY_OPEN MAC ENTRY_CLOSE;
	size_t big_size = 256 * 1024 + 1;
	char *big = malloc(big_size);
	munit_assert_not_null(big);
	memset(big, ' ', big_size);
	memcpy(big, good, strlen(good));
	write_file(path, big, big_size);
	free(big);
	munit_assert_int(ck_registry_load(&registry, path), ==, CHIAKI_ERR_INVALID_DATA);
	return MUNIT_OK;
}

/** A save that cannot finish leaves the old file as it was. */
static MunitResult test_save_errors(const MunitParameter params[], void *fixture)
{
	(void)params;
	TmpDir *dir = fixture;
	char path[512];
	dir_file(dir, "consoles.plist", path, sizeof(path));
	CKRegistry registry;
	memset(&registry, 0, sizeof(registry));
	ChiakiRegisteredHost host = make_host(0x5f, "Sala", "1a2b3c4d");
	munit_assert_int(ck_registry_put(&registry, &host, "192.168.0.10"), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_int(ck_registry_save(&registry, path), ==, CHIAKI_ERR_SUCCESS);
	char *before = read_file(path);

	// The temporary file's name is taken by a folder, which even root cannot open for writing.
	char tmp[600];
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	munit_assert_int(mkdir(tmp, 0755), ==, 0);
	CKRegistry other;
	memset(&other, 0, sizeof(other));
	munit_assert_int(ck_registry_save(&other, path), !=, CHIAKI_ERR_SUCCESS);
	char *after = read_file(path);
	munit_assert_string_equal(after, before);
	free(after);
	free(before);
	munit_assert_int(rmdir(tmp), ==, 0);

	// A folder in the path is a file.
	char blocked[600];
	snprintf(blocked, sizeof(blocked), "%s/sub/consoles.plist", path);
	munit_assert_int(ck_registry_save(&registry, blocked), !=, CHIAKI_ERR_SUCCESS);

	// A folder where the file should be cannot be read.
	char folder[512];
	dir_file(dir, "folder.plist", folder, sizeof(folder));
	munit_assert_int(mkdir(folder, 0755), ==, 0);
	munit_assert_int(ck_registry_load(&registry, folder), !=, CHIAKI_ERR_SUCCESS);
	munit_assert_size(registry.count, ==, 0);
	return MUNIT_OK;
}

#define TEST(name, fn) { name, fn, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }
#define DIR_TEST(name, fn) { name, fn, setup_dir, teardown_dir, MUNIT_TEST_OPTION_NONE, NULL }

static MunitTest tests[] = {
	TEST("/mac_hex", test_mac_hex),
	TEST("/put_find_remove", test_put_find_remove),
	TEST("/put_long_nickname", test_put_long_nickname),
	DIR_TEST("/round_trip", test_round_trip),
	DIR_TEST("/empty_and_missing", test_empty_and_missing),
	DIR_TEST("/foundation_plist", test_foundation_plist),
	DIR_TEST("/corrupt_files", test_corrupt_files),
	DIR_TEST("/too_many_or_too_big", test_too_many_or_too_big),
	DIR_TEST("/save_errors", test_save_errors),
	{ NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL },
};

static const MunitSuite suite = { "/registry", tests, NULL, 1, MUNIT_SUITE_OPTION_NONE };

int main(int argc, char *argv[])
{
	return munit_suite_main(&suite, NULL, argc, argv);
}
