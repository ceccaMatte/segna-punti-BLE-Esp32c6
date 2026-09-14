/**
 * @file test_main.c
 * @brief Punto di ingresso dei test host.
 *
 * SPDX-License-Identifier: MIT
 */

#include "test_util.h"

int main(void)
{
    printf("\nTest host della logica pura del segnapunti padel\n");
    printf("Nessun hardware richiesto.\n\n");

    test_timing_all();
    test_match_all();
    test_button_all();
    test_controller_all();
    test_font_all();
    test_gfx_all();
    test_dirty_all();
    test_ui_view_all();
    test_layout_all();
    test_led_anim_all();
    test_ble_protocol_all();
    test_device_identity_all();
    test_hold_gesture_all();
    test_score_state_adapter_all();
    test_commissioning_state_all();

    return test_summary();
}
