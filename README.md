# Segnapunti da padel — Waveshare ESP32-C6-LCD-1.47

Segnapunti elettronico per il padel, con **un solo pulsante** e un display da
172 × 320 pixel montati su una scheda **Waveshare ESP32-C6-LCD-1.47**.

Il pulsante è quello di **BOOT**, l'unico presente sulla scheda. Fa tutto lui:

| Gesto | Effetto |
|---|---|
| 1 click | punto a **NOI** (pannello di destra) |
| 2 click | punto a **LORO** (pannello di sinistra) |
| 3 click | **annulla** l'ultima azione |
| 4 click o più | nessuna azione |
| pressione di 4 secondi | **azzera** la partita e ricomincia |
| pressione di 6 secondi | **azzera** e apre la finestra di **commissioning** |

Un click solo non viene eseguito al rilascio del pulsante, ma allo scadere di
una finestra di 400 ms: è quello che permette di distinguere uno, due e tre
click senza che il punteggio cambi a ogni tentativo.

La partita è al meglio dei cinque set. I game si contano 0 / 15 / 30 / 40 con
vantaggio illimitato (niente punto decisivo), i set si chiudono a 6 con due di
scarto, e sul 6-6 si va al tie-break, che si chiude a 7 con due di scarto e
senza limite. A partita finita appare la schermata del vincitore, che resta
quattro secondi e poi la partita ricomincia da sola.

Il **LED RGB di bordo** dice chi ha segnato l'ultimo punto: a ogni punto fa uno
spettacolo di luci e poi resta acceso del colore della squadra che l'ha vinto,
verde per LORO e azzurro per NOI. Annullando non c'è nessuno spettacolo: il
colore torna semplicemente indietro, perché chi si è corretto non deve vedersi
una festa. La luminosità si regola da menuconfig, e a zero il LED resta spento.

Una **pagina web** può collegarsi via Bluetooth e mostrare lo stesso punteggio
sul computer, senza server e senza account. La scheda resta lei la padrona:
manda lo stato, non lo riceve, e chi non si è fatto riconoscere non vede nulla.
Si revoca l'associazione, e si apre la finestra per associarne una nuova, in due
modi che fanno la stessa cosa: tenendo basso il piedino di commissioning per tre
secondi, oppure tenendo premuto il pulsante di BOOT oltre l'azzeramento.

---

## Cosa rende questo progetto diverso dal solito

**Le regole del gioco si provano senza la scheda.** Il motore del punteggio,
la macchina a stati del pulsante, il disegno delle forme e il calcolo di quali
zone ridisegnare non sanno che esista un ESP32: sono normali file C che girano
anche su un computer. `test\run_tests.ps1` ne esegue **oltre diecimila
controlli in pochi secondi**, compresa una partita intera giocata punto per
punto.

**Non si ridisegna mai lo schermo intero mentre si gioca.** A ogni punto cambia
solo il pannello della squadra che l'ha vinto: circa diciassettemila pixel
invece di cinquantacinquemila, un terzo del lavoro. Le zone vicine vengono
comunque unite prima di essere mandate al display, perché due trasmissioni
attaccate costano quasi il doppio di una sola che le contiene. C'è un test che
gioca una partita intera e verifica che questo resti vero.

**Il display non conosce il padel e il padel non conosce il display.** Sono
cinque strati con una sola direzione di dipendenza: se un giorno cambia il
pannello, o le regole, si tocca un file solo.

---

## Come si costruisce la schermata

```
y   3 .. 13    PADEL SCORE                        [TB]
y  17 .. 33    ────────────────── ● ──────────────────

y  38 .. 189        LORO                 NOI
                     •                   ○
                    40                   30

y 196 .. 313  ┌──────────────────────────────────┐
              │           GAME   0 - 0           │
              │   ──────────────────────────     │
              │           SET    0 - 0           │
              └──────────────────────────────────┘
```

I due pannelli sono larghi 79 pixel e hanno un alone del colore della squadra:
LORO in verde acqua a sinistra, NOI in azzurro a destra. Il pallino sopra il
punteggio dice chi serve. Il distintivo `TB` in alto a destra compare solo
durante il tie-break, quando al posto di 0 / 15 / 30 / 40 compaiono i punti
contati uno per uno. La pallina sulla riga di separazione è il segno della
scheda, non un'informazione: è lì perché una riga sola sembrava un taglio.

I game e i set stanno in **una scheda sola**, divisa da una riga sottile: due
schede separate con un margine in mezzo sprecavano spazio senza guadagnare
niente.

La cifra grande viene scelta in base a **quanto è larga davvero** la stringa,
non a quanti caratteri ha: `40`, `AD` e `103` hanno lunghezze simili ma
larghezze molto diverse, e ciascuno viene dimensionato per quello che occupa.
Il carattere più grande non si usa mai qui — servirebbe solo a far sembrare
schiacciati i numeri contro la cornice — e resta alla schermata del vincitore,
dove la cifra è una sola.

---

## Architettura

```
GPIO9 ──→ button.c ──→ controller.c ──→ match.c ──→ history.c
                          │                 │
                          │                 └──→ ui_view.c ──→ ui.c ──→ gfx.c
                          │                                     │
                          ├──→ led_anim.c ──→ rgb_led.c         └──→ display.c
                          │           │              │                  │
                          └──→ ble_score_service.c ──┼──────────────────┴──→ schermo, LED
                                    │                │
GPIO0 ──→ commissioning_manager.c ──┴──→ ble_gatt.c ──→ Bluetooth ──→ pagina web
                    │                              ▲
                    └──→ nvs_store.c (token)       └── commissioning_ui.c → schermo
```

Un solo senso di marcia. Nessuno risale la catena.

| Modulo | Cosa fa | Sa cosa è un ESP32? |
|---|---|---|
| `match` | le regole del padel | no |
| `history` | le azioni annullabili | no |
| `button` | riconosce i gesti dal livello del piedino | no |
| `controller` | traduce i gesti in azioni, gestisce la fine partita | no |
| `font` | tabelle dei glifi e scelta del carattere | no |
| `gfx` | forme e testo su un buffer di pixel | no |
| `dirty` | unisce le zone da ridisegnare | no |
| `ui_view` | cosa va mostrato e cosa è cambiato | no |
| `palette` | i colori delle due squadre, per schermo e LED | no |
| `led_anim` | lo spettacolo di luci e che colore lasciare acceso | no |
| `ble_protocol` | UUID, opcode e disposizione dei byte dei pacchetti | no |
| `score_state_adapter` | trasforma il punteggio nel pacchetto da spedire | no |
| `commissioning_state` | la macchina a stati dell'associazione | no |
| `hold_gesture` | riconosce il piedino tenuto basso per tre secondi | no |
| `device_identity` | dall'indirizzo della scheda al suo nome breve | no |
| `ui` | come si disegna la schermata | solo per la larghezza |
| `commissioning_ui` | come si disegna la schermata di commissioning | solo per la larghezza |
| `display` | bus SPI, controller ST7789, retroilluminazione | sì |
| `rgb_led` | il LED di bordo, WS2812B sul periferico RMT | sì |
| `ble_gatt` | NimBLE: servizio, annuncio, connessioni, notifiche | sì |
| `nvs_store` | il token di associazione, l'unica cosa scritta in memoria | sì |
| `commissioning_manager` | la regia del commissioning | sì |
| `ble_score_service` | il punto in cui il punteggio incontra la radio | sì |
| `rgb_led` | il LED di bordo, WS2812B sul periferico RMT | sì |
| `main` | ciclo principale | sì |

Gli undici moduli senza ESP32 si compilano anche su PC. È questa separazione
che rende verificabile quello che altrimenti si vedrebbe solo guardando lo
schermo: perfino lo spettacolo di luci, che sul PC si guarda istante per
istante invece di aspettare che capiti il punto giusto.

---

## Come si usa

Tutti i comandi passano da `scripts\flash.ps1`, che attiva da solo l'ambiente
ESP-IDF giusto (v6.0.2) e trova la porta seriale della scheda.

```powershell
# compila
.\scripts\flash.ps1 -Action build

# compila, scrive sulla scheda e apre il monitor seriale
.\scripts\flash.ps1 -Action all -Port COM18

# solo il monitor seriale
.\scripts\flash.ps1 -Action monitor -Port COM18

# quanto spazio occupa
.\scripts\flash.ps1 -Action size
```

### La pagina web

La pagina sta in `web/` ed è una webapp statica: nessun server applicativo, la
comunicazione è diretta fra browser e scheda.

```powershell
cd web
npm install
npm run dev        # apre su http://localhost:5173

npm run test       # prove dei moduli puri
npm run typecheck  # controllo dei tipi
npm run build      # versione da pubblicare, in web/dist
```

Servono **Chrome o Edge su computer**, il **Bluetooth acceso** e una pagina
servita da `localhost` oppure in HTTPS: Web Bluetooth non funziona altrove, ed è
una regola del browser, non una scelta di questo progetto.

Quello che si vede, dall'alto in basso:

- una **banda di stato** che non sparisce mai: pallino colorato, una riga grossa
  («COLLEGATA a PADEL_SCORE_EE26», «NON COLLEGATA», «Collegamento in corso…») e
  una riga che dice da quanto non arriva niente;
- il pannello **Collegamento**, con i dati della scheda (nome, nome breve,
  firmware, associazione), i pulsanti e l'elenco delle **schede che questa
  pagina vede**, con l'identificativo di ciascuna;
- il pannello **Partita**, che è il tabellone: quando i dati sono vecchi resta
  in grigio, per non far credere che sia ancora vivo;
- il pannello **Diagnostica**: pacchetti, salti, duplicati, ritardi.

La pagina **riprova da sola** quando il collegamento cade — la scheda si
riavvia, o passa un disturbo — come fanno le cuffie quando le riaccendi: ogni
mezzo secondo all'inizio, poi sempre più di rado, senza che nessuno prema
niente. E non aspetta che sia il computer ad accorgersene: la scheda manda un
piccolo **battito** ogni 1,2 secondi anche quando il punteggio non cambia, così
se ne accorge in **tre secondi** invece di una decina. Mentre ci prova la banda
in alto diventa gialla e lo scrive; **ANNULLA RICONNESSIONE** la ferma, e
**SCOLLEGA** la spegne (la pagina non riapre mai un collegamento chiuso a mano).
Nel pannello Diagnostica si legge **quanto è durata l'ultima interruzione**,
che è il modo di sapere se la riconnessione è veloce come si vorrebbe. La scheda
inoltre, appena collegata, **chiede un collegamento più reattivo**: intervallo
di 30 ms e morte dichiarata dopo 2 secondi invece dei dieci che propone il
computer, così anche il sistema si accorge prima che è sparita.
Dopo un **ricaricamento della pagina**, invece, riprendere la scheda richiede un
click dove il browser non ha `getDevices()`: è una regola di Web Bluetooth, e la
pagina lo dice invece di provarci per sempre.

### I test, senza scheda collegata

```powershell
.\test\run_tests.ps1
```

Serve solo un compilatore C: lo script trova da sé quello di Strawberry Perl, di
MinGW o di Visual Studio. Se non ne trova nessuno spiega come installarlo.
Nessuna scheda va collegata, e i test durano pochi secondi.

---

## Prerequisiti

- **ESP-IDF v6.0.2**, già installato su questa macchina in `C:\esp\v6.0.2\esp-idf`.
  Lo script lo seleziona da solo.
- Un **cavo USB-C dati** per collegare la scheda.

### Se usi l'estensione ESP-IDF per VS Code

Il file `C:\Espressif\tools\eim_idf.json` potrebbe avere selezionata una
versione diversa. Apri l'**ESP-IDF Manager** dalla palette dei comandi e
seleziona **v6.0.2** prima di compilare. Da riga di comando non serve: lo
script `flash.ps1` è indipendente da quella impostazione.

---

## Impostazioni

Tutto si configura da `idf.py menuconfig` → **Segnapunti padel**.

| Voce | Predefinito | A cosa serve |
|---|---|---|
| Luminosità | 500 ‰ | metà potenza: il produttore avverte che il massimo lascia aloni permanenti |
| Luminosità LED | 400 ‰ | quanto è luminoso il LED di bordo, spettacolo compreso; a 0 resta spento |
| Piedino di commissioning | GPIO0 | tenuto verso massa per 3 s apre la finestra |
| Tenuta per aprire | 3000 ms | quanto va tenuto basso il piedino |
| Durata della finestra | 60000 ms | per quanto la scheda accetta una nuova associazione |
| Esito a video | 2000 ms | quanto resta SUCCESS o TIMEOUT prima di tornare al punteggio |
| Rimbalzo ignorato | 25 ms | quanto deve restare stabile il piedino prima di essere creduto |
| Finestra multi-click | 400 ms | quanto si aspetta per capire se arriva un altro click |
| Pressione lunga | 4000 ms | durata per l'azzeramento |
| Pressione prolungata | 6000 ms | tenuta che apre il commissioning, senza fili |
| Durata schermata finale | 4000 ms | quanto resta a video chi ha vinto |
| Chi serve per primo | NOI | nel padel si sorteggia, quindi si sceglie qui |
| Set per vincere | 3 | al meglio dei 5 |
| Figura di prova all'avvio | no | vedi «Se i colori sono sbagliati» |
| Log degli eventi | no | scrive sul monitor seriale quale gesto è stato riconosciuto |
| Log dei ridisegni | no | scrive quante zone e quanti pixel sono stati riscritti |
| Verifica coerenza | no | controlla lo stato della partita dopo ogni punto |

---

## Associare una pagina web (commissioning)

La scheda non si fa comandare da chiunque: prima bisogna associarla a una pagina
web, e l'associazione si puo' revocare in qualsiasi momento.

**Prima volta**

1. Porta il piedino **GPIO0 verso massa** e tienilo li' per **tre secondi**,
oppure tieni premuto il pulsante di **BOOT** fino a **sei secondi**: la partita
si azzera e si apre la finestra di commissioning. Sul
display compare la schermata `COMMISSIONING`, con il titolo che **lampeggia di
blu** e il conto alla rovescia. La finestra resta aperta **sessanta
secondi**.
2. Apri la pagina (`cd web; npm run dev`) e premi **COMMISSIONA SCHEDA**.
3. Nella finestra del browser scegli `PADEL_SCORE_XXXX`, dove `XXXX` sono gli
ultimi due byte dell'indirizzo Bluetooth della scheda: cosi' si riconosce
quale scheda si sta prendendo.
4. La pagina genera un token casuale, lo manda con `CLAIM`, e lo salva nel
browser insieme al riferimento alla scheda. Sul display compare `SUCCESS`.

**Volte successive**

La pagina ritrova da sola la scheda e si fa riconoscere con il token salvato: se
il browser non lo permette, c'è il pulsante **RICONNETTI**.

**Per revocare l'associazione**

Tieni di nuovo GPIO0 verso massa per tre secondi, oppure il pulsante di BOOT
fino a sei: la scheda cancella il token e riapre la finestra. D'ora in poi la
vecchia pagina riceve
*"associazione non piu' valida"* e si rifa' il commissioning.

> ⚠️ Il piedino di commissioning è **GPIO0**, che sulla scheda è libero e non è
> uno dei piedini di avvio. Va portato verso massa con un filo o un pulsante:
> non è uno dei tasti presenti a bordo. Se non hai un filo a portata di mano,
> usa il pulsante di BOOT: tenuto premuto oltre l'azzeramento fa la stessa cosa.

Il protocollo completo, pacchetto per pacchetto, sta in
[`docs/ble-architecture.md`](docs/ble-architecture.md).

---

## Se qualcosa non va

### La scheda non parte

Se il pulsante di BOOT è premuto mentre si dà alimentazione, la scheda entra in
modalità download invece di avviare il programma. **È il comportamento normale**:
accendi e poi premi.

### I colori sono sbagliati

Apri `menuconfig`, attiva **Figura di prova all'avvio**, ricompila e riavvia: per
cinque secondi vedrai quattro barre verticali, rosso, verde, azzurro e bianco,
con una squadretta bianca nell'angolo in alto a sinistra. Poi parte la partita.

Guarda le barre e cambia **una sola riga** in fondo a `main/display.c`:

| Cosa vedi | Cosa cambiare |
|---|---|
| Barre nell'ordine giusto | niente, è a posto |
| Rosso e azzurro scambiati | `LCD_RGB_ORDER` → `LCD_RGB_ELEMENT_ORDER_BGR` |
| Immagine in negativo | `LCD_INVERT` → `false` |
| Colori completamente sballati | `LCD_ENDIAN` → `LCD_RGB_DATA_ENDIAN_BIG` |
| Capovolta | `LCD_MIRROR_Y` → `true` |
| Specchiata | `LCD_MIRROR_X` → `true` |
| Girata di lato | `LCD_SWAP_XY` → `true` e il margine diventa `LCD_Y_GAP 34` |
| Spostata verso destra o tagliata | `LCD_X_GAP` → `0` |

Sono le uniche quattro incognite che non si possono dedurre dai documenti: il
resto è verificato.

### Il Bluetooth non si vede

Il monitor seriale lo dice all'avvio:

```
I (238) led: LED RGB acceso su GPIO8
I (240) commissioning: servizio pronto, nome PADEL_SCORE_A31F
I (241) ble: in annuncio come PADEL_SCORE_A31F
```

Se la riga `servizio pronto` manca, la riga sopra dice perché, e in ogni caso il
segnapunti continua a funzionare: si gioca anche senza pagina web.

Se la scheda non compare nella finestra di scelta del browser, controlla di
essere su `localhost` o in HTTPS, che il Bluetooth del computer sia acceso e che
non ci siano altri programmi collegati alla scheda: la connessione è una sola.

### La pagina dice «associazione non più valida»

Qualcuno ha tenuto basso il piedino di commissioning: la scheda ha cancellato il
vecchio token e ne accetta uno nuovo solo a finestra aperta. Si tiene basso il
piedino per tre secondi e si preme **COMMISSIONA SCHEDA**.

### Il LED non si accende

Il monitor seriale lo dice all'avvio. Cerca questa riga:

```
I (238) led: LED RGB acceso su GPIO8
```

Se c'è, il periferico è partito e il problema è la luminosità impostata a zero.
Se non c'è, la riga sopra spiega perché: il canale RMT non era disponibile o il
piedino non si è lasciato configurare, e in entrambi i casi il segnapunti va
avanti lo stesso, senza LED.

### Lo schermo resta nero

Il monitor seriale lo dice. Se compare
`schermo non disponibile (...)`, il motivo è scritto lì: il programma prosegue
senza display invece di riavviarsi in ciclo, proprio perché il messaggio resti
leggibile.

### Niente appare sul monitor seriale

La scheda non ha un convertitore USB-seriale: il connettore va dritto nel
controller USB del chip. La console deve quindi essere instradata lì, ed è
quello che fa `sdkconfig.defaults` con `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`.
Se hai modificato la configurazione, controlla che quella voce sia ancora
attiva.

---

## Altro materiale

- Il pinout completo della scheda, con le note sul doppio ruolo del piedino di
  BOOT, sta in [`docs/HARDWARE.md`](docs/HARDWARE.md).
- In [`examples/boot_hello/`](examples/boot_hello/) c'è il primo programma
  scritto per questa scheda, quello che stampava `Hello World` a ogni pressione
  del pulsante. Non entra nella compilazione: è rimasto come riferimento per la
  piedinatura e per la stampa sul monitor seriale.


---

## Struttura del progetto

```
test-Deep-seek/
├── CMakeLists.txt              # progetto ESP-IDF
├── sdkconfig.defaults          # target, console, flash, Bluetooth
├── .vscode/
│   └── c_cpp_properties.json   # IntelliSense via build/compile_commands.json
├── main/                       # il firmware
│   ├── match.c · history.c         # le regole del padel             (puri)
│   ├── button.c · controller.c     # gesti e fasi della partita      (puri)
│   ├── ui_view.c · gfx.c · font.c  # schermata e caratteri           (puri)
│   ├── led_anim.c                  # lo spettacolo di luci           (puro)
│   ├── ble_protocol.c              # UUID, opcode, pacchetti         (puro)
│   ├── score_state_adapter.c       # dal punteggio al pacchetto      (puro)
│   ├── commissioning_state.c       # macchina a stati dell'associazione (pura)
│   ├── hold_gesture.c              # il piedino tenuto basso          (puro)
│   ├── device_identity.c           # il nome della scheda            (puro)
│   ├── ui.c · display.c            # disegno e controller ST7789
│   ├── rgb_led.c                   # LED di bordo, WS2812B via RMT
│   ├── ble_gatt.c                  # NimBLE: servizio, annuncio, notifiche
│   ├── nvs_store.c                 # il token in memoria permanente
│   ├── commissioning_manager.c · commissioning_ui.c
│   ├── ble_score_service.c         # pubblica lo stato sulla radio
│   └── main.c                      # il ciclo principale
├── test/                       # prove host: .\test\run_tests.ps1
├── web/                        # la pagina web (Vite + TypeScript)
├── scripts/flash.ps1           # build / flash / verify / monitor
├── docs/HARDWARE.md            # piedinatura verificata
├── docs/ble-architecture.md    # il protocollo Bluetooth, in dettaglio
└── examples/boot_hello/        # il primo programma, fuori dal build
```

> Dopo il primo `idf.py build` viene generato `build/compile_commands.json`, che
> `.vscode/c_cpp_properties.json` usa per far risolvere a IntelliSense gli header
> di ESP-IDF. Prima del primo build l'editor segnala header non trovati: è
> normale e non influisce sulla compilazione.

### La scheda

Il segnapunti è scritto e verificato per la **Waveshare ESP32-C6-LCD-1.47**, e
non è previsto il supporto ad altre schede: la piedinatura sta in
`main/board.h`, il target è dichiarato in `sdkconfig.defaults`, quindi non serve
eseguire `idf.py set-target`. Compilando per un altro chip, `main.c` se ne
accorge con un `#error` prima di provare a leggere un piedino.

---

## Compilare e flashare

### Metodo rapido (script)

```powershell
# Attiva ESP-IDF v6.0.2, compila, flasha e apre il monitor
.\scripts\flash.ps1

# Solo compilazione
.\scripts\flash.ps1 -Action build

# Porta esplicita, se hai più schede collegate
.\scripts\flash.ps1 -Port COM7

# Footprint del firmware
.\scripts\flash.ps1 -Action size
```

Lo script rileva automaticamente la porta della scheda cercando il
dispositivo USB con VID `303A` (Espressif).

### Metodo manuale

Da un terminale PowerShell:

```powershell
. C:\Espressif\tools\Microsoft.7101770.PowerShell_profile.ps1   # attiva ESP-IDF v6.0.2
Set-Location d:\mio\test-Deep-seek

idf.py build
idf.py -p COMx flash monitor
```

> Non serve eseguire `idf.py set-target esp32c6`: il target è già dichiarato in
> `sdkconfig.defaults` e viene rilevato automaticamente al primo build.

Per uscire dal monitor seriale premi **Ctrl + ]**.

---

## Output atteso

All'avvio compare il banner (output reale, catturato dalla scheda):

```
==================================================
  Segnapunti padel - ESP32-C6-LCD-1.47
==================================================
  chip       ESP32-C6 rev v0.1, ESP-IDF v6.0.2
  schermo    ST7789 172x320, margine X 34, SPI 40 MHz
  LED RGB    GPIO8, luminosita' 40%
  pulsante   GPIO9, attivo basso
  gesti      1 click NOI | 2 click LORO | 3 click annulla
             4 click niente | 4000 ms azzera
             6000 ms apre il commissioning
  finestra   400 ms per i click multipli
  partita    al meglio di 5 set, serve per primo NOI
  vincitore  4000 ms a video
  Bluetooth  PADEL_SCORE_A31F, protocollo v1
  associaz.  GPIO0 tenuto basso per 3000 ms apre la finestra
--------------------------------------------------
I (233) display: ST7789 172x320 pronto, 110080 byte per l'immagine
I (238) led: LED RGB acceso su GPIO8
I (241) ble: in annuncio come PADEL_SCORE_A31F
```

---

## Come funziona

`main/main.c` è un ciclo da cinque millisecondi che non prende nessuna decisione
sul padel. A ogni giro:

1. legge il pulsante di BOOT e lo passa al `controller`, che traduce i gesti in
   azioni secondo la fase della partita;
2. fa avanzare il timer della schermata del vincitore;
3. guarda il piedino di commissioning e lascia lavorare
   `commissioning_manager`;
4. chiede a `ble_score_service` se il punteggio è cambiato e, se lo è, lo
   pubblica sulla radio;
5. disegna: la schermata di commissioning se è aperta, altrimenti il punteggio.

Il motore del punteggio non sa che esiste un display, un pulsante o una radio:
riceve "punto a NOI" e aggiorna lo stato. Tutto il resto guarda.

Niente attese: il collegamento Bluetooth, lo schermo e il riconoscimento del
piedino vivono nello stesso ciclo senza `delay()` e senza cicli di attesa.

### Parametri regolabili

Tutti in `menuconfig` → **Segnapunti padel** (vedi la tabella delle impostazioni
più sopra), oppure direttamente in `sdkconfig.defaults` per le cose che devono
valere fin dalla prima compilazione, come il Bluetooth.

---

## ⚠️ Nota importante sul tasto BOOT

Il pin del pulsante non è solo un ingresso: è anche il **pin di strapping del
bootloader** (confermato dalla documentazione esptool per entrambi i chip).
Significa che:

- **Leggere** il pin a runtime è del tutto sicuro — è quello che fa questo
  programma.
- **Tenerlo premuto durante un reset o all'accensione** mette il chip in
  modalità download seriale: l'applicazione non parte e sul monitor compare
  `waiting for download`.

Lo stesso vale per il piedino del LED di bordo (GPIO8): il segnapunti lo
configura solo a chip avviato, dopo lo schermo, e non lo tocca prima.

---

## Troubleshooting

| Sintomo | Causa / Soluzione |
|---|---|
| Nessuna porta COM appare | Il cavo USB-C è di sola ricarica. Usa un cavo dati. |
| `idf.py` non trovato | Non hai attivato l'ambiente: esegui il dot-source dello script `Microsoft.*.PowerShell_profile.ps1` (oppure usa `scripts\flash.ps1`). |
| Flash ok ma niente output | Verifica in `sdkconfig` che `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`. |
| `#error` in fase di build | Il target non è l'ESP32-C6: controlla `CONFIG_IDF_TARGET` in `sdkconfig.defaults`. |
| `waiting for download` all'avvio | Hai tenuto premuto BOOT durante il reset: premi RESET (EN) senza toccare BOOT. |
| Più punti per una pressione sola | Contatto rimbalzante: alza **Rimbalzo ignorato** in menuconfig. |
| Il monitor non risponde a Ctrl+C | Per uscire dal monitor si usa **Ctrl + ]**. |
| `This chip is ESP32-xx, not ESP32-yy` | Il firmware è compilato per un chip diverso da quello collegato. Verifica il modello con `python -m esptool -p COMx chip-id`. |

---

## Verifica rapida

1. `.\test\run_tests.ps1` → tutti i test passano, senza scheda collegata.
2. `scripts\flash.ps1 -Action all -Port COM18` → compila, flasha, apre il monitor.
3. Il monitor mostra `display: ST7789 ... pronto`, `led: LED RGB acceso su GPIO8`
   e `ble: in annuncio come PADEL_SCORE_XXXX`.
4. Un click → 15 a NOI. Due click → 15 a LORO. Tre click → si torna indietro.
5. Quattro secondi di pressione → la partita si azzera.
6. Sei secondi di pressione → compare la schermata `COMMISSIONING` con il conto
   alla rovescia.
7. Piedino GPIO0 verso massa per tre secondi → stessa schermata, senza toccare
   il pulsante.
8. `cd web; npm run dev` → **COMMISSIONA SCHEDA**, si sceglie
   `PADEL_SCORE_XXXX`, e il tabellone compare e segue i punti.
9. `scripts\flash.ps1 -Action verify -Port COM18` → la scheda contiene
   esattamente il programma compilato, partizione per partizione.

---

## Licenza

MIT — vedi l'intestazione SPDX nei file sorgente.
