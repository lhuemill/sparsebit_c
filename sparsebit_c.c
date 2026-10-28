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
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <unistd.h>

#include <arpa/inet.h>

#define SB_TEST
#include "sparsebit_c.h"

#define MEMCLEAR(p, n) memset((p), 0, (n))
#define ARRAY_SIZE(array) (sizeof(array) / sizeof((array)[0]))
#define CONST_STRLEN(str) (ARRAY_SIZE(str) - 1)

// Complete definition of sparsebit root structure.
struct node;
typedef struct node node_t;
struct sb {
	node_t *root;
	node_t *hint;
};

enum node_color {
	BLACK = false,
	RED = true,
};

typedef uint64_t mask_t;
#define NODE_PRI_MASK PRIx64
#define NUM_MASK_BITS (sizeof(mask_t) * CHAR_BIT)
#define MASK_BOUNDARY(idx) ((idx) - ((idx) % NUM_MASK_BITS))
#define MASK_BIT_MASK(bit_idx) ((mask_t)1 << (bit_idx))

struct node {
	sb_idx_t idx_start;  // Index of first bit in mask.
	mask_t mask;
	sb_num_t set_after;  // Number of contiguously set bits, after last mask bit.

#ifdef SBIT_SUBTREENUM
	sb_num_t num_set_subtree;  // Number of bits set in this node plus all of
	                           // its children, even children of children.
	                           // Note: Nodes always have at least 1 set bit,
	                           // so a num_set_subtree value of 0, actually
	                           // represents SB_NUM_MAX + 1 set bits.
#endif

	enum node_color color;
	node_t *parent;
	node_t *left;
	node_t *right;
};

static node_t *node_find(sb_t *sb, sb_idx_t idx);
static node_t *node_find_or_next(sb_t *sb, sb_idx_t idx);
static node_t *node_find_or_prev(sb_t *sb, sb_idx_t idx);
static node_t *node_find_or_create(sb_t *sb, sb_idx_t idx);
static const node_t *node_find_const(const sb_t *sb_const, sb_idx_t idx);
static const node_t *node_find_or_next_const(const sb_t *sb_const,
	sb_idx_t idx);
static const node_t *node_find_or_prev_const(const sb_t *sb_const,
	sb_idx_t idx);
static bool node_supports_idx(const node_t *node, sb_idx_t idx);
static int node_cmp(node_t *node1, node_t *node2);
static int node_cmp_idx(node_t *node, sb_idx_t idx);
static bool node_prev_adj(node_t *node);
static node_t *node_split(sb_t *sb, node_t *node, sb_idx_t at);
static void node_merge(sb_t *sb, node_t *prev, node_t *next);
static void node_adjacent_fixup(sb_t *sb, sb_idx_t idx_start, sb_idx_t idx_end);
static bool mask_any_set(mask_t mask);
static bool mask_only_leading(mask_t mask);

static unsigned int num_set_mask(mask_t mask);
static sb_num_t num_set_node(const node_t *node);
static sb_num_t num_set_subtree(const node_t *node);
static void num_set_adjust(node_t *node, sb_num_t amt, bool positive);

static bool node_is_bud(node_t *node);
static unsigned int node_depth(const node_t *node);
static unsigned int node_depth_black(const node_t *node);
static node_t *node_grandparent(node_t *node);
static node_t *node_uncle(node_t *node);
static node_t *node_minimum(node_t *node);
static node_t *node_maximum(node_t *node);
static node_t *node_next(node_t *node);
static node_t *node_prev(node_t *node);
static const node_t *node_minimum_const(const node_t *nodep_const);
static const node_t *node_next_const(const node_t *nodep_const);
static const node_t *node_prev_const(const node_t *nodep_const);
static void node_rotate_left(sb_t *sb, node_t *x);
static void node_rotate_right(sb_t *sb, node_t *x);
static void node_transplant(sb_t *sb, node_t *u, node_t *v);
static void node_insert(sb_t *sb, node_t *parent, node_t *node);
static void node_delete(sb_t *sb, node_t *node);
static void node_delete_fixup(sb_t *sb, node_t *node);
static sb_pdynstr_t dump_subtree(const sb_t *sb, const node_t *node,
	unsigned int indent);

static int node_write(FILE *file, node_t *node, sb_idx_t *base_idx);
static int record_write(FILE *file, const union sb_rcd *record,
	sb_idx_t *base_idx);
static sb_idx_t record_idx_adjust(const union sb_rcd *record);
static void record_to_record_packed(const union sb_rcd *record,
	union sb_rcd_packed *record_packed);
static sb_rcd_id_t packed_id_byte_to_id(uint8_t packed_id_byte);
static void record_packed_to_record(const union sb_rcd_packed *record_packed,
	union sb_rcd *record);
static size_t record_packed_size(sb_rcd_id_t id);

sb_t *sb_create(void)
{
	sb_t *p = calloc(1, sizeof(sb_t));
	if (p == NULL) {
		errno = ENOMEM;
		return NULL;
	}

	return p;
}

void sb_delete(sb_t* sb)
{
	while (sb->root) {
		sb->root->mask = 0;
		sb->root->set_after = 0;
		node_delete(sb, sb->root);
	}

	free(sb);
}

int sb_copy(sb_t *dest, const sb_t *src)
{
	// Nothing to do if source and destination are the same.
	if (src == dest) {
		return 0;
	}

	// Delete any nodes that currently exist at destination.
	while (dest->root) {
		node_delete(dest, dest->root);
	}
	dest->hint = NULL;

	// For each node in the source, create and add to source a
	// duplicate of the node.
	if (src->root != NULL) {
		for (const node_t *node_src = node_minimum(src->root);
			// TODO: create const version of node_next().
			node_src != NULL; node_src = (const node_t *) node_next(
				(node_t *) node_src)) {
			node_t *node_dest = calloc(1, sizeof(node_t));

			node_dest->idx_start = node_src->idx_start;
			node_dest->mask = node_src->mask;
			node_dest->set_after = node_src->set_after;

			node_t *parent = dest->root;
			while (parent != NULL) {
				assert(!node_supports_idx(parent, node_dest->idx_start));
				if (node_dest->idx_start < parent->idx_start) {
					if (parent->left) {
						parent = parent->left;
						continue;
					} else {
						break;
					}
				} else {
					if (parent->right) {
						parent = parent->right;
						continue;
					} else {
						break;
					}
				}
			}

			node_insert(dest, parent, node_dest);
		}
	}

	return 0;
}

// ==== Set Operations
int sb_set(sb_t* sb, sb_idx_t idx)
{
	sb_idx_t idx_boundary = MASK_BOUNDARY(idx);
	node_t *node = node_find_or_create(sb, idx_boundary);
	if (node == NULL) {
		errno = ENOMEM;
		return -1;
	}

	// Is idx beyond nodes mask bits (e.g. part of set_after)?
	if ((idx - node->idx_start) >= NUM_MASK_BITS) {
		// Is the bit already set?
		if (idx <= (node->idx_start + NUM_MASK_BITS + node->set_after - 1)) {
			return 0;
		}

		// Is bit just 1 beyond the bits described by the node's set_after value?
		if (idx == (node->idx_start + NUM_MASK_BITS + node->set_after)) {
			node->set_after++;
			num_set_adjust(node, 1, true);
			node_adjacent_fixup(sb, idx, idx);

			return 0;
		}

		// Gap between last bit set via node->set_after and index of bit
		// to be set.
		node = node_split(sb, node, MASK_BOUNDARY(idx));
		if (node == NULL) {
			errno = ENOMEM;
			return -1;
		}
	}

	mask_t mask = MASK_BIT_MASK(idx - node->idx_start);
#ifndef SBIT_SUBTREENUM
	if ((node->mask & mask) == 0) {
		node->mask |= mask;
		num_set_adjust(node, 1, true);
	}
#else
	if ((node->mask & mask) == 0) {
		node->mask |= mask;
		num_set_adjust(node, 1, true);
	}
#endif

	node_adjacent_fixup(sb, idx, idx);

	return 0;
}

int sb_setnum(sb_t* sb, sb_idx_t idx_start, sb_num_t num)
{
	if (num == 0) {
		return 0;
	}

	// Does range extend beyond the maximum index?
	if ((idx_start + num - 1) < idx_start) {
		errno = EINVAL;
		return -1;
	}

	node_t *node_start = node_find_or_create(sb, MASK_BOUNDARY(idx_start));
	if (node_start == NULL) {
		errno = ENOMEM;
		return -1;
	}
	if (idx_start > (node_start->idx_start + NUM_MASK_BITS
		+ node_start->set_after - 1)) {
		node_start = node_split(sb, node_start, MASK_BOUNDARY(idx_start));
		if (node_start == NULL) {
			node_adjacent_fixup(sb, idx_start, idx_start + num - 1);
			errno = ENOMEM;

			return -1;
		}
	}

	sb_idx_t idx_end = idx_start + num - 1;
	node_t *node_end = node_find_or_create(sb, MASK_BOUNDARY(idx_end));
	if (node_end == NULL) {
		// Was a new empty node for the start created?  If so, need to
		// remove that empty node, before returning the ENOMEM indication
		// for the end node.
		if (num_set_node(node_start) == 0) {
			node_delete(sb, node_start);
		}
		errno = ENOMEM;
		return -1;
	}
	// TODO: Don't split if end can be handled by just increasing
	//       the current node_end->set_after value.
	if (node_end->idx_start != MASK_BOUNDARY(idx_end)) {
		node_end = node_split(sb, node_end, MASK_BOUNDARY(idx_end));
		if (node_end == NULL) {
			node_adjacent_fixup(sb, idx_start, idx_start + num - 1);
			errno = ENOMEM;
			return -1;
		}
	}

	// Remove any intermediate nodes, between the start and end nodes.
	while ((node_start != node_end) && (node_next(node_start) != node_end)) {
		node_t *node = node_next(node_start);
		assert(sb_numset(sb) >= num_set_node(node));
		assert(num_set_node(node) >= 1);
		num_set_adjust(node, num_set_node(node), false);
		node_delete(sb, node);
	}

	// As needed, set mask bits from the start until the next mask boundary.
	sb_idx_t idx = idx_start;
	sb_num_t num_mask_set = 0;
	while ((idx <= idx_end) && (idx >= idx_start)
		&& ((idx - node_start->idx_start) < NUM_MASK_BITS)) {
		unsigned int offset = idx - node_start->idx_start;
		assert(offset < NUM_MASK_BITS);
		mask_t mask = MASK_BIT_MASK(offset);
		if ((mask & node_start->mask) == 0) {
			node_start->mask |= mask;
			num_mask_set++;
		}
		idx++;
	}
	num_set_adjust(node_start, num_mask_set, true);

	// As needed, handle bits from last mask boundary to ending index.
	if (idx <= idx_end) {
		idx = (node_end->idx_start > idx_start)
			? node_end->idx_start : idx_start;
		num_mask_set = 0;
		while ((idx <= idx_end) && (idx >= node_end->idx_start)
			&& ((idx - node_end->idx_start) < NUM_MASK_BITS)) {
			unsigned int offset = idx - node_end->idx_start;
			assert(offset < NUM_MASK_BITS);
			mask_t mask = MASK_BIT_MASK(offset);
			if ((mask & node_end->mask) == 0) {
				node_end->mask |= mask;
				num_mask_set++;
			}
			idx++;
		}
		num_set_adjust(node_end, num_mask_set, true);
	}

	// If needed, set all bits in gap between start and end nodes.
	if (node_start != node_end) {
		if ((node_start->idx_start + NUM_MASK_BITS + node_start->set_after)
			!= node_end->idx_start) {
				assert((node_start->idx_start + NUM_MASK_BITS + node_start->set_after)
					< node_end->idx_start);
				sb_num_t num = node_end->idx_start - (node_start->idx_start
					+ NUM_MASK_BITS + node_start->set_after);
				node_start->set_after += num;
				num_set_adjust(node_start, num, true);
			}
	}

	node_adjacent_fixup(sb, idx_start, idx_start + num - 1);

	return 0;
}

bool sb_anyset(const sb_t* sb)
{
	if (sb->root) {
		return true;
	}

	return false;
}

bool sb_allset(const sb_t* sb)
{
#ifdef SBIT_SUBTREENUM
	if (sb->root && (sb->root->num_set_subtree == 0)) {
		assert(sb->root->idx_start == 0);
		assert(sb->root->mask == ~((mask_t) 0));
		assert(sb->root->set_after == (((sb_num_t) 0) - NUM_MASK_BITS));
		assert(sb->root->left == NULL);
		assert(sb->root->right == NULL);
		return true;
	}
#else
	// Note: Due to overflow, SB_IDX_MAX + 1 == 0.
	if (sb->root && (num_set_subtree(sb->root) == (SB_IDX_MAX + 1))) {
		return true;
	}
#endif

	return false;
}

sb_num_t sb_numset(const sb_t* sb)
{
	return (sb->root != NULL) ? num_set_subtree(sb->root) : 0;
}

bool sb_isset(const sb_t* sb, sb_idx_t idx)
{
	const node_t *node = node_find_const(sb, idx);

	if (node == NULL) {
		return false;
	}

	if ((idx - node->idx_start) < NUM_MASK_BITS) {
		return node->mask & MASK_BIT_MASK(idx - node->idx_start);
	} else {
		// Index of bit is within range of bits specified by setting of
		// set_after and thus is set.
		return true;
	}

	// NOT REACHED
	assert(false);
}

sb_idx_t sb_firstset(const sb_t* sb)
{
	if (sb->root != NULL) {
		node_t *node = node_minimum(sb->root);
		for (size_t n = 0; n < NUM_MASK_BITS; n++) {
			if (node->mask & MASK_BIT_MASK(n)) {
				return node->idx_start + n;
			}
		}
		assert(0);  // Node with no mask bits set.
	}

	return 0;
}

sb_idx_t sb_lastset(const sb_t* sb)
{
	if (sb->root != NULL) {
		node_t *node = node_maximum(sb->root);
		if (node->set_after > 0) {
			return node->idx_start + NUM_MASK_BITS + node->set_after - 1;
		}
		for (ssize_t n = NUM_MASK_BITS - 1; n >= 0; n--) {
			if (node->mask & MASK_BIT_MASK(n)) {
				return node->idx_start + n;
			}
		}
		assert(0);  // Node with no mask bits set.
	}

	return 0;
}

sb_idx_t sb_nextset(const sb_t* sb, sb_idx_t idx)
{
	const node_t *node = node_find_or_next_const(sb, idx);

	// No node with idx or a node after that, so there is no next set bit.
	if (node == NULL) {
		return 0;
	}

	// Does node point to the node supporting idx?
	if (node_supports_idx(node, idx)) {
		// Is idx within the mask or part of the set_after range?
		if ((idx - node->idx_start) < NUM_MASK_BITS) {
			// Is there another set bit within the mask beyond the mask bit for the
			// idx?
			for (size_t n = (idx - node->idx_start) + 1;
				n < NUM_MASK_BITS; n++) {
				if (node->mask & MASK_BIT_MASK(n)) {
					return node->idx_start + n;
				}
			}
			// If there are any set_after bits, return the index of the first
			// set_after bit.
			if (node->set_after > 0) {
				return node->idx_start + NUM_MASK_BITS;
			}
		} else {
			// If there is another set_after bit beyond the one at idx,
			// return the index of the next set_after bit.
			if (((idx + 1) > idx) && ((idx + 1)
				<= (node->idx_start + NUM_MASK_BITS + node->set_after - 1))) {
				return idx + 1;
			}
		}

		// Node supports idx, but there are no mask bits beyond the mask bit for
		// idx that are set. As such, next set bit beyond idx will be the first
		// set bit in the next node.
		node = node_next_const(node);
		if (node == NULL) {
			return 0;
		}
	}

	// The node pointer points to the first node after the node supporting
	// idx.  The first mask bit set in node, will be for the first set bit
	// after idx.
	for (size_t n = 0; n < NUM_MASK_BITS; n++) {
		if (node->mask & MASK_BIT_MASK(n)) {
			return node->idx_start + n;
		}
	}
	assert(0);  // Node with no mask bits set.

	return 0;
}

sb_idx_t sb_prevset(const sb_t* sb, sb_idx_t idx)
{
	const node_t *node = node_find_or_prev_const(sb, idx);

	// No node with idx or a node prior that, so there is no previously set bit.
	if (node == NULL) {
		return 0;
	}
	((sb_t *)sb)->hint = (node_t *) node;

	// Does node point to the node supporting idx?
	if (node_supports_idx(node, idx)) {
		// Is idx part of the node's set_after bits and not
		// the first set_after bit?
		if ((idx - node->idx_start) > NUM_MASK_BITS) {
			return idx - 1;
		}
		// Idx is either the first set_after bit or within the node's mask.
		// If present, return index of first set mask bit before the bit
		// of idx.
		for (ssize_t n = (idx - node->idx_start) - 1; n >= 0; n--) {
			if (node->mask & MASK_BIT_MASK(n)) {
				return node->idx_start + n;
			}
		}

		// Node supports idx, but there are no mask bits before the mask bit for
		// idx that are set. As such, previous set bit of idx will be the last
		// set bit of the previous node.
		node = node_prev_const(node);
		if (node == NULL) {
			return 0;
		}
	}

	// The node pointer points to the first node supporting bits bits idx.
	// If that node has any set after bits, return the index to the highest
	// set after bit.  Otherwise return index of highest set mask bit.
	if (node->set_after) {
		return node->idx_start + NUM_MASK_BITS + node->set_after - 1;
	}
	for (ssize_t n = NUM_MASK_BITS - 1; n >= 0; n--) {
		if (node->mask & MASK_BIT_MASK(n)) {
			return node->idx_start + n;
		}
	}
	assert(0);  // Node with no mask bits set.

	return 0;
}

// On success returns index of zero-based Nth set bit.
// When there are not enough set bits, errno is set equal to ESRCH
// and an index of 0 is returned.
sb_idx_t sb_nthset(const sb_t* sb, sb_idx_t nth)
{
	if ((nth >= sb_numset(sb)) && !sb_allset(sb)) {
		errno = ESRCH;
		return 0;
	}

	if (sb_allset(sb)) {
		return nth;
	}

	const node_t *node = sb->root;
	assert(node != NULL);
	sb_num_t num = nth;
	do {
		sb_num_t low = (node->left) ? num_set_subtree(node->left)
			: (num_set_subtree(node)
				- ((node->right) ? num_set_subtree(node->right) : 0)
				- num_set_node(node));
		sb_num_t high = num_set_subtree(node)
				- ((node->right) ? num_set_subtree(node->right) : 0) - 1;
		if (num < low) {
			assert(node->left != NULL);
			node = node->left;
		} else if (num > high) {
			sb_num_t amt = num_set_subtree(node) - num_set_subtree(node->right);
			assert(num >= amt);
			num -= amt;
			assert(node->right != NULL);
			node = node->right;
		} else {
			unsigned int num_mask_set = 0;
			for (size_t mask_idx = 0; mask_idx < NUM_MASK_BITS; mask_idx++) {
				if (node->mask & MASK_BIT_MASK(mask_idx)) {
					num_mask_set++;
				}
				if ((num + 1) == (low + num_mask_set)) {
					return node->idx_start + mask_idx;
				}
			}

			return (node->idx_start + NUM_MASK_BITS) + (num - (low + num_mask_set));
		}
	} while (true);
}

// ==== Clear/Cleared Operations
int sb_clear(sb_t* sb, sb_idx_t idx)
{
	node_t *node = node_find(sb, idx);

	if (node == NULL) {
		return 0;
	}

	// Is bit to be cleared described by nodes set_after value?
	if ((idx - node->idx_start) >= NUM_MASK_BITS) {
		node = node_split(sb, node, MASK_BOUNDARY(idx));
		if (node == NULL) {
			errno = ENOMEM;
			return -1;
		}
	}

	mask_t mask = MASK_BIT_MASK(idx - node->idx_start);
	if (node->mask & mask) {
		node->mask &= ~mask;
		num_set_adjust(node, 1, false);
	}

	// Are all the mask bits cleared?
	if (!mask_any_set(node->mask)) {
		// If no set_after bits, then just need to delete the node.
		if (node->set_after == 0) {
			node_delete(sb, node);
			node = NULL;
		} else {
			// Node still has some set_after bits. Move up to NUM_MASK_BITS
			// worth of set_after bits into the nodes mask.
			assert(node->mask == 0);
			assert((node->idx_start + NUM_MASK_BITS) > node->idx_start);
			node->idx_start += NUM_MASK_BITS;
			for (size_t bit_idx = 0; (bit_idx < NUM_MASK_BITS)
				&& (node->set_after > 0); bit_idx++) {
				node->mask |= MASK_BIT_MASK(bit_idx);
				node->set_after--;
			}
		}
	}

	sb_idx_t next_set = sb_nextset(sb, idx);
	next_set = (next_set == 0) ? SB_IDX_MAX : next_set;
	node_adjacent_fixup(sb, idx, next_set);

	return 0;
}

int sb_clearnum(sb_t* sb, sb_idx_t idx_start, sb_num_t num)
{
	if (num == 0) {
		return 0;
	}

	// Does range extended beyond the maximum index?
	sb_idx_t idx_end = idx_start + num - 1;
	if ((idx_start + num - 1) < idx_start) {
		errno = EINVAL;
		return -1;
	}

	if (sb->root == NULL) {
		return 0;
	}

	node_t *node_start = node_find_or_next(sb, idx_start);
	if (node_start == NULL) {
		return 0;
	}
	if (node_start->idx_start > idx_end) {
		return 0;
	}
	// Is idx_start part of the set_after portion of node_start?
	// If so, split node_start so that idx_start is within the
	// mask portion of a node.
	if (node_supports_idx(node_start, idx_start)
		&& (idx_start > (node_start->idx_start + NUM_MASK_BITS - 1))) {
		node_start = node_split(sb, node_start, MASK_BOUNDARY(idx_start));
		if (node_start == NULL) {
			node_adjacent_fixup(sb, idx_start, idx_start + num - 1);

			errno = ENOMEM;
			return -1;
		}
	}

	node_t *node_end = node_find_or_next(sb, MASK_BOUNDARY(idx_end));
	// Is idx_end part of the set_after portion of node_end?
	// If so, split node_end so that idx_end is within the
	// mask portion of a node.
	if ((node_end != NULL) && node_supports_idx(node_end, MASK_BOUNDARY(idx_end))
		&& (idx_end > (node_end->idx_start + NUM_MASK_BITS - 1))) {
		node_end = node_split(sb, node_end, MASK_BOUNDARY(idx_end));
		if (node_end == NULL) {
			node_adjacent_fixup(sb, idx_start, idx_start + num - 1);

			errno = ENOMEM;
			return -1;
		}
	}

	// As needed, remove intermediate nodes between node_start and node_end.
	while ((node_start != node_end) && (node_next(node_start) != node_end)) {
		node_t *node = node_next(node_start);
		assert(sb_numset(sb) >= num_set_node(node));
		node_delete(sb, node);
	}
	// Remove node_start in cases where there is no node that supports
	// idx_start and node_start doesn't point to the same node as the
	// node that supports idx_end.  In such cases, node_start will point
	// to the next node beyond where a node to support idx_start would be.
	if ((node_start != node_end) && !node_supports_idx(node_start, idx_start)) {
		assert(sb_numset(sb) >= num_set_node(node_start));
		node_delete(sb, node_start);
		node_start = node_end;
		if (node_start == NULL) {
			node_adjacent_fixup(sb, idx_start, idx_start + num - 1);

			return 0;
		}
	}

	// As needed, clear mask bits from the start until the next mask boundary.
	if ((idx_start >= node_start->idx_start)
		&& (idx_start <= (node_start->idx_start + NUM_MASK_BITS - 1))) {
		sb_idx_t idx = idx_start;
		sb_num_t num_cleared = 0;
		while ((idx <= idx_end) && (idx >= idx_start)
			&& ((idx - node_start->idx_start) < NUM_MASK_BITS)) {
			unsigned int offset = idx - node_start->idx_start;
			assert(offset < NUM_MASK_BITS);
			mask_t mask = MASK_BIT_MASK(offset);
			if ((mask & node_start->mask) != 0) {
				node_start->mask &= ~mask;
				num_cleared++;
				if (!mask_any_set(node_start->mask)) {
					break;
				}
			}
			idx++;
		}
		if ((node_start != node_end) && (node_start->set_after > 0)) {
			assert(sb_numset(sb) >= node_start->set_after);
			num_cleared += node_start->set_after;
			node_start->set_after = 0;
		}
		num_set_adjust(node_start, num_cleared, false);
		if (!mask_any_set(node_start->mask)) {
			// If no set_after bits, then just need to delete the node.
			if (node_start->set_after == 0) {
				node_delete(sb, node_start);
				if (node_end == node_start) {
					node_end = NULL;
				}
				node_start = NULL;
			} else {
				// Node still has some set_after bits. Move up to NUM_MASK_BITS
				// worth of set_after bits into the nodes mask.
				assert(node_start->mask == 0);
				assert((node_start->idx_start + NUM_MASK_BITS)
					> node_start->idx_start);
				node_start->idx_start += NUM_MASK_BITS;
				for (size_t bit_idx = 0; (bit_idx < NUM_MASK_BITS)
					&& (node_start->set_after > 0); bit_idx++) {
					node_start->mask |= MASK_BIT_MASK(bit_idx);
					node_start->set_after--;
				}
			}
		}
	}

	// As needed, handle bits from last mask boundary to ending index.
	if ((node_end != NULL)
		&& ((node_start != node_end) || (idx_start < node_end->idx_start))) {
		sb_idx_t idx = MASK_BOUNDARY(idx_end);
		sb_num_t num_cleared = 0;
		while ((idx <= idx_end) && (idx >= node_end->idx_start)
			&& ((idx - node_end->idx_start) < NUM_MASK_BITS)) {
			unsigned int offset = idx - node_end->idx_start;
			assert(offset < NUM_MASK_BITS);
			mask_t mask = MASK_BIT_MASK(offset);
			if ((mask & node_end->mask) != 0) {
				node_end->mask &= ~mask;
				num_cleared++;
			}
			if (!mask_any_set(node_end->mask)) {
				break;
			}
			idx++;
		}
		num_set_adjust(node_end, num_cleared, false);
		if (!mask_any_set(node_end->mask)) {
			// If no set_after bits, then just need to delete the node.
			if (node_end->set_after == 0) {
				node_delete(sb, node_end);
				node_end = NULL;
			} else {
				// Node still has some set_after bits. Move up to NUM_MASK_BITS
				// worth of set_after bits into the nodes mask.
				assert(node_end->mask == 0);
				assert((node_end->idx_start + NUM_MASK_BITS)
					> node_end->idx_start);
				node_end->idx_start += NUM_MASK_BITS;
				for (size_t bit_idx = 0; (bit_idx < NUM_MASK_BITS)
					&& (node_end->set_after > 0); bit_idx++) {
					node_end->mask |= MASK_BIT_MASK(bit_idx);
					node_end->set_after--;
				}
			}
		}
	}

	node_adjacent_fixup(sb, idx_start, idx_end);

	return 0;
}

bool sb_anycleared(const sb_t* sb)
{
	// sb_numcleared() returns 0 both when all bits are set and all bits
	// are cleared, because there are a total of ~(sb_num_t)0 + 1 bits.
	// With overflow, one more than ~(sb_num_t)0 is 0.
	return sb_numcleared(sb) || (sb->root == NULL);
}

sb_num_t sb_numcleared(const sb_t* sb)
{
	return (sb_num_t)0 - sb_numset(sb);
}

bool sb_iscleared(const sb_t* sb, sb_idx_t idx)
{
	return !sb_isset(sb, idx);
}

sb_idx_t sb_firstcleared(const sb_t* sb)
{
	if (sb->root != NULL) {
		sb_idx_t prev_node_idx_end = (sb_idx_t)0 - 1;

		// First gap in node starting index or a node with a mask that contains
		// a cleared bit will determine the index of the first cleared bit.
		for (node_t * node = node_minimum(sb->root); node != NULL;
			node = node_next(node)) {
			// Is there a gap in the node starting index.
			if (node->idx_start != (prev_node_idx_end + 1)) {
				return prev_node_idx_end + 1;
			}

			if (node->mask != ~(mask_t)0) {
				for (size_t n = 0; n < NUM_MASK_BITS; n++) {
					if ((node->mask & MASK_BIT_MASK(n)) == 0) {
						return node->idx_start + n;
					}
				}
				assert(0);  // Cleared bit not found.
			}

			prev_node_idx_end = node->idx_start + NUM_MASK_BITS + node->set_after - 1;
		}

		return prev_node_idx_end + 1;
	}

	return 0;
}

sb_idx_t sb_nextcleared(const sb_t* sb, sb_idx_t idx)
{
	if (idx == SB_IDX_MAX) {
		return 0;
	}

	const node_t *node = node_find_const(sb, idx + 1);

	if (node == NULL) {
		// No node that supports idx.
		return idx + 1;
	} else {
		// Node supports idx + 1.  Is there a cleared bit at or beyond the mask
		// bit for idx + 1?
		for (size_t n = (idx + 1) - node->idx_start;
			n < NUM_MASK_BITS; n++) {
			if ((node->mask & MASK_BIT_MASK(n)) == 0) {
				return node->idx_start + n;
			}
		}
	}

	// No cleared bits in mask after mask bit for idx.
	// Is there a cleared bit after bits described by next_set and the
	// potential next node?
	if ((node->set_after % NUM_MASK_BITS) != 0) {
		return node->idx_start + NUM_MASK_BITS + node->set_after;
	}

	// Next cleared bit will be in the the next node with a cleared mask bit
	// or first index supported by a gap in the node ranges.
	for (sb_idx_t idx2 = node->idx_start + node->set_after
		+ (NUM_MASK_BITS - (node->set_after % NUM_MASK_BITS));
		idx2 > idx; idx2 += NUM_MASK_BITS) {
		node = node_find_const(sb, idx2);

		if (node == NULL) {
			// Gap in indexes supported by nodes.
			return idx2;
		} else {
			// Any cleared bits in the node?
			if (node->mask != ~(mask_t)0) {
				for (size_t n = 0; n < NUM_MASK_BITS; n++) {
					if ((node->mask & MASK_BIT_MASK(n)) == 0) {
						return node->idx_start + n;
					}
				}
				assert(0);  // Mask asyncronously changed.  No longer has a
				            // cleared mask bit.
			}

			// Is there a gap between this and the next node.  If so, return
			// index of last bit set in this node plus one.
			const node_t *node2 = node_next_const(node);
			if ((node2 == NULL) || (node2->idx_start != (node->idx_start
				+ NUM_MASK_BITS + node->set_after))) {
				return node->idx_start + NUM_MASK_BITS + node->set_after;
			}
		}
	}

	return 0;
}

sb_idx_t sb_prevcleared(const sb_t* sb, sb_idx_t idx)
{
	if (idx == 0) {
		return 0;
	}

	const node_t *node = node_find_const(sb, idx - 1);

	if (node == NULL) {
		// No node that supports idx - 1.
		return idx - 1;
	}
	((sb_t *)sb)->hint = (node_t *)node;

	// Node supports idx - 1.  Is there a cleared bit at or before the mask
	// bit for idx - 1?
	for (ssize_t n = (((idx - 1) - node->idx_start) >= NUM_MASK_BITS)
		? NUM_MASK_BITS - 1
		: (idx - 1) - node->idx_start; n >= 0; n--) {
		if ((node->mask & MASK_BIT_MASK(n)) == 0) {
			return node->idx_start + n;
		}
	}

	// No cleared bit in mask before mask bit for idx - 1.
	do {
		const node_t *prev = node_prev_const(node);

		if (prev == NULL) {
			return (node->idx_start != 0) ? node->idx_start - 1 : 0;
		}

		// If there is a gap between current and previous node, return
		// starting index of current node minus 1.
		if ((prev->idx_start + NUM_MASK_BITS + prev->set_after)
			!= node->idx_start) {
			assert((prev->idx_start + NUM_MASK_BITS + prev->set_after)
				< node->idx_start);
			return node->idx_start - 1;
		}

		for (ssize_t n = NUM_MASK_BITS - 1; n >= 0; n--) {
			if ((prev->mask & MASK_BIT_MASK(n)) == 0) {
				return prev->idx_start + n;
			}
		}

		node = prev;
	} while (node != NULL);

	// Is there a cleared bit after bits described by next_set and the
	// potential next node?
	if ((node->set_after % NUM_MASK_BITS) != 0) {
		return node->idx_start + NUM_MASK_BITS + node->set_after;
	}

	// Next cleared bit will be in the the next node with a cleared mask bit
	// or first index supported by a gap in the node ranges.
	for (sb_idx_t idx2 = node->idx_start + node->set_after
		+ (NUM_MASK_BITS - (node->set_after % NUM_MASK_BITS));
		idx2 > idx; idx2 += NUM_MASK_BITS) {
		node = node_find_const(sb, idx2);

		if (node == NULL) {
			// Gap in indexes supported by nodes.
			return idx2;
		} else {
			// Any cleared bits in the node?
			if (node->mask != ~(mask_t)0) {
				for (size_t n = 0; n < NUM_MASK_BITS; n++) {
					if ((node->mask & MASK_BIT_MASK(n)) == 0) {
						return node->idx_start + n;
					}
				}
				assert(0);  // Mask asyncronously changed.  No longer has a
				            // cleared mask bit.
			}

			// Is there a gap between this and the next node.  If so, return
			// index of last bit set in this node plus one.
			const node_t *node2 = node_next_const(node);
			if ((node2 == NULL) || (node2->idx_start != (node->idx_start
				+ NUM_MASK_BITS + node->set_after))) {
				return node->idx_start + NUM_MASK_BITS + node->set_after;
			}
		}
	}
	return 0;
}

// ==== Sparsebit Node Operations ====
static node_t *node_find(sb_t *sb, sb_idx_t idx)
{
	node_t *node;

	if ((sb->hint != NULL) && node_supports_idx(sb->hint, idx)) {
		return sb->hint;
	}

	node = sb->root;
	while ((node != NULL) && (!node_supports_idx(node, idx))) {
		int cmp_rv = node_cmp_idx(node, idx);

		assert(cmp_rv != 0);  // Only in this loop when node doesn't support idx,
		                      // so why does node->idx_start == idx?
		if (cmp_rv > 0) {
			node = node->left;
		} else {
			node = node->right;
		}
	}
	sb->hint = node;

	return node;
}

static node_t *node_find_or_next(sb_t *sb, sb_idx_t idx)
{
	node_t *node;

	if ((sb->hint != NULL) && node_supports_idx(sb->hint, idx)) {
		return sb->hint;
	}

	node = sb->root;
	while ((node != NULL) && (!node_supports_idx(node, idx))) {
		int cmp_rv = node_cmp_idx(node, idx);

		assert(cmp_rv != 0);  // Only in this loop when node doesn't support idx,
		                      // so why does node->idx_start == idx?
		if (cmp_rv > 0) {
			if (node->left == NULL) {
				sb->hint = node;
				return node;
			}

			node = node->left;
		} else {
			if (node->right == NULL) {
				node = node_next(node);
				sb->hint = node;
				return node;
			}

			node = node->right;
		}
	}
	sb->hint = node;

	return node;
}

static node_t *node_find_or_prev(sb_t *sb, sb_idx_t idx)
{
	node_t *node;

	if ((sb->hint != NULL) && node_supports_idx(sb->hint, idx)) {
		return sb->hint;
	}

	node = sb->root;
	while ((node != NULL) && (!node_supports_idx(node, idx))) {
		int cmp_rv = node_cmp_idx(node, idx);

		assert(cmp_rv != 0);  // Only in this loop when node doesn't support idx,
		                      // so why does node->idx_start == idx?
		if (cmp_rv > 0) {
			if (node->left == NULL) {
				node = node_prev(node);
				sb->hint = node;
				return node;
			}

			node = node->left;
		} else {
			if (node->right == NULL) {
				sb->hint = node;
				return node;
			}

			node = node->right;
		}
	}
	sb->hint = node;

	return node;
}

static node_t *node_find_or_create(sb_t *sb, sb_idx_t idx)
{
	node_t *node;

	if ((sb->hint != NULL) && node_supports_idx(sb->hint, idx)) {
		return sb->hint;
	}

	// Create root node, if it doesn't already exist.
	if (sb->root == NULL) {
		node = calloc(1, sizeof(node_t));
		node->idx_start = MASK_BOUNDARY(idx);
		node_insert(sb, NULL, node);

		sb->hint = node;
		return node;
	}

	node = sb->root;
	while (!node_supports_idx(node, idx)) {
		if (idx < node->idx_start) {
			if (node->left) {
				node = node->left;
				continue;
			}

			node_t *parent = node;
			node_t *child = calloc(1, sizeof(node_t));
			if (child != NULL) {
				child->idx_start = MASK_BOUNDARY(idx);
				node_insert(sb, parent, child);
			}

			assert((child == NULL) || node_supports_idx(child, idx));

			sb->hint = child;
			return child;
		} else {
			if (node->right) {
				node = node->right;
				continue;
			}

			node_t *parent = node;
			node_t *child = calloc(1, sizeof(node_t));
			if (child != NULL) {
				child->idx_start = MASK_BOUNDARY(idx);
				node_insert(sb, parent, child);
			}

			assert((child == NULL) || node_supports_idx(child, idx));

			sb->hint = child;
			return child;
		}
	}

	assert((node != NULL) && node_supports_idx(node, idx));

	sb->hint = node;
	return node;
}

static const node_t *node_find_const(const sb_t *sb_const, sb_idx_t idx) {
	return (const node_t *) node_find((sb_t *) sb_const, idx);
}

static const node_t *node_find_or_next_const(const sb_t *sb_const, sb_idx_t idx)
{
	return (const node_t *) node_find_or_next((sb_t *) sb_const, idx);
}

static const node_t *node_find_or_prev_const(const sb_t *sb_const, sb_idx_t idx)
{
	return (const node_t *) node_find_or_prev((sb_t *) sb_const, idx);
}

static bool node_supports_idx(const node_t *node, sb_idx_t idx)
{
	if ((idx >= node->idx_start)
		&& (idx <= (node->idx_start + NUM_MASK_BITS + node->set_after - 1))) {
		return true;
	}

	return false;
}

static int node_cmp(node_t *node1, node_t *node2)
{
	if (node1->idx_start < node2->idx_start) {
		return -1;
	} else if (node1->idx_start > node2->idx_start) {
		return 1;
	}

	assert(node1->idx_start == node2->idx_start);
	return 0;
}

static int node_cmp_idx(node_t *node, sb_idx_t idx)
{
	if (node->idx_start < idx) {
		return -1;
	} else if (node->idx_start > idx) {
		return 1;
	}

	assert(node->idx_start == idx);
	return 0;
}

// Returns true if nodes left or right pointer doesn't point to another
// node.
static bool node_is_bud(node_t *node)
{
	if ((node->left == NULL) || (node->right == NULL)) {
		return true;
	}

	return false;
}

static unsigned int node_depth(const node_t *node)
{
	unsigned int depth = 0;

	const node_t *p = node;
	while (p->parent) {
		p = p->parent;
		depth++;
	}

	return depth;
}

static unsigned int node_depth_black(const node_t *node)
{
	unsigned int depth_black = 0;

	while (node != NULL) {
		if (node->color == BLACK) {
			depth_black++;
		}

		node = node->parent;
	}

	return depth_black;
}

static node_t *node_grandparent(node_t *node)
{
	if (node->parent && node->parent->parent) {
		return node->parent->parent;
	}

	return NULL;
}

static node_t *node_uncle(node_t *node)
{
	node_t *grandparent = node_grandparent(node);

	if (grandparent) {
		return (grandparent->left == node->parent)
			? grandparent->right
			: grandparent->left;
	}

	return NULL;
}

static node_t *node_minimum(node_t *node)
{
	while (node->left != NULL) {
		node = node->left;
	}

	return node;
}

static node_t *node_maximum(node_t *node)
{
	while (node->right != NULL) {
		node = node->right;
	}

	return node;
}

static node_t *node_next(node_t *node)
{
	if (node->right != NULL) {
		return node_minimum(node->right);
	}

	node_t *y = node->parent;
	while ((y != NULL) && (node == y->right)) {
		node = y;
		y = y->parent;
	}

	return y;
}

static node_t *node_prev(node_t *node)
{
	if (node->left != NULL) {
		return node_maximum(node->left);
	}

	node_t *y = node->parent;
	while ((y != NULL) && (node == y->left)) {
		node = y;
		y = y->parent;
	}

	return y;
}

static const node_t *node_minimum_const(const node_t *nodep_const)
{
	return (const node_t *) node_minimum((node_t *) nodep_const);
}

static const node_t *node_next_const(const node_t *nodep_const)
{
	return (const node_t *) node_next((node_t *) nodep_const);
}

static const node_t *node_prev_const(const node_t *nodep_const)
{
	return (const node_t *) node_prev((node_t *) nodep_const);
}

/*
 *              |                               |             
 *            +---+                           +---+               
 *            | y |                           | x |               
 *            +---+                           +---+               
 *           /     \                         /     \        
 *      +---+       c  <-- rotate_left(x)   a       +---+   
 *      | x |                                       | y |   
 *      +---+                                       +---+   
 *     /     \                                     /     \  
 *    a       b                                   b       c 
 */                                                         
static void node_rotate_left(sb_t *sb, node_t *x)
{
	node_t *y = x->right;

	x->right = y->left;
	if (y->left != NULL) {
		y->left->parent = x;
	}
	y->parent = x->parent;
	if (x->parent == NULL) {
		sb->root = y;
	} else if (x == x->parent->left) {
		x->parent->left = y;
	} else {
		x->parent->right = y;
	}
	y->left = x;
	x->parent = y;

#ifdef SBIT_SUBTREENUM
	x->num_set_subtree = num_set_node(x);
	x->num_set_subtree += (x->left) ? x->left->num_set_subtree : 0;
	x->num_set_subtree += (x->right) ? x->right->num_set_subtree : 0;
	y->num_set_subtree = num_set_node(y);
	y->num_set_subtree += (y->left) ? y->left->num_set_subtree : 0;
	y->num_set_subtree += (y->right) ? y->right->num_set_subtree : 0;
#endif
}

/*
 *              |                               |             
 *            +---+                           +---+               
 *            | y |                           | x |               
 *            +---+                           +---+               
 *           /     \                         /     \        
 *      +---+       c  rotate_right(y) -->  a       +---+   
 *      | x |                                       | y |   
 *      +---+                                       +---+   
 *     /     \                                     /     \  
 *    a       b                                   b       c 
 */                                                          
static void node_rotate_right(sb_t *sb, node_t *y)
{
	node_t *x = y->left;

	y->left = x->right;
	if (x->right != NULL) {
		x->right->parent = y;
	}
	x->parent = y->parent;
	if (y->parent == NULL) {
		sb->root = x;
	} else if (y == y->parent->right) {
		y->parent->right = x;
	} else {
		y->parent->left = x;
	}
	x->right = y;
	y->parent = x;

#ifdef SBIT_SUBTREENUM
	y->num_set_subtree = num_set_node(y);
	y->num_set_subtree += (y->left) ? y->left->num_set_subtree : 0;
	y->num_set_subtree += (y->right) ? y->right->num_set_subtree : 0;
	x->num_set_subtree = num_set_node(x);
	x->num_set_subtree += (x->left) ? x->left->num_set_subtree : 0;
	x->num_set_subtree += (x->right) ? x->right->num_set_subtree : 0;
#endif
}

static void node_transplant(sb_t *sb, node_t *u, node_t *v)
{
#ifdef SBIT_SUBTREENUM
	if (v) { num_set_adjust(v, v->num_set_subtree, false); }
	if (u) { num_set_adjust(u, u->num_set_subtree, false); }
#endif

	if (u->parent == NULL) {
		sb->root = v;
	} else if (u == u->parent->left) {
		u->parent->left = v;
	} else {
		u->parent->right = v;
	}
	if (v != NULL) {
		v->parent = u->parent;

#ifdef SBIT_SUBTREENUM
		num_set_adjust(v, num_set_node(v)
			+ ((v->left) ? v->left->num_set_subtree : 0)
			+ ((v->right) ? v->right->num_set_subtree : 0), true);
#endif
	}

	return;
}

static void node_insert(sb_t *sb, node_t *parent, node_t *node)
{
	node->parent = parent;
	if (parent == NULL) {
		assert(sb->root == NULL);
		node->color = BLACK;
		sb->root = node;
	} else {
		int cmp_rv = node_cmp(node, parent);
		if (cmp_rv < 0) {
			assert(parent->left == NULL);
			parent->left = node;
		} else if (cmp_rv > 0) {
			assert(parent->right == NULL);
			parent->right = node;
		} else {
			assert(false);  // Node to be inserted compares equal to parent.
		}
	}
	node->left = NULL;
	node->right = NULL;
	node->color = RED;
	num_set_adjust(node, num_set_node(node), true);

	// Node Insert Fixup
	assert(node->color == RED);  // Newly inserted nodes are always colored RED.
	while (node->parent && node_grandparent(node)
		&& (node->parent->color == RED)) {
		node_t *grandparent = node_grandparent(node);

		// Both node and node's parent are RED, which is a violation
		// of RED nodes only have BLACK children.
		assert(node->color == RED);
		assert(node->parent->color == RED);
		if (node->parent == grandparent->left) {
			node_t *uncle = node_uncle(node);
			if (uncle && uncle->color == RED) {  // Case 1 - Parent and uncle are
			                                     //          both red.
				node->parent->color = BLACK;
				uncle->color = BLACK;
				node->parent->parent->color = RED;
				node = node->parent->parent;
			} else {
				if (node == node->parent->right) {  // Case 2 - Parent is red,
				                                    //          uncle is black.
					node = node->parent;
					node_rotate_left(sb, node);
				}
				node->parent->color = BLACK;  // Case 3 - Node is left child,
				                              //          uncle is black.
				node->parent->parent->color = RED;
				node_rotate_right(sb, node->parent->parent);
			}
		} else {
			node_t *uncle = node_uncle(node);
			if (uncle && uncle->color == RED) {  // Case 1
				node->parent->color = BLACK;
				uncle->color = BLACK;
				node->parent->parent->color = RED;
				node = node->parent->parent;
			} else {
				if (node == node->parent->left) {  // Case 2
					node = node->parent;
					node_rotate_right(sb, node);
				}
				node->parent->color = BLACK;  // Case 3
				node->parent->parent->color = RED;
				node_rotate_left(sb, node->parent->parent);
			}
		}
	}
	sb->root->color = BLACK;
}

static void node_delete(sb_t *sb, node_t *node)
{
	node_t *y = node;  // Points to node either removed or moved.
	node_t *x;
	node_t sentinel;

	MEMCLEAR(&sentinel, sizeof(sentinel));
	assert(sentinel.color == BLACK);

	enum node_color y_orig_color = y->color;
	if (node->left == NULL) {
		if (node->right == NULL) {
			sentinel.parent = node;
			node->right = &sentinel;
		}
		x = node->right;
		node_transplant(sb, node, node->right);
	} else if (node->right == NULL) {
		if (node->left == NULL) {
			sentinel.parent = node;
			node->left = &sentinel;
		}
		x = node->left;
		node_transplant(sb, node, node->left);
	} else {
		y = node_minimum(node->right);
		y_orig_color = y->color;
		if (y->right == NULL) {
			sentinel.parent = y->parent;
			y->right = &sentinel;
		}
		x = y->right;
		if (y->parent == node) {
			x->parent = y;
		} else {
			node_transplant(sb, y, y->right);
			y->right = node->right;
			y->right->parent = y;
		}
		node_transplant(sb, node, y);

#ifdef SBIT_SUBTREENUM
		if (y->left) {
			num_set_adjust(y->left, y->left->num_set_subtree, false);
		}
#endif
		y->left = node->left;
		y->left->parent = y;
		y->color = node->color;
#ifdef SBIT_SUBTREENUM
		num_set_adjust(y, y->left->num_set_subtree, true);
#endif
	}

	if (y_orig_color == BLACK) {
		node_delete_fixup(sb, x);
	}

	// If node points to sentinel, set its pointer to NULL, before the
	// sentinel variable goes out-of-scope.
	if (sentinel.parent != NULL) {
		if (sentinel.parent->left == &sentinel) {
			sentinel.parent->left = NULL;
		}
		if (sentinel.parent->right == &sentinel) {
			sentinel.parent->right = NULL;
		}
	}
	if (sb->root == &sentinel) {
		sb->root = NULL;
	}

	sb->hint = NULL;
	free(node);
}

static void node_delete_fixup(sb_t *sb, node_t *node)
{
	while ((node != sb->root) && (node->color == BLACK)) {
		if (node == node->parent->left) {
			node_t *w = node->parent->right;
			if (w->color == RED) {
				w->color = BLACK;
				node->parent->color = RED;
				node_rotate_left(sb, node->parent);
				w = node->parent->right;
			}
			if (((w->left == NULL) || (w->left->color == BLACK))
				&& ((w->right == NULL) || (w->right->color == BLACK))) {
				w->color = RED;
				node = node->parent;
			} else {
				if ((w->right == NULL) || (w->right->color == BLACK)) {
					w->left->color = BLACK;
					w->color = RED;
					node_rotate_right(sb, w);
					w = node->parent->right;
				}
				w->color = node->parent->color;
				node->parent->color = BLACK;
				w->right->color = BLACK;
				node_rotate_left(sb, node->parent);
				node = sb->root;
			}
		} else {
			node_t *w = node->parent->left;
			if (w->color == RED) {
				w->color = BLACK;
				node->parent->color = RED;
				node_rotate_right(sb, node->parent);
				// node_rotate_right(sb, node);
				w = node->parent->left;
			}
			if (((w->right == NULL) || (w->right->color == BLACK))
				&& ((w->left == NULL) || (w->left->color == BLACK))) {
				w->color = RED;
				node = node->parent;
			} else {
				if ((w->left == NULL) || (w->left->color == BLACK)) {
					w->right->color = BLACK;
					w->color = RED;
					node_rotate_left(sb, w);
					// node_rotate_left(sb, w->parent);
					w = node->parent->left;
				}
				w->color = node->parent->color;
				node->parent->color = BLACK;
				w->left->color = BLACK;
				node_rotate_right(sb, node->parent);
				// node_rotate_right(sb, node);
				node = sb->root;
			}
		}
	}

	node->color = BLACK;
}

sb_num_t sb_nodes_num(const sb_t *sb)
{
	sb_num_t num = 0;

	if (sb->root != NULL) {
		for (const node_t *node = node_minimum_const(sb->root); node != NULL;
			node = node_next_const(node)) {
			num++;
		}
	}

	return num;
}

// Current implementation only has one type of node, which always
// has a mask with NUM_MASK_BITS bits;
sb_num_t sb_mask_bits_min(void)
{
	return NUM_MASK_BITS;
}

sb_num_t sb_mask_bits_max(void)
{
	return NUM_MASK_BITS;
}

// Returns NULL on sucess, otherwise returns a pointer to a dynamically
// allocated error message.
sb_pdynstr_t sb_validate(const sb_t* sb)
{
	sb_num_t sum_pernode_set = 0;
	node_t *highest_bud = NULL;
	node_t *lowest_bud = NULL;
	node_t *first_bud = NULL;

	// When it exists, the root node should be BLACK.
	if ((sb->root) && (sb->root->color != BLACK)) {
		return sb_dyn_sprintf("Root node (%p) color is not BLACK",
			sb->root->color);
	}

	// For each node.
	if (sb->root) {
		node_t *prev = NULL;
		for (node_t *node = node_minimum(sb->root); node != NULL;
			node = node_next(node)) {
			// Validate node start index is on a mask boundary.
			if ((node->idx_start % NUM_MASK_BITS) != 0) {
				return sb_dyn_sprintf("Node start index not on mask boundary,\n"
					"  node: %p idx_start: %#" SB_PRI_IDX " NUM_MASK_BITS: %u",
					node, node->idx_start, NUM_MASK_BITS);
			}

			if (prev != NULL) {
				// Validate current node has starting index greater than the
				// previous node.
				if (prev->idx_start >= node->idx_start) {
					return sb_dyn_sprintf("Node start index not greater than previous "
						"node index,\n"
						"  prev: %p prev->idx_start: %#" SB_PRI_IDX "\n"
						"  node: %p node->idx_start: %#" SB_PRI_IDX "",
						prev, prev->idx_start, node, node->idx_start);
				}

				// Validate a non-zero previous node set_after value doesn't
				// cause the bits it supports to overlap with the start index
				// of the current node.
				if ((prev->idx_start + NUM_MASK_BITS + prev->set_after - 1)
					>= node->idx_start) {
					return sb_dyn_sprintf("Previous node's set_after bits overlap with "
						"the current node,\n"
						"  NUM_MASK_BITS: %u\n"
						"  prev: %p prev->idx_start: %#" SB_PRI_IDX
						" prev->set_after: %#" SB_PRI_NUM "\n"
						"  node: %p node->idx_start: %#" SB_PRI_IDX "",
						NUM_MASK_BITS,
						prev, prev->idx_start,
						prev->set_after,
						node, node->idx_start);
				}
				// Is this node adjacent to the mask or set_after bits of the
				// previous node.  If so and this node has only leading mask bits,
				// those bits should have been part of the prvious nodes set_after
				// bits.
				if (node_prev_adj(node) && mask_only_leading(node->mask)) {
					return sb_dyn_sprintf("Nodes leading bits should be part of "
						"adjacent nodes set_after,\n"
						"  prev: %p prev->idx_start: %#" SB_PRI_IDX
						" prev->set_after: %#" SB_PRI_NUM "\n"
						"  node: %p node->idx_start: %#" SB_PRI_IDX
						" node->mask: %#018" NODE_PRI_MASK "\n",
						prev, prev->idx_start,
						prev->set_after,
						node, node->idx_start,
						node->mask);
				}
			}

			// Is this a higher bud node?  Node where there is no child
			// to the left or right.
			if (node_is_bud(node) && ((highest_bud == NULL)
				|| (node_depth(node) < node_depth(highest_bud))) ) {
				highest_bud = node;
			}

			// Is this a lower bud node?
			if (node_is_bud(node) && ((lowest_bud == NULL)
				|| (node_depth(node) > node_depth(lowest_bud))) ) {
				lowest_bud = node;
			}

			// Add to total sum of bits set, the number of bits set in this node.
			sum_pernode_set += num_set_node(node);

			// Nodes should only exist if they represent at least 1 set bit.
			if (num_set_node(node) == 0) {
				return sb_dyn_sprintf("Node doesn't represent any set bits,\n"
					"  node: %p mask: %#" NODE_PRI_MASK "",
					node, node->mask);
			}

			// Direct children of red nodes should not also be red.
			if (node->color == RED) {
				if (node->left && (node->left->color == RED)) {
					return sb_dyn_sprintf("Red node: %p has a left child node: %p\n"
						"that is also Red.", node, node->left);
				}
				if (node->right && (node->right->color == RED)) {
					return sb_dyn_sprintf("Red node: %p has a right child "
						"node: %p that is also Red.", node, node->right);
				}
			}

			// All nodes with any leaves (left == NULL or right == NULL)
			// should have the same number of black nodes on path to
			// the root node.
			if (node_is_bud(node)) {
				unsigned int this_depth_black = node_depth_black(node);
				if (first_bud != NULL) {
					if (this_depth_black != node_depth_black(first_bud)) {
						return sb_dyn_sprintf("Bud node: %p has a black depth of %u,\n"
							"  which is different from bud node: %p black depth of %u",
							node, this_depth_black, first_bud, node_depth_black(first_bud));
					}
				} else {
					first_bud = node;
				}
			}

#ifdef SBIT_SUBTREENUM
			// Validate nodes subtree num set value is equal to the number of
			// bits set in this node plus all its children.
			sb_num_t expected_subtree_num = num_set_node(node);
			expected_subtree_num += (node->left) ? num_set_subtree(node->left) : 0;
			expected_subtree_num += (node->right) ? num_set_subtree(node->right) : 0;
			if (node->num_set_subtree != expected_subtree_num) {
				return sb_dyn_sprintf("Unexpected num_set_subtree\n"
					"  node->idx_start: %#" SB_PRI_IDX
					" node->mask: %#018" NODE_PRI_MASK
					" node->set_after: %#" SB_PRI_NUM "\n"
					"  left_num_set_subtree: %#" SB_PRI_NUM "\n"
					"  right_num_set_subtree: %#" SB_PRI_NUM "\n"
					"  num_set_mask: %u"
					"  node->num_set_subtree: %#" SB_PRI_NUM "\n"
					"  expected_subtree_num: %#" SB_PRI_NUM "\n",
					node->idx_start, node->mask, node->set_after,
					(node->left) ? num_set_subtree(node->left) : (sb_num_t) 0,
					(node->right) ? num_set_subtree(node->right) : (sb_num_t) 0,
					num_set_mask(node->mask), node->num_set_subtree,
					expected_subtree_num);
			}
#endif

			prev = node;
		}
	}

	// Is the tree sufficiently balanced?  Implementation uses a Red-Black
	// binary-tree.  For such a binary-tree the lowest leaf should be no
	// more than twice the depth of the highest leaf.  Only kepth track of
	// the highest and lowest bud nodes, bud nodes have a leaf or leaves,
	// so need to add 1 to get the depth of the leaf attached to a bud node.
	if (highest_bud) {
		if ((node_depth(lowest_bud) + 1)
			> (2 * (node_depth(highest_bud) + 1))) {
			return sb_dyn_sprintf("lowest_node: %p at more than twice the\n"
				"  depth of the highest_bud: %p\n"
				"  lowest_depth: %u hightest_depth: %u",
				lowest_bud, highest_bud, node_depth(lowest_bud) + 1,
				node_depth(highest_bud) + 1);
		}
	}

	// Is the sum of bits set across all the nodes equal to the total
	// maintained in the sparsebit structure.
	if (sum_pernode_set != sb_numset(sb)) {
		return sb_dyn_sprintf("sb->num_set: %#" SB_PRI_NUM
			"not equal to sum_pernode_set: %l#" SB_PRI_NUM,
			sb_numset(sb), sum_pernode_set);
	}

	return NULL;
}

// Returns a pointer to a dynamically allocated string, that represents
// the current state of the sparsebit array pointed to by sb.
sb_pdynstr_t sb_dump(const sb_t* sb)
{
	sb_pdynstr_t rv_str = sb_dyn_sprintf("sb: %p\n"
		"root: %p\n", sb, sb->root);
	if (sb->root) {
		sb_pdynstr_t tmp_str = dump_subtree(sb, sb->root, 2);
		rv_str = sb_dyn_sprintf("%s%s", rv_str, tmp_str);
		free(tmp_str);
	}

	return rv_str;
}

int sb_save(FILE *file, const sb_t *sb)
{
	int rv;
	size_t sz;
	sb_idx_t idx = 0;

	// Write sparsebit file identifier and version.
	errno = 0;
	sz = fwrite(SB_ID, CONST_STRLEN(SB_ID), 1, file);
	if (sz != 1) {
		return -1;
	}
	unsigned char version = SB_VERSION;
	errno = 0;
	sz = fwrite(&version, sizeof(version), 1, file);
	if (sz != 1) {
		return -1;
	}

	// For each node
	if (sb->root) {
		for (node_t *node = node_minimum(sb->root); node != NULL;
			node = node_next(node)) {
			rv = node_write(file, node, &idx);
			if (rv != 0) { return rv; }
		}
	}

	// Write END record.
	sb_rcd_end_packed_t record_end_packed;
	MEMCLEAR(&record_end_packed, sizeof(record_end_packed));
	record_end_packed.id = SB_RCD_END;
	errno = 0;
	sz = fwrite(&record_end_packed, sizeof(record_end_packed), 1, file);
	if (sz != 1) {
		return -1;
	}

	return 0;
}

int sb_load(FILE *file, sb_t *sb)
{
	size_t sz;

	char file_id[CONST_STRLEN(SB_ID)];
	uint8_t version;

	// Read and validate sparsebit file identifier and version.
	errno = 0;
	sz = fread(file_id, 1, sizeof(file_id), file);
	if (sz != sizeof(file_id)) {
		if (sz == 0) { errno = EPROTO; return -1; }
		fprintf(stderr, "Unexpected sparsebit file idenifier length of: %zu\n",
			sz);
		errno = EPROTO;
		return -1;
	}
	if (strncmp(file_id, SB_ID, CONST_STRLEN(SB_ID)) != 0) {
		fprintf(stderr, "Unexpected sparsebit file identifier of: %*s\n",
			(int) CONST_STRLEN(SB_ID), file_id);
		errno = EPROTO;
		return -1;
	}

	errno = 0;
	sz = fread(&version, 1, sizeof(version), file);
	if (sz != sizeof(version)) {
		if (sz == 0) { errno = EPROTO; return -1; }
		fprintf(stderr, "Unexpected version length of: %zu\n",
			sz);
		errno = EPROTO;
		return -1;
	}
	if (version > SB_VERSION) {
		fprintf(stderr, "Unexpected sparsebit file version of: %u\n",
			(unsigned int) version);
		errno = EPROTO;
		return -1;
	}

	union sb_rcd_packed record_packed;

	sb_idx_t base_idx = 0;
	do {
		int rv;

		errno = 0;
		sz = fread(&record_packed, sizeof(record_packed.id), 1, file);
		if (sz != 1) {
			if (feof(file)) { break; } /* End-Of-FIle */
			if (sz == 0) { errno = EPROTO; return -1; }
			if (sz != sizeof(record_packed.id)) {
				fprintf(stderr, "Unexpected read length of packed record ID, sz: %zu\n",
					sz);
				errno = EPROTO;
				return -1;
			}
		}

		sb_rcd_id_t id = packed_id_byte_to_id(record_packed.id);

		// If needed, read in rest of packed record.
		size_t sz = record_packed_size(id);
		if (sz != sizeof(record_packed.id)) {
			assert(sz > sizeof(record_packed.id));
			size_t read_sz = sz - sizeof(record_packed.id);
			errno = 0;
			sz = fread(((char *) &record_packed) + sizeof(record_packed.id),
				1, read_sz, file);
				if (sz != read_sz) {
					if (sz == 0) { errno = EPROTO; return -1; }
					fprintf(stderr, "Partial Packed Record Read, sz: %zu\n", sz);
					errno = EPROTO;
					return -1;
				}
		}

		union sb_rcd record;
		record_packed_to_record(&record_packed, &record);

		switch (id) {
  	case SB_RCD_INC4_MASK8:
			base_idx += record.inc4_mask8.amt;
			for (size_t i = 0; i < 8; i++) {
				if ((1ull << i) & record.inc4_mask8.mask) {
					rv = sb_set(sb, base_idx + i);
					if (rv != 0) { return rv; }
				}
			}
			base_idx += 8;
			break;

  	case SB_RCD_INC4_MASK16:
			base_idx += record.inc4_mask16.amt;
			for (size_t i = 0; i < 16; i++) {
				if ((1ull << i) & record.inc4_mask16.mask) {
					rv = sb_set(sb, base_idx + i);
				}
			}
			base_idx += 16;
			break;

  	case SB_RCD_INC4_MASK32:
			base_idx += record.inc4_mask32.amt;
			for (size_t i = 0; i < 32; i++) {
				if ((1ull << i) & record.inc4_mask32.mask) {
					rv = sb_set(sb, base_idx + i);
				}
			}
			base_idx += 32;
			break;

  	case SB_RCD_INC4_MASK64:
			base_idx += record.inc4_mask64.amt;
			for (size_t i = 0; i < 64; i++) {
				if ((1ull << i) & record.inc4_mask64.mask) {
					rv = sb_set(sb, base_idx + i);
				}
			}
			base_idx += 64;
			break;

		case SB_RCD_INC12:
			base_idx += record.inc12.amt;
			break;

		case SB_RCD_INC12_MASK8:
			base_idx += record.inc12_mask8.amt;
			for (size_t i = 0; i < 8; i++) {
				if ((1ull << i) & record.inc12_mask8.mask) {
					rv = sb_set(sb, base_idx + i);
				}
			}
			base_idx += 8;
			break;

		case SB_RCD_INC12_MASK16:
			base_idx += record.inc12_mask16.amt;
			for (size_t i = 0; i < 16; i++) {
				if ((1ull << i) & record.inc12_mask16.mask) {
					rv = sb_set(sb, base_idx + i);
				}
			}
			base_idx += 16;
			break;

  	case SB_RCD_END:
			break;

  	case SB_RCD_SETNUM8:
			rv = sb_setnum(sb, base_idx, record.setnum8.amt);
			base_idx += record.setnum8.amt;
			break;

  	case SB_RCD_SETNUM64:
			rv = sb_setnum(sb, base_idx, record.setnum64.amt);
			base_idx += record.setnum64.amt;
			break;

  	case SB_RCD_INC64:
			base_idx += record.inc64.amt;
			break;

  	case SB_RCD_INC7_SET1:
			base_idx += record.inc7_set1.amt;
			rv = sb_set(sb, base_idx);
			base_idx += 1;
			break;
		}
		if (id == SB_RCD_END) { break; }
	} while(true);

	return 0;
}

static bool node_prev_adj(node_t *node)
{
	node_t *prev = node_prev(node);

	if (prev != NULL) {
		if ((prev->idx_start + NUM_MASK_BITS + prev->set_after)
			== node->idx_start) {
			return true;
		}
	}

	return false;
}

static node_t *node_split(sb_t *sb, node_t *node, sb_idx_t at)
{
	node_t *first = node;
	node_t *second = calloc(1, sizeof(node_t));
	if (second == NULL) {
		return second;
	}
	second->idx_start = MASK_BOUNDARY(at);
	second->set_after = first->set_after
		- (second->idx_start - (first->idx_start + NUM_MASK_BITS));
	first->set_after -= second->set_after;
	for (size_t bit_idx = 0; (second->set_after > 0)
		&& (bit_idx < NUM_MASK_BITS); bit_idx++) {
		second->mask |= MASK_BIT_MASK(bit_idx);
		second->set_after--;
	}
	num_set_adjust(first, num_set_node(second), false);

	node_t *parent = sb->root;
	while (parent != NULL) {
		int cmp_rv = node_cmp_idx(parent, at);

		assert(cmp_rv != 0);  // Only in this loop when second node, which
		                      // has not been inserted yet, supports idx.  So
		                      // no node should be found already within the
		                      // sb->root tree, that supports that index.
		if (cmp_rv > 0) {
			if (parent->left == NULL) {
				break;
			}
			parent = parent->left;
		} else {
			if (parent->right == NULL) {
				break;
			}
			parent = parent->right;
		}
	}
	node_insert(sb, parent, second);

	sb->hint = second;
	return second;
}

static void node_merge(sb_t *sb, node_t *prev, node_t *next)
{
	assert(node_prev_adj(next));
	assert(mask_only_leading(next->mask));

	sb_num_t adj_amt = 0;

	sb_num_t num_cleared = 0;
	if (next->mask == ~(mask_t)0) {
		// Merge all of next node, both its mask and set_after bits, into
		// the previous node.
		prev->set_after += NUM_MASK_BITS + next->set_after;
		num_set_adjust(prev, NUM_MASK_BITS + next->set_after, true);
		node_delete(sb, next);
	} else {
		// Add next nodes contigously set leading mask bits
		// to previous nodes set_after value.
		for (size_t n = 0; n < NUM_MASK_BITS; n++) {
			if ((MASK_BIT_MASK(n) & next->mask) != 0) {
				prev->set_after++;
				adj_amt++;
				num_cleared++;
			} else {
				break;
			}
		}
		num_set_adjust(next, num_cleared, false);
		next->mask = 0;
		next->idx_start += NUM_MASK_BITS;
		num_set_adjust(prev, adj_amt, true);
		adj_amt = 0;

		if (next->set_after > 0) {
			for (size_t n = 0; (n < NUM_MASK_BITS)
				&& (next->set_after > 0); n++) {
				next->mask |= MASK_BIT_MASK(n);
				next->set_after--;
			}
		} else {
			assert(next->mask == 0);
			assert(next->set_after == 0);
			node_delete(sb, next);
		}
	}
	num_set_adjust(prev, adj_amt, true);

	node_t *next2 = node_next(prev);
	if ((next2 != NULL) && node_prev_adj(next2)
		&& mask_only_leading(next2->mask)) {
		node_merge(sb, prev, next2);
	}
}

static void node_adjacent_fixup(sb_t *sb, sb_idx_t idx_start, sb_idx_t idx_end)
{
	node_t *node;

	if (sb->root == NULL) {
		return;
	}

	// Determine last index that fixup needs to be checked for.
	// If bit is set at caller specified idx_end, then just need
	// to check nodes up to that address.  If bit at caller specified
	// idx_end is not set, then need to check until index of next set
	// bit, if it exists.  If it doesn't exist, because there is no set
	// bit at or after idx_end, then just need to check to first set bit
	// before idx_end, but will instead check until caller specified idx_end.
	sb_idx_t fixup_end_idx = idx_end;
	if (!sb_isset(sb, fixup_end_idx)) {
		sb_idx_t next_idx = sb_nextset(sb, fixup_end_idx);
		if (next_idx > fixup_end_idx) {
			fixup_end_idx = next_idx;
		}
	}

	node = node_find_or_next(sb, idx_start);
	if (node != NULL) {
		node_t *prev = node_prev(node);
		if (prev != NULL) {
			node = prev;
		}
	}
	bool unconditional_process = true;
	bool first_pass = true;
	while ((node != NULL) && (unconditional_process
			|| (node->idx_start <= ((fixup_end_idx != SB_IDX_MAX)
				? (fixup_end_idx + 1) : SB_IDX_MAX))) ) {
		unconditional_process = false;
		node_t *next = node_next(node);

		if ((next != NULL) && mask_only_leading(next->mask)
			&& node_prev_adj(next)) {
			node_merge(sb, node, next);
			unconditional_process = true;
		}

		if (first_pass) {
			unconditional_process = true;
			first_pass = false;
		}

		node = node_next(node);
	}
}

static bool mask_any_set(mask_t mask)
{
	if (mask != 0) {
		return true;
	}

	return false;
}

static bool mask_only_leading(mask_t mask)
{
	if ((mask != 0) && (((mask + 1) & (mask)) == 0)) {
		return true;
	}

	return false;
}

static unsigned int num_set_mask(mask_t mask)
{
	sb_num_t num = 0;

	// TODO: Where available, use machine instruction that returns
	// num set bits, otherwise use non-loop method described in the
	// book Hacker's Delight.
	for (size_t n = 0; n < NUM_MASK_BITS; n++) {
		if (mask & MASK_BIT_MASK(n)) {
			num++;
		}
	}

	return num;
}

static sb_num_t num_set_node(const node_t *node)
{
	return num_set_mask(node->mask) + node->set_after;
}

static sb_num_t num_set_subtree(const node_t *node)
{
#ifdef SBIT_SUBTREENUM
	return node->num_set_subtree;
#endif

	sb_num_t sum = num_set_node(node);
	sum += (node->left) ? num_set_subtree(node->left) : 0;
	sum += (node->right) ? num_set_subtree(node->right) : 0;

	return sum;
}

static void num_set_adjust(node_t *node, sb_num_t amt, bool positive)
{
#ifdef SBIT_SUBTREENUM
	if (amt == 0) { return; }

	for (; node != NULL; node = node->parent) {
		if (positive) {
			assert(((node->num_set_subtree + amt) > node->num_set_subtree)
				|| ((node->num_set_subtree + amt) == 0));
			node->num_set_subtree += amt;
		} else {
			assert((node->num_set_subtree >= amt)
				|| (node->num_set_subtree == 0));
			node->num_set_subtree -= amt;
		}
	}
#endif
}

static sb_pdynstr_t dump_subtree(const sb_t *sb, const node_t *node,
	unsigned int indent)
{
	sb_pdynstr_t rv_str;

#ifndef SBIT_SUBTREENUM
	sb_pdynstr_t node_str = sb_dyn_sprintf(
		"%*snode: %p idx_start: %#" SB_PRI_IDX " mask: %#018" NODE_PRI_MASK
		" set_after: %#" SB_PRI_NUM "\n"
		"%*scolor: %s depth: %u parent: %p left: %p right: %p\n",
		indent, "", node, node->idx_start, node->mask, node->set_after,
		indent + 2, "", (node->color == RED) ? "RED  " : "BLACK",
		node_depth(node), node->parent, node->left, node->right);
#else
	sb_pdynstr_t node_str = sb_dyn_sprintf(
		"%*snode: %p idx_start: %#" SB_PRI_IDX " mask: %#018" NODE_PRI_MASK
		" set_after: %#" SB_PRI_NUM "\n"
		"%*scolor: %s depth: %u parent: %p left: %p right: %p\n"
		"%*snum_set_subtree: %#" SB_PRI_NUM "\n",
		indent, "", node, node->idx_start, node->mask, node->set_after,
		indent + 2, "", (node->color == RED) ? "RED  " : "BLACK",
		node_depth(node), node->parent, node->left, node->right,
		indent + 2, "", node->num_set_subtree);
#endif

	sb_pdynstr_t left_str = NULL, right_str = NULL;
	if (node->left) {
		left_str = dump_subtree(sb, node->left, indent + 2);
	}
	if (node->right) {
		right_str = dump_subtree(sb, node->right, indent + 2);
	}

	rv_str = sb_dyn_sprintf("%s%s%s", node_str,
		(left_str) ? left_str : "", (right_str) ? right_str : "");
	sb_dynstr_free(node_str);
	if (left_str) {
		sb_dynstr_free(left_str);
	}
	if (right_str) {
		sb_dynstr_free(right_str);
	}

	return rv_str;
}

sb_pdynstr_t sb_dyn_sprintf(const char *format, ...)
{
	int n;
	size_t s_allocated;
	va_list ap;
	char *s = NULL;

	// Determine size of string, if the caller doesn't change any of the
	// argument values, while this call is being performed.
	va_start(ap, format);
	n = vsnprintf(s, 0, format, ap);
	va_end(ap);

	// For now, just get a pointer to any allocated memory. Later, the loop
	// below will reallocate to the anticipated size.
	s = malloc(sizeof(*s));
	if (s == NULL) {
		errno = ENOMEM;
		return NULL;
	}

	do {
		// Reallocate to anticipated size, plus two extra bytes.  One for the
		// trailing \0 and another so as to know if enough was allocated.
		s_allocated = n + 2;
		s = realloc(s, s_allocated);
		va_start(ap, format);
		n = vsnprintf(s, s_allocated, format, ap);
		va_end(ap);
	} while (n >= s_allocated);

	return s;
}

void sb_dynstr_free(sb_pdynstr_t pdynstr)
{
	free(pdynstr);
}

static int node_write(FILE *file, node_t *node, sb_idx_t *base_idx)
{
	int rv;
	union sb_rcd record;

	int mask_first_set_idx = -1;
	for (size_t n1 = 0; n1 < sizeof(node->mask) * CHAR_BIT; n1++) {
		if ((node->mask & (1ull << n1))) {
 			if (mask_first_set_idx < 0) {
				mask_first_set_idx = n1;
			}
		}
	}

	// If needed increament current index up to nodes starting index.
	if (*base_idx != node->idx_start) {
		assert(*base_idx <= node->idx_start);
		sb_idx_t diff = node->idx_start - *base_idx;
		if (diff <= 0xfff) {
			MEMCLEAR(&record, sizeof(record.inc12));
			record.inc12.id = SB_RCD_INC12;
			record.inc12.amt = diff;
		} else {
			MEMCLEAR(&record, sizeof(record.inc64));
			record.inc64.id = SB_RCD_INC64;
			record.inc64.amt = diff;
		}
		rv = record_write(file, &record, base_idx);
		if (rv != 0) { return rv; }
	}

	// Write the nodes mask value
	MEMCLEAR(&record, sizeof(record.inc64));
	record.inc4_mask64.id = SB_RCD_INC4_MASK64;
	record.inc4_mask64.mask = node->mask;
	rv = record_write(file, &record, base_idx);
	if (rv != 0) { return rv; }

	if (node->set_after) {
		if (node->set_after <= 0xff) {
			MEMCLEAR(&record, sizeof(record.setnum8));
			record.setnum8.id = SB_RCD_SETNUM8;
			record.setnum8.amt = node->set_after;
		} else {
			MEMCLEAR(&record, sizeof(record.setnum64));
			record.setnum64.id = SB_RCD_SETNUM64;
			record.setnum64.amt = node->set_after;
		}
		rv = record_write(file, &record, base_idx);
	}

	return 0;
}

static int record_write(FILE *file, const union sb_rcd *record,
	sb_idx_t *base_idx)
{
	size_t sz;

	union sb_rcd_packed record_packed;

	record_to_record_packed(record, &record_packed);

	errno = 0;
	sz = fwrite(&record_packed, record_packed_size(record->id), 1, file);
	if (sz != 1) {
		return -1;
	}

	*base_idx += record_idx_adjust(record);

	return 0;
}

static sb_idx_t record_idx_adjust(const union sb_rcd *record)
{
	switch (record->id) {
  case SB_RCD_INC4_MASK8:
		return record->inc4_mask8.amt + 8;

  case SB_RCD_INC4_MASK16:
		return record->inc4_mask16.amt + 16;

  case SB_RCD_INC4_MASK32:
		return record->inc4_mask32.amt + 32;

  case SB_RCD_INC4_MASK64:
		return record->inc4_mask64.amt + 64;

	case SB_RCD_INC12:
		return record->inc12.amt;

	case SB_RCD_INC12_MASK8:
		return record->inc12.amt + 1;

	case SB_RCD_INC12_MASK16:
		return record->inc12.amt + 2;

  case SB_RCD_END:
		return 0;

  case SB_RCD_SETNUM8:
		return record->setnum8.amt;

  case SB_RCD_SETNUM64:
		return record->setnum64.amt;

  case SB_RCD_INC64:
		return record->inc64.amt;

  case SB_RCD_INC7_SET1:
		return record->inc7_set1.amt + 1;
	}

	assert(false); // Unknown record type
	return 0;
}

static void record_to_record_packed(const union sb_rcd *record,
	union sb_rcd_packed *record_packed)
{
	switch (record->id) {
  case SB_RCD_INC4_MASK8:
		MEMCLEAR(&record_packed->inc4_mask8, sizeof(record_packed->inc4_mask8));
		record_packed->inc4_mask8.id_amt = SB_RCD_INC4_MASK8 & 0xf0;
		record_packed->inc4_mask8.id_amt |= record->inc4_mask8.amt & 0x0f;
		record_packed->inc4_mask8.mask = record->inc4_mask8.mask;
		return;

  case SB_RCD_INC4_MASK16:
		MEMCLEAR(&record_packed->inc4_mask16, sizeof(record_packed->inc4_mask16));
		record_packed->inc4_mask16.id_amt = SB_RCD_INC4_MASK16 & 0xf0;
		record_packed->inc4_mask16.id_amt |= record->inc4_mask16.amt & 0x0f;
		record_packed->inc4_mask16.mask[0] = record->inc4_mask16.mask >> 8;
		record_packed->inc4_mask16.mask[1] = record->inc4_mask16.mask & 0xff;
		return;

  case SB_RCD_INC4_MASK32:
		MEMCLEAR(&record_packed->inc4_mask32, sizeof(record_packed->inc4_mask32));
		record_packed->inc4_mask32.id_amt = SB_RCD_INC4_MASK32 & 0xf0;
		record_packed->inc4_mask32.id_amt |= record->inc4_mask32.amt & 0x0f;
		record_packed->inc4_mask32.mask[0]
			= (record->inc4_mask32.mask >> 24) & 0xff;
		record_packed->inc4_mask32.mask[1]
			= (record->inc4_mask32.mask >> 16) & 0xff;
		record_packed->inc4_mask32.mask[2]
			= (record->inc4_mask32.mask >> 8)  & 0xff;
		record_packed->inc4_mask32.mask[3]
			= (record->inc4_mask32.mask >> 0)  & 0xff;
		return;

  case SB_RCD_INC4_MASK64:
		MEMCLEAR(&record_packed->inc4_mask64, sizeof(record_packed->inc4_mask64));
		record_packed->inc4_mask64.id_amt = SB_RCD_INC4_MASK64 & 0xf0;
		record_packed->inc4_mask64.id_amt |= record->inc4_mask64.amt & 0x0f;

		for (size_t i0 = 0; i0 < 8; i0++) {
			record_packed->inc4_mask64.mask[i0] = 0;
			for (size_t i1 = 0; i1 < 8; i1++) {
				if (record->inc4_mask64.mask & MASK_BIT_MASK(i0 * 8 + i1)) {
					record_packed->inc4_mask64.mask[i0] |= 0x80 >> i1;
				}
			}
		}
		return;

	case SB_RCD_INC12:
		MEMCLEAR(&record_packed->inc12, sizeof(record_packed->inc12));
		record_packed->inc12.id_amt0 = SB_RCD_INC12 & 0xf0;
		record_packed->inc12.id_amt0 |= (record->inc12.amt >> 8) & 0x0f;
		record_packed->inc12.amt1 = record->inc12.amt & 0xff;
		return;

	case SB_RCD_INC12_MASK8:
		record_packed->inc12_mask8.id_amt0 = SB_RCD_END & 0xf0;
		record_packed->inc12_mask8.id_amt0 |= (record->inc12_mask8.amt >> 8)
			& 0x0f;
		record_packed->inc12_mask8.amt1 = record->inc12_mask8.amt & 0xff;
		record_packed->inc12_mask8.mask = record->inc12_mask8.mask;
		return;

	case SB_RCD_INC12_MASK16:
		MEMCLEAR(&record_packed->inc12_mask16, sizeof(record_packed->inc12_mask16));
		record_packed->inc12_mask16.id_amt0 = SB_RCD_END & 0xf0;
		record_packed->inc12_mask16.id_amt0 |= (record->inc12_mask16.amt >> 8)
			& 0x0f;
		record_packed->inc12_mask16.amt1 = record->inc12_mask16.amt & 0xff;
		record_packed->inc12_mask16.mask[0] = (record->inc12_mask16.mask >> 8)
			& 0xff;
		record_packed->inc12_mask16.mask[1] = record->inc12_mask16.mask & 0xff;
		return;

  case SB_RCD_END:
		MEMCLEAR(&record_packed->end, sizeof(record_packed->end));
		record_packed->end.id = SB_RCD_END;
		return;

  case SB_RCD_SETNUM8:
		MEMCLEAR(&record_packed->setnum8, sizeof(record_packed->setnum8));
		record_packed->setnum8.id = SB_RCD_SETNUM8;
		record_packed->setnum8.amt = record->setnum8.amt;
		return;

  case SB_RCD_SETNUM64:
		MEMCLEAR(&record_packed->setnum64, sizeof(record_packed->setnum64));
		record_packed->setnum64.id = SB_RCD_SETNUM64;
		record_packed->setnum64.amt[0] = (record->setnum64.amt >> 56) & 0xff;
		record_packed->setnum64.amt[1] = (record->setnum64.amt >> 48) & 0xff;
		record_packed->setnum64.amt[2] = (record->setnum64.amt >> 40) & 0xff;
		record_packed->setnum64.amt[3] = (record->setnum64.amt >> 32) & 0xff;
		record_packed->setnum64.amt[4] = (record->setnum64.amt >> 24) & 0xff;
		record_packed->setnum64.amt[5] = (record->setnum64.amt >> 16) & 0xff;
		record_packed->setnum64.amt[6] = (record->setnum64.amt >> 8)  & 0xff;
		record_packed->setnum64.amt[7] = (record->setnum64.amt >> 0)  & 0xff;
		return;

  case SB_RCD_INC64:
		MEMCLEAR(&record_packed->inc64, sizeof(record_packed->inc64));
		record_packed->inc64.id = SB_RCD_INC64;
		record_packed->inc64.amt[0] = (record->inc64.amt >> 56) & 0xff;
		record_packed->inc64.amt[1] = (record->inc64.amt >> 48) & 0xff;
		record_packed->inc64.amt[2] = (record->inc64.amt >> 40) & 0xff;
		record_packed->inc64.amt[3] = (record->inc64.amt >> 32) & 0xff;
		record_packed->inc64.amt[4] = (record->inc64.amt >> 24) & 0xff;
		record_packed->inc64.amt[5] = (record->inc64.amt >> 16) & 0xff;
		record_packed->inc64.amt[6] = (record->inc64.amt >> 8)  & 0xff;
		record_packed->inc64.amt[7] = (record->inc64.amt >> 0)  & 0xff;
		return;

  case SB_RCD_INC7_SET1:
		MEMCLEAR(&record_packed->inc7_set1, sizeof(record_packed->inc7_set1));
		record_packed->inc7_set1.id_amt = SB_RCD_INC7_SET1 & 0x80;
		record_packed->inc7_set1.id_amt |= record->inc7_set1.amt & 0x7f;
		return;
	}

	assert(false); // Unknown record type
}

static sb_rcd_id_t packed_id_byte_to_id(uint8_t packed_id_byte)
{
	if ((packed_id_byte & 0xf0) <= 0x6f) { return packed_id_byte & 0xf0; }
	if ((packed_id_byte & 0x80) == 0x80) { return 0x80; }

	return packed_id_byte;
}

static void record_packed_to_record(const union sb_rcd_packed *record_packed,
	union sb_rcd *record)
{
	sb_rcd_id_t id = packed_id_byte_to_id(record_packed->id);

	switch (id) {
  case SB_RCD_INC4_MASK8:
		MEMCLEAR(&record->inc4_mask8, sizeof(record->inc4_mask8));
		record->inc4_mask8.id = SB_RCD_INC4_MASK8;
		record->inc4_mask8.amt = record_packed->inc4_mask8.id_amt & 0x0f;
		record->inc4_mask8.mask = 0;
		for (size_t i1 = 0; i1 < 8; i1++) {
			if (record_packed->inc4_mask8.mask & (0x80 >> i1)) {
				record->inc4_mask8.mask |= MASK_BIT_MASK(i1);
			}
		}
		return;

  case SB_RCD_INC4_MASK16:
		MEMCLEAR(&record->inc4_mask16, sizeof(record->inc4_mask16));
		record->inc4_mask16.id = SB_RCD_INC4_MASK16;
		record->inc4_mask16.amt = record_packed->inc4_mask16.id_amt & 0x0f;
		record->inc4_mask16.mask = 0;
		for (size_t i0 = 0; i0 < 2; i0++) {
			if (record_packed->inc4_mask16.mask[i0] == 0) { continue; }
			for (size_t i1 = 0; i1 < 8; i1++) {
				if (record_packed->inc4_mask16.mask[i0] & (0x80 >> i1)) {
					record->inc4_mask16.mask |= MASK_BIT_MASK(i0 * 8 + i1);
				}
			}
		}
		return;

  case SB_RCD_INC4_MASK32:
		MEMCLEAR(&record->inc4_mask32, sizeof(record->inc4_mask32));
		record->inc4_mask32.id = SB_RCD_INC4_MASK32;
		record->inc4_mask32.amt = record_packed->inc4_mask32.id_amt & 0x0f;
		record->inc4_mask32.mask = 0;
		for (size_t i0 = 0; i0 < 4; i0++) {
			if (record_packed->inc4_mask32.mask[i0] == 0) { continue; }
			for (size_t i1 = 0; i1 < 8; i1++) {
				if (record_packed->inc4_mask32.mask[i0] & (0x80 >> i1)) {
					record->inc4_mask32.mask |= MASK_BIT_MASK(i0 * 8 + i1);
				}
			}
		}
		return;

  case SB_RCD_INC4_MASK64:
		MEMCLEAR(&record->inc4_mask64, sizeof(record->inc4_mask64));
		record->inc4_mask64.id = SB_RCD_INC4_MASK64;
		record->inc4_mask64.amt = record_packed->inc4_mask64.id_amt & 0x0f;
		record->inc4_mask64.mask = 0;
		for (size_t i0 = 0; i0 < 8; i0++) {
			if (record_packed->inc4_mask64.mask[i0] == 0) { continue; }
			for (size_t i1 = 0; i1 < 8; i1++) {
				if (record_packed->inc4_mask64.mask[i0] & (0x80 >> i1)) {
					record->inc4_mask64.mask |= MASK_BIT_MASK(i0 * 8 + i1);
				}
			}
		}
		return;

	case SB_RCD_INC12:
		MEMCLEAR(&record->inc12, sizeof(record->inc12));
		record->inc12.id = SB_RCD_INC12;
		record->inc12.amt = (((uint16_t) record_packed->inc12.id_amt0 & 0x0f) << 8)
			| record_packed->inc12.amt1;
		return;

	case SB_RCD_INC12_MASK8:
		record->inc12_mask8.id = SB_RCD_INC12_MASK8;
		record->inc12_mask8.amt = (record_packed->inc12_mask8.id_amt0 & 0x0f)
			<< (8 + record_packed->inc12_mask8.amt1);
		record->inc12_mask8.mask = 0;
		for (size_t i1 = 0; i1 < 8; i1++) {
			if (record_packed->inc12_mask8.mask & (0x80 >> i1)) {
				record->inc12_mask8.mask |= MASK_BIT_MASK(i1);
			}
		}
		return;

	case SB_RCD_INC12_MASK16:
		record->inc12_mask16.id = SB_RCD_INC12_MASK16;
		record->inc12_mask16.amt
			= (record_packed->inc12_mask16.id_amt0 & 0x0f)
				<< ( 8 + record_packed->inc12_mask16.amt1);
		record->inc12_mask16.mask = 0;
		for (size_t i0 = 0; i0 < 2; i0++) {
			if (record_packed->inc12_mask16.mask[i0] == 0) { continue; }
			for (size_t i1 = 0; i1 < 8; i1++) {
				if (record_packed->inc12_mask16.mask[i0] & (0x80 >> i1)) {
					record->inc12_mask16.mask |= MASK_BIT_MASK(i0 * 8 + i1);
				}
			}
		}
		return;

  case SB_RCD_END:
		MEMCLEAR(&record->end, sizeof(record->end));
		record->end.id = SB_RCD_END;
		return;

  case SB_RCD_SETNUM8:
		MEMCLEAR(&record->setnum8, sizeof(record->setnum8));
		record->setnum8.id = SB_RCD_SETNUM8;
		record->setnum8.amt = record_packed->setnum8.amt;
		return;

  case SB_RCD_SETNUM64:
		MEMCLEAR(&record->setnum64, sizeof(record->setnum64));
		record->setnum64.id = SB_RCD_SETNUM64;
		record->setnum64.amt = ((uint64_t) record_packed->setnum64.amt[0] << 56)
			| ((uint64_t) record_packed->setnum64.amt[1] << 48)
			| ((uint64_t) record_packed->setnum64.amt[2] << 40)
			| ((uint64_t) record_packed->setnum64.amt[3] << 32)
			| ((uint64_t) record_packed->setnum64.amt[4] << 24)
			| ((uint64_t) record_packed->setnum64.amt[5] << 16)
			| ((uint64_t) record_packed->setnum64.amt[6] << 8)
			| ((uint64_t) record_packed->setnum64.amt[7] << 0);
		return;

  case SB_RCD_INC64:
		MEMCLEAR(&record->inc64, sizeof(record->inc64));
		record->inc64.id = SB_RCD_INC64;
		record->inc64.amt = ((uint64_t) record_packed->inc64.amt[0] << 56)
			| ((uint64_t) record_packed->inc64.amt[1] << 48)
			| ((uint64_t) record_packed->inc64.amt[2] << 40)
			| ((uint64_t) record_packed->inc64.amt[3] << 32)
			| ((uint64_t) record_packed->inc64.amt[4] << 24)
			| ((uint64_t) record_packed->inc64.amt[5] << 16)
			| ((uint64_t) record_packed->inc64.amt[6] << 8)
			| ((uint64_t) record_packed->inc64.amt[7] << 0);
		return;

  case SB_RCD_INC7_SET1:
		MEMCLEAR(&record->inc7_set1, sizeof(record->inc7_set1));
		record->inc7_set1.id = SB_RCD_INC7_SET1;
		record->inc7_set1.amt |= record_packed->inc7_set1.id_amt & 0x7f;
		return;
	}

	assert(false); // Unknown record type
}

static size_t record_packed_size(sb_rcd_id_t id)
{
	switch (id) {
  case SB_RCD_INC4_MASK8:
		return sizeof(sb_rcd_inc4_mask8_packed_t);

  case SB_RCD_INC4_MASK16:
		return sizeof(sb_rcd_inc4_mask16_packed_t);

  case SB_RCD_INC4_MASK32:
		return sizeof(sb_rcd_inc4_mask32_packed_t);

  case SB_RCD_INC4_MASK64:
		return sizeof(sb_rcd_inc4_mask64_packed_t);

	case SB_RCD_INC12:
		return sizeof(sb_rcd_inc12_packed_t);

	case SB_RCD_INC12_MASK8:
		return sizeof(sb_rcd_inc12_mask8_packed_t);

	case SB_RCD_INC12_MASK16:
		return sizeof(sb_rcd_inc12_mask16_packed_t);

  case SB_RCD_END:
		return sizeof(sb_rcd_end_packed_t);

  case SB_RCD_SETNUM8:
		return sizeof(sb_rcd_setnum8_packed_t);

  case SB_RCD_SETNUM64:
		return sizeof(sb_rcd_setnum64_packed_t);

  case SB_RCD_INC64:
		return sizeof(sb_rcd_inc64_packed_t);

  case SB_RCD_INC7_SET1:
		return sizeof(sb_rcd_inc7_set1_packed_t);
	}

	assert(false); // Unknown record type
}
