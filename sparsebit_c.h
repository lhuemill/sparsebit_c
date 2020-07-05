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

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

struct sb;  // Intentional incomplete structure definition.
            // Concrete private definition only available within
            // private library implementation.
typedef struct sb sb_t;

typedef uint64_t sb_idx_t;
#define SB_PRI_IDX PRIx64
typedef uint64_t sb_num_t;
#define SB_PRI_NUM PRIx64

#define SB_IDX_MAX (~(sb_idx_t)0)

sb_t *sb_create(void);
void sb_delete(sb_t* sb);
int sb_copy(sb_t *dest, const sb_t *src);

// ==== Set Operations ====
int sb_set(sb_t* sb, sb_idx_t idx);
int sb_setnum(sb_t* sb, sb_idx_t idx_start, sb_num_t num);
bool sb_anyset(const sb_t* sb);
sb_num_t sb_numset(const sb_t* sb);
bool sb_isset(const sb_t* sb, sb_idx_t idx);
sb_idx_t sb_firstset(const sb_t* sb);
sb_idx_t sb_nextset(const sb_t* sb, sb_idx_t idx);
sb_idx_t sb_prevset(const sb_t* sb, sb_idx_t idx);

// ==== Clear/Cleared Operations ====
int sb_clear(sb_t* sb, sb_idx_t idx);
int sb_clearnum(sb_t* sb, sb_idx_t idx_start, sb_num_t num);
bool sb_anycleared(const sb_t* sb);
sb_num_t sb_numcleared(const sb_t* sb);
bool sb_iscleared(const sb_t* sb, sb_idx_t idx);
sb_idx_t sb_firstcleared(const sb_t* sb);
sb_idx_t sb_nextcleared(const sb_t* sb, sb_idx_t idx);
sb_idx_t sb_prevcleared(const sb_t* sb, sb_idx_t idx);

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
#endif
