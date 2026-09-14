# ESP32-C6-LCD-1.47 — Riferimento hardware

Pinout verificato della scheda Waveshare **ESP32-C6-LCD-1.47**. I valori per la
scheda gemella **ESP32-C5-LCD-1.47** restano indicati dove differiscono, ma il
segnapunti è scritto e verificato solo per la C6.

Fonti:

- [Waveshare — ESP32-C6-LCD-1.47](https://docs.waveshare.com/ESP32-C6-LCD-1.47/)
- [Waveshare — ESP32-C5-LCD-1.47](https://docs.waveshare.com/ESP32-C5-LCD-1.47/)
- [Espressif — esptool, ESP32-C6 boot mode selection](https://docs.espressif.com/projects/esptool/en/latest/esp32c6/advanced-topics/boot-mode-selection.html)
- [Espressif — esptool, ESP32-C5 boot mode selection](https://docs.espressif.com/projects/esptool/en/latest/esp32c5/advanced-topics/boot-mode-selection.html)
- [Espressif — ESP32-C6 USB Serial/JTAG console](https://docs.espressif.com/projects/esp-idf/en/v6.0/esp32c6/api-guides/usb-serial-jtag-console.html)
- [Espressif — ESP32-C5 USB Serial/JTAG console](https://docs.espressif.com/projects/esp-idf/en/v6.0/esp32c5/api-guides/usb-serial-jtag-console.html)

---

## Core

| Voce | ESP32-C6-LCD-1.47 | ESP32-C5-LCD-1.47 |
|---|---|---|
| MCU | ESP32-C6FH4 | ESP32-C5FH4 |
| Architettura | RISC-V 32 bit, 160 MHz + LP core 20 MHz | RISC-V 32 bit, 240 MHz + LP core 48 MHz |
| Memoria | 320 KB ROM, 512 KB HP SRAM, 16 KB LP SRAM, **4 MB flash** | 320 KB ROM, 384 KB HP SRAM, 16 KB LP SRAM, **4 MB flash** |
| PSRAM | **Assente** | **Assente** |
| Radio | Wi-Fi 6 2,4 GHz, BT 5 LE, IEEE 802.15.4 | Wi-Fi 6 2,4 / 5 GHz, BT 5 LE, IEEE 802.15.4 |
| Touch | **Assente** | **Assente** |
| Onboard | Slot TF, LED RGB, BOOT, RESET, LDO ME6217C33 | Slot TF, LED RGB, BOOT, RESET, MP1605GTF |

> ⚠️ **Attenzione al calore (C6)**: il produttore raccomanda di tenere la
> luminosità del display al **50% o meno** e di non usarlo a piena luminosità a
> lungo. Il surriscaldamento può causare aloni scuri permanenti sul pannello. Se
> il display mostra anomalie, lascia raffreddare la scheda e flasha un
> programma con luminosità più bassa.

---

## Display LCD

| Segnale | ESP32-C6 | ESP32-C5 |
|---|---|---|
| `MOSI` | **GPIO6** | **GPIO6** |
| `SCLK` | **GPIO7** | **GPIO7** |
| `LCD_CS` | **GPIO14** | **GPIO23** |
| `LCD_DC` | **GPIO15** | **GPIO24** |
| `LCD_RST` | **GPIO21** | **GPIO26** |
| `LCD_BL` (backlight, PWM via LEDC) | **GPIO22** | **GPIO10** |

| Parametro | Valore |
|---|---|
| Controller | ST7789 |
| Risoluzione | 172 × 320 (RGB565) |
| Interfaccia | SPI, modo 0, 40 MHz |
| Bus | **SPI2** dell'ESP32-C6 |
| Margine laterale | **X = 34**, Y = 0 in orientamento verticale |
| Inversione colori | **obbligatoria** |

### Tre cose che il pannello pretende

**Il margine di 34 pixel.** La memoria del controller ST7789 è più grande del
vetro visibile: la parte che si vede comincia a 34 pixel dal bordo sinistro
della memoria. Va dichiarata con `esp_lcd_panel_set_gap(panel, 34, 0)` in
orientamento verticale. Senza, l'immagine appare spostata e con una striscia di
rumore su un lato.

**L'inversione dei colori.** Questo pannello è montato con la polarizzazione
invertita rispetto agli ST7789 normali: senza `esp_lcd_panel_invert_color(panel,
true)` si vede il negativo dell'immagine. Non è una preferenza estetica, è un
requisito del pannello.

**L'ordine dei byte.** Il driver ST7789 di ESP-IDF v6.0.2 non ha nessuna opzione
per scambiare i byte dei pixel: l'ordine si sceglie con `data_endian` nella
configurazione del pannello, che il driver traduce nel bit corrispondente del
registro `RAMCTL`. Chiedendo `LCD_RGB_DATA_ENDIAN_LITTLE` il pannello fa da solo
lo scambio che altrimenti andrebbe fatto a mano su ogni byte prima di
trasmettere.

### Il controller SPI

ESP32-C6 ha tre controller SPI, ma SPI0 e SPI1 sono impegnati dalla memoria
flash: per le periferiche resta solo **SPI2**. Attenzione al numero: nella
costante `SPI2_HOST` il valore è **1**, non 2, perché l'enumerazione parte da
SPI1. Scrivere il numero grezzo 2 porta a `invalid host_id` e il bus non viene
creato — un errore che si vede solo a scheda accesa.

### La luminosità

La retroilluminazione è pilotata in larghezza di impulso su GPIO22. Il
segnapunti si ferma volutamente a metà potenza, per il motivo spiegato
nell'avviso sul calore qui sopra.

> ⚠️ Il display occupa tutta la larghezza di banda che gli serve: se in futuro si
> volesse usare anche la microSD, ricordare che i due condividono `SCLK` e `MOSI`.

---

## Slot microSD — SPI condiviso con il display

**Identico su entrambe le schede.**

| Segnale | GPIO |
|---|---|
| `SCLK` | **GPIO7** |
| `MOSI` | **GPIO6** |
| `MISO` | **GPIO5** |
| `CS` | **GPIO4** |
| `SD_D1`, `SD_D2` | non collegati |

> ⚠️ LCD e microSD condividono `SCLK` e `MOSI`. Se si usano entrambi, i rispettivi
> chip-select vanno gestiti con attenzione quando si cambia dispositivo sul bus.

---

## LED RGB

**Identico su entrambe le schede.**

| Segnale | GPIO |
|---|---|
| Dato WS2812B | **GPIO8** |

Un solo LED, ordine colori RGB. Nel core Arduino si pilota con `rgbLedWrite()`;
in ESP-IDF si usa il periferico RMT: il progetto scrive i ventiquattro bit del
colore nella memoria del canale e manda la trasmissione, senza librerie
esterne.

> ⚠️ Sulla ESP32-C6 GPIO8 è anche un pin di strapping e deve restare **alto**
> durante il reset. L'ingresso del WS2812B è ad alta impedenza, quindi non lo
disturba; quello che non si deve fare è configurare il piedino *prima* che il
chip abbia finito di avviarsi. Il progetto lo prepara dopo lo schermo, a partenza
avvenuta.

---

## USB e seriale

| Segnale | ESP32-C6 | ESP32-C5 |
|---|---|---|
| USB `D+` | **GPIO13** | **GPIO14** |
| USB `D-` | **GPIO12** | **GPIO13** |
| UART0 `TX` | **GPIO16** | **GPIO11** |
| UART0 `RX` | **GPIO17** | **GPIO12** |

Nessuna delle due schede **ha un chip USB-to-UART**: il connettore USB-C è
collegato direttamente al controller **USB Serial/JTAG** del chip. Di
conseguenza la console deve essere instradata su quella porta:

```
CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y
```

Senza questa opzione ESP-IDF manda la console primaria su UART0, raggiungibile
solo dall'header pin — quindi **il monitor seriale via USB non mostrerebbe
nulla**. Questo progetto la imposta in `sdkconfig.defaults`.

Su Windows il dispositivo appare come `COMx` con VID `303A` (Espressif) e nome
`Dispositivo seriale USB`; la stessa scheda espone anche una seconda interfaccia
`USB JTAG/serial debug unit`.

> ⚠️ Se l'applicazione riconfigura per errore i pin USB o disabilita il
> controller USB Serial/JTAG, il dispositivo sparisce dal sistema. Per
> recuperarlo bisogna entrare manualmente in download mode tirando basso il pin
> BOOT (GPIO9 sulla C6, GPIO28 sulla C5) e resettando il chip.

---

## Pulsanti

| Pulsante | ESP32-C6 | ESP32-C5 | Note |
|---|---|---|---|
| **BOOT** | **GPIO9** | **GPIO28** | Attivo basso, pull-up interno ~45 kΩ. **Anche pin di strapping del bootloader.** |
| RESET | — | — | Collegato a `EN` / `CHIP_PU` |

### Il doppio ruolo del pin BOOT

Il pin del pulsante è anche il pin di strapping che seleziona la modalità di
avvio. Secondo la documentazione esptool:

| Chip | Esecuzione normale | Download mode | Combinazione non valida |
|---|---|---|---|
| ESP32-C6 | GPIO9 alto al reset | GPIO9 basso al reset | GPIO8 basso **e** GPIO9 basso |
| ESP32-C5 | GPIO28 alto al reset | GPIO28 basso al reset | GPIO27 basso **e** GPIO28 basso |

Conseguenze pratiche:

| Situazione | Effetto |
|---|---|
| Premere BOOT mentre l'app è in esecuzione | Sicuro — è una normale lettura di GPIO |
| Tenere premuto BOOT durante un reset | La scheda entra in modalità download |
| Tenere premuto BOOT all'accensione | La scheda entra in modalità download |

Il pull-up interno è di circa 45 kΩ e ha una corrente limitata: se si collega un
pulsante esterno sul pin BOOT serve un pull-down "forte", ad esempio 10 kΩ verso
GND. Lo stesso vale per il pin di strapping secondario (GPIO8 sulla C6,
GPIO27 sulla C5), che deve restare alto.

---

## File meccanici

Le dimensioni meccaniche sono nei disegni nel repository del produttore.

> ⚠️ Il file DXF incluso dichiara `$INSUNITS=1` (pollici) mentre la geometria è
> in millimetri. Importandolo secondo l'header il modello risulta 25,4 volte più
> grande: forzare l'unità a millimetri e verificare le due quote di riferimento.
