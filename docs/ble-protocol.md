# BLE protocol — Playmaker Wearable

## Principio

Il wearable è un terminale di input e feedback. L'autorità sul punteggio è
l'app/server Playmaker. In particolare, l'ESP32 non deve decidere se un punto
chiude un game, un set o il match.

Protocol version: **2**.

## ACTION — wearable -> app

12 byte:

| Offset | Dimensione | Campo |
| ---: | ---: | --- |
| 0 | 1 | protocol version |
| 1 | 1 | message type = ACTION |
| 2 | 4 | session_id, little-endian |
| 6 | 4 | sequence, little-endian |
| 10 | 1 | action |
| 11 | 1 | reserved |

`session_id + sequence` è l'identificatore idempotente del comando.

## ACK — app -> wearable

20 byte, quindi entra anche nel payload ATT minimo senza richiedere un MTU
maggiore:

| Offset | Dimensione | Campo |
| ---: | ---: | --- |
| 0 | 1 | protocol version |
| 1 | 1 | message type = ACK |
| 2 | 1 | status |
| 3 | 1 | transition flags |
| 4 | 4 | session_id |
| 8 | 4 | sequence |
| 12 | 2 | match revision |
| 14 | 1 | points A |
| 15 | 1 | points B |
| 16 | 1 | games A |
| 17 | 1 | games B |
| 18 | 1 | sets A |
| 19 | 1 | sets B |

Status:

- `0 = OK`
- `1 = REJECTED`
- `2 = TEMPORARY_ERROR`

Transition flags:

- bit 0: `GAME_ENDED`
- bit 1: `SET_ENDED`
- bit 2: `MATCH_ENDED`

Lo snapshot è sempre lo stato **dopo** l'elaborazione del comando.

## Idempotenza

La web app deve conservare l'esito di ogni `(session_id, sequence)` almeno per
la durata della sessione. Se riceve lo stesso comando una seconda volta perché
l'ACK precedente è andato perso, **non applica di nuovo il punto**: restituisce
lo stesso ACK già prodotto, inclusi snapshot e transition flags.

## Modifiche fatte dall'app

Le modifiche fatte direttamente dal telefono vengono applicate al motore
autorevole prima dei comandi successivi del wearable.

Esempio:

1. dall'app l'utente porta A a 40;
2. il wearable invia `POINT_A`;
3. il motore applica il punto allo stato corrente e chiude il game;
4. l'ACK torna con il nuovo score e `GAME_ENDED`;
5. il wearable suona conferma + melodia game.

Il wearable non confronta localmente 40 -> game e non mantiene un secondo
motore di scoring.

## Priorità feedback

Su ACK `OK`:

1. suono di conferma dell'azione;
2. `MATCH_ENDED`, se presente;
3. altrimenti `SET_ENDED`;
4. altrimenti `GAME_ENDED`.

Su `REJECTED` il comando è terminale: viene rimosso dalla coda e il wearable
riproduce il suono di errore.

Su `TEMPORARY_ERROR` il comando **resta in coda con lo stesso
`session_id + sequence`** e viene ritentato. Il wearable non crea un nuovo ID,
quindi un problema temporaneo non può trasformarsi in un doppio punto. Dopo un
numero massimo di tentativi il comando viene abbandonato e viene emesso il
feedback di errore.

## Pagina web-wearable

La pagina `web-wearable` è una pagina di configurazione/collaudo, non il motore
partita. Per i test invia ACK v2 con stato neutro. L'integrazione Playmaker reale
deve sostituire quel mock con lo stato restituito dal motore partita.
