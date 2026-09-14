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

    test_match_all();
    test_button_all();
    test_controller_all();
    test_font_all();
    test_gfx_all();
    test_dirty_all();
    test_ui_view_all();
    test_layout_all();

    return test_summary();
}
