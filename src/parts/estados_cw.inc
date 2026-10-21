// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 EA4ILN
// Chispas, parte de main.cpp: maquina de estados CW.
// Se incluye desde main.cpp en su orden (una sola unidad de compilacion); no compila suelta.

// ============================================================================
// MAQUINA DE ESTADOS CW Y BOTON PRG
// ============================================================================
void iniciarPracticaCW() {
  enPractica3_2 = true;
  grupoActualNum = 0;
  textoCompletoLeccion = "";
  numeroErrores = 0;
  paginaListado3_3 = 0;
  opcionFinListado3_3 = 0;
  memset(marcasFallo, 0, sizeof(marcasFallo));
  opcionPractica = 0;
  totalCaracteresLeccion = 0;  // Se cuenta al generar cada grupo
  // Con indicativos o frases el texto es mas largo: tope para que quepa en MAX_TEXTO_LECCION
  gruposPractica = gruposCW;
  if (textoKoch == TEXTO_INDICATIVOS) gruposPractica = min(gruposCW, (uint8_t)MAX_INDICATIVOS_PRACTICA);
  if (textoKoch == TEXTO_QSO) gruposPractica = min(gruposCW, (uint8_t)MAX_FRASES_PRACTICA);

  guardarUltimaConfiguracionNVS();
  if (textoKoch == TEXTO_GRUPOS) informarRefuerzo("KOCH", leccionCW, refuerzoKoch);

  // Cuenta atras de 3 s antes del primer grupo, para prepararse a copiar
  estadoCW = CW_CUENTA_ATRAS;
  cuentaAtrasFinMs = millis() + CUENTA_ATRAS_KOCH_MS;
  cuentaAtrasMostrada = CUENTA_ATRAS_KOCH_MS / 1000;
  mostrarTextoGrupo = false;
  tiempoCWProximo = millis();
}

// Segundos que faltan de la cuenta atras (3, 2, 1)
uint8_t segundosCuentaAtras() {
  int32_t falta = (int32_t)(cuentaAtrasFinMs - millis());
  return (falta <= 0) ? 0 : (uint8_t)((falta + 999) / 1000);
}

void detenerPracticaCW() {
  motorCWDetener();
  enPractica3_2 = false;
  estadoCW = CW_IDLE;
  actualizarPantalla();
}

// ----------------------------------------------------------------------------
// TEXTO INDICATIVOS Y QSO POTA (ajuste TEXTO del Koch): no siguen la leccion, usan todo el alfabeto
// ----------------------------------------------------------------------------
// Prefijos al azar ('#' = una cifra del 1 al 9: EA0 no existe); los espanoles salen mas a menudo
const char* const PREFIJOS_INDICATIVO[] = {
  "EA#", "EA#", "EA#", "EA#", "EB#", "EC#", "F#", "G#", "M#", "DL#", "DK#", "I#", "IK#", "ON#", "PA#",
  "OE#", "HB9", "CT#", "K#", "W#", "N#", "VE#", "JA#", "SP#", "OK#", "OH#", "SM#", "LA#", "YO#", "9A#", "S5#"
};

// Indicativo verosimil: prefijo, cifra y sufijo de 1 a 3 letras; a veces "/P" (portable, como en POTA)
String indicativoAleatorio() {
  const uint8_t n = sizeof(PREFIJOS_INDICATIVO) / sizeof(PREFIJOS_INDICATIVO[0]);
  String t;
  for (const char* p = PREFIJOS_INDICATIVO[random(n)]; *p; p++) t += (*p == '#') ? (char)('1' + random(9)) : *p;
  uint8_t r = random(10);
  uint8_t letras = (r == 0) ? 1 : (r < 5 ? 2 : 3);
  for (uint8_t i = 0; i < letras; i++) t += (char)('A' + random(26));
  if (random(100) < 15) t += "/P";
  return t;
}

// Referencia de parque en CW, sin guion (el '-' no esta en la tabla Koch): "ES0072"
String referenciaParqueAleatoria() {
  char t[8];
  snprintf(t, sizeof(t), "ES%04ld", random(1, 1500));
  return String(t);
}

// Frase de un QSO POTA tipico (A = activador, H = cazador, R = parque), hasta 30 caracteres
String fraseQsoAleatoria() {
  String a = indicativoAleatorio(), h = indicativoAleatorio();
  switch (random(7)) {
    case 0:  return "CQ POTA DE " + a + " " + a + " K";
    case 1:  return h + " " + h;
    case 2:  return h + " GM 5NN 5NN BK";
    case 3:  return "BK R R TU 5NN 5NN 73 BK";
    case 4:  return h + " TU " + referenciaParqueAleatoria() + " 73 E E";
    case 5:  return "QRZ DE " + a + " POTA K";
    default: return "R TU 73 QRZ DE " + a;
  }
}

void generarGrupoKoch() {
  grupoActualStr = "";
  if (textoKoch == TEXTO_INDICATIVOS) {
    grupoActualStr = indicativoAleatorio();
  } else if (textoKoch == TEXTO_QSO) {
    grupoActualStr = fraseQsoAleatoria();
  } else {
    for (int i = 0; i < caracteresCW; i++) {
      grupoActualStr += caracterKochAleatorio(leccionCW, refuerzoKoch);
    }
  }

  // Por si acaso (los topes ya lo evitan): lo que no quepa no entra en la correccion
  if (textoCompletoLeccion.length() + 1 + grupoActualStr.length() > MAX_TEXTO_LECCION) return;
  if (textoCompletoLeccion.length() > 0) textoCompletoLeccion += " ";
  textoCompletoLeccion += grupoActualStr;
  for (uint16_t i = 0; i < grupoActualStr.length(); i++) {
    if (grupoActualStr[i] != ' ') totalCaracteresLeccion++;
  }
}

// "ok" de la correccion: guarda el % de la leccion y, segun el modo, los aciertos y fallos de cada caracter
// marcado (x letra) o solo los totales (total: no se sabe que letras fallaron)
void enviarResultadoKoch(bool porLetra) {
  if (!porLetra) numeroErrores = fallosTotales;
  float pct = 0.0;
  if (totalCaracteresLeccion > 0) pct = ((float)(totalCaracteresLeccion - numeroErrores) / (float)totalCaracteresLeccion) * 100.0;
  // Indicativos y QSO no son de la leccion: no cuentan para su %, pero si para las letras y los totales
  if (textoKoch == TEXTO_GRUPOS) registrarResultadoLeccionNVS(leccionCW, pct);

  if (porLetra) {
    Serial.print(F("[KOCH] FALLOS:"));
    for (uint16_t i = 0; i < textoCompletoLeccion.length(); i++) {
      char c = textoCompletoLeccion[i];
      if (c == ' ') continue;
      registrarCaracter(statsCarKoch, c, !marcasFallo[i]);
      if (marcasFallo[i]) { Serial.print(' '); Serial.print(c); }
    }
    Serial.printf(" (%u/%u)\n", numeroErrores, totalCaracteresLeccion);
    guardarStatsKochNVS();
  } else {
    kochTotCaracteres += totalCaracteresLeccion;
    kochTotAciertos += totalCaracteresLeccion - numeroErrores;
    Serial.printf("[KOCH] FALLOS TOTALES: %u/%u\n", numeroErrores, totalCaracteresLeccion);
    guardarTotalesKochNVS();
  }

  ultimoPctKoch = pct;
  ultimoPctKochMs = millis();
  estadoCW = CW_PANTALLA_RESUMEN_FINAL;
  opcionResumen = 0;
}

// Pausa del Koch entre un grupo (o indicativo o frase) y el siguiente, con el anterior a la vista (2 s hasta el
// 16 oct 2026)
#define PAUSA_ENTRE_GRUPOS_MS 3000

void procesarMaquinaEstadosCW() {
  if (!enPractica3_2 || estadoCW == CW_PAUSADO || estadoCW == CW_IDLE || 
      estadoCW == CW_PANTALLA_3_3_LISTADO || estadoCW == CW_PANTALLA_CORREGIR_ERRORES ||
      estadoCW == CW_PANTALLA_FALLOS_TOTALES || estadoCW == CW_PANTALLA_RESUMEN_FINAL) return;

  if (estadoCW == CW_CUENTA_ATRAS) {
    uint8_t s = segundosCuentaAtras();
    if (s == 0) {
      estadoCW = CW_GENERAR_GRUPO;
    } else {
      if (s != cuentaAtrasMostrada) { cuentaAtrasMostrada = s; actualizarPantalla(); }
      return;
    }
  }

  if (millis() < tiempoCWProximo) return;

  switch (estadoCW) {
    case CW_GENERAR_GRUPO:
      if (grupoActualNum >= gruposPractica) {
        estadoCW = CW_PANTALLA_3_3_LISTADO;
        paginaListado3_3 = 0;
        opcionFinListado3_3 = 0;
        actualizarPantalla();
        return;
      }

      grupoActualNum++;
      generarGrupoKoch();
      mostrarTextoGrupo = false;
      estadoCW = CW_EMITIENDO_GRUPO;
      actualizarPantalla();
      // Los indicativos suenan dos veces ("EA4ABC EA4ABC", como en el aire); en el texto a corregir, una
      if (textoKoch == TEXTO_INDICATIVOS) {
        motorCWEmitir((grupoActualStr + " " + grupoActualStr).c_str(), wpmCW, efectivaCW, "KOCH");
      } else {
        motorCWEmitir(grupoActualStr.c_str(), wpmCW, efectivaCW, "KOCH");
      }
      break;

    case CW_EMITIENDO_GRUPO:
      if (motorCWOcupado()) return;
      mostrarTextoGrupo = true;
      actualizarPantalla();
      estadoCW = CW_PAUSA_ENTRE_GRUPOS;
      tiempoCWProximo = motorCWFinMs + PAUSA_ENTRE_GRUPOS_MS;
      break;

    case CW_PAUSA_ENTRE_GRUPOS:
      estadoCW = CW_GENERAR_GRUPO;
      break;

    default:
      break;
  }
}

