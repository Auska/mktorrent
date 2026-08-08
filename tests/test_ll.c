/*
 * Unit tests for the linked list implementation (ll.c).
 */
#include "unity/unity.h"

#include <stdlib.h>
#include <string.h>

#include "export.h"
#include "ll.h"

static int cmp_ints(const void *a, const void *b)
{
	int x = *(const int *)a;
	int y = *(const int *)b;
	return (x > y) - (x < y);
}

/* used by the stability test: compares on the key field only */
struct pair { int key; int seq; };

static int cmp_pair_key(const void *a, const void *b)
{
	const struct pair *x = a, *y = b;
	return (x->key > y->key) - (x->key < y->key);
}

void test_ll_new_is_empty(void)
{
	struct ll *list = ll_new();

	TEST_ASSERT_NOT_NULL(list);
	TEST_ASSERT_TRUE(LL_IS_EMPTY(list));

	ll_free(list, NULL);
}

void test_ll_append_copies_data(void)
{
	struct ll *list = ll_new();
	int value = 42;
	struct ll_node *node = ll_append(list, &value, sizeof(value));

	TEST_ASSERT_NOT_NULL(node);
	TEST_ASSERT_FALSE(LL_IS_EMPTY(list));
	/* data_size > 0 means the payload is copied into the node */
	TEST_ASSERT_EQUAL_INT(42, *(int *)LL_DATA(node));
	TEST_ASSERT_TRUE(LL_DATA(node) != &value); /* copied, not stored */
	TEST_ASSERT_EQUAL_INT(sizeof(value), LL_DATASIZE(node));

	ll_free(list, NULL);
}

void test_ll_append_stores_pointer_when_size_zero(void)
{
	struct ll *list = ll_new();
	int value = 7;
	struct ll_node *node = ll_append(list, &value, 0);

	TEST_ASSERT_NOT_NULL(node);
	/* data_size == 0 means the pointer itself is stored */
	TEST_ASSERT_EQUAL_PTR(&value, LL_DATA(node));
	TEST_ASSERT_EQUAL_INT(0, LL_DATASIZE(node));

	ll_free(list, NULL);
}

void test_ll_extend_concatenates(void)
{
	struct ll *left = ll_new();
	struct ll *right = ll_new();
	int a = 1, b = 2, c = 3;

	ll_append(left, &a, sizeof(a));
	ll_append(left, &b, sizeof(b));
	ll_append(right, &c, sizeof(c));

	struct ll *ret = ll_extend(left, right);

	TEST_ASSERT_EQUAL_PTR(left, ret);
	TEST_ASSERT_FALSE(LL_IS_EMPTY(left));

	int values[3], i = 0;
	LL_FOR(node, left)
		values[i++] = *(int *)LL_DATA(node);

	TEST_ASSERT_EQUAL_INT(1, values[0]);
	TEST_ASSERT_EQUAL_INT(2, values[1]);
	TEST_ASSERT_EQUAL_INT(3, values[2]);

	ll_free(left, NULL);
}

void test_ll_sort_orders(void)
{
	struct ll *list = ll_new();
	int data[] = { 5, 3, 8, 1, 9, 2, 7, 4, 6 };
	unsigned int i;

	for (i = 0; i < sizeof(data) / sizeof(data[0]); i++)
		ll_append(list, &data[i], sizeof(data[i]));

	ll_sort(list, cmp_ints);

	int expected[] = { 1, 2, 3, 4, 5, 6, 7, 8, 9 };
	i = 0;
	LL_FOR(node, list) {
		TEST_ASSERT_EQUAL_INT(expected[i], *(int *)LL_DATA(node));
		i++;
	}
	TEST_ASSERT_EQUAL_UINT(sizeof(expected) / sizeof(expected[0]), i);

	ll_free(list, NULL);
}

void test_ll_sort_is_stable(void)
{
	struct ll *list = ll_new();
	struct pair data[] = {
		{ 2, 0 }, { 1, 0 }, { 2, 1 }, { 1, 1 }, { 1, 2 }, { 2, 2 }
	};
	unsigned int i;

	for (i = 0; i < sizeof(data) / sizeof(data[0]); i++)
		ll_append(list, &data[i], sizeof(data[i]));

	/* compare by key only, so equal keys must keep insertion order */
	ll_sort(list, cmp_pair_key);

	int last_key = -1, last_seq = -1;
	LL_FOR(node, list) {
		struct pair *p = LL_DATA(node);
		TEST_ASSERT_TRUE(p->key >= last_key);
		if (p->key == last_key)
			TEST_ASSERT_TRUE(p->seq > last_seq);
		last_key = p->key;
		last_seq = p->seq;
	}

	ll_free(list, NULL);
}

static unsigned int dtor_calls;

static void count_free(void *data)
{
	(void)data;
	dtor_calls++;
}

void test_ll_free_calls_destructor(void)
{
	struct ll *list = ll_new();
	char *s1 = strdup("one");
	char *s2 = strdup("two");

	dtor_calls = 0;
	ll_append(list, s1, 0);
	ll_append(list, s2, 0);

	ll_free(list, count_free);

	/* the destructor is called once per node */
	TEST_ASSERT_EQUAL_UINT(2, dtor_calls);

	free(s1);
	free(s2);
}
