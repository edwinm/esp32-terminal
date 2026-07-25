// Host-test stub: the mutex is a no-op; the tests are single-threaded.
#pragma once
#include "freertos/FreeRTOS.h"
typedef struct { int dummy; } StaticSemaphore_t;
typedef StaticSemaphore_t *SemaphoreHandle_t;
static inline SemaphoreHandle_t xSemaphoreCreateMutexStatic(StaticSemaphore_t *b) { return b; }
static inline int xSemaphoreTake(SemaphoreHandle_t s, unsigned t) { (void)s; (void)t; return 1; }
static inline int xSemaphoreGive(SemaphoreHandle_t s) { (void)s; return 1; }
