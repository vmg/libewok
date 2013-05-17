#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <assert.h>
#include <stdlib.h>

typedef uint32_t ewk_word;

#define BITS_IN_WORD (sizeof(ewk_word) * 8)

struct ewk_bitarray {
	ewk_word *buffer;
	size_t buffer_size;
	size_t alloc_size;
	size_t bit_size;
	size_t last_rlw;
};

#define RLW_RUNNING_BITS (sizeof(ewk_word) * 4)
#define RLW_LITERAL_BITS (sizeof(ewk_word) * 8 - 1 - RLW_RUNNING_BITS)

#define RLW_LARGEST_RUNNING_COUNT (((ewk_word)1 << RLW_RUNNING_BITS) - 1)
#define RLW_LARGEST_LITERAL_COUNT (((ewk_word)1 << RLW_LITERAL_BITS) - 1)

#define RLW_LARGEST_RUNNING_COUNT_SHIFT (RLW_LARGEST_RUNNING_COUNT << 1)

#define RLW_RUNNING_LEN_PLUS_BIT ((1 << (RLW_RUNNING_BITS + 1)) - 1)

static bool rlw_get_run_bit(struct ewk_bitarray *self)
{
	return self->buffer[self->last_rlw] & (ewk_word)1;
}

static void rlw_set_run_bit(struct ewk_bitarray *self, bool b)
{
	if (b) {
		self->buffer[self->last_rlw] |= (ewk_word)1;
	} else {
		self->buffer[self->last_rlw] &= (ewk_word)(~1);
	}
}

static void rlw_set_running_len(struct ewk_bitarray *self, ewk_word l)
{
	self->buffer[self->last_rlw] |= RLW_LARGEST_RUNNING_COUNT_SHIFT;
	self->buffer[self->last_rlw] &= (l << 1) | (~RLW_LARGEST_RUNNING_COUNT_SHIFT);
}

static ewk_word rlw_get_running_len(struct ewk_bitarray *self)
{
	return (self->buffer[self->last_rlw] >> 1) & RLW_LARGEST_RUNNING_COUNT;
}

static ewk_word rlw_get_literal_words(struct ewk_bitarray *self)
{
	return self->buffer[self->last_rlw] >> (1 + RLW_RUNNING_BITS);
}

static ewk_word rlw_set_literal_words(struct ewk_bitarray *self, ewk_word l)
{
	self->buffer[self->last_rlw] |= ~RLW_RUNNING_LEN_PLUS_BIT;
	self->buffer[self->last_rlw] &=
		(l << (RLW_RUNNING_BITS + 1)) | RLW_RUNNING_LEN_PLUS_BIT;
}

static ewk_word rlw_size(struct ewk_bitarray *self)
{
	return rlw_get_running_len(self) + rlw_get_literal_words(self);
}

static ewk_word min_size(size_t a, size_t b)
{
	return a < b ? a : b;
}

static void buffer_push(struct ewk_bitarray *self, ewk_word value)
{
	if (self->buffer_size + 1 >= self->alloc_size) {
		self->alloc_size = self->alloc_size * 1.5;
		self->buffer = realloc(self->buffer, self->alloc_size * sizeof(ewk_word));
	}

	self->buffer[self->buffer_size++] = value;
}

static void add_word_stream(struct ewk_bitarray *self, bool v, size_t number)
{
	if (rlw_get_run_bit(self) != v && rlw_size(self) == 0) {
		rlw_set_run_bit(self, v);
	}
	else if (rlw_get_literal_words(self) != 0 || rlw_get_run_bit(self) != v) {
		buffer_push(self, 0);
		self->last_rlw = self->buffer_size - 1;

		if (v)
			rlw_set_run_bit(self, v);
	}

	ewk_word runlen = rlw_get_running_len(self); 
	ewk_word can_add = min_size(number, RLW_LARGEST_RUNNING_COUNT - runlen);

	rlw_set_running_len(self, runlen + can_add);
	number -= can_add;

	while (number >= RLW_LARGEST_RUNNING_COUNT) {
		buffer_push(self, 0);
		self->last_rlw = self->buffer_size - 1;

		if (v) rlw_set_run_bit(self, v);
		rlw_set_running_len(self, RLW_LARGEST_RUNNING_COUNT);

		number -= RLW_LARGEST_RUNNING_COUNT;
	}

	if (number > 0) {
		buffer_push(self, 0);
		self->last_rlw = self->buffer_size - 1;

		if (v) rlw_set_run_bit(self, v);
		rlw_set_running_len(self, number);
	}
}

static size_t add_literal(struct ewk_bitarray *self, ewk_word new_data)
{
	ewk_word current_num = rlw_get_literal_words(self); 

	if (current_num >= RLW_LARGEST_LITERAL_COUNT) {
		buffer_push(self, 0);
		self->last_rlw = self->buffer_size - 1;

		rlw_set_literal_words(self, 1);
		buffer_push(self, new_data);
		return 2;
	}

	rlw_set_literal_words(self, current_num + 1);

	/* sanity check */
	assert(rlw_get_literal_words(self) == current_num + 1);

	buffer_push(self, new_data);
	return 1;
}

static size_t add_empty_word(struct ewk_bitarray *self, bool v)
{
	bool no_literal = (rlw_get_literal_words(self) == 0);
	ewk_word run_len = rlw_get_running_len(self);

	if (no_literal && run_len == 0) {
		rlw_set_run_bit(self, v);
		assert(rlw_get_run_bit(self) == v);
	}

	if (no_literal && rlw_get_run_bit(self) == v && run_len < RLW_LARGEST_RUNNING_COUNT) {
		rlw_set_running_len(self, run_len + 1);
		assert(rlw_get_running_len(self) == run_len + 1);
		return 0;
	}
	
	else {
		buffer_push(self, 0);
		self->last_rlw = self->buffer_size - 1;

		assert(rlw_get_running_len(self) == 0);
		assert(rlw_get_run_bit(self) == 0);
		assert(rlw_get_literal_words(self) == 0);

		rlw_set_run_bit(self, v);
		assert(rlw_get_run_bit(self) == v);

		rlw_set_running_len(self, 1);
		assert(rlw_get_running_len(self) == 1);
		assert(rlw_get_literal_words(self) == 0);
		return 1;
	}
}

void ewk_set(struct ewk_bitarray *self, size_t i)
{
	const size_t dist =
		(i + BITS_IN_WORD) / BITS_IN_WORD -
		(self->bit_size + BITS_IN_WORD - 1) / BITS_IN_WORD;

	assert(i >= self->bit_size);

	self->bit_size = i + 1;

	if (dist > 0) {
		if (dist > 1)
			add_word_stream(self, false, dist - 1);

		add_literal(self, 1 << (i % BITS_IN_WORD));
		return;
	}

	if (rlw_get_literal_words(self) == 0) {
		rlw_set_running_len(self, rlw_get_running_len(self) - 1);
		add_literal(self, 1 << (i % BITS_IN_WORD));
		return;
	}

	self->buffer[self->buffer_size - 1] |= (1 << (i % BITS_IN_WORD));

	/* check if we just completed a stream of 1s */
	if (self->buffer[self->buffer_size - 1] == (ewk_word)(~0)) {
		self->buffer[--self->buffer_size] = 0;
		rlw_set_literal_words(self, rlw_get_literal_words(self) - 1);
		add_empty_word(self, true);
	}
}

void ewk_each_bit(struct ewk_bitarray *self, void (*callback)(size_t, void*), void *payload)
{
	size_t pos = 0;
	size_t pointer = 0;
	size_t k;

	while (pointer < self->buffer_size) {
		self->last_rlw = pointer;

		if (rlw_get_run_bit(self)) {
			size_t len = rlw_get_running_len(self) * BITS_IN_WORD;
			for (k = 0; k < len; ++k, ++pos) {
				callback(pos, payload);
			}
		} else {
			pos += rlw_get_running_len(self) * BITS_IN_WORD;
		}

		++pointer;

		for (k = 0; k < rlw_get_literal_words(self); ++k) {
			int c;

			/* todo: zero count optimization */
			for (c = 0; c < BITS_IN_WORD; ++c, ++pos) {
				if ((self->buffer[pointer] & (1 << c)) != 0) {
					callback(pos, payload);
				}
			}

			++pointer;
		}
	}
}

struct ewk_bitarray *ewk_new(void)
{
	struct ewk_bitarray *array;

	array = malloc(sizeof(struct ewk_bitarray));
	if (array == NULL)
		return NULL;

	array->buffer = malloc(32 * sizeof(ewk_word));
	array->alloc_size = 32;

	array->buffer_size = 1;
	array->bit_size = 0;
	array->last_rlw = 0;

	return array;
}

static void test_print(size_t pos, void *p)
{
	printf("%d, ", (int)pos);
}

int main(int argc, char *argv[])
{
	struct ewk_bitarray *array = ewk_new();
	int i;

	ewk_set(array, 3);
	ewk_set(array, 24);
	ewk_set(array, 42);
	ewk_set(array, 4242);
	ewk_set(array, 12345);
	ewk_set(array, 99999);
	ewk_set(array, 42424242);

	for (i = 0; i < array->buffer_size; ++i)
		printf("%08X ", array->buffer[i]);
	printf("\n");

	ewk_each_bit(array, &test_print, NULL);
	printf("\n");

	return 0;
}
