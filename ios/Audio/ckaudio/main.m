// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// ckaudio (A4.2, A4.3): tries the iPad's audio from a shell, without the app.
// "ckaudio tono" plays a test tone through the same AudioQueue output and
// audio session the player uses, and every 10 s says whether the queue ran
// dry. P2-5 is two minutes of it without a cut. "ckaudio opus" plays a
// cksession -g recording through the player itself (CKAudioSink.h), fed as
// the core would, with jitter and loss if asked. See ../README.md.

#import <Foundation/Foundation.h>

#include "Audio/ck_audio_output.h"
#include "Audio/ck_audio_player.h"
#include "Audio/ck_audio_session.h"
#include "Audio/ck_audio_tone.h"
#include "Audio/ck_ogg_opus.h"

#include <opus/opus.h>

#include <mach/mach.h>
#include <mach/mach_time.h>
#include <mach/thread_policy.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char usage[] =
	"Uso: ckaudio tono [-o rio|aq] [-s SEGUNDOS] [-f HZ] [-d DBFS] [-b MS] [-n BÚFERES] [-i MS]\n"
	"     ckaudio opus ARCHIVO.opus [-o rio|aq] [-s SEGUNDOS] [-j MS] [-p POR_CIENTO] [-b MS] [-n BÚFERES]\n"
	"\n"
	"  tono: toca un tono de prueba por la bocina del iPad, como lo hará el\n"
	"  reproductor de audio, y cada 10 s dice si la salida se quedó sin audio.\n"
	"\n"
	"  -o rio|aq    la salida: RemoteIO, en el hilo de tiempo real del audio (rio,\n"
	"               la del reproductor), o AudioQueue (aq)\n"
	"  -s SEGUNDOS  cuánto suena (120)\n"
	"  -f HZ        la frecuencia del tono (440)\n"
	"  -d DBFS      el volumen, en dB bajo el máximo (-12; 0 es el máximo)\n"
	"  -b MS        con aq, lo que dura cada búfer de la cola (10)\n"
	"  -n BÚFERES   con aq, cuántos búferes tiene la cola (3)\n"
	"  -i MS        el búfer del hardware que se le pide a iOS (10; 0 deja el de iOS)\n"
	"\n"
	"  opus: toca una grabación de cksession -g por el reproductor de audio de la\n"
	"  app, como si llegara de la PS5: un cuadro cada 10 ms desde otro hilo, como\n"
	"  el del core. Cada 10 s dice el nivel del búfer, los cuadros ocultados y si\n"
	"  se quedó sin audio.\n"
	"\n"
	"  -s SEGUNDOS    cuánto suena como mucho (hasta que se acaba la grabación)\n"
	"  -j MS          cada cuadro llega tarde al azar de 0 a MS milisegundos (0)\n"
	"  -p POR_CIENTO  cuadros que se pierden al azar, como en la red (0)\n"
	"  -o rio|aq      la salida, como en tono (rio)\n"
	"  -b MS          con aq, lo que dura cada búfer de la cola, de 5 a 25 (20)\n"
	"  -n BÚFERES     con aq, cuántos búferes tiene la cola (4)\n"
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
	uint32_t window_min_frames;
	uint32_t window_max_frames;
	bool remoteio;
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
			printf("  terminó la interrupción: la salida vuelve a sonar\n");
			ck_audio_output_start(run->output);
		}
		if(route)
			print_session(" cambió de salida");
		if(atomic_exchange(&events_reset, 0))
		{
			printf("  el servidor de audio de iOS se reinició: la salida ya no sirve\n");
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
		if(stats.callbacks && stats.min_frames < run->window_min_frames)
			run->window_min_frames = stats.min_frames;
		if(stats.max_frames > run->window_max_frames)
			run->window_max_frames = stats.max_frames;

		if(run->elapsed % REPORT_SECONDS == 0 || run->elapsed >= run->seconds || interrupted || run->media_reset)
		{
			printf("  %3ld s: %llu búferes, hueco máximo %.1f ms, %u posibles cortes", run->elapsed,
				(unsigned long long)run->callbacks, run->window_max_gap_us / 1000.0, run->window_late);
			if(run->remoteio)
				printf(", de %u a %u cuadros por llamada%s\n",
					run->window_min_frames == UINT32_MAX ? 0 : run->window_min_frames, run->window_max_frames,
					stats.running ? "" : ", SALIDA DETENIDA");
			else
				printf(", en cola %u a %u ms%s\n", run->window_min_queued_ms, run->window_max_queued_ms,
					stats.running ? "" : ", COLA DETENIDA");
			fflush(stdout);
			run->window_late = 0;
			run->window_max_gap_us = 0;
			run->window_min_queued_ms = UINT32_MAX;
			run->window_max_queued_ms = 0;
			run->window_min_frames = UINT32_MAX;
			run->window_max_frames = 0;
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

/** -o: rio or aq. */
static bool parse_kind(const char *text, CKAudioOutputKind *kind)
{
	if(!strcmp(text, "rio"))
		*kind = CK_AUDIO_OUTPUT_REMOTEIO;
	else if(!strcmp(text, "aq"))
		*kind = CK_AUDIO_OUTPUT_AUDIOQUEUE;
	else
		return false;
	return true;
}

static int cmd_tone(int argc, char **argv)
{
	long seconds = 120, hz = 440, dbfs = -12, buffer_ms = 10, buffers = 3, io_ms = 10;
	CKAudioOutputKind kind = CK_AUDIO_OUTPUT_REMOTEIO;
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
		if(!strcmp(arg, "-o"))
		{
			if(!parse_kind(value, &kind))
				return usage_error("-o pide rio o aq");
		}
		else if(!strcmp(arg, "-s"))
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
	CKAudioOutputConfig config = { kind, TONE_RATE, TONE_CHANNELS, (uint32_t)(TONE_RATE * buffer_ms / 1000),
		(uint32_t)buffers };
	CKAudioOutput *output = ck_audio_output_new(&log, &config, ck_audio_tone_fill, &tone);
	if(!output)
	{
		fprintf(stderr, "ckaudio: no se creó la salida de audio\n");
		ck_audio_session_end();
		return 1;
	}
	if(kind == CK_AUDIO_OUTPUT_REMOTEIO)
		printf("  tono de %ld Hz a %ld dBFS por %ld s: salida RemoteIO a %u Hz, estéreo\n", hz, dbfs, seconds, config.rate);
	else
		printf("  tono de %ld Hz a %ld dBFS por %ld s: AudioQueue de %ld búferes de %ld ms (%u cuadros) a %u Hz, estéreo\n",
			hz, dbfs, seconds, buffers, buffer_ms, config.buffer_frames, config.rate);
	if(!ck_audio_output_start(output))
	{
		fprintf(stderr, "ckaudio: la salida de audio no arrancó\n");
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
	run.window_min_frames = UINT32_MAX;
	run.remoteio = kind == CK_AUDIO_OUTPUT_REMOTEIO;
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
		"%u s con la salida detenida\n",
		run.elapsed, (unsigned long long)run.callbacks, run.max_gap_us / 1000.0, run.late, run.discontinuities,
		run.stopped_seconds);
	if(ok)
		printf("RESULTADO: sin cortes medidos. Falta tu oído: ¿sonó parejo, sin chasquidos ni huecos?\n");
	else if(run.elapsed < seconds)
		printf("RESULTADO: se paró antes de tiempo\n");
	else
		printf("RESULTADO: la salida pudo quedarse sin audio\n");
	return ok ? 0 : 1;
}

/** What the feeder thread hands the player: the recording as the core would hand over a stream. */
typedef struct opus_feed_t
{
	CKOggOpus ogg;
	ChiakiAudioSink sink;
	/** Samples per frame at 48 kHz, from the first packet. */
	uint32_t frame;
	double jitter_ms;
	double loss;
	uint32_t seed;
	bool realtime;
	atomic_bool stop;
	atomic_bool done;
	atomic_uint frames;
	atomic_uint lost;
} OpusFeed;

static double feed_random(OpusFeed *feed)
{
	feed->seed = feed->seed * 1664525u + 1013904223u;
	return (double)(feed->seed >> 8) / (double)(1u << 24);
}

/**
 * The feeder sleeps until each frame is due. A process started over SSH has
 * its timers held back by up to 100 ms (docs/ipad/dualsense-directo.md, T9),
 * which would hand the frames over in bursts no network makes; a
 * time-constraint thread is exempt. The core's own thread waits on its
 * socket, not on a timer, and needs none of this.
 */
static bool make_realtime(void)
{
	mach_timebase_info_data_t timebase;
	mach_timebase_info(&timebase);
	double ticks_per_ms = 1e6 * timebase.denom / timebase.numer;
	thread_time_constraint_policy_data_t policy;
	policy.period = (uint32_t)(10.0 * ticks_per_ms);
	policy.computation = (uint32_t)(2.0 * ticks_per_ms);
	policy.constraint = (uint32_t)(5.0 * ticks_per_ms);
	policy.preemptible = TRUE;
	return thread_policy_set(pthread_mach_thread_np(pthread_self()), THREAD_TIME_CONSTRAINT_POLICY,
		(thread_policy_t)&policy, THREAD_TIME_CONSTRAINT_POLICY_COUNT) == KERN_SUCCESS;
}

static void *feed_thread(void *user)
{
	OpusFeed *feed = user;
	feed->realtime = make_realtime();
	mach_timebase_info_data_t timebase;
	mach_timebase_info(&timebase);
	double ticks_per_ms = 1e6 * timebase.denom / timebase.numer;
	double period_ms = feed->frame * 1000.0 / 48000.0;
	uint64_t start = mach_absolute_time() + (uint64_t)(20.0 * ticks_per_ms);
	uint64_t due_prev = 0;
	uint32_t index = 0, missing = 0;
	const uint8_t *packet;
	size_t size;
	while(!atomic_load(&feed->stop) && ck_ogg_opus_next(&feed->ogg, &packet, &size) == 1)
	{
		// Its slot on the PS5's clock, late by up to jitter_ms, never before the one before it.
		uint64_t due = start + (uint64_t)((index * period_ms + feed_random(feed) * feed->jitter_ms) * ticks_per_ms);
		if(due < due_prev)
			due = due_prev;
		due_prev = due;
		index++;
		if(ck_ogg_opus_packet_is_lost(size) || feed_random(feed) * 100.0 < feed->loss)
		{
			missing++;
			continue;
		}
		mach_wait_until(due);
		// As the core does: one lost frame per missing one, five at most, then the frame.
		for(uint32_t m = 0; m < missing && m < CHIAKI_AUDIO_RECEIVER_CONCEAL_MAX_FRAMES; m++)
		{
			feed->sink.frame_cb(NULL, 0, feed->sink.user);
			atomic_fetch_add(&feed->lost, 1);
		}
		missing = 0;
		feed->sink.frame_cb((uint8_t *)packet, size, feed->sink.user);
		atomic_fetch_add(&feed->frames, 1);
	}
	atomic_store(&feed->done, true);
	return NULL;
}

typedef struct opus_run_t
{
	CKAudioPlayer *player;
	OpusFeed *feed;
	long seconds;
	long elapsed;
	long done_at;
	uint32_t concealed;
	uint32_t underruns;
	uint32_t dropped;
	uint32_t made_up;
	uint32_t late;
	uint32_t resets;
	uint32_t level_min_ms;
	uint32_t level_max_ms;
	uint64_t level_sum;
	uint32_t level_seconds;
	uint32_t window_min_ms;
	uint32_t window_max_ms;
	uint32_t window_concealed;
	uint32_t window_underruns;
	uint32_t window_late;
	uint32_t window_max_gap_us;
} OpusRun;

static void opus_tick(CFRunLoopTimerRef timer, void *info)
{
	(void)timer;
	OpusRun *run = info;
	CKAudioPlayerDetails d;
	ck_audio_player_take_details(run->player, &d);
	run->elapsed++;
	uint32_t concealed = d.lost + d.refused;
	run->concealed += concealed;
	run->underruns += d.buffer.underruns;
	run->dropped += d.buffer.dropped;
	run->made_up += d.buffer.requested;
	run->late += d.output.late;
	run->resets += d.output_resets;
	run->window_concealed += concealed;
	run->window_underruns += d.buffer.underruns;
	run->window_late += d.output.late;
	if(d.output.max_gap_us > run->window_max_gap_us)
		run->window_max_gap_us = d.output.max_gap_us;
	// The level once it plays: the first second is the wait for the buffer.
	if(d.buffer.playing && run->elapsed > 1)
	{
		run->level_sum += d.buffer.level_ms;
		run->level_seconds++;
		if(d.buffer.level_min_ms < run->window_min_ms)
			run->window_min_ms = d.buffer.level_min_ms;
		if(d.buffer.level_max_ms > run->window_max_ms)
			run->window_max_ms = d.buffer.level_max_ms;
		if(d.buffer.level_ms < run->level_min_ms)
			run->level_min_ms = d.buffer.level_ms;
		if(d.buffer.level_ms > run->level_max_ms)
			run->level_max_ms = d.buffer.level_ms;
	}
	if(atomic_load(&run->feed->done) && !run->done_at)
		run->done_at = run->elapsed;
	// After the last frame, a second for what is buffered to play.
	bool end = run->elapsed >= run->seconds || interrupted || (run->done_at && run->elapsed > run->done_at);
	if(run->elapsed % REPORT_SECONDS == 0 || end)
	{
		printf("  %3ld s: búfer de %u a %u ms, %u ocultados, %u veces sin audio, hueco máximo de la salida %.1f ms, "
			"%u posibles cortes de la salida\n",
			run->elapsed, run->window_min_ms == UINT32_MAX ? 0 : run->window_min_ms, run->window_max_ms,
			run->window_concealed, run->window_underruns, run->window_max_gap_us / 1000.0, run->window_late);
		fflush(stdout);
		run->window_min_ms = UINT32_MAX;
		run->window_max_ms = 0;
		run->window_late = 0;
		run->window_concealed = 0;
		run->window_underruns = 0;
		run->window_max_gap_us = 0;
	}
	if(end)
		CFRunLoopStop(CFRunLoopGetMain());
}

static int cmd_opus(int argc, char **argv)
{
	if(argc < 2 || argv[1][0] == '-')
		return usage_error("opus pide el archivo de la grabación");
	const char *path = argv[1];
	long seconds = 3600, jitter_ms = 0, loss = 0;
	long buffer_ms = CK_AUDIO_PLAYER_OUTPUT_BUFFER_MS, buffers = CK_AUDIO_PLAYER_OUTPUT_BUFFERS;
	CKAudioOutputKind kind = CK_AUDIO_PLAYER_OUTPUT;
	for(int i = 2; i < argc; i++)
	{
		const char *arg = argv[i];
		if(i + 1 >= argc)
			return usage_error("a una opción le falta su valor");
		const char *value = argv[++i];
		if(!strcmp(arg, "-o"))
		{
			if(!parse_kind(value, &kind))
				return usage_error("-o pide rio o aq");
		}
		else if(!strcmp(arg, "-s"))
		{
			if(!parse_number(value, 1, 3600, &seconds))
				return usage_error("-s pide segundos de 1 a 3600");
		}
		else if(!strcmp(arg, "-j"))
		{
			if(!parse_number(value, 0, 200, &jitter_ms))
				return usage_error("-j pide milisegundos de 0 a 200");
		}
		else if(!strcmp(arg, "-p"))
		{
			if(!parse_number(value, 0, 50, &loss))
				return usage_error("-p pide un por ciento de 0 a 50");
		}
		else if(!strcmp(arg, "-b"))
		{
			if(!parse_number(value, CK_AUDIO_PLAYER_OUTPUT_BUFFER_MS_MIN, CK_AUDIO_PLAYER_OUTPUT_BUFFER_MS_MAX, &buffer_ms))
				return usage_error("-b pide milisegundos de 5 a 25");
		}
		else if(!strcmp(arg, "-n"))
		{
			if(!parse_number(value, 2, CK_AUDIO_OUTPUT_BUFFERS_MAX, &buffers))
				return usage_error("-n pide de 2 a 8 búferes");
		}
		else
			return usage_error("opción desconocida");
	}

	NSData *data = [NSData dataWithContentsOfFile:[NSString stringWithUTF8String:path]];
	if(!data)
	{
		fprintf(stderr, "ckaudio: no pude leer %s\n", path);
		return 1;
	}
	static OpusFeed feed;
	memset(&feed, 0, sizeof(feed));
	const char *bad = ck_ogg_opus_open(&feed.ogg, data.bytes, data.length);
	if(bad)
	{
		fprintf(stderr, "ckaudio: %s %s\n", path, bad);
		return 1;
	}
	// The frame length, from the first real packet, without moving the reader.
	static CKOggOpus peek;
	peek = feed.ogg;
	const uint8_t *packet;
	size_t size;
	int frame = 0;
	while(frame <= 0 && ck_ogg_opus_next(&peek, &packet, &size) == 1)
		if(!ck_ogg_opus_packet_is_lost(size))
			frame = opus_packet_get_nb_samples(packet, (opus_int32)size, 48000);
	if(frame <= 0)
	{
		fprintf(stderr, "ckaudio: %s no trae audio que se pueda tocar\n", path);
		return 1;
	}

	const char *verbose = getenv("CK_LOG_VERBOSE");
	ChiakiLog log;
	chiaki_log_init(&log, verbose && verbose[0] == '1' ? CHIAKI_LOG_ALL : CHIAKI_LOG_ERROR | CHIAKI_LOG_WARNING,
		chiaki_log_cb_print, NULL);
	CKAudioPlayer *player = ck_audio_player_new_output(&log, kind, (uint32_t)buffer_ms, (uint32_t)buffers);
	if(!player)
	{
		fprintf(stderr, "ckaudio: el reproductor no arrancó\n");
		return 1;
	}
	print_session("");
	ck_audio_player_get_sink(player, &feed.sink);
	ChiakiAudioHeader header = { feed.ogg.channels, 16, 48000, (uint32_t)frame, 0 };
	feed.sink.header_cb(&header, feed.sink.user);
	feed.frame = (uint32_t)frame;
	feed.jitter_ms = (double)jitter_ms;
	feed.loss = (double)loss;
	feed.seed = 1;
	atomic_init(&feed.stop, false);
	atomic_init(&feed.done, false);
	atomic_init(&feed.frames, 0);
	atomic_init(&feed.lost, 0);
	char out[64];
	if(kind == CK_AUDIO_OUTPUT_REMOTEIO)
		snprintf(out, sizeof(out), "salida RemoteIO");
	else
		snprintf(out, sizeof(out), "AudioQueue de %ld búferes de %ld ms", buffers, buffer_ms);
	printf("  %s: %u canales, cuadros de %.1f ms; búfer de %d ms (máximo %d), %s, llegan tarde de 0 a %ld ms, "
		"se pierde el %ld %%\n",
		path, feed.ogg.channels, frame / 48.0, CK_AUDIO_PLAYER_TARGET_MS, CK_AUDIO_PLAYER_MAX_MS, out, jitter_ms, loss);

	pthread_t thread;
	if(pthread_create(&thread, NULL, feed_thread, &feed) != 0)
	{
		fprintf(stderr, "ckaudio: no pude crear el hilo que entrega los cuadros\n");
		ck_audio_player_free(player);
		return 1;
	}

	OpusRun run;
	memset(&run, 0, sizeof(run));
	run.player = player;
	run.feed = &feed;
	run.seconds = seconds;
	run.level_min_ms = UINT32_MAX;
	run.window_min_ms = UINT32_MAX;
	signal(SIGINT, on_sigint);
	CFRunLoopTimerContext context = { 0, &run, NULL, NULL, NULL };
	CFRunLoopTimerRef timer = CFRunLoopTimerCreate(NULL, CFAbsoluteTimeGetCurrent() + 1.0, 1.0, 0, 0, opus_tick, &context);
	CFRunLoopAddTimer(CFRunLoopGetMain(), timer, kCFRunLoopDefaultMode);
	CFRunLoopRun();
	CFRunLoopTimerInvalidate(timer);
	CFRelease(timer);

	atomic_store(&feed.stop, true);
	pthread_join(thread, NULL);
	ck_audio_player_free(player);

	uint32_t average = run.level_seconds ? (uint32_t)(run.level_sum / run.level_seconds) : 0;
	printf("\n  sonó %ld s: %u cuadros, %u perdidos y ocultados, %u veces sin audio, %u descartados y %u inventados "
		"por el reloj, %u posibles cortes de la salida, búfer de %u a %u ms (promedio %u); hilo de entrada en tiempo real: %s\n",
		run.elapsed, atomic_load(&feed.frames), run.concealed, run.underruns, run.dropped, run.made_up, run.late,
		run.level_min_ms == UINT32_MAX ? 0 : run.level_min_ms, run.level_max_ms, average, feed.realtime ? "sí" : "no");
	bool ok = run.underruns == 0 && run.late == 0 && run.resets == 0;
	if(ok)
		printf("RESULTADO: sin cortes medidos. Falta tu oído: ¿se oyó limpio, sin chasquidos ni huecos?\n");
	else
		printf("RESULTADO: %u veces sin audio y %u posibles cortes de la salida\n", run.underruns, run.late);
	return ok ? 0 : 1;
}

int main(int argc, char **argv)
{
	@autoreleasepool
	{
		if(argc >= 2 && !strcmp(argv[1], "tono"))
			return cmd_tone(argc - 1, argv + 1);
		if(argc >= 2 && !strcmp(argv[1], "opus"))
			return cmd_opus(argc - 1, argv + 1);
		if(argc >= 2 && (!strcmp(argv[1], "-h") || !strcmp(argv[1], "--help")))
		{
			fputs(usage, stdout);
			return 0;
		}
		return usage_error(argc < 2 ? "falta qué hacer" : "no conozco esa orden");
	}
}
