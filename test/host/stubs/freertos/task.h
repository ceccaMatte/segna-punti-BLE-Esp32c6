#pragma once
#include <stddef.h>
#include <stdint.h>
static inline uint32_t xTaskGetTickCount(void) { return 0; }
static inline void vTaskDelay(unsigned delay) { (void)delay; }
static inline int xTaskCreate(void(*fn)(void*), const char *name, unsigned stack, void *arg, unsigned priority, void *handle)
{ (void)fn; (void)name; (void)stack; (void)arg; (void)priority; (void)handle; return 1; }
