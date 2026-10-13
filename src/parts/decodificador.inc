// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 EA4ILN
// Chispas, parte de main.cpp: decodificador CW (tarea decod, Goertzel, ADC por DMA).
// Se incluye desde main.cpp en su orden (una sola unidad de compilacion); no compila suelta.

// ============================================================================
// DECODIFICADOR CW DEL AUDIO DE LA EMISORA (GPIO6, ADC1)
// ============================================================================
// Una tarea en el nucleo 0 lee el ADC por DMA a 8 kHz (sin cargar loop()) y, cada bloque de 96 muestras
// (12 ms), mide con Goertzel la energia del tono. Umbral adaptativo (ruido de fondo y pico) con histeresis;
// la duracion de las marcas y los silencios, comparada con un punto que se adapta a la velocidad, da puntos,
// rayas, fin de caracter y espacio. Tono fijo (400-1000 Hz) o automatico: busca el mas fuerte de 400 a
// 1000 Hz en pasos de 50. Mientras sale por KEY (y 300 ms despues) no escucha, para no descodificar el tono
// de la propia emisora. Silenciador: un tono solo cuenta si supera DECOD_SNR_INICIO veces el ruido medio y dura
// al menos dos bloques (con solo ruido de banda no descodifica). El fin de palabra se adapta a la pausa real
// entre letras (vale con espaciado Farnsworth). Probado con Morse limpio el 11 oct 2026.
#define DECOD_CANAL_ADC   5     // GPIO6 = ADC1, canal 5
#define DECOD_FS          8000  // Hz
#define DECOD_N           96    // Muestras por bloque (12 ms, ancho de banda ~80 Hz)
#define DECOD_BLOQUE_MS   12
#define DECOD_NUM_BINS    13    // 400..1000 Hz cada 50 (modo automatico)
#define DECOD_MIN_SENAL   1500.0f   // Magnitud minima para considerar que hay tono (amplitud ~30 cuentas)
#define DECOD_ESCALA_MAX  96000.0f  // Magnitud a fondo de escala (amplitud ~2000 cuentas): barra al 100 %
#define DECOD_SORDO_TRAS_KEY_MS 300
#define DECOD_SNR_INICIO  4.0f  // Un tono empieza si supera 4 veces el ruido medio (12 dB)
#define DECOD_AUTO_DESTACA 2.5f // AUTO: se cambia de tono si destaca 2,5 veces sobre la media de todos
#define DECOD_MARCA_MIN_MS 24   // Marcas mas cortas (un bloque) son ruido
#define DECOD_PALABRA_X   1.7f  // Fin de palabra: silencio de 1,7 veces la pausa media entre letras
#define DECOD_PILA        4096

TaskHandle_t tareaDecod = NULL;
QueueHandle_t colaDecod = NULL;         // Caracteres descodificados -> loop()
volatile bool decodActivo = false;      // Lo escribe loop()
volatile bool decodEnUso = false;       // Lo escribe la tarea: esta leyendo el ADC
volatile uint8_t decodNivelPct = 0;     // Barra de nivel (escala logaritmica)
volatile uint8_t decodUmbralPct = 0;    // Marca del umbral en la barra
volatile uint16_t decodFrecHz = 700;    // Tono que se esta usando
volatile uint8_t decodWpm = 0;          // Velocidad estimada (0 = aun nada)
volatile bool decodSaturado = false;    // La entrada llega a los topes del ADC: bajar RV2
volatile bool decodTono = false;        // Hay tono ahora

float goertzel(const float* x, float coef) {
  float s1 = 0, s2 = 0;
  for (int i = 0; i < DECOD_N; i++) {
    float s = x[i] + coef * s1 - s2;
    s2 = s1;
    s1 = s;
  }
  float p = s1 * s1 + s2 * s2 - coef * s1 * s2;
  return p > 0 ? sqrtf(p) : 0;
}

float coefGoertzel(float f) {
  return 2.0f * cosf(2.0f * PI * f / DECOD_FS);
}

uint8_t nivelEnBarra(float m) {
  if (m <= DECOD_MIN_SENAL) return 0;
  float pct = 100.0f * logf(m / DECOD_MIN_SENAL) / logf(DECOD_ESCALA_MAX / DECOD_MIN_SENAL);
  return (uint8_t)constrain(pct, 0.0f, 100.0f);
}

void decodEnviarCaracter(char c) {
  xQueueSend(colaDecod, &c, 0);
}

void tareaDecodFn(void* parametro) {
  static float muestras[DECOD_N];
  static uint8_t crudo[DECOD_N * SOC_ADC_DIGI_RESULT_BYTES];
  int n = 0;
  float energiaBin[DECOD_NUM_BINS] = {0};
  uint8_t binActual = 6;  // 700 Hz
  float ruido = DECOD_MIN_SENAL, pico = DECOD_MIN_SENAL;
  bool tono = false;
  uint32_t marcaMs = 0, silencioMs = 0;
  float puntoMs = 60;  // 20 wpm para empezar
  char patron[9];
  uint8_t largo = 0;
  bool espacioPendiente = false;
  bool saturado = false;
  float huecoLetraMs = 3 * puntoMs;  // Pausa media entre letras (media movil): da el fin de palabra

  for (;;) {
    if (!decodActivo) {
      decodEnUso = false;
      n = 0;
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      continue;
    }
    decodEnUso = true;

    uint32_t leidos = 0;
    esp_err_t err = adc_digi_read_bytes(crudo, sizeof(crudo), &leidos, 50);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) continue;  // Sin datos (o se ha desbordado: se sigue)

    for (uint32_t i = 0; i + SOC_ADC_DIGI_RESULT_BYTES <= leidos; i += SOC_ADC_DIGI_RESULT_BYTES) {
      adc_digi_output_data_t* d = (adc_digi_output_data_t*)&crudo[i];
      if (d->type2.unit != 0 || d->type2.channel != DECOD_CANAL_ADC) continue;
      uint16_t v = d->type2.data;
      if (v < 40 || v > 4055) saturado = true;
      muestras[n++] = v;
      if (n < DECOD_N) continue;
      n = 0;

      // Bloque completo: se quita la continua (polarizacion a 1,65 V)
      float media = 0;
      for (int k = 0; k < DECOD_N; k++) media += muestras[k];
      media /= DECOD_N;
      for (int k = 0; k < DECOD_N; k++) muestras[k] -= media;

      float m;
      if (trxTonoDecodHz == 0) {
        // Automatico: energia de cada tono, suavizada; se cambia al mas fuerte si destaca de verdad
        // (y sobre la media de todos: el ruido es plano y un tono no)
        uint8_t mejor = binActual;
        float mediaBins = 0;
        for (uint8_t b = 0; b < DECOD_NUM_BINS; b++) {
          float e = goertzel(muestras, coefGoertzel(400 + 50 * b));
          energiaBin[b] = 0.9f * energiaBin[b] + 0.1f * e;
          mediaBins += energiaBin[b];
          if (energiaBin[b] > energiaBin[mejor]) mejor = b;
        }
        mediaBins /= DECOD_NUM_BINS;
        if (mejor != binActual && energiaBin[mejor] > 1.5f * energiaBin[binActual] &&
            energiaBin[mejor] > DECOD_AUTO_DESTACA * mediaBins && energiaBin[mejor] > DECOD_MIN_SENAL) {
          binActual = mejor;
        }
        decodFrecHz = 400 + 50 * binActual;
      } else {
        decodFrecHz = trxTonoDecodHz;
      }
      m = goertzel(muestras, coefGoertzel(decodFrecHz));

      // Umbral adaptativo: ruido de fondo y pico (sube rapido, baja despacio). El ruido se mide como un suelo:
      // baja enseguida y sube muy despacio, para que los bordes de los puntos y rayas no lo inflen (con un ruido
      // inflado, el silenciador se comia elementos)
      if (!tono) ruido = (m < ruido) ? 0.7f * ruido + 0.3f * m : 0.98f * ruido + 0.02f * m;
      if (m > pico) pico = 0.5f * pico + 0.5f * m;
      else pico = 0.995f * pico;
      if (pico < ruido * 2) pico = ruido * 2;
      float alto = ruido + 0.55f * (pico - ruido);
      float bajo = ruido + 0.40f * (pico - ruido);
      // Para empezar, ademas, el silenciador: destacar sobre el ruido medio (con solo ruido no hay tono)
      bool hayTono = tono ? (m > bajo)
                          : (m > alto && m > DECOD_MIN_SENAL && pico > 3 * ruido && m > DECOD_SNR_INICIO * ruido);

      // Sordo mientras se emite por KEY y justo despues
      bool sordo = keyActivaDesdeMs != 0 || millis() - keyFinMs < DECOD_SORDO_TRAS_KEY_MS;
      if (sordo) hayTono = false;

      decodNivelPct = nivelEnBarra(m);
      decodUmbralPct = nivelEnBarra(alto);
      decodSaturado = saturado;
      saturado = false;
      decodTono = hayTono;

      if (hayTono) {
        if (!tono) marcaMs = 0;
        marcaMs += DECOD_BLOQUE_MS;
      } else {
        if (tono) {
          // Fin de una marca: punto o raya segun el punto estimado; las muy cortas son ruido
          if (marcaMs >= max((float)DECOD_MARCA_MIN_MS, puntoMs * 0.4f)) {
            // El silencio de antes de esta marca, si cerro una letra sin llegar a palabra, es una pausa entre
            // letras: su media da el fin de palabra
            if (espacioPendiente && silencioMs >= 2 * puntoMs) {
              huecoLetraMs = constrain(0.75f * huecoLetraMs + 0.25f * silencioMs, 3 * puntoMs, 15 * puntoMs);
            }
            bool raya = marcaMs > 2 * puntoMs;
            if (largo < 8) patron[largo] = raya ? '-' : '.';
            if (largo < 8) largo++;
            float estimado = raya ? marcaMs / 3.0f : marcaMs;
            puntoMs = constrain(0.7f * puntoMs + 0.3f * estimado, 24.0f, 240.0f);  // 50 a 5 wpm
            decodWpm = (uint8_t)(1200 / puntoMs);
            espacioPendiente = false;
          }
          silencioMs = 0;
        }
        silencioMs += DECOD_BLOQUE_MS;
        if (largo > 0 && silencioMs >= 2 * puntoMs) {
          patron[min(largo, (uint8_t)8)] = '\0';
          decodEnviarCaracter(largo <= 7 ? decodificarPatron(patron) : '*');
          largo = 0;
          espacioPendiente = true;
        } else if (espacioPendiente && silencioMs >= max(5 * puntoMs, DECOD_PALABRA_X * huecoLetraMs)) {
          decodEnviarCaracter(' ');
          espacioPendiente = false;
        }
      }
      tono = hayTono;
    }
  }
}

void iniciarDecodificador() {
  colaDecod = xQueueCreate(64, sizeof(char));
  xTaskCreatePinnedToCore(tareaDecodFn, "decod", DECOD_PILA, NULL, 2, &tareaDecod, 0);
}

// Pone en marcha el ADC por DMA en GPIO6 y despierta la tarea. Mientras, la bateria no se lee (mismo ADC1)
void decodEmpezar() {
  if (decodActivo) return;
  adc_digi_init_config_t ini = {};
  ini.max_store_buf_size = DECOD_N * SOC_ADC_DIGI_RESULT_BYTES * 4;
  ini.conv_num_each_intr = DECOD_N * SOC_ADC_DIGI_RESULT_BYTES;
  ini.adc1_chan_mask = BIT(DECOD_CANAL_ADC);
  ini.adc2_chan_mask = 0;
  esp_err_t err = adc_digi_initialize(&ini);
  if (err != ESP_OK) {
    Serial.printf("[DECOD] ERROR AL INICIAR EL ADC: %d\n", err);
    return;
  }
  adc_digi_pattern_config_t patron = {};
  patron.atten = ADC_ATTEN_DB_12;
  patron.channel = DECOD_CANAL_ADC;
  patron.unit = 0;  // ADC1
  patron.bit_width = SOC_ADC_DIGI_MAX_BITWIDTH;
  adc_digi_configuration_t cfg = {};
  cfg.conv_limit_en = false;
  cfg.conv_limit_num = 250;
  cfg.pattern_num = 1;
  cfg.adc_pattern = &patron;
  cfg.sample_freq_hz = DECOD_FS;
  cfg.conv_mode = ADC_CONV_SINGLE_UNIT_1;
  cfg.format = ADC_DIGI_OUTPUT_FORMAT_TYPE2;
  err = adc_digi_controller_configure(&cfg);
  if (err == ESP_OK) err = adc_digi_start();
  if (err != ESP_OK) {
    Serial.printf("[DECOD] ERROR AL CONFIGURAR EL ADC: %d\n", err);
    adc_digi_deinitialize();
    return;
  }
  decodWpm = 0;
  xQueueReset(colaDecod);
  decodActivo = true;
  xTaskNotifyGive(tareaDecod);
  Serial.println(F("[DECOD] EN MARCHA (GPIO6)"));
}

void decodParar() {
  if (!decodActivo) return;
  decodActivo = false;
  unsigned long inicio = millis();
  while (decodEnUso && millis() - inicio < 200) vTaskDelay(1);  // La lectura caduca a los 50 ms
  adc_digi_stop();
  adc_digi_deinitialize();
  decodNivelPct = 0;
  decodTono = false;
  Serial.println(F("[DECOD] PARADO"));
}

