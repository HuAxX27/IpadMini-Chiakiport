// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// ckctl (A2.4): the command line face of Bridge/CKConsoles.h. See README.md.

#include "ckctl.h"

#include "Bridge/CKConsoles.h"

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

static const char usage[] =
	"Uso:\n"
	"  ckctl [-v] discover [-H IP] [-t SEGUNDOS]\n"
	"      Busca consolas en la red (o solo la de IP) durante SEGUNDOS (2).\n"
	"  ckctl [-v] regist -H IP -a ACCOUNT_ID -p PIN\n"
	"      Registra el iPad con la consola. ACCOUNT_ID va en base64 o en decimal;\n"
	"      el PIN es el de 8 cifras que muestra la consola en Ajustes > Sistema >\n"
	"      Remote Play > Vincular dispositivo.\n"
	"  ckctl [-v] wakeup -H IP -k LLAVE [-w SEGUNDOS] [--ps4]\n"
	"      Despierta a la consola en reposo con la llave de registro (rp_regist_key\n"
	"      de ckctl regist). Con -w espera hasta SEGUNDOS a que se encienda.\n"
	"\n"
	"  -v, --verbose   muestra el registro del core en la salida de errores\n";

typedef struct ckctl_t
{
	FILE *out;
	FILE *err;
	ChiakiLog log;
	const char *host;
	const char *account;
	const char *pin;
	const char *key;
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

static void print_console(FILE *out, const CKConsole *console)
{
	fprintf(out, "%-15s  %s  %-11s  %s  id %s  fw %s", console->addr, console->ps5 ? "PS5" : "PS4",
		ck_console_state_string(console->state), console->name[0] ? console->name : "(sin nombre)",
		console->host_id[0] ? console->host_id : "?", console->system_version[0] ? console->system_version : "?");
	if(console->running_app_name[0])
		fprintf(out, "  jugando %s", console->running_app_name);
	fputc('\n', out);
}

/** The console at ctl->host, if it answers a search within PROBE_TIMEOUT_MS. */
static bool probe(Ckctl *ctl, CKConsole *console)
{
	size_t count = 0;
	ChiakiErrorCode err = ck_consoles_discover(&ctl->log, ctl->host, PROBE_TIMEOUT_MS, console, 1, &count);
	return err == CHIAKI_ERR_SUCCESS && count > 0;
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
	for(size_t i = 0; i < count; i++)
		print_console(ctl->out, &consoles[i]);
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
	if(probe(ctl, &console))
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
	fprintf(ctl->out, "Guarda estos datos fuera del repo; ckctl wakeup usa rp_regist_key:\n");
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
	return 0;
}

static int cmd_wakeup(Ckctl *ctl)
{
	if(!ctl->host || !ctl->key)
		return usage_error(ctl, "wakeup pide -H y -k");
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
	if(probe(ctl, &console) && console.state == CHIAKI_DISCOVERY_HOST_STATE_READY)
	{
		fprintf(ctl->out, "%s ya está encendida.\n", console.name);
		return 0;
	}

	ChiakiErrorCode err = ck_console_wakeup(&ctl->log, ctl->host, &registered);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		fprintf(ctl->err, "ckctl: no se pudo mandar la señal para despertar: %s\n", chiaki_error_string(err));
		return 1;
	}
	fprintf(ctl->out, "Mandé la señal para despertar a %s.\n", ctl->host);
	if(!ctl->seconds_set || !ctl->seconds)
		return 0;

	uint64_t start = chiaki_time_now_monotonic_ms();
	uint64_t last_wake = start;
	uint64_t limit = (uint64_t)ctl->seconds * 1000;
	while(true)
	{
		uint64_t poll = chiaki_time_now_monotonic_ms();
		if(probe(ctl, &console) && console.state == CHIAKI_DISCOVERY_HOST_STATE_READY)
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
			ck_console_wakeup(&ctl->log, ctl->host, &registered);
			last_wake = now;
		}
		if(now - poll < WAKE_POLL_MS)
			chiaki_sleep_ms(WAKE_POLL_MS - (now - poll));
	}
	fprintf(ctl->err, "ckctl: %s no se encendió en %ld s\n", ctl->host, ctl->seconds);
	return 1;
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
	fprintf(err, "ckctl: comando desconocido: %s\n\n%s", command, usage);
	return 2;
}
