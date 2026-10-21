// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 EA4ILN
// Chispas, parte de main.cpp: tarea motorCW y su API.
// Se incluye desde main.cpp en su orden (una sola unidad de compilacion); no compila suelta.

// ============================================================================
// MOTOR CW EN UNA TAREA FREERTOS
// ============================================================================
// El LED y el tono los maneja una tarea con mas prioridad que loop() y en su mismo nucleo:
// la temporizacion ya no depende de lo que bloquee loop() (OLED, LoRa, WiFi, bateria).
// loop() pide un texto con motorCWEmitir(), lo corta con motorCWDetener() y mira si ha
// terminado con motorCWOcupado(). Solo la tarea llama a conmutarEmisionMorse().
#define MOTOR_CW_TEXTO_MAX 64
#define MOTOR_CW_PRIORIDAD 3     // loop() corre con prioridad 1
#define MOTOR_CW_NUCLEO    1     // El de loop() (CONFIG_ARDUINO_RUNNING_CORE); el WiFi va en el 0
#define MOTOR_CW_PILA      4096

struct PeticionCW {
  uint32_t id;
  char texto[MOTOR_CW_TEXTO_MAX + 1];
  uint8_t wpm;
  uint8_t efectiva;  // Velocidad efectiva (Farnsworth): estira los huecos entre letras y entre palabras
  uint16_t portadoraMs;  // Distinto de 0: portadora continua (tune) en vez del texto
  const char* origen;  // Para la depuracion por serie
};

TaskHandle_t tareaMotorCW = NULL;
QueueHandle_t colaMotorCW = NULL;
// Cada peticion lleva un id creciente: la tarea esta ocupada mientras no haya terminado la ultima
volatile uint32_t motorCWPedidas = 0;     // Lo escribe loop()
volatile uint32_t motorCWCancelada = 0;   // Lo escribe loop(): las peticiones con id <= este se cortan
volatile uint32_t motorCWTerminadas = 0;  // Lo escribe la tarea
volatile unsigned long motorCWFinMs = 0;  // millis() al acabar la ultima emision

bool motorCWOcupado() {
  return motorCWTerminadas != motorCWPedidas;
}

/**
 * Espera hasta el tick objetivo. Devuelve false si la peticion se ha cancelado: motorCWDetener()
 * despierta la tarea con una notificacion, asi que no hay que esperar a que acabe la pausa.
 */
bool esperarMotorCW(TickType_t objetivo, uint32_t id) {
  for (;;) {
    if (id <= motorCWCancelada) return false;
    TickType_t ahora = xTaskGetTickCount();
    if ((int32_t)(objetivo - ahora) <= 0) {
      if (depuActiva(DEPU_CW)) registrarRetrasoCW(ahora - objetivo);
      return true;
    }
    ulTaskNotifyTake(pdTRUE, objetivo - ahora);
  }
}

/**
 * Huecos entre letras y entre palabras (totales, en ms) con velocidad efectiva, como LCWO (jscwlib de DJ1YFK):
 * el punto efectivo (1200 / efectiva) se estira con su formula ajustada a la medida, y los huecos son 3 y 7 de
 * esos puntos. Con la efectiva igual al wpm (o mayor, o 0), 3 y 7 puntos normales. 20/10: letra ~11 puntos.
 */
void huecosCW(uint8_t wpm, uint8_t efectiva, uint32_t& letraMs, uint32_t& palabraMs) {
  float ef = (efectiva == 0 || efectiva > wpm) ? wpm : efectiva;
  float estirar = 2.5f - 1.5f / powf(wpm / ef, 1.25f);
  float hueco = 1200.0f / ef * estirar;
  letraMs = lroundf(3 * hueco);
  palabraMs = lroundf(7 * hueco);
}

// Valor de la velocidad efectiva en las listas de ajustes: "NO" si no estira
String textoEfectiva(uint8_t efectiva, uint8_t wpm) {
  return efectiva >= wpm ? String("NO") : String(efectiva);
}

/**
 * Emite el texto con plazos absolutos (cada uno se suma al anterior, sin acumular retrasos).
 * Los espacios marcan el espacio entre palabras; los caracteres sin codigo Morse se saltan.
 */
void emitirTextoMotorCW(const PeticionCW& p) {
  if (p.portadoraMs != 0) {  // Tune: tareaEmisionCW apaga al volver, tambien si se cancela
    conmutarEmisionMorse(true);
    esperarMotorCW(xTaskGetTickCount() + pdMS_TO_TICKS(p.portadoraMs), p.id);
    return;
  }
  uint32_t tDit = 1200 / p.wpm;
  uint32_t tCharGap, tWordGap;
  huecosCW(p.wpm, p.efectiva, tCharGap, tWordGap);

  TickType_t objetivo = xTaskGetTickCount();
  bool primero = true;
  bool hayEspacio = false;
  for (const char* c = p.texto; *c != '\0'; c++) {
    if (*c == ' ') { hayEspacio = true; continue; }
    const char* codigo = obtenerCodigoMorse(*c);
    if (*codigo == '\0') continue;

    if (!primero) {
      objetivo += pdMS_TO_TICKS(hayEspacio ? tWordGap : tCharGap);
      if (!esperarMotorCW(objetivo, p.id)) return;
    }
    primero = false;
    hayEspacio = false;

    for (const char* e = codigo; *e != '\0'; e++) {
      if (e != codigo) {
        objetivo += pdMS_TO_TICKS(tDit);
        if (!esperarMotorCW(objetivo, p.id)) return;
      }
      conmutarEmisionMorse(true);
      objetivo += pdMS_TO_TICKS(*e == '-' ? tDit * 3 : tDit);
      if (!esperarMotorCW(objetivo, p.id)) return;
      conmutarEmisionMorse(false);
    }
  }
}

void tareaEmisionCW(void* parametro) {
  PeticionCW p;
  for (;;) {
    xQueueReceive(colaMotorCW, &p, portMAX_DELAY);
    unsigned long inicioMs = millis();
    emitirTextoMotorCW(p);
    conmutarEmisionMorse(false);  // Por si se ha cortado con el tono encendido
    motorCWFinMs = millis();
    if (depuActiva(DEPU_CW) && p.portadoraMs == 0) {
      uint32_t letraMs, palabraMs;
      huecosCW(p.wpm, p.efectiva, letraMs, palabraMs);
      Serial.printf("[CW %s] %u/%u wpm, huecos %u/%u ms, %lu ms\n", p.origen, p.wpm, p.efectiva, letraMs,
                    palabraMs, motorCWFinMs - inicioMs);
    }
    if (depuActiva(DEPU_CW)) informarRetrasoCW(p.origen);
    motorCWTerminadas = p.id;
  }
}

void iniciarMotorCW() {
  colaMotorCW = xQueueCreate(1, sizeof(PeticionCW));
  xTaskCreatePinnedToCore(tareaEmisionCW, "motorCW", MOTOR_CW_PILA, NULL, MOTOR_CW_PRIORIDAD,
                          &tareaMotorCW, MOTOR_CW_NUCLEO);
}

/**
 * Corta lo que este sonando y espera a que la tarea apague LED y tono.
 */
void motorCWDetener() {
  if (!motorCWOcupado()) return;
  motorCWCancelada = motorCWPedidas;
  xTaskNotifyGive(tareaMotorCW);
  unsigned long inicio = millis();
  while (motorCWOcupado() && millis() - inicio < 50) vTaskDelay(1);
}

/**
 * Emite un texto (corta antes lo que estuviera sonando). Vuelve enseguida.
 */
void motorCWEmitir(const char* texto, uint8_t wpm, uint8_t efectiva, const char* origen) {
  motorCWDetener();

  PeticionCW p;
  p.id = motorCWPedidas + 1;
  strncpy(p.texto, texto, MOTOR_CW_TEXTO_MAX);
  p.texto[MOTOR_CW_TEXTO_MAX] = '\0';
  p.wpm = max(wpm, (uint8_t)1);
  p.efectiva = efectiva;
  p.portadoraMs = 0;
  p.origen = origen;
  if (xQueueSend(colaMotorCW, &p, 0) == pdTRUE) motorCWPedidas = p.id;
}

/**
 * Portadora continua de ms milisegundos (tune), cortable con motorCWDetener(). Vuelve enseguida.
 */
void motorCWPortadora(uint16_t ms, const char* origen) {
  motorCWDetener();

  PeticionCW p;
  p.id = motorCWPedidas + 1;
  p.texto[0] = '\0';
  p.wpm = 1;
  p.efectiva = 1;
  p.portadoraMs = ms;
  p.origen = origen;
  if (xQueueSend(colaMotorCW, &p, 0) == pdTRUE) motorCWPedidas = p.id;
}

