#pragma once

#include "esp_err.h"

/*
 * Start the inactivity watchdog and report a previous Deep-sleep wake, if any.
 * The timeout is CONFIG_WEARABLE_IDLE_SLEEP_MS.
 */
esp_err_t sleep_manager_start(void);

/* Reset the inactivity timer on a physical button press. */
void sleep_manager_note_button_activity(void);
