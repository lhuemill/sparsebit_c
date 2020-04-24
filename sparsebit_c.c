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

/* TODO: Work-In-Progress
 *   Appears to be functionally correct (e.g. Passes all smoke tests).
 *   Following near-term improvements are anticipated:
 *     + Use a semi-balanced binary-tree (e.g. Red-Black Binary-Tree)
 *       instead of a binary-tree, with no automated balancing.
 *
 *     + Implement node set_after member, which allows a single node to
 *       describe any number of contigous set bits after the bits
 *       describe by the mask.
 *
 *     + Improved sb_nextset() and sb_nextcleared() improvement, through
 *       the use of a node hint pointer.  For this, the sparsebit root
 *       structure would maintain a hint pointer, which points to the
 *       most recently accessed node of the binary tree.  As long as the
 *       tree structure doesn't change, operations can use the hint pointer
 *       as a means to access a node that is likely to support the current
 *       operation.
 */
#include <assert.h>
#include <errno.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#define SB_TEST
#include "sparsebit_c.h"

#define BITS_PER_BYTE 8
#define MEMCLEAR(p, n) memset((p), 0, (n))

// Complete definition of sparsebit root structure.
struct node;
typedef struct node node_t;
struct sb {
	node_t *root;
	sb_num_t num_set;
};

typedef uint64_t mask_t;
#define NUM_MASK_BITS (sizeof(mask_t) * BITS_PER_BYTE)
struct node {
	sb_idx_t idx_start;  // Index of first bit in mask.
	mask_t mask;
	sb_num_t set_after;  // Number of contiguosly set bits after bits in mask.
	bool color;
	node_t *parent;
	node_t *left;
	node_t *right;
};

static node_t *node_find(sb_t *sb, sb_idx_t idx);
static node_t *node_find_or_next(sb_t *sb, sb_idx_t idx);
static node_t *node_find_or_create(sb_t *sb, sb_idx_t idx);
static bool node_supports_idx(node_t *node, sb_idx_t idx);
static sb_num_t node_num_set(node_t *node);
static node_t *node_minimum(node_t *node);
static node_t *node_maximum(node_t *node);
static node_t *node_next(node_t *node);
static node_t *node_prev(node_t *node);
static void node_transplant(sb_t *sb, node_t *u, node_t *v);
static void node_delete(sb_t *sb, node_t *node);
static sb_pdynstr_t dyn_sprintf(const char *format, ...);

sb_t *sb_create(void)
{
	sb_t *p = malloc(sizeof(sb_t));
	if (p == NULL) {
		errno = ENOMEM;
		return NULL;
	}
	MEMCLEAR(p, sizeof(*p));

	return p;
}

void sb_delete(sb_t* sb)
{
	// TODO: Replace with:
	//         sb_clearnum(sb, 0, ~(sb_num_t)0);
	//         sb_clear(sb, SB_IDX_MAX);
	//       once time complexity of sb_clearnum() is O(N),
	//       where N is number of currently set bits to be cleared,
	//       instead of the current implementation where N is the
	//       value of the num parameter passed to sb_clearnum().
	while (sb->root) {
		sb->root->mask = 0;
		node_delete(sb, sb->root);
	}

	free(sb);
}

// ==== Set Operations
int sb_set(sb_t* sb, sb_idx_t idx)
{
	node_t *node = node_find_or_create(sb, idx);
	if (node == NULL) {
		errno = ENOMEM;
		return -1;
	}

	mask_t mask = (mask_t)1 << (idx - node->idx_start);
	if ((node->mask & mask) == 0) {
		sb->num_set++;
	}
	node->mask |= mask;

	return 0;
}

int sb_setnum(sb_t* sb, sb_idx_t idx, sb_num_t num)
{
	for (sb_num_t n = 0; n < num; n++) {
		int rv = sb_set(sb, idx + n);
		if (rv) {
			return rv;
		}
	}

	return 0;
}


bool sb_anyset(sb_t* sb)
{
	if (sb->root) {
		return true;
	}

	return false;
}

sb_num_t sb_numset(sb_t* sb)
{
	return sb->num_set;
}

bool sb_isset(sb_t* sb, sb_idx_t idx)
{
	node_t *node = node_find(sb, idx);

	if (node == NULL) {
		return false;
	}

	return node->mask & ((mask_t)1 << (idx - node->idx_start));
}

sb_idx_t sb_firstset(sb_t* sb)
{
	if (sb->root != NULL) {
		node_t *node = node_minimum(sb->root);
		for (sb_idx_t n = 0; n < NUM_MASK_BITS; n++) {
			if (node->mask & ((mask_t)1 << n)) {
				return node->idx_start + n;
			}
		}
		assert(0);  // Node with no mask bits set.
	}

	return 0;
}

sb_idx_t sb_nextset(sb_t* sb, sb_idx_t idx)
{
	node_t *node = node_find_or_next(sb, idx);

	// No node with idx or a node after that, so there is no next set bit.
	if (node == NULL) {
		return 0;
	}

	// Does node point to the node supporting idx? If so, is there
	// another set bit within the mask beyond the mask bit for idx?
	if (node_supports_idx(node, idx)) {
		for (unsigned int n = (idx - node->idx_start) + 1; n < NUM_MASK_BITS; n++) {
			if (node->mask & ((mask_t)1 << n)) {
				return node->idx_start + n;
			}
		}

		// Node supports idx, but there are no mask bits beyond the mask bit for
		// idx that are set. As such, next set bit beyond idx will be the first
		// set bit in the next node.
		node = node_next(node);
		if (node == NULL) {
			return 0;
		}
	}

	// The node pointer points to the first node after the node supporting
	// idx.  The first mask bit set in node, will be for the first set bit
	// after idx.
	for (unsigned int n = 0; n < NUM_MASK_BITS; n++) {
		if (node->mask & ((mask_t)1 << n)) {
			return node->idx_start + n;
		}
	}
	assert(0);  // Node with no mask bits set.

	return 0;
}

// ==== Clear/Cleared Operations
int sb_clear(sb_t* sb, sb_idx_t idx)
{
	node_t *node = node_find(sb, idx);

	if (node == NULL) {
		return 0;
	}

	mask_t mask = (mask_t)1 << (idx - node->idx_start);
	if (node->mask & mask) {
		sb->num_set--;
	}
	node->mask &= ~mask;

	// Delete the node when it no longer represents any set bits.
	if (node->mask == 0) {
		node_delete(sb, node);
	}

	return 0;
}

int sb_clearnum(sb_t* sb, sb_idx_t idx, sb_num_t num)
{
	for (sb_num_t n = 0; n < num; n++) {
		int rv = sb_clear(sb, idx + n);
		if (rv) {
			return rv;
		}
	}

	return 0;
}

bool sb_anycleared(sb_t* sb)
{
	// sb_numcleared() returns 0 both when all bits are set and all bits
	// are cleared, because there are a total of ~(sb_num_t)0 + 1 bits.
	// With overflow, one more than ~(sb_num_t)0 is 0.
	return sb_numcleared(sb) || (sb->root && (sb->root->mask != ~(mask_t)0));
}

sb_num_t sb_numcleared(sb_t* sb)
{
	return (sb_num_t)0 - sb->num_set;
}


bool sb_iscleared(sb_t* sb, sb_idx_t idx)
{
	return !sb_isset(sb, idx);
}

sb_idx_t sb_firstcleared(sb_t* sb)
{
	if (sb->root != NULL) {
		sb_idx_t prev_node_idx_start = (sb_idx_t)0 - NUM_MASK_BITS;

		// First gap in node starting index or a node with a mask that contains
		// a cleared bit will determine the index of the first cleared bit.
		for (node_t * node = node_minimum(sb->root); node != NULL;
			node = node_next(node)) {
			// Is there a gap in the node starting index.
			if (node->idx_start != (prev_node_idx_start + NUM_MASK_BITS)) {
				return prev_node_idx_start + NUM_MASK_BITS;
			}

			if (node->mask != ~(mask_t)0) {
				for (sb_idx_t n = 0; n < NUM_MASK_BITS; n++) {
					if (node->mask && ((mask_t)1 << n)) {
						return node->idx_start + n;
					}
				}
				assert(0);  // Cleared bit not found.
			}

			prev_node_idx_start = node->idx_start;
		}
	}

	return 0;
}

sb_idx_t sb_nextcleared(sb_t* sb, sb_idx_t idx)
{
	if (idx == SB_IDX_MAX) {
		return 0;
	}

	node_t *node = node_find(sb, idx + 1);

	if (node == NULL) {
		// No node that supports idx.
		return idx + 1;
	} else {
		// Node supports idx + 1.  Is there a cleared bit at or beyond the mask
		// bit for idx + 1?
		for (unsigned int n = (idx + 1) - node->idx_start;
			n < NUM_MASK_BITS; n++) {
			if ((node->mask & ((mask_t)1 << n)) == 0) {
				return node->idx_start + n;
			}
		}
	}

	// Next cleared bit will be in the the next node with a cleared mask bit
	// or first index supported by a gap in the node ranges.
	for (sb_idx_t idx2 = idx - (idx % NUM_MASK_BITS) + NUM_MASK_BITS;
		idx2 > idx; idx2 += NUM_MASK_BITS) {
		node = node_find(sb, idx2);

		if (node == NULL) {
			// Gap in indexes supported by nodes.
			return idx2;
		} else {
			// Any cleared bits in the node?
			if (node->mask != ~(mask_t)0) {
				for (unsigned int n = 0; n < NUM_MASK_BITS; n++) {
					if ((node->mask & ((mask_t)1 << n)) == 0) {
						return node->idx_start + n;
					}
				}
				assert(0);  // Mask asyncronously changed.  No longer has a
				            // cleared mask bit.
			}
		}
	}

	return 0;
}

// ==== Sparsebit Node Operations ====
static node_t *node_find(sb_t *sb, sb_idx_t idx)
{
	node_t *node;

	node = sb->root;
	while ((node != NULL) && (!node_supports_idx(node, idx))) {
		if (idx < node->idx_start) {
			node = node->left;
		} else {
			node = node->right;
		}
	}

	return node;
}

static node_t *node_find_or_next(sb_t *sb, sb_idx_t idx)
{
	node_t *node;

	node = sb->root;
	while ((node != NULL) && (!node_supports_idx(node, idx))) {
		if (idx < node->idx_start) {
			if (node->left == NULL) {
				return node;
			}

			node = node->left;
		} else {
			if (node->right == NULL) {
				return node_next(node);
			}

			node = node->right;
		}
	}

	return node;
}

static node_t *node_find_or_create(sb_t *sb, sb_idx_t idx)
{
	node_t *node;

	// Create root node, if it doesn't already exist.
	if (!sb->root) {
		node = malloc(sizeof(node_t));
		MEMCLEAR(node, sizeof(*node));
		node->idx_start = idx - (idx % NUM_MASK_BITS);
		sb->root = node;

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
			node_t *child = malloc(sizeof(node_t));
			if (child != NULL) {
				MEMCLEAR(child, sizeof(*child));
				child->idx_start = idx - (idx % NUM_MASK_BITS);
				child->parent = parent;
				parent->left = child;
			}

			assert((child == NULL) || node_supports_idx(child, idx));
			return child;
		} else {
			if (node->right) {
				node = node->right;
				continue;
			}

			node_t *parent = node;
			node_t *child = malloc(sizeof(node_t));
			if (child != NULL) {
				MEMCLEAR(child, sizeof(*child));
				child->idx_start = idx - (idx % NUM_MASK_BITS);
				child->parent = parent;
				parent->right = child;
			}

			assert((child == NULL) || node_supports_idx(child, idx));
			return child;
		}
	}

	assert((node != NULL) && node_supports_idx(node, idx));

	return node;
}

static bool node_supports_idx(node_t *node, sb_idx_t idx)
{
	if ((idx >= node->idx_start)
		&& (idx <= (node->idx_start + (NUM_MASK_BITS - 1))) ) {
		return true;
	}

	return false;
}

static sb_num_t node_num_set(node_t *node)
{
	sb_num_t num = 0;

	for (sb_idx_t n = 0; n < NUM_MASK_BITS; n++) {
		if (node->mask & ((mask_t)1 << n)) {
			num++;
		}
	}

	return num;
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

static void node_transplant(sb_t *sb, node_t *u, node_t *v)
{
	if (u->parent == NULL) {
		sb->root = v;
	} else if (u == u->parent->left) {
		u->parent->left = v;
	} else {
		u->parent->right = v;
	}
	if (v != NULL) {
		v->parent = u->parent;
	}

	return;
}

static void node_delete(sb_t *sb, node_t *node)
{
	// Should only delete nodes that represent no set bits.
	assert(node->mask == 0);

	if (node->left == NULL) {
		node_transplant(sb, node, node->right);
	} else if (node->right == NULL) {
		node_transplant(sb, node, node->left);
	} else {
		node_t *y = node_minimum(node->right);

		if (y->parent != node) {
			node_transplant(sb, y, y->right);
			y->right = node->right;
			y->right->parent = y;
		}
		node_transplant(sb, node, y);
		y->left = node->left;
		y->left->parent = y;
	}

	free(node);
}

void sb_pdynstr_free(sb_pdynstr_t pdynstr)
{



}

// Returns NULL on sucess, otherwise returns a pointer to a dynamically
// allocated error message.
sb_pdynstr_t sb_validate(sb_t* sb)
{
	sb_num_t sum_pernode_set = 0;

	// For each node.
	if (sb->root) {
		for (node_t *node = node_minimum(sb->root); node != NULL;
			node = node_next(node)) {
			sum_pernode_set += node_num_set(node);

			// Nodes should only exist if they represent at least 1 set bit.
			if (node_num_set(node) == 0) {
				return dyn_sprintf("Node doesn't represent any set bits,\n"
					"  node: %p mask: %#llx",
					node, node->mask);
			}
		}
	}
	if (sum_pernode_set != sb->num_set) {
		return dyn_sprintf("sb->num_set: %llu not equal to"
			"sum_pernode_set: %llu", sb->num_set, sum_pernode_set);
	}

	return NULL;
}

// Returns a pointer to a dynamically allocated string, that represents
// the current state of the sparsebit array pointed to by sb.
sb_pdynstr_t sb_dump(sb_t* sb)
{
	sb_pdynstr_t s = dyn_sprintf("sb: %p\n"
		"  root: %p num_set: %llu\n", sb, sb->root, sb->num_set);
	if (sb->root) {
		for (node_t *node = node_minimum(sb->root); node != NULL;
			node = node_next(node)) {
			sb_pdynstr_t prev = s;
			s = dyn_sprintf("%s  node: %p idx_start: %#llx mask: %#llx\n"
				"    parent: %p left: %p right: %p\n",
				prev, node, node->idx_start, node->mask,
				node->parent, node->left, node->right);
			free(prev);
		}
	}

	return s;
}

static sb_pdynstr_t dyn_sprintf(const char *format, ...)
{
	int n;
	size_t s_allocated;
	va_list ap;
	char *s;

	// Determine size of string, if the caller doesn't change any of the
	// argument values, while this call is being performed.
	va_start(ap, format);
	n = vsnprintf(s, 0, format, ap);
	va_end(ap);

	// For now, just get a pointer to any allocated memory. Later, the loop
	// below will reallocate to the anticipated size.
	s = malloc(0);
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



