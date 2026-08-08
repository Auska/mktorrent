/*
 * Shared helpers for the Unity test suite.
 */
#include "test_util.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>

#ifdef _WIN32
#define TEST_NULL_DEVICE "NUL"
#else
#define TEST_NULL_DEVICE "/dev/null"
#endif

static int saved_stdout_fd = -1;
static int null_fd = -1;
static int saved_stderr_fd = -1;

void silence_stdout(void)
{
	fflush(stdout);
	saved_stdout_fd = dup(STDOUT_FILENO);
	null_fd = open(TEST_NULL_DEVICE, O_WRONLY);
	dup2(null_fd, STDOUT_FILENO);
}

void restore_stdout(void)
{
	fflush(stdout);
	dup2(saved_stdout_fd, STDOUT_FILENO);
	close(saved_stdout_fd);
	close(null_fd);
	saved_stdout_fd = -1;
}

void silence_stderr(void)
{
	fflush(stderr);
	saved_stderr_fd = dup(STDERR_FILENO);
	null_fd = open(TEST_NULL_DEVICE, O_WRONLY);
	dup2(null_fd, STDERR_FILENO);
}

void restore_stderr(void)
{
	fflush(stderr);
	dup2(saved_stderr_fd, STDERR_FILENO);
	close(saved_stderr_fd);
	close(null_fd);
	saved_stderr_fd = -1;
}

void test_free_file_data(void *data)
{
	struct file_data *fd = data;
	free(fd->path);
}

char *test_write_temp_file(const unsigned char *content, size_t len)
{
	char path[] = "/tmp/mktorrent_test_XXXXXX";
	int fd = mkstemp(path);

	if (fd < 0)
		return NULL;
	if (write(fd, content, len) != (ssize_t)len) {
		close(fd);
		unlink(path);
		return NULL;
	}
	close(fd);
	return strdup(path);
}

size_t test_find_bytes(const unsigned char *buf, size_t n, const unsigned char *needle, size_t m)
{
	size_t i;

	if (m == 0)
		return 0;
	for (i = 0; i + m <= n; i++)
		if (memcmp(buf + i, needle, m) == 0)
			return i;
	return (size_t)-1;
}
