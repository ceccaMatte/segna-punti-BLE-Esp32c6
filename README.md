# Playmaker wearable · Wi-Fi LAN

Questo branch usa Wi-Fi al posto di BLE. L'ESP32-C3 serve direttamente il tabellone
padel e una pagina di configurazione per scegliere e memorizzare la rete.

1. Collegati all'AP `PLAYMAKER_XXXX` (password `playmaker`).
2. Apri `http://192.168.4.1/settings`, scegli la rete a 2,4 GHz e inserisci la password.
3. Dopo il collegamento, riporta il PC sulla stessa LAN e apri l'IP indicato dalla pagina.

L'ultima rete collegata con successo viene salvata in NVS. Se non è disponibile,
il wearable prova automaticamente la rete di riserva `Se` con password `ciao1234`.

Il tabellone si collega automaticamente, conferma i comandi dei pulsanti e salva
la partita nel browser. Restano disponibili gesture programmabili, suoni, Undo e
risparmio energetico. Bluetooth è disabilitato nel firmware di questo branch.

Vedi [procedura completa, API e limiti](docs/wifi.md).

Per la seconda ESP usata come ricevitore USB su Ubuntu Server, vedi il
[contratto API USB v1 e modalità diretta/LAN](docs/receiver-usb-api.md).
Il firmware è nel [progetto separato receiver/](receiver/README.md), che riusa
protocollo e rete del wearable. Per usarlo vanno programmati il ricevitore e il
wearable aggiornato con discovery; il programma Ubuntu è a cura del socio.

## Build e test

```sh
idf.py build
idf.py -p COM26 flash
node test/host/test_wifi_web.cjs
```

Pin del prototipo: A=GPIO3, B=GPIO0, Moment=GPIO1, Undo=GPIO2; buzzer=GPIO5.
Il firmware richiede ESP-IDF 6.0.2 e flash da 4 MiB con la tabella partizioni inclusa.

La grafica del tabellone e della configurazione riprende
[Playmaker Italia](https://playmakeritalia.it/); le risorse sono integrate nell'ESP
per funzionare anche senza Internet. Il contenuto dei precedenti documenti BLE
rimane un riferimento storico del formato binario, non della connessione attiva.
