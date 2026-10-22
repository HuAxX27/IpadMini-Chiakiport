// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

// Not from chiaki-ng: the ECDH key exchange the way every session runs it (streamconnection.c),
// with a fresh random key on each side. gkcrypt/ecdh checks fixed keys against known values.

#include <munit.h>

#include <chiaki/ecdh.h>
#include <chiaki/session.h> // CHIAKI_HANDSHAKE_KEY_SIZE

#include <string.h>

static const uint8_t handshake_key[CHIAKI_HANDSHAKE_KEY_SIZE] = { 0xfc, 0x5d, 0x4b, 0xa0, 0x3a, 0x35, 0x3a, 0xbb, 0x6a, 0x7f, 0xac, 0x79, 0x1b, 0x17, 0xbb, 0x34 };

typedef struct side_t
{
	ChiakiECDH ecdh;
	uint8_t pub_key[128];
	size_t pub_key_size;
	uint8_t sig[32];
	size_t sig_size;
} Side;

static void side_init(Side *side)
{
	munit_assert_int(chiaki_ecdh_init(&side->ecdh), ==, CHIAKI_ERR_SUCCESS);
	side->pub_key_size = sizeof(side->pub_key);
	side->sig_size = sizeof(side->sig);
	munit_assert_int(chiaki_ecdh_get_local_pub_key(&side->ecdh, side->pub_key, &side->pub_key_size, handshake_key, side->sig, &side->sig_size), ==, CHIAKI_ERR_SUCCESS);
}

static MunitResult test_round_trip(const MunitParameter params[], void *user)
{
	Side a, b;
	side_init(&a);
	side_init(&b);

	// secp256k1 point, uncompressed: 0x04, then X and Y. The signature is an HMAC-SHA256.
	munit_assert_size(a.pub_key_size, ==, 65);
	munit_assert_uint8(a.pub_key[0], ==, 0x04);
	munit_assert_size(a.sig_size, ==, 32);
	munit_assert_memory_not_equal(a.pub_key_size, a.pub_key, b.pub_key);

	uint8_t secret_a[CHIAKI_ECDH_SECRET_SIZE];
	uint8_t secret_b[CHIAKI_ECDH_SECRET_SIZE];
	munit_assert_int(chiaki_ecdh_derive_secret(&a.ecdh, secret_a, b.pub_key, b.pub_key_size, handshake_key, b.sig, b.sig_size), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_int(chiaki_ecdh_derive_secret(&b.ecdh, secret_b, a.pub_key, a.pub_key_size, handshake_key, a.sig, a.sig_size), ==, CHIAKI_ERR_SUCCESS);
	munit_assert_memory_equal(sizeof(secret_a), secret_a, secret_b);

	chiaki_ecdh_fini(&a.ecdh);
	chiaki_ecdh_fini(&b.ecdh);
	return MUNIT_OK;
}

static MunitResult test_bad_remote_key(const MunitParameter params[], void *user)
{
	Side a, b;
	side_init(&a);
	side_init(&b);

	// Flip one bit of Y: the point is no longer on the curve.
	b.pub_key[b.pub_key_size - 1] ^= 1;
	uint8_t secret[CHIAKI_ECDH_SECRET_SIZE];
	munit_assert_int(chiaki_ecdh_derive_secret(&a.ecdh, secret, b.pub_key, b.pub_key_size, handshake_key, b.sig, b.sig_size), !=, CHIAKI_ERR_SUCCESS);

	chiaki_ecdh_fini(&a.ecdh);
	chiaki_ecdh_fini(&b.ecdh);
	return MUNIT_OK;
}

MunitTest tests_ecdh[] = {
	{
		"/round_trip",
		test_round_trip,
		NULL,
		NULL,
		MUNIT_TEST_OPTION_NONE,
		NULL
	},
	{
		"/bad_remote_key",
		test_bad_remote_key,
		NULL,
		NULL,
		MUNIT_TEST_OPTION_NONE,
		NULL
	},
	{ NULL, NULL, NULL, NULL, MUNIT_TEST_OPTION_NONE, NULL }
};
