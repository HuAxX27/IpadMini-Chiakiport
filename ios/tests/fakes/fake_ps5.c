// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// The fake PS5 of fake_ps5.h: one thread, three sockets on 127.0.0.1.

#include "fake_ps5.h"

#include <chiaki/base64.h>
#include <chiaki/rpcrypt.h>

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#define REGIST_PORT 9295
/** Where the request carries the encrypted headers, and the hidden ambassador. */
#define INNER_OFF 0x1e0
#define KEY_0_OFF_BYTE 0x18d
#define AEROPAUSE_A_OFF 0x191
#define AEROPAUSE_B_OFF 0xc7

void fake_ps5_init(FakePs5 *ps5)
{
	memset(ps5, 0, sizeof(*ps5));
	ps5->pin = 12345678;
	static const uint8_t account[CHIAKI_PSN_ACCOUNT_ID_SIZE] = { 0x21, 0x43, 0x65, 0x87, 0x09, 0xba, 0xdc, 0x0e };
	memcpy(ps5->psn_account_id, account, sizeof(account));
	ps5->state = CHIAKI_DISCOVERY_HOST_STATE_STANDBY;
	ps5->regist_mode = FAKE_PS5_REGIST_OK;
	ps5->udp_discovery = ps5->udp_regist = ps5->tcp_regist = -1;
	ps5->stop_pipe[0] = ps5->stop_pipe[1] = -1;
}

static int bind_local(int type, uint16_t port)
{
	int fd = socket(AF_INET, type, 0);
	if(fd < 0)
		return -1;
	int one = 1;
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
	struct sockaddr_in addr;
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = htons(port);
	if(bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0 || (type == SOCK_STREAM && listen(fd, 4) < 0))
	{
		int e = errno;
		close(fd);
		errno = e;
		return -1;
	}
	return fd;
}

static void discovery_packet(FakePs5 *ps5)
{
	char buf[512];
	struct sockaddr_in from;
	socklen_t from_len = sizeof(from);
	ssize_t n = recvfrom(ps5->udp_discovery, buf, sizeof(buf) - 1, 0, (struct sockaddr *)&from, &from_len);
	if(n <= 0)
		return;
	buf[n] = '\0';

	pthread_mutex_lock(&ps5->mutex);
	if(!strncmp(buf, "WAKEUP ", 7))
	{
		const char *cred = strstr(buf, "user-credential:");
		uint64_t credential = cred ? strtoull(cred + strlen("user-credential:"), NULL, 10) : 0;
		ps5->wakeups++;
		ps5->last_credential = credential;
		if(credential == FAKE_PS5_CREDENTIAL)
			ps5->state = CHIAKI_DISCOVERY_HOST_STATE_READY;
		pthread_mutex_unlock(&ps5->mutex);
		return;
	}
	if(strncmp(buf, "SRCH ", 5) || ps5->discovery_silent)
	{
		pthread_mutex_unlock(&ps5->mutex);
		return;
	}
	ps5->searches++;
	bool ready = ps5->state == CHIAKI_DISCOVERY_HOST_STATE_READY;
	pthread_mutex_unlock(&ps5->mutex);

	char reply[512];
	int len = snprintf(reply, sizeof(reply),
		"HTTP/1.1 %s\n"
		"host-id:" FAKE_PS5_HOST_ID "\n"
		"host-type:PS5\n"
		"host-name:" FAKE_PS5_NAME "\n"
		"host-request-port:997\n"
		"device-discovery-protocol-version:" CHIAKI_DISCOVERY_PROTOCOL_VERSION_PS5 "\n"
		"system-version:" FAKE_PS5_SYSTEM_VERSION "\n"
		"%s",
		ready ? "200 Ok" : "620 Server Standby",
		ready ? "running-app-titleid:PPSA01234\nrunning-app-name:Astro Bot\n" : "");
	sendto(ps5->udp_discovery, reply, (size_t)len, 0, (struct sockaddr *)&from, from_len);
}

static void regist_search(FakePs5 *ps5)
{
	char buf[64];
	struct sockaddr_in from;
	socklen_t from_len = sizeof(from);
	ssize_t n = recvfrom(ps5->udp_regist, buf, sizeof(buf), 0, (struct sockaddr *)&from, &from_len);
	if(n < 4 || memcmp(buf, "SRC3", 4) || ps5->regist_mode == FAKE_PS5_REGIST_SILENT)
		return;
	sendto(ps5->udp_regist, "RES3", 4, 0, (struct sockaddr *)&from, from_len);
}

static void send_all(int fd, const void *buf, size_t size)
{
	const char *p = buf;
	while(size)
	{
		ssize_t n = send(fd, p, size, 0);
		if(n <= 0)
			return;
		p += n;
		size -= (size_t)n;
	}
}

static void send_refusal(int fd, int code, const char *reason)
{
	char head[256];
	int len = snprintf(head, sizeof(head), "HTTP/1.1 %d %s\r\n%s%s%sContent-Length: 0\r\n\r\n", code,
		code == 403 ? "Forbidden" : "Error", reason ? "RP-Application-Reason: " : "", reason ? reason : "",
		reason ? "\r\n" : "");
	send_all(fd, head, (size_t)len);
}

/** Reads the request into buf: header, then Content-Length bytes. Returns the payload or NULL. */
static uint8_t *read_request(int fd, char *buf, size_t buf_size, size_t *payload_size)
{
	size_t filled = 0;
	char *body = NULL;
	size_t content = 0;
	while(true)
	{
		if(body && filled >= (size_t)(body - buf) + content)
			break;
		if(filled == buf_size)
			return NULL;
		ssize_t n = recv(fd, buf + filled, buf_size - filled, 0);
		if(n <= 0)
			return NULL;
		filled += (size_t)n;
		if(body)
			continue;
		char *end = NULL;
		for(size_t i = 0; i + 4 <= filled; i++)
		{
			if(!memcmp(buf + i, "\r\n\r\n", 4))
			{
				end = buf + i;
				break;
			}
		}
		if(!end)
			continue;
		*end = '\0';
		if(!strstr(buf, "POST /sie/ps5/rp/sess/rgst "))
			return NULL;
		const char *length = strstr(buf, "Content-Length: ");
		if(!length)
			return NULL;
		content = strtoul(length + strlen("Content-Length: "), NULL, 10);
		body = end + 4;
		if(content > buf_size - (size_t)(body - buf))
			return NULL;
	}
	*payload_size = content;
	return (uint8_t *)body;
}

/**
 * Undoes chiaki_rpcrypt_aeropause(). Each of its bytes depends only on the
 * same byte of the ambassador, so trying the 256 values of every byte is
 * enough, without a copy of the core's key tables.
 */
static bool ambassador_from_aeropause(size_t key_1_off, const uint8_t *aeropause, uint8_t *ambassador)
{
	bool found[CHIAKI_RPCRYPT_KEY_SIZE] = { false };
	for(unsigned v = 0; v < 256; v++)
	{
		uint8_t guess[CHIAKI_RPCRYPT_KEY_SIZE];
		uint8_t out[CHIAKI_RPCRYPT_KEY_SIZE];
		memset(guess, (int)v, sizeof(guess));
		if(chiaki_rpcrypt_aeropause(CHIAKI_TARGET_PS5_1, key_1_off, out, guess) != CHIAKI_ERR_SUCCESS)
			return false;
		for(size_t i = 0; i < CHIAKI_RPCRYPT_KEY_SIZE; i++)
		{
			if(out[i] == aeropause[i])
			{
				ambassador[i] = (uint8_t)v;
				found[i] = true;
			}
		}
	}
	for(size_t i = 0; i < CHIAKI_RPCRYPT_KEY_SIZE; i++)
		if(!found[i])
			return false;
	return true;
}

static void regist_request(FakePs5 *ps5, int fd)
{
	struct timeval tv = { 2, 0 };
	setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));

	char buf[2048];
	size_t size = 0;
	uint8_t *payload = read_request(fd, buf, sizeof(buf), &size);
	pthread_mutex_lock(&ps5->mutex);
	ps5->regist_requests++;
	FakePs5RegistMode mode = ps5->regist_mode;
	pthread_mutex_unlock(&ps5->mutex);
	if(!payload || mode == FAKE_PS5_REGIST_HANG_UP)
		return;
	if(mode == FAKE_PS5_REGIST_SERVER_ERROR)
	{
		send_refusal(fd, 500, NULL);
		return;
	}
	if(size <= INNER_OFF)
	{
		send_refusal(fd, 400, NULL);
		return;
	}

	// The client hides its random ambassador in the payload as the
	// "aeropause", and picks the key tables with two of its filler bytes.
	uint8_t aeropause[CHIAKI_RPCRYPT_KEY_SIZE];
	memcpy(aeropause, payload + AEROPAUSE_A_OFF, 8);
	memcpy(aeropause + 8, payload + AEROPAUSE_B_OFF, 8);
	size_t key_0_off = payload[KEY_0_OFF_BYTE] & 0x1f;
	size_t key_1_off = payload[0] >> 3;
	uint8_t ambassador[CHIAKI_RPCRYPT_KEY_SIZE];
	ChiakiRPCrypt crypt;
	if(!ambassador_from_aeropause(key_1_off, aeropause, ambassador)
		|| chiaki_rpcrypt_init_regist(&crypt, CHIAKI_TARGET_PS5_1, ambassador, key_0_off, ps5->pin) != CHIAKI_ERR_SUCCESS)
	{
		send_refusal(fd, 400, NULL);
		return;
	}

	// With another PIN the keys differ and the headers come out as noise.
	char inner[512];
	size_t inner_size = size - INNER_OFF;
	if(inner_size >= sizeof(inner))
		inner_size = sizeof(inner) - 1;
	chiaki_rpcrypt_decrypt(&crypt, 0, payload + INNER_OFF, (uint8_t *)inner, inner_size);
	inner[inner_size] = '\0';
	if(strncmp(inner, "Client-Type: ", strlen("Client-Type: ")))
	{
		send_refusal(fd, 403, "80108b09");
		return;
	}

	char account[32] = "";
	const char *id = strstr(inner, "Np-AccountId: ");
	if(id)
	{
		id += strlen("Np-AccountId: ");
		size_t len = strcspn(id, "\r\n");
		if(len < sizeof(account))
		{
			memcpy(account, id, len);
			account[len] = '\0';
		}
	}
	char expected[32];
	chiaki_base64_encode(ps5->psn_account_id, sizeof(ps5->psn_account_id), expected, sizeof(expected));
	pthread_mutex_lock(&ps5->mutex);
	snprintf(ps5->last_account_b64, sizeof(ps5->last_account_b64), "%s", account);
	pthread_mutex_unlock(&ps5->mutex);
	if(strcmp(account, expected))
	{
		send_refusal(fd, 403, "80108b02");
		return;
	}

	// The PS5 sends its registration key as the hex of its text.
	char regist_key_hex[2 * sizeof(FAKE_PS5_REGIST_KEY)];
	for(size_t i = 0; i < strlen(FAKE_PS5_REGIST_KEY); i++)
		snprintf(regist_key_hex + 2 * i, 3, "%02x", (unsigned char)FAKE_PS5_REGIST_KEY[i]);
	char body[512];
	int body_len = snprintf(body, sizeof(body),
		"AP-Ssid: CKFalsa\r\n"
		"AP-Name: CKFalsa\r\n"
		"PS5-Mac: 0a1b2c3d4e5f\r\n"
		"%s%s%s"
		"PS5-Nickname: " FAKE_PS5_NAME "\r\n"
		"RP-KeyType: 2\r\n"
		"RP-Key: " FAKE_PS5_RP_KEY_HEX "\r\n",
		mode == FAKE_PS5_REGIST_NO_KEY ? "" : "PS5-RegistKey: ",
		mode == FAKE_PS5_REGIST_NO_KEY ? "" : regist_key_hex,
		mode == FAKE_PS5_REGIST_NO_KEY ? "" : "\r\n");
	chiaki_rpcrypt_encrypt(&crypt, 0, (uint8_t *)body, (uint8_t *)body, (size_t)body_len);
	char head[128];
	int head_len = snprintf(head, sizeof(head), "HTTP/1.1 200 OK\r\nContent-Length: %d\r\n\r\n", body_len);
	send_all(fd, head, (size_t)head_len);
	send_all(fd, body, (size_t)body_len);
}

static void *fake_ps5_thread(void *user)
{
	FakePs5 *ps5 = user;
	while(true)
	{
		fd_set fds;
		FD_ZERO(&fds);
		int max = ps5->stop_pipe[0];
		FD_SET(ps5->stop_pipe[0], &fds);
		int socks[] = { ps5->udp_discovery, ps5->udp_regist, ps5->tcp_regist };
		for(size_t i = 0; i < sizeof(socks) / sizeof(socks[0]); i++)
		{
			if(socks[i] < 0)
				continue;
			FD_SET(socks[i], &fds);
			if(socks[i] > max)
				max = socks[i];
		}
		if(select(max + 1, &fds, NULL, NULL, NULL) < 0)
		{
			if(errno == EINTR)
				continue;
			break;
		}
		if(FD_ISSET(ps5->stop_pipe[0], &fds))
			break;
		if(FD_ISSET(ps5->udp_discovery, &fds))
			discovery_packet(ps5);
		if(FD_ISSET(ps5->udp_regist, &fds))
			regist_search(ps5);
		if(ps5->tcp_regist >= 0 && FD_ISSET(ps5->tcp_regist, &fds))
		{
			int fd = accept(ps5->tcp_regist, NULL, NULL);
			if(fd >= 0)
			{
				regist_request(ps5, fd);
				close(fd);
			}
		}
	}
	return NULL;
}

static void close_fd(int *fd)
{
	if(*fd >= 0)
		close(*fd);
	*fd = -1;
}

static void close_all(FakePs5 *ps5)
{
	close_fd(&ps5->udp_discovery);
	close_fd(&ps5->udp_regist);
	close_fd(&ps5->tcp_regist);
	close_fd(&ps5->stop_pipe[0]);
	close_fd(&ps5->stop_pipe[1]);
}

int fake_ps5_start(FakePs5 *ps5)
{
	if(pthread_mutex_init(&ps5->mutex, NULL) != 0)
		return -1;
	ps5->udp_discovery = bind_local(SOCK_DGRAM, CHIAKI_DISCOVERY_PORT_PS5);
	ps5->udp_regist = bind_local(SOCK_DGRAM, REGIST_PORT);
	if(ps5->regist_mode != FAKE_PS5_REGIST_NO_TCP)
		ps5->tcp_regist = bind_local(SOCK_STREAM, REGIST_PORT);
	if(ps5->udp_discovery < 0 || ps5->udp_regist < 0
		|| (ps5->regist_mode != FAKE_PS5_REGIST_NO_TCP && ps5->tcp_regist < 0)
		|| pipe(ps5->stop_pipe) < 0)
	{
		int e = errno;
		close_all(ps5);
		pthread_mutex_destroy(&ps5->mutex);
		errno = e;
		return -1;
	}
	if(pthread_create(&ps5->thread, NULL, fake_ps5_thread, ps5) != 0)
	{
		close_all(ps5);
		pthread_mutex_destroy(&ps5->mutex);
		return -1;
	}
	return 0;
}

void fake_ps5_stop(FakePs5 *ps5)
{
	ssize_t n = write(ps5->stop_pipe[1], "x", 1);
	(void)n;
	pthread_join(ps5->thread, NULL);
	close_all(ps5);
	pthread_mutex_destroy(&ps5->mutex);
}

ChiakiDiscoveryHostState fake_ps5_state(FakePs5 *ps5)
{
	pthread_mutex_lock(&ps5->mutex);
	ChiakiDiscoveryHostState state = ps5->state;
	pthread_mutex_unlock(&ps5->mutex);
	return state;
}

int fake_ps5_wakeups(FakePs5 *ps5)
{
	pthread_mutex_lock(&ps5->mutex);
	int wakeups = ps5->wakeups;
	pthread_mutex_unlock(&ps5->mutex);
	return wakeups;
}
