// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 EA4ILN
// Chispas, parte de main.cpp: tarea keyer y palas.
// Se incluye desde main.cpp en su orden (una sola unidad de compilacion); no compila suelta.

// ============================================================================
// KEYER IAMBICO EN UNA TAREA FREERTOS
// ============================================================================
// Lee las palas cada 1 ms y genera puntos y rayas con la misma prioridad que el motor CW
// (no suenan a la vez: el keyer solo esta activo en su pantalla). Como sabe que elementos
// ha generado, descodifica lo manipulado y lo pasa a loop() por una cola.
// Memoria de la pala contraria: modo A, solo si se pulsa en la pausa entre elementos;
// modo B, tambien durante el elemento (al soltar un apreton sale un elemento mas).
#define KEYER_WPM_MIN 10
#define KEYER_WPM_MAX 40
#define KEYER_PILA    3072

void guardarKeyerNVS();                // Definida en la seccion NVS
void guardarEchoNVS();                 // Definida en la seccion NVS
void qsoCaracterManipulado(char c);    // Definida con la radio LoRa
void trxCaracterManipulado(char c);    // Definida con la pantalla TRX
void simRecibirCaracter(char c);       // Definida con el simulador de QSO
extern bool pantallaPendiente;         // Definida con la bateria
extern unsigned long ultimaActividadMs;  // Definida con la cara (ultimo giro, pulsacion o manipulacion)

TaskHandle_t tareaKeyer = NULL;
QueueHandle_t colaKeyerTexto = NULL;  // Caracteres descodificados, de la tarea a loop()
volatile bool keyerActivo = false;

// Ritmo de la manipulacion (TX LIBRE > menu > RITMO). Con palas iambicas el keyer hace puntos y rayas exactos:
// lo que depende de quien manipula son los silencios. Se miden en decimas de punto desde el final de un
// elemento hasta el principio del siguiente: dentro de la letra (ideal 1; desde 1,5, titubeo), entre letras
// (ideal 3; el keyer cierra la letra a los 2) y entre palabras (ideal 7; cierra la palabra a los 6). Mas de
// RITMO_PAUSA_10 es una pausa y no cuenta. msReal/msIdeal dan la velocidad real (con los espacios de verdad)
#define RITMO_PAUSA_10 150
struct RitmoKeyer {
  uint32_t letras, sumaLetra10, sumaLetra10Cuad;  // Espacios entre letras
  uint32_t palabras, sumaPalabra10;               // Espacios entre palabras
  uint32_t caracteres, desconocidos, titubeos;
  uint32_t msReal, msIdeal;
};
RitmoKeyer ritmoKeyer = {};          // Lo escribe la tarea; loop() solo lo lee (para la pantalla)
volatile bool ritmoBorrar = false;   // loop() pide ponerlo a cero; lo hace la tarea

char decodificarPatron(const char* patron) {
  for (size_t i = 0; i < sizeof(DICCIONARIO_MORSE) / sizeof(PatronMorse); i++) {
    if (strcmp(DICCIONARIO_MORSE[i].codigo, patron) == 0) return DICCIONARIO_MORSE[i].c;
  }
  return '*';
}

void leerPalas(bool& dit, bool& dah) {
  bool punta = digitalRead(KEYER_PADDLE_DIT) == LOW;
  bool anillo = digitalRead(KEYER_PADDLE_DAH) == LOW;
  dit = keyerPalasInvertidas ? anillo : punta;
  dah = keyerPalasInvertidas ? punta : anillo;
}

void tareaKeyerFn(void* parametro) {
  enum { K_ESPERA, K_ELEMENTO, K_PAUSA } estado = K_ESPERA;
  char elemento = 0;  // Ultimo elemento generado ('.' o '-')
  bool memoriaDit = false, memoriaDah = false;
  char patron[9];
  uint8_t largo = 0;              // Elementos del caracter en curso (8 = demasiados: error)
  bool espacioPendiente = false;  // Tras un caracter: falta ver si hay espacio entre palabras
  TickType_t finFase = 0, finUltimoElemento = 0;
  bool ritmoHayAnterior = false;  // Ya hubo un elemento: el siguiente silencio se puede medir

  for (;;) {
    if (!keyerActivo) {
      conmutarEmisionMorse(false);
      estado = K_ESPERA;
      largo = 0;
      espacioPendiente = false;
      memoriaDit = memoriaDah = false;
      ritmoHayAnterior = false;
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);  // Hasta que se abra la pantalla del keyer
      continue;
    }

    vTaskDelay(1);
    if (ritmoBorrar) {
      memset(&ritmoKeyer, 0, sizeof(ritmoKeyer));
      ritmoHayAnterior = false;
      ritmoBorrar = false;
    }
    TickType_t ahora = xTaskGetTickCount();
    // El Echo y la pantalla TRX tienen su propio wpm
    uint8_t wpm = enEcho ? echoWpm : (pantallaActual == PANTALLA_TRX ? trxWpm : keyerWpm);
    TickType_t dit = pdMS_TO_TICKS(1200 / wpm);
    bool pDit, pDah;
    leerPalas(pDit, pDah);
    char siguiente = 0;

    switch (estado) {
      case K_ELEMENTO:
        if (keyerModoB) {
          if (elemento == '.' && pDah) memoriaDah = true;
          if (elemento == '-' && pDit) memoriaDit = true;
        }
        if ((int32_t)(ahora - finFase) >= 0) {
          conmutarEmisionMorse(false);
          finUltimoElemento = ahora;
          finFase = ahora + dit;
          estado = K_PAUSA;
        }
        break;

      case K_PAUSA:
        if (elemento == '.' && pDah) memoriaDah = true;
        if (elemento == '-' && pDit) memoriaDit = true;
        if ((int32_t)(ahora - finFase) >= 0) {
          // Se alterna si hay memoria o la pala contraria esta pulsada; si no, se repite la misma
          if (elemento == '.') siguiente = (memoriaDah || pDah) ? '-' : (pDit ? '.' : 0);
          else siguiente = (memoriaDit || pDit) ? '.' : (pDah ? '-' : 0);
          memoriaDit = memoriaDah = false;
          if (siguiente == 0) estado = K_ESPERA;
        }
        break;

      case K_ESPERA:
        if (pDit || pDah) {
          siguiente = (pDit && pDah) ? (elemento == '.' ? '-' : '.') : (pDit ? '.' : '-');
        } else {
          // Silencio desde el ultimo elemento: 2 puntos cierran el caracter y 6, la palabra (con 5, una pausa
          // algo larga entre letras partia la palabra; en QSO se enviaba letra a letra)
          TickType_t silencio = ahora - finUltimoElemento;
          if (largo > 0 && silencio >= 2 * dit) {
            patron[min(largo, (uint8_t)8)] = '\0';
            char c = (largo <= 7) ? decodificarPatron(patron) : '*';
            xQueueSend(colaKeyerTexto, &c, 0);
            ritmoKeyer.caracteres++;
            if (c == '*') ritmoKeyer.desconocidos++;
            largo = 0;
            espacioPendiente = true;
          } else if (espacioPendiente && silencio >= 6 * dit) {
            char c = ' ';
            xQueueSend(colaKeyerTexto, &c, 0);
            espacioPendiente = false;
          }
        }
        break;
    }

    if (pDit || pDah || estado != K_ESPERA || siguiente != 0) keyerUltimoUsoMs = millis();

    if (siguiente != 0) {
      // Ritmo: el silencio que acaba aqui (antes de tocar largo y espacioPendiente, que dicen de que tipo es)
      TickType_t duracion = (siguiente == '.') ? dit : 3 * dit;
      if (ritmoHayAnterior) {
        TickType_t silencio = ahora - finUltimoElemento;
        uint32_t d10 = (uint32_t)silencio * 10 / dit;
        if (largo > 0) {               // Dentro de la letra
          if (d10 >= 15) ritmoKeyer.titubeos++;
          ritmoKeyer.msReal += silencio;
          ritmoKeyer.msIdeal += dit;
        } else if (espacioPendiente) { // Entre letras
          ritmoKeyer.letras++;
          ritmoKeyer.sumaLetra10 += d10;
          ritmoKeyer.sumaLetra10Cuad += d10 * d10;
          ritmoKeyer.msReal += silencio;
          ritmoKeyer.msIdeal += 3 * dit;
        } else if (d10 <= RITMO_PAUSA_10) {  // Entre palabras
          ritmoKeyer.palabras++;
          ritmoKeyer.sumaPalabra10 += d10;
          ritmoKeyer.msReal += silencio;
          ritmoKeyer.msIdeal += 7 * dit;
        }
      }
      ritmoHayAnterior = true;
      ritmoKeyer.msReal += duracion;
      ritmoKeyer.msIdeal += duracion;

      elemento = siguiente;
      if (largo < 8) patron[largo] = siguiente;
      if (largo < 8) largo++;
      espacioPendiente = false;
      conmutarEmisionMorse(true);
      finFase = ahora + duracion;
      estado = K_ELEMENTO;
    }
  }
}

void iniciarKeyer() {
  colaKeyerTexto = xQueueCreate(64, sizeof(char));
  xTaskCreatePinnedToCore(tareaKeyerFn, "keyer", KEYER_PILA, NULL, MOTOR_CW_PRIORIDAD,
                          &tareaKeyer, MOTOR_CW_NUCLEO);
}

void entrarEnKeyer() {
  motorCWDetener();
  enKeyer = true;
  keyerEnAjustes = false;
  keyerEnRitmo = false;
  opcionKeyer = 0;
  textoKeyer = "";
  ritmoBorrar = true;
  xQueueReset(colaKeyerTexto);
  keyerActivo = true;
  xTaskNotifyGive(tareaKeyer);
}

void salirDeKeyer() {
  keyerActivo = false;  // La tarea apaga LED y tono en menos de 1 ms
  enKeyer = false;
  editandoValor = false;
  guardarKeyerNVS();
}

// Para y reanuda el keyer sin salir de la pantalla (en Echo, mientras Chispas emite).
// Al pararlo, la espera deja que la tarea apague el LED antes de que lo use el motor CW
void keyerPausar() {
  keyerActivo = false;
  vTaskDelay(3);
}

void keyerReanudar() {
  xQueueReset(colaKeyerTexto);
  keyerActivo = true;
  xTaskNotifyGive(tareaKeyer);
}

