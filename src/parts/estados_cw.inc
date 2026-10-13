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
  totalCaracteresLeccion = gruposCW * caracteresCW;

  guardarUltimaConfiguracionNVS();
  informarRefuerzo("KOCH", leccionCW, refuerzoKoch);

  estadoCW = CW_GENERAR_GRUPO;
  mostrarTextoGrupo = false;
  tiempoCWProximo = millis();
}

void detenerPracticaCW() {
  motorCWDetener();
  enPractica3_2 = false;
  estadoCW = CW_IDLE;
  actualizarPantalla();
}

void generarGrupoKoch() {
  grupoActualStr = "";
  for (int i = 0; i < caracteresCW; i++) {
    grupoActualStr += caracterKochAleatorio(leccionCW, refuerzoKoch);
  }

  if (textoCompletoLeccion.length() > 0) textoCompletoLeccion += " ";
  textoCompletoLeccion += grupoActualStr;
}

// "ok" de la correccion: guarda el % de la leccion y, segun el modo, los aciertos y fallos de cada caracter
// marcado (x letra) o solo los totales (total: no se sabe que letras fallaron)
void enviarResultadoKoch(bool porLetra) {
  if (!porLetra) numeroErrores = fallosTotales;
  float pct = 0.0;
  if (totalCaracteresLeccion > 0) pct = ((float)(totalCaracteresLeccion - numeroErrores) / (float)totalCaracteresLeccion) * 100.0;
  registrarResultadoLeccionNVS(leccionCW, pct);

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

void procesarMaquinaEstadosCW() {
  if (!enPractica3_2 || estadoCW == CW_PAUSADO || estadoCW == CW_IDLE || 
      estadoCW == CW_PANTALLA_3_3_LISTADO || estadoCW == CW_PANTALLA_CORREGIR_ERRORES ||
      estadoCW == CW_PANTALLA_FALLOS_TOTALES || estadoCW == CW_PANTALLA_RESUMEN_FINAL) return;

  if (millis() < tiempoCWProximo) return;

  switch (estadoCW) {
    case CW_GENERAR_GRUPO:
      if (grupoActualNum >= gruposCW) {
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
      motorCWEmitir(grupoActualStr.c_str(), wpmCW, espaciadoCW, "KOCH");
      break;

    case CW_EMITIENDO_GRUPO:
      if (motorCWOcupado()) return;
      mostrarTextoGrupo = true;
      actualizarPantalla();
      estadoCW = CW_PAUSA_ENTRE_GRUPOS;
      tiempoCWProximo = motorCWFinMs + 2000;
      break;

    case CW_PAUSA_ENTRE_GRUPOS:
      estadoCW = CW_GENERAR_GRUPO;
      break;

    default:
      break;
  }
}

