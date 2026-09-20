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

## Semantica ACK

Ogni comando inviato dal wearable ha la chiave `(session_id, sequence)`. La web app deve deduplicare su questa coppia e rispondere sempre con lo stesso ACK se riceve un retry. L'ACK contiene anche flag autorevoli `GAME_ENDED`, `SET_ENDED` e `MATCH_ENDED`; il wearable puo' quindi riprodurre una melodia senza conoscere le regole del padel.

## Pairing

Il pairing applicativo e' separato dal collegamento BLE. Un token casuale da 128 bit viene salvato in NVS. Se il dispositivo e' gia' associato, la pagina deve autenticarsi con lo stesso token. La gesture configurata su `ENTER_PAIRING` cancella il token precedente e apre una finestra di commissioning.

La configurazione e' validata in modo che esista sempre almeno una gesture di pairing, evitando di rendere il dispositivo irrecuperabile.

## Configurazione hardware

I GPIO non sono hard-coded nell'architettura: si impostano da `idf.py menuconfig -> Playmaker wearable`. I valori di default sono solo un punto di partenza e vanno allineati allo schema elettrico reale prima del flash.

Il rilevamento "carica completa" richiede un segnale power-present separato dal segnale CHRG. Se l'hardware non lo espone, lasciare `POWER_PRESENT_GPIO=-1`: il firmware non inventa uno stato FULL.

## Timing predefiniti

- debounce: 25 ms
- multi-click: 300 ms
- long press: 1200 ms
- chiusura sequenza gesture: 300 ms
- pairing window: 120 s
- low-battery reminder: 20 s

## Config BLE

La characteristic CONFIG supporta comandi piccoli, adatti a Web Bluetooth:

- `0x10`: set mapping
- `0x11`: delete mapping
- `0x12`: set action sound
- `0x13`: set gesture timings
- `0x14`: reset defaults

La lettura della characteristic restituisce l'intera configurazione in formato binario versionato.
