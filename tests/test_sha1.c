/*
 * Unit tests for the bundled SHA-1 implementation (sha1.c),
 * using the FIPS 180-1 test vectors.
 */
#include "unity/unity.h"

#include <stdio.h>
#include <string.h>

#include "sha1.h"

static void digest_to_hex(const unsigned char digest[SHA_DIGEST_LENGTH],
			  char out[SHA_DIGEST_LENGTH * 2 + 1])
{
	unsigned int i;

	for (i = 0; i < SHA_DIGEST_LENGTH; i++)
		sprintf(out + 2 * i, "%02x", digest[i]);
}

static void assert_sha1(const unsigned char *msg, size_t len, const char *expected_hex)
{
	SHA_CTX ctx;
	unsigned char digest[SHA_DIGEST_LENGTH];
	char hex[SHA_DIGEST_LENGTH * 2 + 1];

	SHA1_Init(&ctx);
	SHA1_Update(&ctx, msg, len);
	SHA1_Final(digest, &ctx);

	digest_to_hex(digest, hex);
	TEST_ASSERT_EQUAL_STRING(expected_hex, hex);
}

void test_sha1_empty_message(void)
{
	assert_sha1((const unsigned char *)"", 0, "da39a3ee5e6b4b0d3255bfef95601890afd80709");
}

void test_sha1_abc(void)
{
	assert_sha1((const unsigned char *)"abc", 3, "a9993e364706816aba3e25717850c26c9cd0d89d");
}

void test_sha1_two_block_message(void)
{
	/* 56 bytes, spans two 64-byte blocks */
	static const char msg[] = "abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq";

	assert_sha1((const unsigned char *)msg, sizeof(msg) - 1,
		    "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
}

void test_sha1_million_a_incremental(void)
{
	/* feeds one byte at a time to exercise SHA1_Update's buffering */
	SHA_CTX ctx;
	unsigned char digest[SHA_DIGEST_LENGTH];
	char hex[SHA_DIGEST_LENGTH * 2 + 1];
	int i;

	SHA1_Init(&ctx);
	for (i = 0; i < 1000000; i++)
		SHA1_Update(&ctx, (const unsigned char *)"a", 1);
	SHA1_Final(digest, &ctx);

	digest_to_hex(digest, hex);
	TEST_ASSERT_EQUAL_STRING("34aa973cd4c4daa4f61eeb2bdbad27316534016f", hex);
}
