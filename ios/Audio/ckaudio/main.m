// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// ckaudio (A4.2): tries the iPad's audio output from a shell, without the app.
// "ckaudio tono" plays a test tone through the same AudioQueue output and
// audio session the player uses, and every 10 s says whether the queue ran
// dry. P2-5 is two minutes of it without a cut. See ../README.md.

#import <Foundation/Foundation.h>

#include "Audio/ck_audio_output.h"
#include "Audio/ck_audio_session.h"
#include "Audio/ck_audio_tone.h"

#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char usage[] =
	"Uso: ckaudio tono [-s SEGUNDOS] [-f HZ] [-d DBFS] [-b MS] [-n BÚFERES] [-i MS]\n"
	"  Toca un tono de prueba por la bocina del iPad con AudioQueue, como lo hará\n"
	"  el reproductor de audio, y cada 10 s dice si la cola se quedó sin audio.\n"
	"\n"
	"  -s SEGUNDOS  cuánto suena (120)\n"
	"  -f HZ        la frecuencia del tono (440)\n"
	"  -d DBFS      el volumen, en dB bajo el máximo (-12; 0 es el máximo)\n"
	"  -b MS        lo que dura cada búfer de la cola (10)\n"
	"  -n BÚFERES   cuántos búferes tiene la cola (3)\n"
	"  -i MS        el búfer del hardware que se le pide a iOS (10; 0 deja el de iOS)\n"
	"\n"
	"  Ctrl-C lo para antes. CK_LOG_VERBOSE=1 muestra todo el registro.\n";

#define TONE_RATE 48000
#define TONE_CHANNELS 2
#define REPORT_SECONDS 10

static volatile sig_atomic_t interrupted;

static void on_sigint(int sig)
{
	(void)sig;
	interrupted = 1;
}

/** What the audio session reported, set on iOS's thread and read once per second on the main one. */
static atomic_uint events_began;
static atomic_uint events_ended;
static atomic_uint events_route;
static atomic_uint events_reset;

static void on_session_event(CKAudioSessionEvent event, void *user)
{
	(void)user;
	switch(event)
	{
		case CK_AUDIO_SESSION_INTERRUPTION_BEGAN: atomic_fetch_add(&events_began, 1); break;
		case CK_AUDIO_SESSION_INTERRUPTION_ENDED: atomic_fetch_add(&events_ended, 1); break;
		case CK_AUDIO_SESSION_ROUTE_CHANGED: atomic_fetch_add(&events_route, 1); break;
		case CK_AUDIO_SESSION_MEDIA_RESET: atomic_fetch_add(&events_reset, 1); break;
	}
}

static void print_session(const char *when)
{
	CKAudioSessionInfo info;
	ck_audio_session_get_info(&info);
	printf("  sesión de audio%s: hardware a %.0f Hz, búfer de %.1f ms, latencia de salida %.1f ms, salida %s\n",
		when, info.sample_rate, info.io_buffer_s * 1000.0, info.output_latency_s * 1000.0, info.route);
}

typedef struct tone_run_t
{
	CKAudioOutput *output;
	long seconds;
	long elapsed;
	/** Over the whole run and over the current 10 s. */
	uint64_t callbacks;
	uint32_t late;
	uint32_t discontinuities;
	uint32_t max_gap_us;
	uint32_t stopped_seconds;
	uint32_t window_late;
	uint32_t window_max_gap_us;
	uint32_t window_min_queued_ms;
	uint32_t window_max_queued_ms;
	bool media_reset;
} ToneRun;

static void tone_tick(CFRunLoopTimerRef timer, void *info)
{
	(void)timer;
	ToneRun *run = info;
	@autoreleasepool
	{
		unsigned ended = atomic_exchange(&events_ended, 0);
		unsigned began = atomic_exchange(&events_began, 0);
		unsigned route = atomic_exchange(&events_route, 0);
		if(began)
			printf("  iOS interrumpió el audio (otra app, una alarma o Siri)\n");
		if(ended)
		{
			printf("  terminó la interrupción: la cola vuelve a sonar\n");
			ck_audio_output_start(run->output);
		}
		if(route)
			print_session(" cambió de salida");
		if(atomic_exchange(&events_reset, 0))
		{
			printf("  el servidor de audio de iOS se reinició: la cola ya no sirve\n");
			run->media_reset = true;
		}

		CKAudioOutputStats stats;
		ck_audio_output_take_stats(run->output, &stats);
		run->elapsed++;
		run->callbacks += stats.callbacks;
		run->late += stats.late;
		run->discontinuities += stats.discontinuities;
		if(!stats.running)
			run->stopped_seconds++;
		if(stats.max_gap_us > run->max_gap_us)
			run->max_gap_us = stats.max_gap_us;
		run->window_late += stats.late;
		if(stats.max_gap_us > run->window_max_gap_us)
			run->window_max_gap_us = stats.max_gap_us;
		if(stats.queued_ms < run->window_min_queued_ms)
			run->window_min_queued_ms = stats.queued_ms;
		if(stats.queued_ms > run->window_max_queued_ms)
			run->window_max_queued_ms = stats.queued_ms;

		if(run->elapsed % REPORT_SECONDS == 0 || run->elapsed >= run->seconds || interrupted || run->media_reset)
		{
			printf("  %3ld s: %llu búferes, hueco máximo %.1f ms, %u posibles cortes, en cola %u a %u ms%s\n",
				run->elapsed, (unsigned long long)run->callbacks, run->window_max_gap_us / 1000.0,
				run->window_late, run->window_min_queued_ms, run->window_max_queued_ms,
				stats.running ? "" : ", COLA DETENIDA");
			fflush(stdout);
			run->window_late = 0;
			run->window_max_gap_us = 0;
			run->window_min_queued_ms = UINT32_MAX;
			run->window_max_queued_ms = 0;
		}
		if(run->elapsed >= run->seconds || interrupted || run->media_reset)
			CFRunLoopStop(CFRunLoopGetMain());
	}
}

static int usage_error(const char *what)
{
	fprintf(stderr, "ckaudio: %s\n\n%s", what, usage);
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

static int cmd_tone(int argc, char **argv)
{
	long seconds = 120, hz = 440, dbfs = -12, buffer_ms = 10, buffers = 3, io_ms = 10;
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
		if(!strcmp(arg, "-s"))
		{
			if(!parse_number(value, 1, 3600, &seconds))
				return usage_error("-s pide segundos de 1 a 3600");
		}
		else if(!strcmp(arg, "-f"))
		{
			if(!parse_number(value, 20, 20000, &hz))
				return usage_error("-f pide hercios de 20 a 20000");
		}
		else if(!strcmp(arg, "-d"))
		{
			if(!parse_number(value, -60, 0, &dbfs))
				return usage_error("-d pide dB de -60 a 0");
		}
		else if(!strcmp(arg, "-b"))
		{
			if(!parse_number(value, 2, 100, &buffer_ms))
				return usage_error("-b pide milisegundos de 2 a 100");
		}
		else if(!strcmp(arg, "-n"))
		{
			if(!parse_number(value, 2, CK_AUDIO_OUTPUT_BUFFERS_MAX, &buffers))
				return usage_error("-n pide de 2 a 8 búferes");
		}
		else if(!strcmp(arg, "-i"))
		{
			if(!parse_number(value, 0, 100, &io_ms))
				return usage_error("-i pide milisegundos de 0 a 100");
		}
		else
			return usage_error("opción desconocida");
	}

	const char *verbose = getenv("CK_LOG_VERBOSE");
	ChiakiLog log;
	chiaki_log_init(&log, verbose && verbose[0] == '1' ? CHIAKI_LOG_ALL : CHIAKI_LOG_ERROR | CHIAKI_LOG_WARNING,
		chiaki_log_cb_print, NULL);

	if(!ck_audio_session_begin(&log, TONE_RATE, io_ms / 1000.0, on_session_event, NULL))
	{
		fprintf(stderr, "ckaudio: iOS no activó la sesión de audio\n");
		return 1;
	}
	print_session("");

	CKAudioTone tone;
	ck_audio_tone_init(&tone, TONE_RATE, TONE_CHANNELS, (double)hz, (double)dbfs);
	CKAudioOutputConfig config = { TONE_RATE, TONE_CHANNELS, (uint32_t)(TONE_RATE * buffer_ms / 1000), (uint32_t)buffers };
	CKAudioOutput *output = ck_audio_output_new(&log, &config, ck_audio_tone_fill, &tone);
	if(!output)
	{
		fprintf(stderr, "ckaudio: AudioQueue no creó la cola\n");
		ck_audio_session_end();
		return 1;
	}
	printf("  tono de %ld Hz a %ld dBFS por %ld s: cola de %ld búferes de %ld ms (%u cuadros) a %u Hz, estéreo\n",
		hz, dbfs, seconds, buffers, buffer_ms, config.buffer_frames, config.rate);
	if(!ck_audio_output_start(output))
	{
		fprintf(stderr, "ckaudio: AudioQueue no arrancó la cola\n");
		ck_audio_output_free(output);
		ck_audio_session_end();
		return 1;
	}
	fflush(stdout);

	ToneRun run;
	memset(&run, 0, sizeof(run));
	run.output = output;
	run.seconds = seconds;
	run.window_min_queued_ms = UINT32_MAX;
	signal(SIGINT, on_sigint);
	CFRunLoopTimerContext context = { 0, &run, NULL, NULL, NULL };
	CFRunLoopTimerRef timer = CFRunLoopTimerCreate(NULL, CFAbsoluteTimeGetCurrent() + 1.0, 1.0, 0, 0, tone_tick, &context);
	CFRunLoopAddTimer(CFRunLoopGetMain(), timer, kCFRunLoopDefaultMode);
	CFRunLoopRun();
	CFRunLoopTimerInvalidate(timer);
	CFRelease(timer);

	ck_audio_output_free(output);
	ck_audio_session_end();

	bool ok = run.late == 0 && run.stopped_seconds == 0 && !run.media_reset && run.elapsed >= seconds;
	printf("\n  sonó %ld s: %llu búferes, hueco máximo %.1f ms, %u posibles cortes, %u saltos de la línea de tiempo, "
		"%u s con la cola detenida\n",
		run.elapsed, (unsigned long long)run.callbacks, run.max_gap_us / 1000.0, run.late, run.discontinuities,
		run.stopped_seconds);
	if(ok)
		printf("RESULTADO: sin cortes medidos. Falta tu oído: ¿sonó parejo, sin chasquidos ni huecos?\n");
	else if(run.elapsed < seconds)
		printf("RESULTADO: se paró antes de tiempo\n");
	else
		printf("RESULTADO: la cola pudo quedarse sin audio; prueba otra vez con -b 20\n");
	return ok ? 0 : 1;
}

int main(int argc, char **argv)
{
	@autoreleasepool
	{
		if(argc >= 2 && !strcmp(argv[1], "tono"))
			return cmd_tone(argc - 1, argv + 1);
		if(argc >= 2 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")))
		{
			fputs(usage, stdout);
			return 0;
		}
		return usage_error(argc < 2 ? "falta qué hacer" : "no conozco esa orden");
	}
}
