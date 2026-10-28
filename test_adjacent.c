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
#include <ctype.h>
#include <errno.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>

#define DEFAULT_NODES_NUM (3)
#define DEFAULT_EDGE_MARGIN (1)
#define MASK_BITS (64)
#define NODES_BASE_IDX (0x3a000)

#define MEMCLEAR(p, n) memset((p), 0, (n))
#define NUM(a) (sizeof(a)/sizeof((a)[0]))

enum MASK_TYP {
	MASK0 = 0,
	MASK1_LEADING,
	MASK2_LEADING,
	MASK2_TRAILING,
	MASK1_TRAILING,
	MASK1_CLEARED,
};
struct mask_typ_attr {
	enum MASK_TYP typ;
	char *str;
	uint64_t val;
} mask_typ_attrs[] = {
	{MASK0, "Zero", 0x0},
	{MASK1_LEADING, "1Leading", 0x1},
	{MASK2_LEADING, "2Leading", 0x3},
	{MASK2_TRAILING, "2Trailing", 0xC000000000000000llu},
	{MASK1_TRAILING, "1Trailing", 0x8000000000000000llu},
	{MASK1_CLEARED, "1Cleared", 0xFFFFFFFFFDFFFFFFllu},
};
#define MASK_TYP_FIRST (mask_typ_attrs[0].typ)
#define MASK_TYP_LAST (mask_typ_attrs[NUM(mask_typ_attrs) - 1].typ)
#define MASK_TYP_NUM (NUM(mask_typ_attrs))

enum SETAFTER_TYP {
	SETAFTER0 = 0,
	SETAFTER1,
	SETAFTER2,
	SETAFTER63,
	SETAFTER64,
};
struct setafter_typ_attr {
	enum SETAFTER_TYP typ;
	sb_num_t val; 
} setafter_typ_attrs[] = {
	{SETAFTER0, 0},
	{SETAFTER1, 1},
	{SETAFTER2, 2},
	{SETAFTER63, 63},
	{SETAFTER64, 64},
};
#define SETAFTER_TYP_FIRST (setafter_typ_attrs[0].typ)
#define SETAFTER_TYP_LAST (setafter_typ_attrs[NUM(setafter_typ_attrs) - 1].typ)
#define SETAFTER_TYP_NUM (NUM(setafter_typ_attrs))

struct node {
	// Type/Counter values
	enum MASK_TYP mask_typ;
	enum SETAFTER_TYP set_after_typ;

	// Node attributes, based on counter values.
	sb_idx_t idx_start;
	uint64_t mask;
	sb_num_t set_after;
};
struct node *nodes = NULL;
unsigned long nodes_num = DEFAULT_NODES_NUM;
unsigned long edge_margin = DEFAULT_EDGE_MARGIN;

static void nodes_setup(void);
static sb_t *node_sb_create(sb_pdynstr_t *p_ops);
static int range_set(sb_idx_t start, sb_idx_t end,
	sb_t **p_sb, sb_pdynstr_t *p_ops);
static sb_idx_t *nodes_tst_indexes(ssize_t margin, size_t *pnum);
static bool nodes_expected_isset(sb_idx_t idx);
static sb_idx_t *dyn_add_index_unique(sb_idx_t idx, ssize_t offset,
sb_idx_t *indexes, size_t *pnum);
static int idx_cmp(const void *elem1, const void *elem2);
static void usage(FILE *stream);
static void help(FILE *stream);

int main(int argc, char *argv[])
{
	int rv;
	sb_pdynstr_t err_string;

	// Command-line Parsing
	static const struct option options[] = {
		{"nodes_num", required_argument, 0, 'n'},
		{"edge_margin", required_argument, 0, 'm'},
		{} // End-of-Array Marker
	};
	int opt;
	bool error;
	char *endptr;
	while ((opt = getopt_long(argc, argv, "n:m:h?", options, NULL)) != EOF) {
		switch (opt) {
		case 'n':
			nodes_num = strtoul(optarg, &endptr, 10);
			if ((*endptr != '\0') || (strchr(optarg, '-') != NULL)) {
				fprintf(stderr, "Invalid --nodes_num value of: %s\n", optarg);
				exit(20);
			}
			break;

		case 'm':
			edge_margin = strtoul(optarg, &endptr, 10);
			if ((*endptr != '\0') || (strchr(optarg, '-') != NULL)) {
				fprintf(stderr, "Invalid --edge_margin value of: %s\n", optarg);
				exit(21);
			}
			break;

		case 'h':
		case '?':
		default:
			error = ((optopt != 0) && (optopt != '?') && (optopt != 'h'));
			if (error) {
				usage(stderr);
			} else {
				help(stdout);
				return 0;
			}
			exit(22);
		}
	}
	if ((argc - optind) > 0) {
		fputs("Unexpected positional command-line arguments", stderr);
		exit(23);
	}
	printf("nodes_num: %lu\n", nodes_num);
	printf("edge_margin: %lu\n", edge_margin);
	unsigned long long iter_total = 1;
	for (size_t i = 0; i < nodes_num; i++) {
		iter_total *= (MASK_TYP_NUM * SETAFTER_TYP_NUM);
	}
	printf("total_iterations: %llu\n", iter_total);

	nodes = malloc(nodes_num * sizeof(*nodes));
	if (nodes == NULL) {
		fputs("Insufficient Memory", stderr);
		exit(22);
	}

	bool first_pass = true;
	unsigned long long iter;
	for (iter = 0; ; iter++) {
		if ((iter % 1000) == 0) {
			printf("iteration %llu of %llu\n", iter, iter_total);
		}

		if (first_pass) {
			MEMCLEAR(nodes, sizeof(*nodes) * nodes_num);
			first_pass = false;
		} else {
			// Increment to next combination.
			struct node *nodep;
			for (nodep = nodes; nodep < (nodes + nodes_num); nodep++) {
				nodep->set_after_typ++;
				if (nodep->set_after_typ > SETAFTER_TYP_LAST) {
					nodep->set_after_typ = SETAFTER_TYP_FIRST;
					nodep->mask_typ++;
					if (nodep->mask_typ > MASK_TYP_LAST) {
						nodep->mask_typ = MASK_TYP_FIRST;
						continue;
					}
				}
				break;
			}
			assert(nodep <= nodes + nodes_num);
			if (nodep == (nodes + nodes_num)) {
				break;
			}
		}

		bool skip = false;
		// Skip when all mask and set-after are zero.
		struct node *nodep;
		for (nodep = nodes; nodep < (nodes + nodes_num); nodep++) {
			if ((nodep->mask_typ != MASK0)
				|| (setafter_typ_attrs[nodep->set_after_typ].val != 0)) {
				break;
			}
		}
		if (nodep == (nodes + nodes_num)) {
			skip = true;
			continue;
		}

		// Skip cases where mask of zero is before a non-zero mask.
		bool any_mask0 = false;
		for (nodep = nodes; nodep < (nodes + nodes_num); nodep++) {
			if (nodep->mask_typ == MASK0) {
				any_mask0 = true;
			} else {
				if (any_mask0) {
					skip = true;
					break;
				}
			}
		}
		if (skip) { continue; }

		// Skip cases with mask of zero and a non-zero set-after value.
		for (nodep = nodes; nodep < (nodes + nodes_num); nodep++) {
			if ((nodep->mask_typ == MASK0)
				&& (setafter_typ_attrs[nodep->set_after_typ].val != 0)) {
				skip = true;
				break;
			}
		}
		if (skip) { continue; }

		// Skip cases of only leading mask bits and previous node
		// has set-after that is divisable by the mask size.
		for (nodep = nodes + 1; nodep < (nodes + nodes_num); nodep++) {
			if ((strstr(mask_typ_attrs[nodep->mask_typ].str, "Leading") != NULL)
				&& ((setafter_typ_attrs[(nodep - 1)->set_after_typ].val % MASK_BITS)
				== 0)) {
				skip = true;
				break;
			}
		}
		if (skip) { continue; }

		nodes_setup();

		sb_pdynstr_t ops = sb_dyn_sprintf("");
		if (ops == NULL) {
			fprintf(stderr, "Error: Ops string initialization, errno: %i\n", errno);
			exit(30);
		}
		sb_t *sb_initial = node_sb_create(&ops);
		if (sb_initial == NULL) {
			fprintf(stderr, "Error: sb_initial creation, errno: %i\n", errno);
			exit(31);
		}

		size_t tst_indexes_num;
		sb_idx_t *tst_indexes = nodes_tst_indexes(edge_margin, &tst_indexes_num);

		// For each test index.
		for (size_t i1 = 0; i1 < tst_indexes_num; i1++) {
			sb_idx_t idx1 = *(tst_indexes + i1);

			// ==== Perform and validate sb_set() ====
			sb_t *sb_op_set = sb_create();
			if (sb_op_set == NULL) {
				fprintf(stderr, "Error: sb_op_set creation, errno: %i\n", errno);
				exit(40);
			}
			rv = sb_copy(sb_op_set, sb_initial);
			if (rv != 0) {
				fprintf(stderr, "Error: sb_copy sb_initial to sb_op_set, errno: %i\n",
					errno);
				exit(41);
			}
			err_string = sb_validate(sb_op_set);
			if (err_string) {
				printf("FAILED: Validation Error - "
				"Inconsitent Internal State:\n%s\n", err_string);
				exit(44);
			}
			sb_set(sb_op_set, idx1);
			err_string = sb_validate(sb_op_set);
			if (err_string) {
				fprintf(stderr, "FAILED: Validation Error - "
				"Inconsitent Internal State:\n%s\n", err_string);
				fprintf(stderr, "Operations:\n%s"
					"  SET_BIT idx: %#" SB_PRI_IDX "\n", ops, idx1);
				fputs(sb_dump(sb_op_set), stderr);
				exit(42);
			}
			for (size_t i = 0; i < tst_indexes_num; i++) {
				sb_idx_t idx_check = *(tst_indexes + i);
				bool expected = (idx_check == idx1) ? true
					: nodes_expected_isset(idx_check);
				bool actual = sb_isset(sb_op_set, idx_check);
				if (actual != expected) {
					fprintf(stderr, "FAILED: Validation Error - "
					"Unexpected bit setting:\n"
					"  idx: %#" SB_PRI_IDX " actual: %u expected: %u\n",
					idx_check, actual, expected);
					fprintf(stderr, "Operations:\n%s"
						"  SET_BIT idx: %#" SB_PRI_IDX "\n", ops, idx1);
					fputs(sb_dump(sb_op_set), stderr);
					exit(43);
				}
			}
			sb_delete(sb_op_set);

			// ==== Perform and validate sb_clear() ====
			sb_t *sb_op_clear = sb_create();
			if (sb_op_clear == NULL) {
				fprintf(stderr, "Error: sb_op_clear creation, errno: %i\n", errno);
				exit(50);
			}
			rv = sb_copy(sb_op_clear, sb_initial);
			if (rv != 0) {
				fprintf(stderr, "Error: sb_copy sb_initial to sb_op_clear, errno: %i\n",
					errno);
				exit(51);
			}
			err_string = sb_validate(sb_op_clear);
			if (err_string) {
				printf("FAILED: Validation Error - "
				"Inconsitent Internal State:\n%s\n", err_string);
				exit(54);
			}
			sb_clear(sb_op_clear, idx1);
			err_string = sb_validate(sb_op_clear);
			if (err_string) {
				fprintf(stderr, "FAILED: Validation Error - "
				"Inconsitent Internal State:\n%s\n", err_string);
				fprintf(stderr, "Operations:\n%s"
					"  CLEAR_BIT idx: %#" SB_PRI_IDX "\n", ops, idx1);
				fputs(sb_dump(sb_op_clear), stderr);
				exit(52);
			}
			for (size_t i = 0; i < tst_indexes_num; i++) {
				sb_idx_t idx_check = *(tst_indexes + i);
				bool expected = (idx_check == idx1) ? false
					: nodes_expected_isset(idx_check);
				bool actual = sb_isset(sb_op_clear, idx_check);
				if (actual != expected) {
					fprintf(stderr, "FAILED: Validation Error - "
					"Unexpected bit setting:\n"
					"  idx: %#" SB_PRI_IDX " actual: %u expected: %u\n",
					idx_check, actual, expected);
					fprintf(stderr, "Operations:\n%s"
						"  CLEAR_BIT idx: %#" SB_PRI_IDX "\n", ops, idx1);
					fputs(sb_dump(sb_op_set), stderr);
					exit(53);
				}
			}
			sb_delete(sb_op_clear);

			// For each range of indexes.
			for (size_t i2 = i1; i2 < tst_indexes_num; i2++) {
				sb_idx_t idx2 = *(tst_indexes + i2);
				sb_num_t num = (idx2 - idx1) + 1;

				// ==== Perform and validate sb_setnum() ====
				sb_t *sb_op_setnum = sb_create();
				if (sb_op_setnum == NULL) {
					fprintf(stderr, "Error: sb_op_setnum creation, errno: %i\n", errno);
					exit(60);
				}
				rv = sb_copy(sb_op_setnum, sb_initial);
				if (rv != 0) {
					fprintf(stderr, "Error: sb_copy sb_initial to sb_op_setnum, "
						"errno: %i\n", errno);
					exit(61);
				}
				err_string = sb_validate(sb_op_setnum);
				if (err_string) {
					printf("FAILED: Validation Error - "
					"Inconsitent Internal State:\n%s\n", err_string);
					exit(64);
				}
				sb_setnum(sb_op_setnum, idx1, num);
				err_string = sb_validate(sb_op_setnum);
				if (err_string) {
					fprintf(stderr, "FAILED: Validation Error - "
					"Inconsitent Internal State:\n%s\n", err_string);
					fprintf(stderr, "Operations:\n%s"
						"  SET_NUM idx: %#" SB_PRI_IDX " num: %#" SB_PRI_NUM "\n", ops,
						idx1, num);
					fputs(sb_dump(sb_op_setnum), stderr);
					exit(62);
				}
				for (size_t i = 0; i < tst_indexes_num; i++) {
					sb_idx_t idx_check = *(tst_indexes + i);
					bool expected = ((idx_check >= idx1)
						&& (idx_check <= idx2)) ? true
						: nodes_expected_isset(idx_check);
					bool actual = sb_isset(sb_op_setnum, idx_check);
					if (actual != expected) {
						fprintf(stderr, "FAILED: Validation Error - "
						"Unexpected bit setting:\n"
						"  idx: %#" SB_PRI_IDX " actual: %u expected: %u\n",
						idx_check, actual, expected);
						fprintf(stderr, "Operations:\n%s"
							"  SET_NUM idx: %#" SB_PRI_IDX " num: %#" SB_PRI_NUM "\n", ops,
							idx1, num);
						fputs(sb_dump(sb_op_set), stderr);
						exit(63);
					}
				}
				sb_delete(sb_op_setnum);

				// ==== Perform and validate sb_clearnum() ====
				sb_t *sb_op_clearnum = sb_create();
				if (sb_op_clearnum == NULL) {
					fprintf(stderr, "Error: sb_op_clearnum creation, errno: %i\n", errno);
					exit(70);
				}
				rv = sb_copy(sb_op_clearnum, sb_initial);
				if (rv != 0) {
					fprintf(stderr, "Error: sb_copy sb_initial to sb_op_clearnum, "
						"errno: %i\n", errno);
					exit(71);
				}
				err_string = sb_validate(sb_op_clearnum);
				if (err_string) {
					printf("FAILED: Validation Error - "
					"Inconsitent Internal State:\n%s\n", err_string);
					exit(74);
				}
				sb_clearnum(sb_op_clearnum, idx1, num);
				err_string = sb_validate(sb_op_clearnum);
				if (err_string) {
					fprintf(stderr, "FAILED: Validation Error - "
					"Inconsitent Internal State:\n%s\n", err_string);
					fprintf(stderr, "Operations:\n%s"
						"  CLEAR_NUM idx: %#" SB_PRI_IDX " num: %#" SB_PRI_NUM "\n", ops,
						idx1, num);
					fputs(sb_dump(sb_op_clearnum), stderr);
					exit(72);
				}
				for (size_t i = 0; i < tst_indexes_num; i++) {
					sb_idx_t idx_check = *(tst_indexes + i);
					bool expected = ((idx_check >= idx1)
						&& (idx_check <= idx2)) ? false
						: nodes_expected_isset(idx_check);
					bool actual = sb_isset(sb_op_clearnum, idx_check);
					if (actual != expected) {
						fprintf(stderr, "FAILED: Validation Error - "
						"Unexpected bit setting:\n"
						"  idx: %#" SB_PRI_IDX " actual: %u expected: %u\n",
						idx_check, actual, expected);
						fprintf(stderr, "Operations:\n%s"
							"  CLEAR_NUM idx: %#" SB_PRI_IDX " num: %#" SB_PRI_NUM "\n", ops,
							idx1, num);
						fputs(sb_dump(sb_op_set), stderr);
						exit(73);
					}
				}
				sb_delete(sb_op_clearnum);
			}
		}

		free(tst_indexes);
		sb_delete(sb_initial);
	}

	return 0;
}

// For each node, sets its values based on the mask_typ and set_after_typ,
// counter values.
static void nodes_setup(void)
{
	// Set mask and set_after values, in each node.
	for (struct node *nodep = nodes; nodep < (nodes + nodes_num); nodep++) {
		nodep->mask = mask_typ_attrs[nodep->mask_typ].val;
		nodep->set_after = setafter_typ_attrs[nodep->set_after_typ].val;
	}

	// Determine the start index of each node.
	sb_idx_t idx = NODES_BASE_IDX;
	for (struct node *nodep = nodes; nodep < (nodes + nodes_num); nodep++) {
		if (nodep->mask_typ != MASK0) {
			nodep->idx_start = idx;
		} else {
			break;
		}
		idx += MASK_BITS;
		sb_num_t setafter = setafter_typ_attrs[nodep->set_after_typ].val;
		if (setafter != 0) {
			idx += setafter;
			if (setafter % MASK_BITS) {
				idx += MASK_BITS - (setafter % MASK_BITS);
			}
		}
	}
}

static sb_t *node_sb_create(sb_pdynstr_t *p_ops)
{
	sb_t *sb = sb_create();
	if (sb == NULL) {
		return sb;
	}

	bool within_range = false;  // True when within a range of set bits,
	                            // Searching for the end of the range.
	sb_idx_t idx_start, idx_end;  // Valid only when within_range is true.
	for (struct node *nodep = nodes; nodep < (nodes + nodes_num); nodep++) {
		if (nodep->mask == MASK0) {
			break;
		}

		// Handle node mask bits.
		for (size_t mask_idx = 0; mask_idx < MASK_BITS; mask_idx++) {
			if (nodep->mask & (1llu << mask_idx)) {
				if (!within_range) {
					idx_start = idx_end = nodep->idx_start + mask_idx;
					within_range = true;
				} else {
					idx_end = nodep->idx_start + mask_idx;
				}
			} else {
				if (within_range) {
					int rv = range_set(idx_start, idx_end, &sb, p_ops);
					if (rv != 0) {
						fprintf(stderr, "range_set failed, errno: %i\n", errno);
					}
					within_range = false;
				}
			}
		}
		if (within_range && (nodep->set_after == 0)) {
			int rv = range_set(idx_start, idx_end, &sb, p_ops);
			if (rv != 0) {
				fprintf(stderr, "range_set failed, errno: %i\n", errno);
			}
			within_range = false;
		}

		// If needed, handle node set_after.
		if (nodep->set_after != 0) {
			if (!within_range) {
				idx_start = nodep->idx_start + MASK_BITS;
				within_range = true;
			}
			idx_end = nodep->idx_start + MASK_BITS - 1 + nodep->set_after;
			// Is there a gap between this and the next node?
			if (((nodep + 1) < (nodes + nodes_num))
				&& ((idx_end) != (nodep->idx_start - 1))) {
				int rv = range_set(idx_start, idx_end, &sb, p_ops);
				if (rv != 0) {
					fprintf(stderr, "range_set failed, errno: %i\n", errno);
				}
				within_range = false;
			}
		}
	}
	if (within_range) {
		int rv = range_set(idx_start, idx_end, &sb, p_ops);
		if (rv != 0) {
			fprintf(stderr, "range_set failed, errno: %i\n", errno);
		}
		within_range = false;
	}

	return sb;
}

static int range_set(sb_idx_t start, sb_idx_t end,
	sb_t **p_sb, sb_pdynstr_t *p_ops)
{
	int rv;
	sb_pdynstr_t ops;

	if (start == end) {
		// Set a single bit.
		rv = sb_set(*p_sb, start);
		if (rv != 0) {
			sb_delete(*p_sb);
			*p_sb = NULL;
			sb_dynstr_free(*p_ops);
			*p_ops = NULL;
			return -1;
		}
		ops = sb_dyn_sprintf("%s  SET_BIT idx: %#" SB_PRI_IDX "\n", *p_ops,
				start);
		if (ops == NULL) {
			sb_delete(*p_sb);
			*p_sb = NULL;
			sb_dynstr_free(*p_ops);
			*p_ops = NULL;
			errno = ENOMEM;
			return -1;
		}
		sb_dynstr_free(*p_ops);
		*p_ops = ops;
	} else {
		// Set a range of bits.
		rv = sb_setnum(*p_sb, start, end - start + 1);
		if (rv != 0) {
			sb_delete(*p_sb);
			*p_sb = NULL;
			sb_dynstr_free(*p_ops);
			*p_ops = NULL;
			return -1;
		}
		ops = sb_dyn_sprintf("%s  SET_NUM idx: %#" SB_PRI_IDX
			" num: %#" SB_PRI_NUM "\n", *p_ops, start, end - start + 1);
		if (ops == NULL) {
			sb_delete(*p_sb);
			*p_sb = NULL;
			sb_dynstr_free(*p_ops);
			*p_ops = NULL;
			errno = ENOMEM;
			return -1;
		}
		sb_dynstr_free(*p_ops);
		*p_ops = ops;
	}

	return 0;
}

static sb_idx_t *nodes_tst_indexes(ssize_t margin, size_t *pnum)
{
	sb_idx_t *indexes = NULL;
	size_t num = 0;

	for (struct node *nodep = nodes; nodep < (nodes + nodes_num); nodep++) {
		if (nodep->mask == MASK0) {
			break;
		}

		sb_idx_t idx = nodep->idx_start;

		// Add indexes around start and end of mask.
		for (ssize_t offset = -margin; offset <= margin; offset++) {
			indexes = dyn_add_index_unique(idx, offset, indexes, &num);
			indexes = dyn_add_index_unique(idx + MASK_BITS - 1, offset,
				indexes, &num);
		}

		// Add indexes for mask bits with settings different from previous
		// mask bit.
		for (size_t i = 1; i < MASK_BITS; i++) {
			if ((bool)(nodep->mask & (1llu << (i - 1)))
				!= (bool)(nodep->mask & (1llu << i))) {
				for (ssize_t offset = -margin; offset <= margin; offset++) {
					indexes = dyn_add_index_unique(idx + i, offset, indexes, &num);
				}
			}
		}

		// If needed add indexes abround end of set_after.
		if (nodep->set_after != 0) {
			for (ssize_t offset = -margin; offset <= margin; offset++) {
				indexes = dyn_add_index_unique(idx + MASK_BITS +
					nodep->set_after - 1, offset, indexes, &num);
			}
		}

	}

	qsort(indexes, num, sizeof(*indexes), idx_cmp);

	*pnum = num;
	return indexes;
}

static bool nodes_expected_isset(sb_idx_t idx)
{
	for (struct node *nodep = nodes; nodep < (nodes + nodes_num); nodep++) {
		if (nodep->mask == MASK0) {
			break;
		}

		if ((idx >= nodep->idx_start)
			&& (idx <= (nodep->idx_start + MASK_BITS - 1))) {
			return (nodep->mask & (1llu << (idx - nodep->idx_start))) ? true : false;
		}
		if (nodep->set_after >= 1) {
			if ((idx >= (nodep->idx_start + MASK_BITS))
				&& (idx <= (nodep->idx_start + MASK_BITS + nodep->set_after - 1))) {
				return true;
			}
		}
	}

	return false;
}

static sb_idx_t *dyn_add_index_unique(sb_idx_t idx, ssize_t offset,
sb_idx_t *indexes, size_t *pnum)
{
	sb_idx_t *ptr;
	sb_idx_t index = idx + offset;

	// Skip if index + offset overflows.
	if (((offset < 0) && (index > idx))
		|| ((offset > 0) && (index < idx))) {
		return indexes;
	}


	// Is idx already present.
	for (ptr = indexes; ptr < (indexes + *pnum); ptr++) {
		if (*ptr == index) {
			// Index to be added is already present, so just need to return
			// address to current index array.
			return indexes;
		}
	}

	// The index is not already present, so grow the array size by 1
	// and add it.
	ptr = realloc(indexes, sizeof(sb_idx_t) * (*pnum + 1));
	if (ptr == NULL) {
		fputs("Insufficient Memory\n", stderr);
		exit(28);
	}
	(*pnum)++;
	*(ptr + (*pnum - 1)) = index;

	return ptr;
}

static int idx_cmp(const void *elem1, const void *elem2)
{
	sb_idx_t idx1 = *((sb_idx_t *)elem1);
	sb_idx_t idx2 = *((sb_idx_t *)elem2);

	if (idx1 > idx2) return 1;
	if (idx1 < idx2) return -1;

	return 0;
}

static void usage(FILE *stream)
{
	fputs("usage: test_adjacent [-n nodes_num] [-m edge_margin] [-h?]\n",
		stream);
}

static void help(FILE *stream)
{
	usage(stream);
	fputs("Sparsebit_c Adjacent Test\n", stream);
	fputs("  -n, --nodes_num=NUM: Number of adjacent nodes\n", stream);
	fputs("  -m, --edge_margin=NUM: Size of edge margin\n", stream);
	fputs("  -?h,--help: Help\n", stream);
}
