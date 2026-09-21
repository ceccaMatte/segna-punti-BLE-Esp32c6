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

## Pinout prototipo volante attuale

| Funzione | ESP32-C3 GPIO | Configurazione |
| --- | ---: | --- |
| BTN_B | GPIO0 | input, pull-up interno, active-low |
| BTN_Moment | GPIO1 | input, pull-up interno, active-low |
| BTN_UNDO | GPIO2 | input, pull-up interno, active-low |
| BTN_A | GPIO3 | input, pull-up interno, active-low |
| battery_state | GPIO4 | MCP73831 STAT via partitore, ADC1_CH4 |
| Buzzer | GPIO5 | riservato; non montato sul prototipo |
| AUX | GPIO20 | input, nessun pull |
| ritorno logico prototipo | GPIO21 | output mantenuto LOW |

La console di sviluppo resta su USB Serial/JTAG. GPIO20/21 non vengono usati
dalla UART di debug. GPIO21 è usato come ritorno LOW solo per piccoli segnali
del prototipo e non come massa di alimentazione.

La mappa GPIO del prototipo è centralizzata in `hardware/board_pins.h`.
`button_manager`, `board_io` e `sound_manager` usano direttamente quel
profilo hardware, quindi un `sdkconfig` generato con un vecchio pinout non può
più cambiare i pulsanti. Il log RAW dei pulsanti è a livello INFO e riporta
anche GPIO e livello logico, utile in particolare per diagnosticare UNDO/GPIO3.

`battery_state` è STAT, non Vbat. Con il partitore 100k/100k legge circa
0 V con STAT LOW e circa 2,5 V con STAT HIGH. Con il circuito attuale STAT LOW e
charger non alimentato non sono distinguibili senza un segnale VBUS separato.

## Configurazione e schema NVS

La schema version corrente è **5**. Il passaggio dai vecchi token a 8 bit ai
token v3 a 16 bit invalida intenzionalmente la configurazione NVS precedente:
al primo boot vengono caricati e salvati i nuovi default.

Il default contiene:

- A click -> POINT_A
- B click -> POINT_B
- Moment click -> MOMENT
- Undo click -> UNDO
- A+B click -> UNDO
- A+B doppio click -> POINT_A
- Moment doppio click -> VAR
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

Il token applicativo da 128 bit viene persistito in NVS.

La gesture fisica di pairing porta il wearable in **virgin pairing mode**:
cancella il token NVS/RAM precedente, azzera commissioning/autenticazione,
svuota la coda ACTION, genera una nuova sessione e disconnette il client
corrente. La finestra di pairing resta aperta per
`CONFIG_WEARABLE_PAIRING_WINDOW_MS`.

Per evitare che un vecchio browser in auto-reconnect monopolizzi l'unica
connessione BLE, ogni connessione non autenticata durante virgin pairing deve
inviare `CLAIM` entro 5 secondi; altrimenti viene terminata e l'advertising
riparte.

Sul client Web Bluetooth lo stato `commissioned=0 + pairingOpen=1` cancella
immediatamente il token locale e disabilita l'auto-reconnect. Un nuovo CLAIM è
permesso solo dopo un'azione esplicita **Connetti / Pair**, che genera sempre un
token nuovo invece di riutilizzare quello precedente.

Non è attivo il bonding SMP persistente NimBLE: l'associazione persistente è il
token applicativo Playmaker.

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


## Deep-sleep dopo inattività

Il modulo `power/sleep_manager` misura esclusivamente l'attività fisica dei
pulsanti. Ogni PRESS debounced azzera il timer. Con il default di
`CONFIG_WEARABLE_IDLE_SLEEP_MS=300000`, dopo 5 minuti entra in Deep-sleep.

Wake sources: i quattro pulsanti definiti in `hardware/board_pins.h`, con
trigger LOW. La mappa fisica non viene duplicata nel power manager: viene letta
dallo stesso board profile usato dal Button Manager.

Prima di dormire viene verificato che nessun pulsante sia già premuto; in tal
caso lo sleep viene rimandato per evitare un wake immediato. GPIO21, usato dal
prototipo come ritorno LOW dei pulsanti, viene mantenuto LOW tramite
`gpio_hold_en()` + `gpio_deep_sleep_hold_en()`.

Al wake `board_io` ripristina GPIO21 LOW e rimuove il hold. Il firmware poi
riparte normalmente, quindi le connessioni BLE e SoftAP vengono ricreate.

La console seriale stampa banner molto visibili sia all'ingresso sia all'uscita
dal Deep-sleep e, quando disponibile, la mask GPIO che ha causato il wake.
