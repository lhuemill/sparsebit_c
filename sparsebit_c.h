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

// ==== Set Operations ====
int sb_set(sb_t* sb, sb_idx_t idx);
int sb_setnum(sb_t* sb, sb_idx_t idx, sb_num_t num);
bool sb_anyset(sb_t* sb);
sb_num_t sb_numset(sb_t* sb);
bool sb_isset(sb_t* sb, sb_idx_t idx);
sb_idx_t sb_firstset(sb_t* sb);
sb_idx_t sb_nextset(sb_t* sb, sb_idx_t idx);

// ==== Clear/Cleared Operations ====
int sb_clear(sb_t* sb, sb_idx_t idx);
int sb_clearnum(sb_t* sb, sb_idx_t idx, sb_num_t num);
bool sb_anycleared(sb_t* sb);
sb_num_t sb_numcleared(sb_t* sb);
bool sb_iscleared(sb_t* sb, sb_idx_t idx);
sb_idx_t sb_firstcleared(sb_t* sb);
sb_idx_t sb_nextcleared(sb_t* sb, sb_idx_t idx);

#ifdef SB_TEST
sb_num_t sb_nodes_num(sb_t *sb);
typedef char * sb_pdynstr_t;  // Dynamically allocated \0 terminated string.
                              // Caller is responsible for calling
                              // sb_pdynstr_free() once caller is done
                              // with the string.
void sb_pdynstr_free(sb_pdynstr_t pdynstr);
sb_pdynstr_t sb_validate(sb_t* sb);
sb_pdynstr_t sb_dump(sb_t* sb);
#endif
