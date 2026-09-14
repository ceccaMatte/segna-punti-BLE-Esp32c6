/**
 * @file main.c
 * @brief Punto di ingresso: due righe, e nessuna decisione.
 *
 * Quello che il programma fa sta tutto in ``app/``, diviso per operazione:
 * ``app.h`` ha la mappa. Qui non c'e' niente da leggere, e non e' una
 * formalita': un file d'ingresso che prende decisioni e' il posto dove le
 * decisioni si nascondono, e questo progetto preferisce che si vedano.
 *
 * SPDX-License-Identifier: MIT
 */

#include "app.h"

void app_main(void)
{
    app_init();
    app_run();
}
