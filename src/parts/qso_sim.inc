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
// Con QSB, el volumen del activador sube y baja (simCicloQsb)
// Lo manipulado se compara sin espacios (una pausa larga parte el indicativo) y solo se buscan las palabras
// clave: para empezar a manipular basta. "AGN" o "?" piden que repita. Si no te entiende, pide lo que falta
#define SIM_FIN_TURNO_MS     3000   // Silencio que cierra tu turno (o 25 puntos, si es mas)
#define SIM_SIN_RESPUESTA_MS 20000  // Sin manipular nada: vuelve a llamar (o repite)
#define SIM_DESPEDIDA_MS     6000   // En la despedida, sin manipular: se acaba

String simIndicativo = "";  // El del activador
String simParque = "";
String simEmitido = "";     // Lo ultimo que ha emitido Chispas (para "repetir")
String simRespuesta = "";   // Lo manipulado en este turno
unsigned long simInicioTurnoMs = 0, simInicioMs = 0, simDuracionMs = 0;
uint8_t simRepeticiones = 0;  // Veces que el activador no te entendio
uint32_t simWpmReal = 0;      // Velocidad real de tu manipulacion (como en RITMO), al acabar
float simQsbFase1 = 0, simQsbFase2 = 0;  // Al azar en cada QSO

// QSB: el volumen del activador baja y sube despacio, con dos senos de periodos que no encajan (5,3 y 2,1 s)
// para que no se repita igual. Profundidad: SUAVE, hasta -8 dB; FUERTE, hasta -26 dB (casi desaparece).
// De la amplitud al ciclo del PWM: la fundamental de la cuadrada va con sin(pi * ciclo), asi que
// ciclo = asin(amplitud) / pi (128 = 50 % = el maximo)
uint8_t simCicloQsb() {
  if (simQsb == QSB_NO) return 128;
  float t = millis() / 1000.0f;
  float f = 0.5f + 0.5f * (0.7f * sinf(2 * PI * t / 5.3f + simQsbFase1) + 0.3f * sinf(2 * PI * t / 2.1f + simQsbFase2));
  float profundidad = (simQsb == QSB_FUERTE) ? 0.95f : 0.6f;
  float amplitud = 1.0f - profundidad * f;
  int ciclo = lroundf(256.0f * asinf(amplitud) / PI);
  return (uint8_t)constrain(ciclo, 2, 128);
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
  keyerPausar();
  cicloTono = simCicloQsb();  // Ya para el primer elemento (luego lo sigue procesarSim)
  motorCWEmitir(texto.c_str(), simWpm, simEspaciado, "SIM");
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
  Serial.printf("\n[SIM] QSO con %s %s: %lus, %u rep., %lu WPM\n", simIndicativo.c_str(), simParque.c_str(),
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

// Fin de tu turno: segun la fase y lo manipulado, el activador sigue o pide lo que falta
void simEvaluar() {
  String r = simRespuesta;
  r.replace(" ", "");
  Serial.printf("\n[SIM] %s\n", simRespuesta.c_str());

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
  // QSB solo mientras emite el activador: tu tono de las palas (y el resto de Chispas) siempre al maximo
  cicloTono = (enSim && estadoSim == SIM_EMITIENDO) ? simCicloQsb() : 128;
  if (!enSim) return;
  if (estadoSim == SIM_EMITIENDO && !motorCWOcupado()) {
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
    } else if (faseSim == FASE_DESPEDIDA) {
      if (ahora - simInicioTurnoMs >= SIM_DESPEDIDA_MS) simTerminar();
    } else if (ahora - simInicioTurnoMs >= SIM_SIN_RESPUESTA_MS) {
      if (faseSim == FASE_LLAMADA) simLlamar();
      else simEmitir(simEmitido);
    }
  }
}

// "repetir": el activador vuelve a emitir lo ultimo (no cuenta)
void simRepetir() {
  if (estadoSim == SIM_ESCUCHANDO || estadoSim == SIM_EMITIENDO) {
    motorCWDetener();
    simEmitir(simEmitido);
  }
}

// ----------------------------------------------------------------------------
// Pantallas
// ----------------------------------------------------------------------------
#define N_AJUSTES_SIM 5
const char* const NOMBRES_AJUSTES_SIM[N_AJUSTES_SIM] = {
  "WPM ACTIVADOR", "ESPACIADO", "WPM PALAS", "VER TEXTO", "QSB"
};

String valorAjusteSim(uint8_t i) {
  switch (i) {
    case 0: return String(simWpm);
    case 1: return String(simEspaciado) + "%";
    case 2: return String(keyerWpm);
    case 3: return simVerTexto ? String("SI") : String("NO");
    case 4: return simQsb == QSB_FUERTE ? String("FUERTE") : (simQsb == QSB_SUAVE ? String("SUAVE") : String("NO"));
    default: return String("");
  }
}

void dibujarPantallaSim() {
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  if (estadoSim == SIM_AJUSTES) {
    dibujarListaOpciones(NOMBRES_AJUSTES_SIM, N_AJUSTES_SIM, opcionSim, valorAjusteSim, 0x7);  // 0-2
    dibujarPieDos(opcionSim == N_AJUSTES_SIM, "iniciar", opcionSim == N_AJUSTES_SIM + 1, "volver");

  } else if (estadoSim == SIM_FIN) {
    dibujarTituloCentrado(F("- QSO HECHO -"), 12);
    char linea[22];
    display.setCursor(0, 24);
    display.print("CON " + simIndicativo + " " + simParque);
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
