// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "ck_ui_stream.h"

#include <math.h>
#include <stdio.h>

bool ck_ui_session_should_retry(ChiakiQuitReason reason, bool streamed, unsigned retries, double since_last_session)
{
	if(streamed || retries >= CK_UI_SESSION_RETRY_MAX)
		return false;
	if(reason == CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_IN_USE)
		return true;
	// Without a session just before, a failed connection is a real one: say so instead of waiting 30 s.
	if(reason == CHIAKI_QUIT_REASON_CTRL_CONNECT_FAILED)
		return retries > 0 || (since_last_session >= 0 && since_last_session < CK_UI_SESSION_BUSY_SECONDS);
	return false;
}

const char *ck_ui_quit_reason_string(ChiakiQuitReason reason)
{
	switch(reason)
	{
		case CHIAKI_QUIT_REASON_NONE:
		case CHIAKI_QUIT_REASON_STOPPED:
			return "Saliste de la sesión.";
		case CHIAKI_QUIT_REASON_SESSION_REQUEST_CONNECTION_REFUSED:
		case CHIAKI_QUIT_REASON_CTRL_CONNECTION_REFUSED:
			return "La consola rechazó la conexión. Revisa que esté encendida y con Remote Play activado.";
		case CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_IN_USE:
			return "La consola tiene otra sesión de Remote Play abierta, quizá en otro dispositivo. Ciérrala y vuelve a "
				"intentarlo.";
		case CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_CRASH:
			return "Remote Play falló en la consola. Reiníciala y vuelve a intentarlo.";
		case CHIAKI_QUIT_REASON_SESSION_REQUEST_RP_VERSION_MISMATCH:
			return "La consola pide una versión de Remote Play que esta app no tiene.";
		case CHIAKI_QUIT_REASON_SESSION_REQUEST_UNKNOWN:
		case CHIAKI_QUIT_REASON_CTRL_UNKNOWN:
		case CHIAKI_QUIT_REASON_CTRL_CONNECT_FAILED:
			return "No pude conectar con la consola. Revisa que el iPad y la consola estén en la misma red y vuelve a "
				"intentarlo.";
		case CHIAKI_QUIT_REASON_STREAM_CONNECTION_REMOTE_DISCONNECTED:
			return "La consola cerró la sesión.";
		case CHIAKI_QUIT_REASON_STREAM_CONNECTION_REMOTE_SHUTDOWN:
			return "La consola se apagó o entró en reposo.";
		case CHIAKI_QUIT_REASON_PSN_REGIST_FAILED:
			return "La consola ya no reconoce el registro de este iPad: hay que registrarlo otra vez.";
		case CHIAKI_QUIT_REASON_STREAM_CONNECTION_TRANSPORT_FAILED:
			return "La red del iPad no pudo con el video. Baja la resolución o el bitrate en Ajustes.";
		default:
			return "Se perdió la conexión con la consola.";
	}
}

/** "4,2": one decimal, with a decimal comma. */
static void format_decimal(float value, char *out, size_t size)
{
	if(!isfinite(value) || value < 0)
		value = 0;
	// Nothing real gets near: it only keeps the conversion defined.
	if(value > 100000)
		value = 100000;
	unsigned tenths = (unsigned)(value * 10 + 0.5f);
	snprintf(out, size, "%u,%u", tenths / 10, tenths % 10);
}

void ck_ui_metrics_line(const CKMetrics *metrics, char *out, size_t size)
{
	if(!size)
		return;
	char fps[16], decode[16], mbps[16];
	format_decimal(metrics->fps_decoded, fps, sizeof(fps));
	format_decimal(metrics->decode_ms_avg, decode, sizeof(decode));
	format_decimal(metrics->bitrate_kbps / 1000.0f, mbps, sizeof(mbps));
	snprintf(out, size, "%s fps · decodifica %s ms · %s Mbps · %u cuadros perdidos · audio %u ocultados, %u ms · %u MB "
		"de RAM", fps, decode, mbps, (unsigned)metrics->frames_lost, (unsigned)metrics->audio_frames_concealed,
		(unsigned)metrics->audio_buffered_ms, (unsigned)((metrics->resident_kb + 512) / 1024));
}
