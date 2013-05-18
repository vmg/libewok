#ifndef __EWOK_BITMAP_C__
#define __EWOK_BITMAP_C__

#include <stdbool.h>
#include <stdint.h>

typedef uint64_t eword_t;
#define BITS_IN_WORD (sizeof(eword_t) * 8)

struct ewah_bitmap {
	eword_t *buffer;
	size_t buffer_size;
	size_t alloc_size;
	size_t bit_size;
	eword_t *rlw;
};

struct ewah_bitmap *ewah_bitmap_new(void);
void ewah_bitmap_clear(struct ewah_bitmap *bitmap);
void ewah_bitmap_free(struct ewah_bitmap *bitmap);

int ewah_bitmap_load(struct ewah_bitmap *self, int fd, bool load_bit_size);
int ewah_bitmap_dump(struct ewah_bitmap *self, int fd, bool dump_bit_size);

void ewah_bitmap_not(struct ewah_bitmap *self);
void ewah_bitmap_each_bit(struct ewah_bitmap *self, void (*callback)(size_t, void*), void *payload);

void ewah_bitmap_set(struct ewah_bitmap *self, size_t i);
size_t ewah_bitmap_add_word_stream(struct ewah_bitmap *self, bool v, size_t number);

struct ewah_iterator {
	const eword_t *buffer;
	size_t buffer_size;

	size_t pointer;
	eword_t compressed, literals;
	eword_t rl, lw;
	bool b;
};

void ewah_iterator_init(struct ewah_iterator *it, struct ewah_bitmap *parent);
bool ewah_iterator_next(eword_t *next, struct ewah_iterator *it);

struct bitmap {
	eword_t *words;
	size_t word_alloc;
};

void bitmap_set(struct bitmap *self, size_t pos);
void bitmap_clear(struct bitmap *self, size_t pos);
bool bitmap_get(struct bitmap *self, size_t pos);
void bitmap_compress(struct ewah_bitmap *ewah, struct bitmap *bitmap);

#endif
