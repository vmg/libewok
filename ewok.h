#ifndef __EWAH_BITMAP_C__
#define __EWAH_BITMAP_C__

#include <stdbool.h>
#include <stdint.h>

typedef uint32_t eword_t;

struct ewah_bitmap {
	eword_t *buffer;
	size_t buffer_size;
	size_t alloc_size;
	size_t bit_size;
	eword_t *rlw;
};

struct ewah_bitmap *ewah_bitmap_new(void);
int ewah_bitmap_load(struct ewah_bitmap *self, int fd, bool load_bit_size);
int ewah_bitmap_dump(struct ewah_bitmap *self, int fd, bool dump_bit_size);

void ewah_bitmap_not(struct ewah_bitmap *self);
void ewah_bitmap_each_bit(struct ewah_bitmap *self, void (*callback)(size_t, void*), void *payload);

void ewah_bitmap_set(struct ewah_bitmap *self, size_t i);

#endif
