# Segnapunti padel.
#
# La divisione importante e' fra i moduli che sanno cos'e' un ESP32 e quelli che
# non lo sanno. I secondi (match, history, button, controller, font, gfx, dirty,
# ui_view, led_anim, ble_protocol, device_identity, hold_gesture,
# score_state_adapter, commissioning_state) si compilano anche su un computer
# normale: e' li' che vengono provati, con test\run_tests.ps1, senza scheda
# collegata. Restano in questo elenco perche' servono anche al programma che
# gira sul chip.
#
# Chi parla davvero con l'hardware: ui.c, display.c, rgb_led.c, ble_gatt.c,
# nvs_store.c, commissioning_manager.c, commissioning_ui.c, ble_score_service.c.

set(PADEL_LOGIC_SOURCES
    "match.c"
    "history.c"
    "button.c"
    "controller.c"
    "font.c"
    "gfx.c"
    "dirty.c"
    "ui_view.c"
    "led_anim.c"
    "ble_protocol.c"
    "device_identity.c"
    "hold_gesture.c"
    "score_state_adapter.c"
    "commissioning_state.c"
)

set(PADEL_DEVICE_SOURCES
    "ui.c"
    "display.c"
    "rgb_led.c"
    "ble_gatt.c"
    "nvs_store.c"
    "commissioning_manager.c"
    "commissioning_ui.c"
    "ble_score_service.c"
    "gpio_scan.c"
)

# Serve il driver del display (che a sua volta vuole SPI e GPIO), quello della
# retroilluminazione, che e' pilotata in larghezza di impulso, il periferico RMT
# che manda i colori al LED di bordo, il contatore di microsecondi con cui si
# misura il tempo davvero trascorso fra un giro e l'altro del ciclo principale,
# lo stack Bluetooth (NimBLE, il piu' leggero dei due) e la memoria permanente
# dove vive il token di associazione.
idf_component_register(SRCS
                       ${PADEL_LOGIC_SOURCES}
                       ${PADEL_DEVICE_SOURCES}
                       "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_driver_gpio esp_driver_spi esp_driver_ledc esp_driver_rmt esp_timer esp_lcd esp_hw_support bt nvs_flash)

# Le scelte fatte da menuconfig arrivano ai moduli puri come costanti di
# compilazione. E' l'unico modo di configurare dei moduli che non possono
# includere sdkconfig.h senza perdere la possibilita' di compilarli sul PC.
#
# I moduli non si accorgono di niente: le loro intestazioni scrivono
# "#ifndef ... #define <valore predefinito> ... #endif", quindi una costante
# gia' definita qui vince, e il valore scritto nel sorgente resta come ripiego
# per i test sul computer.
target_compile_definitions(${COMPONENT_LIB} PRIVATE
    BTN_DEBOUNCE_MS=${CONFIG_PADEL_BUTTON_DEBOUNCE_MS}u
    BTN_MULTI_CLICK_MS=${CONFIG_PADEL_MULTI_CLICK_MS}u
    BTN_LONG_PRESS_MS=${CONFIG_PADEL_LONG_PRESS_MS}u
    BTN_VERY_LONG_PRESS_MS=${CONFIG_PADEL_BUTTON_COMMISSION_MS}u
    CONTROLLER_FINISHED_SCREEN_MS=${CONFIG_PADEL_FINAL_SCREEN_MS}u
    MATCH_SETS_TO_WIN=${CONFIG_PADEL_SETS_TO_WIN}
    LED_BRIGHTNESS_PERMILLE=${CONFIG_PADEL_LED_BRIGHTNESS}u
    COMMISSIONING_HOLD_MS=${CONFIG_PADEL_COMMISSIONING_HOLD_MS}u
    COMMISSIONING_WINDOW_MS=${CONFIG_PADEL_COMMISSIONING_WINDOW_MS}u
    COMMISSIONING_NOTICE_MS=${CONFIG_PADEL_COMMISSIONING_NOTICE_MS}u
)
