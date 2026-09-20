#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "wearable_types.h"

#define WEARABLE_TOKEN_LEN 16u

void config_store_init(void);
void config_store_defaults(wearable_config_t *out);
bool config_store_load(wearable_config_t *out);
bool config_store_save(const wearable_config_t *config);
bool config_store_validate(const wearable_config_t *config);

bool config_store_load_pairing_token(uint8_t token[WEARABLE_TOKEN_LEN]);
bool config_store_save_pairing_token(const uint8_t token[WEARABLE_TOKEN_LEN]);
void config_store_clear_pairing_token(void);
