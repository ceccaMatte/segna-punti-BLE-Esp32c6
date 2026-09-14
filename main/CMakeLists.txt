# Segnapunti padel.
#
# I sorgenti sono divisi per ruolo:
#
#   app/    il ciclo principale e le sue operazioni, una per file
#   game/   le regole del gioco
#   input/  i gesti del pulsante
#   ui/     il disegno sullo schermo
#   link/   Bluetooth, protocollo e associazione
#   board/  l'hardware della scheda: schermo, LED, piedini
#
# La divisione che conta davvero e' pero' un'altra, ed e' dentro queste cartelle:
# i moduli che non sanno cos'e' un ESP32 si compilano anche su un computer
# normale, ed e' li' che vengono provati, con test\run_tests.ps1, senza scheda
# collegata. Restano in questo elenco perche' servono anche al programma che
# gira sul chip.
#
# La lista PADEL_LOGIC_SOURCES e' la fonte di verita' di quella divisione: un
# modulo che entra li' va aggiunto anche a test\run_tests.ps1, che compila
# esattamente quelli.

set(PADEL_LOGIC_SOURCES
    "game/match.c"
    "game/history.c"
    "game/controller.c"
    "game/score_state_adapter.c"
    "input/button.c"
    "input/hold_gesture.c"
    "ui/font.c"
    "ui/gfx.c"
    "ui/dirty.c"
    "ui/ui_view.c"
    "board/led_anim.c"
    "link/ble_protocol.c"
    "link/device_identity.c"
    "link/commissioning_state.c"
    "app/timing.c"
)

set(PADEL_DEVICE_SOURCES
    "app/app.c"
    "app/banner.c"
    "app/gestures.c"
    "app/indicators.c"
    "app/screens.c"
    "ui/ui.c"
    "ui/commissioning_ui.c"
    "board/display.c"
    "board/rgb_led.c"
    "board/gpio_scan.c"
    "link/ble_gatt.c"
    "link/nvs_store.c"
    "link/commissioning_pin.c"
    "link/commissioning_manager.c"
    "link/ble_score_service.c"
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
                       INCLUDE_DIRS "." "app" "game" "input" "ui" "link" "board"
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
