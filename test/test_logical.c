#include <stdio.h>
#include <stdlib.h>
#include "ewok.h"

static void cb__blowup_test(size_t pos, void *payload)
{
	struct bitmap *bm = payload;
	bitmap_set(bm, pos);
}

static void verify_blowup(struct ewah_bitmap *ewah, struct bitmap *blowup)
{
	struct bitmap *aux = bitmap_new();
	size_t i;
	ewah_each_bit(ewah, &cb__blowup_test, aux);

	for (i = 0; i < aux->word_alloc; ++i) {
		if (aux->words[i] != blowup->words[i]) {
			fprintf(stderr, "[%zu / %zu] %016llx vs %016llx ## FAIL \n",
				i, aux->word_alloc,
				(unsigned long long)aux->words[i],
				(unsigned long long)blowup->words[i]);
				exit(-1);
		}

	}

	bitmap_free(aux);
}

static size_t op_xor(size_t a, size_t b)
{
	return a ^ b;
}

static size_t op_and(size_t a, size_t b)
{
	return a & b;
}

static size_t op_or(size_t a, size_t b)
{
	return a | b;
}

static size_t op_andnot(size_t a, size_t b)
{
	return a & ~b;
}

static bool verify_operation(
	struct ewah_bitmap *_a, struct ewah_bitmap *_b,
	struct ewah_bitmap *_result, size_t (*op)(size_t, size_t))
{
	struct bitmap *a = ewah_to_bitmap(_a);
	struct bitmap *b = ewah_to_bitmap(_b);
	struct bitmap *result = ewah_to_bitmap(_result);
	size_t i;
	bool ok = true;

	verify_blowup(_a, a);
	verify_blowup(_b, b);
	verify_blowup(_result, result);

	fprintf(stderr, "%zu ", _result->bit_size / BITS_IN_WORD);

	for (i = 0; i < _result->bit_size / BITS_IN_WORD; ++i) {
		size_t r = op(a->words[i], b->words[i]);

		if (r != result->words[i]) {
			fprintf(stderr, "\nMiss [%zu / %zu] GOT %016llX EXPECT %016llX\n",
				i, result->word_alloc,
				(unsigned long long)r,
				(unsigned long long)result->words[i]
			);
			ok = false;
			break;
		}
	}

	bitmap_free(a);
	bitmap_free(b);
	bitmap_free(result);

	return ok;
}

static void cb__test_print(size_t pos, void *p)
{
	printf("%zu, ", pos);
}

static void print_bitmap(const char *name, struct ewah_bitmap *bitmap)
{
	printf("%s = {", name);
	ewah_each_bit(bitmap, &cb__test_print, NULL);
	printf("};\n\n");
}

static struct ewah_bitmap *generate_bitmap(size_t max_size)
{
	static const size_t BIT_CHANCE = 50;

	struct ewah_bitmap *bitmap = ewah_new();
	size_t i;

	for (i = 0; i < max_size; ++i) {
		if (rand() % 100 <= BIT_CHANCE)
			ewah_set(bitmap, i);
	}

	return bitmap;
}

static void test_for_size(size_t size)
{
	struct ewah_bitmap *a = generate_bitmap(size);
	struct ewah_bitmap *b = generate_bitmap(size);
	struct ewah_bitmap *result = ewah_new();
	size_t i;

	struct {
		const char *name;
		void (*generate)(struct ewah_bitmap *, struct ewah_bitmap *, struct ewah_bitmap *);
		size_t (*check)(size_t, size_t);
	} tests[] = {
		{"or", &ewah_or, &op_or},
		{"xor", &ewah_xor, &op_xor},
		{"and", &ewah_and, &op_and},
		{"and-not", &ewah_and_not, &op_andnot}
	};

	for (i = 0; i < sizeof(tests)/sizeof(tests[0]); ++i) {
		fprintf(stderr, "'%s' in %zu bits... ", tests[i].name, size);

		tests[i].generate(a, b, result);

		if (verify_operation(a, b, result, tests[i].check))
			fprintf(stderr, "OK\n");

		ewah_clear(result);
	}

	ewah_free(a);
	ewah_free(b);
	ewah_free(result);
}

int main(int argc, char *argv[])
{
	size_t i;
	srand(time(NULL));

	for (i = 8; i < 30; ++i) {
		test_for_size((size_t)1 << i);
	}

	return 0;
}
