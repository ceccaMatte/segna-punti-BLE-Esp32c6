#pragma once

/*
 * Flying prototype board profile.
 *
 * Keep hardware pin ownership here instead of Kconfig so an old generated
 * sdkconfig cannot silently restore a previous pinout.
 */
#define BOARD_GPIO_BUTTON_A       3
#define BOARD_GPIO_BUTTON_B       0
#define BOARD_GPIO_BUTTON_MOMENT  1
#define BOARD_GPIO_BUTTON_UNDO    2

#define BOARD_GPIO_BUZZER         5
#define BOARD_GPIO_AUX_INPUT      20
#define BOARD_GPIO_GND_SINK       21
