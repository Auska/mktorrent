/*
 * Unity test runner: wires all test modules together.
 */
#include "unity/unity.h"

void setUp(void) {}
void tearDown(void) {}

/* test_ll.c */
void test_ll_new_is_empty(void);
void test_ll_append_copies_data(void);
void test_ll_append_stores_pointer_when_size_zero(void);
void test_ll_extend_concatenates(void);
void test_ll_sort_orders(void);
void test_ll_sort_is_stable(void);
void test_ll_free_calls_destructor(void);

/* test_sha1.c */
void test_sha1_empty_message(void);
void test_sha1_abc(void);
void test_sha1_two_block_message(void);
void test_sha1_million_a_incremental(void);

/* test_hash.c */
void test_hash_single_piece(void);
void test_hash_two_pieces_single_file(void);
void test_hash_spanning_files(void);
void test_hash_spanning_three_files(void);

/* test_output.c */
void test_output_single_file(void);
void test_output_optional_fields(void);
void test_output_multi_file(void);
void test_output_cross_seed(void);

/* test_ftw.c */
void test_ftw_collects_tree(void);
void test_ftw_exclude_pattern(void);
void test_ftw_skips_symlinks(void);
void test_ftw_nonexistent_dir(void);

/* test_init.c */
void test_init_strip_ending_dirseps(void);
void test_init_path_basename(void);
void test_init_set_absolute_file_path(void);
void test_init_get_slist(void);

int main(void)
{
	UNITY_BEGIN();

	RUN_TEST(test_ll_new_is_empty);
	RUN_TEST(test_ll_append_copies_data);
	RUN_TEST(test_ll_append_stores_pointer_when_size_zero);
	RUN_TEST(test_ll_extend_concatenates);
	RUN_TEST(test_ll_sort_orders);
	RUN_TEST(test_ll_sort_is_stable);
	RUN_TEST(test_ll_free_calls_destructor);

	RUN_TEST(test_sha1_empty_message);
	RUN_TEST(test_sha1_abc);
	RUN_TEST(test_sha1_two_block_message);
	RUN_TEST(test_sha1_million_a_incremental);

	RUN_TEST(test_hash_single_piece);
	RUN_TEST(test_hash_two_pieces_single_file);
	RUN_TEST(test_hash_spanning_files);
	RUN_TEST(test_hash_spanning_three_files);

	RUN_TEST(test_output_single_file);
	RUN_TEST(test_output_optional_fields);
	RUN_TEST(test_output_multi_file);
	RUN_TEST(test_output_cross_seed);

	RUN_TEST(test_ftw_collects_tree);
	RUN_TEST(test_ftw_exclude_pattern);
	RUN_TEST(test_ftw_skips_symlinks);
	RUN_TEST(test_ftw_nonexistent_dir);

	RUN_TEST(test_init_strip_ending_dirseps);
	RUN_TEST(test_init_path_basename);
	RUN_TEST(test_init_set_absolute_file_path);
	RUN_TEST(test_init_get_slist);

	return UNITY_END();
}
