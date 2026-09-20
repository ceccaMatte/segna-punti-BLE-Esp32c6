# Diagnostica bring-up

La diagnostica è pensata per il primo flash e per i test end-to-end, senza
aggiungere elementi visibili all'utente finale.

## Console ESP32-C3

Usare il monitor ESP-IDF sulla USB Serial/JTAG. Una sequenza normale produce
eventi come:

```text
I buttons: button=0 gpio=3 initial=released
D buttons: button=0 stable=pressed
D buttons: emit primitive=click mask=0x01 token=0x0001
I gesture: match mapping=0 action=0
I wearable_app: action requested=0
D ble_manager: action sent seq=14 attempt=1
I ble_manager: ACK rx ... seq=14 ... rev=31 ...
```

Per una chord A+B:

```text
D buttons: chord window open button=0 window=60 ms
D buttons: chord candidate mask=0x03
I buttons: chord recognized mask=0x03
D buttons: emit primitive=chord mask=0x03 token=0x0043
I gesture: match mapping=4 action=3
```

## Console browser

Aprire DevTools sulla pagina `web-wearable`. Tutto il trasporto usa il
prefisso:

```text
[Playmaker][BLE]
```

Sono registrati connect/reconnect, status, autenticazione, GATT write, ACTION,
ACK, lettura/salvataggio configurazione e errori.

Un write CONFIG considerato riuscito significa che il firmware ha già validato
e committato la configurazione in NVS: il GATT handler persiste prima di
restituire successo e prima di aggiornare il runtime.

## Correlazione

Per problemi di scoring confrontare sempre:

- `session_id`
- `sequence`
- `match revision`

La stessa `sequence` deve apparire nel log invio firmware, nella console
browser/app e nel log ACK firmware. Un retry conserva la stessa chiave.
