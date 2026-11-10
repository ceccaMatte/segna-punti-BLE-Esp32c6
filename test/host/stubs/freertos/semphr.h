#pragma once
#include <stddef.h>
typedef void *SemaphoreHandle_t;
#define portMAX_DELAY 0xffffffffu
static inline SemaphoreHandle_t xSemaphoreCreateMutex(void) { return (void *)1; }
static inline void xSemaphoreTake(SemaphoreHandle_t lock, unsigned delay) { (void)lock;(void)delay; }
static inline void xSemaphoreGive(SemaphoreHandle_t lock) { (void)lock; }
