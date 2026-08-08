/*
 * Shared helpers for the Unity test suite.
 */
#ifndef MKTORRENT_TEST_UTIL_H
#define MKTORRENT_TEST_UTIL_H

#include <stddef.h>

#include "export.h"
#include "mktorrent.h"

/* redirect stdout/stderr to the null device and back (silences noise) */
void silence_stdout(void);
void restore_stdout(void);
void silence_stderr(void);
void restore_stderr(void);

/* frees the path member of a file_data node (ll_free destructor) */
void test_free_file_data(void *data);

/* creates a temporary file with the given content, returns its path */
char *test_write_temp_file(const unsigned char *content, size_t len);

/* byte-string search over binary data */
size_t test_find_bytes(const unsigned char *buf, size_t n, const unsigned char *needle, size_t m);

#endif /* MKTORRENT_TEST_UTIL_H */
