/*
 * Unit tests for make_hash() (hash.c): the serial piece-hashing loop.
 */
#include "unity/unity.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "export.h"
#include "mktorrent.h"
#include "hash.h"
#include "sha1.h"
#include "test_util.h"

static struct metafile make_empty_metafile(void)
{
	struct metafile m;

	memset(&m, 0, sizeof(m));
	m.piece_length = 32768;
#ifdef USE_PTHREADS
	m.threads = 2;
#endif
	m.file_list = ll_new();
	TEST_ASSERT_NOT_NULL(m.file_list);
	return m;
}

static void sha1_of(const unsigned char *msg, size_t len,
		unsigned char digest[SHA_DIGEST_LENGTH])
{
	SHA_CTX ctx;

	SHA1_Init(&ctx);
	SHA1_Update(&ctx, msg, len);
	SHA1_Final(digest, &ctx);
}

void test_hash_single_piece(void)
{
	struct metafile m = make_empty_metafile();
	char *path = test_write_temp_file((const unsigned char *)"abc", 3);
	struct file_data fd = { path, 3 };
	unsigned char expected[SHA_DIGEST_LENGTH];

	TEST_ASSERT_NOT_NULL(path);
	ll_append(m.file_list, &fd, sizeof(fd));
	m.size = 3;
	m.pieces = 1;

	silence_stdout();
	unsigned char *hash = make_hash(&m);
	restore_stdout();

	sha1_of((const unsigned char *)"abc", 3, expected);
	TEST_ASSERT_EQUAL_MEMORY(expected, hash, SHA_DIGEST_LENGTH);

	free(hash);
	unlink(path);
	ll_free(m.file_list, test_free_file_data);
}

void test_hash_two_pieces_single_file(void)
{
	const size_t plen = 32768;
	unsigned char *content = malloc(plen + 3);
	struct metafile m = make_empty_metafile();
	char *path;
	struct file_data fd;
	unsigned char expected[SHA_DIGEST_LENGTH];
	unsigned int i;

	for (i = 0; i < plen; i++)
		content[i] = 'A';
	memcpy(content + plen, "abc", 3);

	path = test_write_temp_file(content, plen + 3);
	TEST_ASSERT_NOT_NULL(path);
	fd.path = path;
	fd.size = plen + 3;
	ll_append(m.file_list, &fd, sizeof(fd));
	m.size = plen + 3;
	m.pieces = 2;

	silence_stdout();
	unsigned char *hash = make_hash(&m);
	restore_stdout();

	/* first piece covers the first plen bytes */
	sha1_of(content, plen, expected);
	TEST_ASSERT_EQUAL_MEMORY(expected, hash, SHA_DIGEST_LENGTH);

	/* second (short) piece covers the trailing "abc" */
	sha1_of((const unsigned char *)"abc", 3, expected);
	TEST_ASSERT_EQUAL_MEMORY(expected, hash + SHA_DIGEST_LENGTH,
			SHA_DIGEST_LENGTH);

	free(hash);
	free(content);
	unlink(path);
	ll_free(m.file_list, test_free_file_data);
}

void test_hash_spanning_files(void)
{
	const size_t plen = 32768;
	unsigned char *content1 = malloc(plen);
	struct metafile m = make_empty_metafile();
	char *path1;
	char *path2;
	struct file_data fd1, fd2;
	unsigned char expected[SHA_DIGEST_LENGTH];
	unsigned int i;

	for (i = 0; i < plen; i++)
		content1[i] = 'B';

	path1 = test_write_temp_file(content1, plen);
	path2 = test_write_temp_file((const unsigned char *)"abc", 3);
	TEST_ASSERT_NOT_NULL(path1);
	TEST_ASSERT_NOT_NULL(path2);
	fd1.path = path1;
	fd1.size = plen;
	fd2.path = path2;
	fd2.size = 3;
	ll_append(m.file_list, &fd1, sizeof(fd1));
	ll_append(m.file_list, &fd2, sizeof(fd2));
	m.size = plen + 3;
	m.pieces = 2;

	silence_stdout();
	unsigned char *hash = make_hash(&m);
	restore_stdout();

	/* a piece boundary falls between the two files */
	sha1_of(content1, plen, expected);
	TEST_ASSERT_EQUAL_MEMORY(expected, hash, SHA_DIGEST_LENGTH);

	sha1_of((const unsigned char *)"abc", 3, expected);
	TEST_ASSERT_EQUAL_MEMORY(expected, hash + SHA_DIGEST_LENGTH,
			SHA_DIGEST_LENGTH);

	free(hash);
	free(content1);
	unlink(path1);
	unlink(path2);
	ll_free(m.file_list, test_free_file_data);
}

void test_hash_spanning_three_files(void)
{
	const size_t plen = 32768;
	struct metafile m = make_empty_metafile();
	char *path1, *path2, *path3;
	struct file_data fd1, fd2, fd3;
	unsigned char expected[SHA_DIGEST_LENGTH];
	unsigned char *content1 = malloc(plen - 2);
	unsigned char *piece1 = malloc(plen);
	unsigned int i;

	/* 32766 + 2 + 4 bytes = two pieces: the first piece ends 2 bytes
	   into the second file, the second piece lives entirely in the third */
	for (i = 0; i < plen - 2; i++)
		content1[i] = 'A';

	path1 = test_write_temp_file(content1, plen - 2);
	path2 = test_write_temp_file((const unsigned char *)"bc", 2);
	path3 = test_write_temp_file((const unsigned char *)"defg", 4);
	TEST_ASSERT_NOT_NULL(path1);
	TEST_ASSERT_NOT_NULL(path2);
	TEST_ASSERT_NOT_NULL(path3);
	fd1.path = path1;
	fd1.size = plen - 2;
	fd2.path = path2;
	fd2.size = 2;
	fd3.path = path3;
	fd3.size = 4;
	ll_append(m.file_list, &fd1, sizeof(fd1));
	ll_append(m.file_list, &fd2, sizeof(fd2));
	ll_append(m.file_list, &fd3, sizeof(fd3));
	m.size = plen + 4;
	m.pieces = 2;

	silence_stdout();
	unsigned char *hash = make_hash(&m);
	restore_stdout();

	/* piece 1 spans file1 plus the first 2 bytes of file2 */
	memcpy(piece1, content1, plen - 2);
	memcpy(piece1 + (plen - 2), "bc", 2);
	sha1_of(piece1, plen, expected);
	TEST_ASSERT_EQUAL_MEMORY(expected, hash, SHA_DIGEST_LENGTH);

	/* piece 2 is entirely inside file3 */
	sha1_of((const unsigned char *)"defg", 4, expected);
	TEST_ASSERT_EQUAL_MEMORY(expected, hash + SHA_DIGEST_LENGTH,
			SHA_DIGEST_LENGTH);

	free(hash);
	free(content1);
	free(piece1);
	unlink(path1);
	unlink(path2);
	unlink(path3);
	ll_free(m.file_list, test_free_file_data);
}
