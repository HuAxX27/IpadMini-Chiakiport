// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#include "ck_ui_regist.h"

#include <stdio.h>
#include <string.h>

CKUIRegistSave ck_ui_regist_save(const char *path, const ChiakiRegisteredHost *host, const char *last_host,
	CKRegistry *registry, ChiakiErrorCode *err)
{
	CKRegistry fresh;
	ChiakiErrorCode e = ck_registry_load(&fresh, path);
	if(e != CHIAKI_ERR_SUCCESS)
	{
		if(err)
			*err = e;
		return CK_UI_REGIST_UNREADABLE;
	}
	if(ck_registry_put(&fresh, host, last_host) != CHIAKI_ERR_SUCCESS)
		return CK_UI_REGIST_FULL;
	e = ck_registry_save(&fresh, path);
	if(e != CHIAKI_ERR_SUCCESS)
	{
		if(err)
			*err = e;
		return CK_UI_REGIST_UNWRITABLE;
	}
	*registry = fresh;
	return CK_UI_REGIST_SAVED;
}

void ck_ui_regist_message(const CKRegistOutcome *outcome, char *out, size_t size)
{
	if(!size)
		return;
	const char *text = ck_regist_result_string(outcome->result);
	int len;
	if(outcome->http_code && outcome->reason)
		len = snprintf(out, size, "%s (HTTP %d, razón 0x%08x)", text, outcome->http_code, (unsigned)outcome->reason);
	else if(outcome->http_code)
		len = snprintf(out, size, "%s (HTTP %d)", text, outcome->http_code);
	else if(outcome->reason)
		len = snprintf(out, size, "%s (razón 0x%08x)", text, (unsigned)outcome->reason);
	else
		len = snprintf(out, size, "%s", text);
	if(len < 0)
	{
		out[0] = '\0';
		return;
	}
	if((size_t)len < size)
		return;
	// Cut: drop a character snprintf left half written.
	size_t end = size - 1;
	size_t start = end;
	while(start && ((unsigned char)out[start - 1] & 0xc0) == 0x80)
		start--;
	if(start)
	{
		unsigned char lead = (unsigned char)out[start - 1];
		size_t need = lead >= 0xf0 ? 4 : lead >= 0xe0 ? 3 : lead >= 0xc0 ? 2 : 1;
		if(end - (start - 1) < need)
			end = start - 1;
	}
	out[end] = '\0';
}
