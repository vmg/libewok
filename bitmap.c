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
