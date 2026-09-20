# Playmaker wearable — ESP32-C3

Questo branch converte il vecchio segnapunti ESP32-C6 in un firmware dedicato al wearable. Lo stack scelto e' ESP-IDF + NimBLE.

## Architettura

- **button_manager**: debounce e riconoscimento click, doppio click, triplo click e pressione lunga.
- **gesture_engine**: concatena primitive in sequenze configurabili; usa una finestra temporale per risolvere prefissi ambigui.
- **config_store**: configurazione versionata in NVS. Le gesture sono array di token a 4 bit: 2 bit pulsante + 2 bit primitiva.
- **ble_manager**: GATT, commissioning persistente, autenticazione con token, coda eventi, retry e ACK idempotenti.
- **wearable_protocol**: protocollo binario indipendente dal trasporto.
- **sound_manager**: un solo owner del buzzer e coda dei pattern sonori.
- **power_manager**: batteria, stato di carica e LED.

## Pinout PCB attuale

Pinout derivato dallo schema fornito il 20/09/2026:

| Funzione | ESP32-C3 GPIO | Note |
| --- | ---: | --- |
| BTN_A | GPIO3 | ingresso active-low, pull-up esterno 100 kΩ |
| BTN_B | GPIO1 | ingresso active-low, pull-up esterno 100 kΩ |
| BTN_Moment | GPIO0 | ingresso active-low, pull-up esterno 100 kΩ |
| BTN_UNDO | GPIO5 | ingresso active-low, pull-up esterno 100 kΩ |
| Buzzer | GPIO20 | uscita PWM/LEDC |
| battery_state | GPIO4 | collegato sul PCB; ADC1_CH4 se il segnale e' analogico |
| 3V3 | 3V3 | alimentazione logica |
| +5V | 5V | ingresso 5 V della SuperMini |

GPIO2, GPIO8 e GPIO9 sono strapping pin dell'ESP32-C3 e non vengono usati dal wearable. I quattro tasti sono tutti su GPIO0..5, quindi la piedinatura lascia aperta la possibilita' di usarli come wake source da deep sleep.

GPIO20 coincide con U0RXD sull'ESP32-C3. Per evitare che il buzzer condivida il pin con la console UART0, il branch imposta la console di sviluppo su USB Serial/JTAG.

`battery_state` non e' la tensione della batteria: e' MCP73831 STAT portato a GPIO4 / ADC1_CH4 attraverso R10=100 kΩ e R11=100 kΩ. Il firmware lo legge in analogico. Con VDD caricatore a 5 V, STAT HIGH produce circa 2.5 V sul GPIO; STAT LOW produce circa 0 V.

Per MCP73831 STAT e' LOW durante la carica, HIGH a carica completata e High-Z quando il caricatore non e' alimentato. Con R11 verso massa, High-Z torna circa 0 V. Di conseguenza il solo `battery_state` distingue bene "carica completa" da "non completa", ma LOW e' ambiguo fra "sta caricando" e "USB/5V assente".

Per far lampeggiare un LED *solo* durante la carica serve quindi anche un segnale VBUS/+5V-present (oppure un LED alimentato direttamente dal ramo +5V con una topologia hardware adatta). Il relativo GPIO e il GPIO del LED restano disabilitati finche' non vengono definiti nello schema.

Analogamente, `battery_state` non permette di sapere quando la LiPo si sta scaricando sotto una soglia. Il beep di batteria scarica richiede una misura separata di Vbat tramite partitore su un ADC. Il firmware non usa STAT come falsa misura della batteria.

## Semantica ACK

Ogni comando inviato dal wearable ha la chiave `(session_id, sequence)`. La web app deve deduplicare su questa coppia e, se riceve un retry, restituire lo stesso ACK senza applicare nuovamente il comando.

L'ACK v2 contiene lo stato partita autorevole **dopo** l'elaborazione del comando (revision, punti, game e set) e i flag di transizione `GAME_ENDED`, `SET_ENDED` e `MATCH_ENDED`. Il wearable non contiene il motore del punteggio. Questo evita divergenze quando la partita viene modificata direttamente dall'app: ogni nuovo comando viene applicato allo stato corrente lato app/server e il feedback sonoro deriva esclusivamente dall'ACK autorevole.

## Pairing

Il pairing applicativo e' separato dal collegamento BLE. Un token casuale da 128 bit viene salvato in NVS. Se il dispositivo e' gia' associato, la pagina deve autenticarsi con lo stesso token. La gesture configurata su `ENTER_PAIRING` cancella il token precedente e apre una finestra di commissioning.

La configurazione e' validata in modo che esista sempre almeno una gesture di pairing, evitando di rendere il dispositivo irrecuperabile.

## Timing predefiniti

- debounce: 25 ms
- multi-click: 300 ms
- long press: 1200 ms
- chiusura sequenza gesture: 300 ms
- pairing window: 120 s
- low-battery reminder: 20 s (attivo solo quando la misura batteria sara' configurata)

## Config BLE

La characteristic CONFIG supporta comandi piccoli, adatti a Web Bluetooth:

- `0x10`: set mapping
- `0x11`: delete mapping
- `0x12`: set action sound
- `0x13`: set gesture timings
- `0x14`: reset defaults

La lettura della characteristic restituisce l'intera configurazione in formato binario versionato.


## Robustezza firmware

Il refactoring separa il modello di configurazione dalla persistenza NVS:
`core/config_model` contiene default e validazione, mentre `storage/config_store`
si occupa soltanto della memoria permanente. In questo modo protocollo e logica
di configurazione restano testabili su PC senza ESP-IDF.

Le primitive dei pulsanti e il Gesture Engine proteggono con sezioni critiche i
parametri modificabili dalla pagina web, evitando race fra task input, timer e
callback BLE.

La coda BLE ha dimensione limitata, retry con backoff e massimo numero di
tentativi. `TEMPORARY_ERROR` non rimuove il comando; `OK` e `REJECTED` sono
terminali. Il feedback ACTION + GAME/SET/MATCH viene inserito nel Sound Manager
come un unico pattern, così la melodia di fine game non può essere separata dal
beep di conferma dello stesso ACK.

Quando viene avviato un nuovo pairing, il firmware notifica il vecchio client
prima di disconnetterlo. La pagina di configurazione sospende l'auto-reconnect e
non può reclamare automaticamente il wearable: una nuova associazione richiede
esplicitamente il pulsante **Connetti / Pair**.
