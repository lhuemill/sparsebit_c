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
#include <getopt.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define DEFAULT_PASS_START 0
#define DEFAULT_PASS_END 10000
#define PASSED_DISPLAY_STRIDE 100

#define NUM_TEST_RANGES 4
#define BITS_PER_TEST_RANGE 300
#define PER_PASS_NUM_OPS 1000

#define MEMCLEAR(p, n) memset((p), 0, (n))
#define NUM(a) (sizeof(a)/sizeof((a)[0]))

#define USE_DEFAULT_RANGE_THRESHOLD ((int)((float)RAND_MAX * 0.05))
sb_idx_t default_ranges[] = {
	0,
	(SB_IDX_MAX / 2) - (BITS_PER_TEST_RANGE / 2),
	(SB_IDX_MAX - BITS_PER_TEST_RANGE) + 1,
};
#define RAND_IDX() (((rand() & (((sb_idx_t)1 << 32) - 1)) << 32) | rand())

static void usage(FILE *stream);
static void help(FILE *stream);
static void display_passed_passes(long *palready_displayed, long last_passed);
static int range_idx_cmp(const void *elem1, const void *elem2);
static sb_num_t range_numset(unsigned int range_idx);
static sb_idx_t range_firstset(unsigned int range_idx);
static bool range_supports_idx(unsigned int range_idx, sb_idx_t idx);
static sb_idx_t ranges_nextset(sb_idx_t start_idx);
static sb_idx_t ranges_nextcleared(sb_idx_t start_idx);

struct test_range {
	sb_idx_t start_idx;
	bool expected[BITS_PER_TEST_RANGE];
};
struct test_range test_ranges[NUM_TEST_RANGES];

enum OP {
	SET_BIT = 132,
	SET_BITNUM,
	CLEAR_BIT,
	CLEAR_BITNUM,
};
#define OP_TYPE_FIRST SET_BIT
#define OP_TYPE_LAST CLEAR_BITNUM
#define OP_TYPE_NUM (OP_TYPE_LAST - OP_TYPE_FIRST)
struct operation {
	unsigned int range_idx;
	enum OP op;
	unsigned int offset;
	sb_num_t num;
};
struct operation operations[PER_PASS_NUM_OPS];


int main(int argc, char *argv[])
{
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
		}
	}
	printf("pass_start: %lu\n", pass_start);
	printf("pass_end: %lu\n", pass_end);
	puts("");
	if (pass_end < pass_start) {
		fputs("Pass_start must be before pass_end\n", stderr);
		exit(24);
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
			if ((operations[op_idx].op == SET_BITNUM)
				|| (operations[op_idx].op == CLEAR_BITNUM)) {
				operations[op_idx].num = rand()
					% ((BITS_PER_TEST_RANGE - operations[op_idx].offset) + 1);

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
			
			sb_idx_t idx = test_ranges[p_op->range_idx].start_idx + p_op->offset;

			switch (p_op->op) {
			case SET_BIT:
				sb_set(sb, idx);
				test_ranges[p_op->range_idx].expected[p_op->offset] = true;
				break;

			case SET_BITNUM:
				sb_setnum(sb, idx, p_op->num);
				for (unsigned int n1 = 0; n1 < p_op->num; n1++) {
					test_ranges[p_op->range_idx].expected[p_op->offset + n1] = true;
				}
				break;

			case CLEAR_BIT:
				sb_clear(sb, idx);
				test_ranges[p_op->range_idx].expected[p_op->offset] = false;
				break;

			case CLEAR_BITNUM:
				sb_clearnum(sb, idx, p_op->num);
				for (unsigned int n1 = 0; n1 < p_op->num; n1++) {
					test_ranges[p_op->range_idx].expected[p_op->offset + n1] = false;
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
			fputs(sb_dump(sb), stderr);
			exit(31);
		}

		// Validate Individual Bit Settings
		// within the  espected bit array of each range.
		for (unsigned int range_idx = 0; range_idx < NUM(test_ranges);
			range_idx++) {
			for (unsigned int bit_idx = 0;
				bit_idx < NUM(test_ranges[range_idx].expected); bit_idx++) {
					bool expected = test_ranges[range_idx].expected[bit_idx];
					bool actual = sb_isset(sb, test_ranges[range_idx].start_idx
						+ bit_idx) ? true : false;

					if (actual != expected) {
						display_passed_passes(&passed_last_displayed, pass - 1);
						fprintf(stderr, "Pass %li: Validation Error - "
							"Unexpected Bit Setting:\n"
							"  idx: %#llx actual: %i expected: %i\n",
							pass, (unsigned long long)test_ranges[range_idx].start_idx
							+ bit_idx, actual, expected);
						fputs(sb_dump(sb), stderr);
						exit(32);
					}
				}
		}

		// Validate Num Set Bits
		sb_num_t expected_numset = 0;
		for (unsigned int range_idx = 0; range_idx < NUM(test_ranges);
			range_idx++) {
			expected_numset += range_numset(range_idx);
		}
		sb_num_t actual_numset = sb_numset(sb);
		if (actual_numset != expected_numset) {
			display_passed_passes(&passed_last_displayed, pass - 1);
			fprintf(stderr, "Pass %li: Validation Error - "
				"Unexpected Num Bits Set:\n"
				"  actual: %#llx expected: %#llx\n",
				pass, (unsigned long long)actual_numset,
				(unsigned long long)expected_numset);
			fputs(sb_dump(sb), stderr);
			exit(33);
		}

		// Validate sb_firstset()
		sb_idx_t expected_firstset = 5;
		for (unsigned int range_idx = 0; range_idx < NUM(test_ranges);
			range_idx++) {
			if (range_numset(range_idx)) {
				expected_firstset = range_firstset(range_idx);
				break;  // First range with any bits set, will be the range with
				        // the first set bit.
			}
		}
		sb_idx_t actual_firstset = sb_firstset(sb);
		if (actual_firstset != expected_firstset) {
			display_passed_passes(&passed_last_displayed, pass - 1);
			fprintf(stderr, "Pass %li: Validation Error - "
				"Unexpected First Set:\n"
				"  actual: %#llx expected: %#llx\n",
				pass, (unsigned long long)actual_firstset,
				(unsigned long long)expected_firstset);
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
			 	sb_idx_t expected = ranges_nextset(idx1);

				sb_idx_t actual = sb_nextset(sb, idx1);

				if (actual != expected) {
					display_passed_passes(&passed_last_displayed, pass - 1);
					fprintf(stderr, "Pass %li: Validation Error - "
						"Unexpected Next Set:\n"
						"  idx1: %#llx actual: %#llx expected: %#llx\n",
						pass, (unsigned long long)idx1,
						(unsigned long long)actual, (unsigned long long)expected);
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
			 	sb_idx_t expected = ranges_nextcleared(idx1);

				sb_idx_t actual = sb_nextcleared(sb, idx1);

				if (actual != expected) {
					display_passed_passes(&passed_last_displayed, pass - 1);
					fprintf(stderr, "Pass %li: Validation Error - "
						"Unexpected Next Cleared:\n"
						"  idx1: %#llx actual: %#llx expected: %#llx\n",
						pass, (unsigned long long)idx1,
						(unsigned long long)actual, (unsigned long long)expected);
					fputs(sb_dump(sb), stderr);
					exit(36);
				}
			}
		}

		sb_delete(sb);

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
		printf("Passes %6li to %6li: Passed\n", *palready_displayed + 1, last_passed);
	}

	*palready_displayed = last_passed;

	return;
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

static sb_idx_t ranges_nextset(sb_idx_t start_idx)
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

static sb_idx_t ranges_nextcleared(sb_idx_t start_idx)
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
