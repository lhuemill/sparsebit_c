/* Copyright 2020 Louis Huemiller Jr.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "sparsebit_c.h"  // Intentionally included first, to aid in
                          // detection of includes, that may be missing
                          // within sparsebit_c.h.

#include <assert.h>
#include <stdbool.h>
#include <stdio.h>
#include <CUnit/Basic.h>

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
#define NUM(a) (sizeof(a)/sizeof((a)[0]))
#define MEMCLEAR(p, n) memset((p), 0, (n))

void test_create_delete(void)
{
	sb_t *sb = sb_create();
	CU_ASSERT_PTR_NOT_EQUAL_FATAL(sb, NULL);

	sb_pdynstr_t err_string = sb_validate(sb);
	if (err_string) {
		fprintf(stderr, "Sparsebit Validate Failed: %s\n", err_string);
		fputs(sb_dump(sb), stderr);
		CU_FAIL("Sparsebit - Inconsistent Internal State");
		sb_pdynstr_free(err_string);
	}

	sb_delete(sb);
}

void test_set(void)
{
	int rv;
	sb_t *sb = sb_create();
	CU_ASSERT_PTR_NOT_EQUAL_FATAL(sb, NULL);

	// Set bit at index 5.
	rv = sb_set(sb, 5);
	CU_ASSERT_EQUAL_FATAL(rv, 0);

	CU_ASSERT_FALSE(sb_isset(sb, 0));
	CU_ASSERT_FALSE(!sb_iscleared(sb, 0));
	CU_ASSERT_FALSE(sb_isset(sb, 1));
	CU_ASSERT_FALSE(!sb_iscleared(sb, 1));
	CU_ASSERT_FALSE(sb_isset(sb, 2));
	CU_ASSERT_FALSE(!sb_iscleared(sb, 2));
	CU_ASSERT_FALSE(sb_isset(sb, 3));
	CU_ASSERT_FALSE(!sb_iscleared(sb, 3));
	CU_ASSERT_FALSE(sb_isset(sb, 4));
	CU_ASSERT_FALSE(!sb_iscleared(sb, 4));
	CU_ASSERT_TRUE(sb_isset(sb, 5));
	CU_ASSERT_TRUE(!sb_iscleared(sb, 5));
	CU_ASSERT_FALSE(sb_isset(sb, 6));
	CU_ASSERT_FALSE(!sb_iscleared(sb, 6));
	CU_ASSERT_FALSE(sb_isset(sb, 7));
	CU_ASSERT_FALSE(!sb_iscleared(sb, 7));
	CU_ASSERT_FALSE(sb_isset(sb, 8));
	CU_ASSERT_FALSE(!sb_iscleared(sb, 8));
	CU_ASSERT_FALSE(sb_isset(sb, 9));
	CU_ASSERT_FALSE(!sb_iscleared(sb, 9));
	CU_ASSERT_FALSE(sb_isset(sb, 10));
	CU_ASSERT_FALSE(!sb_iscleared(sb, 10));

	sb_pdynstr_t err_string = sb_validate(sb);
	if (err_string) {
		fprintf(stderr, "Sparsebit Validate Failed: %s\n", err_string);
		fputs(sb_dump(sb), stderr);
		CU_FAIL("Sparsebit - Inconsistent Internal State");
		sb_pdynstr_free(err_string);
	}

	sb_delete(sb);
}

void test_clear(void)
{
	int rv;
	sb_t *sb = sb_create();
	CU_ASSERT_PTR_NOT_EQUAL_FATAL(sb, NULL);

	// Set a few bits.
	rv = sb_set(sb, 2);
	CU_ASSERT_EQUAL_FATAL(rv, 0);
	rv = sb_set(sb, 4);
	CU_ASSERT_EQUAL_FATAL(rv, 0);
	rv = sb_set(sb, 8);
	CU_ASSERT_EQUAL_FATAL(rv, 0);
	rv = sb_set(sb, 9);
	CU_ASSERT_EQUAL_FATAL(rv, 0);
	rv = sb_set(sb, 10);
	CU_ASSERT_EQUAL_FATAL(rv, 0);

	// Clear a few bits, including one that is already cleared.
	rv = sb_clear(sb, 4);
	CU_ASSERT_EQUAL_FATAL(rv, 0);
	rv = sb_clear(sb, 6);
	CU_ASSERT_EQUAL_FATAL(rv, 0);
	rv = sb_clear(sb, 9);
	CU_ASSERT_EQUAL_FATAL(rv, 0);

	CU_ASSERT_FALSE(sb_isset(sb, 0));
	CU_ASSERT_FALSE(!sb_iscleared(sb, 0));
	CU_ASSERT_FALSE(sb_isset(sb, 1));
	CU_ASSERT_FALSE(!sb_iscleared(sb, 1));
	CU_ASSERT_TRUE(sb_isset(sb, 2));
	CU_ASSERT_TRUE(!sb_iscleared(sb, 2));
	CU_ASSERT_FALSE(sb_isset(sb, 3));
	CU_ASSERT_FALSE(!sb_iscleared(sb, 3));
	CU_ASSERT_FALSE(sb_isset(sb, 4));
	CU_ASSERT_FALSE(!sb_iscleared(sb, 4));
	CU_ASSERT_FALSE(sb_isset(sb, 5));
	CU_ASSERT_FALSE(!sb_iscleared(sb, 5));
	CU_ASSERT_FALSE(sb_isset(sb, 6));
	CU_ASSERT_FALSE(!sb_iscleared(sb, 6));
	CU_ASSERT_FALSE(sb_isset(sb, 7));
	CU_ASSERT_FALSE(!sb_iscleared(sb, 7));
	CU_ASSERT_TRUE(sb_isset(sb, 8));
	CU_ASSERT_TRUE(!sb_iscleared(sb, 8));
	CU_ASSERT_FALSE(sb_isset(sb, 9));
	CU_ASSERT_FALSE(!sb_iscleared(sb, 9));
	CU_ASSERT_TRUE(sb_isset(sb, 10));
	CU_ASSERT_TRUE(!sb_iscleared(sb, 10));
	CU_ASSERT_FALSE(sb_isset(sb, 11));
	CU_ASSERT_FALSE(!sb_iscleared(sb, 11));
	CU_ASSERT_FALSE(sb_isset(sb, 12));
	CU_ASSERT_FALSE(!sb_iscleared(sb, 12));

	sb_pdynstr_t err_string = sb_validate(sb);
	if (err_string) {
		fprintf(stderr, "Sparsebit Validate Failed: %s\n", err_string);
		fputs(sb_dump(sb), stderr);
		CU_FAIL("Sparsebit - Inconsistent Internal State");
		sb_pdynstr_free(err_string);
	}

	sb_delete(sb);
}

void test_multinode(void)
{
	int rv;
	sb_t *sb = sb_create();
	CU_ASSERT_PTR_NOT_EQUAL_FATAL(sb, NULL);

	// Set two bits, with indexes sufficently different that their state
	// can't be described in the mask of a single node.
	sb_idx_t idx1 = 73;
	sb_idx_t idx2 = 173;
	rv = sb_set(sb, idx1);
	CU_ASSERT_EQUAL_FATAL(rv, 0);
	rv = sb_set(sb, idx2);
	CU_ASSERT_EQUAL_FATAL(rv, 0);

	unsigned int pad = 10;
	for (sb_idx_t idx = (MIN(idx1, idx2) - pad);
		idx < (MAX(idx1, idx2) + pad); idx++) {
		if ((idx != idx1) && (idx != idx2)) {
			CU_ASSERT_FALSE(sb_isset(sb, idx));
			CU_ASSERT_TRUE(sb_iscleared(sb, idx));
		} else {
			CU_ASSERT_TRUE(sb_isset(sb, idx));
			CU_ASSERT_FALSE(sb_iscleared(sb, idx));
		}
	}

	sb_pdynstr_t err_string = sb_validate(sb);
	if (err_string) {
		fprintf(stderr, "Sparsebit Validate Failed: %s\n", err_string);
		fputs(sb_dump(sb), stderr);
		CU_FAIL("Sparsebit - Inconsistent Internal State");
		sb_pdynstr_free(err_string);
	}

	sb_delete(sb);
}

void test_adjacentnode(void)
{
	int rv;
	sb_t *sb = sb_create();
	CU_ASSERT_PTR_NOT_EQUAL_FATAL(sb, NULL);

	// Set two bits, with indexes sufficently close, yet far enough apart,
	// to be supported by two adjacent nodes.
	sb_idx_t idx1 = 122;  // 128 - 6
	sb_idx_t idx2 = 130;  // 128 + 2
	rv = sb_set(sb, idx1);
	CU_ASSERT_EQUAL_FATAL(rv, 0);
	rv = sb_set(sb, idx2);
	CU_ASSERT_EQUAL_FATAL(rv, 0);

	unsigned int pad = 10;
	for (sb_idx_t idx = (MIN(idx1, idx2) - pad);
		idx < (MAX(idx1, idx2) + pad); idx++) {
		if ((idx != idx1) && (idx != idx2)) {
			CU_ASSERT_FALSE(sb_isset(sb, idx));
			CU_ASSERT_TRUE(sb_iscleared(sb, idx));
		} else {
			CU_ASSERT_TRUE(sb_isset(sb, idx));
			CU_ASSERT_FALSE(sb_iscleared(sb, idx));
		}
	}

	sb_pdynstr_t err_string = sb_validate(sb);
	if (err_string) {
		fprintf(stderr, "Sparsebit Validate Failed: %s\n", err_string);
		fputs(sb_dump(sb), stderr);
		CU_FAIL("Sparsebit - Inconsistent Internal State");
		sb_pdynstr_free(err_string);
	}

	sb_delete(sb);
}

void test_clear_two_nodes(void)
{
	int rv;
	sb_t *sb = sb_create();
	CU_ASSERT_PTR_NOT_EQUAL_FATAL(sb, NULL);

	// Create 5 distinct nodes.
	sb_idx_t idx1 = 20;
	sb_idx_t idx2 = 328;
	sb_idx_t idx3 = 732;
	sb_idx_t idx4 = 923;
	sb_idx_t idx5 = 2056;
	rv = sb_set(sb, idx1);
	CU_ASSERT_EQUAL_FATAL(rv, 0);
	rv = sb_set(sb, idx2);
	CU_ASSERT_EQUAL_FATAL(rv, 0);
	rv = sb_set(sb, idx3);
	CU_ASSERT_EQUAL_FATAL(rv, 0);
	rv = sb_set(sb, idx4);
	CU_ASSERT_EQUAL_FATAL(rv, 0);
	rv = sb_set(sb, idx5);
	CU_ASSERT_EQUAL_FATAL(rv, 0);

	// Delete two of the nodes, by clearing all the bits described by the nodes.
	rv = sb_clear(sb, idx2);
	CU_ASSERT_EQUAL_FATAL(rv, 0);
	rv = sb_clear(sb, idx4);
	CU_ASSERT_EQUAL_FATAL(rv, 0);

	// Check that bits not cleared are still set.
	unsigned int pad = 10;
	for (sb_idx_t idx = idx1 - pad; idx < idx2 + pad; idx++) {
		if ((idx != idx1) && (idx != idx3) && (idx != idx5)) {
			CU_ASSERT_FALSE(sb_isset(sb, idx));
			CU_ASSERT_TRUE(sb_iscleared(sb, idx));
		} else {
			CU_ASSERT_TRUE(sb_isset(sb, idx));
			CU_ASSERT_FALSE(sb_iscleared(sb, idx));
		}
	}

	sb_pdynstr_t err_string = sb_validate(sb);
	if (err_string) {
		fprintf(stderr, "Sparsebit Validate Failed: %s\n", err_string);
		fputs(sb_dump(sb), stderr);
		CU_FAIL("Sparsebit - Inconsistent Internal State");
		sb_pdynstr_free(err_string);
	}

	sb_delete(sb);
}

void test_first_next_set(void)
{
	int rv;
	sb_idx_t idx;

	sb_t *sb = sb_create();
	CU_ASSERT_PTR_NOT_EQUAL_FATAL(sb, NULL);

	// Set some bits, including ones at the lowest and highest indexes.
	sb_idx_t idx_set[] = {
		0, 1, 2,
		74, 256, 512 - 1,
		SB_IDX_MAX - 3, SB_IDX_MAX - 1, SB_IDX_MAX,
	};
	for (unsigned int n = 0; n < NUM(idx_set); n++) {
		rv = sb_set(sb, idx_set[n]);
		CU_ASSERT_EQUAL_FATAL(rv, 0);
	}

	// Clear and reset a bit in the middle, so that a simple binary-tree
	// implementation won't store the nodes effectively as a linked-list.
	// By creating nodes in accending order, a simple binary-tree implementation
	// would only add nodes to the right of the lowest child. By clearing then
	// reseting all the bits of a node in the middle, a simple binary-tree
	// implementation will add back this node to the left of one of the
	// existing nodes.
	sb_clear(sb, idx_set[4]);
	sb_set(sb, idx_set[4]);

	idx = sb_firstset(sb);
	CU_ASSERT_EQUAL(idx, idx_set[0]);

	for (unsigned int n = 1; n < NUM(idx_set); n++) {
		idx = sb_nextset(sb, idx_set[n]);
		if ((n + 1) < NUM(idx_set)) {
			CU_ASSERT_EQUAL(idx, idx_set[n + 1]);
		} else {
			CU_ASSERT_EQUAL(idx, 0);
		}
	}

	// Clear the bits at index 0 and index ~0 and test again.
	assert(idx_set[0] == 0);
	assert(idx_set[NUM(idx_set) - 1] == SB_IDX_MAX);
	sb_clear(sb, idx_set[0]);
	sb_clear(sb, idx_set[NUM(idx_set) - 1]);
	
	idx = sb_firstset(sb);
	CU_ASSERT_EQUAL(idx, idx_set[1]);

	for (unsigned int n = 2; n < (NUM(idx_set) - 1); n++) {
		idx = sb_nextset(sb, idx_set[n]);
		if ((n + 1) < (NUM(idx_set) - 1)) {
			CU_ASSERT_EQUAL(idx, idx_set[n + 1]);
		} else {
			CU_ASSERT_EQUAL(idx, 0);
		}
	}

	sb_pdynstr_t err_string = sb_validate(sb);
	if (err_string) {
		fprintf(stderr, "Sparsebit Validate Failed: %s\n", err_string);
		fputs(sb_dump(sb), stderr);
		CU_FAIL("Sparsebit - Inconsistent Internal State");
		sb_pdynstr_free(err_string);
	}

	sb_delete(sb);
}

void test_first_next_cleared(void)
{
	int rv;
	sb_idx_t idx;

	sb_t *sb = sb_create();
	CU_ASSERT_PTR_NOT_EQUAL_FATAL(sb, NULL);

	// Set significant number of bits starting at lowest index and ending
	// at highest index.
	sb_num_t set_num = 2000;
	rv = sb_setnum(sb, 0, set_num);
	CU_ASSERT_EQUAL_FATAL(rv, 0);
	rv = sb_setnum(sb, SB_IDX_MAX - set_num + 1, set_num);
	CU_ASSERT_EQUAL_FATAL(rv, 0);

	// Clear some bits, including ones at the lowest and highest indexes.
	sb_idx_t idx_cleared[] = {
		0, 1, 2,
		68, 316, 1024 - 1, 1024,
		SB_IDX_MAX - 3, SB_IDX_MAX - 1, SB_IDX_MAX,
	};
	for (unsigned int n = 0; n < NUM(idx_cleared); n++) {
		rv = sb_clear(sb, idx_cleared[n]);
		CU_ASSERT_EQUAL_FATAL(rv, 0);
	}

	// Clear and reset at least a node worth of bits in the middle, so that a
	// simple binary-tree implementation won't store the nodes effectively as
	// a linked-list.  By creating nodes in accending order, a simple
	// binary-tree implementation would only add nodes to the right of the
	// lowest child. By clearing then reseting all the bits of a node in the
	// middle, a simple binary-tree implementation will add back this node to
	// the left of one of the existing nodes.
	sb_clearnum(sb, idx_cleared[4] + 1, 100);
	sb_setnum(sb, idx_cleared[4] + 1, 100);

	idx = sb_firstcleared(sb);
	CU_ASSERT_EQUAL(idx, idx_cleared[0]);

	for (unsigned int n = 1; n < NUM(idx_cleared); n++) {
		idx = sb_nextcleared(sb, idx_cleared[n]);
		if ((n + 1) < NUM(idx_cleared)) {
			if ((idx_cleared[n] < set_num) && ((idx_cleared[n + 1] > set_num))) {
				CU_ASSERT_EQUAL(idx, set_num);
			} else {
				CU_ASSERT_EQUAL(idx, idx_cleared[n + 1]);
			}
		} else {
			CU_ASSERT_EQUAL(idx, 0);
		}
	}

	sb_pdynstr_t err_string = sb_validate(sb);
	if (err_string) {
		fprintf(stderr, "Sparsebit Validate Failed: %s\n", err_string);
		fputs(sb_dump(sb), stderr);
		CU_FAIL("Sparsebit - Inconsistent Internal State");
		sb_pdynstr_free(err_string);
	}

	sb_delete(sb);
}

// Sets then clears 6 bits in all possible orders/permutations.  The bits
// indexes are sufficiently seperated such that the state of each bit
// will be maintained by its own node.  The primary purpose of this test
// is to validate that the binary-tree used to maintain the nodes is valid
// no matter which order the nodes are added then deleted.
// There is a trade-off between how many nodes are added then deleted and
// how long the test takes to execute.  A complete binary tree has 1 node
// at the root (level-0), 2 at level-1, and 4 at level-2.  For a total of
// 7 nodes in the first 3 levels.  Ideally would validate all permutations
// for the first 3 levels, but that tends to take several minutes to
// execute.  This test performs all permutations of just 6 nodes, which
// tends to execute in significantly less than a minute and provides
// nearly the same level of validations.  Nearly the same level of validation
// in that the third level of a complete binary-tree will have 3 of the 4
// nodes present, with 2 of them attached to the left and right of the next
// higher level and the 3rd attached to either the left or right of the
// other node in the next higher level.
void test_permutations6(void)
{
	unsigned int idx_set[6];
	unsigned int idx_clear[6];

	sb_idx_t idx_base = 0x2350000;
	sb_idx_t idx_stride = 0x100;  // At least twice the number of bits
	                              // in a nodes mask, so each of the set
	                              // bits are maintained in a distinct node.

	// For each permutation of the Set indexes.
	MEMCLEAR(idx_set, sizeof(idx_set));
	do {
		// Increment Add Indexes
		unsigned int n;
		for (n = 0; n < NUM(idx_set); n++) {
			idx_set[n]++;
			if (idx_set[n] < NUM(idx_set)) {
				break;
			}
			idx_set[n] = 0;
		}
		if (n == NUM(idx_set)) break;

		// Skip if any duplicate set indexes.
		bool skip = false;
		for (unsigned int n1 = 0; n1 < NUM(idx_set) - 1; n1++) {
			for (unsigned int n2 = n1 + 1; n2 < NUM(idx_set); n2++) {
				if (idx_set[n1] == idx_set[n2]) {
					skip = true;
					break;
				}
			}
			if (skip) {
				break;
			}
		}
		if (skip) continue;

		// For each permutation of the clear indexes.
		MEMCLEAR(idx_clear, sizeof(idx_clear));
		do {
			// Increment Clear Indexes
			unsigned int n;
			for (n = 0; n < NUM(idx_clear); n++) {
				idx_clear[n]++;
				if (idx_clear[n] < NUM(idx_clear)) {
					break;
				}
				idx_clear[n] = 0;
			}
			if (n == NUM(idx_clear)) break;

			// Skip if any duplicate clear indexes.
			bool skip = false;
			for (unsigned int n1 = 0; n1 < NUM(idx_clear) - 1; n1++) {
				for (unsigned int n2 = n1 + 1; n2 < NUM(idx_clear); n2++) {
					if (idx_clear[n1] == idx_clear[n2]) {
						skip = true;
						break;
					}
				}
				if (skip) {
					break;
				}
			}
			if (skip) continue;

			sb_t *sb = sb_create();

			// Perform the set operations, in the order given by idx_set[].
			for (unsigned int n = 0; n < NUM(idx_set); n++) {
				sb_set(sb, (idx_set[n] * idx_stride) + idx_base);
			}
			sb_pdynstr_t err_string = sb_validate(sb);
			if (err_string) {
				fprintf(stderr, "Sparsebit Validate Failed: %s\n", err_string);
				fputs(sb_dump(sb), stderr);
				CU_FAIL_FATAL("Sparsebit - Inconsistent Internal State");
				sb_pdynstr_free(err_string);
			}

			// Perform the clear operations, in the order given by idx_clear[].
			for (unsigned int n = 0; n < NUM(idx_clear); n++) {
				sb_clear(sb, (idx_clear[n] * idx_stride) + idx_base);
				err_string = sb_validate(sb);
				if (err_string) {
					fprintf(stderr, "Sparsebit Validate Failed: %s\n", err_string);
					fputs(sb_dump(sb), stderr);
					CU_FAIL_FATAL("Sparsebit - Inconsistent Internal State");
					sb_pdynstr_free(err_string);
				}
			}
			err_string = sb_validate(sb);
			if (err_string) {
				fprintf(stderr, "Sparsebit Validate Failed: %s\n", err_string);
				fputs(sb_dump(sb), stderr);
				CU_FAIL_FATAL("Sparsebit - Inconsistent Internal State");
				sb_pdynstr_free(err_string);
			}

			sb_delete(sb);
		} while (true);
	} while (true);


















}

int main(void)
{
	bool any_failed;
	CU_ErrorCode cu_error_code;
	CU_pSuite pSuite = NULL;

	/* Initialize CUnit  Test Registry */
	if (CUE_SUCCESS != CU_initialize_registry()) {
		return CU_get_error();
	}

	/* Add Test Suite */
	pSuite = CU_add_suite("Suite_1", NULL, NULL);
	if (pSuite == NULL) {
		CU_cleanup_registry();
		return CU_get_error();
	}

	/* Add Tests */
	if ((CU_add_test(pSuite, "create_delete", test_create_delete) == NULL)
		|| (CU_add_test(pSuite, "set", test_set) == NULL)
		|| (CU_add_test(pSuite, "clear", test_clear) == NULL)
		|| (CU_add_test(pSuite, "multinode", test_multinode) == NULL)
		|| (CU_add_test(pSuite, "adjacentnode", test_adjacentnode) == NULL)
		|| (CU_add_test(pSuite, "clear_two_nodes", test_clear_two_nodes) == NULL)
		|| (CU_add_test(pSuite, "first_next_set", test_first_next_set) == NULL)
		|| (CU_add_test(pSuite, "first_next_cleared", test_first_next_cleared)
			== NULL)
		|| (CU_add_test(pSuite, "permutations6", test_permutations6) == NULL)
		) {
		CU_cleanup_registry();

		return CU_get_error();
	}

	/* Run tests - Basic Mode */
	CU_basic_set_mode(CU_BRM_VERBOSE);
	CU_basic_run_tests();
	any_failed = (CU_get_number_of_tests_failed()) ? true : false;

	CU_cleanup_registry();
	cu_error_code = CU_get_error();
	if (cu_error_code) {
		return cu_error_code;
	}

	return (any_failed) ? 1 : 0;
}

