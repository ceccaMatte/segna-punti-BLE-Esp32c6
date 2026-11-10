# Playmaker Receiver — contratto USB API v1

Stato: **contratto implementato nel progetto `receiver/`**, con discovery sul
wearable. Entrambe le schede richiedono il firmware aggiornato. Il collaudo con
due schede fisiche e il programma Ubuntu resta necessario.
Il software Ubuntu Server sarà sviluppato dal socio, sulla base del contratto qui definito.

## 1. Responsabilità e modalità di rete

```text
Modalità diretta
Radiocomando -- Wi-Fi --> AP dell'ESP ricevitore -- USB --> Ubuntu Server

Modalità LAN
Radiocomando -- Wi-Fi --> router <-- Wi-Fi -- ESP ricevitore -- USB --> Ubuntu Server
```

Internet non è necessario. Il PC deve avere soltanto una porta USB.
La stessa API USB vale in entrambe le modalità: il programma Ubuntu non gestisce
SSID, password, indirizzi IP o pacchetti radio.

| Componente | Responsabilità |
| --- | --- |
| Wearable | Riconoscere pulsanti/gesture, creare un ID per comando, aspettare l'ACK e limitare gli invii |
| Ricevitore | Collegarsi al wearable, inoltrare azioni su USB, inoltrare l'ACK del PC al wearable |
| Programma Ubuntu | Applicare le regole della partita, conservare punteggio e deduplicazione, restituire l'ACK con il risultato |

Il ricevitore **non calcola il punteggio**. `.A` significa aggiungere un punto ad A,
non è il valore finale del punteggio. Se il programma Ubuntu espone poi API HTTP,
database o un tabellone, quelle interfacce saranno definite dal suo autore.

### Diretta

- Il ricevitore crea `PLAYMAKER_RX_XXXX`, password iniziale `playmaker`.
- Usa la sottorete `192.168.8.0/24`, indirizzo `192.168.8.1`, con DHCP attivo.
  Questa rete è diversa dall'AP di configurazione del wearable (`192.168.4.1`).
- Dalla pagina di configurazione del wearable si seleziona una volta la rete del
  ricevitore; le credenziali restano salvate e vengono riutilizzate agli avvii successivi.
- Il ricevitore individua il wearable sulla propria rete e ne legge le azioni HTTP.

### LAN

- Da telefono o altro dispositivo si configura ciascuna scheda tramite il suo AP.
- Entrambe le schede si collegano allo stesso router Wi-Fi a 2,4 GHz.
- Ogni scheda conserva l'ultima rete configurata con successo. Resta il fallback
  `Se` / `ciao1234`; il fallback non sovrascrive la rete preferita salvata.
- Il ricevitore trova il wearable con il discovery descritto nella sezione 8.
- Reti ospiti con isolamento fra client possono impedire la comunicazione.

La modalità si seleziona dalla pagina locale del ricevitore. In LAN, l'AP del
ricevitore rimane disponibile per la configurazione. Il profilo diretto, invece,
usa il proprio AP come rete operativa del wearable e non richiede un router.
Il ricevitore non deve cambiare autonomamente modalità durante una partita.

## 2. Porta USB e framing

- USB nativa ESP32-C3, classe seriale CDC/USB Serial-JTAG.
- Ubuntu espone normalmente `/dev/ttyACM0`; scegliere il percorso persistente
  `/dev/serial/by-id/...`, perché il numero ACM può cambiare.
- Aprire la porta in modalità raw, 115200 baud, 8 bit, nessuna parità, un bit di
  stop, senza controllo di flusso, echo o conversioni dei fine riga.
- UTF-8; un oggetto JSON compatto per riga (**JSON Lines**), terminato da `LF`.
- Massimo **1024 byte per riga**, incluso `LF`. Il mittente non inserisce newline
  all'interno di un oggetto. Il parser può tollerare `CRLF`.
- Una lettura USB può restituire una parte di riga o più righe: accumulare fino a
  `LF`, poi analizzare ciascun oggetto separatamente.
- Una riga troppo lunga va scartata fino al successivo `LF`, senza applicare azioni.
- Ogni messaggio contiene `v:1` e `type`. Campi aggiuntivi sconosciuti si ignorano;
  i campi obbligatori mancanti, i tipi errati e i valori fuori intervallo sono errori.
- Un solo programma Ubuntu apre la porta. L'utente del servizio deve avere
  accesso al dispositivo seriale; il programma non deve dipendere da una GUI.

Nessun log testuale deve essere mescolato agli eventi nel firmware ricevitore.
I messaggi ROM/boot eventualmente presenti prima dell'handshake non sono eventi:
il programma li ignora e aspetta un `hello` valido. Dopo l'handshake, eventuali
diagnostiche usano messaggi JSON `error`, non righe arbitrarie.

Le righe nude `.A` o `.B` **non** sono il protocollo completo: il campo `command`
mantiene questi nomi, ma l'involucro JSON contiene gli ID indispensabili per ACK
e deduplicazione.

## 3. Apertura della sessione USB

Appena apre la porta, il PC invia `hello`. Il ricevitore risponde ogni volta,
anche se è già stato avviato prima del programma Ubuntu.

PC → ricevitore:

```json
{"v":1,"type":"hello"}
```

Ricevitore → PC:

```json
{"v":1,"type":"hello","receiver_id":"ac276e45e004","boot_id":"7c901a21","mode":"direct","max_attempts":3,"ack_timeout_ms":1000}
```

`receiver_id` è l'identificativo stabile della scheda, MAC derivato Bluetooth di 12 caratteri
esadecimali minuscoli senza separatori. `boot_id` identifica un avvio del
ricevitore; è una stringa di 8 caratteri esadecimali, non l'ID di un comando.
`mode` è `direct` oppure `lan`.

Il PC risponde quando è pronto a leggere e salvare gli eventi:

```json
{"v":1,"type":"ready"}
```

Solo dopo `ready` il ricevitore può acquisire la sessione HTTP del wearable e
richiedere azioni. La presenza del ricevitore USB da sola non deve far credere al
wearable che un programma sul PC stia confermando i punti.

## 4. Evento del radiocomando

Ricevitore → PC:

```json
{"v":1,"type":"action","wearable_id":"ac276e45e006","session_id":305419896,"sequence":42,"action":0,"command":".A","attempt":1}
```

| Campo | Tipo e significato |
| --- | --- |
| `wearable_id` | Identificativo stabile del wearable, 12 cifre esadecimali minuscole |
| `session_id` | Intero senza segno 32 bit, da 1 a 4294967295; sessione di trasporto del wearable |
| `sequence` | Intero senza segno 32 bit, da 1 a 4294967295; numero del comando nella sessione |
| `action` | Codice numerico nella tabella seguente |
| `command` | Nome leggibile, deve corrispondere al codice `action` |
| `attempt` | Intero da 1 a 3; numero di trasmissione dello stesso comando |

L'identità univoca ai fini applicativi è la terna
**`(wearable_id, session_id, sequence)`**. Non usare solo `sequence`, `attempt`,
`receiver_id`, orari del PC o punteggio per identificare un comando.

| `action` | `command` | Effetto richiesto al PC |
| --- | --- | --- |
| 0 | `.A` | Aggiunge un punto alla squadra A |
| 1 | `.B` | Aggiunge un punto alla squadra B |
| 2 | `.MOMENTO` | Segna il momento |
| 3 | `.UNDO` | Annulla l'ultima azione di punteggio secondo la cronologia del PC |
| 4 | `.VAR` | Avvia la funzione VAR dell'applicazione |
| 5 | `.AVANTI_START` | Avvia avanzamento veloce |
| 6 | `.INDIETRO_START` | Avvia riavvolgimento veloce |
| 7 | `.STOP` | Ferma avanzamento/riavvolgimento |

Il comando interno 8 (`ENTER_PAIRING` / configurazione Wi-Fi) non viene inoltrato
al PC. Le gesture vengono interpretate dal wearable: il programma Ubuntu non
deve ricostruire click, doppi click o pressioni lunghe dai tempi di arrivo USB.

## 5. ACK e punteggio

Il PC applica il comando, salva atomicamente il risultato e il record di
deduplicazione, quindi invia l'ACK. Non inviare `ok` soltanto perché la riga USB è
stata letta o messa in una coda di lavoro.

PC → ricevitore:

```json
{"v":1,"type":"ack","wearable_id":"ac276e45e006","session_id":305419896,"sequence":42,"status":"ok","transition_flags":0,"score":{"revision":7,"points_a":15,"points_b":0,"games_a":0,"games_b":0,"sets_a":0,"sets_b":0}}
```

Tutti i campi dell'esempio sono obbligatori. L'ACK ripete gli identificativi
dell'evento; non ne crea di nuovi e non contiene `attempt`.

| `status` | Codice HTTP/binary bridge | Comportamento |
| --- | --- | --- |
| `ok` | status ACK v6 = 0 | Azione applicata e salvata; conferma definitiva |
| `rejected` | status ACK v6 = 1 | Azione definitivamente rifiutata, ad esempio Undo senza cronologia; nessun retry |
| `temporary_error` | status ACK v6 = 2 | Errore transitorio; l'azione può essere riproposta entro il budget residuo, senza azzerare i tentativi |

Per `rejected` e `temporary_error` il PC restituisce lo stato corrente, senza
applicare modifiche parziali, e `transition_flags:0`. Per Momento, VAR e comandi
video l'ACK contiene comunque il punteggio corrente; la revisione può restare
invariata se il punteggio non cambia.

### Campi `score`

| Campo | Valori |
| --- | --- |
| `revision` | Intero 0…65535; aumenta per ogni modifica di punteggio, incluso Undo; wrap modulo 65536 |
| `points_a`, `points_b` | Interi 0…255; nei game normali `0`, `15`, `30`, `40`, `50` (`50` significa vantaggio); nel tie-break, punti numerici |
| `games_a`, `games_b` | Interi 0…255, game del set corrente |
| `sets_a`, `sets_b` | Interi 0…255, set vinti |

Questa snapshot è compatibile con l'ACK binario v6 attuale. Non contiene il
formato della partita, il flag tie-break, chi serve o la cronologia dei set:
il programma Ubuntu conserva queste informazioni nel proprio modello di partita.
Il solo oggetto `score` non basta per ricostruire integralmente una partita dopo
la perdita del database del PC.

`transition_flags` è un intero 0…7, OR dei seguenti bit:

| Bit | Valore | Transizione causata dal comando |
| --- | --- | --- |
| 0 | 1 | Game terminato |
| 1 | 2 | Set terminato |
| 2 | 4 | Partita terminata |

Per esempio `3` indica game e set terminati. La snapshot contiene sempre il
risultato **dopo** il comando, comprese eventuali ripartenze da zero dei punti o
dei game. È il PC a scegliere correttamente i flag secondo le proprie regole.

### Conferma dell'inoltro

Il ricevitore conferma separatamente che il wearable ha accettato l'ACK:

```json
{"v":1,"type":"ack_result","wearable_id":"ac276e45e006","session_id":305419896,"sequence":42,"result":"forwarded"}
```

`result` è `forwarded`, `expired` oppure `link_error`. `forwarded` significa che
`POST /api/ack` ha risposto con successo, non certifica fisicamente il suono del
buzzer. Il PC non aspetta `ack_result` prima di salvare il punteggio e non applica
nuovamente il comando se la conferma USB viene persa.

## 6. Retry, timeout e deduplicazione

1. Un comando può essere trasmesso al massimo **tre volte in totale**, con lo
   stesso ID e `attempt` pari a 1, 2, 3. Non sono tre retry oltre al primo invio.
2. L'intervallo di attesa ACK è un secondo. Il programma Ubuntu dovrebbe rispondere
   entro **500 ms**, lasciando tempo al ricevitore di inoltrare l'ACK via Wi-Fi.
3. Il ricevitore non aggiunge un proprio ciclo di tre retry: segue il budget del
   wearable, evitando di trasformare tre tentativi in nove.
4. Se arriva un duplicato già applicato, il PC restituisce l'ACK salvato per
   quell'ID, compresa la snapshot originale. Non aggiunge un altro punto, non
   esegue di nuovo VAR/Momento e non usa il punteggio corrente per ricreare l'ACK.
5. Dopo l'ultimo timeout il wearable scarta il comando. Il ricevitore non lo
   ripropone alla successiva riconnessione USB; una nuova pressione crea un nuovo ID.
6. Un ACK tardivo ottiene `ack_result:expired`. Non deve confermare il comando
   successivo. `temporary_error` non riapre un comando già scaduto.
7. Se un comando è stato applicato dal PC ma tutti gli ACK sono andati persi,
   il wearable può segnalare errore mentre il punto sul PC è già presente.
   Non esiste una garanzia di consegna esattamente una volta con ACK persi e
   nuovi ID: prima di ripremere, il giocatore deve verificare il tabellone.

Il firmware wearable attuale scade comunque un comando in testa dopo tre secondi
se il controller non lo legge. Non è garantito che ci siano tre consegne effettive
durante un'interruzione di rete. Il ricevitore non deve azzerare questo timeout.

Per rilevare il processo Ubuntu bloccato, dopo `ready` il PC invia `ping` ogni
secondo e il ricevitore risponde `pong`. Senza alcun messaggio PC valido per tre
secondi, o alla disconnessione USB rilevata, il ricevitore sospende il polling e
richiede nuovamente l'handshake `hello`/`ready`. Non invia ACK di successo autonomi.

```json
{"v":1,"type":"ping","nonce":123}
{"v":1,"type":"pong","nonce":123}
```

`nonce` è un intero 0…4294967295, restituito invariato. La prima riga è PC →
ricevitore, la seconda ricevitore → PC. Un heartbeat non è un ACK di un'azione.

## 7. Stato ed errori

Richiesta PC:

```json
{"v":1,"type":"get_status"}
```

Risposta ricevitore:

```json
{"v":1,"type":"status","receiver_id":"ac276e45e004","boot_id":"7c901a21","mode":"lan","wifi_connected":true,"ip":"192.168.1.50","pc_ready":true,"wearable_connected":true,"wearable_id":"ac276e45e006","wearable_ip":"192.168.1.51","last_confirmed_score":{"revision":7,"points_a":15,"points_b":0,"games_a":0,"games_b":0,"sets_a":0,"sets_b":0}}
```

`last_confirmed_score` è la snapshot dell'ultimo ACK `ok` inoltrato con successo,
oppure `null` se non disponibile (per esempio dopo riavvio). È una cache in RAM,
non un database della partita. Gli indirizzi IP non disponibili sono `null`;
`wearable_id` è `null` finché non è stato selezionato un wearable.
`wearable_connected` indica l'ultima verifica HTTP riuscita entro tre secondi,
non la sola visibilità dell'SSID. Il firmware emette `status` anche ai cambiamenti
di connettività. Nessuna risposta contiene password o credenziali Wi-Fi.

Errori di protocollo hanno questa forma:

```json
{"v":1,"type":"error","code":"INVALID_MESSAGE","message":"Campo sequence mancante"}
```

Codici definiti: `INVALID_MESSAGE`, `UNSUPPORTED_VERSION`, `LINE_TOO_LONG`,
`NOT_READY`, `UNKNOWN_COMMAND`, `UNKNOWN_ACTION`, `NO_PENDING_ACTION`,
`WEARABLE_BUSY`, `LINK_UNAVAILABLE`. `message` è diagnostico e non va usato per
decidere la logica del programma. Un messaggio USB invalido non produce ACK radio.
Per dati malformati non è richiesta la ripetizione di eventuali ID non affidabili.

## 8. Ponte verso le API del wearable

Il ricevitore sarà **client HTTP** delle API esistenti del wearable, sia sulla
propria rete diretta sia nella LAN del router. Il PC non usa queste API HTTP:
vede esclusivamente l'USB JSON Lines.

- Prima di acquisire comandi: handshake USB completato e wearable selezionato.
- Header HTTP: `X-Playmaker-Client`, ID casuale di 32 cifre esadecimali minuscole,
  mantenuto per la sessione del ricevitore.
- `POST /api/session`, corpo vuoto: prende il controllo. HTTP 204 è successo;
  HTTP 409 indica un altro controller (per esempio un tabellone browser aperto).
- `GET /api/action`, polling ogni 150 ms, senza sovrapporre richieste: HTTP 204
  significa nessuna azione; HTTP 200 porta ACTION binario v6 di 12 byte.
- Il ricevitore conserva ID e codice azione, converte il pacchetto in `action`
  JSON e lo inoltra al PC. `attempt` aumenta soltanto per una nuova consegna
  dello stesso pacchetto; un riavvio del ricevitore non autorizza replay autonomi.
- L'ACK USB valido viene convertito nell'ACK v6 di 20 byte e scritto con
  `POST /api/ack`, `Content-Type: application/octet-stream`.
- HTTP 204 produce `ack_result:forwarded`; HTTP 410 produce `expired`;
  un errore di rete produce `link_error`, senza inventare una conferma positiva.
- Il ricevitore aggiorna anche `GET /api/network` ogni 10 secondi mentre il PC
  è pronto, mantenendo sveglio il wearable come fa il tabellone attuale.
- Ricevitore e tabellone browser non devono controllare contemporaneamente lo
  stesso wearable. Il tabellone deve essere chiuso per liberare il lease HTTP.

### Discovery implementato

Per evitare che l'IP DHCP debba essere impostato manualmente:

- Il wearable ascolta sulla porta UDP **37020**.
- Il ricevitore invia alla broadcast della propria interfaccia Wi-Fi:

```json
{"v":1,"type":"discover","service":"playmaker-wearable","nonce":123}
```

- Il wearable risponde **unicast** all'IP/porta sorgente della richiesta:

```json
{"v":1,"type":"discovery","service":"playmaker-wearable","wearable_id":"ac276e45e006","http_port":80,"nonce":123}
```

Il ricevitore usa l'indirizzo sorgente UDP della risposta, verifica nonce e
servizio, quindi verifica le API HTTP. Non si fida di un IP scritto nel JSON.
I datagrammi non superano 512 byte. La ricerca avviene circa ogni tre secondi,
anche per aggiornare la lista della pagina di configurazione. Nel prototipo il
caso supportato è **un wearable per ricevitore**.
Con più wearable visibili, il primo abbinamento richiede una selezione esplicita
dalla pagina del ricevitore; l'ID selezionato si salva in NVS e non cambia da solo.

Il discovery è locale alla sottorete, non attraversa VLAN o router ed è destinato
a reti fidate. Né il nonce di discovery né l'ID di sessione USB sono credenziali
di autenticazione. Non esporre queste API su Internet.

## 9. Checklist di integrazione per il software Ubuntu

1. Identificare la porta stabile, aprirla raw e avviare `hello`/`ready`.
2. Leggere JSON Lines gestendo righe frammentate e inviare heartbeat.
3. Per ogni `action`, controllare la terna identificativa prima di applicare effetti.
4. Salvare risultato, cronologia ed ACK in un'unica transazione persistente.
5. Restituire l'ACK, riusando quello salvato per i duplicati.
6. Gestire eventi non di punteggio senza modificarne i contatori.
7. Alla perdita USB, riaprire la porta e rifare l'handshake; non resettare la
   partita e non cancellare la deduplicazione solo perché cambia `boot_id`.
8. Verificare: cinque click A → cinque punti richiesti; doppio centrale → un VAR;
   ACK perso → nessun doppio punto; tre timeout → comando scartato; distacco USB
   → nessuna falsa conferma; modalità diretta e LAN → stesso protocollo USB.

Il contratto v1 può essere usato dal socio per sviluppare e testare il parser con
righe simulate. Per il collaudo con le schede seguire `receiver/README.md` e
programmare entrambe con i rispettivi build. Non è incluso un servizio Ubuntu.
