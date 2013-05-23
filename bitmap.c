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
#include <string.h>

#include "ewok.h"

#define MASK(x) ((eword_t)1 << (x % BITS_IN_WORD))
#define BLOCK(x) (x / BITS_IN_WORD)

struct bitmap *bitmap_new(void)
{
	struct bitmap *bitmap = ewah_malloc(sizeof(struct bitmap));
	bitmap->words = ewah_calloc(32, sizeof(eword_t));
	bitmap->word_alloc = 32;
	return bitmap;
}

void bitmap_set(struct bitmap *self, size_t pos)
{
	size_t block = BLOCK(pos);

	if (block >= self->word_alloc) {
		size_t old_size = self->word_alloc;
		self->word_alloc = block * 2;
		self->words = ewah_realloc(self->words, self->word_alloc * sizeof(eword_t));

		memset(self->words + old_size, 0x0,
			(self->word_alloc - old_size) * sizeof(eword_t));
	}

	self->words[block] |= MASK(pos);
}

void bitmap_clear(struct bitmap *self, size_t pos)
{
	size_t block = BLOCK(pos);

	if (block < self->word_alloc)
		self->words[block] &= ~MASK(pos);
}

bool bitmap_get(struct bitmap *self, size_t pos)
{
	size_t block = BLOCK(pos);
	return block < self->word_alloc && (self->words[block] & MASK(pos)) != 0;
}

struct ewah_bitmap *bitmap_compress(struct bitmap *bitmap)
{
	struct ewah_bitmap *ewah = ewah_new();
	size_t i, running_empty_words = 0;
	eword_t last_word = 0;

	for (i = 0; i < bitmap->word_alloc; ++i) {
		if (bitmap->words[i] == 0) {
			running_empty_words++;
			continue;
		}

		if (last_word != 0) {
			ewah_add(ewah, last_word);
		}

		if (running_empty_words > 0) {
			ewah_add_empty_words(ewah, false, running_empty_words);
			running_empty_words = 0;
		}

		last_word = bitmap->words[i];
	}

	ewah_add(ewah, last_word);
	return ewah;
}

struct bitmap *ewah_to_bitmap(struct ewah_bitmap *ewah)
{
	struct bitmap *bitmap = bitmap_new();
	struct ewah_iterator it;
	eword_t blowup;
	size_t i = 0;

	ewah_iterator_init(&it, ewah);

	while (ewah_iterator_next(&blowup, &it)) {
		if (i >= bitmap->word_alloc) {
			bitmap->word_alloc *= 1.5;
			bitmap->words = ewah_realloc(
				bitmap->words, bitmap->word_alloc * sizeof(eword_t));
		}

		bitmap->words[i++] = blowup;
	}

	bitmap->word_alloc = i;
	return bitmap;
}

void bitmap_free(struct bitmap *bitmap)
{
	free(bitmap->words);
	free(bitmap);
}
