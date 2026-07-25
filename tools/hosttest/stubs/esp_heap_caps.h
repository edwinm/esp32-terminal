#pragma once
#include <stdlib.h>
#include <stddef.h>
#define MALLOC_CAP_DMA      1
#define MALLOC_CAP_INTERNAL 2
#define MALLOC_CAP_SPIRAM   4
static inline void *heap_caps_malloc(size_t n, int caps) { (void)caps; return malloc(n); }
static inline size_t heap_caps_get_free_size(int caps) { (void)caps; return 200000; }
