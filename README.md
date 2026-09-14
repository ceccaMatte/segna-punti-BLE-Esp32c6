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

Un click solo non viene eseguito al rilascio del pulsante, ma allo scadere di
una finestra di 400 ms: è quello che permette di distinguere uno, due e tre
click senza che il punteggio cambi a ogni tentativo.

La partita è al meglio dei cinque set. I game si contano 0 / 15 / 30 / 40 con
vantaggio illimitato (niente punto decisivo), i set si chiudono a 6 con due di
scarto, e sul 6-6 si va al tie-break, che si chiude a 7 con due di scarto e
senza limite. A partita finita appare la schermata del vincitore, che resta
quattro secondi e poi la partita ricomincia da sola.

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
y   0 .. 23    PADEL SCORE                            [TB]
y  24 .. 25    ────────────────────────────────────────────
y  28 .. 248        LORO                NOI
                     •                   ○
                    40                   30
y 252 .. 285     1      GAME      2
y 286 .. 319     0      SET       1
```

I due pannelli sono larghi 82 pixel: LORO in verde acqua a sinistra, NOI in
azzurro a destra. Il pallino sopra il punteggio dice chi serve. Il distintivo
`TB` in alto a destra compare solo durante il tie-break, quando al posto di
0 / 15 / 30 / 40 compaiono i punti contati uno per uno.

La cifra grande viene scelta in base a **quanto è larga davvero** la stringa,
non a quanti caratteri ha: `40`, `AD` e `103` hanno lunghezze simili ma
larghezze molto diverse, e ciascuno viene dimensionato per quello che occupa.

---

## Architettura

```
GPIO9 ──→ button.c ──→ controller.c ──→ match.c ──→ history.c
                                            │
                       ui_view.c ──→ ui.c ──→ gfx.c ──→ display.c ──→ ST7789
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
| `ui` | come si disegna la schermata | solo per la larghezza |
| `display` | bus SPI, controller ST7789, retroilluminazione | sì |
| `main` | ciclo principale | sì |

I primi otto si compilano anche su PC. È questa separazione che rende
verificabile quello che altrimenti si vedrebbe solo guardando lo schermo.

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
| Rimbalzo ignorato | 25 ms | quanto deve restare stabile il piedino prima di essere creduto |
| Finestra multi-click | 400 ms | quanto si aspetta per capire se arriva un altro click |
| Pressione lunga | 4000 ms | durata per l'azzeramento |
| Durata schermata finale | 4000 ms | quanto resta a video chi ha vinto |
| Chi serve per primo | NOI | nel padel si sorteggia, quindi si sceglie qui |
| Set per vincere | 3 | al meglio dei 5 |
| Figura di prova all'avvio | no | vedi «Se i colori sono sbagliati» |
| Log degli eventi | no | scrive sul monitor seriale quale gesto è stato riconosciuto |
| Log dei ridisegni | no | scrive quante zone e quanti pixel sono stati riscritti |
| Verifica coerenza | no | controlla lo stato della partita dopo ogni punto |

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
├── CMakeLists.txt          # Progetto ESP-IDF
├── sdkconfig.defaults      # Target, scheda, console, flash
├── .vscode/
│   └── c_cpp_properties.json  # IntelliSense via build/compile_commands.json
├── main/
│   ├── CMakeLists.txt
│   ├── Kconfig.projbuild   # Menu "Board configuration" di menuconfig
│   └── main.c              # Tutta la logica: init GPIO + lettura pulsante
├── scripts/
│   └── flash.ps1           # Script build / flash / monitor per PowerShell
├── docs/
│   └── HARDWARE.md         # Pinout verificato di entrambe le schede
└── README.md
```

> Dopo il primo `idf.py build` viene generato `build/compile_commands.json`, che
> `.vscode/c_cpp_properties.json` usa per far risolvere a IntelliSense gli header
> di ESP-IDF. Prima del primo build l'editor segnala header non trovati: è
> normale e non influisce sulla compilazione.

### Scegliere la scheda

Il progetto supporta entrambe le schede. La selezione vive in
`sdkconfig.defaults` e si cambia da `menuconfig` → **Board configuration**:

| Voce | Default | Note |
|---|---|---|
| `CONFIG_BOARD_ESP32_C6_LCD_147` | **attiva** | Imposta il pin BOOT a GPIO9 |
| `CONFIG_BOARD_ESP32_C5_LCD_147` | disattiva | Imposta il pin BOOT a GPIO28 |
| `CONFIG_BOOT_BUTTON_GPIO` | 9 / 28 | Sovrascrivibile per un pulsante esterno |
| `CONFIG_POLL_INTERVAL_MS` | 20 | Periodo di campionamento |
| `CONFIG_DEBOUNCE_SAMPLES` | 3 | Letture identiche richieste |
| `CONFIG_PRINT_PRESS_COUNTER` | off | Stampa `Hello World (#N)` |

Scheda e target di build devono coincidere: `main.c` lo verifica a tempo di
compilazione con un `#error` esplicito, così non si legge mai il pin sbagliato.

Per passare alla ESP32-C5:

```powershell
idf.py set-target esp32c5
# poi in menuconfig -> Board configuration, seleziona ESP32-C5-LCD-1.47
```

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

All'avvio compare il banner (output reale catturato dalla scheda):

```
==================================================
  Waveshare ESP32-C6-LCD-1.47
  BOOT button demo
==================================================
  SoC      : ESP32-C6, 1 core(s), silicon rev v0.1
  ESP-IDF  : v6.0.2
  Console  : USB Serial/JTAG
  BOOT     : GPIO9 (active low, 60 ms debounce)
--------------------------------------------------
  Press the BOOT button to print "Hello World".
  NOTE: holding BOOT while resetting enters
        download mode instead of starting the app.
        GPIO8 must stay high; GPIO9 low at reset = download mode
==================================================
```

Poi, **una riga per ogni pressione** del pulsante BOOT:

```
Hello World
Hello World
Hello World
```

---

## Come funziona

`main/main.c` è composto da tre parti:

1. **`boot_button_init()`** — configura il pin BOOT come ingresso con pull-up,
   interrupt disabilitati. Il polling avviene in un task normale, che è
   l'unico contesto in cui `printf()` può essere chiamato senza rischi.
2. **`print_banner()`** — stampa le informazioni di diagnostica tramite
   `esp_chip_info()` e `esp_get_idf_version()`.
3. **`watch_boot_button()`** — ciclo infinito con `vTaskDelay(20 ms)` che
   campiona il pin e applica un **debounce a conteggio**: un nuovo livello
   diventa "stabile" solo dopo 3 letture consecutive identiche
   (`CONFIG_DEBOUNCE_SAMPLES` × `CONFIG_POLL_INTERVAL_MS` = 60 ms). La
   transizione stabile alto → basso corrisponde alla pressione, quindi
   `Hello World` viene stampato **una sola volta** per pressione, senza
   ripetizioni mentre il tasto resta premuto.

In testa al file, prima di ogni altra cosa, ci sono tre controlli `#error`
che confrontano la scheda selezionata con il target di build: se non
coincidono la compilazione si ferma subito con un messaggio che indica il
comando `idf.py set-target` da eseguire.

### Parametri regolabili

Tutti in `menuconfig` → **Board configuration** (vedi la tabella sopra), oppure
direttamente in `sdkconfig.defaults`.

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

Le combinazioni di strapping sono:

| Chip | Pin del tasto | Condizione di download mode | Combinazione non valida |
|---|---|---|---|
| ESP32-C6 | GPIO9 | GPIO9 basso al reset | GPIO8 basso **e** GPIO9 basso |
| ESP32-C5 | GPIO28 | GPIO28 basso al reset | GPIO27 basso **e** GPIO28 basso |

Quindi premi BOOT **dopo** che la scheda si è avviata. Se ti serve resettare
mentre lo tieni premuto, rilascia e premi **RESET** (EN) per tornare
all'esecuzione normale.

---

## Troubleshooting

| Sintomo | Causa / Soluzione |
|---|---|
| Nessuna porta COM appare | Il cavo USB-C è di sola ricarica. Usa un cavo dati. |
| `idf.py` non trovato | Non hai attivato l'ambiente: esegui il dot-source dello script `Microsoft.*.PowerShell_profile.ps1` (oppure usa `scripts\flash.ps1`). |
| Flash ok ma niente output | Verifica in `sdkconfig` che `CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG=y`. |
| `#error` sulla scheda in fase di build | La scheda in `menuconfig` non corrisponde al target. Esegui il comando `idf.py set-target` indicato nel messaggio. |
| `This chip is ESP32-xx, not ESP32-yy` | Il firmware è compilato per un chip diverso da quello collegato. Verifica il modello con `python -m esptool -p COMx chip-id`. |
| `waiting for download` all'avvio | Hai tenuto premuto BOOT durante il reset: premi RESET (EN) senza toccare BOOT. |
| Più `Hello World` per una pressione | Contatto rimbalzante: aumenta `CONFIG_DEBOUNCE_SAMPLES` a 4–5. |
| Il monitor non risponde a Ctrl+C | Per uscire dal monitor si usa **Ctrl + ]**. |
| `Could not detect the ESP port` | Passa la porta manualmente con `-Port COMx`. |

---

## Verifica rapida

1. `.\scripts\flash.ps1` → compila, flasha e apre il monitor.
2. Premi BOOT una volta → **una** riga `Hello World`.
3. Tieni BOOT premuto per qualche secondo → **nessuna** riga aggiuntiva.
4. Rilascia e premi di nuovo → **una** riga in più.
5. `.\scripts\flash.ps1 -Action size` → firmware molto al di sotto dei limiti
   della partizione (circa 160 KB su 1,5 MB disponibili).

---

## Licenza

MIT — vedi l'intestazione SPDX nei file sorgente.
