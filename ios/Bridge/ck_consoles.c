// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// A2.4: discovery, registration and wake-up of consoles (CKConsoles.h) on
// top of the core's discovery and regist. Plain C and POSIX sockets, the same
// on the iPad and on Linux, where ios/tests/core/consoles.c runs it against a
// fake PS5.

#include "CKConsoles.h"

#include <chiaki/base64.h>
#include <chiaki/session.h>
#include <chiaki/thread.h>
#include <chiaki/time.h>

#include <arpa/inet.h>
#include <errno.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

/** Addresses one search goes to: one per interface when broadcasting. */
#define TARGETS_MAX 8
#define DISCOVER_PING_MS 500
#define BROWSER_PING_MS 1000
#define BROWSER_DROP_PINGS 3

static void copy_string(char *dst, size_t dst_size, const char *src)
{
	if(!src)
		src = "";
	size_t len = strlen(src);
	if(len >= dst_size)
		len = dst_size - 1;
	memcpy(dst, src, len);
	dst[len] = '\0';
}

const char *ck_console_state_string(ChiakiDiscoveryHostState state)
{
	switch(state)
	{
		case CHIAKI_DISCOVERY_HOST_STATE_READY:
			return "encendida";
		case CHIAKI_DISCOVERY_HOST_STATE_STANDBY:
			return "en reposo";
		default:
			return "desconocido";
	}
}

static void console_from_host(CKConsole *console, ChiakiDiscoveryHost *host)
{
	memset(console, 0, sizeof(*console));
	copy_string(console->addr, sizeof(console->addr), host->host_addr);
	copy_string(console->name, sizeof(console->name), host->host_name);
	copy_string(console->host_id, sizeof(console->host_id), host->host_id);
	// Uppercase, so it compares equal to the registry's key (C5).
	for(char *c = console->host_id; *c; c++)
		if(*c >= 'a' && *c <= 'f')
			*c = (char)(*c - 'a' + 'A');
	copy_string(console->system_version, sizeof(console->system_version), host->system_version);
	copy_string(console->running_app_titleid, sizeof(console->running_app_titleid), host->running_app_titleid);
	copy_string(console->running_app_name, sizeof(console->running_app_name), host->running_app_name);
	console->state = host->state;
	console->ps5 = chiaki_discovery_host_is_ps5(host);
	// chiaki_discovery_host_system_version_target() reads system_version unchecked.
	if(host->system_version)
		console->target = chiaki_discovery_host_system_version_target(host);
	else
		console->target = console->ps5 ? CHIAKI_TARGET_PS5_1 : CHIAKI_TARGET_PS4_UNKNOWN;
}

static bool console_equal(const CKConsole *a, const CKConsole *b)
{
	return !strcmp(a->addr, b->addr) && !strcmp(a->name, b->name) && !strcmp(a->host_id, b->host_id)
		&& !strcmp(a->system_version, b->system_version)
		&& !strcmp(a->running_app_titleid, b->running_app_titleid)
		&& !strcmp(a->running_app_name, b->running_app_name)
		&& a->state == b->state && a->ps5 == b->ps5 && a->target == b->target;
}

/** The same console: by host-id, or by address for a console that sent none. */
static bool console_same(const CKConsole *a, const CKConsole *b)
{
	if(a->host_id[0] || b->host_id[0])
		return !strcmp(a->host_id, b->host_id);
	return !strcmp(a->addr, b->addr);
}

// ---------------------------------------------------------------------------
// Where searches go
// ---------------------------------------------------------------------------

static void target_add(struct sockaddr_in *targets, size_t *count, const struct sockaddr_in *addr)
{
	for(size_t i = 0; i < *count; i++)
		if(targets[i].sin_addr.s_addr == addr->sin_addr.s_addr)
			return;
	if(*count >= TARGETS_MAX)
		return;
	memset(&targets[*count], 0, sizeof(targets[*count]));
#ifdef __APPLE__
	targets[*count].sin_len = sizeof(targets[*count]);
#endif
	targets[*count].sin_family = AF_INET;
	targets[*count].sin_addr = addr->sin_addr;
	(*count)++;
}

/**
 * host's IPv4 address, or the broadcast address of every IPv4 interface that
 * has one. 255.255.255.255 alone goes out of a single interface on BSD
 * systems like iOS, so it is only the fallback.
 */
static ChiakiErrorCode targets_resolve(ChiakiLog *log, const char *host, struct sockaddr_in *targets, size_t *count)
{
	*count = 0;
	if(host)
	{
		struct addrinfo hints;
		memset(&hints, 0, sizeof(hints));
		hints.ai_family = AF_INET;
		hints.ai_socktype = SOCK_DGRAM;
		struct addrinfo *ai = NULL;
		if(getaddrinfo(host, NULL, &hints, &ai) != 0 || !ai)
		{
			CHIAKI_LOGE(log, "ck_consoles: %s is not an IPv4 address", host);
			return CHIAKI_ERR_PARSE_ADDR;
		}
		for(struct addrinfo *a = ai; a; a = a->ai_next)
			if(a->ai_family == AF_INET && a->ai_addrlen >= sizeof(struct sockaddr_in))
				target_add(targets, count, (const struct sockaddr_in *)a->ai_addr);
		freeaddrinfo(ai);
		return *count ? CHIAKI_ERR_SUCCESS : CHIAKI_ERR_PARSE_ADDR;
	}

	struct ifaddrs *ifap = NULL;
	if(getifaddrs(&ifap) == 0)
	{
		for(struct ifaddrs *a = ifap; a; a = a->ifa_next)
		{
			if(!a->ifa_addr || a->ifa_addr->sa_family != AF_INET)
				continue;
			if(!(a->ifa_flags & IFF_UP) || !(a->ifa_flags & IFF_BROADCAST) || (a->ifa_flags & IFF_LOOPBACK))
				continue;
			if(!a->ifa_broadaddr || a->ifa_broadaddr->sa_family != AF_INET)
				continue;
			struct sockaddr_in addr;
			memcpy(&addr, a->ifa_broadaddr, sizeof(addr));
			if(addr.sin_addr.s_addr != htonl(INADDR_ANY))
				target_add(targets, count, &addr);
		}
		freeifaddrs(ifap);
	}
	if(!*count)
	{
		struct sockaddr_in addr;
		memset(&addr, 0, sizeof(addr));
		addr.sin_addr.s_addr = htonl(INADDR_BROADCAST);
		target_add(targets, count, &addr);
	}
	return CHIAKI_ERR_SUCCESS;
}

// ---------------------------------------------------------------------------
// Browser: a thread that sends the searches, and the core's discovery thread
// that receives the answers. ck_consoles_discover() runs one for a while.
// ---------------------------------------------------------------------------

struct ck_console_browser_t
{
	ChiakiLog *log;
	CKConsoleBrowserCb cb;
	void *cb_user;
	uint64_t ping_ms;
	/** Missed searches before a console is dropped; 0 keeps them all. */
	uint64_t drop_pings;

	struct sockaddr_in targets[TARGETS_MAX];
	size_t targets_count;

	ChiakiDiscovery discovery;
	ChiakiDiscoveryThread recv_thread;
	ChiakiThread ping_thread;
	ChiakiBoolPredCond stop_cond;

	/** Guards everything below, and signals answered on each answer. */
	ChiakiMutex mutex;
	ChiakiCond answered;
	uint64_t ping_index;
	size_t count;
	CKConsole consoles[CK_CONSOLES_MAX];
	uint64_t seen_ping[CK_CONSOLES_MAX];
};

static void browser_send_search(CKConsoleBrowser *browser)
{
	static const struct
	{
		uint16_t port;
		const char *version;
	} kinds[] = {
		{ CHIAKI_DISCOVERY_PORT_PS5, CHIAKI_DISCOVERY_PROTOCOL_VERSION_PS5 },
		{ CHIAKI_DISCOVERY_PORT_PS4, CHIAKI_DISCOVERY_PROTOCOL_VERSION_PS4 },
	};
	for(size_t t = 0; t < browser->targets_count; t++)
	{
		for(size_t k = 0; k < sizeof(kinds) / sizeof(kinds[0]); k++)
		{
			struct sockaddr_in addr = browser->targets[t];
			addr.sin_port = htons(kinds[k].port);
			ChiakiDiscoveryPacket packet;
			memset(&packet, 0, sizeof(packet));
			packet.cmd = CHIAKI_DISCOVERY_CMD_SRCH;
			packet.protocol_version = (char *)kinds[k].version;
			ChiakiErrorCode err = chiaki_discovery_send(&browser->discovery, &packet, (struct sockaddr *)&addr, sizeof(addr));
			if(err != CHIAKI_ERR_SUCCESS)
				CHIAKI_LOGV(browser->log, "ck_consoles: search to port %u failed: %s",
					(unsigned)kinds[k].port, chiaki_error_string(err));
		}
	}
}

/** browser->mutex must be held. */
static void browser_report(CKConsoleBrowser *browser)
{
	if(browser->cb)
		browser->cb(browser->consoles, browser->count, browser->cb_user);
}

static void browser_ping(CKConsoleBrowser *browser)
{
	chiaki_mutex_lock(&browser->mutex);
	browser->ping_index++;
	bool changed = false;
	for(size_t i = 0; browser->drop_pings && i < browser->count;)
	{
		if(browser->seen_ping[i] + browser->drop_pings >= browser->ping_index)
		{
			i++;
			continue;
		}
		CHIAKI_LOGI(browser->log, "ck_consoles: %s (%s) stopped answering",
			browser->consoles[i].name, browser->consoles[i].addr);
		size_t rest = browser->count - i - 1;
		memmove(&browser->consoles[i], &browser->consoles[i + 1], rest * sizeof(browser->consoles[0]));
		memmove(&browser->seen_ping[i], &browser->seen_ping[i + 1], rest * sizeof(browser->seen_ping[0]));
		browser->count--;
		changed = true;
	}
	if(changed)
		browser_report(browser);
	chiaki_mutex_unlock(&browser->mutex);

	browser_send_search(browser);
}

static void *browser_ping_thread(void *user)
{
	CKConsoleBrowser *browser = user;
	if(chiaki_bool_pred_cond_lock(&browser->stop_cond) != CHIAKI_ERR_SUCCESS)
		return NULL;
	ChiakiErrorCode err = CHIAKI_ERR_TIMEOUT;
	while(err == CHIAKI_ERR_TIMEOUT)
	{
		browser_ping(browser);
		err = chiaki_bool_pred_cond_timedwait(&browser->stop_cond, browser->ping_ms);
	}
	chiaki_bool_pred_cond_unlock(&browser->stop_cond);
	return NULL;
}

/** Runs on the core's discovery thread for every answer. */
static void browser_answer(ChiakiDiscoveryHost *host, void *user)
{
	CKConsoleBrowser *browser = user;
	CKConsole console;
	console_from_host(&console, host);

	chiaki_mutex_lock(&browser->mutex);
	size_t index = browser->count;
	for(size_t i = 0; i < browser->count; i++)
	{
		if(console_same(&browser->consoles[i], &console))
		{
			index = i;
			break;
		}
	}
	bool changed = false;
	if(index == browser->count)
	{
		if(browser->count < CK_CONSOLES_MAX)
		{
			CHIAKI_LOGI(browser->log, "ck_consoles: found %s (%s) at %s", console.name,
				ck_console_state_string(console.state), console.addr);
			browser->consoles[browser->count++] = console;
			changed = true;
		}
		else
		{
			CHIAKI_LOGW(browser->log, "ck_consoles: more than %d consoles, %s left out", CK_CONSOLES_MAX, console.addr);
			index = SIZE_MAX;
		}
	}
	else if(!console_equal(&browser->consoles[index], &console))
	{
		browser->consoles[index] = console;
		changed = true;
	}
	if(index != SIZE_MAX)
		browser->seen_ping[index] = browser->ping_index;
	if(changed)
		browser_report(browser);
	chiaki_cond_broadcast(&browser->answered);
	chiaki_mutex_unlock(&browser->mutex);
}

static ChiakiErrorCode browser_init(CKConsoleBrowser *browser, ChiakiLog *log, const char *host,
	uint64_t ping_ms, uint64_t drop_pings, CKConsoleBrowserCb cb, void *user)
{
	memset(browser, 0, sizeof(*browser));
	browser->log = log;
	browser->cb = cb;
	browser->cb_user = user;
	browser->ping_ms = ping_ms;
	browser->drop_pings = drop_pings;

	ChiakiErrorCode err = targets_resolve(log, host, browser->targets, &browser->targets_count);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;

	err = chiaki_mutex_init(&browser->mutex, false);
	if(err != CHIAKI_ERR_SUCCESS)
		return err;
	err = chiaki_cond_init(&browser->answered, &browser->mutex);
	if(err != CHIAKI_ERR_SUCCESS)
		goto error_mutex;
	err = chiaki_bool_pred_cond_init(&browser->stop_cond);
	if(err != CHIAKI_ERR_SUCCESS)
		goto error_answered;
	err = chiaki_discovery_init(&browser->discovery, log, AF_INET);
	if(err != CHIAKI_ERR_SUCCESS)
		goto error_stop_cond;
	err = chiaki_discovery_thread_start(&browser->recv_thread, &browser->discovery, browser_answer, browser);
	if(err != CHIAKI_ERR_SUCCESS)
		goto error_discovery;
	err = chiaki_thread_create(&browser->ping_thread, browser_ping_thread, browser);
	if(err != CHIAKI_ERR_SUCCESS)
		goto error_recv_thread;
	chiaki_thread_set_name(&browser->ping_thread, "CK Console Search");
	return CHIAKI_ERR_SUCCESS;

error_recv_thread:
	chiaki_discovery_thread_stop(&browser->recv_thread);
error_discovery:
	chiaki_discovery_fini(&browser->discovery);
error_stop_cond:
	chiaki_bool_pred_cond_fini(&browser->stop_cond);
error_answered:
	chiaki_cond_fini(&browser->answered);
error_mutex:
	chiaki_mutex_fini(&browser->mutex);
	return err;
}

static void browser_fini(CKConsoleBrowser *browser)
{
	chiaki_bool_pred_cond_signal(&browser->stop_cond);
	chiaki_thread_join(&browser->ping_thread, NULL);
	// After this no answer can arrive, so the callback is never called again.
	chiaki_discovery_thread_stop(&browser->recv_thread);
	chiaki_discovery_fini(&browser->discovery);
	chiaki_bool_pred_cond_fini(&browser->stop_cond);
	chiaki_cond_fini(&browser->answered);
	chiaki_mutex_fini(&browser->mutex);
}

ChiakiErrorCode ck_console_browser_start(CKConsoleBrowser **browser, ChiakiLog *log, const char *host,
	CKConsoleBrowserCb cb, void *user)
{
	*browser = NULL;
	CKConsoleBrowser *b = malloc(sizeof(*b));
	if(!b)
		return CHIAKI_ERR_MEMORY;
	ChiakiErrorCode err = browser_init(b, log, host, BROWSER_PING_MS, BROWSER_DROP_PINGS, cb, user);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		free(b);
		return err;
	}
	*browser = b;
	return CHIAKI_ERR_SUCCESS;
}

void ck_console_browser_stop(CKConsoleBrowser *browser)
{
	if(!browser)
		return;
	browser_fini(browser);
	free(browser);
}

ChiakiErrorCode ck_consoles_discover(ChiakiLog *log, const char *host, uint32_t timeout_ms,
	CKConsole *consoles, size_t consoles_max, size_t *count)
{
	*count = 0;
	CKConsoleBrowser *browser = malloc(sizeof(*browser));
	if(!browser)
		return CHIAKI_ERR_MEMORY;
	ChiakiErrorCode err = browser_init(browser, log, host, DISCOVER_PING_MS, 0, NULL, NULL);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		free(browser);
		return err;
	}

	uint64_t deadline = chiaki_time_now_monotonic_ms() + timeout_ms;
	chiaki_mutex_lock(&browser->mutex);
	// With a host, its first answer is all there is to wait for.
	while(!(host && browser->count))
	{
		uint64_t now = chiaki_time_now_monotonic_ms();
		if(now >= deadline)
			break;
		chiaki_cond_timedwait(&browser->answered, &browser->mutex, deadline - now);
	}
	size_t n = browser->count < consoles_max ? browser->count : consoles_max;
	memcpy(consoles, browser->consoles, n * sizeof(consoles[0]));
	*count = n;
	chiaki_mutex_unlock(&browser->mutex);

	browser_fini(browser);
	free(browser);
	return CHIAKI_ERR_SUCCESS;
}

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

static bool is_space(char c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

ChiakiErrorCode ck_parse_psn_account_id(const char *text, uint8_t account_id[CHIAKI_PSN_ACCOUNT_ID_SIZE])
{
	if(!text)
		return CHIAKI_ERR_INVALID_DATA;
	while(is_space(*text))
		text++;
	size_t len = strlen(text);
	while(len && is_space(text[len - 1]))
		len--;
	if(!len)
		return CHIAKI_ERR_INVALID_DATA;

	bool decimal = true;
	for(size_t i = 0; i < len; i++)
		decimal = decimal && text[i] >= '0' && text[i] <= '9';

	uint8_t bytes[CHIAKI_PSN_ACCOUNT_ID_SIZE + 4];
	if(decimal)
	{
		// UINT64_MAX has 20 digits.
		if(len > 20)
			return CHIAKI_ERR_INVALID_DATA;
		uint64_t value = 0;
		for(size_t i = 0; i < len; i++)
		{
			uint64_t digit = (uint64_t)(text[i] - '0');
			if(value > (UINT64_MAX - digit) / 10)
				return CHIAKI_ERR_INVALID_DATA;
			value = value * 10 + digit;
		}
		for(size_t i = 0; i < CHIAKI_PSN_ACCOUNT_ID_SIZE; i++)
			bytes[i] = (uint8_t)(value >> (8 * i));
	}
	else
	{
		// 8 bytes are 12 base64 characters, the last one "=". The core's decoder
		// indexes a table with each char, so anything else is refused first.
		if(len != 12 || text[11] != '=' || text[10] == '=')
			return CHIAKI_ERR_INVALID_DATA;
		for(size_t i = 0; i < 11; i++)
		{
			char c = text[i];
			if(!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '+' || c == '/'))
				return CHIAKI_ERR_INVALID_DATA;
		}
		size_t size = sizeof(bytes);
		if(chiaki_base64_decode(text, len, bytes, &size) != CHIAKI_ERR_SUCCESS || size != CHIAKI_PSN_ACCOUNT_ID_SIZE)
			return CHIAKI_ERR_INVALID_DATA;
	}

	bool zero = true;
	for(size_t i = 0; i < CHIAKI_PSN_ACCOUNT_ID_SIZE; i++)
		zero = zero && !bytes[i];
	if(zero)
		return CHIAKI_ERR_INVALID_DATA;
	memcpy(account_id, bytes, CHIAKI_PSN_ACCOUNT_ID_SIZE);
	return CHIAKI_ERR_SUCCESS;
}

ChiakiErrorCode ck_parse_regist_pin(const char *text, uint32_t *pin)
{
	if(!text)
		return CHIAKI_ERR_INVALID_DATA;
	uint32_t value = 0;
	int digits = 0;
	for(; *text; text++)
	{
		if(*text == ' ' || *text == '-')
			continue;
		if(*text < '0' || *text > '9' || digits == 8)
			return CHIAKI_ERR_INVALID_DATA;
		value = value * 10 + (uint32_t)(*text - '0');
		digits++;
	}
	if(digits != 8)
		return CHIAKI_ERR_INVALID_DATA;
	*pin = value;
	return CHIAKI_ERR_SUCCESS;
}

const char *ck_regist_result_string(CKRegistResult result)
{
	switch(result)
	{
		case CK_REGIST_SUCCESS:
			return "Registro listo.";
		case CK_REGIST_CANCELED:
			return "Registro cancelado.";
		case CK_REGIST_BAD_HOST:
			return "La dirección de la consola no es válida.";
		case CK_REGIST_NOT_FOUND:
			return "La consola no respondió. Revisa la IP y que la consola esté encendida y en la pantalla de vincular dispositivo.";
		case CK_REGIST_UNREACHABLE:
			return "No se pudo conectar con la consola.";
		case CK_REGIST_WRONG_PIN:
			return "La consola rechazó el PIN. Pide uno nuevo en la consola e inténtalo otra vez.";
		case CK_REGIST_WRONG_ACCOUNT:
			return "La consola rechazó el Account ID de PSN. Revisa que sea el de la cuenta de la consola.";
		case CK_REGIST_REJECTED:
			return "La consola rechazó el registro.";
		case CK_REGIST_NO_REPLY:
			return "La consola no contestó a tiempo.";
		case CK_REGIST_BAD_REPLY:
			return "La respuesta de la consola llegó incompleta.";
		default:
			return "No se pudo hacer el registro.";
	}
}

struct ck_regist_t
{
	ChiakiRegist regist;
	/** What the core logs into: forwarded to forward_log and read by regist_log_cb. */
	ChiakiLog log;
	ChiakiLog *forward_log;
	CKRegistCb cb;
	void *cb_user;
	/** Filled from the core's error lines while it runs; only its thread touches it. */
	CKRegistResult failure;
	int http_code;
	uint32_t reason;
};

/**
 * The core's regist only reports success or failure; the error line it logs
 * before failing says why. Matched by prefix, first match wins.
 */
static const struct
{
	const char *prefix;
	CKRegistResult result;
} regist_errors[] = {
	{ "Regist failed to getaddrinfo", CK_REGIST_BAD_HOST },
	{ "Regist timed out waiting for search response", CK_REGIST_NOT_FOUND },
	{ "Regist failed to receive search response", CK_REGIST_NOT_FOUND },
	{ "Regist eventually failed to connect", CK_REGIST_UNREACHABLE },
	{ "Regist connect failed", CK_REGIST_UNREACHABLE },
	{ "Regist failed to send", CK_REGIST_UNREACHABLE },
	{ "Regist received HTTP code", CK_REGIST_REJECTED },
	{ "Regist failed to receive response", CK_REGIST_NO_REPLY },
	{ "Regist timed out receiving response", CK_REGIST_NO_REPLY },
	{ "Regist failed to pare response", CK_REGIST_BAD_REPLY },
	{ "Regist failed to parse response", CK_REGIST_BAD_REPLY },
	{ "Regist response", CK_REGIST_BAD_REPLY },
	{ "Regist received invalid", CK_REGIST_BAD_REPLY },
};

static const char reason_prefix[] = "Reported Application Reason: ";

static void regist_classify(CKRegist *regist, const char *msg)
{
	if(!strncmp(msg, reason_prefix, sizeof(reason_prefix) - 1))
	{
		regist->reason = (uint32_t)strtoul(msg + sizeof(reason_prefix) - 1, NULL, 16);
		if(regist->reason == CHIAKI_RP_APPLICATION_REASON_REGIST_FAILED)
			regist->failure = CK_REGIST_WRONG_PIN;
		else if(regist->reason == CHIAKI_RP_APPLICATION_REASON_INVALID_PSN_ID)
			regist->failure = CK_REGIST_WRONG_ACCOUNT;
		return;
	}
	if(regist->failure != CK_REGIST_FAILED)
		return;
	for(size_t i = 0; i < sizeof(regist_errors) / sizeof(regist_errors[0]); i++)
	{
		if(strncmp(msg, regist_errors[i].prefix, strlen(regist_errors[i].prefix)))
			continue;
		regist->failure = regist_errors[i].result;
		if(regist->failure == CK_REGIST_REJECTED)
			sscanf(msg, "Regist received HTTP code %d", &regist->http_code);
		return;
	}
}

static void regist_log_cb(ChiakiLogLevel level, const char *msg, void *user)
{
	CKRegist *regist = user;
	if(level == CHIAKI_LOG_ERROR)
		regist_classify(regist, msg);
	ChiakiLog *forward = regist->forward_log;
	if(forward && (forward->level_mask & level))
		(forward->cb ? forward->cb : chiaki_log_cb_print)(level, msg, forward->user);
}

static void regist_event_cb(ChiakiRegistEvent *event, void *user)
{
	CKRegist *regist = user;
	CKRegistOutcome outcome;
	memset(&outcome, 0, sizeof(outcome));
	switch(event->type)
	{
		case CHIAKI_REGIST_EVENT_TYPE_FINISHED_SUCCESS:
			outcome.result = CK_REGIST_SUCCESS;
			outcome.registered_host = *event->registered_host;
			break;
		case CHIAKI_REGIST_EVENT_TYPE_FINISHED_CANCELED:
			outcome.result = CK_REGIST_CANCELED;
			break;
		default:
			outcome.result = regist->failure;
			outcome.http_code = regist->http_code;
			outcome.reason = regist->reason;
			break;
	}
	regist->cb(&outcome, regist->cb_user);
}

ChiakiErrorCode ck_regist_start(CKRegist **regist, ChiakiLog *log, const CKRegistRequest *request,
	CKRegistCb cb, void *user)
{
	*regist = NULL;
	if(!request->host || !cb)
		return CHIAKI_ERR_INVALID_DATA;
	CKRegist *r = calloc(1, sizeof(*r));
	if(!r)
		return CHIAKI_ERR_MEMORY;
	r->forward_log = log;
	r->cb = cb;
	r->cb_user = user;
	r->failure = CK_REGIST_FAILED;
	chiaki_log_init(&r->log, (log ? log->level_mask : 0) | CHIAKI_LOG_ERROR, regist_log_cb, r);

	ChiakiRegistInfo info;
	memset(&info, 0, sizeof(info));
	info.target = request->target;
	info.host = request->host;
	info.broadcast = false;
	info.psn_online_id = NULL;
	memcpy(info.psn_account_id, request->psn_account_id, sizeof(info.psn_account_id));
	info.pin = request->pin;

	ChiakiErrorCode err = chiaki_regist_start(&r->regist, &r->log, &info, regist_event_cb, r);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		free(r);
		return err;
	}
	*regist = r;
	return CHIAKI_ERR_SUCCESS;
}

void ck_regist_cancel(CKRegist *regist)
{
	if(regist)
		chiaki_regist_stop(&regist->regist);
}

void ck_regist_free(CKRegist *regist)
{
	if(!regist)
		return;
	chiaki_regist_fini(&regist->regist);
	free(regist);
}

typedef struct regist_wait_t
{
	ChiakiBoolPredCond done;
	CKRegistOutcome *outcome;
} RegistWait;

static void regist_wait_cb(const CKRegistOutcome *outcome, void *user)
{
	RegistWait *wait = user;
	*wait->outcome = *outcome;
	chiaki_bool_pred_cond_signal(&wait->done);
}

CKRegistResult ck_regist_run(ChiakiLog *log, const CKRegistRequest *request, CKRegistOutcome *outcome)
{
	memset(outcome, 0, sizeof(*outcome));
	outcome->result = CK_REGIST_FAILED;
	RegistWait wait;
	wait.outcome = outcome;
	if(chiaki_bool_pred_cond_init(&wait.done) != CHIAKI_ERR_SUCCESS)
		return outcome->result;
	CKRegist *regist;
	if(ck_regist_start(&regist, log, request, regist_wait_cb, &wait) == CHIAKI_ERR_SUCCESS)
	{
		chiaki_bool_pred_cond_lock(&wait.done);
		chiaki_bool_pred_cond_wait(&wait.done);
		chiaki_bool_pred_cond_unlock(&wait.done);
		ck_regist_free(regist);
	}
	chiaki_bool_pred_cond_fini(&wait.done);
	return outcome->result;
}

// ---------------------------------------------------------------------------
// Wake-up
// ---------------------------------------------------------------------------

ChiakiErrorCode ck_regist_key_credential(const char rp_regist_key[CHIAKI_SESSION_AUTH_SIZE], uint64_t *credential)
{
	char key[CHIAKI_SESSION_AUTH_SIZE + 1];
	memcpy(key, rp_regist_key, CHIAKI_SESSION_AUTH_SIZE);
	key[CHIAKI_SESSION_AUTH_SIZE] = '\0';
	size_t len = strlen(key);
	if(!len)
		return CHIAKI_ERR_INVALID_DATA;
	uint64_t value = 0;
	for(size_t i = 0; i < len; i++)
	{
		char c = key[i];
		unsigned digit;
		if(c >= '0' && c <= '9')
			digit = (unsigned)(c - '0');
		else if(c >= 'a' && c <= 'f')
			digit = (unsigned)(c - 'a' + 10);
		else if(c >= 'A' && c <= 'F')
			digit = (unsigned)(c - 'A' + 10);
		else
			return CHIAKI_ERR_INVALID_DATA;
		value = (value << 4) | digit;
	}
	*credential = value;
	return CHIAKI_ERR_SUCCESS;
}

ChiakiErrorCode ck_console_wakeup(ChiakiLog *log, const char *host, const ChiakiRegisteredHost *registered_host)
{
	uint64_t credential;
	ChiakiErrorCode err = ck_regist_key_credential(registered_host->rp_regist_key, &credential);
	if(err != CHIAKI_ERR_SUCCESS)
	{
		CHIAKI_LOGE(log, "ck_consoles: the registration has no usable regist key");
		return err;
	}
	return chiaki_discovery_wakeup(log, NULL, host, credential, chiaki_target_is_ps5(registered_host->target));
}
