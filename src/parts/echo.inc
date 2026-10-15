// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 EA4ILN
// Chispas, parte de main.cpp: modo Echo.
// Se incluye desde main.cpp en su orden (una sola unidad de compilacion); no compila suelta.

// ============================================================================
// MODO ECHO: CHISPAS EMITE Y HAY QUE REPETIRLO CON LAS PALAS
// ============================================================================
#define ECHO_PAUSA_ACIERTO_MS 1200
#define ECHO_PAUSA_FALLO_MS   3000

/**
 * Emite un reto. Con repetirMismo vuelve a emitir el actual (tras un fallo o con "repetir").
 * Las letras salen de la leccion del Echo, con la letra nueva al 25 %.
 */
void echoNuevoReto(bool repetirMismo) {
  if (!repetirMismo || echoObjetivo.length() == 0) {
    echoObjetivo = "";
    for (uint8_t i = 0; i < echoLargo; i++) echoObjetivo += caracterKochAleatorio(echoLeccion, echoRefuerzo);
  }
  echoRespuesta = "";
  keyerPausar();
  motorCWEmitir(echoObjetivo.c_str(), echoWpm, echoEspaciado, "ECHO");
  estadoEcho = ECHO_REPRODUCIENDO;
  pantallaPendiente = true;
}

// Abre los ajustes del Echo (la practica empieza con "iniciar")
void entrarEnEcho() {
  motorCWDetener();
  enEcho = true;
  echoPracticando = false;
  enEstadisticasEcho = false;
  opcionEcho = OPCION_ECHO_INICIAR;
  estadoEcho = ECHO_ESPERA;
}

void iniciarPracticaEcho() {
  guardarEchoNVS();
  informarRefuerzo("ECHO", echoLeccion, echoRefuerzo);
  echoPracticando = true;
  opcionPracticaEcho = 0;
  echoAciertos = 0;
  echoIntentos = 0;
  echoObjetivo = "";
  echoNuevoReto(false);
}

// De la practica (o del resultado) a los ajustes
void salirPracticaEcho() {
  keyerPausar();
  motorCWDetener();
  echoPracticando = false;
  estadoEcho = ECHO_ESPERA;
}

void salirDeEcho() {
  salirPracticaEcho();
  enEcho = false;
  editandoValor = false;
  guardarEchoNVS();
}

void echoEvaluar() {
  echoIntentos++;
  echoAcierto = (echoRespuesta == echoObjetivo);
  if (echoAcierto) echoAciertos++;
  echoTotalIntentos++;
  if (echoAcierto) echoTotalAciertos++;
  // Por caracter, posicion a posicion (lo que falte en una respuesta corta cuenta como fallo)
  for (uint8_t i = 0; i < echoObjetivo.length(); i++) {
    registrarCaracter(statsCarEcho, echoObjetivo[i], i < echoRespuesta.length() && echoRespuesta[i] == echoObjetivo[i]);
  }
  guardarStatsEchoNVS();
  Serial.printf("\n[ECHO] %s -> %s: %s (%u/%u)\n", echoObjetivo.c_str(), echoRespuesta.c_str(),
                echoAcierto ? "BIEN" : "MAL", echoAciertos, echoIntentos);
  keyerPausar();
  estadoEcho = ECHO_RESULTADO;
  echoFinResultadoMs = millis() + (echoAcierto ? ECHO_PAUSA_ACIERTO_MS : ECHO_PAUSA_FALLO_MS);
  pantallaPendiente = true;
}

// Un caracter descodificado del keyer durante el Echo
void echoRecibirCaracter(char c) {
  if (estadoEcho != ECHO_ESCUCHANDO) return;
  if (c == ' ') {
    // Silencio largo con algo manipulado: se da por terminada la respuesta aunque sea corta
    if (echoRespuesta.length() > 0) echoEvaluar();
    return;
  }
  echoRespuesta += c;
  pantallaPendiente = true;
  if (echoRespuesta.length() >= echoObjetivo.length()) echoEvaluar();
}

void procesarEcho() {
  if (!enEcho || !echoPracticando) return;
  if (estadoEcho == ECHO_REPRODUCIENDO && !motorCWOcupado()) {
    keyerReanudar();  // Turno del usuario
    estadoEcho = ECHO_ESCUCHANDO;
    pantallaPendiente = true;
  } else if (estadoEcho == ECHO_RESULTADO && (long)(millis() - echoFinResultadoMs) >= 0) {
    if (echoIntentos >= echoRetos) {
      // Fin de la practica: resultado como en el Koch
      estadoEcho = ECHO_FIN;
      opcionPracticaEcho = 0;
      echoSesiones++;
      guardarStatsEchoNVS();
      registrarLeccionEchoNVS(echoLeccion, echoIntentos > 0 ? echoAciertos * 100.0f / echoIntentos : 0);
      Serial.printf("[ECHO] FIN: %u/%u\n", echoAciertos, echoIntentos);
      pantallaPendiente = true;
    } else {
      echoNuevoReto(!echoAcierto);  // Tras un fallo se repite el mismo
    }
  }
}

// Pasa a la pantalla lo que el keyer ha descodificado (y por serie, para depurar)
void procesarTextoKeyer() {
  char c;
  while (xQueueReceive(colaKeyerTexto, &c, 0) == pdTRUE) {
    ultimaActividadMs = millis();  // Manipular cuenta como uso (brillo y apagado automatico)
    Serial.print(c);
    if (enEcho) {
      echoRecibirCaracter(c);
      continue;
    }
    if (enSim) {
      simRecibirCaracter(c);
      continue;
    }
    if (pantallaActual == PANTALLA_LORA && enPantalla4_Sub && subpantalla4Activa == 2) {
      qsoCaracterManipulado(c);
      continue;
    }
    if (pantallaActual == PANTALLA_TRX) {
      trxCaracterManipulado(c);
      continue;
    }
    textoKeyer += c;
    if (textoKeyer.length() > 200) textoKeyer.remove(0, textoKeyer.length() - 200);
    pantallaPendiente = true;
  }
}

