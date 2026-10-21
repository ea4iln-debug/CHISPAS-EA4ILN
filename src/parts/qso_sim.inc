// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 EA4ILN
// Chispas, parte de main.cpp: simulador de QSO POTA (CW > qso sim).
// Se incluye desde main.cpp en su orden (una sola unidad de compilacion); no compila suelta.

// ============================================================================
// SIMULADOR DE QSO: CHISPAS HACE DE ACTIVADOR POTA Y TU, DE CAZADOR, CON LAS PALAS
// ============================================================================
// Tres turnos, como un QSO POTA corto:
//   LLAMADA:    "CQ POTA DE EA3XYZ/P EA3XYZ/P K"  -> tu indicativo (una o dos veces)
//   INFORME:    "EA4ILN GM 5NN 5NN BK"            -> "BK TU 5NN 5NN 73 BK" (basta con el 5NN o 599)
//   DESPEDIDA:  "TU ES0072 73 E E"                 -> "E E" (opcional) y resultado
// Con QSB, el volumen del activador sube y baja (simNivelQsb); con RUIDO, TORMENTA y QRM se oye la banda
// Lo manipulado se compara sin espacios (una pausa larga parte el indicativo) y solo se buscan las palabras
// clave: para empezar a manipular basta. "AGN" o "?" piden que repita. Si no te entiende, pide lo que falta
//
// Con ROL ACTIVADOR se cambian los papeles: tu llamas y Chispas es el cazador (ver "Rol activador", abajo)
#define SIM_FIN_TURNO_MS     3000   // Silencio que cierra tu turno (o 25 puntos, si es mas)
#define SIM_SIN_RESPUESTA_MS 20000  // Sin manipular nada: vuelve a llamar (o repite)
#define SIM_DESPEDIDA_MS     6000   // En la despedida, sin manipular: se acaba
#define SIM_CAZADOR_TARDE_MS 8000   // Rol activador: si no contestas a su indicativo, el cazador vuelve a llamar

String simIndicativo = "";  // El de Chispas (activador o cazador)
String simParque = "";
String simEmitido = "";     // Lo ultimo que ha emitido Chispas (para "repetir")
String simRespuesta = "";   // Lo manipulado en este turno
unsigned long simInicioTurnoMs = 0, simInicioMs = 0, simDuracionMs = 0;
uint8_t simRepeticiones = 0;  // Veces que el activador no te entendio
uint32_t simWpmReal = 0;      // Velocidad real de tu manipulacion (como en RITMO), al acabar
float simQsbFase1 = 0, simQsbFase2 = 0;  // Al azar en cada QSO
// Rol activador: lo que hace el cazador
bool simCazParcial = false;     // Ha dado solo parte de su indicativo ("EA3"): falta que se lo pidas
bool simCazMalCopiado = false;  // Ha copiado mal tu indicativo: falta que se lo corrijas
bool simCazPidioAgn = false;    // Ya ha pedido "AGN?" una vez en este QSO
bool simCazLlamando = false;    // Lo ultimo que emitio fue su indicativo (si tardas, vuelve a llamar)
bool simAcabarAlEmitir = false; // Su "E E" final: al acabar de sonar, resultado

// QSB: el volumen del activador baja y sube despacio, con dos senos de periodos que no encajan (5,3 y 2,1 s)
// para que no se repita igual. Profundidad: SUAVE, hasta -8 dB; FUERTE, hasta -26 dB (casi desaparece).
// Devuelve el volumen del tono para la sintesis (256 = el maximo)
int32_t simNivelQsb() {
  if (simQsb == QSB_NO) return 256;
  float t = millis() / 1000.0f;
  float f = 0.5f + 0.5f * (0.7f * sinf(2 * PI * t / 5.3f + simQsbFase1) + 0.3f * sinf(2 * PI * t / 2.1f + simQsbFase2));
  float profundidad = (simQsb == QSB_FUERTE) ? 0.95f : 0.6f;
  return lroundf(256.0f * (1.0f - profundidad * f));
}

// La banda durante el QSO (no en los ajustes ni en el resultado). SUAVE = lo elegido de oido en
// pruebas/prueba_audio.cpp (ruido 30, 30 chasquidos por minuto, pitido 10); FUERTE, el doble.
// El pitido de interferencia va 180 Hz por encima del tono y deriva +-60 Hz en 17 s
void simAplicarBanda() {
  bool enQso = enSim && (estadoSim == SIM_EMITIENDO || estadoSim == SIM_ESCUCHANDO);
  audioQsb = (enQso && estadoSim == SIM_EMITIENDO) ? simNivelQsb() : 256;
  audioRuido = enQso ? simRuido * 30 : 0;
  static uint8_t tormentaPuesta = 0;
  uint8_t tormenta = enQso ? simTormenta : 0;
  if (tormenta != tormentaPuesta) {
    tormentaPuesta = tormenta;
    audioPonerChasquidos(tormenta * 30);
  }
  audioInterf = enQso ? simQrm * 10 : 0;
  if (audioInterf > 0) {
    float t = millis() / 1000.0f;
    audioIncInterf = audioIncremento(frecTonoHz + 180 + 60 * sinf(2 * PI * t / 17.0f));
  }
}

// El saludo segun la hora local: GM hasta las 13, GA hasta las 20 y GE despues
const char* simSaludo() {
  struct tm t;
  if (!horaLocalValida(t)) return "GM";
  return t.tm_hour < 13 ? "GM" : (t.tm_hour < 20 ? "GA" : "GE");
}

void simEmitir(const String& texto) {
  simEmitido = texto;
  simRespuesta = "";
  simCazLlamando = false;
  keyerPausar();
  audioQsb = simNivelQsb();  // Ya para el primer elemento (luego lo sigue procesarSim)
  motorCWEmitir(texto.c_str(), simWpm, simEfectiva, "SIM");
  estadoSim = SIM_EMITIENDO;
  pantallaPendiente = true;
}

void simLlamar() {
  simEmitir("CQ POTA DE " + simIndicativo + " " + simIndicativo + " K");
}

void simInforme() {
  simEmitir(String(indicativoEstacion) + " " + simSaludo() + " 5NN 5NN BK");
}

void entrarEnSim() {
  motorCWDetener();
  enSim = true;
  estadoSim = SIM_AJUSTES;
  opcionSim = OPCION_SIM_INICIAR;
}

void iniciarSim() {
  guardarSimNVS();
  guardarKeyerNVS();
  simIndicativo = indicativoAleatorio();
  simParque = referenciaParqueAleatoria();
  simRepeticiones = 0;
  simInicioMs = millis();
  simQsbFase1 = random(628) / 100.0f;
  simQsbFase2 = random(628) / 100.0f;
  faseSim = FASE_LLAMADA;
  opcionPracticaSim = 0;
  ritmoBorrar = true;  // Para la velocidad real del resultado
  simCazParcial = simCazMalCopiado = simCazPidioAgn = simCazLlamando = simAcabarAlEmitir = false;
  if (simRol == SIM_ROL_ACTIVADOR) {
    // El cazador llama desde casa: sin "/P". Empiezas tu con el CQ
    if (simIndicativo.endsWith("/P")) simIndicativo.remove(simIndicativo.length() - 2);
    simParque = "";
    simEmitido = "";
    simRespuesta = "";
    keyerReanudar();
    estadoSim = SIM_ESCUCHANDO;
    simInicioTurnoMs = millis();
    pantallaPendiente = true;
    Serial.printf("\n[SIM] ACTIVADOR: cazador %s\n", simIndicativo.c_str());
    return;
  }
  simLlamar();
}

// Del QSO (o del resultado) a los ajustes
void simAAjustes() {
  keyerPausar();
  motorCWDetener();
  estadoSim = SIM_AJUSTES;
  editandoValor = false;
}

void salirDeSim() {
  simAAjustes();
  enSim = false;
  guardarSimNVS();
  guardarKeyerNVS();
}

void simTerminar() {
  keyerPausar();
  simDuracionMs = millis() - simInicioMs;
  const RitmoKeyer& r = ritmoKeyer;
  simWpmReal = r.msReal > 0 ? (uint32_t)lroundf((float)keyerWpm * r.msIdeal / r.msReal) : 0;
  Serial.printf("\n[SIM] QSO con %s %s: %lus, %u rep., %lu WPM\n", simIndicativo.c_str(),
                simRol == SIM_ROL_ACTIVADOR ? "(cazador)" : simParque.c_str(),
                simDuracionMs / 1000, simRepeticiones, (unsigned long)simWpmReal);
  estadoSim = SIM_FIN;
  opcionPracticaSim = 0;
  pantallaPendiente = true;
}

// Cuantos caracteres del principio de tu indicativo hay al principio de lo manipulado
uint8_t simPrefijoComun(const String& r) {
  const char* mi = indicativoEstacion;
  uint8_t n = 0;
  while (mi[n] && n < r.length() && r[n] == mi[n]) n++;
  return n;
}

// ----------------------------------------------------------------------------
// Rol activador: tu llamas y Chispas es el cazador
// ----------------------------------------------------------------------------
//   LLAMADA:    "CQ POTA DE EA4ILN/P K" (o "QRZ")  -> su indicativo, una o dos veces
//   INFORME:    "EA3XYZ GM 5NN BK" (su indicativo y 5NN o 599) -> "BK TU 5NN 5NN 73 BK"
//   DESPEDIDA:  "TU 73" (cualquier cosa)            -> "E E" y resultado
// Para que no sea siempre igual, a veces el cazador:
//   - da solo el principio de su indicativo ("EA3"): pidelo con "EA3?", "QRZ?" o "AGN" (si no, lo da entero y cuenta)
//   - tarda en oirte: si no contestas a su indicativo en 8 s, vuelve a llamar
//   - copia mal tu indicativo ("BK EA4ILM? 5NN 5NN BK"): mandale el tuyo bien (si no, lo repite y cuenta)
//   - pide "AGN?" tras tu informe (una vez por QSO, no cuenta)
// Si a tu informe le falta su indicativo, lo vuelve a dar; si le falta el 5NN, "RST? BK". Las dos cuentan

// Principio del indicativo hasta su cifra: "EA3XYZ" -> "EA3"
String simIndicativoParcial(const String& ind) {
  for (uint16_t i = 1; i < ind.length(); i++) {
    if (isDigit(ind[i])) return ind.substring(0, i + 1);
  }
  return ind.substring(0, min((unsigned)3, ind.length()));
}

// Tu indicativo con una letra del sufijo cambiada: "EA4ILN" -> "EA4ILM"
String simIndicativoMalCopiado() {
  String m = indicativoEstacion;
  int16_t cifra = -1;
  for (uint16_t i = 1; i < m.length(); i++) if (isDigit(m[i])) cifra = i;
  uint8_t n = 0;
  int16_t pos = -1;
  for (int16_t i = cifra + 1; i < (int16_t)m.length(); i++) {
    if (isAlpha(m[i]) && random(++n) == 0) pos = i;  // Una letra al azar del sufijo
  }
  if (pos < 0) pos = m.length() - 1;
  char c;
  do c = 'A' + random(26); while (c == m[pos]);
  m.setCharAt(pos, c);
  return m;
}

// El cazador da su indicativo (o el principio, si le toca); si tardas, vuelve a llamar
void simCazadorLlama(bool dosVeces) {
  String ind = simCazParcial ? simIndicativoParcial(simIndicativo) : simIndicativo;
  simEmitir(dosVeces ? ind + " " + ind : ind);
  simCazLlamando = true;
}

void simEvaluarActivador(const String& r) {
  bool pide = r.indexOf('?') >= 0 || r.indexOf("AGN") >= 0 || r.indexOf("QRZ") >= 0;

  if (faseSim == FASE_LLAMADA) {
    if (r.indexOf("CQ") >= 0 || r.indexOf("QRZ") >= 0) {
      faseSim = FASE_INFORME;
      simCazParcial = random(100) < 25;
      simCazadorLlama(!simCazParcial && random(2) == 0);
    } else {
      // Sin CQ nadie contesta: sigues llamando
      simRespuesta = "";
      simInicioTurnoMs = millis();
      pantallaPendiente = true;
    }

  } else if (faseSim == FASE_INFORME) {
    if (simCazMalCopiado) {
      if (indicativoEstacion[0] && r.indexOf(indicativoEstacion) >= 0) {
        simCazMalCopiado = false;
        faseSim = FASE_DESPEDIDA;
        simEmitir("R R TU 5NN 5NN 73 BK");
      } else {
        if (!pide) simRepeticiones++;
        simEmitir(simEmitido);
      }
    } else if (simCazParcial) {
      // Se lo pidas o no, ahora lo da entero (si no lo pediste, cuenta)
      if (!pide) simRepeticiones++;
      simCazParcial = false;
      simCazadorLlama(true);
    } else {
      bool suIndicativo = r.indexOf(simIndicativo) >= 0;
      bool informe = r.indexOf("5NN") >= 0 || r.indexOf("599") >= 0;
      if (suIndicativo && informe) {
        uint8_t dado = random(100);
        if (!simCazPidioAgn && dado < 15) {
          simCazPidioAgn = true;
          simEmitir("AGN?");
        } else if (indicativoEstacion[0] && dado < 40) {
          simCazMalCopiado = true;
          simEmitir("BK " + simIndicativoMalCopiado() + "? 5NN 5NN BK");
        } else {
          faseSim = FASE_DESPEDIDA;
          simEmitir("BK TU 5NN 5NN 73 BK");
        }
      } else if (pide && !informe) {
        simCazadorLlama(true);  // "QRZ?", "AGN": no cuenta
      } else {
        simRepeticiones++;
        if (suIndicativo) simEmitir("RST? BK");
        else simCazadorLlama(true);
      }
    }

  } else {  // FASE_DESPEDIDA ("TU 73 QRZ" es despedirse: aqui QRZ no pide nada)
    if (r.indexOf('?') >= 0 || r.indexOf("AGN") >= 0) {
      simEmitir(simEmitido);
    } else {
      simEmitir(random(2) ? "E E" : "73 E E");
      simAcabarAlEmitir = true;
    }
  }
}

// Fin de tu turno: segun la fase y lo manipulado, el activador sigue o pide lo que falta
void simEvaluar() {
  String r = simRespuesta;
  r.replace(" ", "");
  Serial.printf("\n[SIM] %s\n", simRespuesta.c_str());
  if (simRol == SIM_ROL_ACTIVADOR) { simEvaluarActivador(r); return; }

  if (faseSim == FASE_DESPEDIDA) { simTerminar(); return; }
  if (r == "?" || r.indexOf("AGN") >= 0) { simEmitir(simEmitido); return; }  // Lo pides tu: no cuenta

  if (faseSim == FASE_LLAMADA) {
    if (indicativoEstacion[0] && r.indexOf(indicativoEstacion) >= 0) {
      faseSim = FASE_INFORME;
      simInforme();
    } else {
      // Lo que ha copiado y "?" (como un activador de verdad), o "AGN?" si no se parece a tu indicativo
      simRepeticiones++;
      uint8_t n = simPrefijoComun(r);
      simEmitir(n >= 3 ? String(indicativoEstacion).substring(0, n) + "?" : String("AGN?"));
    }
  } else {  // FASE_INFORME
    if (r.indexOf("5NN") >= 0 || r.indexOf("599") >= 0) {
      faseSim = FASE_DESPEDIDA;
      simEmitir("TU " + simParque + " 73 E E");
    } else {
      simRepeticiones++;
      simEmitir("RST? BK");
    }
  }
}

// Un caracter descodificado del keyer durante el QSO
void simRecibirCaracter(char c) {
  if (estadoSim != SIM_ESCUCHANDO) return;
  if (c == ' ' && (simRespuesta.length() == 0 || simRespuesta.endsWith(" "))) return;
  simRespuesta += c;
  if (simRespuesta.length() > 60) simRespuesta.remove(0, simRespuesta.length() - 60);
  pantallaPendiente = true;
}

void procesarSim() {
  // QSB solo mientras emite el activador: tu tono de las palas (y el resto de Chispas) siempre al maximo.
  // Ruido, tormenta y QRM durante todo el QSO
  simAplicarBanda();
  if (!enSim) return;
  if (estadoSim == SIM_EMITIENDO && !motorCWOcupado()) {
    if (simAcabarAlEmitir) { simTerminar(); return; }  // El "E E" del cazador
    keyerReanudar();  // Tu turno
    estadoSim = SIM_ESCUCHANDO;
    simInicioTurnoMs = millis();
    pantallaPendiente = true;
  } else if (estadoSim == SIM_ESCUCHANDO) {
    unsigned long ahora = millis();
    String r = simRespuesta;
    r.trim();
    if (r.length() > 0) {
      uint32_t finTurno = max((uint32_t)SIM_FIN_TURNO_MS, (uint32_t)(25 * 1200 / keyerWpm));
      if (ahora - keyerUltimoUsoMs >= finTurno) simEvaluar();
    } else if (simRol == SIM_ROL_ACTIVADOR) {
      // El cazador no se va: si tardas, vuelve a llamar (8 s) o repite lo ultimo (20 s). Tu CQ, sin prisa
      if (faseSim == FASE_LLAMADA) return;
      uint32_t espera = simCazLlamando ? SIM_CAZADOR_TARDE_MS : SIM_SIN_RESPUESTA_MS;
      if (ahora - simInicioTurnoMs >= espera) {
        if (simCazLlamando) simCazadorLlama(false);
        else simEmitir(simEmitido);
      }
    } else if (faseSim == FASE_DESPEDIDA) {
      if (ahora - simInicioTurnoMs >= SIM_DESPEDIDA_MS) simTerminar();
    } else if (ahora - simInicioTurnoMs >= SIM_SIN_RESPUESTA_MS) {
      if (faseSim == FASE_LLAMADA) simLlamar();
      else simEmitir(simEmitido);
    }
  }
}

// "repetir": Chispas vuelve a emitir lo ultimo (no cuenta). Con rol activador, antes de tu CQ no hay nada
void simRepetir() {
  if (simEmitido.length() == 0) return;
  if (estadoSim == SIM_ESCUCHANDO || estadoSim == SIM_EMITIENDO) {
    bool llamando = simCazLlamando;
    motorCWDetener();
    simEmitir(simEmitido);
    simCazLlamando = llamando;
  }
}

// ----------------------------------------------------------------------------
// Pantallas
// ----------------------------------------------------------------------------
#define N_AJUSTES_SIM 9
// El 1 es la velocidad de Chispas: se nombra segun el rol al dibujar
const char* nombresAjustesSim[N_AJUSTES_SIM] = {
  "ROL", "WPM ACTIVADOR", "EFECTIVA", "WPM PALAS", "VER TEXTO", "QSB", "RUIDO", "TORMENTA", "QRM"
};

String textoNivelSim(uint8_t nivel) {
  return nivel == QSB_FUERTE ? String("FUERTE") : (nivel == QSB_SUAVE ? String("SUAVE") : String("NO"));
}

String valorAjusteSim(uint8_t i) {
  switch (i) {
    case 0: return simRol == SIM_ROL_ACTIVADOR ? String("ACTIVADOR") : String("CAZADOR");
    case 1: return String(simWpm);
    case 2: return textoEfectiva(simEfectiva, simWpm);
    case 3: return String(keyerWpm);
    case 4: return simVerTexto ? String("SI") : String("NO");
    case 5: return textoNivelSim(simQsb);
    case 6: return textoNivelSim(simRuido);
    case 7: return textoNivelSim(simTormenta);
    case 8: return textoNivelSim(simQrm);
    default: return String("");
  }
}

void dibujarPantallaSim() {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  if (estadoSim == SIM_AJUSTES) {
    nombresAjustesSim[1] = simRol == SIM_ROL_ACTIVADOR ? "WPM CAZADOR" : "WPM ACTIVADOR";
    dibujarListaOpciones(nombresAjustesSim, N_AJUSTES_SIM, opcionSim, valorAjusteSim, 0xE);  // 1-3
    dibujarPieDos(opcionSim == N_AJUSTES_SIM, "iniciar", opcionSim == N_AJUSTES_SIM + 1, "volver");

  } else if (estadoSim == SIM_FIN) {
    dibujarTituloCentrado(F("- QSO HECHO -"), 12);
    char linea[22];
    display.setCursor(0, 24);
    display.print("CON " + simIndicativo + " " + (simRol == SIM_ROL_ACTIVADOR ? String("") : simParque));
    unsigned long s = simDuracionMs / 1000;
    snprintf(linea, sizeof(linea), "TIEMPO %lu:%02lu  REP. %u", s / 60, s % 60, simRepeticiones);
    display.setCursor(0, 33); display.print(linea);
    if (simWpmReal > 0) snprintf(linea, sizeof(linea), "VELOCIDAD %2lu DE %2u", (unsigned long)simWpmReal, keyerWpm);
    else snprintf(linea, sizeof(linea), "VELOCIDAD -- DE %2u", keyerWpm);
    display.setCursor(0, 42); display.print(linea);
    dibujarPieDos(opcionPracticaSim == 0, "volver", opcionPracticaSim == 1, "otro");

  } else {
    // Como QSO LIBRE: el activador arriba (2 lineas), separador con el turno, lo tuyo abajo (2 lineas).
    // Con VER NO, lo que emite sale como "?" mientras suena
    String rx = simEmitido;
    if (estadoSim == SIM_EMITIENDO && !simVerTexto) {
      for (uint16_t i = 0; i < rx.length(); i++) if (rx[i] != ' ') rx.setCharAt(i, '?');
    }
    if (rx.length() == 0) rx = "LLAMA: CQ POTA DE " + String(indicativoEstacion) + " K";  // Rol activador, al empezar
    dibujarDosLineasPorPalabras(rx, 11, 8);

    display.drawFastHLine(0, 31, 128, SSD1306_WHITE);
    String etiqueta = String(" ") + (int)(faseSim + 1) + "/3 " +
                      (estadoSim == SIM_EMITIENDO ? "ESCUCHA" : "TU TURNO") + " ";
    int16_t x1, y1;
    uint16_t w, h;
    display.getTextBounds(etiqueta, 0, 0, &x1, &y1, &w, &h);
    display.setTextColor(SSD1306_WHITE, SSD1306_BLACK);  // El fondo negro tapa la linea
    display.setCursor((128 - w) / 2, 28);
    display.print(etiqueta);
    display.setTextColor(SSD1306_WHITE);

    dibujarUltimasLineas(simRespuesta + (estadoSim == SIM_ESCUCHANDO ? "_" : ""), 37, 2, 8);
    dibujarPieDos(opcionPracticaSim == 0, "repetir", opcionPracticaSim == 1, "salir");
  }
}
