# Playmaker Wearable — ESP32-C3

Firmware ESP-IDF per il telecomando/wearable Playmaker. Questo branch non è un
segnapunti autonomo: non contiene display, UI locale o motore delle regole del
padel. Il punteggio autorevole vive nell'applicazione Playmaker.

## Funzioni

- 4 pulsanti fisici: A, B, Moment, Undo
- gesture configurabili: click, doppio click, triplo click, pressione lunga
- sequenze di gesture configurabili e persistenti in NVS
- mapping gesture -> POINT_A / POINT_B / MOMENT / UNDO / pairing
- BLE NimBLE con commissioning persistente e riconnessione lato Web Bluetooth
- coda comandi con `session_id + sequence`, retry e ACK idempotenti
- ACK con stato partita autorevole e transizioni GAME/SET/MATCH
- buzzer con suoni azione configurabili e melodie di sistema
- gestione MCP73831 STAT via ADC
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

## Struttura

```text
main/
  app/          orchestrazione firmware
  core/         tipi condivisi
  input/        pulsanti + gesture engine
  link/         BLE + protocollo
  feedback/     buzzer / sound manager
  power/        charger / battery / power
  storage/      NVS e configurazione
web-wearable/   pagina di configurazione Web Bluetooth
docs/           architettura e protocollo
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
