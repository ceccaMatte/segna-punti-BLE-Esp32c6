# Playmaker Wearable — ESP32-C3

Firmware ESP-IDF per il telecomando/wearable Playmaker. Questo branch non è un
segnapunti autonomo: non contiene display, UI locale o motore delle regole del
padel. Il punteggio autorevole vive nell'applicazione Playmaker.

## Funzioni

- 4 pulsanti fisici: A, B, Moment, Undo
- gesture configurabili: click, doppio click, triplo click, pressione lunga e
  **pressione simultanea (chord)**
- sequenze di gesture configurabili e persistenti in NVS
- mapping gesture -> POINT_A / POINT_B / MOMENT / UNDO / pairing
- mapping di default aggiuntivo: **A+B simultanei -> UNDO**
- BLE NimBLE con commissioning persistente e riconnessione lato Web Bluetooth
- coda comandi con `session_id + sequence`, retry e ACK idempotenti
- ACK con stato partita autorevole e transizioni GAME/SET/MATCH
- buzzer con suoni azione configurabili e melodie di sistema
- gestione MCP73831 STAT via ADC
- logging seriale strutturato tramite ESP-IDF e diagnostica lato browser in
  `console.debug` / `console.error`
- struttura pronta per low-power / wake da pulsante

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

## Gesture simultanee

Il Button Manager apre una finestra configurabile (default **60 ms**) quando
rileva il primo pulsante stabile. Se almeno due pulsanti rimangono premuti entro
quella finestra genera un singolo token `CHORD` con una bitmask dei pulsanti.

La chord **consuma** le gesture individuali coinvolte: A+B non può generare
prima `POINT_A` e `POINT_B` e poi `UNDO`.

Il protocollo/configurazione usa token gesture a 16 bit:

```text
bits 0..3  button mask: A B Moment Undo
bits 4..6  tipo: click / double / triple / long / chord
bits 7..15 riservati
```

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

## Hardware attuale

- BTN_A: GPIO3
- BTN_B: GPIO1
- BTN_Moment: GPIO0
- BTN_UNDO: GPIO5
- buzzer: GPIO20
- MCP73831 `battery_state` / STAT divider: GPIO4 / ADC1_CH4

Vedi `docs/wearable-architecture.md` per i dettagli hardware e
`docs/ble-protocol.md` per il contratto con l'app.

## Verifica rapida

La parte pura del firmware (modello configurazione + protocollo binario) ha test
host-side che non richiedono ESP-IDF:

```sh
./test/host/run.sh
```

I test coprono token singoli/chord, configurazione di default, protezione della
gesture di pairing, serializzazione config, ACTION, ACK v3 e timing chord.

La build completa ESP-IDF va comunque eseguita prima del flash sulla scheda,
perché BLE/ADC/GPIO dipendono dalla versione di ESP-IDF installata.
