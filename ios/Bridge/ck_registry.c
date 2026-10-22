// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A2.5: the registration plist of contract C5 (ck_registry.h). A small
// writer, and a reader for the XML plist subset that it and Foundation
// write. Nothing here depends on the platform beyond POSIX files.

#include "ck_registry.h"

#include <chiaki/base64.h>

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

/** A registry is a few KB; anything much larger is not one. */
#define FILE_MAX (256 * 1024)
#define TEXT_MAX 1024
#define NAME_MAX_LEN 16

static const char hex_digits[] = "0123456789ABCDEF";

void ck_registry_mac_hex(const uint8_t mac[6], char out[13])
{
	for(size_t i = 0; i < 6; i++)
	{
		out[2 * i] = hex_digits[mac[i] >> 4];
		out[2 * i + 1] = hex_digits[mac[i] & 0xf];
	}
	out[12] = '\0';
}

/** Copies src into dst, cut at a UTF-8 character boundary to fit, NUL-terminated. */
static void copy_utf8(char *dst, size_t dst_size, const char *src, size_t len)
{
	if(len >= dst_size)
	{
		len = dst_size - 1;
		while(len && ((unsigned char)src[len] & 0xc0) == 0x80)
			len--;
	}
	memcpy(dst, src, len);
	dst[len] = '\0';
}

// ---------------------------------------------------------------------------
// Lookups
// ---------------------------------------------------------------------------

CKRegistryEntry *ck_registry_find_mac(CKRegistry *registry, const uint8_t mac[6])
{
	for(size_t i = 0; i < registry->count; i++)
		if(!memcmp(registry->entries[i].host.server_mac, mac, 6))
			return &registry->entries[i];
	return NULL;
}

static int hex_value(char c)
{
	if(c >= '0' && c <= '9')
		return c - '0';
	if(c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if(c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

static bool parse_mac_hex(const char *text, uint8_t mac[6])
{
	if(strlen(text) != 12)
		return false;
	for(size_t i = 0; i < 6; i++)
	{
		int hi = hex_value(text[2 * i]);
		int lo = hex_value(text[2 * i + 1]);
		if(hi < 0 || lo < 0)
			return false;
		mac[i] = (uint8_t)(hi << 4 | lo);
	}
	return true;
}

CKRegistryEntry *ck_registry_find_host_id(CKRegistry *registry, const char *host_id)
{
	uint8_t mac[6];
	if(!host_id || !parse_mac_hex(host_id, mac))
		return NULL;
	return ck_registry_find_mac(registry, mac);
}

CKRegistryEntry *ck_registry_find(CKRegistry *registry, const char *query)
{
	if(!query || !*query)
		return NULL;
	CKRegistryEntry *entry = ck_registry_find_host_id(registry, query);
	if(entry)
		return entry;
	for(size_t i = 0; i < registry->count; i++)
		if(!strcmp(registry->entries[i].last_host, query))
			return &registry->entries[i];
	for(size_t i = 0; i < registry->count; i++)
	{
		const char *nickname = registry->entries[i].host.server_nickname;
		size_t len = strnlen(nickname, sizeof(registry->entries[i].host.server_nickname));
		if(strlen(query) == len && !memcmp(nickname, query, len))
			return &registry->entries[i];
	}
	return NULL;
}

/** Only the fields C5 keeps, so an entry looks the same before and after a save. */
static void keep_c5_fields(ChiakiRegisteredHost *dst, const ChiakiRegisteredHost *src)
{
	memset(dst, 0, sizeof(*dst));
	dst->target = src->target;
	copy_utf8(dst->server_nickname, sizeof(dst->server_nickname), src->server_nickname,
		strnlen(src->server_nickname, sizeof(src->server_nickname)));
	memcpy(dst->server_mac, src->server_mac, sizeof(dst->server_mac));
	memcpy(dst->rp_regist_key, src->rp_regist_key, sizeof(dst->rp_regist_key));
	dst->rp_key_type = src->rp_key_type;
	memcpy(dst->rp_key, src->rp_key, sizeof(dst->rp_key));
}

ChiakiErrorCode ck_registry_put(CKRegistry *registry, const ChiakiRegisteredHost *host, const char *last_host)
{
	CKRegistryEntry *entry = ck_registry_find_mac(registry, host->server_mac);
	if(!entry)
	{
		if(registry->count >= CK_REGISTRY_MAX)
			return CHIAKI_ERR_OVERFLOW;
		entry = &registry->entries[registry->count++];
		memset(entry, 0, sizeof(*entry));
	}
	keep_c5_fields(&entry->host, host);
	if(last_host)
		copy_utf8(entry->last_host, sizeof(entry->last_host), last_host, strlen(last_host));
	return CHIAKI_ERR_SUCCESS;
}

bool ck_registry_remove(CKRegistry *registry, CKRegistryEntry *entry)
{
	if(!entry || entry < registry->entries || entry >= registry->entries + registry->count)
		return false;
	size_t index = (size_t)(entry - registry->entries);
	memmove(entry, entry + 1, (registry->count - index - 1) * sizeof(*entry));
	registry->count--;
	return true;
}

// ---------------------------------------------------------------------------
// Writing
// ---------------------------------------------------------------------------

static void write_string(FILE *f, const char *s, size_t len)
{
	for(size_t i = 0; i < len; i++)
	{
		unsigned char c = (unsigned char)s[i];
		switch(c)
		{
			case '&':
				fputs("&amp;", f);
				break;
			case '<':
				fputs("&lt;", f);
				break;
			case '>':
				fputs("&gt;", f);
				break;
			default:
				// XML 1.0 has no other control characters.
				fputc(c < 0x20 && c != '\t' && c != '\n' && c != '\r' ? '?' : c, f);
				break;
		}
	}
}

static bool write_data(FILE *f, const uint8_t *buf, size_t size)
{
	char b64[64];
	if(chiaki_base64_encode(buf, size, b64, sizeof(b64)) != CHIAKI_ERR_SUCCESS)
		return false;
	fprintf(f, "\t\t<data>%s</data>\n", b64);
	return true;
}

static bool write_entry(FILE *f, const CKRegistryEntry *entry)
{
	const ChiakiRegisteredHost *host = &entry->host;
	char mac[13];
	ck_registry_mac_hex(host->server_mac, mac);
	// In key order, as Foundation writes them.
	fprintf(f, "\t<key>%s</key>\n\t<dict>\n", mac);
	fputs("\t\t<key>host</key>\n\t\t<string>", f);
	write_string(f, entry->last_host, strnlen(entry->last_host, sizeof(entry->last_host)));
	fputs("</string>\n\t\t<key>rp_key</key>\n", f);
	if(!write_data(f, host->rp_key, sizeof(host->rp_key)))
		return false;
	fprintf(f, "\t\t<key>rp_key_type</key>\n\t\t<integer>%u</integer>\n", (unsigned)host->rp_key_type);
	fputs("\t\t<key>rp_regist_key</key>\n", f);
	if(!write_data(f, (const uint8_t *)host->rp_regist_key, sizeof(host->rp_regist_key)))
		return false;
	fputs("\t\t<key>server_mac</key>\n", f);
	if(!write_data(f, host->server_mac, sizeof(host->server_mac)))
		return false;
	fputs("\t\t<key>server_nickname</key>\n\t\t<string>", f);
	write_string(f, host->server_nickname, strnlen(host->server_nickname, sizeof(host->server_nickname)));
	fprintf(f, "</string>\n\t\t<key>target</key>\n\t\t<integer>%d</integer>\n\t</dict>\n", (int)host->target);
	return true;
}

/** The owner of path's folder, for files root creates in a user's folder. */
static bool parent_owner(const char *path, struct stat *st)
{
	char dir[1024];
	size_t len = strlen(path);
	if(len >= sizeof(dir))
		return false;
	memcpy(dir, path, len + 1);
	char *slash = strrchr(dir, '/');
	if(!slash)
		return stat(".", st) == 0;
	if(slash == dir)
		slash[1] = '\0';
	else
		*slash = '\0';
	return stat(dir, st) == 0;
}

static void give_to_parent_owner(const char *path)
{
	struct stat st;
	if(geteuid() == 0 && parent_owner(path, &st))
	{
		int r = chown(path, st.st_uid, st.st_gid);
		(void)r;
	}
}

/** mkdir -p of path's folder. */
static ChiakiErrorCode make_parent_dirs(const char *path)
{
	char dir[1024];
	size_t len = strlen(path);
	if(len >= sizeof(dir))
		return CHIAKI_ERR_BUF_TOO_SMALL;
	memcpy(dir, path, len + 1);
	char *slash = strrchr(dir, '/');
	if(!slash || slash == dir)
		return CHIAKI_ERR_SUCCESS;
	*slash = '\0';
	for(char *p = dir + 1; ; p++)
	{
		if(*p != '/' && *p != '\0')
			continue;
		char saved = *p;
		*p = '\0';
		if(mkdir(dir, 0755) == 0)
			give_to_parent_owner(dir);
		else if(errno != EEXIST)
			return CHIAKI_ERR_UNKNOWN;
		*p = saved;
		if(!saved)
			break;
	}
	return CHIAKI_ERR_SUCCESS;
}

ChiakiErrorCode ck_registry_save(const CKRegistry *registry, const char *path)
{
	ChiakiErrorCode err = make_parent_dirs(path);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	char tmp[1024];
	if(snprintf(tmp, sizeof(tmp), "%s.tmp", path) >= (int)sizeof(tmp))
		return CHIAKI_ERR_BUF_TOO_SMALL;
	int fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC, 0600);
	if(fd < 0)
		return CHIAKI_ERR_UNKNOWN;
	FILE *f = fdopen(fd, "w");
	if(!f)
	{
		close(fd);
		unlink(tmp);
		return CHIAKI_ERR_UNKNOWN;
	}

	bool ok = true;
	fputs("<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
		"<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
		"<plist version=\"1.0\">\n", f);
	if(!registry->count)
		fputs("<dict/>\n", f);
	else
	{
		fputs("<dict>\n", f);
		for(size_t i = 0; ok && i < registry->count; i++)
			ok = write_entry(f, &registry->entries[i]);
		fputs("</dict>\n", f);
	}
	fputs("</plist>\n", f);
	ok = ok && fflush(f) == 0 && !ferror(f) && fsync(fileno(f)) == 0;
	ok = fclose(f) == 0 && ok;
	if(!ok)
	{
		unlink(tmp);
		return CHIAKI_ERR_UNKNOWN;
	}
	give_to_parent_owner(tmp);
	if(rename(tmp, path) != 0)
	{
		unlink(tmp);
		return CHIAKI_ERR_UNKNOWN;
	}
	return CHIAKI_ERR_SUCCESS;
}

// ---------------------------------------------------------------------------
// Reading
// ---------------------------------------------------------------------------

typedef struct parser_t
{
	const char *p;
	const char *end;
} Parser;

typedef struct tag_t
{
	char name[NAME_MAX_LEN];
	bool closing;
	bool empty;
} Tag;

static bool starts_with(const Parser *ps, const char *s)
{
	size_t len = strlen(s);
	return (size_t)(ps->end - ps->p) >= len && !memcmp(ps->p, s, len);
}

static bool skip_past(Parser *ps, const char *s)
{
	size_t len = strlen(s);
	for(; ps->p + len <= ps->end; ps->p++)
	{
		if(!memcmp(ps->p, s, len))
		{
			ps->p += len;
			return true;
		}
	}
	return false;
}

static void skip_space(Parser *ps)
{
	while(ps->p < ps->end && (*ps->p == ' ' || *ps->p == '\t' || *ps->p == '\n' || *ps->p == '\r'))
		ps->p++;
}

/** The next tag, past spaces, comments and declarations. */
static bool next_tag(Parser *ps, Tag *tag)
{
	while(true)
	{
		skip_space(ps);
		if(starts_with(ps, "<!--"))
		{
			if(!skip_past(ps, "-->"))
				return false;
			continue;
		}
		if(starts_with(ps, "<?") || starts_with(ps, "<!"))
		{
			if(!skip_past(ps, ">"))
				return false;
			continue;
		}
		break;
	}
	if(ps->p >= ps->end || *ps->p != '<')
		return false;
	ps->p++;
	tag->closing = ps->p < ps->end && *ps->p == '/';
	if(tag->closing)
		ps->p++;
	size_t len = 0;
	while(ps->p < ps->end && *ps->p != '>' && *ps->p != '/' && *ps->p != ' ' && *ps->p != '\t' && *ps->p != '\n'
		&& *ps->p != '\r')
	{
		if(len + 1 >= sizeof(tag->name))
			return false;
		tag->name[len++] = *ps->p++;
	}
	tag->name[len] = '\0';
	if(!len)
		return false;
	// Attributes, as in <plist version="1.0">, are skipped.
	const char *close = memchr(ps->p, '>', (size_t)(ps->end - ps->p));
	if(!close)
		return false;
	tag->empty = close > ps->p && close[-1] == '/';
	ps->p = close + 1;
	return true;
}

/** The text up to </name>, entities decoded, into out. */
static bool read_text(Parser *ps, const char *name, char *out, size_t out_size, size_t *out_len)
{
	char close[NAME_MAX_LEN + 3];
	snprintf(close, sizeof(close), "</%s>", name);
	size_t len = 0;
	while(true)
	{
		if(ps->p >= ps->end)
			return false;
		if(starts_with(ps, close))
		{
			ps->p += strlen(close);
			break;
		}
		char c = *ps->p++;
		if(c == '<')
			return false;
		if(c == '&')
		{
			const char *semi = memchr(ps->p, ';', (size_t)(ps->end - ps->p) < 10 ? (size_t)(ps->end - ps->p) : 10);
			if(!semi)
				return false;
			size_t elen = (size_t)(semi - ps->p);
			if(elen == 3 && !memcmp(ps->p, "amp", 3))
				c = '&';
			else if(elen == 2 && !memcmp(ps->p, "lt", 2))
				c = '<';
			else if(elen == 2 && !memcmp(ps->p, "gt", 2))
				c = '>';
			else if(elen == 4 && !memcmp(ps->p, "quot", 4))
				c = '"';
			else if(elen == 4 && !memcmp(ps->p, "apos", 4))
				c = '\'';
			else if(elen >= 2 && ps->p[0] == '#')
			{
				char num[12];
				memcpy(num, ps->p + 1, elen - 1);
				num[elen - 1] = '\0';
				char *end;
				unsigned long v = num[0] == 'x' ? strtoul(num + 1, &end, 16) : strtoul(num, &end, 10);
				if(*end)
					return false;
				// Only ASCII comes out as itself; the fields here never need more.
				c = v < 0x80 ? (char)v : '?';
			}
			else
				return false;
			ps->p = semi + 1;
		}
		if(len + 1 < out_size)
			out[len++] = c;
		else
			return false;
	}
	out[len] = '\0';
	*out_len = len;
	return true;
}

/** Skips the value that opened with tag, nested dicts and arrays included. */
static bool skip_value(Parser *ps, const Tag *tag)
{
	if(tag->empty)
		return true;
	if(strcmp(tag->name, "dict") && strcmp(tag->name, "array"))
	{
		char close[NAME_MAX_LEN + 3];
		snprintf(close, sizeof(close), "</%s>", tag->name);
		return skip_past(ps, close);
	}
	int depth = 1;
	while(depth)
	{
		Tag t;
		if(!next_tag(ps, &t))
			return false;
		if(t.empty)
			continue;
		if(t.closing)
		{
			if(!strcmp(t.name, "dict") || !strcmp(t.name, "array"))
				depth--;
			continue;
		}
		if(!strcmp(t.name, "dict") || !strcmp(t.name, "array"))
		{
			depth++;
			continue;
		}
		char close[NAME_MAX_LEN + 3];
		snprintf(close, sizeof(close), "</%s>", t.name);
		if(!skip_past(ps, close))
			return false;
	}
	return true;
}

static bool read_data(Parser *ps, uint8_t *out, size_t out_max, size_t *out_size)
{
	char text[TEXT_MAX];
	size_t len;
	if(!read_text(ps, "data", text, sizeof(text), &len))
		return false;
	// Foundation wraps data in lines indented with tabs, which the core's
	// decoder rejects; it also indexes a table with each char, so only
	// base64 gets there.
	size_t b64_len = 0;
	for(size_t i = 0; i < len; i++)
	{
		char c = text[i];
		if(c == ' ' || c == '\t' || c == '\n' || c == '\r')
			continue;
		if(!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' || c == '/'
			|| c == '='))
			return false;
		text[b64_len++] = c;
	}
	uint8_t buf[64];
	size_t size = sizeof(buf);
	if(chiaki_base64_decode(text, b64_len, buf, &size) != CHIAKI_ERR_SUCCESS || size > out_max)
		return false;
	memcpy(out, buf, size);
	*out_size = size;
	return true;
}

static bool read_integer(Parser *ps, long long *value)
{
	char text[32];
	size_t len;
	if(!read_text(ps, "integer", text, sizeof(text), &len))
		return false;
	char *end;
	errno = 0;
	*value = strtoll(text, &end, 10);
	return len && !*end && errno == 0;
}

/** One console's dictionary, after its <dict>. */
static bool read_entry(Parser *ps, CKRegistryEntry *entry)
{
	memset(entry, 0, sizeof(*entry));
	ChiakiRegisteredHost *host = &entry->host;
	bool mac_found = false;
	while(true)
	{
		Tag tag;
		if(!next_tag(ps, &tag))
			return false;
		if(tag.closing && !strcmp(tag.name, "dict"))
			break;
		if(tag.closing || strcmp(tag.name, "key") || tag.empty)
			return false;
		char key[64];
		size_t key_len;
		if(!read_text(ps, "key", key, sizeof(key), &key_len))
			return false;
		Tag value;
		if(!next_tag(ps, &value) || value.closing)
			return false;

		bool is_string = !strcmp(value.name, "string");
		bool is_data = !strcmp(value.name, "data");
		bool is_integer = !strcmp(value.name, "integer");
		char text[TEXT_MAX];
		size_t len = 0;
		size_t size = 0;
		long long number = 0;
		if(value.empty && (is_string || is_data))
		{
			// <string/> is an empty string, <data/> no bytes.
			if(!strcmp(key, "host"))
				entry->last_host[0] = '\0';
			continue;
		}
		if(!strcmp(key, "host") && is_string)
		{
			if(!read_text(ps, "string", text, sizeof(text), &len))
				return false;
			copy_utf8(entry->last_host, sizeof(entry->last_host), text, len);
		}
		else if(!strcmp(key, "server_nickname") && is_string)
		{
			if(!read_text(ps, "string", text, sizeof(text), &len))
				return false;
			copy_utf8(host->server_nickname, sizeof(host->server_nickname), text, len);
		}
		else if(!strcmp(key, "target") && is_integer)
		{
			if(!read_integer(ps, &number))
				return false;
			host->target = (ChiakiTarget)number;
		}
		else if(!strcmp(key, "rp_key_type") && is_integer)
		{
			if(!read_integer(ps, &number) || number < 0 || number > UINT32_MAX)
				return false;
			host->rp_key_type = (uint32_t)number;
		}
		else if(!strcmp(key, "server_mac") && is_data)
		{
			if(!read_data(ps, host->server_mac, sizeof(host->server_mac), &size) || size != sizeof(host->server_mac))
				return false;
			mac_found = true;
		}
		else if(!strcmp(key, "rp_key") && is_data)
		{
			if(!read_data(ps, host->rp_key, sizeof(host->rp_key), &size) || size != sizeof(host->rp_key))
				return false;
		}
		else if(!strcmp(key, "rp_regist_key") && is_data)
		{
			if(!read_data(ps, (uint8_t *)host->rp_regist_key, sizeof(host->rp_regist_key), &size))
				return false;
		}
		else if(!strcmp(key, "rp_regist_key") && is_string)
		{
			// Written by hand as text: the same bytes, NUL-padded.
			if(!read_text(ps, "string", text, sizeof(text), &len) || len > sizeof(host->rp_regist_key))
				return false;
			memcpy(host->rp_regist_key, text, len);
		}
		else if(!skip_value(ps, &value))
			return false;
	}
	return mac_found;
}

static ChiakiErrorCode parse_registry(CKRegistry *registry, const char *buf, size_t size)
{
	Parser ps = { buf, buf + size };
	Tag tag;
	if(!next_tag(&ps, &tag) || tag.closing || strcmp(tag.name, "plist") || tag.empty)
		return CHIAKI_ERR_INVALID_DATA;
	if(!next_tag(&ps, &tag) || tag.closing || strcmp(tag.name, "dict"))
		return CHIAKI_ERR_INVALID_DATA;
	if(!tag.empty)
	{
		while(true)
		{
			if(!next_tag(&ps, &tag))
				return CHIAKI_ERR_INVALID_DATA;
			if(tag.closing && !strcmp(tag.name, "dict"))
				break;
			char key[64];
			size_t key_len;
			if(tag.closing || tag.empty || strcmp(tag.name, "key") || !read_text(&ps, "key", key, sizeof(key), &key_len))
				return CHIAKI_ERR_INVALID_DATA;
			Tag value;
			if(!next_tag(&ps, &value) || value.closing)
				return CHIAKI_ERR_INVALID_DATA;
			if(strcmp(value.name, "dict") || value.empty)
			{
				// Not a console: something the app keeps next to them.
				if(!skip_value(&ps, &value))
					return CHIAKI_ERR_INVALID_DATA;
				continue;
			}
			if(registry->count >= CK_REGISTRY_MAX)
				return CHIAKI_ERR_OVERFLOW;
			if(!read_entry(&ps, &registry->entries[registry->count]))
				return CHIAKI_ERR_INVALID_DATA;
			registry->count++;
		}
	}
	if(!next_tag(&ps, &tag) || !tag.closing || strcmp(tag.name, "plist"))
		return CHIAKI_ERR_INVALID_DATA;
	return CHIAKI_ERR_SUCCESS;
}

ChiakiErrorCode ck_registry_load(CKRegistry *registry, const char *path)
{
	memset(registry, 0, sizeof(*registry));
	FILE *f = fopen(path, "rb");
	if(!f)
		return errno == ENOENT ? CHIAKI_ERR_SUCCESS : CHIAKI_ERR_UNKNOWN;
	char *buf = malloc(FILE_MAX + 1);
	if(!buf)
	{
		fclose(f);
		return CHIAKI_ERR_MEMORY;
	}
	size_t size = fread(buf, 1, FILE_MAX + 1, f);
	bool read_error = ferror(f) != 0;
	fclose(f);
	ChiakiErrorCode err;
	if(read_error)
		err = CHIAKI_ERR_UNKNOWN;
	else if(size > FILE_MAX)
		err = CHIAKI_ERR_INVALID_DATA;
	else
	{
		buf[size] = '\0';
		err = parse_registry(registry, buf, size);
	}
	free(buf);
	if(err != CHIAKI_ERR_SUCCESS)
		memset(registry, 0, sizeof(*registry));
	return err;
}
