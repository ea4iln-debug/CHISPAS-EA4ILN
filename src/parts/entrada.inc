// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 EA4ILN
// Chispas, parte de main.cpp: encoder, ISR y eventos de boton.
// Se incluye desde main.cpp en su orden (una sola unidad de compilacion); no compila suelta.

// ============================================================================
// ENTRADA DE USUARIO: EVENTOS DE BOTON
// ============================================================================
// Toda la logica de menus trabaja con eventos, no con el hardware. Hay dos fuentes:
// - Boton PRG: corta / larga, como siempre.
// - Encoder: giro a derecha / izquierda (mueve el foco o cambia el valor que se edita),
//   pulsacion corta del eje (= corta del PRG) y larga del eje (= larga del PRG, salvo
//   donde esta solo mueve el foco: ahi no hace nada, o sale de la edicion).
enum EventoBoton { BOTON_NINGUNO, BOTON_CORTA, BOTON_LARGA, ENC_DERECHA, ENC_IZQUIERDA, ENC_PULSA, ENC_LARGA };
EventoBoton eventoSerie = BOTON_NINGUNO;  // Evento simulado por serie (ver procesarOrdenesSerie)

struct Pulsador {
  uint8_t pin;
  bool ultimoEstado;
  unsigned long inicio;
  bool pulsando;
  unsigned long ultimaDuracion;  // De la ultima pulsacion (para la muy larga del modo sin encoder)
};
Pulsador pulsadorPrg = { BUTTON_PIN, HIGH, 0, false, 0 };
Pulsador pulsadorEje = { ENC_SW_PIN, HIGH, 0, false, 0 };

/**
 * Lee un pulsador sin bloquear y da el evento al SOLTARLO: 1 = corta (mas de
 * TIEMPO_ANTIRREBOTE_MS), 2 = larga (>= TIEMPO_PULSACION_LARGA), 0 = nada.
 * Los rebotes dan pulsaciones de menos de TIEMPO_ANTIRREBOTE_MS y se ignoran
 * (el pulsador del encoder rebota al soltar: probado en placa).
 */
uint8_t leerPulsador(Pulsador& p) {
  bool estadoActual = digitalRead(p.pin);
  uint8_t ev = 0;

  if (p.ultimoEstado == HIGH && estadoActual == LOW) {
    p.inicio = millis();
    p.pulsando = true;
  }

  if (p.ultimoEstado == LOW && estadoActual == HIGH && p.pulsando) {
    unsigned long duracion = millis() - p.inicio;
    p.pulsando = false;
    p.ultimaDuracion = duracion;

    if (duracion >= TIEMPO_PULSACION_LARGA) ev = 2;
    else if (duracion > TIEMPO_ANTIRREBOTE_MS) ev = 1;
  }

  p.ultimoEstado = estadoActual;
  return ev;
}

// Encoder por interrupcion: un cambio en CLK o DT avanza un decodificador de cuadratura por tabla.
// El KY-040 da 4 transiciones por paso y reposa con CLK = DT = 1. Los rebotes van y vuelven
// en la tabla y no suman pasos. Sentido horario = positivo (probado en placa).
static_assert(ENC_CLK_PIN < 32 && ENC_DT_PIN < 32, "La ISR lee CLK y DT de GPIO_IN_REG (GPIO 0-31)");
DRAM_ATTR const int8_t TABLA_CUADRATURA[16] = { 0, -1, 1, 0, 1, 0, 0, -1, -1, 0, 0, 1, 0, 1, -1, 0 };
volatile uint8_t encEstado = 3;
volatile int8_t encAcumulado = 0;
volatile int16_t encPasos = 0;  // Pasos pendientes de procesar (+ horario, - antihorario)
// Aceleracion: la ISR mide el tiempo entre pasos. Menos de ENC_RAPIDO_MS = giro rapido: ajustarValor()
// avanza aceleracionGiro veces mas en los valores con muchos pasos (no en la cola de eventos: se mide
// en el encoder, asi que un loop() ocupado no lo dispara). aceleracionGiro se elige en CONF > CONFIG
#define ENC_RAPIDO_MS 40
#define ENC_MIN_PASOS_ACELERAR 20  // Valores con menos pasos de minimo a maximo, sin aceleracion
volatile TickType_t encUltimoPasoTick = 0;
volatile TickType_t encIntervaloTicks = portMAX_DELAY;
bool giroRapido = false;  // El paso que se esta procesando llego rapido (lo pone leerEventoBoton)
portMUX_TYPE muxEncoder = portMUX_INITIALIZER_UNLOCKED;

// Solo registros y datos en RAM: puede ejecutarse mientras se escribe la flash (NVS)
void IRAM_ATTR isrEncoder() {
  uint32_t entradas = REG_READ(GPIO_IN_REG);
  uint8_t nuevo = (((entradas >> ENC_CLK_PIN) & 1) << 1) | ((entradas >> ENC_DT_PIN) & 1);
  portENTER_CRITICAL_ISR(&muxEncoder);
  if (nuevo != encEstado) {
    encAcumulado += TABLA_CUADRATURA[(encEstado << 2) | nuevo];
    encEstado = nuevo;
    if (nuevo == 3) {
      if (encAcumulado >= 2 || encAcumulado <= -2) {
        if (encAcumulado >= 2) encPasos++;
        else encPasos--;
        TickType_t ahora = xTaskGetTickCountFromISR();
        encIntervaloTicks = ahora - encUltimoPasoTick;
        encUltimoPasoTick = ahora;
      }
      encAcumulado = 0;
    }
  }
  portEXIT_CRITICAL_ISR(&muxEncoder);
}

void iniciarEncoder() {
  pinMode(ENC_CLK_PIN, INPUT_PULLUP);
  pinMode(ENC_DT_PIN, INPUT_PULLUP);
  pinMode(ENC_SW_PIN, INPUT_PULLUP);
  encEstado = (digitalRead(ENC_CLK_PIN) << 1) | digitalRead(ENC_DT_PIN);
  attachInterrupt(digitalPinToInterrupt(ENC_CLK_PIN), isrEncoder, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC_DT_PIN), isrEncoder, CHANGE);
}

/**
 * Devuelve el siguiente evento de entrada: primero el PRG, luego el pulsador del eje y
 * luego un paso de giro (si se acumulan varios, se entregan de uno en uno).
 */
EventoBoton leerEventoBoton() {
  uint8_t prg = leerPulsador(pulsadorPrg);
#ifdef SIN_ENCODER
  // Placa sin encoder (entorno heltec_sin_encoder): el PRG hace de encoder. Corta = girar a la
  // derecha (mover el foco), larga = pulsar el eje (ejecutar), muy larga = PRG largo (dormir).
  // No hay atras: todas las pantallas tienen volver o ant./sig.
  if (prg == 1) return ENC_DERECHA;
  if (prg == 2) return (pulsadorPrg.ultimaDuracion >= TIEMPO_PULSACION_DORMIR) ? BOTON_LARGA : ENC_PULSA;
  return BOTON_NINGUNO;
#endif
  if (prg == 1) return BOTON_CORTA;
  if (prg == 2) return BOTON_LARGA;

  uint8_t eje = leerPulsador(pulsadorEje);
  if (eje == 1) return ENC_PULSA;
  if (eje == 2) return ENC_LARGA;

  int16_t pasos;
  TickType_t intervalo;
  portENTER_CRITICAL(&muxEncoder);
  pasos = encPasos;
  if (pasos > 0) encPasos--;
  else if (pasos < 0) encPasos++;
  intervalo = encIntervaloTicks;
  portEXIT_CRITICAL(&muxEncoder);
  giroRapido = (pasos != 0) && intervalo < pdMS_TO_TICKS(ENC_RAPIDO_MS);
  if (pasos > 0) return ENC_DERECHA;
  if (pasos < 0) return ENC_IZQUIERDA;
  return BOTON_NINGUNO;
}

/**
 * Mueve el foco por el anillo de una pantalla: posicion 0 = "sig.", 1..barra = opciones
 * 0..barra-1 y barra+1 = "ant.". Con paso +1 es el ciclo de siempre de la pulsacion larga.
 */
void moverFocoAnillo(uint8_t& opcion, uint8_t barra, int8_t paso) {
  uint8_t n = barra + 2;
  uint8_t pos = (opcion == barra) ? (opcionNavegacionP1234 == 1 ? 0 : barra + 1) : opcion + 1;
  pos = (pos + n + paso) % n;
  if (pos == 0) { opcion = barra; opcionNavegacionP1234 = 1; }
  else if (pos == barra + 1) { opcion = barra; opcionNavegacionP1234 = 0; }
  else opcion = pos - 1;
}

// Cambia un valor con el encoder, sin dar la vuelta al llegar a los limites. Al girar rapido, los valores
// con muchos pasos (fallos, wpm, leccion...) avanzan de ENC_FACTOR_RAPIDO en ENC_FACTOR_RAPIDO
template <typename T>
void ajustarValor(T& valor, int paso, int minimo, int maximo) {
#ifdef SIN_ENCODER
  // Sin encoder solo se avanza en un sentido: al pasar del maximo se vuelve al minimo
  int v = (int)valor + paso;
  valor = (T)(v > maximo ? minimo : (v < minimo ? maximo : v));
#else
  if (giroRapido && aceleracionGiro > 1 && paso != 0 && (maximo - minimo) / abs(paso) >= ENC_MIN_PASOS_ACELERAR) {
    paso *= aceleracionGiro;
  }
  valor = (T)constrain((int)valor + paso, minimo, maximo);
#endif
}

/**
 * Cambia de pantalla principal con las opciones en reposo y el foco en la barra: en "sig."
 * si se avanza (o se va a Inicio) y en "ant." si se retrocede, para poder seguir en el mismo sentido
 */
void entrarEnPantalla(uint8_t pantalla, bool haciaAtras = false) {
  pantallaActual = pantalla;
  opcionNavegacionP1234 = haciaAtras ? 0 : 1;
  if (pantalla == PANTALLA_CW) opcionPantalla3 = OPCION_BARRA_CW;
  else if (pantalla == PANTALLA_LORA) opcionPantalla4 = 2;
  else if (pantalla == PANTALLA_TRX) { opcionTrx = TRX_OPCION_BARRA; subTrx = TRX_MENU; }
  else if (pantalla == PANTALLA_PROPAGACION) { opcionPantallaProp = OPCION_BARRA_PROP; subDatos = DATOS_MENU; }
  else if (pantalla == PANTALLA_SPOTS) { opcionPantallaSpots = OPCION_BARRA_SPOTS; subDatos = DATOS_MENU; }
  else if (pantalla == PANTALLA_SOL) { opcionPantallaSol = OPCION_BARRA_DATOS; subDatos = DATOS_MENU; }
  else if (pantalla == PANTALLA_CONFIG) {
    opcionPantallaConfig = OPCION_BARRA_CONFIG;
    subConf = CONF_MENU;
    confirmandoFabrica = false;
  }
}

// "ant." o "sig." desde la barra inferior
void navegarDesdeBarra() {
  if (opcionNavegacionP1234 == 0) entrarEnPantalla(pantallaActual == 1 ? TOTAL_PANTALLAS : pantallaActual - 1, true);
  else entrarEnPantalla(pantallaActual == TOTAL_PANTALLAS ? 1 : pantallaActual + 1);
}

