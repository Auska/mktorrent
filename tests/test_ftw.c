/*
 * Unit tests for file_tree_walk() (ftw.c): directory traversal and
 * exclude patterns.
 */
#include "unity/unity.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

#include "export.h"
#include "mktorrent.h"
#include "ftw.h"
#include "test_util.h"

static int cmp_file_data_name(const void *a, const void *b)
{
	const struct file_data *x = a, *y = b;
	return strcmp(x->path, y->path);
}

static int collect_files(const char *path, const struct stat *sb, void *data)
{
	struct metafile *m = data;
	struct file_data fd;

	/* only regular files belong in the file list (see process_node) */
	if (!S_ISREG(sb->st_mode))
		return 0;

	fd.path = strdup(path);
	fd.size = (uintmax_t)sb->st_size;
	if (fd.path == NULL)
		return -1;
	if (ll_append(m->file_list, &fd, sizeof(fd)) == NULL)
		return -1;
	return 0;
}

static void write_file(const char *path, const char *content)
{
	FILE *f = fopen(path, "wb");

	TEST_ASSERT_NOT_NULL(f);
	TEST_ASSERT_TRUE(fwrite(content, 1, strlen(content), f) == strlen(content));
	fclose(f);
}

static void setup_tree(const char *root)
{
	char path[512];

	snprintf(path, sizeof(path), "%s/a.txt", root);
	write_file(path, "aaa");
	snprintf(path, sizeof(path), "%s/sub", root);
	TEST_ASSERT_EQUAL_INT(0, mkdir(path, 0755));
	snprintf(path, sizeof(path), "%s/sub/b.txt", root);
	write_file(path, "bbbb");
	snprintf(path, sizeof(path), "%s/sub/c.bin", root);
	write_file(path, "cc");
}

static void remove_tree(const char *root)
{
	char path[512];

	snprintf(path, sizeof(path), "%s/a.txt", root);
	unlink(path);
	snprintf(path, sizeof(path), "%s/sub/b.txt", root);
	unlink(path);
	snprintf(path, sizeof(path), "%s/sub/c.bin", root);
	unlink(path);
	snprintf(path, sizeof(path), "%s/sub", root);
	rmdir(path);
	rmdir(root);
}

static struct metafile make_metafile(void)
{
	struct metafile m;

	memset(&m, 0, sizeof(m));
	m.file_list = ll_new();
	m.exclude_list = ll_new();
	TEST_ASSERT_NOT_NULL(m.file_list);
	TEST_ASSERT_NOT_NULL(m.exclude_list);
	return m;
}

static void assert_basenames(struct metafile *m, const char *const *expected,
		unsigned int n)
{
	unsigned int i = 0;

	ll_sort(m->file_list, cmp_file_data_name);
	LL_FOR(node, m->file_list) {
		struct file_data *fd = LL_DATA(node);
		const char *base = strrchr(fd->path, '/');

		base = base ? base + 1 : fd->path;
		TEST_ASSERT_TRUE(i < n);
		TEST_ASSERT_EQUAL_STRING(expected[i], base);
		i++;
	}
	TEST_ASSERT_EQUAL_UINT(n, i);
}

void test_ftw_collects_tree(void)
{
	char root[] = "/tmp/mktorrent_ftw_XXXXXX";
	struct metafile m;
	const char *expected[] = { "a.txt", "b.txt", "c.bin" };
	uintmax_t total = 0;

	TEST_ASSERT_NOT_NULL(mkdtemp(root));
	setup_tree(root);
	m = make_metafile();

	TEST_ASSERT_EQUAL_INT(0, file_tree_walk(root, 10, collect_files, &m));

	assert_basenames(&m, expected, 3);

	/* the reported sizes must add up to 3 + 4 + 2 bytes */
	LL_FOR(node, m.file_list)
		total += LL_DATA_AS(node, struct file_data *)->size;
	TEST_ASSERT_EQUAL_UINT(9, total);

	ll_free(m.file_list, test_free_file_data);
	ll_free(m.exclude_list, NULL);
	remove_tree(root);
}

void test_ftw_exclude_pattern(void)
{
	char root[] = "/tmp/mktorrent_ftw_XXXXXX";
	struct metafile m;
	const char *expected[] = { "a.txt", "b.txt" };

	TEST_ASSERT_NOT_NULL(mkdtemp(root));
	setup_tree(root);
	m = make_metafile();

	/* fnmatch is applied to the entry basename, so c.bin is skipped */
	ll_append(m.exclude_list, (void *)"*.bin", 0);
	TEST_ASSERT_EQUAL_INT(0, file_tree_walk(root, 10, collect_files, &m));

	assert_basenames(&m, expected, 2);

	ll_free(m.file_list, test_free_file_data);
	ll_free(m.exclude_list, NULL);
	remove_tree(root);
}

void test_ftw_skips_symlinks(void)
{
	char root[] = "/tmp/mktorrent_ftw_XXXXXX";
	struct metafile m;
	const char *expected[] = { "real.txt" };
	char path[512];

	TEST_ASSERT_NOT_NULL(mkdtemp(root));

	snprintf(path, sizeof(path), "%s/real.txt", root);
	write_file(path, "data");
	/* a symlink to a file: its content must not be hashed twice */
	snprintf(path, sizeof(path), "%s/dup", root);
	TEST_ASSERT_EQUAL_INT(0, symlink("real.txt", path));
	/* a symlink cycle: must not be recursed into (would ELOOP) */
	snprintf(path, sizeof(path), "%s/loop", root);
	TEST_ASSERT_EQUAL_INT(0, symlink(".", path));

	m = make_metafile();
	TEST_ASSERT_EQUAL_INT(0, file_tree_walk(root, 10, collect_files, &m));
	assert_basenames(&m, expected, 1);

	ll_free(m.file_list, test_free_file_data);
	ll_free(m.exclude_list, NULL);

	snprintf(path, sizeof(path), "%s/dup", root);
	unlink(path);
	snprintf(path, sizeof(path), "%s/loop", root);
	unlink(path);
	snprintf(path, sizeof(path), "%s/real.txt", root);
	unlink(path);
	rmdir(root);
}

void test_ftw_nonexistent_dir(void)
{
	struct metafile m = make_metafile();
	int r;

	silence_stderr();
	r = file_tree_walk("/nonexistent_mktorrent_test_dir", 10,
			collect_files, &m);
	restore_stderr();

	/* the walk reports failure instead of crashing */
	TEST_ASSERT_NOT_EQUAL(0, r);
	TEST_ASSERT_TRUE(LL_IS_EMPTY(m.file_list));

	ll_free(m.file_list, test_free_file_data);
	ll_free(m.exclude_list, NULL);
}
