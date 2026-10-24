// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A6.1, A6.2: the console list's rows (ck_console_rows.h).

#include "ck_console_rows.h"

#include <string.h>

/** Copies at most src_size bytes of src, which may lack its NUL, cut at a UTF-8 boundary to fit. */
static void copy_text(char *dst, size_t dst_size, const char *src, size_t src_size)
{
	size_t len = strnlen(src, src_size);
	if(len >= dst_size)
	{
		len = dst_size - 1;
		while(len && ((unsigned char)src[len] & 0xc0) == 0x80)
			len--;
	}
	memcpy(dst, src, len);
	dst[len] = '\0';
}

static bool hex_equal(const char *a, const char *b)
{
	for(;; a++, b++)
	{
		char ca = *a >= 'a' && *a <= 'f' ? (char)(*a - 'a' + 'A') : *a;
		char cb = *b >= 'a' && *b <= 'f' ? (char)(*b - 'a' + 'A') : *b;
		if(ca != cb)
			return false;
		if(!ca)
			return true;
	}
}

/** The registry entry of host_id, as an index, or -1. */
static int registry_index(const CKRegistry *registry, const char *host_id)
{
	if(!registry)
		return -1;
	for(size_t i = 0; i < registry->count && i < CK_REGISTRY_MAX; i++)
	{
		char hex[13];
		ck_registry_mac_hex(registry->entries[i].host.server_mac, hex);
		if(hex_equal(hex, host_id))
			return (int)i;
	}
	return -1;
}

static CKConsoleRowState state_of(ChiakiDiscoveryHostState state)
{
	switch(state)
	{
		case CHIAKI_DISCOVERY_HOST_STATE_READY:
			return CK_CONSOLE_ROW_READY;
		case CHIAKI_DISCOVERY_HOST_STATE_STANDBY:
			return CK_CONSOLE_ROW_STANDBY;
		default:
			return CK_CONSOLE_ROW_UNKNOWN;
	}
}

static CKConsoleAction action_of(const CKConsoleRow *row)
{
	if(!row->registered)
		// Registering needs the console on and showing its PIN; one in rest
		// mode cannot be woken without a registration.
		return row->state == CK_CONSOLE_ROW_READY ? CK_CONSOLE_ACTION_REGISTER : CK_CONSOLE_ACTION_NONE;
	switch(row->state)
	{
		case CK_CONSOLE_ROW_READY:
			return CK_CONSOLE_ACTION_PLAY;
		case CK_CONSOLE_ROW_STANDBY:
			return CK_CONSOLE_ACTION_WAKE;
		case CK_CONSOLE_ROW_MISSING:
			// The search may have been lost on the Wi-Fi: a wake-up to the last
			// known IP costs nothing and the list shows whether it worked.
			return row->addr[0] ? CK_CONSOLE_ACTION_WAKE : CK_CONSOLE_ACTION_NONE;
		default:
			return CK_CONSOLE_ACTION_NONE;
	}
}

static int compare_ascii_nocase(const char *a, const char *b)
{
	for(;; a++, b++)
	{
		unsigned char ca = (unsigned char)*a, cb = (unsigned char)*b;
		if(ca >= 'A' && ca <= 'Z')
			ca = (unsigned char)(ca - 'A' + 'a');
		if(cb >= 'A' && cb <= 'Z')
			cb = (unsigned char)(cb - 'A' + 'a');
		if(ca != cb || !ca)
			return (int)ca - (int)cb;
	}
}

/** Whether a goes before b. */
static bool row_before(const CKConsoleRow *a, const CKConsoleRow *b)
{
	if(a->registered != b->registered)
		return a->registered;
	int names = compare_ascii_nocase(a->name, b->name);
	if(names)
		return names < 0;
	return strcmp(a->host_id, b->host_id) < 0;
}

size_t ck_console_rows_build(const CKConsole *consoles, size_t count, const CKRegistry *registry,
	CKConsoleRow *rows, size_t rows_max)
{
	bool seen[CK_REGISTRY_MAX] = { false };
	size_t n = 0;

	for(size_t i = 0; i < count && n < rows_max; i++)
	{
		const CKConsole *console = &consoles[i];
		CKConsoleRow *row = &rows[n++];
		memset(row, 0, sizeof(*row));
		copy_text(row->name, sizeof(row->name), console->name, sizeof(console->name));
		copy_text(row->addr, sizeof(row->addr), console->addr, sizeof(console->addr));
		copy_text(row->host_id, sizeof(row->host_id), console->host_id, sizeof(console->host_id));
		for(char *c = row->host_id; *c; c++)
			if(*c >= 'a' && *c <= 'f')
				*c = (char)(*c - 'a' + 'A');
		copy_text(row->running_app_name, sizeof(row->running_app_name), console->running_app_name,
			sizeof(console->running_app_name));
		row->state = state_of(console->state);
		row->ps5 = console->ps5;
		row->target = console->target;
		int index = registry_index(registry, row->host_id);
		if(index >= 0)
		{
			seen[index] = true;
			row->registered = true;
			row->target = registry->entries[index].host.target;
			row->ps5 = chiaki_target_is_ps5(row->target);
			if(!row->name[0])
				copy_text(row->name, sizeof(row->name), registry->entries[index].host.server_nickname,
					sizeof(registry->entries[index].host.server_nickname));
		}
	}

	for(size_t i = 0; registry && i < registry->count && i < CK_REGISTRY_MAX && n < rows_max; i++)
	{
		if(seen[i])
			continue;
		const CKRegistryEntry *entry = &registry->entries[i];
		CKConsoleRow *row = &rows[n++];
		memset(row, 0, sizeof(*row));
		copy_text(row->name, sizeof(row->name), entry->host.server_nickname, sizeof(entry->host.server_nickname));
		copy_text(row->addr, sizeof(row->addr), entry->last_host, sizeof(entry->last_host));
		ck_registry_mac_hex(entry->host.server_mac, row->host_id);
		row->state = CK_CONSOLE_ROW_MISSING;
		row->registered = true;
		row->target = entry->host.target;
		row->ps5 = chiaki_target_is_ps5(row->target);
	}

	for(size_t i = 0; i < n; i++)
		rows[i].action = action_of(&rows[i]);

	// Insertion sort: a handful of rows, and stable.
	for(size_t i = 1; i < n; i++)
	{
		CKConsoleRow row = rows[i];
		size_t j = i;
		for(; j > 0 && row_before(&row, &rows[j - 1]); j--)
			rows[j] = rows[j - 1];
		rows[j] = row;
	}
	return n;
}

bool ck_console_rows_update_registry(const CKConsole *consoles, size_t count, CKRegistry *registry)
{
	bool changed = false;
	for(size_t i = 0; i < count; i++)
	{
		const CKConsole *console = &consoles[i];
		if(!console->addr[0])
			continue;
		int index = registry_index(registry, console->host_id);
		if(index < 0)
			continue;
		char *last = registry->entries[index].last_host;
		size_t last_size = sizeof(registry->entries[index].last_host);
		if(strncmp(last, console->addr, last_size))
		{
			copy_text(last, last_size, console->addr, sizeof(console->addr));
			changed = true;
		}
	}
	return changed;
}

const char *ck_console_row_state_string(CKConsoleRowState state)
{
	switch(state)
	{
		case CK_CONSOLE_ROW_READY:
			return "encendida";
		case CK_CONSOLE_ROW_STANDBY:
			return "en reposo";
		case CK_CONSOLE_ROW_MISSING:
			return "no contesta";
		default:
			return "estado desconocido";
	}
}

const char *ck_console_action_string(CKConsoleAction action)
{
	switch(action)
	{
		case CK_CONSOLE_ACTION_REGISTER:
			return "Registrar";
		case CK_CONSOLE_ACTION_WAKE:
			return "Despertar";
		case CK_CONSOLE_ACTION_PLAY:
			return "Jugar";
		default:
			return "";
	}
}
