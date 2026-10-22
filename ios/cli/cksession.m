// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// cksession (A2.6): opens a streaming session with a registered console
// through CKSession, the class the app uses (contract C1), and closes it, as
// many times as asked, to try the session on the iPad without the app. Prints
// every change of state, the metrics once per second and the resident memory
// after each round. See README.md.

#import "Bridge/CKSession.h"

#include "Bridge/ck_registry.h"
#include "Bridge/ck_session.h"

#include <chiaki/common.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char usage[] =
	"Uso: cksession [-c CONSOLA] [-H IP] [-s RUTA] [-n VECES] [-t SEGUNDOS]\n"
	"                [-r 360|540|720] [-f 30|60] [-b KBPS]\n"
	"  Abre una sesión con la consola registrada (ckctl regist), la deja\n"
	"  transmitir SEGUNDOS (10) y la cierra, VECES veces (1). La consola debe\n"
	"  estar encendida: ckctl wakeup -w 60 la despierta.\n"
	"\n"
	"  -c CONSOLA   nombre, id o IP de la consola; sin -c, la única registrada\n"
	"  -H IP        su IP, si no es la última que guardó ckctl\n"
	"  -s RUTA      el archivo de registros (" CK_REGISTRY_DEFAULT_PATH ")\n"
	"  -r, -f, -b   resolución, cuadros por segundo y bitrate (540p, 30, el del core)\n"
	"\n"
	"  CK_LOG_VERBOSE=1 cksession ...   muestra todo lo que dice el core\n";

static const char *const state_names[] = { "inactiva", "conectando", "transmitiendo", "detenida" };

@interface CKSessionRunner : NSObject <CKSessionDelegate>
@property (nonatomic) CKSessionState lastState;
@property (nonatomic) BOOL streamed;
@property (nonatomic) BOOL quit;
@end

@implementation CKSessionRunner

- (void)session:(CKSession *)session didChangeState:(CKSessionState)state
{
	(void)session;
	self.lastState = state;
	if(state == CKSessionStateStreaming)
		self.streamed = YES;
	printf("  estado: %s\n", state_names[state]);
	fflush(stdout);
}

- (void)session:(CKSession *)session didQuitWithReason:(ChiakiQuitReason)reason message:(NSString *)message
{
	(void)session;
	self.quit = YES;
	printf("  terminó: %s%s%s\n", chiaki_quit_reason_string(reason), message ? ": " : "", message ? message.UTF8String : "");
	fflush(stdout);
}

- (void)session:(CKSession *)session didUpdateMetrics:(CKMetrics)metrics
{
	(void)session;
	printf("  %.1f cuadros/s, %u kbps, %u perdidos, %u de audio ocultados, %.1f ms en el callback, %u KB\n",
		metrics.fps_decoded, metrics.bitrate_kbps, metrics.frames_lost, metrics.audio_frames_concealed,
		metrics.decode_ms_avg, metrics.resident_kb);
	fflush(stdout);
}

@end

/** Runs the main run loop, where CKSession delivers, until done() or seconds pass. */
static void run_until(NSTimeInterval seconds, BOOL (^done)(void))
{
	NSDate *limit = [NSDate dateWithTimeIntervalSinceNow:seconds];
	while(!done() && [limit timeIntervalSinceNow] > 0)
	{
		@autoreleasepool
		{
			[[NSRunLoop mainRunLoop] runMode:NSDefaultRunLoopMode
								  beforeDate:[NSDate dateWithTimeIntervalSinceNow:0.1]];
		}
	}
}

static int usage_error(const char *what)
{
	fprintf(stderr, "cksession: %s\n\n%s", what, usage);
	return 2;
}

static bool parse_number(const char *text, long min, long max, long *value)
{
	char *end;
	long v = strtol(text, &end, 10);
	if(!*text || *end || v < min || v > max)
		return false;
	*value = v;
	return true;
}

int main(int argc, char **argv)
{
	@autoreleasepool
	{
		const char *console = NULL;
		const char *host = NULL;
		const char *store = CK_REGISTRY_DEFAULT_PATH;
		long rounds = 1;
		long seconds = 10;
		CKSettings settings;
		ck_settings_set_default(&settings);

		for(int i = 1; i < argc; i++)
		{
			const char *arg = argv[i];
			if(!strcmp(arg, "-h") || !strcmp(arg, "--help"))
			{
				fputs(usage, stdout);
				return 0;
			}
			if(i + 1 >= argc)
				return usage_error("a una opción le falta su valor");
			const char *value = argv[++i];
			long number;
			if(!strcmp(arg, "-c"))
				console = value;
			else if(!strcmp(arg, "-H"))
				host = value;
			else if(!strcmp(arg, "-s"))
				store = value;
			else if(!strcmp(arg, "-n"))
			{
				if(!parse_number(value, 1, 1000, &rounds))
					return usage_error("-n pide un número de 1 a 1000");
			}
			else if(!strcmp(arg, "-t"))
			{
				if(!parse_number(value, 1, 3600, &seconds))
					return usage_error("-t pide segundos de 1 a 3600");
			}
			else if(!strcmp(arg, "-r"))
			{
				if(!strcmp(value, "360"))
					settings.resolution = CHIAKI_VIDEO_RESOLUTION_PRESET_360p;
				else if(!strcmp(value, "540"))
					settings.resolution = CHIAKI_VIDEO_RESOLUTION_PRESET_540p;
				else if(!strcmp(value, "720"))
					settings.resolution = CHIAKI_VIDEO_RESOLUTION_PRESET_720p;
				else
					return usage_error("-r pide 360, 540 o 720");
			}
			else if(!strcmp(arg, "-f"))
			{
				if(!strcmp(value, "30"))
					settings.fps = CHIAKI_VIDEO_FPS_PRESET_30;
				else if(!strcmp(value, "60"))
					settings.fps = CHIAKI_VIDEO_FPS_PRESET_60;
				else
					return usage_error("-f pide 30 o 60");
			}
			else if(!strcmp(arg, "-b"))
			{
				if(!parse_number(value, 500, 50000, &number))
					return usage_error("-b pide kbps de 500 a 50000");
				settings.bitrate_kbps = (uint32_t)number;
			}
			else
				return usage_error("opción desconocida");
		}

		ChiakiErrorCode err = chiaki_lib_init();
		if(err != CHIAKI_ERR_SUCCESS)
		{
			fprintf(stderr, "cksession: no se pudo iniciar el core: %s\n", chiaki_error_string(err));
			return 1;
		}

		CKRegistry registry;
		err = ck_registry_load(&registry, store);
		if(err != CHIAKI_ERR_SUCCESS)
		{
			fprintf(stderr, "cksession: no pude leer los registros de %s\n", store);
			return 1;
		}
		CKRegistryEntry *entry = NULL;
		if(console)
			entry = ck_registry_find(&registry, console);
		else if(registry.count == 1)
			entry = &registry.entries[0];
		else if(host)
			entry = ck_registry_find(&registry, host);
		if(!entry)
		{
			fprintf(stderr, "cksession: %s\n", registry.count ? "elige la consola con -c (ckctl list las muestra)"
				: "no hay consolas registradas; regístrala con ckctl regist");
			return 1;
		}
		if(!host)
			host = entry->last_host;
		if(!host[0])
		{
			fprintf(stderr, "cksession: no sé la IP de la consola; pásala con -H o corre ckctl discover\n");
			return 1;
		}

		ChiakiConnectVideoProfile profile;
		ck_session_video_profile(&settings, &profile);
		printf("Consola %.*s en %s, %ux%u a %u cuadros/s, %u kbps. Memoria: %u KB\n",
			(int)strnlen(entry->host.server_nickname, sizeof(entry->host.server_nickname)),
			entry->host.server_nickname, host, profile.width, profile.height, profile.max_fps, profile.bitrate,
			ck_resident_kb());

		long streamed = 0;
		for(long round = 1; round <= rounds; round++)
		{
			// The console needs a moment to let go of the last session.
			if(round > 1)
				run_until(3, ^BOOL { return NO; });
			printf("Ronda %ld de %ld\n", round, rounds);
			@autoreleasepool
			{
				CKSessionRunner *runner = [CKSessionRunner new];
				CKSession *session = [[CKSession alloc] initWithHost:@(host) registeredHost:&entry->host settings:settings];
				session.delegate = runner;
				NSError *error = nil;
				if(![session start:&error])
				{
					printf("  no arrancó: %s\n", error.localizedDescription.UTF8String);
					continue;
				}
				// Connecting takes a few seconds; then the stream runs for -t.
				run_until(30, ^BOOL { return runner.streamed || runner.quit; });
				if(runner.streamed && !runner.quit)
					run_until(seconds, ^BOOL { return runner.quit; });
				[session stop];
				// The quit and the last state come through the run loop.
				run_until(5, ^BOOL { return runner.lastState == CKSessionStateStopped && runner.quit; });
				if(runner.streamed)
					streamed++;
			}
			printf("  memoria después de cerrar: %u KB\n", ck_resident_kb());
			fflush(stdout);
		}
		printf("Transmitió en %ld de %ld rondas.\n", streamed, rounds);
		return streamed == rounds ? 0 : 1;
	}
}
