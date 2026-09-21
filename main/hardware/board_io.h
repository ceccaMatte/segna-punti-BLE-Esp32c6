#pragma once

#include "esp_err.h"

/*
 * Initializes the board-only signals that do not belong to a functional
 * subsystem such as buttons, power or buzzer.
 */
esp_err_t board_io_init(void);
