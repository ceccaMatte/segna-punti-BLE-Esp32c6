# Playmaker wearable — ESP32-C3

Firmware dedicato al wearable Playmaker. Stack: ESP-IDF + NimBLE.

## Architettura

- **button_manager**: debounce, click/double/triple/long e chord simultanee.
- **gesture_engine**: sequenze configurabili con risoluzione dei prefissi.
- **config_model**: default e validazione indipendenti da ESP-IDF.
- **config_store**: persistenza NVS.
- **ble_manager**: GATT, commissioning, coda, retry e ACK idempotenti.
- **wearable_protocol**: protocollo binario indipendente dal trasporto.
- **sound_manager**: owner unico del buzzer.
- **power_manager**: charger/batteria/LED nei limiti dell'hardware disponibile.

## Pipeline input

```text
GPIO
 -> debounce
 -> primitive singola oppure CHORD
 -> token 16 bit
 -> Gesture Engine
 -> action semantica
 -> coda BLE
 -> app/server autorevole
 -> ACK + stato + GAME/SET/MATCH
 -> feedback sonoro
```

### Chord

Default `chord_window_ms = 60`.

Il primo press stabile apre la finestra. Se almeno due pulsanti rimangono
premuti fino alla chiusura della finestra viene emesso un singolo token CHORD
con bitmask. Tutti i click/long dei pulsanti partecipanti sono consumati.

Questo rende deterministico il caso A+B: non vengono mai emessi POINT_A,
POINT_B e poi UNDO.

La chord può anche essere uno step dentro una sequenza più lunga.

## Pinout PCB attuale

| Funzione | ESP32-C3 GPIO | Note |
| --- | ---: | --- |
| BTN_A | GPIO3 | active-low, pull-up esterno 100 kΩ |
| BTN_B | GPIO1 | active-low, pull-up esterno 100 kΩ |
| BTN_Moment | GPIO0 | active-low, pull-up esterno 100 kΩ |
| BTN_UNDO | GPIO5 | active-low, pull-up esterno 100 kΩ |
| Buzzer | GPIO20 | PWM/LEDC |
| battery_state | GPIO4 | MCP73831 STAT via partitore, ADC1_CH4 |
| 3V3 | 3V3 | alimentazione logica |
| +5V | 5V | ingresso 5 V SuperMini |

GPIO20 coincide con U0RXD; la console di sviluppo usa USB Serial/JTAG.

`battery_state` è STAT, non Vbat. Con il partitore 100k/100k legge circa
0 V con STAT LOW e circa 2,5 V con STAT HIGH. Con il circuito attuale STAT LOW e
charger non alimentato non sono distinguibili senza un segnale VBUS separato.

## Configurazione e schema NVS

La schema version corrente è **2**. Il passaggio dai vecchi token a 8 bit ai
token v3 a 16 bit invalida intenzionalmente la configurazione NVS precedente:
al primo boot vengono caricati e salvati i nuovi default.

Il default contiene:

- A click -> POINT_A
- B click -> POINT_B
- Moment click -> MOMENT
- Undo click -> UNDO
- A+B simultanei -> UNDO
- Undo long -> pairing

## ACK autorevole

Ogni comando usa `(session_id, sequence)`. L'app/server deduplica, applica
l'azione allo stato partita corrente e restituisce lo snapshot dopo
l'elaborazione più i flag GAME/SET/MATCH.

L'ESP non mantiene un secondo motore di scoring.

## Diagnostica

La diagnostica firmware usa ESP-IDF logging sulla console seriale USB:

- `buttons`: transizioni, primitive, chord
- `gesture`: token e mapping
- `ble_manager`: connessione, invii, retry, ACK, config
- `config_store`: NVS
- `wearable_app`: action e stato autorevole
- `power`: charger/batteria

Gli eventi importanti sono a livello INFO/WARN/ERROR; i dettagli rumorosi sono
DEBUG.

La pagina BLE non visualizza un pannello diagnostico: scrive i dettagli in
console, così l'interfaccia resta pulita.

## Pairing

Il token applicativo da 128 bit viene persistito in NVS. Il pairing fisico
cancella prima la persistenza, notifica il vecchio client e poi disconnette.
L'auto-reconnect del vecchio browser viene sospeso per evitare che reclami subito
il wearable.

## Robustezza

- configurazione validata prima di NVS;
- mapping compatti e gesture duplicate rifiutate;
- almeno una gesture di pairing obbligatoria;
- queue BLE limitata + backoff + massimo tentativi;
- TEMPORARY_ERROR conserva stesso ID;
- sound feedback ACTION + GAME/SET/MATCH atomico;
- parametri runtime protetti tra task/callback.
