// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Smoke test for the iOS 9 toolchain. The memset, memcpy and strcmp calls take
// their length from argc so the compiler cannot inline them: they must resolve
// against the SDK's libSystem, which is what fix-sdk93.sh repairs.

#include <pthread.h>
#include <stdio.h>
#include <string.h>

static void *worker(void *arg)
{
	return arg;
}

int main(int argc, char *argv[])
{
	char a[64], b[64];
	size_t n = (size_t)argc < sizeof(a) ? (size_t)argc : sizeof(a);
	memset(a, 'k', n);
	memcpy(b, a, n);

	pthread_t thread;
	void *ret = NULL;
	if (pthread_create(&thread, NULL, worker, argv[0]) != 0 || pthread_join(thread, &ret) != 0)
		return 1;

	printf("ck-smoke: %s, armv7, iOS 9\n", strcmp(argv[0], (const char *)ret) == 0 ? "ok" : "mal");
	return memcmp(a, b, n);
}
