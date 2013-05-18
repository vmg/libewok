/*
 * Copyright 2013, GitHub, Inc
 * Copyright 2009-2013, Daniel Lemire, Cliff Moon,
 *	David McIntosh, Robert Becho, Google Inc. and Veronika Zenz
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 * 
 * 	http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef __EWOK_RLW_H__
#define __EWOK_RLW_H__

#define RLW_RUNNING_BITS (sizeof(eword_t) * 4)
#define RLW_LITERAL_BITS (sizeof(eword_t) * 8 - 1 - RLW_RUNNING_BITS)

#define RLW_LARGEST_RUNNING_COUNT (((eword_t)1 << RLW_RUNNING_BITS) - 1)
#define RLW_LARGEST_LITERAL_COUNT (((eword_t)1 << RLW_LITERAL_BITS) - 1)

#define RLW_LARGEST_RUNNING_COUNT_SHIFT (RLW_LARGEST_RUNNING_COUNT << 1)

#define RLW_RUNNING_LEN_PLUS_BIT (((eword_t)1 << (RLW_RUNNING_BITS + 1)) - 1)

static bool rlw_get_run_bit(const eword_t *word)
{
	return *word & (eword_t)1;
}

static void rlw_set_run_bit(eword_t *word, bool b)
{
	if (b) {
		*word |= (eword_t)1;
	} else {
		*word &= (eword_t)(~1);
	}
}

static void rlw_xor_run_bit(eword_t *word)
{
	if (*word & 1) {
		*word &= (eword_t)(~1);
	} else {
		*word |= (eword_t)1;
	}
}

static void rlw_set_running_len(eword_t *word, eword_t l)
{
	*word |= RLW_LARGEST_RUNNING_COUNT_SHIFT;
	*word &= (l << 1) | (~RLW_LARGEST_RUNNING_COUNT_SHIFT);
}

static eword_t rlw_get_running_len(const eword_t *word)
{
	return (*word >> 1) & RLW_LARGEST_RUNNING_COUNT;
}

static eword_t rlw_get_literal_words(const eword_t *word)
{
	return *word >> (1 + RLW_RUNNING_BITS);
}

static void rlw_set_literal_words(eword_t *word, eword_t l)
{
	*word |= ~RLW_RUNNING_LEN_PLUS_BIT;
	*word &= (l << (RLW_RUNNING_BITS + 1)) | RLW_RUNNING_LEN_PLUS_BIT;
}

static eword_t rlw_size(const eword_t *self)
{
	return rlw_get_running_len(self) + rlw_get_literal_words(self);
}

#endif

