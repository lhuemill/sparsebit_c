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

#ifndef _SPARSEBIT_C_H_
#define _SPARSEBIT_C_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define SB_VERSION 1

struct sb;  // Intentional incomplete structure definition.
            // Concrete private definition only available within
            // private library implementation.
typedef struct sb sb_t;

typedef uint64_t sb_idx_t;
#define SB_PRI_IDX PRIx64
typedef uint64_t sb_num_t;
#define SB_PRI_NUM PRIx64

#define SB_IDX_MAX (~(sb_idx_t)0)
#define SB_NUM_MAX (~(sb_num_t)0)

sb_t *sb_create(void);
void sb_delete(sb_t* sb);
int sb_copy(sb_t *dest, const sb_t *src);

// ==== Set Operations ====
int sb_set(sb_t* sb, sb_idx_t idx);
int sb_setnum(sb_t* sb, sb_idx_t idx_start, sb_num_t num);
bool sb_anyset(const sb_t* sb);
bool sb_allset(const sb_t* sb);
sb_num_t sb_numset(const sb_t* sb);
bool sb_isset(const sb_t* sb, sb_idx_t idx);
sb_idx_t sb_firstset(const sb_t* sb);
sb_idx_t sb_lastset(const sb_t* sb);
sb_idx_t sb_nextset(const sb_t* sb, sb_idx_t idx);
sb_idx_t sb_prevset(const sb_t* sb, sb_idx_t idx);
sb_idx_t sb_nthset(const sb_t* sb, sb_idx_t nth);

// ==== Clear/Cleared Operations ====
int sb_clear(sb_t* sb, sb_idx_t idx);
int sb_clearnum(sb_t* sb, sb_idx_t idx_start, sb_num_t num);
bool sb_anycleared(const sb_t* sb);
sb_num_t sb_numcleared(const sb_t* sb);
bool sb_iscleared(const sb_t* sb, sb_idx_t idx);
sb_idx_t sb_firstcleared(const sb_t* sb);
sb_idx_t sb_nextcleared(const sb_t* sb, sb_idx_t idx);
sb_idx_t sb_prevcleared(const sb_t* sb, sb_idx_t idx);

// ==== Pesistent operations and records
// 0000 ---- SB_RCD_INC4_MASK8
// 0001 ---- SB_RCD_INC4_MASK16
// 0010 ---- SB_RCD_INC4_MASK32
// 0011 ---- SB_RCD_INC4_MASK64
// 0100 ---- SB_RCD_INC12
// 0101 ---- SB_RCD_INC12_MASK8
// 0110 ---- SB_RCD_INC12_MASK16
// 0111 0000 SB_RCD_END
// 0111 0001 SB_RCD_SETNUM8
// 0111 0010 SB_RCD_SETNUM64
// 0111 0011 SB_RCD_INC64
// 0111 0100
// 0111 0101
// 0111 0110
// 0111 0111 
// 0111 1000
// 0111 1001 
// 0111 1010 
// 0111 1011 
// 0111 1100 
// 0111 1101 
// 0111 1110 
// 0111 1111 
// 1--- ---- SB_RCD_INC7_SET1

#define SB_ID "sbit"

typedef enum {
	SB_RCD_INC4_MASK8   = 0x00,
	SB_RCD_INC4_MASK16  = 0x10,
	SB_RCD_INC4_MASK32  = 0x20,
	SB_RCD_INC4_MASK64  = 0x30,

	SB_RCD_INC12        = 0x40,
	SB_RCD_INC12_MASK8  = 0x50,
	SB_RCD_INC12_MASK16 = 0x60,

	SB_RCD_END = 0x70,
	SB_RCD_SETNUM8,
	SB_RCD_SETNUM64,
	SB_RCD_INC64,

	SB_RCD_INC7_SET1 = 0x80,
} sb_rcd_id_t;

// 0000 ---- SB_RCD_INC4_MASK8
typedef struct {
	uint8_t id;
	uint8_t amt : 4;
	uint8_t mask;
} sb_rcd_inc4_mask8_t;
typedef struct {
	uint8_t id_amt;
	uint8_t mask;
} sb_rcd_inc4_mask8_packed_t;

// 0001 ---- SB_RCD_INC4_MASk16
typedef struct {
	uint8_t id;
	uint8_t amt : 4;
	uint16_t mask;
} sb_rcd_inc4_mask16_t;
typedef struct {
	uint8_t id_amt;
	uint8_t mask[2];
} sb_rcd_inc4_mask16_packed_t;

// 0010 ---- SB_RCD_INC4_MASK32
typedef struct {
	uint8_t id;
	uint8_t amt : 4;
	uint32_t mask;
} sb_rcd_inc4_mask32_t;
typedef struct {
	uint8_t id_amt;
	uint8_t mask[4];
} sb_rcd_inc4_mask32_packed_t;

// 0011 ---- SB_RCD_INC4_MASK64
typedef struct {
	uint8_t id;
	uint8_t amt : 4;
	uint64_t mask;
} sb_rcd_inc4_mask64_t;
typedef struct {
	uint8_t id_amt;
	uint8_t mask[8];
} sb_rcd_inc4_mask64_packed_t;

// 0100 ---- SB_RCD_INC12
typedef struct {
	uint8_t id;
	uint16_t amt : 12;
} sb_rcd_inc12_t;
typedef struct {
	uint8_t id_amt0;
	uint8_t amt1;
} sb_rcd_inc12_packed_t;

// 0101 ---- SB_RCD_INC12_MASK8
typedef struct {
	uint8_t id;
	uint16_t amt : 12;
	uint8_t mask;
} sb_rcd_inc12_mask8_t;
typedef struct {
	uint8_t id_amt0;
	uint8_t amt1;
	uint8_t mask;
} sb_rcd_inc12_mask8_packed_t;

// 0110 ---- SB_RCD_INC12_MASK16
typedef struct {
	uint8_t id;
	uint16_t amt : 12;
	uint16_t mask;
} sb_rcd_inc12_mask16_t;
typedef struct {
	uint8_t id_amt0;
	uint8_t amt1;
	uint8_t mask[2];
} sb_rcd_inc12_mask16_packed_t;

// 0111 0000 SB_RCD_END
typedef struct {
	uint8_t id;
} sb_rcd_end_t;
typedef struct {
	uint8_t id;
} sb_rcd_end_packed_t;

// 0111 0001 SB_RCD_SETNUM8
typedef struct {
	uint8_t id;
	uint8_t amt;
} sb_rcd_setnum8_t;
typedef struct {
	uint8_t id;
	uint8_t amt;
} sb_rcd_setnum8_packed_t;

// 0111 0010 SB_RCD_SETNUM64
typedef struct {
	uint8_t id;
	uint64_t amt;
} sb_rcd_setnum64_t;
typedef struct {
	uint8_t id;
	uint8_t amt[8];
} sb_rcd_setnum64_packed_t;

// 0111 1000 SB_RCD_INC64
typedef struct {
	uint8_t id;
	uint64_t amt;
} sb_rcd_inc64_t;
typedef struct {
	uint8_t id;
	uint8_t amt[8];
} sb_rcd_inc64_packed_t;

// 1--- ---- SB_RCD_INC7_SET1
typedef struct {
	uint8_t id;
	uint8_t amt : 7;
} sb_rcd_inc7_set1_t;
typedef struct {
	uint8_t id_amt;
} sb_rcd_inc7_set1_packed_t;

union sb_rcd {
	uint8_t id;
	sb_rcd_inc12_t        inc12;
	sb_rcd_inc12_mask8_t  inc12_mask8;
	sb_rcd_inc12_mask16_t inc12_mask16;
	sb_rcd_end_t          end;
	sb_rcd_setnum8_t      setnum8;
	sb_rcd_setnum64_t     setnum64;
	sb_rcd_inc64_t        inc64;
	sb_rcd_inc4_mask8_t   inc4_mask8;
	sb_rcd_inc4_mask16_t  inc4_mask16;
	sb_rcd_inc4_mask32_t  inc4_mask32;
	sb_rcd_inc4_mask64_t  inc4_mask64;
	sb_rcd_inc7_set1_t    inc7_set1;
};

union sb_rcd_packed {
	uint8_t id;
	sb_rcd_inc12_packed_t        inc12;
	sb_rcd_inc12_mask8_packed_t  inc12_mask8;
	sb_rcd_inc12_mask16_packed_t inc12_mask16;
	sb_rcd_end_packed_t          end;
	sb_rcd_setnum8_packed_t      setnum8;
	sb_rcd_setnum64_packed_t     setnum64;
	sb_rcd_inc64_packed_t        inc64;
	sb_rcd_inc4_mask8_packed_t   inc4_mask8;
	sb_rcd_inc4_mask16_packed_t  inc4_mask16;
	sb_rcd_inc4_mask32_packed_t  inc4_mask32;
	sb_rcd_inc4_mask64_packed_t  inc4_mask64;
	sb_rcd_inc7_set1_packed_t    inc7_set1;
};

int sb_save(FILE *file, const sb_t *sb);
int sb_load(FILE *file, sb_t *sb);

#ifdef SB_TEST
sb_num_t sb_nodes_num(const sb_t *sb);
size_t sb_mask_bits_min(void);
size_t sb_mask_bits_max(void);
typedef char * sb_pdynstr_t;  // Dynamically allocated \0 terminated string.
                              // Caller is responsible for calling
                              // sb_dynstr_free() once caller is done
                              // with the string.
sb_pdynstr_t sb_dyn_sprintf(const char *format, ...);
void sb_dynstr_free(sb_pdynstr_t pdynstr);
sb_pdynstr_t sb_validate(const sb_t* sb);
sb_pdynstr_t sb_dump(const sb_t* sb);
#endif /* SB_TEST */

#ifdef __cplusplus
}
#endif

#endif /* _SPARSEBIT_C_H_ */
