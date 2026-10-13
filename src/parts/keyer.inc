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
extern bool pantallaPendiente;         // Definida con la bateria
extern unsigned long ultimaActividadMs;  // Definida con la cara (ultimo giro, pulsacion o manipulacion)

TaskHandle_t tareaKeyer = NULL;
QueueHandle_t colaKeyerTexto = NULL;  // Caracteres descodificados, de la tarea a loop()
volatile bool keyerActivo = false;

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

  for (;;) {
    if (!keyerActivo) {
      conmutarEmisionMorse(false);
      estado = K_ESPERA;
      largo = 0;
      espacioPendiente = false;
      memoriaDit = memoriaDah = false;
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);  // Hasta que se abra la pantalla del keyer
      continue;
    }

    vTaskDelay(1);
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
      elemento = siguiente;
      if (largo < 8) patron[largo] = siguiente;
      if (largo < 8) largo++;
      espacioPendiente = false;
      conmutarEmisionMorse(true);
      finFase = ahora + (siguiente == '.' ? dit : 3 * dit);
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
  opcionKeyer = 0;
  textoKeyer = "";
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

