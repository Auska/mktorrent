/*
 * Unit tests for the helper functions inside init.c.
 *
 * init.c is compiled into this translation unit with `static` stripped so
 * its internal helpers are reachable; the test target therefore must not
 * compile init.c as a separate object.
 */
#include "unity/unity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define static
#include "init.c"
#undef static

#include "ll.h"

void test_init_strip_ending_dirseps(void)
{
	char a[] = "foo/bar/";
	char b[] = "foo/bar";
	char c[] = "/";
	char d[] = "";

	strip_ending_dirseps(a);
	TEST_ASSERT_EQUAL_STRING("foo/bar", a);

	strip_ending_dirseps(b);
	TEST_ASSERT_EQUAL_STRING("foo/bar", b);

	/* the root path must not be stripped into an empty string */
	strip_ending_dirseps(c);
	TEST_ASSERT_EQUAL_STRING("/", c);

	strip_ending_dirseps(d);
	TEST_ASSERT_EQUAL_STRING("", d);
}

void test_init_path_basename(void)
{
	TEST_ASSERT_EQUAL_STRING("c", path_basename("/a/b/c"));
	TEST_ASSERT_EQUAL_STRING("b", path_basename("a/b"));
	TEST_ASSERT_EQUAL_STRING("file.txt", path_basename("dir/file.txt"));
	TEST_ASSERT_EQUAL_STRING("mktorrent", path_basename("mktorrent"));
}

void test_init_set_absolute_file_path(void)
{
	struct metafile m;
	char cwd[4096];
	char expected[8192];

	memset(&m, 0, sizeof(m));
	TEST_ASSERT_NOT_NULL(getcwd(cwd, sizeof(cwd)));

	/* no output path given: default to <cwd>/<name>.torrent */
	m.torrent_name = "foo";
	m.metainfo_file_path = NULL;
	set_absolute_file_path(&m);
	TEST_ASSERT_NOT_NULL(m.metainfo_file_path);
	TEST_ASSERT_TRUE(m.metainfo_file_path[0] == DIRSEP_CHAR);
	snprintf(expected, sizeof(expected), "%s" DIRSEP "foo.torrent", cwd);
	TEST_ASSERT_EQUAL_STRING(expected, m.metainfo_file_path);
	free(m.metainfo_file_path);

	/* a relative output path is resolved against the cwd */
	m.metainfo_file_path = "sub/out.torrent";
	set_absolute_file_path(&m);
	snprintf(expected, sizeof(expected), "%s" DIRSEP "sub/out.torrent", cwd);
	TEST_ASSERT_EQUAL_STRING(expected, m.metainfo_file_path);
	free(m.metainfo_file_path);

	/* an absolute output path is kept as-is */
	m.metainfo_file_path = "/abs/out.torrent";
	set_absolute_file_path(&m);
	TEST_ASSERT_EQUAL_STRING("/abs/out.torrent", m.metainfo_file_path);
	free(m.metainfo_file_path);
}

void test_init_get_slist(void)
{
	char input[] = "a,b,c";
	struct ll *list = get_slist(input);
	const char *expected[] = { "a", "b", "c" };
	unsigned int i = 0;

	LL_FOR(node, list) {
		TEST_ASSERT_TRUE(i < 3);
		TEST_ASSERT_EQUAL_STRING(expected[i], (const char *)LL_DATA(node));
		i++;
	}
	TEST_ASSERT_EQUAL_UINT(3, i);
	ll_free(list, NULL);

	char single[] = "only";
	list = get_slist(single);
	i = 0;
	LL_FOR(node, list) {
		TEST_ASSERT_TRUE(i < 1);
		TEST_ASSERT_EQUAL_STRING("only", (const char *)LL_DATA(node));
		i++;
	}
	TEST_ASSERT_EQUAL_UINT(1, i);
	ll_free(list, NULL);
}
