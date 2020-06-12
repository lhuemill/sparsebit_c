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
#include <stdlib.h>
#include <sys/types.h>
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
		sb_dynstr_free(err_string);
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
		sb_dynstr_free(err_string);
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
		sb_dynstr_free(err_string);
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
		sb_dynstr_free(err_string);
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
		sb_dynstr_free(err_string);
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
		sb_dynstr_free(err_string);
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
		sb_dynstr_free(err_string);
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
				sb_dynstr_free(err_string);
			}

			// Perform the clear operations, in the order given by idx_clear[].
			for (unsigned int n = 0; n < NUM(idx_clear); n++) {
				sb_clear(sb, (idx_clear[n] * idx_stride) + idx_base);
				err_string = sb_validate(sb);
				if (err_string) {
					fprintf(stderr, "Sparsebit Validate Failed: %s\n", err_string);
					fputs(sb_dump(sb), stderr);
					CU_FAIL_FATAL("Sparsebit - Inconsistent Internal State");
					sb_dynstr_free(err_string);
				}
			}
			err_string = sb_validate(sb);
			if (err_string) {
				fprintf(stderr, "Sparsebit Validate Failed: %s\n", err_string);
				fputs(sb_dump(sb), stderr);
				CU_FAIL_FATAL("Sparsebit - Inconsistent Internal State");
				sb_dynstr_free(err_string);
			}

			sb_delete(sb);
		} while (true);
	} while (true);
}

void test_set_after_single(void)
{
	int rv;

	sb_t *sb = sb_create();
	CU_ASSERT_PTR_NOT_EQUAL_FATAL(sb, NULL);

	// Set a few bits before a mask boundary
	// and then a single bit at the mask boundary.
	sb_idx_t boundary = 0x1000;
	int offsets[] = {-5, -2, -1, 0};
	for (unsigned int n = 0; n < NUM(offsets); n++) {
		rv = sb_set(sb, boundary + offsets[n]);
		CU_ASSERT_EQUAL_FATAL(rv, 0);
	}

	sb_num_t num = sb_nodes_num(sb);
	CU_ASSERT_EQUAL(num, 1);

	sb_pdynstr_t err_string = sb_validate(sb);
	if (err_string) {
		fprintf(stderr, "Sparsebit Validate Failed: %s\n", err_string);
		fputs(sb_dump(sb), stderr);
		CU_FAIL("Sparsebit - Inconsistent Internal State");
		sb_dynstr_free(err_string);
	}

	sb_delete(sb);
}

void test_set_after_clear_single(void)
{
	int rv;

	sb_t *sb = sb_create();
	CU_ASSERT_PTR_NOT_EQUAL_FATAL(sb, NULL);

	// Set a few bits before a mask boundary and then a few
	// contiguous bits starting at the mask boundary.
	sb_idx_t boundary = 0x2000;
	int offsets[] = {-5, -3, -2, 0, 1, 2, 3, 4};
	int offset_clear = 2;
	for (unsigned int n = 0; n < NUM(offsets); n++) {
		rv = sb_set(sb, boundary + offsets[n]);
		CU_ASSERT_EQUAL_FATAL(rv, 0);
	}
	rv = sb_clear(sb, boundary + offset_clear);
	CU_ASSERT_EQUAL_FATAL(rv, 0);

	sb_num_t num = sb_nodes_num(sb);
	CU_ASSERT_EQUAL(num, 2);

	sb_pdynstr_t err_string = sb_validate(sb);
	if (err_string) {
		fprintf(stderr, "Sparsebit Validate Failed: %s\n", err_string);
		fputs(sb_dump(sb), stderr);
		CU_FAIL("Sparsebit - Inconsistent Internal State");
		sb_dynstr_free(err_string);
	}

	sb_delete(sb);
}

void test_set_after_then_clear_mask(void)
{
	int rv;

	sb_t *sb = sb_create();
	CU_ASSERT_PTR_NOT_EQUAL_FATAL(sb, NULL);

	// Set a single bit before a mask boundary
	// and then a few contiguous bits starting at the mask boundary.
	// The bits starting at the mask boundary will get described by
	// the nodes set_after value.
	sb_idx_t boundary = 0x2000;
	int offsets[] = {-4, 0, 1, 2};
	int offset_clear = -4;
	for (unsigned int n = 0; n < NUM(offsets); n++) {
		rv = sb_set(sb, boundary + offsets[n]);
		CU_ASSERT_EQUAL_FATAL(rv, 0);
		sb_pdynstr_t err_string = sb_validate(sb);
		if (err_string) {
			fprintf(stderr, "Sparsebit Validate Failed: %s\n", err_string);
			fputs(sb_dump(sb), stderr);
			CU_FAIL_FATAL("Sparsebit - Inconsistent Internal State");
			sb_dynstr_free(err_string);
		}
	}
	rv = sb_clear(sb, boundary + offset_clear);
	CU_ASSERT_EQUAL_FATAL(rv, 0);

	sb_num_t num = sb_nodes_num(sb);
	CU_ASSERT_EQUAL(num, 1);

	sb_pdynstr_t err_string = sb_validate(sb);
	if (err_string) {
		fprintf(stderr, "Sparsebit Validate Failed: %s\n", err_string);
		fputs(sb_dump(sb), stderr);
		CU_FAIL("Sparsebit - Inconsistent Internal State");
		sb_dynstr_free(err_string);
	}

	sb_delete(sb);
}

// Performs a sb_clearnum(), within a set of acjacent nodes, such
// that the sb_clearnum() is performed where:
//   + Start of sb_clearnum() range is in gap just before node with
//     mask that will be cleared.
//   + End of mask to be cleared is beyond highest set bit within the
//     mask, but also still within that mask (e.g. before set_after
//     bits of the node).
//   + At least one bit described by the set_after value of the node
//     whoses mask will be cleared.
void test_adjacent_clear_across_mask(void)
{
	int rv;
	sb_pdynstr_t err_string;
	sb_num_t expected_nodes_num, actual_nodes_num;

	size_t groups_num = 8;
	sb_idx_t groups_idx_start = 0x1a75000;
	sb_num_t groups_idx_stride = sb_mask_bits_min();

	// Index of where to perform sb_clear(), within the chain of
	// adjacent nodes.  Plus offset, from where sb_clear() was performed,
	// and length for a sb_clearnum() operations.
	size_t idx_clear = groups_idx_start + 3 * groups_idx_stride;
	ssize_t clearnum_offset = -1;
	sb_num_t clearnum_length = 0x42;

	sb_t *sb = sb_create();
	CU_ASSERT_PTR_NOT_EQUAL_FATAL(sb, NULL);

	for (unsigned int n = 0; n < groups_num; n++) {
		sb_idx_t idx = groups_idx_start + n * groups_idx_stride;

		rv = sb_set(sb, idx);
		CU_ASSERT_EQUAL_FATAL(rv, 0);
		err_string = sb_validate(sb);
		if (err_string) {
			fprintf(stderr, "Sparsebit Validate Failed001: %s\n", err_string);
			fputs(sb_dump(sb), stderr);
			CU_FAIL_FATAL("Sparsebit - Inconsistent Internal State");
			sb_dynstr_free(err_string);
		}
	}
	expected_nodes_num = groups_num / 2 + (groups_num % 2);
	actual_nodes_num = sb_nodes_num(sb);
	if (actual_nodes_num != expected_nodes_num) {
		fprintf(stderr, "Unexpected number of nodes001:\n"
			"  actual: %#" SB_PRI_IDX " expected: %#" SB_PRI_IDX "\n",
			actual_nodes_num, expected_nodes_num);
		fputs(sb_dump(sb), stderr);
		CU_FAIL("Unexpected number of nodes001");
	}

	rv = sb_clear(sb, idx_clear);
	CU_ASSERT_EQUAL_FATAL(rv, 0);
	err_string = sb_validate(sb);
	if (err_string) {
		fprintf(stderr, "Sparsebit Validate Failed003: %s\n", err_string);
		fputs(sb_dump(sb), stderr);
		CU_FAIL_FATAL("Sparsebit - Inconsistent Internal State");
		sb_dynstr_free(err_string);
	}
	// Mask bit cleared within node that also has a set_after bit, so
	// number of nodes should not change.
	expected_nodes_num += 0;
	actual_nodes_num = sb_nodes_num(sb);
	if (actual_nodes_num != expected_nodes_num) {
		fprintf(stderr, "Unexpected number of nodes002:\n"
			"  actual: %#" SB_PRI_IDX " expected: %#" SB_PRI_IDX "\n",
			actual_nodes_num, expected_nodes_num);
		fputs(sb_dump(sb), stderr);
		CU_FAIL("Unexpected number of nodes002");
	}

	rv = sb_clearnum(sb, idx_clear + clearnum_offset, clearnum_length);
	CU_ASSERT_EQUAL_FATAL(rv, 0);
	err_string = sb_validate(sb);
	if (err_string) {
		fprintf(stderr, "Sparsebit Validate Failed002: %s\n", err_string);
		fputs(sb_dump(sb), stderr);
		CU_FAIL_FATAL("Sparsebit - Inconsistent Internal State");
		sb_dynstr_free(err_string);
	}
	// The sb_clearnum() was done from a gap before a node's mask bits, to
	// a bit beyond the bits set within the node's mask, yet still in that
	// mask.  The node where the mask bits are cleared, also has set_after
	// bits, which are not cleared, so number of nodes should not change.
	expected_nodes_num += 0;
	actual_nodes_num = sb_nodes_num(sb);
	if (actual_nodes_num != expected_nodes_num) {
		fprintf(stderr, "Unexpected number of nodes003:\n"
			"  actual: %#" SB_PRI_IDX " expected: %#" SB_PRI_IDX "\n",
			actual_nodes_num, expected_nodes_num);
		fputs(sb_dump(sb), stderr);
		CU_FAIL("Unexpected number of nodes003");
	}

	sb_delete(sb);
}

void test_combinations4(void)
{
	struct range {
		sb_idx_t idx;
		sb_num_t num;
	};
	struct range ranges[] = {
		{0x1000, 1},
		{0x1001, 1},
		{0x1002, 60},
		{0x103e, 1},
		{0x103f, 1},
		{0x1040, 1},
		{0x1041, 1},
		{0x1042, 60},
		{0x107e, 1},
		{0x107f, 1},
		{0x1080, 1},
		{0x1081, 1},
		{0x1082, 60},
		{0x10be, 1},
		{0x10bf, 1},
	};

	sb_idx_t range_min, range_max;
	for (unsigned int n = 0; n < NUM(ranges); n++) {
		if ((n == 0) || (ranges[n].idx < range_min)) {
			range_min = ranges[n].idx;
		}
		if ((n == 0) || ((ranges[n].idx + ranges[n].num - 1) > range_max)) {
			range_max = ranges[n].idx + ranges[n].num - 1;
		}
	}
	bool *expected = malloc(sizeof(bool) * ((range_max - range_min) + 1));

	struct op {
		bool do_set;
		unsigned int range_idx;
	} operations[4];


	MEMCLEAR(operations, sizeof(operations));

	do {
		MEMCLEAR(expected, sizeof(bool) * ((range_max - range_min) + 1));
		sb_t *sb = sb_create();

		// Perform Operations
		for (unsigned int n1 = 0; n1 < NUM(operations); n1++) {
			struct range *prange = &ranges[operations[n1].range_idx];
			if (operations[n1].do_set) {
				sb_setnum(sb, prange->idx, prange->num);
				for (unsigned int n2 = 0; n2 < prange->num; n2++) {
					expected[prange->idx - range_min + n2] = true;
				} 
			} else {
				sb_clearnum(sb, prange->idx, prange->num);
				for (unsigned int n2 = 0; n2 < prange->num; n2++) {
					expected[prange->idx - range_min + n2] = false;
				}
			}

			sb_pdynstr_t err_string = sb_validate(sb);
			if (err_string) {
				fprintf(stderr, "Sparsebit Validate Failed: %s\n", err_string);
				fputs("Operations:\n", stderr);
				for (unsigned int op_idx = 0; op_idx < NUM(operations); op_idx++) {
					fprintf(stderr, "  %u %s idx: %#" SB_PRI_IDX
						" num: %#" SB_PRI_NUM "\n",
						op_idx, operations[op_idx].do_set ? "SET_NUM" : "CLEAR_NUM",
						ranges[operations[op_idx].range_idx].idx,
						ranges[operations[op_idx].range_idx].num);
				}
				fputs(sb_dump(sb), stderr);
				CU_FAIL_FATAL("Sparsebit - Inconsistent Internal State");
				sb_dynstr_free(err_string);
			}
		}

		// Validate setting of each bit, along with nextset and nextcleared,
		// from each bit in the ranges.
		for (unsigned int n1 = 0; n1 < (range_max - range_min + 1); n1++) {
			sb_idx_t idx = range_min + n1;

			bool expected_isset = expected[n1];

			sb_idx_t expected_nextset = 0;
			unsigned int n2;
			for (n2 = (idx - range_min) + 1; n2 < (range_max - range_min + 1); n2++) {
				if (expected[n2]) {
					expected_nextset = range_min + n2;
					break;
				}
			}

			sb_idx_t expected_nextcleared = 0;
			for (n2 = (idx - range_min) + 1; n2 < (range_max - range_min + 1); n2++) {
				if (!expected[n2]) {
					expected_nextcleared = range_min + n2;
					break;
				}
			}
			if (n2 == (range_max - range_min + 1)) {
				expected_nextcleared = range_max + 1;
			}

			bool actual_isset = sb_isset(sb, idx);
			if (actual_isset != expected_isset) {
				fputs("Error - Unexpected bit setting\n", stderr);
				fputs("Operations:\n", stderr);
				for (unsigned int op_idx = 0; op_idx < NUM(operations); op_idx++) {
					fprintf(stderr, "  %u %s idx: %#" SB_PRI_IDX
						" num: %#" SB_PRI_NUM "\n",
						op_idx, operations[op_idx].do_set ? "SET_NUM" : "CLEAR_NUM",
						ranges[operations[op_idx].range_idx].idx,
						ranges[operations[op_idx].range_idx].num);
				}
				fprintf(stderr, "idx: %#" SB_PRI_IDX
					" actual_isset: %u expected_isset: %u\n",
					idx, actual_isset, expected_isset);
				fputs(sb_dump(sb), stderr);

				CU_FAIL_FATAL("Unexpected Bit Setting");
			}

			sb_idx_t actual_nextset = sb_nextset(sb, idx);
			if (actual_nextset != expected_nextset) {
				fputs("Error - Unexpected next set\n", stderr);
				fputs("Operations:\n", stderr);
				for (unsigned int op_idx = 0; op_idx < NUM(operations); op_idx++) {
					fprintf(stderr, "  %u %s idx: %#" SB_PRI_IDX
						" num: %#" SB_PRI_NUM "\n",
						op_idx, operations[op_idx].do_set ? "SET_NUM" : "CLEAR_NUM",
						ranges[operations[op_idx].range_idx].idx,
						ranges[operations[op_idx].range_idx].num);
				}
				fprintf(stderr,
					"idx: %#" SB_PRI_IDX " actual_nextset: %#" SB_PRI_IDX
					" expected_nextset: %#" SB_PRI_IDX "\n",
					idx, actual_nextset, expected_nextset);
				fputs(sb_dump(sb), stderr);

				CU_FAIL_FATAL("Unexpected Next Set Index");
			}

			sb_idx_t actual_nextcleared = sb_nextcleared(sb, idx);
			if (actual_nextcleared != expected_nextcleared) {
				fputs("Error - Unexpected next cleared\n", stderr);
				fputs("Operations:\n", stderr);
				for (unsigned int op_idx = 0; op_idx < NUM(operations); op_idx++) {
					fprintf(stderr, "  %u %s idx: %#" SB_PRI_IDX
						" num: %#" SB_PRI_NUM "\n",
						op_idx, operations[op_idx].do_set ? "SET_NUM" : "CLEAR_NUM",
						ranges[operations[op_idx].range_idx].idx,
						ranges[operations[op_idx].range_idx].num);
				}
				fprintf(stderr,
					"idx: %#" SB_PRI_IDX " actual_nextcleared: %#" SB_PRI_IDX
					" expected_nextcleared: %#" SB_PRI_IDX "\n",
					idx, actual_nextcleared, expected_nextcleared);
				fputs(sb_dump(sb), stderr);

				CU_FAIL_FATAL("Unexpected Next Cleared Index");
			}
		}

		sb_delete(sb);

		// Increment to next combination of operations.
		unsigned int n;
		for (n = 0; n < NUM(operations); n++) {
			operations[n].range_idx++;
			if (operations[n].range_idx >= NUM(ranges)) {
				operations[n].range_idx = 0;
				if (operations[n].do_set == false) {
					operations[n].do_set = true;
				} else {
					operations[n].do_set = false;
					break;
				}
			} else {
				break;
			}
		}
		if (n == NUM(operations)) {
			break;
		}
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
	// TODO: Optionally use positional command-line arguments, that are
	//       regular expressions, to specify which tests are to be added.

	if ((CU_add_test(pSuite, "create_delete", test_create_delete) == NULL)
		|| (CU_add_test(pSuite, "set", test_set) == NULL)
		|| (CU_add_test(pSuite, "clear", test_clear) == NULL)
		|| (CU_add_test(pSuite, "multinode", test_multinode) == NULL)
		|| (CU_add_test(pSuite, "clear_two_nodes", test_clear_two_nodes) == NULL)
		|| (CU_add_test(pSuite, "first_next_set", test_first_next_set) == NULL)
		|| (CU_add_test(pSuite, "first_next_cleared", test_first_next_cleared)
			== NULL)
		|| (CU_add_test(pSuite, "permutations6", test_permutations6) == NULL)
		|| (CU_add_test(pSuite, "set_after_single", test_set_after_single) == NULL)
		|| (CU_add_test(pSuite, "set_after_clear_single",
			test_set_after_clear_single) == NULL)
		|| (CU_add_test(pSuite, "set_after_then_clear_mask",
			test_set_after_then_clear_mask) == NULL)
		|| (CU_add_test(pSuite, "adjacent_clear_across_mask",
			test_adjacent_clear_across_mask) == NULL)
		|| (CU_add_test(pSuite, "combinations4", test_combinations4) == NULL)
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
