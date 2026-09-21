# Playmaker Wearable — ESP32-C3

Firmware ESP-IDF per il telecomando/wearable Playmaker. Questo branch non è un
segnapunti autonomo: non contiene display, UI locale o motore delle regole del
padel. Il punteggio autorevole vive nell'applicazione Playmaker.

## Funzioni

- 4 pulsanti fisici: A, B, Moment, Undo
- gesture configurabili: click, doppio click, triplo click, pressione lunga e
  **pressione simultanea (chord)**
- sequenze di gesture configurabili e persistenti in NVS
- mapping gesture -> POINT_A / POINT_B / MOMENT / UNDO / VAR / pairing
- default: **Moment doppio click -> VAR** e **Undo long press -> pairing**
- BLE NimBLE con commissioning persistente e riconnessione lato Web Bluetooth
- coda comandi con `session_id + sequence`, retry e ACK idempotenti
- ACK con stato partita autorevole e transizioni GAME/SET/MATCH
- buzzer con suoni azione configurabili e melodie di sistema
- gestione MCP73831 STAT via ADC
- logging seriale strutturato tramite ESP-IDF e diagnostica lato browser in
  `console.debug` / `console.error`
- SoftAP Wi-Fi locale **aperta, senza password**, SSID
  `wearable_config_eps32_c3`, con pagina di configurazione su
  `http://192.168.4.1/`
- Deep-sleep automatico dopo **5 minuti senza pressioni**, con wake da uno
  qualsiasi dei quattro pulsanti

## Regola fondamentale del punteggio

L'ESP32-C3 **non calcola mai il punteggio**.

Quando il wearable invia, per esempio, `POINT_A`, l'app/server:

1. deduplica `(session_id, sequence)`;
2. applica il comando allo stato partita autorevole corrente;
3. produce un ACK con lo stato risultante;
4. aggiunge i flag di transizione `GAME_ENDED`, `SET_ENDED`,
   `MATCH_ENDED` se quel comando ha chiuso una di quelle unità.

Questo è importante perché il punteggio può essere modificato anche direttamente
dall'app. Il wearable non deve avere una copia delle regole che può divergere.

Se un punto del wearable chiude il game, l'ACK contiene `GAME_ENDED` e il
wearable riproduce la melodia di game. Se chiude contemporaneamente un set o il
match, set/match hanno priorità sonora.

## Gesture programmabili

Ogni step è rappresentato da **mask pulsanti + tipo di pressione**. La stessa
logica vale per uno o più pulsanti:

```text
A + CLICK                  -> POINT_A
A+B + CLICK                -> UNDO
A+B + DOUBLE               -> POINT_A
MOMENT + DOUBLE            -> VAR
UNDO + LONG                 -> PAIRING
```

La finestra `simultaneous_window_ms` (default 60 ms) indica quanto possono
essere sfalsati i press iniziali per appartenere allo stesso gruppo. Lo stesso
gruppo può poi essere classificato come click, doppio, triplo o long press.

Il protocollo usa token a 16 bit: bit 0..3 = mask pulsanti, bit 4..5 = tipo
(click/double/triple/long). Non esiste più una primitive CHORD separata.

## Diagnostica

Sul firmware i tag principali sono:

- `buttons`: press/release, primitive e chord
- `gesture`: token, sequenze e mapping riconosciuto
- `wearable_app`: azione semantica e stato autorevole ricevuto
- `ble_manager`: connessione, invio, retry, ACK e configurazione
- `config_store`: load/save NVS e pairing token
- `power`: stato charger e low battery

La pagina `web-wearable` mantiene l'interfaccia pulita. I dettagli di
connessione, GATT, ACTION, ACK, configurazione e retry sono nella console del
browser con prefisso `[Playmaker][BLE]`.

## Struttura

```text
main/
  app/          orchestrazione firmware
  core/         tipi + modello configurazione
  input/        pulsanti + gesture engine
  link/         BLE + protocollo
  feedback/     buzzer / sound manager
  power/        charger / battery / power
  storage/      NVS
web-wearable/   pagina di configurazione Web Bluetooth
docs/           architettura e protocollo
test/host/      test della parte C indipendente da ESP-IDF
```

Il vecchio codice ESP32-C6 relativo a display, motore punteggio, UI e scoreboard
è stato intenzionalmente rimosso da questo branch.

## Hardware attuale — prototipo volante

- BTN_B: **GPIO0**, input con pull-up interno, active-low
- BTN_Moment: **GPIO1**, input con pull-up interno, active-low
- BTN_UNDO: **GPIO2**, input con pull-up interno, active-low
- BTN_A: **GPIO3**, input con pull-up interno, active-low
- GPIO20: **input**
- GPIO21: mantenuto **LOW** come ritorno di massa logica del prototipo
- buzzer: **GPIO5** riservato; sul prototipo attuale il buzzer non è montato
- MCP73831 `battery_state` / STAT divider: GPIO4 / ADC1_CH4

GPIO21 viene usato come sink LOW solo per segnali a corrente molto bassa del
prototipo; non va trattato come una vera massa di potenza.

Il pinout del prototipo è definito in `main/hardware/board_pins.h` e non più
nei default Kconfig. Questo evita che un vecchio file `sdkconfig` locale
ripristini silenziosamente un pinout precedente. Al boot il tag `buttons`
stampa sempre la mappa effettiva e il livello iniziale dei quattro ingressi.

Vedi `docs/wearable-architecture.md` per i dettagli hardware e
`docs/ble-protocol.md` per il contratto con l'app.

## Verifica rapida

La parte pura del firmware (modello configurazione + protocollo binario) ha test
host-side che non richiedono ESP-IDF:

```sh
./test/host/run.sh
```

I test coprono token a maschera multi-pulsante, configurazione di default, protezione della
gesture di pairing, serializzazione config, ACTION, ACK v3 e timing chord.

La build completa ESP-IDF va comunque eseguita prima del flash sulla scheda,
perché BLE/ADC/GPIO dipendono dalla versione di ESP-IDF installata.


## SoftAP di configurazione

Il firmware avvia a boot una rete Wi-Fi SoftAP con:

```text
SSID: wearable_config_eps32_c3
Password: nessuna
URL: http://192.168.4.1/
```

BLE e Wi-Fi restano attivi contemporaneamente. La pagina SoftAP modifica la
stessa configurazione runtime/NVS usata da BLE; le due interfacce passano da un
unico `config_runtime` con mutex, validazione e commit NVS prima della
pubblicazione.

La rete è volutamente aperta: chiunque sia nel raggio radio e si colleghi può
modificare la configurazione del wearable.


## Deep-sleep / autonomia

Dopo `300000 ms` (5 minuti) senza alcuna pressione fisica il wearable entra
in **Deep-sleep**. Il timeout viene azzerato alla pressione debounced di
qualunque pulsante.

I quattro pulsanti del prototipo sono tutti su GPIO0..3, quindi sono utilizzabili
come sorgenti di wake da Deep-sleep sull'ESP32-C3. Il wake è level-low perché i
pulsanti sono active-low.

Il prototipo volante usa GPIO21 come ritorno LOW dei pulsanti: prima del
Deep-sleep il firmware blocca GPIO21 a LOW con il pad hold, altrimenti il pin
diventerebbe high-impedance e i pulsanti non potrebbero svegliare il chip.

Il primo pulsante premuto dopo i 5 minuti **sveglia il dispositivo**; il wake da
Deep-sleep riavvia il firmware e BLE/SoftAP vengono quindi ricreati e
riconnessi normalmente.

Nel monitor seriale i passaggi sono evidenziati dai banner
`ENTERING DEEP SLEEP` e `WAKE FROM DEEP SLEEP`, incluso il GPIO/pulsante che
ha causato il wake.


## Pairing vergine / cambio dispositivo

La gesture di pairing non è più un semplice "riapri pairing": esegue un vero
**reset dell'associazione applicativa**.

Quando viene lanciata:

- cancella da NVS il token del precedente proprietario;
- cancella il token anche dalla RAM;
- azzera autenticazione e commissioning;
- scarta eventuali ACTION ancora in coda e crea una nuova sessione;
- disconnette forzatamente il client BLE corrente;
- apre la finestra di pairing per un nuovo dispositivo.

Durante questa fase un client che si connette ma non invia un nuovo `CLAIM`
entro 5 secondi viene disconnesso, così un vecchio computer in auto-reconnect non
può occupare indefinitamente l'unico slot BLE.

La pagina Web Bluetooth, quando vede il wearable non commissioned con pairing
aperto, cancella il proprio token locale e blocca l'auto-reconnect. Solo il
pulsante esplicito **Connetti / Pair** può generare un nuovo token casuale e
diventare il nuovo proprietario.

Il firmware attuale usa un token applicativo Playmaker e non abilita il bonding
SMP persistente di NimBLE; quindi non ci sono ulteriori chiavi BLE di sistema da
cancellare sul wearable.
