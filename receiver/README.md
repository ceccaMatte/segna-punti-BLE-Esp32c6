# Playmaker Receiver USB — ESP32-C3

Progetto ESP-IDF separato dal wearable. Usa una seconda ESP32-C3 collegata via
USB al PC Ubuntu Server: riceve le azioni Wi-Fi e le inoltra al programma del PC
come JSON Lines. Il punteggio viene calcolato e confermato dal programma Ubuntu.

## Build e programmazione

Richiede ESP-IDF **6.0.2**, ESP32-C3 con USB nativa e flash da 4 MiB.
Dalla shell ESP-IDF:

```sh
cd receiver
idf.py build
idf.py -p PORTA_DEL_RICEVITORE flash
```

Output: `receiver/build/playmaker_receiver.bin` e relativi bootloader/partizioni.
Flashare il progetto completo con `idf.py flash`, non soltanto il binario app su
una scheda con partizioni diverse. Su Ubuntu la porta è normalmente
`/dev/ttyACM0`; su Windows identificare la COM della **seconda scheda**.

Il progetto compila direttamente i sorgenti condivisi dal resto del repository:
non copiare soltanto questa cartella. Alla prima compilazione il gestore ESP-IDF
scarica `espressif/cjson` **1.7.19~2**. Internet serve al build iniziale, non al
funzionamento delle schede o del PC Ubuntu.

Anche il wearable va aggiornato dal progetto nella radice del repository:

```sh
# Dalla radice, sulla scheda radiocomando:
idf.py build
idf.py -p PORTA_DEL_WEARABLE flash
```

Il nuovo wearable aggiunge discovery UDP e header HTTP con numero di tentativo
e tempo residuo. Il ricevitore richiede questi campi per rispettare il limite di
tre invii già gestito dal wearable; non aggiunge un secondo ciclo di retry.

## Configurazione da telefono o PC con Wi-Fi

Il ricevitore parte in modalità **diretta**:

1. Collegati all'AP `PLAYMAKER_RX_XXXX`, password `playmaker`.
2. Apri `http://192.168.8.1/`.
3. Lascia selezionata Diretta, oppure scegli LAN e salva: il ricevitore si riavvia.

### Diretta, senza router

Dalla configurazione del wearable (`http://192.168.4.1/settings` sul suo AP)
seleziona il Wi-Fi del ricevitore e salva la password `playmaker`.
Il wearable memorizza la rete come una normale rete preferita.
Il ricevitore offre DHCP nella sottorete `192.168.8.0/24`.

### LAN del router

Dopo aver selezionato LAN e atteso il riavvio:

1. Ricollegati all'AP del ricevitore e riapri `http://192.168.8.1/`.
2. Premi **Cerca reti**, seleziona la rete a 2,4 GHz, inserisci la password e salva.
3. Configura la stessa rete sul wearable tramite la sua pagina.
4. Lascia che il discovery trovi il wearable, oppure selezionalo dalla pagina se
   sono presenti più dispositivi.

L'ultima rete collegata con successo resta in NVS; se non disponibile viene
provata `Se` / `ciao1234`. L'AP di configurazione rimane disponibile in LAN.
La pagina del ricevitore mostra anche l'indirizzo ottenuto dal router.
Non usare sul router la stessa sottorete `192.168.8.0/24` dell'AP ricevitore.
Il discovery non attraversa VLAN o isolamento fra client Wi-Fi.

Un solo wearable è controllato da ciascun ricevitore. Quando ne viene scoperto
uno solo, l'identificativo viene salvato automaticamente; con più wearable va
selezionato esplicitamente. Il ricevitore conserva l'abbinamento e non passa a
un altro wearable automaticamente. Cambiare selezione richiede un nuovo handshake USB.

## Collegamento al programma Ubuntu

La USB nativa usa il driver USB Serial/JTAG direttamente, senza console testuale.
I log applicativi e Wi-Fi sono disabilitati; eventuali righe ROM precedenti al
primo `hello` devono essere ignorate dal programma Ubuntu.

Il programma del socio deve implementare il [contratto USB API v1](../docs/receiver-usb-api.md):

1. Aprire la porta raw a 115200 8N1 senza controllo di flusso.
2. Inviare `{"v":1,"type":"hello"}` e attendere la risposta.
3. Inviare `{"v":1,"type":"ready"}`.
4. Inviare un `ping` ogni secondo e leggere `action`/`status`/`pong`.
5. Applicare e salvare ogni azione una sola volta, quindi rispondere con l'ACK.

Gli esempi sopra sono righe terminate da LF, non comandi per una shell.
Il solo cavo USB non abilita la lettura delle azioni: serve il programma pronto.
Alla perdita USB o dopo tre secondi senza messaggi validi dal PC, il polling
si ferma e occorre ripetere `hello`/`ready`. Un ACK del ricevitore non viene mai
inventato al posto del PC. Chiudere il tabellone browser del wearable prima di
usare il ricevitore: il controllo HTTP è esclusivo e il lease dura dieci secondi.

## Sorgenti riutilizzati

| Sorgente | Uso |
| --- | --- |
| `main/link/wifi_network.c` | AP/STA, scansione, credenziali NVS, fallback e API rete |
| `main/link/wearable_protocol.c` | Codifica/decodifica binaria ACTION/ACK v6 |
| `main/core/config_model.c` e header core | Tipi e dipendenze del protocollo esistente |
| `shared/discovery/` | Identità e responder UDP del wearable |
| `web-wearable/brand.css` | Stile Playmaker, incluso nel firmware |

Il ricevitore non avvia pulsanti, buzzer, deep sleep o radio Bluetooth.

## Verifica

I test host validano JSON e conversione binaria, mentre il test browser verifica
la pagina su desktop/mobile con API simulate. Per eseguire il test C dopo il
primo build del wearable, dalla radice:

```sh
gcc -std=c11 -Wall -Wextra -Werror -DCJSON_NESTING_LIMIT=16 \
  -Ireceiver/main -Imain/core -Imain/link -Imanaged_components/espressif__cjson/cJSON \
  test/host/test_receiver_protocol.c receiver/main/usb_protocol.c \
  main/link/wearable_protocol.c main/core/config_model.c \
  managed_components/espressif__cjson/cJSON/cJSON.c -lm -o build/test_receiver_protocol
./build/test_receiver_protocol
node test/host/test_receiver_ui.cjs
```

Il test UI usa l'installazione Playwright in `build/qa` e il percorso Chrome
indicato nello script. Il collaudo radio/USB completo richiede le due schede e
un client di prova compatibile con l'API, in entrambe le modalità di rete.
Il servizio applicativo Ubuntu non fa parte di questo progetto.
