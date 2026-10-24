// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// open, fstat and mmap are POSIX, which glibc hides under -std=c11.
#if !defined(__APPLE__) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif

#include "ck_ds5_source.h"

#include <fcntl.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

void ck_ds5_source_init(CKDS5Source *source, const char *path)
{
	memset(source, 0, sizeof(*source));
	source->path = path;
	chiaki_controller_state_set_idle(&source->state);
}

static void unmap(CKDS5Source *source)
{
	if(source->shared)
		munmap((void *)source->shared, sizeof(CKDS5Shared));
	source->shared = NULL;
}

void ck_ds5_source_fini(CKDS5Source *source)
{
	unmap(source);
	source->connected = false;
	chiaki_controller_state_set_idle(&source->state);
}

// Maps the file when there is one and it is not the one already mapped. A
// file that vanished stays mapped: whoever still has it open may write to it.
static void check_file(CKDS5Source *source)
{
	struct stat st;
	if(stat(source->path, &st) != 0 || st.st_size < (off_t)sizeof(CKDS5Shared))
		return;
	if(source->shared && source->dev == (uint64_t)st.st_dev && source->ino == (uint64_t)st.st_ino)
		return;
	int fd = open(source->path, O_RDONLY);
	if(fd < 0)
		return;
	void *map = MAP_FAILED;
	if(fstat(fd, &st) == 0 && st.st_size >= (off_t)sizeof(CKDS5Shared))
		map = mmap(NULL, sizeof(CKDS5Shared), PROT_READ, MAP_SHARED, fd, 0);
	close(fd);
	if(map == MAP_FAILED)
		return;

	// A new file means a new BTServer: whatever the old one said is over.
	unmap(source);
	source->shared = map;
	source->dev = (uint64_t)st.st_dev;
	source->ino = (uint64_t)st.st_ino;
	source->seq = 0;
	source->connected = false;
	chiaki_controller_state_set_idle(&source->state);
	source->maps++;
}

bool ck_ds5_source_poll(void *user, ChiakiControllerState *state)
{
	CKDS5Source *source = user;
	if(source->polls_to_check == 0)
	{
		source->polls_to_check = CK_DS5_SOURCE_CHECK_POLLS;
		check_file(source);
	}
	source->polls_to_check--;
	if(!source->shared)
		return false;

	size_t len;
	if(ck_ds5_shared_read(source->shared, &source->seq, source->report, &len))
	{
		if(len == 0)
		{
			source->connected = false;
			chiaki_controller_state_set_idle(&source->state);
		}
		else
		{
			// An unknown report or a bad CRC leaves the state as it was.
			CKDS5ReportKind kind = ck_ds5_parse_input_report(source->report, len, &source->state);
			source->kind = kind;
			source->kinds[kind]++;
			if(kind != CK_DS5_REPORT_UNKNOWN && kind != CK_DS5_REPORT_BAD_CRC)
				source->connected = true;
		}
	}
	if(!source->connected)
		return false;
	*state = source->state;
	return true;
}

CKInputSource ck_ds5_source_input(CKDS5Source *source)
{
	CKInputSource input = { CK_DS5_SOURCE_NAME, source, ck_ds5_source_poll };
	return input;
}
