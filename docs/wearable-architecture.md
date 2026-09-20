# Playmaker wearable — ESP32-C3

Firmware dedicato al wearable Playmaker. Stack: ESP-IDF + NimBLE.

## Architettura

- **button_manager**: debounce, raggruppamento simultaneo di 1–4 pulsanti e classificazione click/double/triple/long.
- **gesture_engine**: sequenze configurabili con risoluzione dei prefissi.
- **config_model**: default e validazione indipendenti da ESP-IDF.
- **config_store**: persistenza NVS.
- **config_runtime**: owner sincronizzato della configurazione condivisa da BLE
  e SoftAP; valida, salva in NVS e poi pubblica il nuovo stato.
- **config_ap**: SoftAP Wi-Fi aperta + HTTP server locale per la configurazione.
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

### Gruppi simultanei

Il primo press stabile apre una finestra configurabile, default 60 ms. Tutti i
pulsanti che iniziano la pressione dentro la finestra formano un gruppo. Il
gruppo viene poi classificato come CLICK, DOUBLE, TRIPLE o LONG.

Il LONG scatta appena supera la soglia mentre tutti i pulsanti del gruppo sono
ancora tenuti premuti; non è necessario rilasciarli.

Esempi validi: `A+B CLICK`, `A+B DOUBLE`, `A+B+Moment LONG`.

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

La schema version corrente è **3**. Il passaggio dai vecchi token a 8 bit ai
token v3 a 16 bit invalida intenzionalmente la configurazione NVS precedente:
al primo boot vengono caricati e salvati i nuovi default.

Il default contiene:

- A click -> POINT_A
- B click -> POINT_B
- Moment click -> MOMENT
- Undo click -> UNDO
- A+B click -> UNDO
- A+B doppio click -> POINT_A
- A+B+Moment long -> pairing

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


## SoftAP locale

All'avvio viene creata una rete:

```text
SSID: wearable_config_eps32_c3
auth: OPEN
IP ESP32-C3: 192.168.4.1
HTTP: http://192.168.4.1/
```

La pagina usa `GET /api/config` per leggere il pacchetto config v3 e
`POST /api/config` per inviare gli stessi comandi binari usati dalla
caratteristica BLE CONFIG. `GET /api/status` espone solo stato diagnostico
essenziale. I dettagli rimangono nella console browser con prefisso
`[Playmaker][AP]`.

Wi-Fi e BLE possono essere attivi nello stesso momento; la configurazione è
serializzata da `config_runtime` per evitare race tra scritture HTTP e GATT.
