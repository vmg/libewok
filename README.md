libewok
=======

EWAH Compressed bitmaps in C.

Usage
-----

```c
struct ewah_bitmap *array = ewah_bitmap_new();
struct ewah_iterator it;
eword_t word;

ewah_bitmap_set(array, 3);
ewah_bitmap_set(array, 32);
ewah_bitmap_set(array, 48);
ewah_bitmap_set(array, 63);
ewah_bitmap_set(array, 1024);
ewah_bitmap_set(array, 7600);

ewah_iterator_init(&it, array);

while (ewah_iterator_next(&word, &it))
    printf("%08llX ", word);

ewah_bitmap_each_bit(array, &print_a_bit, NULL);

ewah_bitmap_free(array);
````

Related docs:
------------

- https://github.com/lemire/EWAHBoolArray
- https://github.com/lemire/javaewah
- http://arxiv.org/abs/0901.3751
- http://www.slideshare.net/lemire/all-about-bitmap-indexes-and-sorting-them

License
-------

This port is based off the original EWAHBoolArray implementation by Daniel Lemire and it's licensed under the terms of the GPLv2, with the original author's permission.

Original contributors as follows:

- Daniel Lemire (http://lemire.me/en/), 
- Cliff Moon (https://github.com/cliffmoon), 
- David McIntosh (https://github.com/mctofu),
- Robert Becho (https://github.com/RBecho),
- Colby Ranger (https://github.com/crangeratgoogle)
- Veronika Zenz (https://github.com/veronikazenz)
