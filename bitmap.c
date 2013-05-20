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

#include "ewok.h"

#define MASK(x) (1 << (x % BITS_IN_WORD))
#define BLOCK(x) (x / BITS_IN_WORD)

void bitmap_set(struct bitmap *self, size_t pos)
{
	size_t block = BLOCK(pos);

	if (block >= self->word_alloc) {
		self->word_alloc = block * 2;
		self->words = realloc(self->words, self->word_alloc);
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

void bitmap_compress(struct ewah_bitmap *ewah, struct bitmap *bitmap)
{
	size_t i, running_empty_words = 0;
	eword_t last_word = 0;

	for (i = 0; i < bitmap->word_alloc; ++i) {
		if (bitmap->words[i] == 0) {
			running_empty_words++;
			continue;
		}

		if (last_word != 0) {
			ewah_bitmap_add(ewah, last_word);
		}

		if (running_empty_words > 0) {
			ewah_bitmap_add_word_stream(ewah, false, running_empty_words);
			running_empty_words = 0;
		}

		last_word = bitmap->words[i];
	}

	ewah_bitmap_add(ewah, last_word);
}
