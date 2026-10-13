// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 EA4ILN
// Chispas, parte de main.cpp: TX/RX LoRa por interrupcion, Duo y QSO.
// Se incluye desde main.cpp en su orden (una sola unidad de compilacion); no compila suelta.

// ============================================================================
// TRANSMISION Y RECEPCION LORA POR INTERRUPCION
// ============================================================================
void iniciarEmisionLocalDuo(String cadena, uint8_t wpm) {
  cadena.trim();
  if (cadena.length() == 0) return;

  wpmEmisionDuo = wpm;
  motorCWEmitir(cadena.c_str(), wpm, espaciadoCW, "DUO");
  emitiendoMorseDuo = true;
}

/**
 * Cuando el motor CW acaba el grupo de Duo, guarda el instante (para el espacio hasta el siguiente).
 */
void procesarFinEmisionDuo() {
  if (!emitiendoMorseDuo || motorCWOcupado()) return;
  emitiendoMorseDuo = false;
  finEmisionDuo = motorCWFinMs;
  // No se reinicia la recepcion: la radio ya vuelve a ella tras cada TX y RX, y
  // startReceive() aqui podria cortar una trama que estuviera llegando
}

void procesarRecepcionLoRa();  // Definida mas abajo

/**
 * Antes de transmitir: procesa una trama que haya llegado y aun no se haya leido (transmit()
 * machacaria el buffer) y escucha el canal (CAD). Si la otra placa esta emitiendo, se pone en
 * recepcion hasta que llegue su trama y vuelve a probar.
 */
void prepararCanalParaTransmitir() {
  if (flagPaqueteRecibido) procesarRecepcionLoRa();

  for (uint8_t intento = 0; intento < LORA_CAD_INTENTOS; intento++) {
    int estadoCad = radio.scanChannel();
    flagPaqueteRecibido = false;  // El fin del CAD tambien activa DIO1
    if (estadoCad != RADIOLIB_LORA_DETECTED) return;  // Libre (o error de CAD): se transmite

    Serial.println(F("[LORA TX] CANAL OCUPADO, ESPERANDO TRAMA"));
    radio.startReceive();
    unsigned long inicio = millis();
    while (!flagPaqueteRecibido && millis() - inicio < LORA_ESPERA_OCUPADO_MS) yield();
    if (flagPaqueteRecibido) procesarRecepcionLoRa();
  }
  Serial.println(F("[LORA TX] CANAL SIGUE OCUPADO, SE TRANSMITE IGUAL"));
}

/**
 * Arma una trama v2 del tipo indicado (grupo de Duo o palabra de QSO) y la transmite.
 * Devuelve false si el texto no tiene ningun caracter Morse valido (no se transmite nada).
 */
bool transmitirTextoLoRa(uint8_t tipo, uint8_t wpm, const String& texto) {
  uint8_t trama[TRAMA_LONG_MAX];
  trama[0] = 'M';
  trama[1] = 'M';
  trama[2] = (TRAMA_VERSION << 4) | tipo;
  trama[4] = wpm;
  memset(&trama[5], 0, TRAMA_LONG_INDICATIVO);
  memcpy(&trama[5], indicativoEstacion, min(strlen(indicativoEstacion), (size_t)TRAMA_LONG_INDICATIVO));

  size_t longitud = TRAMA_LONG_CABECERA;
  for (size_t i = 0; i < texto.length() && longitud < TRAMA_LONG_MAX; i++) {
    uint8_t b = codificarCaracterMorse(texto.charAt(i));
    if (b != MORSE_NO_VALIDO) trama[longitud++] = b;
  }
  if (longitud == TRAMA_LONG_CABECERA) return false;  // Nada que enviar

  prepararCanalParaTransmitir();
  trama[3] = seqTx++;

  Serial.printf("[LORA TX] tipo %u seq %u, %u bytes: ", tipo, trama[3], (unsigned)longitud);
  Serial.println(texto);

  int estadoTx = radio.transmit(trama, longitud);
  
  if (estadoTx == RADIOLIB_ERR_NONE) {
    Serial.println(F("[LORA TX] PAQUETE ENVIADO CON EXITO!"));
  } else {
    Serial.print(F("[LORA TX] ERROR RADIOLIB: "));
    Serial.println(estadoTx);
  }

  radio.startReceive();

  // En el SX1262 el mismo pin DIO1 señala "TX terminado" y "paquete recibido",
  // asi que la ISR se dispara al acabar de transmitir. Se descarta ese aviso falso
  // (esto sustituye al antiguo filtro de "eco" por comparacion de texto).
  flagPaqueteRecibido = false;
  return true;
}

void transmitirGrupoDuoLoRa(String grupo) {
  grupoDuoTransmitido = grupo;
  if (!transmitirTextoLoRa(TRAMA_TIPO_GRUPO_DUO, wpmCW, grupo)) return;
  iniciarEmisionLocalDuo(grupo, wpmCW);
}

/**
 * Guarda un grupo recibido y los wpm a los que hay que reproducirlo.
 * Si la cola esta llena se descarta el grupo nuevo.
 */
void encolarGrupoRxDuo(const char* grupo, uint8_t wpm) {
  if (colaRxNum >= COLA_RX_TAM) {
    Serial.println(F("[LORA RX] COLA LLENA, GRUPO DESCARTADO"));
    return;
  }
  uint8_t pos = (colaRxIni + colaRxNum) % COLA_RX_TAM;
  strncpy(colaRxDuo[pos], grupo, TRAMA_MAX_CARACTERES);
  colaRxDuo[pos][TRAMA_MAX_CARACTERES] = '\0';
  colaRxWpm[pos] = wpm;
  colaRxNum++;
}

void actualizarPantalla();  // Definida mas abajo

/**
 * Si no suena nada y hay grupos en cola, reproduce el siguiente tras el espacio entre palabras.
 */
void procesarColaRxDuo() {
  if (subpantalla4Activa != 1 || emitiendoMorseDuo || colaRxNum == 0) return;  // En QSO la vacia procesarQSO()

  uint32_t tDit = 1200 / wpmEmisionDuo;  // Pausa a la velocidad del grupo que acaba de sonar
  uint32_t tWordGap = (tDit * 7 * espaciadoCW) / 100;
  if (millis() - finEmisionDuo < tWordGap) return;

  grupoDuoRecibido = String(colaRxDuo[colaRxIni]);
  grupoDuoRevelado = false;
  uint8_t wpm = colaRxWpm[colaRxIni];
  colaRxIni = (colaRxIni + 1) % COLA_RX_TAM;
  colaRxNum--;

  // Se redibuja antes de empezar, para que el grupo nuevo aparezca a la vez que suena
  actualizarPantalla();
  pantallaPendiente = false;
  iniciarEmisionLocalDuo(grupoDuoRecibido, wpm);
}

// ============================================================================
// QSO POR LORA (PALAS)
// ============================================================================
void loraEscuchar();  // Definida mas abajo

bool enQSO() {
  return pantallaActual == PANTALLA_LORA && enPantalla4_Sub && subpantalla4Activa == 2;
}

// Anade una palabra al texto (con espacio delante) y recorta por el principio si se pasa del maximo
void qsoAnadir(String& texto, const String& palabra) {
  if (palabra.length() == 0) return;
  if (texto.length() > 0 && !texto.endsWith(" ")) texto += ' ';
  texto += palabra;
  if (texto.length() > QSO_TEXTO_MAX) texto.remove(0, texto.length() - QSO_TEXTO_MAX);
}

void qsoEnviarPalabra() {
  if (qsoPalabraTx.length() == 0) return;
  transmitirTextoLoRa(TRAMA_TIPO_TEXTO_QSO, keyerWpm, qsoPalabraTx);
  qsoPalabraTx = "";
}

// Un caracter descodificado por el keyer en el QSO: al llegar el espacio entre palabras se envia la palabra
void qsoCaracterManipulado(char c) {
  if (c == ' ') {
    qsoEnviarPalabra();
    if (qsoTextoTx.length() > 0 && !qsoTextoTx.endsWith(" ")) qsoTextoTx += ' ';
  } else {
    qsoPalabraTx += c;
    qsoTextoTx += c;
    if (qsoPalabraTx.length() >= TRAMA_MAX_CARACTERES) qsoEnviarPalabra();  // No cabe mas en una trama
  }
  if (qsoTextoTx.length() > QSO_TEXTO_MAX) qsoTextoTx.remove(0, qsoTextoTx.length() - QSO_TEXTO_MAX);
  pantallaPendiente = true;
}

void entrarEnQSO() {
  enPantalla4_Sub = true;
  subpantalla4Activa = 2;
  modoRadioRxActivo = true;
  opcionQSO = 1;
  loraEnAjustes = false;
  qsoSonando = false;
  qsoPalabraSonando = qsoTextoRx = qsoTextoTx = qsoPalabraTx = "";
  qsoIndicativoRx[0] = '\0';
  colaRxIni = colaRxNum = 0;
  loraEscuchar();  // La radio duerme fuera de Duo y QSO
  keyerReanudar();  // Las palas funcionan con cualquier foco
}

/**
 * Reproduce lo recibido en el QSO: una palabra cada vez, cuando se lleva un silencio de palabra sin
 * manipular (el keyer queda en pausa mientras suena). Si se toca una pala mientras suena, se corta y lo
 * que quedaba pasa al texto sin sonar.
 */
void procesarQSO() {
  if (!enQSO()) return;

  if (qsoSonando) {
    bool palaPulsada = digitalRead(KEYER_PADDLE_DIT) == LOW || digitalRead(KEYER_PADDLE_DAH) == LOW;
    if (motorCWOcupado() && !palaPulsada) return;
    if (motorCWOcupado()) {
      motorCWDetener();  // Break-in
      if (!qsoVerRx) qsoAnadir(qsoTextoRx, qsoPalabraSonando);
      while (colaRxNum > 0) {
        qsoAnadir(qsoTextoRx, String(colaRxDuo[colaRxIni]));
        colaRxIni = (colaRxIni + 1) % COLA_RX_TAM;
        colaRxNum--;
      }
      finEmisionDuo = millis();
    } else {
      if (!qsoVerRx) qsoAnadir(qsoTextoRx, qsoPalabraSonando);  // Con VER NO se ve al acabar de sonar
      finEmisionDuo = motorCWFinMs;
    }
    qsoSonando = false;
    qsoPalabraSonando = "";
    keyerReanudar();
    pantallaPendiente = true;
    return;
  }

  if (colaRxNum == 0) return;
  // Silencio de palabra (7 puntos) sin manipular y espacio entre palabras recibidas
  if (millis() - keyerUltimoUsoMs < 8400UL / keyerWpm) return;
  if (millis() - finEmisionDuo < 8400UL / wpmEmisionDuo) return;

  qsoPalabraSonando = String(colaRxDuo[colaRxIni]);
  wpmEmisionDuo = colaRxWpm[colaRxIni];
  colaRxIni = (colaRxIni + 1) % COLA_RX_TAM;
  colaRxNum--;
  if (qsoVerRx) qsoAnadir(qsoTextoRx, qsoPalabraSonando);

  keyerPausar();
  qsoSonando = true;
  actualizarPantalla();  // Antes de que suene, como en el Duo
  pantallaPendiente = false;
  motorCWEmitir(qsoPalabraSonando.c_str(), wpmEmisionDuo, 100, "QSO");
}

