/* Copyright 2026 Louis Huemiller Jr.
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

#include <argz.h>  // Included for definition of error_t on Arm based systems.
#include <assert.h>
#include <errno.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEFAULT_PASS_START 0
#define DEFAULT_PASS_END 10000
#define PASSED_DISPLAY_STRIDE 100

#define NUM_TEST_RANGES 6
#define BITS_PER_TEST_RANGE 300
#define PER_PASS_NUM_OPS 1000
#define NTH_IDX_PREFERRED_RANGE 100

#define MEMCLEAR(p, n) memset((p), 0, (n))
#define NUM(a) (sizeof(a)/sizeof((a)[0]))
#define MAX(a, b) (((a) > (b)) ? (a) : (b))

#define USE_DEFAULT_RANGE_THRESHOLD ((int)((float)RAND_MAX * 0.05))
sb_idx_t default_ranges[] = {
	0,
	(SB_IDX_MAX / 2) - (BITS_PER_TEST_RANGE / 2),
	(SB_IDX_MAX - BITS_PER_TEST_RANGE) + 1,
};
#define RAND_IDX() (((rand() & (((sb_idx_t)1 << 32) - 1)) << 32) | rand())

struct test_range {
	sb_idx_t start_idx;
	bool expected[BITS_PER_TEST_RANGE];
};
struct test_range test_ranges[NUM_TEST_RANGES];

enum OP {
	SET_BIT = 132,
	SET_NUM,
	CLEAR_BIT,
	CLEAR_NUM,
	QUERY_ANYSET,
	QUERY_NUMSET,
	QUERY_ISSET,
	QUERY_FIRSTSET,
	QUERY_NEXTSET,
	QUERY_PREVSET,
	QUERY_ANYCLEARED,
	QUERY_NUMCLEARED,
	QUERY_ISCLEARED,
	QUERY_FIRSTCLEARED,
	QUERY_NEXTCLEARED,
	QUERY_PREVCLEARED,
	QUERY_NTHSET,
};
struct op_str {
	enum OP op;
	char *str;
} op_strings[] = {
	{SET_BIT, "SET_BIT"},
	{SET_NUM, "SET_NUM"},
	{CLEAR_BIT, "CLEAR_BIT"},
	{CLEAR_NUM, "CLEAR_NUM"},
	{QUERY_ANYSET, "QUERY_ANYSET"},
	{QUERY_NUMSET, "QUERY_NUMSET"},
	{QUERY_ISSET, "QUERY_ISSET"},
	{QUERY_FIRSTSET, "QUERY_FIRSTSET"},
	{QUERY_NEXTSET, "QUERY_NEXTSET"},
	{QUERY_PREVSET, "QUERY_PREVSET"},
	{QUERY_ANYCLEARED, "QUERY_ANYCLEARED"},
	{QUERY_NUMCLEARED, "QUERY_NUMCLEARED"},
	{QUERY_ISCLEARED, "QUERY_ISCLEARED"},
	{QUERY_FIRSTCLEARED, "QUERY_FIRSTCLEARED"},
	{QUERY_NEXTCLEARED, "QUERY_NEXTCLEARED"},
	{QUERY_PREVCLEARED, "QUERY_PREVCLEARED"},
	{QUERY_NTHSET, "QUERY_NTHSET"},
};
#define OP_TYPE_FIRST (op_strings[0].op)
#define OP_TYPE_LAST (op_strings[NUM(op_strings) - 1].op)
#define OP_TYPE_NUM (OP_TYPE_LAST - OP_TYPE_FIRST + 1)
struct operation {
	unsigned int range_idx;
	enum OP op;
	unsigned int offset;
	sb_num_t num;
	sb_idx_t nth_idx;
};
struct operation operations[PER_PASS_NUM_OPS];

static void usage(FILE *stream);
static void help(FILE *stream);
static void display_passed_passes(long *palready_displayed, long last_passed);
static void display_operations(FILE *stream);
static const char *op_string(enum OP op);
static int range_idx_cmp(const void *elem1, const void *elem2);
static sb_num_t range_numset(unsigned int range_idx);
static sb_idx_t range_firstset(unsigned int range_idx);
static bool range_supports_idx(unsigned int range_idx, sb_idx_t idx);
static bool expected_isset(sb_idx_t idx);
static sb_num_t expected_numset(void);
static sb_idx_t expected_firstset(void);
static sb_idx_t expected_nextset(sb_idx_t start_idx);
static sb_idx_t expected_prevset(sb_idx_t start_idx);
static sb_idx_t expected_firstcleared(void);
static sb_idx_t expected_nextcleared(sb_idx_t start_idx);
static sb_idx_t expected_prevcleared(sb_idx_t start_idx);
static sb_idx_t expected_nthset_rv(sb_idx_t nth_idx);
static void cleanup(void);

char tempfile_path[] = "/tmp/sbit_XXXXXX";
int tempfile_fd = -1;

int main(int argc, char *argv[])
{
	int rv;
	FILE *tempfile;

	// Command-line Parsing
	long pass_start = DEFAULT_PASS_START;
	long pass_end = DEFAULT_PASS_END;
	static const struct option options[] = {
		{} // End-of-Array Marker
	};
	int opt;
	bool error;
	while ((opt = getopt_long(argc, argv, "h?", options, NULL)) != EOF) {
		switch (opt) {
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
			exit(20);
		}
	}
	if ((argc - optind) > 2) {
		fputs("Unexpected positional command-line arguments", stderr);
		exit(21);
	}
	if ((argc - optind) >= 1) {
		char *endptr;
		pass_start = strtol(argv[optind], &endptr, 10);
		if ((endptr == argv[optind]) || (*endptr != '\0') || (pass_start < 0)) {
			fprintf(stderr, "Invalid pass_start of %s\n", argv[optind]);
			exit(22);
		}

		if ((argc - optind) == 2) {
			pass_end = strtol(argv[optind + 1], &endptr, 10);
			if ((endptr == argv[optind + 1]) || (*endptr != '\0') || (pass_end < 0)) {
				fprintf(stderr, "Invalid pass_end of %s\n", argv[optind + 1]);
				exit(23);
			}
		} else {
			pass_end = pass_start;
		}
	}
	printf("pass_start: %lu\n", pass_start);
	printf("pass_end: %lu\n", pass_end);
	puts("");
	if (pass_end < pass_start) {
		fputs("Pass_start must be before pass_end\n", stderr);
		exit(24);
	}

	// Create temp file.
	tempfile_fd = mkstemp(tempfile_path);
	if (tempfile_fd < 0) {
		fprintf(stderr, "Error creating temp file %s, tempfile_fd: %i errno: %i\n",
			tempfile_path, tempfile_fd, errno);
		return(30);
	}
	rv = atexit(cleanup);
	if (rv != 0) {
		fprintf(stderr, "Error registering atexit function, rv: %i errno: %i\n",
			rv, errno);
		return(31);
	}

	long pass, passed_last_displayed = pass_start - 1;
	for (pass = pass_start; pass <= pass_end; pass++) {
		unsigned int num_ops;

		// Per-pass initilization
		srand(pass);
		MEMCLEAR(&test_ranges, sizeof(test_ranges));

		// Pseudo-randomly select the start of the ranges. Typically cause the
		// first three ranges to be at the beginning, across the exact middle, and
		// end of supported range of indexes.  These ranges are more likely to
		// have index specific issues due to warp-around/overflow between
		// the highest to lowest index and improper use of a signed versus
		// unsigned index.
		// bool force_default_ranges = 
		bool use_default_ranges = (rand() >= USE_DEFAULT_RANGE_THRESHOLD)
			? true : false;
		for (unsigned int range_idx = 0; range_idx < NUM(test_ranges);
			range_idx++) {

			bool idx_overlaps = false;
			sb_idx_t idx;
			do {
				idx_overlaps = false;

				idx = ((use_default_ranges)
					&& (range_idx < NUM(default_ranges)))
						? default_ranges[range_idx] : RAND_IDX();

				// Does the chosen starting index desribe a range that extends
				// beyond the maximum supported index?
				if ((((idx + BITS_PER_TEST_RANGE)  - 1) > SB_IDX_MAX)
					|| (((idx + BITS_PER_TEST_RANGE) - 1) < idx) ) {
					idx_overlaps = true;
					continue;
				}

				// Does chosen idx overlap with any of the ranges already setup?
				for (unsigned int range_idx2 = 0; range_idx2 < range_idx;
					range_idx2++) {
					if ((idx >= test_ranges[range_idx2].start_idx)
						&& (idx < test_ranges[range_idx2].start_idx
							+ BITS_PER_TEST_RANGE)) {
						idx_overlaps = true;
						break;
					}
				}
			} while (idx_overlaps);

			test_ranges[range_idx].start_idx = idx;
		}

		// Sort the range indexes into asscending order.
		qsort(test_ranges, NUM(test_ranges), sizeof(*test_ranges),
			range_idx_cmp);

		// Pseudo-Randomly determine the operations to be performed.
		MEMCLEAR(operations, sizeof(operations));
		for (unsigned int op_idx = 0; op_idx < NUM(operations); op_idx++) {
			operations[op_idx].range_idx = rand() % NUM(test_ranges);
			operations[op_idx].op = OP_TYPE_FIRST + (rand() % OP_TYPE_NUM);
			operations[op_idx].offset = rand() % BITS_PER_TEST_RANGE;
			if ((operations[op_idx].op == SET_NUM)
				|| (operations[op_idx].op == CLEAR_NUM)) {
				operations[op_idx].num = rand()
					% ((BITS_PER_TEST_RANGE - operations[op_idx].offset) + 1);
			}
			if (operations[op_idx].op == QUERY_NTHSET) {
				// Half the time, select an Nth index within the preferred
				// Nth index range, otherwise use a range of the maximum
				// number of test bits that could be ever be set.
				sb_num_t nth_idx_range = (rand() % 2) ? NTH_IDX_PREFERRED_RANGE
					: (BITS_PER_TEST_RANGE * NUM_TEST_RANGES);

				operations[op_idx].nth_idx = rand() % nth_idx_range;
			}
		}

		// ==== Perform ====
		sb_t *sb = sb_create();
		if (sb == NULL) {
			display_passed_passes(&passed_last_displayed, pass - 1);
			fprintf(stderr, "Pass %li: sb_create() failed\n", pass);
			exit(30);
		}

		// Perform the pseudo-randomly selected operations.
		for (struct operation *p_op = operations;
			p_op < (operations + NUM(operations)); p_op++) {
			bool expected_bool;
			sb_idx_t expected_idx;
			sb_idx_t expected_idx_rv;
			sb_num_t expected_num;
			bool actual_bool;
			sb_idx_t actual_idx;
			sb_idx_t actual_idx_rv;
			sb_num_t actual_num;
			error_t errno_orig;
			error_t expected_errno;
			error_t actual_errno;
			
			sb_idx_t idx = test_ranges[p_op->range_idx].start_idx + p_op->offset;

			switch (p_op->op) {
			case SET_BIT:
				sb_set(sb, idx);
				test_ranges[p_op->range_idx].expected[p_op->offset] = true;
				break;

			case SET_NUM:
				sb_setnum(sb, idx, p_op->num);
				for (unsigned int n1 = 0; n1 < p_op->num; n1++) {
					test_ranges[p_op->range_idx].expected[p_op->offset + n1] = true;
				}
				break;

			case QUERY_ANYSET:
				expected_bool = (expected_numset() > 0) ? true : false;
				actual_bool = sb_anyset(sb);
				if (expected_bool != actual_bool) {
					display_passed_passes(&passed_last_displayed, pass - 1);
					fprintf(stderr, "Pass %li: Validation Error - "
						"QUERY_ANYSET Unexpected Any Set:\n"
						"  actual: %i expected: %i\n",
						pass, actual_bool, expected_bool);
					display_operations(stderr);
					fputs(sb_dump(sb), stderr);
					exit(40);
				}
				break;

			case QUERY_NUMSET:
				expected_num = expected_numset();
				actual_num = sb_numset(sb);
				if (expected_num != actual_num) {
					display_passed_passes(&passed_last_displayed, pass - 1);
					fprintf(stderr, "Pass %li: Validation Error - "
						"QUERY_NUMSET Unexpected Num Set:\n"
						"  actual: %#" SB_PRI_NUM " expected: %#" SB_PRI_NUM "\n",
						pass, actual_num, expected_num);
					display_operations(stderr);
					fputs(sb_dump(sb), stderr);
					exit(41);
				}
				break;

			case QUERY_ISSET:
				expected_bool = expected_isset(idx);
				actual_bool = sb_isset(sb, idx);
				if (expected_bool != actual_bool) {
					display_passed_passes(&passed_last_displayed, pass - 1);
					fprintf(stderr, "Pass %li: Validation Error - "
						"QUERY_ISSET Unexpected Is Set:\n"
						"  actual: %i expected: %i idx: %#" SB_PRI_IDX "\n",
						pass, actual_bool, expected_bool, idx);
					display_operations(stderr);
					fputs(sb_dump(sb), stderr);
					exit(42);
				}
				break;

			case QUERY_FIRSTSET:
				expected_idx = expected_firstset();
				actual_idx = sb_firstset(sb);
				if (expected_idx != actual_idx) {
					display_passed_passes(&passed_last_displayed, pass - 1);
					fprintf(stderr, "Pass %li: Validation Error - "
						"QUERY_FIRSTSET Unexpected First Set:\n"
						"  actual: %#" SB_PRI_IDX " expected: %#" SB_PRI_IDX "\n",
						pass, actual_idx, expected_idx);
					display_operations(stderr);
					fputs(sb_dump(sb), stderr);
					exit(43);
				}
				break;

			case QUERY_NEXTSET:
				expected_idx = expected_nextset(idx);
				actual_idx = sb_nextset(sb, idx);
				if (expected_idx != actual_idx) {
					display_passed_passes(&passed_last_displayed, pass - 1);
					fprintf(stderr, "Pass %li: Validation Error - "
						"QUERY_NEXTSET Unexpected Next Set:\n"
						"  actual: %#" SB_PRI_IDX " expected: %#" SB_PRI_IDX
						" idx: %#" SB_PRI_IDX "\n",
						pass, actual_idx, expected_idx, idx);
					display_operations(stderr);
					fputs(sb_dump(sb), stderr);
					exit(44);
				}
				break;

			case QUERY_PREVSET:
				expected_idx = expected_prevset(idx);
				actual_idx = sb_prevset(sb, idx);
				if (expected_idx != actual_idx) {
					display_passed_passes(&passed_last_displayed, pass - 1);
					fprintf(stderr, "Pass %li: Validation Error - "
						"QUERY_PREVSET Unexpected Previous Set:\n"
						"  actual: %#" SB_PRI_IDX " expected: %#" SB_PRI_IDX
						" idx: %#" SB_PRI_IDX "\n",
						pass, actual_idx, expected_idx, idx);
					display_operations(stderr);
					fputs(sb_dump(sb), stderr);
					exit(45);
				}
				break;

			case CLEAR_BIT:
				sb_clear(sb, idx);
				test_ranges[p_op->range_idx].expected[p_op->offset] = false;
				break;

			case CLEAR_NUM:
				sb_clearnum(sb, idx, p_op->num);
				for (unsigned int n1 = 0; n1 < p_op->num; n1++) {
					test_ranges[p_op->range_idx].expected[p_op->offset + n1] = false;
				}
				break;

			case QUERY_ANYCLEARED:
				expected_bool = true;  // There are always at least some cleared
				                       // bits between the test ranges.  So far
				                       // the implementation of this test
				                       // never sets all bits.
				actual_bool = sb_anycleared(sb);
				if (expected_bool != actual_bool) {
					display_passed_passes(&passed_last_displayed, pass - 1);
					fprintf(stderr, "Pass %li: Validation Error - "
						"QUERY_ANYCLEARED Unexpected Any Cleared:\n"
						"  actual: %i expected: %i\n",
						pass, actual_bool, expected_bool);
					display_operations(stderr);
					fputs(sb_dump(sb), stderr);
					exit(50);
				}
				break;

			case QUERY_NUMCLEARED:
				expected_num = (sb_num_t) 0 - expected_numset();
				actual_num = sb_numcleared(sb);
				if (expected_num != actual_num) {
					display_passed_passes(&passed_last_displayed, pass - 1);
					fprintf(stderr, "Pass %li: Validation Error - "
						"QUERY_NUMCLEARED Unexpected Num Cleared:\n"
						"  actual: %#" SB_PRI_NUM " expected: %#" SB_PRI_NUM "\n",
						pass, actual_num, expected_num);
					display_operations(stderr);
					fputs(sb_dump(sb), stderr);
					exit(51);
				}
				break;

			case QUERY_ISCLEARED:
				expected_bool = !expected_isset(idx);
				actual_bool = sb_iscleared(sb, idx);
				if (expected_bool != actual_bool) {
					display_passed_passes(&passed_last_displayed, pass - 1);
					fprintf(stderr, "Pass %li: Validation Error - "
						"QUERY_ISCLEARED Unexpected Is Cleared:\n"
						"  actual: %i expected: %i idx: %#" SB_PRI_IDX "\n",
						pass, actual_bool, expected_bool, idx);
					display_operations(stderr);
					fputs(sb_dump(sb), stderr);
					exit(52);
				}
				break;

			case QUERY_FIRSTCLEARED:
				expected_idx = expected_firstcleared();
				actual_idx = sb_firstcleared(sb);
				if (expected_idx != actual_idx) {
					display_passed_passes(&passed_last_displayed, pass - 1);
					fprintf(stderr, "Pass %li: Validation Error - "
						"QUERY_FIRSTCLEARED Unexpected First Cleared:\n"
						"  actual: %#" SB_PRI_IDX " expected: %#" SB_PRI_IDX "\n",
						pass, actual_idx, expected_idx);
					display_operations(stderr);
					fputs(sb_dump(sb), stderr);
					exit(53);
				}
				break;

			case QUERY_NEXTCLEARED:
				expected_idx = expected_nextcleared(idx);
				actual_idx = sb_nextcleared(sb, idx);
				if (expected_idx != actual_idx) {
					display_passed_passes(&passed_last_displayed, pass - 1);
					fprintf(stderr, "Pass %li: Validation Error - "
						"QUERY_NEXTCLEARED Unexpected Next Cleared:\n"
						"  actual: %#" SB_PRI_IDX " expected: %#" SB_PRI_IDX
						" idx: %#" SB_PRI_IDX "\n",
						pass, actual_idx, expected_idx, idx);
					display_operations(stderr);
					fputs(sb_dump(sb), stderr);
					exit(54);
				}
				break;

			case QUERY_PREVCLEARED:
				expected_idx = expected_prevcleared(idx);
				actual_idx = sb_prevcleared(sb, idx);
				if (expected_idx != actual_idx) {
					display_passed_passes(&passed_last_displayed, pass - 1);
					fprintf(stderr, "Pass %li: Validation Error - "
						"QUERY_PREVCLEARED Unexpected Previous Cleared:\n"
						"  actual: %#" SB_PRI_IDX " expected: %#" SB_PRI_IDX
						" idx: %#" SB_PRI_IDX "\n",
						pass, actual_idx, expected_idx, idx);
					display_operations(stderr);
					fputs(sb_dump(sb), stderr);
					exit(55);
				}
				break;

			case QUERY_NTHSET:
				errno_orig = MAX((p_op - operations) % 100, 1);
				expected_idx_rv = expected_nthset_rv(p_op->nth_idx);
				error_t expected_errno = (p_op->nth_idx >= expected_numset())
					? ESRCH : errno_orig;

				errno = errno_orig;
				actual_idx_rv = sb_nthset(sb, p_op->nth_idx);
				actual_errno = errno;
				if (actual_idx_rv != expected_idx_rv) {
					display_passed_passes(&passed_last_displayed, pass - 1);
					fprintf(stderr, "Pass %li: Validation Error - "
						"QUERY_NTHSET Unexpected Return Value:\n"
						"  actual: %#" SB_PRI_IDX " expected: %#" SB_PRI_IDX
						" nth_idx: %#" SB_PRI_IDX "\n",
						pass, actual_idx_rv, expected_idx_rv, p_op->nth_idx);
					display_operations(stderr);
					fputs(sb_dump(sb), stderr);
					exit(56);
				}

				if (actual_errno != expected_errno) {
					display_passed_passes(&passed_last_displayed, pass - 1);
					fprintf(stderr, "Pass %li: Validation Error - "
						"QUERY_NTHSET Unexpected errno Value:\n"
						"  actual: %u expected: %u"
						" nth_idx: %#" SB_PRI_IDX "\n",
						pass, actual_errno, expected_errno, p_op->nth_idx);
					display_operations(stderr);
					fputs(sb_dump(sb), stderr);
					exit(57);
				}

				break;

			default:
				assert(false);  // Unexpected operation
			}
		}

		// ==== Validation ====
		// Validate internal state.
		sb_pdynstr_t err_string = sb_validate(sb);
		if (err_string) {
			display_passed_passes(&passed_last_displayed, pass - 1);
			fprintf(stderr, "Pass %li: Validation Error - "
				"Inconsitent Internal State:\n%s\n", pass, err_string);
			display_operations(stderr);
			fputs(sb_dump(sb), stderr);
			exit(31);
		}

		// Validate Individual Bit Settings
		// within the expected bit array of each range.
		for (unsigned int range_idx = 0; range_idx < NUM(test_ranges);
			range_idx++) {
			for (unsigned int bit_idx = 0;
				bit_idx < NUM(test_ranges[range_idx].expected); bit_idx++) {
				sb_idx_t idx = test_ranges[range_idx].start_idx + bit_idx;
				bool expected = expected_isset(idx);
				bool actual = sb_isset(sb, idx) ? true : false;

				if (actual != expected) {
					display_passed_passes(&passed_last_displayed, pass - 1);

					fprintf(stderr, "Pass %li: Validation Error - "
						"Unexpected Bit Setting:\n"
						"  idx: %#" SB_PRI_IDX " actual: %i expected: %i\n",
						pass, idx, actual, expected);
					display_operations(stderr);
					fputs(sb_dump(sb), stderr);
					exit(32);
				}
			}
		}

		// Validate Num Set Bits
		sb_num_t expected_num = expected_numset();
		sb_num_t actual_num = sb_numset(sb);
		if (actual_num != expected_num) {
			display_passed_passes(&passed_last_displayed, pass - 1);
			fprintf(stderr, "Pass %li: Validation Error - "
				"Unexpected Num Bits Set:\n"
				"  actual: %#" SB_PRI_NUM " expected: %#" SB_PRI_NUM "\n",
				pass, actual_num, expected_num);
			display_operations(stderr);
			fputs(sb_dump(sb), stderr);
			exit(33);
		}

		// Validate sb_firstset()
		sb_idx_t expected_idx = expected_firstset();
		sb_idx_t actual_firstset = sb_firstset(sb);
		if (actual_firstset != expected_idx) {
			display_passed_passes(&passed_last_displayed, pass - 1);
			fprintf(stderr, "Pass %li: Validation Error - "
				"Unexpected First Set:\n"
				"  actual: %#" SB_PRI_IDX " expected: %#" SB_PRI_IDX "\n",
				pass, actual_firstset, expected_idx);
			display_operations(stderr);
			fputs(sb_dump(sb), stderr);
			exit(34);
		}

		// Validate sb_nextset()
		// Will validate sb_nextset() for every bit index within a range, plus
		// where valid the bit indexes for two bits immediately before and
		// immediately after each range.
		for (unsigned int range_idx = 0; range_idx < NUM(test_ranges);
			range_idx++) {
			struct test_range *p_range = &test_ranges[range_idx];
			for (int bit_idx = -2; bit_idx < ((int)NUM(p_range->expected) + 2);
				bit_idx++) {
				sb_idx_t idx1 = p_range->start_idx + bit_idx;
				if ((bit_idx < 0) && (idx1 > p_range->start_idx) ) {
					continue;
				}
				if ((bit_idx > 0) && (idx1 < p_range->start_idx) ) {
					continue;
				}
			 	expected_idx = expected_nextset(idx1);
				sb_idx_t actual = sb_nextset(sb, idx1);

				if (actual != expected_idx) {
					display_passed_passes(&passed_last_displayed, pass - 1);
					fprintf(stderr, "Pass %li: Validation Error - "
						"Unexpected Next Set:\n"
						"  idx1: %#" SB_PRI_IDX " actual: %#" SB_PRI_IDX
						" expected: %#" SB_PRI_IDX "\n",
						pass, idx1, actual, expected_idx);
					display_operations(stderr);
					fputs(sb_dump(sb), stderr);
					exit(35);
				}
			}
		}

		// Validate sb_nextcleared()
		// Will validate sb_nextcleared() for every bit index within a range, plus
		// where valid the bit indexes for two bits immediately before and
		// immediately after each range.
		for (unsigned int range_idx = 0; range_idx < NUM(test_ranges);
			range_idx++) {
			struct test_range *p_range = &test_ranges[range_idx];
			for (int bit_idx = -2; bit_idx < ((int)NUM(p_range->expected) + 2);
				bit_idx++) {
				sb_idx_t idx1 = p_range->start_idx + bit_idx;
				if ((bit_idx < 0) && (idx1 > p_range->start_idx) ) {
					continue;
				}
				if ((bit_idx > 0) && (idx1 < p_range->start_idx) ) {
					continue;
				}
			 	expected_idx = expected_nextcleared(idx1);
				sb_idx_t actual = sb_nextcleared(sb, idx1);

				if (actual != expected_idx) {
					display_passed_passes(&passed_last_displayed, pass - 1);
					fprintf(stderr, "Pass %li: Validation Error - "
						"Unexpected Next Cleared:\n"
						"  idx1: %#" SB_PRI_IDX " actual: %#" SB_PRI_IDX
						" expected: %#" SB_PRI_IDX "\n",
						pass, idx1, actual, expected_idx);
					display_operations(stderr);
					fputs(sb_dump(sb), stderr);
					exit(36);
				}
			}
		}

		// Validate sb_save() and sb_load().
		rv = ftruncate(tempfile_fd, 0);
		if (rv != 0) {
			display_passed_passes(&passed_last_displayed, pass - 1);
			fprintf(stderr, "Failed to truncate temp file %s, rv: %i errno: %i\n",
				tempfile_path, rv, errno);
			exit(31);
		}
		off_t offset = lseek(tempfile_fd, 0, SEEK_SET);
		if (offset != 0) {
			display_passed_passes(&passed_last_displayed, pass - 1);
			fprintf(stderr,
				"Seek before sb_save to start of tempfile failed %s, "
				"offset: %lli errno: %i\n",
				tempfile_path, offset, errno);
			exit(32);
		}
		tempfile = fopen(tempfile_path, "wb");
		if (tempfile == NULL) {
			fprintf(stderr, "Failed to open for write: %s, errno: %i\n",
				tempfile_path, errno);
			exit(33);
		}
		rv = sb_save(tempfile, sb);
		if (rv != 0) {
			display_passed_passes(&passed_last_displayed, pass - 1);
			fprintf(stderr, "failed sb_save to temp file %s, rv: %i errno: %i\n",
				tempfile_path, rv, errno);
			exit(33);
		}
		
		offset = lseek(tempfile_fd, 0, SEEK_SET);
		if (offset != 0) {
			display_passed_passes(&passed_last_displayed, pass - 1);
			fprintf(stderr,
				"Seek before sb_load to start of tempfile failed %s, "
				"offset: %lli errno: %i\n",
				tempfile_path, offset, errno);
			exit(34);
		}
		sb_t *sb2 = sb_create();
		if (sb2 == NULL) {
			display_passed_passes(&passed_last_displayed, pass - 1);
			fprintf(stderr, "Pass %li: sb_create() failed\n", pass);
			exit(35);
		}
		rv = fclose(tempfile);
		if (rv != 0) {
			fputs("Failed to close tempfile before reopen for read\n", stderr);
			exit(37);
		}
		tempfile = fopen(tempfile_path, "rb");
		if (tempfile == NULL) {
			fprintf(stderr, "Failed to open for read: %s, errno: %i\n",
				tempfile_path, errno);
			exit(33);
		}
		rv = sb_load(tempfile, sb2);
		if (rv != 0) {
			display_passed_passes(&passed_last_displayed, pass - 1);
			fprintf(stderr,
				"Failed sb_load from temp file %s, rv: %i errno: %i\n",
				tempfile_path, offset, errno);
			exit(36);
		}
		rv = fclose(tempfile);
		if (rv != 0) {
			fputs("Failed to close tempfile\n", stderr);
			exit(37);
		}
		expected_idx = expected_firstset();
		actual_firstset = sb_firstset(sb2);
		if (actual_firstset != expected_idx) {
			exit(37);
		}
		sb_idx_t prev_idx = expected_idx;
		do {
			expected_idx = expected_nextset(prev_idx);
			sb_idx_t actual_nextset = sb_nextset(sb2, prev_idx);
			if (actual_nextset != expected_idx) {
				display_passed_passes(&passed_last_displayed, pass - 1);
				fprintf(stderr,
					"Failed unexpected nextset, : actual %llx expected %llx\n",
					actual_nextset, expected_nextset);
				exit(38);
			}
			prev_idx = expected_idx;
		} while (expected_idx != 0);

		sb_delete(sb);
		sb_delete(sb2);

		// Every stride worth of passes, display which passes have passed.
		if ((pass != 0) && (pass % PASSED_DISPLAY_STRIDE) == 0) {
			display_passed_passes(&passed_last_displayed, pass);
		}
	}

	display_passed_passes(&passed_last_displayed, pass - 1);

	return 0;
}

static void usage(FILE *stream)
{
	fputs("usage: test_prand [-h?] [pass_start [pass_end]]\n", stream);
}

static void help(FILE *stream)
{
	usage(stream);
	fputs("Sparsebit_c Pseudo-Random Test\n", stream);
	fputs("  -?h,--help Help\n", stream);
	fputs("  pass_start: Starting pass number (default: 0)\n", stream);
	fputs("  pass_end: Ending pass number (default: 10000)\n", stream);
}

static void display_passed_passes(long *palready_displayed, long last_passed)
{
	if (last_passed <= *palready_displayed) {
		// Display of passed passes already up to date.
		return;
	}

	// Need to display 1 or a range of passed passes?
	if (last_passed == (*palready_displayed + 1)) {
		printf("Pass   %6li:           Passed\n", last_passed);
	} else {
		printf("Passes %6li to %6li: Passed\n", *palready_displayed + 1,
			last_passed);
	}
	fflush(stdout);

	*palready_displayed = last_passed;

	return;
}

static void display_operations(FILE *stream)
{
	fputs("Operations:\n", stderr);
	for (unsigned int opt_idx = 0; opt_idx <  NUM(operations); opt_idx++) {
		fprintf(stderr, "  %3u %u (%s)%*s idx: %#" SB_PRI_IDX
			" num: %#" SB_PRI_NUM
			" nth_idx: %#" SB_PRI_IDX "\n",
			opt_idx, operations[opt_idx].op,
			op_string(operations[opt_idx].op),
			(int) (18 - strlen(op_string(operations[opt_idx].op))), "",
			test_ranges[operations[opt_idx].range_idx].start_idx
				+ operations[opt_idx].offset,
			operations[opt_idx].num,
			operations[opt_idx].nth_idx);
	}
}

static const char *op_string(enum OP op)
{
	static const char *unknown_str = "Unknown";

	for (unsigned int n1 = 0; n1 < NUM(op_strings); n1++) {
		if (op_strings[n1].op == op) {
			return op_strings[n1].str;
		}
	}

	return unknown_str;
}

static int range_idx_cmp(const void *elem1, const void *elem2)
{
	sb_idx_t idx1 = ((struct test_range*)elem1)->start_idx;
	sb_idx_t idx2 = ((struct test_range*)elem2)->start_idx;

	if (idx1 > idx2) return 1;
	if (idx1 < idx2) return -1;

	return 0;
}

static sb_num_t range_numset(unsigned int range_idx)
{
	sb_num_t total = 0;

	struct test_range *p_range = &test_ranges[range_idx];

	for (unsigned int n1 = 0; n1 < NUM(p_range->expected); n1++) {
		if (p_range->expected[n1]) {
			total++;
		}
	}

	return total;
}

static sb_idx_t range_firstset(unsigned int range_idx)
{
	struct test_range *p_range = &test_ranges[range_idx];

	for (unsigned int n1 = 0; n1 < NUM(p_range->expected); n1++) {
		if (p_range->expected[n1]) {
			return p_range->start_idx + n1;
		}
	}

	assert(0);  // Caller should only call range_firstset, when range has
	            // atleast one set bit.
}

static bool range_supports_idx(unsigned int range_idx, sb_idx_t idx)
{
	assert(range_idx < NUM(test_ranges));
	struct test_range *p_range = &test_ranges[range_idx];

	if ((idx >= p_range->start_idx)
		&& (idx <= ((p_range->start_idx + NUM(p_range->expected)) - 1)) ) {
		return true;
	}

	return false;
}

static bool expected_isset(sb_idx_t idx)
{
	for (unsigned int range_idx = 0; range_idx < NUM(test_ranges); range_idx++) {
		// Does this range maintain the expected state of idx?
		if ((idx >= test_ranges[range_idx].start_idx)
			&& (idx <= (test_ranges[range_idx].start_idx
				+ NUM(test_ranges[range_idx].expected) - 1))) {
			return test_ranges[range_idx].expected[idx
				- test_ranges[range_idx].start_idx];
		}
	}

	// No range that maintains the expected setting of the bit at index idx,
	// so the bit should always be cleared.
	return false;
}

static sb_num_t expected_numset(void)
{
	sb_num_t expected_numset = 0;
	for (unsigned int range_idx = 0; range_idx < NUM(test_ranges); range_idx++) {
		expected_numset += range_numset(range_idx);
	}

	return expected_numset;
}

static sb_idx_t expected_firstset(void)
{
	for (unsigned int range_idx = 0; range_idx < NUM(test_ranges); range_idx++) {
		if (range_numset(range_idx)) {
			return range_firstset(range_idx);
		}
	}

	// No set bits, in any range.
	return 0;
}

static sb_idx_t expected_nextset(sb_idx_t start_idx)
{
	for (unsigned int range_idx = 0; range_idx < NUM(test_ranges); range_idx++) {
		struct test_range *p_range = &test_ranges[range_idx];
		for (unsigned int bit_idx = 0; bit_idx < NUM(p_range->expected);
			bit_idx++) {
			sb_idx_t idx = p_range->start_idx + bit_idx;
			if (idx <= start_idx) {
				continue;
			}
			if (p_range->expected[bit_idx]) {
				return idx;
			}
		}
	}

	return 0;
}

static sb_idx_t expected_prevset(sb_idx_t start_idx)
{
	for (int range_idx = NUM(test_ranges) - 1; range_idx >= 0; range_idx--) {
		struct test_range *p_range = &test_ranges[range_idx];

		if (p_range->start_idx >= start_idx) { continue; }
		for (int bit_idx = NUM(p_range->expected) - 1; bit_idx >= 0; bit_idx--) {
			sb_idx_t idx = p_range->start_idx + bit_idx;
			if (idx >= start_idx) {
				continue;
			}
			if (p_range->expected[bit_idx]) {
				return idx;
			}
		}
	}

	return 0;
}

static sb_idx_t expected_firstcleared(void)
{
	if (test_ranges[0].start_idx != 0) {
		// Fix test range starts at an index > 0.  All bits before the start
		// of that range are cleared, including the bit at index 0.
		return 0;
	}

	for (unsigned int range_idx = 0; range_idx < NUM(test_ranges); range_idx++) {
		for (int bit_idx = 0; bit_idx
			< NUM(test_ranges[range_idx].expected); bit_idx++) {
			if (!test_ranges[range_idx].expected[bit_idx]) {
				return test_ranges[range_idx].start_idx + bit_idx;
			}
		}

		// Is there a gap between this and the next test range?
		if ((range_idx + 1) < NUM(test_ranges)) {
			// There is a gap.  Return index of first bit within the gap.
			return test_ranges[range_idx].start_idx
				+ NUM(test_ranges[range_idx].expected);
		} else {
			// First cleared bit will be the first bit index after the last
			// test range.
			assert(range_idx == (NUM(test_ranges) - 1));
			assert((test_ranges[range_idx].start_idx
				+ NUM(test_ranges[range_idx].expected)) != 0);
			return test_ranges[range_idx].start_idx
				+ NUM(test_ranges[range_idx].expected);
		}
	}

	assert(false);  // So far the implementation of this testcase does not
	                // allow all bits to be set.  Thus there should always
	                // be at least one cleared bit.
	// NOT REACHED
}

static sb_idx_t expected_nextcleared(sb_idx_t start_idx)
{
	if (start_idx == SB_IDX_MAX) {
		return 0;
	}

	// If start_idx + 1 is not supported by any test range, then
	// start_idx + 1 is the next cleared bit.
	unsigned int range_idx;
	for (range_idx = 0; range_idx < NUM(test_ranges); range_idx++) {
		if (range_supports_idx(range_idx, start_idx + 1)) {
			break;
		}
	}
	if (range_idx >= NUM(test_ranges)) {
		assert(range_idx == NUM(test_ranges));
		return start_idx + 1;
	}

	// First cleared bit within a test range > start_idx is the next
	// cleared bit.
	for (unsigned int range_idx = 0; range_idx < NUM(test_ranges); range_idx++) {
		struct test_range *p_range = &test_ranges[range_idx];

		if (!range_supports_idx(range_idx, start_idx + 1)) {
			continue;
		}
		for (int bit_idx = 0; bit_idx < NUM(p_range->expected);
			bit_idx++) {
			sb_idx_t idx = p_range->start_idx + bit_idx;

			if ((idx > start_idx) && !p_range->expected[bit_idx]) {
				return idx;
			}
		}
		// If next range not adjacent, then return index of first bit within
		// the gap between this and the next range.
		if ((range_idx == NUM(test_ranges) - 1)
			|| ((p_range->start_idx + BITS_PER_TEST_RANGE)
				!= (p_range + 1)->start_idx) ) {
			return p_range->start_idx + BITS_PER_TEST_RANGE;
		}
	}
	
	return 0;
}

static sb_idx_t expected_prevcleared(sb_idx_t start_idx)
{
	for (int range_idx = NUM(test_ranges) - 1; range_idx >= 0; range_idx--) {
		struct test_range *p_range = &test_ranges[range_idx];

		if (p_range->start_idx > start_idx) { continue; }
		for (int bit_idx = NUM(p_range->expected) - 1; bit_idx >= 0; bit_idx--) {
			sb_idx_t idx = p_range->start_idx + bit_idx;
			if (idx >= start_idx) {
				continue;
			}
			if (!p_range->expected[bit_idx]) {
				return idx;
			}
		}
		// If gap between current and previous range, return index of highest
		// bit in the gap.
		if (range_idx >= 1) {
			struct test_range *p_range_prev = &test_ranges[range_idx - 1];
			if ((p_range_prev->start_idx + NUM(p_range_prev->expected))
				< p_range->start_idx) {
					assert(p_range->start_idx >= 1);
					return p_range->start_idx - 1;
			}
		}
	}
	
	return (test_ranges[0].start_idx >= 1)
		? test_ranges[0].start_idx - 1 : 0;
}

static sb_idx_t expected_nthset_rv(sb_idx_t nth_idx)
{
	sb_num_t num = 0;

	// For each test range.
	for (struct test_range *p_range = test_ranges;
		p_range < test_ranges + NUM(test_ranges); p_range++) {
		// For each bit in the current test range.
		for (unsigned int bit_idx = 0; bit_idx < NUM(p_range->expected);
			bit_idx++) {
			if (p_range->expected[bit_idx]) {
				if (num == nth_idx) {
					return p_range->start_idx + bit_idx;
				}
				num++;
			}
		}
	}


	return 0;
}

static void cleanup(void)
{
	int rv;

	if (tempfile_fd >= 0) {
		rv = close(tempfile_fd);
		if (rv != 0) {
			fprintf(stderr, "Error closing tempfile fd %i, rv: %i errno: %i\n",
				tempfile_fd, rv, errno);
			return;
		}

		rv = unlink(tempfile_path);
		if (rv != 0) {
			fprintf(stderr, "Error unlinking tempfile %s, rv: %i errno: %i\n",
				tempfile_path, rv, errno);
			return;
		}
	}

	return;
}
