// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// ckctl (A2.4, A2.5): the command line face of Bridge/CKConsoles.h, with the
// registrations saved through Bridge/ck_registry.h. See README.md.

#include "ckctl.h"

#include "Bridge/CKConsoles.h"
#include "Bridge/ck_registry.h"

#include <chiaki/time.h>

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define DISCOVER_TIMEOUT_S 2
/** How long regist and wakeup wait for the console to answer a search first. */
#define PROBE_TIMEOUT_MS 1000
#define WAKE_POLL_MS 1000
#define WAKE_RESEND_MS 5000
/** server_nickname and its NUL: it is NUL-padded, not always terminated. */
#define NAME_SIZE (sizeof(((ChiakiRegisteredHost *)NULL)->server_nickname) + 1)

static const char usage[] =
	"Uso:\n"
	"  ckctl [-v] discover [-H IP] [-t SEGUNDOS]\n"
	"      Busca consolas en la red (o solo la de IP) durante SEGUNDOS (2) y marca\n"
	"      las registradas.\n"
	"  ckctl [-v] regist -H IP -a ACCOUNT_ID -p PIN\n"
	"      Registra el iPad con la consola y guarda el registro. ACCOUNT_ID va en\n"
	"      base64 o en decimal; el PIN es el de 8 cifras que muestra la consola en\n"
	"      Ajustes > Sistema > Remote Play > Vincular dispositivo.\n"
	"  ckctl [-v] wakeup [-c CONSOLA] [-H IP] [-b DIRECCIÓN] [-w SEGUNDOS]\n"
	"      Despierta a una consola registrada que está en reposo. CONSOLA es su\n"
	"      nombre, su id o su IP; sin -c, la única registrada. La busca en IP, o en\n"
	"      su última IP, y si no está ahí, en toda la red (o en DIRECCIÓN). Con -w\n"
	"      espera hasta SEGUNDOS a que se encienda.\n"
	"  ckctl [-v] wakeup -H IP -k LLAVE [-w SEGUNDOS] [--ps4]\n"
	"      Lo mismo sin el registro guardado, con su llave (rp_regist_key).\n"
	"  ckctl list\n"
	"      Muestra las consolas registradas.\n"
	"  ckctl forget -c CONSOLA\n"
	"      Borra el registro de una consola.\n"
	"\n"
	"  -s, --store RUTA  el archivo de los registros (" CK_REGISTRY_DEFAULT_PATH ")\n"
	"  -v, --verbose     muestra el registro del core en la salida de errores\n";

typedef struct ckctl_t
{
	FILE *out;
	FILE *err;
	ChiakiLog log;
	const char *host;
	const char *account;
	const char *pin;
	const char *key;
	const char *console;
	const char *store;
	const char *search;
	long seconds;
	bool seconds_set;
	bool ps4;
} Ckctl;

static void log_cb(ChiakiLogLevel level, const char *msg, void *user)
{
	FILE *err = user;
	fprintf(err, "[%c] %s\n", chiaki_log_level_char(level), msg);
}

static int usage_error(Ckctl *ctl, const char *what)
{
	fprintf(ctl->err, "ckctl: %s\n\n%s", what, usage);
	return 2;
}

/** Reads the options after the command into ctl. Returns 0, or the usage error's exit code. */
static int parse_options(Ckctl *ctl, int argc, char **argv)
{
	for(int i = 0; i < argc; i++)
	{
		const char *arg = argv[i];
		if(!strcmp(arg, "--ps4"))
		{
			ctl->ps4 = true;
			continue;
		}
		// Either a text option (value) or a number of seconds.
		const char **value = NULL;
		if(!strcmp(arg, "-H") || !strcmp(arg, "--host"))
			value = &ctl->host;
		else if(!strcmp(arg, "-a") || !strcmp(arg, "--account"))
			value = &ctl->account;
		else if(!strcmp(arg, "-p") || !strcmp(arg, "--pin"))
			value = &ctl->pin;
		else if(!strcmp(arg, "-k") || !strcmp(arg, "--key"))
			value = &ctl->key;
		else if(!strcmp(arg, "-c") || !strcmp(arg, "--console"))
			value = &ctl->console;
		else if(!strcmp(arg, "-s") || !strcmp(arg, "--store"))
			value = &ctl->store;
		else if(!strcmp(arg, "-b") || !strcmp(arg, "--broadcast"))
			value = &ctl->search;
		else if(strcmp(arg, "-t") && strcmp(arg, "--timeout") && strcmp(arg, "-w") && strcmp(arg, "--wait"))
		{
			fprintf(ctl->err, "ckctl: opción desconocida: %s\n\n%s", arg, usage);
			return 2;
		}
		if(i + 1 >= argc)
		{
			fprintf(ctl->err, "ckctl: a %s le falta su valor\n\n%s", arg, usage);
			return 2;
		}
		const char *v = argv[++i];
		if(value)
		{
			*value = v;
			continue;
		}
		char *end;
		long seconds = strtol(v, &end, 10);
		if(!*v || *end || seconds < 0 || seconds > 3600)
		{
			fprintf(ctl->err, "ckctl: %s pide segundos entre 0 y 3600, no \"%s\"\n", arg, v);
			return 2;
		}
		ctl->seconds = seconds;
		ctl->seconds_set = true;
	}
	return 0;
}

static void print_console(FILE *out, const CKConsole *console, bool registered)
{
	fprintf(out, "%-15s  %s  %-11s  %s  id %s  fw %s", console->addr, console->ps5 ? "PS5" : "PS4",
		ck_console_state_string(console->state), console->name[0] ? console->name : "(sin nombre)",
		console->host_id[0] ? console->host_id : "?", console->system_version[0] ? console->system_version : "?");
	if(registered)
		fputs("  registrada", out);
	if(console->running_app_name[0])
		fprintf(out, "  jugando %s", console->running_app_name);
	fputc('\n', out);
}

/** The console at host, if it answers a search within PROBE_TIMEOUT_MS. */
static bool probe(Ckctl *ctl, const char *host, CKConsole *console)
{
	size_t count = 0;
	ChiakiErrorCode err = ck_consoles_discover(&ctl->log, host, PROBE_TIMEOUT_MS, console, 1, &count);
	return err == CHIAKI_ERR_SUCCESS && count > 0;
}

// ---------------------------------------------------------------------------
// The saved registrations
// ---------------------------------------------------------------------------

static const char *store_path(const Ckctl *ctl)
{
	return ctl->store ? ctl->store : CK_REGISTRY_DEFAULT_PATH;
}

/** Why the store could not be read, after "no pude leer los registros de PATH: ". */
static const char *load_error_string(ChiakiErrorCode err)
{
	switch(err)
	{
		case CHIAKI_ERR_INVALID_DATA:
			return "no es un archivo de registros o está dañado, y no lo toco";
		case CHIAKI_ERR_OVERFLOW:
			return "tiene más consolas de las que caben, y no lo toco";
		default:
			return "no se puede abrir";
	}
}

/** Loads the store into registry, or says why not and returns false. */
static bool load_store(Ckctl *ctl, CKRegistry *registry)
{
	ChiakiErrorCode err = ck_registry_load(registry, store_path(ctl));
	if(err == CHIAKI_ERR_SUCCESS)
		return true;
	fprintf(ctl->err, "ckctl: no pude leer los registros de %s: %s\n", store_path(ctl), load_error_string(err));
	return false;
}

static bool save_store(Ckctl *ctl, const CKRegistry *registry)
{
	if(ck_registry_save(registry, store_path(ctl)) == CHIAKI_ERR_SUCCESS)
		return true;
	fprintf(ctl->err, "ckctl: no pude guardar los registros en %s\n", store_path(ctl));
	return false;
}

/** The nickname as a C string, in name. */
static const char *entry_name(const CKRegistryEntry *entry, char name[NAME_SIZE])
{
	size_t len = strnlen(entry->host.server_nickname, sizeof(entry->host.server_nickname));
	memcpy(name, entry->host.server_nickname, len);
	name[len] = '\0';
	return len ? name : "(sin nombre)";
}

/** Remembers that the console answered at addr. Returns whether that changed the entry. */
static bool remember_addr(CKRegistryEntry *entry, const char *addr)
{
	if(!strcmp(entry->last_host, addr))
		return false;
	snprintf(entry->last_host, sizeof(entry->last_host), "%s", addr);
	return true;
}

static int cmd_discover(Ckctl *ctl)
{
	long seconds = ctl->seconds_set ? ctl->seconds : DISCOVER_TIMEOUT_S;
	CKConsole consoles[CK_CONSOLES_MAX];
	size_t count = 0;
	ChiakiErrorCode err = ck_consoles_discover(&ctl->log, ctl->host, (uint32_t)seconds * 1000,
		consoles, CK_CONSOLES_MAX, &count);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		fprintf(ctl->err, "ckctl: no se pudo buscar consolas: %s\n", chiaki_error_string(err));
		return 1;
	}
	if(!count)
	{
		fprintf(ctl->err, "ckctl: ninguna consola respondió en %ld s\n", seconds);
		return 1;
	}

	// The registered consoles are marked, and their address kept for wakeup.
	CKRegistry registry;
	ChiakiErrorCode load_err = ck_registry_load(&registry, store_path(ctl));
	if(load_err != CHIAKI_ERR_SUCCESS)
		fprintf(ctl->err, "ckctl: aviso: no pude leer los registros de %s: %s\n", store_path(ctl),
			load_error_string(load_err));
	bool changed = false;
	for(size_t i = 0; i < count; i++)
	{
		CKRegistryEntry *entry = ck_registry_find_host_id(&registry, consoles[i].host_id);
		print_console(ctl->out, &consoles[i], entry != NULL);
		if(entry && remember_addr(entry, consoles[i].addr))
			changed = true;
	}
	if(changed)
		save_store(ctl, &registry);
	return 0;
}

static void print_hex(FILE *out, const uint8_t *buf, size_t size)
{
	for(size_t i = 0; i < size; i++)
		fprintf(out, "%02x", buf[i]);
}

static int cmd_regist(Ckctl *ctl)
{
	if(!ctl->host || !ctl->account || !ctl->pin)
		return usage_error(ctl, "regist pide -H, -a y -p");
	CKRegistRequest request;
	memset(&request, 0, sizeof(request));
	request.host = ctl->host;
	if(ck_parse_psn_account_id(ctl->account, request.psn_account_id) != CHIAKI_ERR_SUCCESS)
	{
		fprintf(ctl->err, "ckctl: el Account ID no es válido: va en base64 (12 caracteres que terminan en =) o en decimal\n");
		return 2;
	}
	if(ck_parse_regist_pin(ctl->pin, &request.pin) != CHIAKI_ERR_SUCCESS)
	{
		fprintf(ctl->err, "ckctl: el PIN debe tener 8 cifras\n");
		return 2;
	}

	CKConsole console;
	if(probe(ctl, ctl->host, &console))
	{
		request.target = console.target;
		fprintf(ctl->out, "Registrando con %s (%s, %s)...\n", console.name, console.ps5 ? "PS5" : "PS4",
			ck_console_state_string(console.state));
	}
	else
	{
		request.target = CHIAKI_TARGET_PS5_1;
		fprintf(ctl->out, "%s no contestó la búsqueda; intento el registro como PS5...\n", ctl->host);
	}

	CKRegistOutcome outcome;
	CKRegistResult result = ck_regist_run(&ctl->log, &request, &outcome);
	if(result != CK_REGIST_SUCCESS)
	{
		fprintf(ctl->err, "ckctl: %s", ck_regist_result_string(result));
		if(outcome.http_code)
			fprintf(ctl->err, " (HTTP %d", outcome.http_code);
		if(outcome.reason)
			fprintf(ctl->err, "%s0x%08x", outcome.http_code ? ", razón " : " (razón ", (unsigned)outcome.reason);
		if(outcome.http_code || outcome.reason)
			fputc(')', ctl->err);
		fputc('\n', ctl->err);
		return 1;
	}

	const ChiakiRegisteredHost *host = &outcome.registered_host;
	fprintf(ctl->out, "%s\n", ck_regist_result_string(result));
	char id[13];
	ck_registry_mac_hex(host->server_mac, id);
	CKRegistry registry;
	if(load_store(ctl, &registry))
	{
		bool replaced = ck_registry_find_mac(&registry, host->server_mac) != NULL;
		if(ck_registry_put(&registry, host, ctl->host) != CHIAKI_ERR_SUCCESS)
			fprintf(ctl->err, "ckctl: ya hay %d consolas registradas; borra una con ckctl forget\n", CK_REGISTRY_MAX);
		else if(save_store(ctl, &registry))
		{
			CKRegistryEntry *entry = ck_registry_find_mac(&registry, host->server_mac);
			char name[NAME_SIZE];
			fprintf(ctl->out, "%s el registro de %s (id %s) en %s.\n", replaced ? "Reemplacé" : "Guardé",
				entry_name(entry, name), id, store_path(ctl));
			fprintf(ctl->out, "Para despertarla: ckctl wakeup -c %s\n", id);
			return 0;
		}
	}

	// Not saved: the keys go to the terminal instead, or the registration is lost.
	fprintf(ctl->out, "No quedó guardado. Guarda estos datos fuera del repo; ckctl wakeup -k usa rp_regist_key:\n");
	fprintf(ctl->out, "  target=%d\n", (int)host->target);
	fprintf(ctl->out, "  server_nickname=%.*s\n", (int)strnlen(host->server_nickname, sizeof(host->server_nickname)),
		host->server_nickname);
	fprintf(ctl->out, "  server_mac=");
	print_hex(ctl->out, host->server_mac, sizeof(host->server_mac));
	fprintf(ctl->out, "\n  rp_regist_key=%.*s\n", (int)strnlen(host->rp_regist_key, sizeof(host->rp_regist_key)),
		host->rp_regist_key);
	fprintf(ctl->out, "  rp_key_type=%u\n", (unsigned)host->rp_key_type);
	fprintf(ctl->out, "  rp_key=");
	print_hex(ctl->out, host->rp_key, sizeof(host->rp_key));
	fputc('\n', ctl->out);
	return 1;
}

/** Sends the wake-up to host and, with -w, waits for the console to turn on. */
static int wake(Ckctl *ctl, const char *host, const ChiakiRegisteredHost *registered)
{
	ChiakiErrorCode err = ck_console_wakeup(&ctl->log, host, registered);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		fprintf(ctl->err, "ckctl: no se pudo mandar la señal para despertar: %s\n", chiaki_error_string(err));
		return 1;
	}
	fprintf(ctl->out, "Mandé la señal para despertar a %s.\n", host);
	if(!ctl->seconds_set || !ctl->seconds)
		return 0;

	CKConsole console;
	uint64_t start = chiaki_time_now_monotonic_ms();
	uint64_t last_wake = start;
	uint64_t limit = (uint64_t)ctl->seconds * 1000;
	while(true)
	{
		uint64_t poll = chiaki_time_now_monotonic_ms();
		if(probe(ctl, host, &console) && console.state == CHIAKI_DISCOVERY_HOST_STATE_READY)
		{
			fprintf(ctl->out, "%s se encendió en %llu s.\n", console.name,
				(unsigned long long)((chiaki_time_now_monotonic_ms() - start + 999) / 1000));
			return 0;
		}
		uint64_t now = chiaki_time_now_monotonic_ms();
		if(now - start >= limit)
			break;
		// The console may have missed the packet: it is UDP.
		if(now - last_wake >= WAKE_RESEND_MS)
		{
			ck_console_wakeup(&ctl->log, host, registered);
			last_wake = now;
		}
		if(now - poll < WAKE_POLL_MS)
			chiaki_sleep_ms(WAKE_POLL_MS - (now - poll));
	}
	fprintf(ctl->err, "ckctl: %s no se encendió en %ld s\n", host, ctl->seconds);
	return 1;
}

/** wakeup -H -k: with a key given by hand, as before the store existed. */
static int wakeup_with_key(Ckctl *ctl)
{
	if(!ctl->host)
		return usage_error(ctl, "wakeup con -k pide -H");
	ChiakiRegisteredHost registered;
	memset(&registered, 0, sizeof(registered));
	registered.target = ctl->ps4 ? CHIAKI_TARGET_PS4_10 : CHIAKI_TARGET_PS5_1;
	size_t key_len = strlen(ctl->key);
	uint64_t credential;
	if(key_len <= sizeof(registered.rp_regist_key))
		memcpy(registered.rp_regist_key, ctl->key, key_len);
	if(key_len > sizeof(registered.rp_regist_key)
		|| ck_regist_key_credential(registered.rp_regist_key, &credential) != CHIAKI_ERR_SUCCESS)
	{
		fprintf(ctl->err, "ckctl: la llave de registro son hasta %d cifras hexadecimales, como la rp_regist_key de ckctl regist\n",
			(int)sizeof(registered.rp_regist_key));
		return 2;
	}

	CKConsole console;
	if(probe(ctl, ctl->host, &console) && console.state == CHIAKI_DISCOVERY_HOST_STATE_READY)
	{
		fprintf(ctl->out, "%s ya está encendida.\n", console.name);
		return 0;
	}
	return wake(ctl, ctl->host, &registered);
}

/** The registered console that -c, or else -H, names; NULL after saying why. */
static CKRegistryEntry *pick_entry(Ckctl *ctl, CKRegistry *registry)
{
	if(!registry->count)
	{
		fprintf(ctl->err, "ckctl: no hay consolas registradas en %s; regístrala con ckctl regist\n", store_path(ctl));
		return NULL;
	}
	if(ctl->console)
	{
		CKRegistryEntry *entry = ck_registry_find(registry, ctl->console);
		if(!entry)
			fprintf(ctl->err, "ckctl: ninguna consola registrada se llama, tiene el id o la IP \"%s\" (ckctl list las muestra)\n",
				ctl->console);
		return entry;
	}
	if(registry->count == 1)
		return &registry->entries[0];
	if(ctl->host)
	{
		CKRegistryEntry *entry = ck_registry_find(registry, ctl->host);
		CKConsole console;
		if(!entry && probe(ctl, ctl->host, &console))
			entry = ck_registry_find_host_id(registry, console.host_id);
		if(entry)
			return entry;
	}
	fprintf(ctl->err, "ckctl: hay %zu consolas registradas: elige una con -c (ckctl list las muestra)\n", registry->count);
	return NULL;
}

/** Whether the console that answered is the registered one, not another at its old IP. */
static bool is_entry(const CKRegistryEntry *entry, const CKConsole *console)
{
	char id[13];
	ck_registry_mac_hex(entry->host.server_mac, id);
	return !strcmp(id, console->host_id);
}

/** Searches the network (or -b) for the registered console, which may have a new IP. */
static bool search_entry(Ckctl *ctl, const CKRegistryEntry *entry, CKConsole *found)
{
	CKConsole consoles[CK_CONSOLES_MAX];
	size_t count = 0;
	if(ck_consoles_discover(&ctl->log, ctl->search, DISCOVER_TIMEOUT_S * 1000, consoles, CK_CONSOLES_MAX, &count)
		!= CHIAKI_ERR_SUCCESS)
		return false;
	for(size_t i = 0; i < count; i++)
	{
		if(is_entry(entry, &consoles[i]))
		{
			*found = consoles[i];
			return true;
		}
	}
	return false;
}

static int cmd_wakeup(Ckctl *ctl)
{
	if(ctl->key)
		return wakeup_with_key(ctl);
	CKRegistry registry;
	if(!load_store(ctl, &registry))
		return 1;
	CKRegistryEntry *entry = pick_entry(ctl, &registry);
	if(!entry)
		return 1;
	char name[NAME_SIZE];
	entry_name(entry, name);

	// Where it was last, then anywhere: the router may have given it a new IP.
	const char *host = ctl->host ? ctl->host : (entry->last_host[0] ? entry->last_host : NULL);
	CKConsole console;
	bool found = host && probe(ctl, host, &console) && is_entry(entry, &console);
	if(!found)
		found = search_entry(ctl, entry, &console);
	char addr[sizeof(console.addr)];
	if(found)
	{
		snprintf(addr, sizeof(addr), "%s", console.addr);
		if(remember_addr(entry, addr))
			save_store(ctl, &registry);
		if(console.state == CHIAKI_DISCOVERY_HOST_STATE_READY)
		{
			fprintf(ctl->out, "%s ya está encendida.\n", name);
			return 0;
		}
	}
	else if(host)
	{
		// In rest mode with its network off it does not answer, but it may still wake.
		snprintf(addr, sizeof(addr), "%s", host);
		fprintf(ctl->out, "%s no contestó la búsqueda; mando la señal a %s de todos modos.\n", name, addr);
	}
	else
	{
		fprintf(ctl->err, "ckctl: no encuentro a %s en la red y no sé su IP; pásala con -H\n", name);
		return 1;
	}
	return wake(ctl, addr, &entry->host);
}

static int cmd_list(Ckctl *ctl)
{
	CKRegistry registry;
	if(!load_store(ctl, &registry))
		return 1;
	if(!registry.count)
	{
		fprintf(ctl->out, "No hay consolas registradas en %s.\n", store_path(ctl));
		return 0;
	}
	for(size_t i = 0; i < registry.count; i++)
	{
		const CKRegistryEntry *entry = &registry.entries[i];
		char id[13];
		ck_registry_mac_hex(entry->host.server_mac, id);
		char name[NAME_SIZE];
		fprintf(ctl->out, "%-15s  %s  %s  id %s\n", entry->last_host[0] ? entry->last_host : "(sin IP)",
			chiaki_target_is_ps5(entry->host.target) ? "PS5" : "PS4", entry_name(entry, name), id);
	}
	return 0;
}

static int cmd_forget(Ckctl *ctl)
{
	if(!ctl->console)
		return usage_error(ctl, "forget pide -c");
	CKRegistry registry;
	if(!load_store(ctl, &registry))
		return 1;
	CKRegistryEntry *entry = ck_registry_find(&registry, ctl->console);
	if(!entry)
	{
		fprintf(ctl->err, "ckctl: ninguna consola registrada se llama, tiene el id o la IP \"%s\"\n", ctl->console);
		return 1;
	}
	char id[13];
	ck_registry_mac_hex(entry->host.server_mac, id);
	char name[NAME_SIZE];
	entry_name(entry, name);
	ck_registry_remove(&registry, entry);
	if(!save_store(ctl, &registry))
		return 1;
	fprintf(ctl->out, "Borré el registro de %s (id %s). Para volver a usarla, regístrala con ckctl regist.\n", name, id);
	return 0;
}

int ckctl_main(int argc, char **argv, FILE *out, FILE *err)
{
	Ckctl ctl;
	memset(&ctl, 0, sizeof(ctl));
	ctl.out = out;
	ctl.err = err;
	bool verbose = false;

	int i = 1;
	for(; i < argc && argv[i][0] == '-'; i++)
	{
		if(!strcmp(argv[i], "-v") || !strcmp(argv[i], "--verbose"))
			verbose = true;
		else if(!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help"))
		{
			fputs(usage, out);
			return 0;
		}
		else
			return usage_error(&ctl, "opción desconocida antes del comando");
	}
	if(i >= argc)
		return usage_error(&ctl, "falta el comando");
	const char *command = argv[i];
	if(!strcmp(command, "help"))
	{
		fputs(usage, out);
		return 0;
	}

	int r = parse_options(&ctl, argc - i - 1, argv + i + 1);
	if(r)
		return r;
	chiaki_log_init(&ctl.log, verbose ? CHIAKI_LOG_ALL : 0, log_cb, err);

	if(!strcmp(command, "discover"))
		return cmd_discover(&ctl);
	if(!strcmp(command, "regist"))
		return cmd_regist(&ctl);
	if(!strcmp(command, "wakeup"))
		return cmd_wakeup(&ctl);
	if(!strcmp(command, "list"))
		return cmd_list(&ctl);
	if(!strcmp(command, "forget"))
		return cmd_forget(&ctl);
	fprintf(err, "ckctl: comando desconocido: %s\n\n%s", command, usage);
	return 2;
}
