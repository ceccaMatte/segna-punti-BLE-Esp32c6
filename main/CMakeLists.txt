# Segnapunti padel.
#
# La divisione importante e' fra i moduli che sanno cos'e' un ESP32 e quelli che
# non lo sanno. I secondi (match, history, button, controller, font, gfx, dirty,
# ui_view) si compilano anche su un computer normale: e' li' che vengono provati,
# con test\run_tests.ps1, senza scheda collegata. Restano in questo elenco
# perche' servono anche al programma che gira sul chip.
#
# Chi disegna davvero parla con l'hardware: ui.c e display.c.

set(PADEL_LOGIC_SOURCES
    "match.c"
    "history.c"
    "button.c"
    "controller.c"
    "font.c"
    "gfx.c"
    "dirty.c"
    "ui_view.c"
)

set(PADEL_DEVICE_SOURCES
    "ui.c"
    "display.c"
)

# Serve il driver del display (che a sua volta vuole SPI e GPIO), quello della
# retroilluminazione, che e' pilotata in larghezza di impulso, e il contatore di
# microsecondi con cui si misura il tempo davvero trascorso fra un giro e
# l'altro del ciclo principale.
idf_component_register(SRCS
                       ${PADEL_LOGIC_SOURCES}
                       ${PADEL_DEVICE_SOURCES}
                       "main.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_driver_gpio esp_driver_spi esp_driver_ledc esp_timer esp_lcd)

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
    CONTROLLER_FINISHED_SCREEN_MS=${CONFIG_PADEL_FINAL_SCREEN_MS}u
    MATCH_SETS_TO_WIN=${CONFIG_PADEL_SETS_TO_WIN}
)
