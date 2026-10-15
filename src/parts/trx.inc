// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 EA4ILN
// Chispas, parte de main.cpp: pantalla TRX (OPERAR, QSO, memorias, editor) y recepcion LoRa.
// Se incluye desde main.cpp en su orden (una sola unidad de compilacion); no compila suelta.

// ============================================================================
// PANTALLA TRX: OPERAR (KEY), QSO, MEMORIAS Y EDITOR
// ============================================================================
bool palaPulsada() {
  return digitalRead(KEYER_PADDLE_DIT) == LOW || digitalRead(KEYER_PADDLE_DAH) == LOW;
}

// Sustituye {MI}, {SU} y {WX} (si no hay corresponsal o sensor, se quedan en nada). Cabe en el motor CW (64)
String trxExpandir(const char* memoria) {
  String t = memoria;
  t.replace("{MI}", indicativoEstacion);
  t.replace("{SU}", trxSu);
  t.replace("{WX}", textoWx());
  while (t.indexOf("  ") >= 0) t.replace("  ", " ");
  t.trim();
  if (t.length() > MOTOR_CW_TEXTO_MAX) t = t.substring(0, MOTOR_CW_TEXTO_MAX);
  return t;
}

/**
 * Deja un texto con lo que se puede emitir: mayusculas, caracteres con codigo Morse, espacios y las
 * variables {MI}, {SU} y {WX} (si se permiten). Sirve para el portal y para lo que llega de las palas.
 */
String trxLimpiarTexto(String t, bool conVariables, uint8_t maximo) {
  t.toUpperCase();
  String r = "";
  for (unsigned int i = 0; i < t.length() && r.length() < maximo; i++) {
    if (conVariables && (t.startsWith("{MI}", i) || t.startsWith("{SU}", i) || t.startsWith("{WX}", i))) {
      if (r.length() + 4 > maximo) break;
      r += t.substring(i, i + 4);
      i += 3;
    } else if (t[i] == ' ') {
      if (r.length() > 0 && !r.endsWith(" ")) r += ' ';
    } else if (*obtenerCodigoMorse(t[i]) != '\0') {
      r += t[i];
    }
  }
  r.trim();
  return r;
}

// OPERAR, QSO o sus ajustes: las palas manipulan la emisora
bool trxManipulando() {
  return subTrx == TRX_OPERAR || subTrx == TRX_QSO || subTrx == TRX_AJUSTES;
}

// Habilita la salida KEY (OPERAR y QSO) y pone el keyer en marcha
void trxHabilitarKey() {
  keyCortadaPorSeguridad = false;
  keyHabilitada = true;
  keyerReanudar();
}

void trxDeshabilitarKey() {
  motorCWDetener();
  keyerPausar();
  keyHabilitada = false;
  digitalWrite(KEY_OUT_PIN, LOW);
  keyActivaDesdeMs = 0;
  trxEnviandoMemoria = false;
  trxSintonizando = false;
}

// Tune: portadora de trxTuneSeg segundos por KEY. Pulsar otra vez o tocar una pala la corta
void trxTune() {
  if (trxSintonizando) {
    motorCWDetener();  // procesarTrx() lo da por acabado y reanuda el keyer
    return;
  }
  if (!keyHabilitada) return;  // Tras un corte de seguridad, no
  keyerPausar();
  trxEnviandoMemoria = false;
  trxSintonizando = true;
  motorCWPortadora(trxTuneSeg * 1000, "TUNE");
  Serial.printf("[TRX] TUNE %u s\n", trxTuneSeg);
}

void trxEntrarSub(SubTrx sub) {
  subTrx = sub;
  editandoValor = false;
  switch (sub) {
    case TRX_DECOD:
      opcionTrxSub = 1;  // menu
      decodEnAjustes = false;
      trxTextoRx = "";
      decodEmpezar();
      break;
    case TRX_OPERAR:
      opcionTrxSub = 0;  // mem
      trxTextoTx = "";
      trxHabilitarKey();
      break;
    case TRX_QSO:
      opcionTrxSub = 0;  // mem
      trxTextoRx = trxTextoTx = "";
      decodEmpezar();
      trxHabilitarKey();
      break;
    case TRX_MEMORIAS:
      trxMemoriaSel = 0;
      break;
    default:
      break;
  }
}

// De una subpantalla al menu TRX: todo apagado y ajustes guardados
void trxSalirAlMenu() {
  trxDeshabilitarKey();
  decodParar();
  guardarTrxNVS();
  subTrx = TRX_MENU;
  editandoValor = false;
}

// Lista de memorias para enviar (desde OPERAR o QSO): la emision por KEY sigue habilitada, el keyer en pausa
void trxAbrirListaEnvio() {
  subTrxOrigen = subTrx;
  subTrx = TRX_LISTA_ENVIO;
  trxMemoriaSel = 0;
  keyerPausar();
}

void trxVolverDeLista() {
  subTrx = subTrxOrigen;
  if (!trxEnviandoMemoria) keyerReanudar();
}

void trxEnviarMemoria(uint8_t i) {
  String texto = trxExpandir(trxMemorias[i]);
  if (texto.length() == 0) return;
  subTrx = subTrxOrigen;
  keyerPausar();
  qsoAnadir(trxTextoTx, texto);
  trxSintonizando = false;
  trxEnviandoMemoria = true;
  motorCWEmitir(texto.c_str(), trxWpm, 100, "MEM");
  Serial.printf("[TRX] MEMORIA %u: %s\n", i + 1, texto.c_str());
}

// Editor: memoria (0..5) o indicativo del corresponsal (-1). Desde OPERAR/QSO la KEY se deshabilita mientras
void trxAbrirEditor(int8_t destino) {
  subTrxOrigen = subTrx;
  if (keyHabilitada) {
    motorCWDetener();
    trxEnviandoMemoria = false;
    trxSintonizando = false;
    keyHabilitada = false;
  }
  editorDestino = destino;
  editorTexto = destino < 0 ? String(trxSu) : String(trxMemorias[destino]);
  editorMax = destino < 0 ? TRAMA_LONG_INDICATIVO : TRX_MEMORIA_MAX;
  editorSel = 0;
  subTrx = TRX_EDITOR;
  keyerReanudar();  // Tambien se escribe con las palas (sin KEY)
}

void trxCerrarEditor(bool guardar) {
  if (guardar) {
    if (editorDestino < 0) {
      strlcpy(trxSu, trxLimpiarTexto(editorTexto, false, TRAMA_LONG_INDICATIVO).c_str(), sizeof(trxSu));
    } else {
      strlcpy(trxMemorias[editorDestino], trxLimpiarTexto(editorTexto, true, TRX_MEMORIA_MAX).c_str(),
              sizeof(trxMemorias[editorDestino]));
      guardarMemoriaNVS(editorDestino);
    }
  }
  subTrx = subTrxOrigen;
  if (trxManipulando()) trxHabilitarKey();
  else keyerPausar();
}

// Rueda de caracteres del editor: A-Z, 0-9, signos y acciones
const char* const EDITOR_ESPECIALES[] = { ".", ",", "?", "/", "=", "ESP", "{MI}", "{SU}", "{WX}", "<-", "CLR", "OK",
                                          "ESC" };
#define EDITOR_NUM_ESPECIALES (sizeof(EDITOR_ESPECIALES) / sizeof(EDITOR_ESPECIALES[0]))
#define EDITOR_TAM_RUEDA (36 + EDITOR_NUM_ESPECIALES)

String editorElemento(uint8_t i) {
  if (i < 26) return String((char)('A' + i));
  if (i < 36) return String((char)('0' + i - 26));
  return String(EDITOR_ESPECIALES[i - 36]);
}

void editorAnadir(const String& s) {
  if (editorTexto.length() + s.length() <= editorMax) editorTexto += s;
}

void confCerrarEditor(bool guardar);  // Definida con la pantalla CONF

// El editor sirve a TRX (memorias y SU) y a CONF (ID y locator de la estacion)
void editorCerrar(bool guardar) {
  if (editorDestino <= EDITOR_ID_ESTACION) confCerrarEditor(guardar);
  else trxCerrarEditor(guardar);
}

void editorAplicar() {
  String e = editorElemento(editorSel);
  editorError = false;
  if (e == "OK") { editorCerrar(true); return; }
  if (e == "ESC") { editorCerrar(false); return; }
  if (e == "CLR") { editorTexto = ""; return; }
  // ID y locator: solo letras y cifras (y "/" en el ID); sin espacios ni signos
  if (editorDestino <= EDITOR_ID_ESTACION && e.length() == 1 && !isalnum(e[0]) &&
      !(e == "/" && editorDestino == EDITOR_ID_ESTACION)) return;
  if (editorDestino <= EDITOR_ID_ESTACION && e == "ESP") return;
  if (e == "<-") {
    if (editorTexto.endsWith("{MI}") || editorTexto.endsWith("{SU}") || editorTexto.endsWith("{WX}")) {
      editorTexto.remove(editorTexto.length() - 4);
    }
    else if (editorTexto.length() > 0) editorTexto.remove(editorTexto.length() - 1);
    return;
  }
  if (e == "ESP") { if (editorTexto.length() > 0 && !editorTexto.endsWith(" ")) editorAnadir(" "); return; }
  if (e.startsWith("{") && editorDestino < 0) return;  // El indicativo no lleva variables
  editorAnadir(e);
}

// Tono del decodificador: AUTO (0) y luego 400..1000 Hz de 50 en 50
void ajustarTonoDecod(int8_t giro) {
  int v = trxTonoDecodHz;
  if (giro > 0) v = (v == 0) ? TONO_MIN_HZ : v + TONO_PASO_HZ;
  else v = (v <= TONO_MIN_HZ) ? 0 : v - TONO_PASO_HZ;
  if (v > TONO_MAX_HZ) v = TONO_MAX_HZ;
  trxTonoDecodHz = v;
}

// Lo descodificado por el keyer en la pantalla TRX: al editor, o a lo manipulado en OPERAR/QSO
void trxCaracterManipulado(char c) {
  if (subTrx == TRX_EDITOR) {
    if (c == ' ') { if (editorTexto.length() > 0 && !editorTexto.endsWith(" ")) editorAnadir(" "); }
    else if (c != '*') editorAnadir(String(c));
  } else if (trxManipulando()) {
    if (c == ' ') { if (trxTextoTx.length() > 0 && !trxTextoTx.endsWith(" ")) trxTextoTx += ' '; }
    else trxTextoTx += c;
    if (trxTextoTx.length() > QSO_TEXTO_MAX) trxTextoTx.remove(0, trxTextoTx.length() - QSO_TEXTO_MAX);
  }
  pantallaPendiente = true;
}

/**
 * Cada vuelta de loop() en la pantalla TRX: lo descodificado, la barra de nivel, el fin (o el corte con
 * las palas) de una memoria y el corte de seguridad de KEY.
 */
void procesarTrx() {
  // Corte de seguridad: KEY activa mas de 10 s seguidos (pala atascada, fallo...). Se lee una sola vez:
  // el keyer y el motor CW (mas prioridad) la cambian en cada elemento y, leida dos veces, podia pasar
  // a 0 entre las dos lecturas (millis() - 0 = siempre mas de 10 s: cortaba al manipular)
  // Con el tune, el limite es su duracion mas un margen (la portadora la apaga el motor CW a su hora)
  unsigned long keyDesde = keyActivaDesdeMs;
  long limiteKey = trxSintonizando ? (long)trxTuneSeg * 1000 + TRX_TUNE_MARGEN_MS : (long)KEY_MAX_CONTINUA_MS;
  if (keyDesde != 0 && (long)(millis() - keyDesde) > limiteKey) {  // Con signo, por si acaso
    motorCWDetener();
    keyerPausar();
    keyHabilitada = false;
    digitalWrite(KEY_OUT_PIN, LOW);
    keyActivaDesdeMs = 0;
    trxEnviandoMemoria = false;
    trxSintonizando = false;
    keyCortadaPorSeguridad = true;
    Serial.printf("[TRX] KEY CORTADA: MAS DE %ld MS ACTIVA (desde %lu, ahora %lu)\n", limiteKey, keyDesde, millis());
    pantallaPendiente = true;
  }

  if (pantallaActual != PANTALLA_TRX) return;

  char c;
  while (colaDecod != NULL && xQueueReceive(colaDecod, &c, 0) == pdTRUE) {
    Serial.print(c);
    if (c == ' ') { if (trxTextoRx.length() > 0 && !trxTextoRx.endsWith(" ")) trxTextoRx += ' '; }
    else trxTextoRx += c;
    if (trxTextoRx.length() > QSO_TEXTO_MAX) trxTextoRx.remove(0, trxTextoRx.length() - QSO_TEXTO_MAX);
    pantallaPendiente = true;
  }

  // La barra de nivel se mueve sola: se redibuja 5 veces por segundo
  static unsigned long ultimoRefresco = 0;
  if ((subTrx == TRX_DECOD || subTrx == TRX_QSO) && millis() - ultimoRefresco >= 200) {
    ultimoRefresco = millis();
    pantallaPendiente = true;
  }

  // Para ajustar RV2 en banco
  static unsigned long ultimoDiag = 0;
  if (depuActiva(DEPU_DECOD) && decodActivo && millis() - ultimoDiag >= 2000) {
    ultimoDiag = millis();
    Serial.printf("[DECOD] %u Hz, nivel %u%%, umbral %u%%, %u wpm%s\n", decodFrecHz, decodNivelPct, decodUmbralPct,
                  decodWpm, decodSaturado ? ", SATURA" : "");
  }

  if (trxEnviandoMemoria || trxSintonizando) {
    if (motorCWOcupado() && palaPulsada()) motorCWDetener();  // Break-in: las palas cortan la memoria o el tune
    if (!motorCWOcupado()) {
      if (trxSintonizando) Serial.println(F("[TRX] TUNE FIN"));
      trxEnviandoMemoria = false;
      trxSintonizando = false;
      pantallaPendiente = true;
      if (trxManipulando()) keyerReanudar();
    }
  }
}

void procesarRecepcionLoRa() {
  if (!flagPaqueteRecibido) return;
  flagPaqueteRecibido = false;

  // Fuera del modo LoRa se descarta cualquier aviso pendiente
  if (!modoRadioRxActivo) return;

  // Cabecera y al menos un caracter, sin pasar del maximo
  size_t longitud = radio.getPacketLength();
  if (longitud <= TRAMA_LONG_CABECERA || longitud > TRAMA_LONG_MAX) {
    Serial.printf("[LORA RX] LONGITUD NO VALIDA: %u\n", (unsigned)longitud);
    radio.startReceive();
    return;
  }

  uint8_t trama[TRAMA_LONG_MAX];
  int state = radio.readData(trama, longitud);

  if (state == RADIOLIB_ERR_NONE && trama[0] == 'M' && trama[1] == 'M' &&
      (trama[2] >> 4) == TRAMA_VERSION) {
    rssiUltimo = radio.getRSSI();
    pantallaPendiente = true;

    uint8_t tipo = trama[2] & 0x0F;
    uint8_t seq = trama[3];
    char indicativo[TRAMA_LONG_INDICATIVO + 1];
    memcpy(indicativo, &trama[5], TRAMA_LONG_INDICATIVO);
    indicativo[TRAMA_LONG_INDICATIVO] = '\0';

    // Duplicado: mismo emisor y mismo seq que la trama anterior
    static bool haySeqRx = false;
    static uint8_t ultimoSeqRx = 0;
    static char ultimoIndicativoRx[TRAMA_LONG_INDICATIVO + 1] = "";
    bool duplicada = haySeqRx && seq == ultimoSeqRx && strcmp(indicativo, ultimoIndicativoRx) == 0;
    haySeqRx = true;
    ultimoSeqRx = seq;
    strcpy(ultimoIndicativoRx, indicativo);

    String texto = "";
    for (size_t i = TRAMA_LONG_CABECERA; i < longitud; i++) texto += decodificarCaracterMorse(trama[i]);
    texto.trim();

    Serial.printf("[LORA RX] %s seq %u wpm %u%s: ", indicativo, seq, trama[4], duplicada ? " (DUPLICADA)" : "");
    Serial.println(texto);

    if (!duplicada && tipo == TRAMA_TIPO_GRUPO_DUO && enPantalla4_Sub && subpantalla4Activa == 1 &&
        texto.length() > 0) {
      // Se reproduce a los wpm del emisor; si el valor no es razonable, a los locales
      uint8_t wpmRx = trama[4];
      if (wpmRx < WPM_MIN_DUO || wpmRx > WPM_MAX_DUO) wpmRx = wpmCW;
      encolarGrupoRxDuo(texto.c_str(), wpmRx);
    } else if (!duplicada && tipo == TRAMA_TIPO_TEXTO_QSO && enQSO() && texto.length() > 0) {
      uint8_t wpmRx = trama[4];
      if (wpmRx < WPM_MIN_DUO || wpmRx > WPM_MAX_DUO) wpmRx = keyerWpm;
      strlcpy(qsoIndicativoRx, indicativo, sizeof(qsoIndicativoRx));
      encolarGrupoRxDuo(texto.c_str(), wpmRx);
    }
  }

  radio.startReceive();
}

/**
 * Despierta la radio (duerme fuera de Duo y QSO) y la pone a escuchar. Con el SX1262 dormido, BUSY
 * esta en alto y startReceive() se queda esperando: hay que despertarlo antes bajando NSS, que es lo
 * que hace standby(..., true). Sin esto, la placa que solo escuchaba no recibia (probado).
 */
void loraEscuchar() {
  int estado = radio.standby(RADIOLIB_SX126X_STANDBY_RC, true);
  if (estado == RADIOLIB_ERR_NONE) estado = radio.startReceive();
  if (estado != RADIOLIB_ERR_NONE) Serial.printf("[LORA] ERROR AL DESPERTAR LA RADIO: %d\n", estado);
}

/**
 * Sale de las subpantallas LoRa (Duo / QSO): desactiva la recepcion, corta el tono que pudiera estar sonando
 * y limpia el estado, para que un paquete que llegue despues no suene encima de otras pantallas
 */
void salirModoLoRa() {
  if (subpantalla4Activa == 2) {
    qsoEnviarPalabra();  // Lo que quedara a medias
    keyerPausar();
    guardarKeyerNVS();   // wpm y VER del QSO
    qsoSonando = false;
  }
  enPantalla4_Sub = false;
  subpantalla4Activa = 0;
  loraEnAjustes = false;
  editandoValor = false;
  modoRadioRxActivo = false;
  radio.sleep();  // Ahorro: fuera de Duo y QSO la radio duerme (sleep templado: conserva la configuracion)
  flagPaqueteRecibido = false;

  emitiendoMorseDuo = false;
  motorCWDetener();

  colaRxIni = 0;
  colaRxNum = 0;
  grupoDuoRecibido = "";
  grupoDuoTransmitido = "";
  grupoDuoRevelado = false;
}

