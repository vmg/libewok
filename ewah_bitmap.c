/**
 * Copyright 2013, GitHub, Inc
 * Copyright 2009-2013, Daniel Lemire, Cliff Moon,
 *	David McIntosh, Robert Becho, Google Inc. and Veronika Zenz
 * 
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */
#include <assert.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>

#include "ewok.h"
#include "ewok_rlw.h"

#if defined(__linux__)
#  include <endian.h>
#elif defined(__FreeBSD__) || defined(__NetBSD__)
#  include <sys/endian.h>
#elif defined(__OpenBSD__)
#  include <sys/types.h>
#  define be16toh(x) betoh16(x)
#  define be32toh(x) betoh32(x)
#  define be64toh(x) betoh64(x)
#endif

static inline eword_t min_size(size_t a, size_t b)
{
	return a < b ? a : b;
}

static inline void buffer_grow(struct ewah_bitmap *self, size_t new_size)
{
	size_t rlw_offset = (uint8_t *)self->rlw - (uint8_t *)self->buffer;

	if (self->alloc_size >= new_size)
		return;

	self->alloc_size = new_size;
	self->buffer = realloc(self->buffer, self->alloc_size * sizeof(eword_t));
	assert(self->buffer);

	self->rlw = self->buffer + (rlw_offset / sizeof(size_t));
}

static inline void buffer_push(struct ewah_bitmap *self, eword_t value)
{
	if (self->buffer_size + 1 >= self->alloc_size) {
		buffer_grow(self, self->buffer_size * 1.5);
	}

	self->buffer[self->buffer_size++] = value;
}

static void buffer_push_rlw(struct ewah_bitmap *self, eword_t value)
{
	buffer_push(self, value);
	self->rlw = self->buffer + self->buffer_size - 1;
}

static size_t add_empty_word_stream(struct ewah_bitmap *self, bool v, size_t number)
{
	size_t added = 0;

	if (rlw_get_run_bit(self->rlw) != v && rlw_size(self->rlw) == 0) {
		rlw_set_run_bit(self->rlw, v);
	}
	else if (rlw_get_literal_words(self->rlw) != 0 || rlw_get_run_bit(self->rlw) != v) {
		buffer_push_rlw(self, 0);
		if (v) rlw_set_run_bit(self->rlw, v);
		added++;
	}

	eword_t runlen = rlw_get_running_len(self->rlw); 
	eword_t can_add = min_size(number, RLW_LARGEST_RUNNING_COUNT - runlen);

	rlw_set_running_len(self->rlw, runlen + can_add);
	number -= can_add;

	while (number >= RLW_LARGEST_RUNNING_COUNT) {
		buffer_push_rlw(self, 0);
		added++;

		if (v) rlw_set_run_bit(self->rlw, v);
		rlw_set_running_len(self->rlw, RLW_LARGEST_RUNNING_COUNT);

		number -= RLW_LARGEST_RUNNING_COUNT;
	}

	if (number > 0) {
		buffer_push_rlw(self, 0);
		added++;

		if (v) rlw_set_run_bit(self->rlw, v);
		rlw_set_running_len(self->rlw, number);
	}

	return added;
}

size_t ewah_bitmap_add_empty_word_stream(struct ewah_bitmap *self, bool v, size_t number)
{
	if (number == 0)
		return 0;

	self->bit_size += number * BITS_IN_WORD;
	return add_empty_word_stream(self, v, number);
}

static size_t add_literal(struct ewah_bitmap *self, eword_t new_data)
{
	eword_t current_num = rlw_get_literal_words(self->rlw); 

	if (current_num >= RLW_LARGEST_LITERAL_COUNT) {
		buffer_push_rlw(self, 0);

		rlw_set_literal_words(self->rlw, 1);
		buffer_push(self, new_data);
		return 2;
	}

	rlw_set_literal_words(self->rlw, current_num + 1);

	/* sanity check */
	assert(rlw_get_literal_words(self->rlw) == current_num + 1);

	buffer_push(self, new_data);
	return 1;
}

static void add_dirty_word_stream(
	struct ewah_bitmap *self, const eword_t *buffer, size_t number, bool negate)
{
	size_t literals, can_add;

	while (1) {
		literals = rlw_get_literal_words(self->rlw);
		can_add = min_size(number, RLW_LARGEST_LITERAL_COUNT - literals);

		rlw_set_literal_words(self->rlw, literals + can_add);

		if (self->buffer_size + can_add >= self->alloc_size) {
			buffer_grow(self, (self->buffer_size + can_add) * 1.5);
		}

		if (negate) {
			size_t i;
			for (i = 0; i < can_add; ++i)
				self->buffer[self->buffer_size++] = ~buffer[i];
		} else {
			memcpy(self->buffer + self->buffer_size, buffer, can_add * sizeof(eword_t));
			self->buffer_size += can_add;
		}

		self->bit_size += can_add * BITS_IN_WORD;

		if (can_add == number)
			return;

		buffer_push_rlw(self, 0);
		buffer += can_add;
		number -= can_add;
	}
}

static size_t add_empty_word(struct ewah_bitmap *self, bool v)
{
	bool no_literal = (rlw_get_literal_words(self->rlw) == 0);
	eword_t run_len = rlw_get_running_len(self->rlw);

	if (no_literal && run_len == 0) {
		rlw_set_run_bit(self->rlw, v);
		assert(rlw_get_run_bit(self->rlw) == v);
	}

	if (no_literal && rlw_get_run_bit(self->rlw) == v &&
		run_len < RLW_LARGEST_RUNNING_COUNT) {
		rlw_set_running_len(self->rlw, run_len + 1);
		assert(rlw_get_running_len(self->rlw) == run_len + 1);
		return 0;
	}
	
	else {
		buffer_push_rlw(self, 0);

		assert(rlw_get_running_len(self->rlw) == 0);
		assert(rlw_get_run_bit(self->rlw) == 0);
		assert(rlw_get_literal_words(self->rlw) == 0);

		rlw_set_run_bit(self->rlw, v);
		assert(rlw_get_run_bit(self->rlw) == v);

		rlw_set_running_len(self->rlw, 1);
		assert(rlw_get_running_len(self->rlw) == 1);
		assert(rlw_get_literal_words(self->rlw) == 0);
		return 1;
	}
}

size_t ewah_bitmap_add(struct ewah_bitmap *self, eword_t word)
{
	self->bit_size += BITS_IN_WORD;

	if (word == 0)
		return add_empty_word(self, false);
	
	if (word == (eword_t)(~0))
		return add_empty_word(self, true);

	return add_literal(self, word);
}

void ewah_bitmap_set(struct ewah_bitmap *self, size_t i)
{
	const size_t dist =
		(i + BITS_IN_WORD) / BITS_IN_WORD -
		(self->bit_size + BITS_IN_WORD - 1) / BITS_IN_WORD;

	assert(i >= self->bit_size);

	self->bit_size = i + 1;

	if (dist > 0) {
		if (dist > 1)
			add_empty_word_stream(self, false, dist - 1);

		add_literal(self, (eword_t)1 << (i % BITS_IN_WORD));
		return;
	}

	if (rlw_get_literal_words(self->rlw) == 0) {
		rlw_set_running_len(self->rlw, rlw_get_running_len(self->rlw) - 1);
		add_literal(self, (eword_t)1 << (i % BITS_IN_WORD));
		return;
	}

	self->buffer[self->buffer_size - 1] |= ((eword_t)1 << (i % BITS_IN_WORD));

	/* check if we just completed a stream of 1s */
	if (self->buffer[self->buffer_size - 1] == (eword_t)(~0)) {
		self->buffer[--self->buffer_size] = 0;
		rlw_set_literal_words(self->rlw, rlw_get_literal_words(self->rlw) - 1);
		add_empty_word(self, true);
	}
}

void ewah_bitmap_each_bit(struct ewah_bitmap *self, void (*callback)(size_t, void*), void *payload)
{
	size_t pos = 0;
	size_t pointer = 0;
	size_t k;

	while (pointer < self->buffer_size) {
		eword_t *word = &self->buffer[pointer];

		if (rlw_get_run_bit(word)) {
			size_t len = rlw_get_running_len(word) * BITS_IN_WORD;
			for (k = 0; k < len; ++k, ++pos) {
				callback(pos, payload);
			}
		} else {
			pos += rlw_get_running_len(word) * BITS_IN_WORD;
		}

		++pointer;

		for (k = 0; k < rlw_get_literal_words(word); ++k) {
			int c;

			/* todo: zero count optimization */
			for (c = 0; c < BITS_IN_WORD; ++c, ++pos) {
				if ((self->buffer[pointer] & ((eword_t)1 << c)) != 0) {
					callback(pos, payload);
				}
			}

			++pointer;
		}
	}
}

void ewah_bitmap_not(struct ewah_bitmap *self)
{
	size_t pointer = 0;

	while (pointer < self->buffer_size) {
		eword_t *word = &self->buffer[pointer];
		size_t literals, k; 

		rlw_xor_run_bit(word);
		++pointer;

		literals = rlw_get_literal_words(word);
		for (k = 0; k < literals; ++k) {
			self->buffer[pointer] = ~self->buffer[pointer];
			++pointer;
		}
	}
}

int ewah_bitmap_serialize(struct ewah_bitmap *self, int fd)
{
	size_t i;
	eword_t dump[2048];
	const size_t words_per_dump = sizeof(dump) / sizeof(eword_t);

	/* 32 bit -- bit size fr the map */
	uint32_t bitsize =  htobe32((uint32_t)self->bit_size);
	if (write(fd, &bitsize, 4) != 4)
		return -1;

	/** 32 bit -- number of compressed 64-bit words */
	uint32_t word_count =  htobe32((uint32_t)self->buffer_size);
	if (write(fd, &word_count, 4) != 4)
		return -1;

	/** 64 bit x N -- compressed words */
	const eword_t *buffer = self->buffer;
	size_t words_left = self->buffer_size;

	while (words_left >= words_per_dump) {
		for (i = 0; i < words_per_dump; ++i, ++buffer)
			dump[i] = htobe64(*buffer);

		if (write(fd, dump, sizeof(dump)) != sizeof(dump))
			return -1;

		words_left -= words_per_dump;
	}

	if (words_left) {
		for (i = 0; i < words_left; ++i, ++buffer)
			dump[i] = htobe64(*buffer);

		if (write(fd, dump, words_left * 8) != words_left * 8)
			return -1;
	}

	/** 32 bit -- position for the RLW */
	uint32_t rlw_pos = (uint8_t*)self->rlw - (uint8_t *)self->buffer;
	rlw_pos = htobe32(rlw_pos / sizeof(eword_t));

	if (write(fd, &rlw_pos, 4) != 4)
		return -1;

	return 0;
}

int ewah_bitmap_deserialize(struct ewah_bitmap *self, int fd)
{
	size_t i;
	eword_t dump[2048];
	const size_t words_per_dump = sizeof(dump) / sizeof(eword_t);

	/* 32 bit -- bit size fr the map */
	uint32_t bitsize;
	if (read(fd, &bitsize, 4) != 4)
		return -1;

	self->bit_size = (size_t)be32toh(bitsize);

	/** 32 bit -- number of compressed 64-bit words */
	uint32_t word_count;
	if (read(fd, &word_count, 4) != 4)
		return -1;

	self->buffer_size = (size_t)be32toh(word_count);
	self->buffer = realloc(self->buffer, self->buffer_size * sizeof(eword_t));

	if (!self->buffer)
		return -1;

	/** 64 bit x N -- compressed words */
	eword_t *buffer = self->buffer;
	size_t words_left = self->buffer_size;

	while (words_left >= words_per_dump) {
		if (read(fd, dump, sizeof(dump)) != sizeof(dump))
			return -1;

		for (i = 0; i < words_per_dump; ++i, ++buffer)
			*buffer = be64toh(dump[i]);

		words_left -= words_per_dump;
	}

	if (words_left) {
		if (read(fd, dump, words_left * 8) != words_left * 8)
			return -1;

		for (i = 0; i < words_left; ++i, ++buffer)
			*buffer = be64toh(dump[i]);
	}

	/** 32 bit -- position for the RLW */
	uint32_t rlw_pos;
	if (read(fd, &rlw_pos, 4) != 4)
		return -1;

	self->rlw = self->buffer + be32toh(rlw_pos);

	return 0;
}

struct ewah_bitmap *ewah_bitmap_new(void)
{
	struct ewah_bitmap *bitmap;

	bitmap = malloc(sizeof(struct ewah_bitmap));
	if (bitmap == NULL)
		return NULL;

	bitmap->buffer = malloc(32 * sizeof(eword_t));
	bitmap->alloc_size = 32;

	ewah_bitmap_clear(bitmap);

	return bitmap;
}

void ewah_bitmap_clear(struct ewah_bitmap *bitmap)
{
	bitmap->buffer_size = 1;
	bitmap->bit_size = 0;
	bitmap->rlw = bitmap->buffer;
}

void ewah_bitmap_free(struct ewah_bitmap *bitmap)
{
	free(bitmap->buffer);
	free(bitmap);
}

static void read_new_rlw(struct ewah_iterator *it)
{
	const eword_t *word = NULL;

	it->literals = 0;
	it->compressed = 0;

	while (1) {
		word = &it->buffer[it->pointer];

		it->rl = rlw_get_running_len(word);
		it->lw = rlw_get_literal_words(word);
		it->b = rlw_get_run_bit(word);

		if (it->rl || it->lw)
			return;

		if (it->pointer < it->buffer_size - 1) {
			it->pointer++;
		} else {
			it->pointer = it->buffer_size;
			return;
		}
	}
}

bool ewah_iterator_next(eword_t *next, struct ewah_iterator *it)
{
	if (it->pointer >= it->buffer_size)
		return false;

	if (it->compressed < it->rl) {
		it->compressed++;
		*next = it->b ? (eword_t)(~0) : 0;
	} else {
		assert(it->literals < it->lw);

		it->literals++;
		it->pointer++;

		assert(it->pointer < it->buffer_size);

		*next = it->buffer[it->pointer];
	}

	if (it->compressed == it->rl && it->literals == it->lw) {
		if (++it->pointer < it->buffer_size)
			read_new_rlw(it);
	}

	return true;
}

void ewah_iterator_init(struct ewah_iterator *it, struct ewah_bitmap *parent)
{
	it->buffer = parent->buffer;
	it->buffer_size = parent->buffer_size;
	it->pointer = 0;

	it->lw = 0;
	it->rl = 0;
	it->compressed = 0;
	it->literals = 0;
	it->b = false;

	if (it->pointer < it->buffer_size)
		read_new_rlw(it);
}

struct rlw_iterator {
	const eword_t *buffer;
	size_t size;
	size_t pointer;
	size_t literal_word_start;

	struct {
		const eword_t *word;
		int literal_words;
		int running_len;
		int literal_word_offset;
		int running_bit;
	} rlw;
};

static inline size_t rlw_iterator_word_size(struct rlw_iterator *it)
{
	return it->rlw.running_len + it->rlw.literal_words;
}

static inline size_t rlw_iterator_literal_words(struct rlw_iterator *it)
{
	return it->pointer - it->rlw.literal_words;
}

static inline bool rlw_iterator_next_word(struct rlw_iterator *it)
{
	if (it->pointer >= it->size)
		return false;

	it->rlw.word = &it->buffer[it->pointer];
	it->pointer = rlw_get_literal_words(it->rlw.word) + 1;

	it->rlw.literal_words = rlw_get_literal_words(it->rlw.word);
	it->rlw.running_len = rlw_get_running_len(it->rlw.word);
	it->rlw.running_bit = rlw_get_run_bit(it->rlw.word);
	it->rlw.literal_word_offset = 0;

	return true;
}

void rlw_iterator_init(struct rlw_iterator *it, struct ewah_bitmap *bitmap)
{
	it->buffer = bitmap->buffer;
	it->size = bitmap->buffer_size;
	it->pointer = 0;

	rlw_iterator_next_word(it);

	it->literal_word_start = rlw_iterator_literal_words(it) + it->rlw.literal_word_offset;
}

void rlw_iterator_discard_first_words(struct rlw_iterator *it, size_t x)
{
	while (x > 0) {
		size_t discard;

		if (it->rlw.running_len > x) {
			it->rlw.running_len -= x;
			return;
		}

		x -= it->rlw.running_len;
		it->rlw.running_len = 0;

		discard = (x > it->rlw.literal_words) ? it->rlw.literal_words : x;

		it->literal_word_start += discard;
		it->rlw.literal_words -= discard;
		x -= discard;

		if (x > 0 || rlw_iterator_word_size(it) == 0) {
			if (!rlw_iterator_next_word(it))
				break;

			it->literal_word_start =
				rlw_iterator_literal_words(it) + it->rlw.literal_word_offset;
		}
	}
}

size_t rlw_iterator_discharge(
	struct rlw_iterator *it, struct ewah_bitmap *out, size_t max)
{
	size_t index = 0;

	while (index < max && rlw_iterator_word_size(it) > 0) {
		size_t pd, pl = it->rlw.running_len;

		if (index + pl > max) {
			pl = max - index;
		}

		ewah_bitmap_add_empty_word_stream(out, it->rlw.running_bit, pl);
		index += pl;

		pd = it->rlw.literal_words;
		if (pd + index > max) {
			pd = max - index;
		}

		add_dirty_word_stream(out, it->buffer + it->literal_word_start, pd, false);
		rlw_iterator_discard_first_words(it, pd + pl);
		index += pd;
	}

	return index;
}

size_t rlw_iterator_discharge_negated(
	struct rlw_iterator *it, struct ewah_bitmap *out, size_t max)
{
	size_t index = 0;

	while (index < max && rlw_iterator_word_size(it) > 0) {
		size_t pd, pl = it->rlw.running_len;

		if (index + pl > max) {
			pl = max - index;
		}

		ewah_bitmap_add_empty_word_stream(out, !it->rlw.running_bit, pl);
		rlw_iterator_discard_first_words(it, pl);
		index += pl;

		pd = it->rlw.literal_words;
		if (pd + index > max) {
			pd = max - index;
		}

		add_dirty_word_stream(out, it->buffer + it->literal_word_start, pd, true);
		rlw_iterator_discard_first_words(it, pd);
		index += pd;
	}

	return index;
}

struct ewah_bitmap *
ewah_bitmap_xor(struct ewah_bitmap *bitmap_i, struct ewah_bitmap *bitmap_j)
{
	struct ewah_bitmap *out = ewah_bitmap_new();

	struct rlw_iterator rlw_i;
	struct rlw_iterator rlw_j;

	rlw_iterator_init(&rlw_i, bitmap_i);
	rlw_iterator_init(&rlw_j, bitmap_j);

	while (rlw_iterator_word_size(&rlw_i) > 0 && rlw_iterator_word_size(&rlw_j) > 0) {
		while (rlw_i.rlw.running_len > 0 || rlw_j.rlw.running_len > 0) {
			struct rlw_iterator *prey, *predator;

			if (rlw_i.rlw.running_len < rlw_j.rlw.running_len) {
				prey = &rlw_i;
				predator = &rlw_j;
			} else {
				prey = &rlw_j;
				predator = &rlw_i;
			}

			if (predator->rlw.running_bit == 0) {
				size_t index = 
					rlw_iterator_discharge(prey, out, predator->rlw.running_len);

				ewah_bitmap_add_empty_word_stream(out,
					false, predator->rlw.running_len - index);

				rlw_iterator_discard_first_words(predator, predator->rlw.running_len);
			} else {
				size_t index = 
					rlw_iterator_discharge_negated(prey, out, predator->rlw.running_len);

				ewah_bitmap_add_empty_word_stream(out,
					true, predator->rlw.running_len - index);

				rlw_iterator_discard_first_words(predator, predator->rlw.running_len);
			}
		}

		size_t literals = min_size(rlw_i.rlw.literal_words, rlw_j.rlw.literal_words);

		if (literals) {
			size_t k;

			for (k = 0; k < literals; ++k) {
				ewah_bitmap_add(out,
					rlw_i.buffer[rlw_i.literal_word_start + k] ^
					rlw_j.buffer[rlw_j.literal_word_start + k]
				);
			}

			rlw_iterator_discard_first_words(&rlw_i, literals);
			rlw_iterator_discard_first_words(&rlw_j, literals);
		}
	}

	return out;
}
