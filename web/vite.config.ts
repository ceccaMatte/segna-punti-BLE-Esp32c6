/// <reference types="vitest" />
import { defineConfig } from 'vite';

/*
 * Non c'e' nessun backend: la pagina parla direttamente con la scheda via
 * Bluetooth. Questo file serve solo a compilare e a far girare i test.
 *
 * Web Bluetooth richiede un contesto sicuro: in sviluppo basta localhost, in
 * produzione serve HTTPS. Non e' una scelta, e' una regola del browser.
 */
export default defineConfig({
  server: {
    host: 'localhost',
    port: 5173,
  },
  build: {
    target: 'es2022',
    outDir: 'dist',
  },
  test: {
    environment: 'node',
    include: ['test/**/*.test.ts'],
  },
});
