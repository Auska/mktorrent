/*
 * Unit tests for write_metainfo() (output.c): the bencoded metainfo output.
 */
#include "unity/unity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "export.h"
#include "mktorrent.h"
#include "output.h"
#include "test_util.h"

static struct metafile make_empty_metafile(void)
{
	struct metafile m;

	memset(&m, 0, sizeof(m));
	m.piece_length = 32768;
	m.file_list = ll_new();
	m.announce_list = ll_new();
	m.web_seed_list = ll_new();
	TEST_ASSERT_NOT_NULL(m.file_list);
	TEST_ASSERT_NOT_NULL(m.announce_list);
	TEST_ASSERT_NOT_NULL(m.web_seed_list);
	return m;
}

/* stores announce URLs as a single tier (like a repeated -a option) */
static void add_announce_tier(struct metafile *m, const char *url)
{
	struct ll *tier = ll_new();

	TEST_ASSERT_NOT_NULL(tier);
	ll_append(tier, (void *)url, 0); /* data_size 0: store pointer only */
	ll_append(m->announce_list, tier, 0);
}

/* writes the metainfo to a memory buffer, silencing the progress prints */
static void write_to_memory(struct metafile *m, unsigned char *hash,
		unsigned char **out, size_t *outlen)
{
	FILE *f = tmpfile();

	TEST_ASSERT_NOT_NULL(f);

	silence_stdout();
	write_metainfo(f, m, hash);
	restore_stdout();

	fseek(f, 0, SEEK_END);
	long size = ftell(f);
	rewind(f);
	unsigned char *buf = malloc(size > 0 ? (size_t)size : 1);
	TEST_ASSERT_NOT_NULL(buf);
	fread(buf, 1, (size_t)size, f);
	fclose(f);

	*out = buf;
	*outlen = (size_t)size;
}

static void assert_contains(const unsigned char *buf, size_t len, const char *s)
{
	TEST_ASSERT_NOT_EQUAL((size_t)-1,
		test_find_bytes(buf, len, (const unsigned char *)s, strlen(s)));
}

static void assert_not_contains(const unsigned char *buf, size_t len,
		const char *s)
{
	TEST_ASSERT_EQUAL((size_t)-1,
		test_find_bytes(buf, len, (const unsigned char *)s, strlen(s)));
}

void test_output_single_file(void)
{
	struct metafile m = make_empty_metafile();
	unsigned char hash[20];
	unsigned char *buf;
	size_t len;
	size_t pos;
	char expected_created[128];
	char path[] = "test.bin";
	struct file_data fd = { path, 5 };

	memset(hash, 0xAB, sizeof(hash));
	add_announce_tier(&m, "http://tracker/announce");
	m.torrent_name = "test";
	m.size = 5;
	m.pieces = 1;
	ll_append(m.file_list, &fd, sizeof(fd));

	write_to_memory(&m, hash, &buf, &len);

	assert_contains(buf, len, "8:announce23:http://tracker/announce");
	assert_contains(buf, len, "4:name4:test");
	assert_contains(buf, len, "6:lengthi5e");
	assert_contains(buf, len, "12:piece lengthi32768e");
	assert_not_contains(buf, len, "5:filesl");

	/* the created by default value must carry the correct length prefix */
	snprintf(expected_created, sizeof(expected_created),
		"10:created by%zu:mktorrent %s",
		strlen("mktorrent ") + strlen(VERSION), VERSION);
	assert_contains(buf, len, expected_created);

	/* the 20 hash bytes must follow the "6:pieces20:" key verbatim */
	pos = test_find_bytes(buf, len, (const unsigned char *)"6:pieces20:", 11);
	TEST_ASSERT_NOT_EQUAL((size_t)-1, pos);
	TEST_ASSERT_EQUAL_MEMORY(hash, buf + pos + 11, sizeof(hash));

	free(buf);
	ll_free(m.file_list, NULL);
	ll_free(m.announce_list, NULL);
	ll_free(m.web_seed_list, NULL);
}

void test_output_optional_fields(void)
{
	struct metafile m = make_empty_metafile();
	unsigned char hash[20];
	unsigned char *buf;
	size_t len;
	char path[] = "priv.bin";
	struct file_data fd = { path, 4 };

	memset(hash, 0, sizeof(hash));
	m.torrent_name = "priv";
	m.size = 4;
	m.pieces = 1;
	m.private = 1;
	m.source = "PRVT";
	m.no_creation_date = 1;
	m.created_by = "mytool";
	ll_append(m.file_list, &fd, sizeof(fd));

	write_to_memory(&m, hash, &buf, &len);

	assert_contains(buf, len, "7:privatei1e");
	assert_contains(buf, len, "6:source4:PRVT");
	assert_contains(buf, len, "10:created by6:mytool");
	assert_not_contains(buf, len, "13:creation datei");
	assert_not_contains(buf, len, "8:announce"); /* -a is optional */

	free(buf);
	ll_free(m.file_list, NULL);
	ll_free(m.announce_list, NULL);
	ll_free(m.web_seed_list, NULL);
}

void test_output_multi_file(void)
{
	struct metafile m = make_empty_metafile();
	unsigned char hash[20];
	unsigned char *buf;
	size_t len;
	char p1[] = "sub/file.txt";
	char p2[] = "a.bin";
	struct file_data fd1 = { p1, 10 };
	struct file_data fd2 = { p2, 3 };

	memset(hash, 0, sizeof(hash));
	m.torrent_name = "multi";
	m.size = 13;
	m.pieces = 1;
	m.target_is_directory = 1;
	ll_append(m.file_list, &fd1, sizeof(fd1));
	ll_append(m.file_list, &fd2, sizeof(fd2));

	write_to_memory(&m, hash, &buf, &len);

	assert_contains(buf, len, "5:filesl");
	/* paths are emitted as a list of directory components + filename */
	assert_contains(buf, len, "6:lengthi10e4:pathl3:sub8:file.txte");
	assert_contains(buf, len, "6:lengthi3e4:pathl5:a.bine");
	assert_not_contains(buf, len, "6:lengthi13e"); /* no single-file length */

	free(buf);
	ll_free(m.file_list, NULL);
	ll_free(m.announce_list, NULL);
	ll_free(m.web_seed_list, NULL);
}

void test_output_cross_seed(void)
{
	struct metafile m = make_empty_metafile();
	unsigned char hash[20];
	unsigned char *buf;
	size_t len;
	char path[] = "seed.bin";
	struct file_data fd = { path, 1 };

	memset(hash, 0, sizeof(hash));
	m.torrent_name = "seed";
	m.size = 1;
	m.pieces = 1;
	m.cross_seed = 1;
	ll_append(m.file_list, &fd, sizeof(fd));

	write_to_memory(&m, hash, &buf, &len);

	/* "mktorrent-" plus 2*CROSS_SEED_RAND_LENGTH hex digits */
	assert_contains(buf, len, "12:x_cross_seed42:mktorrent-");

	free(buf);
	ll_free(m.file_list, NULL);
	ll_free(m.announce_list, NULL);
	ll_free(m.web_seed_list, NULL);
}
