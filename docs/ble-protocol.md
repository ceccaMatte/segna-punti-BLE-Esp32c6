# BLE protocol — Playmaker Wearable

## Principio

Il wearable è un terminale di input e feedback. L'autorità sul punteggio è
l'app/server Playmaker. L'ESP32 non decide mai se un punto chiude un game, un set
o il match.

Protocol version: **6**.

La v6 aggiunge i comandi di navigazione VAR `FAST_FORWARD_START`,
`FAST_REWIND_START`, `STOP` e la primitive `RELEASE` emessa dopo una
pressione lunga. ACTION e ACK mantengono la stessa dimensione binaria.

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

20 byte:

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

Se l'app riceve di nuovo lo stesso `(session_id, sequence)` perché un ACK è
andato perso, non applica nuovamente l'azione: restituisce lo stesso risultato.

Su `TEMPORARY_ERROR` il wearable mantiene lo stesso comando e lo stesso ID in
coda. `OK` e `REJECTED` sono terminali.

## Token gesture v6

Ogni step è un `uint16 little-endian`:

| Bit | Contenuto |
| --- | --- |
| 0..3 | button mask: A, B, Moment, Undo |
| 4..6 | primitive |
| 7..15 | reserved = 0 |

Primitive: 0 click, 1 double, 2 triple, 3 long, 4 release-after-long. La mask
può contenere da 1 a 4 pulsanti. `RELEASE` viene generato solo per un gruppo
che aveva già emesso `LONG`.

Esempi:

```text
A+B CLICK       mask=0x03 primitive=0
A+B DOUBLE      mask=0x03 primitive=1
A+B+M LONG      mask=0x07 primitive=3
A RELEASE        mask=0x01 primitive=4
```

## CONFIG read

Header da 11 byte:

| Offset | Dimensione | Campo |
| ---: | ---: | --- |
| 0 | 1 | protocol version |
| 1 | 1 | message type = CONFIG |
| 2 | 1 | mapping count |
| 3 | 2 | multi-click gap ms |
| 5 | 2 | long press ms |
| 7 | 2 | sequence gap ms |
| 9 | 2 | simultaneous window ms |

Segue ogni mapping, 18 byte:

- action: 1 byte
- sequence length: 1 byte
- 8 token × 2 byte little-endian

Infine 5 sound descriptor da 6 byte: frequency, duty-permille, duration
(POINT_A, POINT_B, MOMENT, UNDO, VAR). I comandi fast/stop non hanno tono
configurabile dedicato.

## CONFIG write

- `0x10 SET_MAPPING`: 20 byte totali; index, action, length, 8 token uint16
- `0x11 DELETE_MAPPING`: 2 byte
- `0x12 SET_ACTION_SOUND`: 8 byte
- `0x13 SET_TIMINGS`: 9 byte, inclusa chord window
- `0x14 RESET_DEFAULTS`: 1 byte

Ogni write viene prima validata, poi salvata in NVS e solo dopo pubblicata alla
configurazione runtime.

## Esempi configurabili

`A+B click -> UNDO`, `A+B double -> POINT_A` e
`A+B+Moment long -> PAIRING` sono mapping normali, senza eccezioni hardcoded.

## Modifiche fatte dall'app

Se l'utente cambia il punteggio dal telefono, il successivo ACTION del wearable
viene applicato a quello stato aggiornato. Se chiude un game, l'ACK contiene
`GAME_ENDED` e il wearable suona la relativa melodia.

## Pagina web-wearable

È una pagina di configurazione/collaudo, non il motore partita. Durante i test
risponde agli ACTION con ACK neutro. Tutti gli eventi di trasporto sono
tracciati nella console del browser con prefisso `[Playmaker][BLE]`.


## Action values v6

```text
0  POINT_A
1  POINT_B
2  MOMENT
3  UNDO
4  VAR
5  FAST_FORWARD_START
6  FAST_REWIND_START
7  STOP
8  ENTER_PAIRING   (solo interno: non viene inviato come ACTION)
```

Default navigazione:

```text
B LONG     -> FAST_FORWARD_START
B RELEASE  -> STOP
A LONG     -> FAST_REWIND_START
A RELEASE  -> STOP
```

Il LONG viene emesso appena la soglia viene superata mentre il pulsante è ancora
premuto. Il RELEASE successivo è un secondo evento indipendente, quindi il
computer riceve prima START e poi STOP.
