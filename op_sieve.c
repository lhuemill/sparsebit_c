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

#define _POSIX_C_SOURCE 200809L  // For getline(3) and ssize_t
#include <assert.h>
#include <errno.h>
#include <malloc.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include "sparsebit_c.h"

#define NUM(a) (sizeof(a)/sizeof((a)[0]))
#define MEMCLEAR(p, n) memset((p), 0, (n))

#define PREFIX_HEX "0x"
#define PREFIX_IDX "idx: "
#define PREFIX_NUM "num: "
#define PREFIX_NTH_IDX "nth_idx: "

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

struct op_trait {
	enum OP op;
	char *nemonic;
} op_traits[] = {
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

struct operation {
	enum OP op;
	sb_idx_t idx;
	sb_num_t num;
	sb_idx_t nth_idx;
	bool perform;
	char *line;
	size_t trait_idx;
};
struct operation *operations = NULL;
size_t operations_num = 0;

static void perform_operations(sb_t *sb);
static sb_pdynstr_t validate(sb_t *sb);
static sb_idx_t ops_ranges_idx_first(void);
static sb_idx_t ops_ranges_idx_next(sb_idx_t idx);
static bool ops_performed_any(void);
static bool ops_expected_isset(sb_idx_t idx);
static sb_num_t ops_expected_numset(void);
static sb_idx_t ops_expected_nextset(sb_idx_t idx_start);
static sb_idx_t ops_expected_nextcleared(sb_idx_t idx_start);
static sb_idx_t ops_expected_prevset(sb_idx_t idx_start);
static sb_idx_t ops_expected_prevcleared(sb_idx_t idx_start);
static sb_idx_t ops_expected_nthset(sb_idx_t nth_idx);

int main(void)
{
	char *chptr, *endptr;
	struct operation *opptr;
	char *line = NULL;
	size_t line_sz = 0;
	ssize_t len;  // Does not include trailing '\0'.
	sb_pdynstr_t err_string;

	operations = malloc(sizeof(struct operation) * operations_num);
	if (operations == NULL) {
		fputs("Insufficient Memory\n", stderr);
		exit(1);
	}

	do {
		// Read next line from stdin.
		if (line != NULL) {
			free(line);
			line = NULL;
		}
		len = getline(&line, &line_sz, stdin);
		if (len == -1) {
			if (errno == 0) {
				break;
			}
			printf("getline error errno: %i\n", errno);
			exit(1);
		}

		// If present, remove trailing newline.
		if ((len > 1) && (line[len - 1] == '\n')) {
			line[len - 1] = '\0';
			len--;
		}

		// Ignore lines that begin with a '#'.
		if ((len >= 1) && (line[0] == '#')) {
			continue;
		}

		// Determine opcode based on finding its nemonic in the line.
		struct op_trait *traits = NULL;
		size_t trait_idx;
		for (trait_idx = 0; trait_idx < NUM(op_traits); trait_idx++) {
			traits = &op_traits[trait_idx];
			char *nemonic = strstr(line, traits->nemonic);
			if (nemonic != NULL) {
				break;
			}
		}
		if (trait_idx >= NUM(op_traits)) {
			fprintf(stderr, "No opcode nemonic found\n"
				"  line: %s\n", line);
			exit(2);
		}

		// Determine index if specified on line.
		sb_idx_t idx = 0;
		chptr = strstr(line, PREFIX_IDX);
		if (chptr != NULL) {
			if (strncmp(chptr + sizeof(PREFIX_IDX) - 1, PREFIX_HEX,
				sizeof(PREFIX_HEX) - 1) == 0) {
				idx = strtoull(chptr + sizeof(PREFIX_IDX) - 1
					+ sizeof(PREFIX_HEX) - 1, &endptr, 16);
			} else {
				idx = strtoull(chptr + sizeof(PREFIX_IDX) - 1, &endptr, 10);
			}
		}
		
		// Determine num if specified on line.
		sb_num_t num = 0;
		chptr = strstr(line, PREFIX_NUM);
		if (chptr != NULL) {
			if (strncmp(chptr + sizeof(PREFIX_NUM) - 1, PREFIX_HEX,
				sizeof(PREFIX_HEX) - 1) == 0) {
				num = strtoull(chptr + sizeof(PREFIX_NUM) - 1
					+ sizeof(PREFIX_HEX) - 1, &endptr, 16);
			} else {
				num = strtoull(chptr + sizeof(PREFIX_NUM) - 1, &endptr, 10);
			}
		}

		// Determine Nth index if specified on line.
		sb_idx_t nth_idx = 0;
		chptr = strstr(line, PREFIX_NTH_IDX);
		if (chptr != NULL) {
			if (strncmp(chptr + sizeof(PREFIX_NTH_IDX) - 1, PREFIX_HEX,
				sizeof(PREFIX_HEX) - 1) == 0) {
				idx = strtoull(chptr + sizeof(PREFIX_NTH_IDX) - 1
					+ sizeof(PREFIX_HEX) - 1, &endptr, 16);
			} else {
				idx = strtoull(chptr + sizeof(PREFIX_NTH_IDX) - 1, &endptr, 10);
			}
		}

		// Add to dynamically allocated array of operations.
		operations = realloc(operations,
			sizeof(struct operation) * (operations_num + 1));
		if (operations == NULL) {
			fputs("Insufficient Memory\n", stderr);
			exit(5);
		}
		operations_num++;
		opptr = &operations[operations_num - 1];
		MEMCLEAR(opptr, sizeof(*opptr));
		opptr->line = line;
		line = NULL;
		opptr->op = traits->op;
		opptr->idx = idx;
		opptr->num = num;
		opptr->trait_idx = traits - op_traits;
	} while (true);

	// Determine initial minimum sequence of operations that need to
	// be performed to create a detected issue.
	for (opptr = operations; opptr < operations + operations_num; opptr++) {
		opptr->perform = true;

		sb_t *sb = sb_create();
		if (sb == NULL) {
			fputs("Insufficient Memory\n", stderr);
			exit(6);
		}

		perform_operations(sb);

		err_string = validate(sb);
		if (err_string) {
			sb_dynstr_free(err_string);
			sb_delete(sb);
			break;
		}

		sb_delete(sb);
	}
	if (opptr >= (operations + operations_num)) {
		// All operations were performed.  Was a validate() detected issue
		// produced by the final operation?  If not, then this sequence of
		// operations does not produce a detected issue.
		sb_t *sb = sb_create();
		if (sb == NULL) {
			fputs("Insufficient Memory\n", stderr);
			exit(6);
		}

		perform_operations(sb);

		err_string = validate(sb);
		if (err_string == NULL) {
			fputs("No issue detected, even after executing all operations.\n",
				stderr);
			exit(7);
		} else {
			sb_dynstr_free(err_string);
		}
		sb_delete(sb);
	}

	// Selectively turn off perform flags for individual operations that
	// don't need to be performed to reproduce the validate() issue.
	for (opptr = operations; opptr < operations + operations_num; opptr++) {
		if (opptr->perform) {
			opptr->perform = false;

			sb_t *sb = sb_create();
			if (sb == NULL) {
				fputs("Insufficient Memory\n", stderr);
				exit(6);
			}

			perform_operations(sb);

			err_string = validate(sb);
			if (err_string == NULL) {
				// No issue detected, thus this operation is needed
				// to reproduce the detected issue.  Turn this operations
				// perform flag back on, so as to get back into a state
				// that produces the detected issue.
				opptr->perform = true;
			} else {
				sb_dynstr_free(err_string);
			}

			sb_delete(sb);
		}
	}


	// Display operations that still have perform flag set.
	puts("operations:");
	for (opptr = operations; opptr < operations + operations_num; opptr++) {
		if (opptr->perform) {
			puts(opptr->line);
		}
	}

	// Regenerate and display error string.
	sb_t *sb = sb_create();
	if (sb == NULL) {
		fputs("Insufficient Memory\n", stderr);
		exit(7);
	}
	perform_operations(sb);
	err_string = validate(sb);
	assert(err_string != NULL);
	printf("Failed: %s\n", err_string);
	sb_dynstr_free(err_string);
	sb_delete(sb);

	return 0;
}

static void perform_operations(sb_t *sb)
{
	for (struct operation *opptr = operations;
		opptr < operations + operations_num; opptr++) {
		if (opptr->perform) {
			sb_idx_t idx = opptr->idx;
			sb_num_t num = opptr->num;
			sb_idx_t nth_idx = opptr->nth_idx;

			switch (opptr->op) {
			case SET_BIT:
				sb_set(sb, idx);
				break;

			case SET_NUM:
				sb_setnum(sb, idx, num);
				break;

			case QUERY_ANYSET:
				(void) sb_anyset(sb);
				break;

			case QUERY_NUMSET:
				(void) sb_numset(sb);
				break;

			case QUERY_ISSET:
				(void) sb_isset(sb, idx);
				break;

			case QUERY_FIRSTSET:
				(void) sb_firstset(sb);
				break;

			case QUERY_NEXTSET:
				(void) sb_nextset(sb, idx);
				break;

			case QUERY_PREVSET:
				(void) sb_prevset(sb, idx);
				break;

			case CLEAR_BIT:
				sb_clear(sb, idx);
				break;

			case CLEAR_NUM:
				sb_clearnum(sb, idx, num);
				break;

			case QUERY_ANYCLEARED:
				(void) sb_anycleared(sb);
				break;

			case QUERY_NUMCLEARED:
				(void) sb_numcleared(sb);
				break;

			case QUERY_ISCLEARED:
				(void) sb_iscleared(sb, idx);
				break;

			case QUERY_FIRSTCLEARED:
				(void) sb_firstcleared(sb);

			case QUERY_NEXTCLEARED:
				(void) sb_nextcleared(sb, idx);
				break;

			case QUERY_PREVCLEARED:
				(void) sb_prevcleared(sb, idx);
				break;

			case QUERY_NTHSET:
				(void) sb_nthset(sb, nth_idx);
				break;

			default:
				assert(false);  // Unexpected operation
			}
		}
	}
}

sb_pdynstr_t validate(sb_t *sb)
{
	// Is the sparse bit array internally consistent?
	sb_pdynstr_t err_string = sb_validate(sb);
	if (err_string != NULL) {
		return err_string;
	}

	// Correct number set?
	sb_num_t expected_numset = ops_expected_numset();
	sb_num_t actual_numset = sb_numset(sb);
	if (actual_numset != expected_numset) {
		return sb_dyn_sprintf("Unexpected numset,\n"
			"  actual: %#" SB_PRI_NUM " expected: %#" SB_PRI_NUM,
			actual_numset, expected_numset);
	}

	// Do each of the bits, within all the operation ranges,
	// have the expected setting?
	bool first_pass = true;
	for (sb_idx_t idx = ops_ranges_idx_first(); first_pass || idx != 0;
		idx = ops_ranges_idx_next(idx)) {
		bool expected_isset = ops_expected_isset(idx);
		bool actual_isset = sb_isset(sb, idx);
		if (actual_isset != expected_isset) {
			return sb_dyn_sprintf("Unexpected isset,\n"
				"  idx: $#" SB_PRI_IDX "actual: %u expected: %u");
		}

		first_pass = false;
	}

	// Validate sb_nextset() and sb_nextcleared().
	sb_idx_t idx_edge = ops_ranges_idx_first();
	do {
		for (int offset = -1; offset <= 1; offset++) {
			sb_idx_t idx = idx_edge + offset;

			sb_idx_t expected_nextset = ops_expected_nextset(idx);
			sb_idx_t expected_nextcleared = ops_expected_nextcleared(idx);

			sb_idx_t actual_nextset = sb_nextset(sb, idx);
			sb_idx_t actual_nextcleared = sb_nextcleared(sb, idx);

			if (actual_nextset != expected_nextset) {
				return sb_dyn_sprintf("Unexpected nextset,\n"
					"  idx: %#" SB_PRI_IDX " actual: %#" SB_PRI_IDX
					" expected: %#" SB_PRI_IDX,
					idx, actual_nextset, expected_nextset);
			}

			if (actual_nextcleared != expected_nextcleared) {
				return sb_dyn_sprintf("Unexpected nextcleared,\n"
					"  idx: %#" SB_PRI_IDX " actual: %#" SB_PRI_IDX
					" expected: %#" SB_PRI_IDX,
					idx, actual_nextcleared, expected_nextcleared);
			}
		}
		idx_edge = ops_ranges_idx_next(idx_edge);
	} while (idx_edge != 0);

	// Validate sb_prevset() and sb_prevcleared().
	idx_edge = ops_ranges_idx_first();
	do {
		for (int offset = -1; offset <= 1; offset++) {
			sb_idx_t idx = idx_edge + offset;

			sb_idx_t expected_prevset = ops_expected_prevset(idx);
			sb_idx_t expected_prevcleared = ops_expected_prevcleared(idx);

			sb_idx_t actual_prevset = sb_prevset(sb, idx);
			sb_idx_t actual_prevcleared = sb_prevcleared(sb, idx);

			if (actual_prevset != expected_prevset) {
				return sb_dyn_sprintf("Unexpected prevset,\n"
					"  idx: %#" SB_PRI_IDX " actual: %#" SB_PRI_IDX
					" expected: %#" SB_PRI_IDX,
					idx, actual_prevset, expected_prevset);
			}

			if (actual_prevcleared != expected_prevcleared) {
				return sb_dyn_sprintf("Unexpected prevcleared,\n"
					"  idx: %#" SB_PRI_IDX " actual: %#" SB_PRI_IDX
					" expected: %#" SB_PRI_IDX,
					idx, actual_prevcleared, expected_prevcleared);
			}
		}
		idx_edge = ops_ranges_idx_next(idx_edge);
	} while (idx_edge != 0);

	// Validate sb_nthset() for nth index of each set bit, plus
	// 3 beyond the number of set bits.
	for (sb_idx_t nth_idx = 0; nth_idx <= ops_expected_numset() + 3; nth_idx++) {
		sb_idx_t expected_rv = ops_expected_nthset(nth_idx);
		error_t orig_errno = ESRCH + 5;
		error_t expected_errno = (nth_idx < ops_expected_numset())
			? orig_errno : ESRCH;
		errno = orig_errno;
		sb_idx_t actual_rv = sb_nthset(sb, nth_idx);
		error_t actual_errno = errno;

		if (actual_rv != expected_rv) {
			return sb_dyn_sprintf("Unexpected nthset rv,\n"
				"  nth_idx: %#" SB_PRI_IDX " actual_rv: %#" SB_PRI_IDX
				" expected_rv: %#" SB_PRI_IDX,
				nth_idx, actual_rv, expected_rv);
		}

		if (actual_errno != expected_errno) {
			return sb_dyn_sprintf("Unexpected nthset errno,\n"
				"  nth_idx: %#" SB_PRI_IDX " actual_errno: %u"
				" expected_errno: %u",
				nth_idx, actual_errno, expected_errno);
		}
	}

	return NULL;
}

static sb_idx_t ops_ranges_idx_first(void)
{
	sb_idx_t first_idx = SB_IDX_MAX;

	for (struct operation *opptr = operations;
		opptr < operations + operations_num; opptr++) {
		// Skip operations that are not to be performed.
		if (!opptr->perform) {
			continue;
		}

		if (opptr->idx < first_idx) {
			first_idx = opptr->idx;
		}
	}

	return first_idx;
}

static sb_idx_t ops_ranges_idx_next(sb_idx_t idx)
{
	bool found = false;
	sb_idx_t next_idx = SB_IDX_MAX;

	if (idx == SB_IDX_MAX) {
		return 0;
	}

	for (struct operation *opptr = operations;
		opptr < operations + operations_num; opptr++) {
		// Skip operations that are not to be performed.
		if (!opptr->perform) {
			continue;
		}
		sb_idx_t start = opptr->idx;
		sb_idx_t end = start + ((opptr->num == 0) ? 1 : opptr->num) - 1;

		if (end > idx) {
			if (start <= idx) {
				return idx + 1;
			}

			if (!found || (start < next_idx)) {
				next_idx = start;
				found = true;
			}
		}
	}

	return (found) ? next_idx : 0;
}

static bool ops_performed_any(void)
{
	for (struct operation *opptr = operations;
		opptr < operations + operations_num; opptr++) {
		if (opptr->perform) {
			return true;
		}
	}

	return false;
}

static bool ops_expected_isset(sb_idx_t idx)
{
	bool isset = false;

	for (struct operation *opptr = operations;
		opptr < operations + operations_num; opptr++) {
		// Skip operations that are not to be performed.
		if (!opptr->perform) {
			continue;
		}

		switch (opptr->op) {
		case SET_BIT:
			if (opptr->idx == idx) {
				isset = true;
			}
			break;

		case SET_NUM:
			if ((idx >= opptr->idx) && (idx <= (opptr->idx + opptr->num - 1))) {
				isset = true;
			}
			break;

		case CLEAR_BIT:
			if (opptr->idx == idx) {
				isset = false;
			}
			break;

		case CLEAR_NUM:
			if ((idx >= opptr->idx) && (idx <= (opptr->idx + opptr->num - 1))) {
				isset = false;
			}
			break;
		}
	}

	return isset;
}

static sb_num_t ops_expected_numset(void)
{
	sb_num_t num = 0;

	sb_idx_t idx = ops_ranges_idx_first();
	do {
		if (ops_expected_isset(idx)) {
			num++;
		}
		idx = ops_ranges_idx_next(idx);
	} while (idx != 0);

	return num;
}

static sb_idx_t ops_expected_nextset(sb_idx_t idx_start)
{
	sb_idx_t idx_edge = ops_ranges_idx_first();
	do {
		for (int offset = -1; offset <= 1; offset++) {
			sb_idx_t idx = idx_edge + offset;
			if ((offset > 0) && (idx < idx_edge)) {
				continue;
			}
			if ((offset < 0) && (idx > idx_edge)) {
				continue;
			}

			if (idx <= idx_start) {
				continue;
			}

			if (ops_expected_isset(idx)) {
				return idx;
			}
		}

		idx_edge = ops_ranges_idx_next(idx_edge);
	} while (idx_edge != 0);

	return 0;
}

static sb_idx_t ops_expected_nextcleared(sb_idx_t idx_start)
{
	if (!ops_performed_any()) {
		return idx_start + 1;
	}

	if (!ops_expected_isset(idx_start + 1)) {
		return idx_start + 1;
	}

	sb_idx_t idx_edge = ops_ranges_idx_first();
	do {
		for (int offset = -1; offset <= 1; offset++) {
			sb_idx_t idx = idx_edge + offset;
			if ((offset > 0) && (idx < idx_edge)) {
				continue;
			}
			if ((offset < 0) && (idx > idx_edge)) {
				continue;
			}

			if (idx <= idx_start) {
				continue;
			}

			if (!ops_expected_isset(idx)) {
				return idx;
			}
		}

		idx_edge = ops_ranges_idx_next(idx_edge);
	} while (idx_edge != 0);

	return 0;
}

static sb_idx_t ops_expected_prevset(sb_idx_t idx_start)
{
	sb_idx_t idx_rv = 0;

	sb_idx_t idx_edge = ops_ranges_idx_first();
	do {
		for (int offset = -1; offset <= 1; offset++) {
			sb_idx_t idx = idx_edge + offset;
			if ((offset > 0) && (idx < idx_edge)) {
				continue;
			}
			if ((offset < 0) && (idx > idx_edge)) {
				continue;
			}
			if (idx >= idx_start) { break; }

			if (ops_expected_isset(idx)) {
				if (idx > idx_rv) {
					idx_rv = idx;
				}
			}
		}

		idx_edge = ops_ranges_idx_next(idx_edge);
	} while (idx_edge != 0);

	return idx_rv;
}

static sb_idx_t ops_expected_prevcleared(sb_idx_t idx_start)
{
	sb_idx_t idx_rv = 0;

	if (idx_start == 0) { return 0; }
	if (!ops_expected_isset(idx_start - 1)) { idx_rv = idx_start - 1; }

	sb_idx_t idx_edge = ops_ranges_idx_first();
	do {
		for (int offset = -1; offset <= 1; offset++) {
			sb_idx_t idx = idx_edge + offset;
			if ((offset > 0) && (idx < idx_edge)) {
				continue;
			}
			if ((offset < 0) && (idx > idx_edge)) {
				continue;
			}

			if (idx >= idx_start) { break; }

			if (!ops_expected_isset(idx)) {
				if (idx > idx_rv) {
					idx_rv = idx;
				}
			}
		}

		idx_edge = ops_ranges_idx_next(idx_edge);
	} while (idx_edge != 0);

	return idx_rv;
}

static sb_idx_t ops_expected_nthset(sb_idx_t nth_idx)
{
	if (nth_idx >= ops_expected_numset()) {
		return 0;
	}

	sb_idx_t idx = (ops_expected_isset(0)) ? 0 : ops_expected_nextset(0);
	sb_num_t num = 0;

	while ((num < nth_idx) && ((num + 1) < ops_expected_numset())) {
		idx = ops_expected_nextset(idx);
		num++;
	}

	return idx;
}
