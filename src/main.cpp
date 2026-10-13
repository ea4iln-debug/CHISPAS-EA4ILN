/**
 * @file main.cpp
 * @author EA4ILN
 * @brief Chispas: entrenador de CW portatil para la Heltec WiFi LoRa 32 V3 (ESP32-S3). Koch, Echo, keyer
 *        iambico, practica y QSO por LoRa, manipulacion del transceptor (KEY), decodificador CW, propagacion,
 *        spots POTA, sol y linea gris, y actualizacion por OTA desde las releases de GitHub.
 * @version 1.06b - SALVAPANTALLAS en CONFIG, CONFIG por bloques y expresiones de radioaficionado en la cara
 * @date 2026-10-11
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 EA4ILN
 *
 * Este programa es software libre: puedes redistribuirlo y/o modificarlo bajo los terminos de la
 * GNU General Public License publicada por la Free Software Foundation, version 3 o (a tu eleccion)
 * cualquier version posterior. Se distribuye con la esperanza de que sea util, pero SIN NINGUNA
 * GARANTIA, ni siquiera la implicita de COMERCIABILIDAD o IDONEIDAD PARA UN FIN DETERMINADO.
 * Consulta el archivo LICENSE o <https://www.gnu.org/licenses/>.
 */

// Version: se ve en Inicio, en CONF > STATUS y por serie. La etiqueta de la release de GitHub es "v" + esto
// (la actualizacion OTA las compara). Cambiarla en cada version que se cargue o se publique
#define FW_VERSION "1.06b"

// Depuracion: se activa en CONF > CONFIG > DEPURACION (NVS "depu", un bit por opcion; todo apagado por defecto)
#define DEPU_SERIE 0x01  // Ordenes por serie: 'p' vuelca la OLED (docs/captura_oled.py) y d/i/k/l/a simulan el encoder
#define DEPU_DIAG  0x02  // "[DIAG]" cada minuto: memoria, pilas, NVS y CPU
#define DEPU_DECOD 0x04  // "[DECOD]" cada 2 s con el decodificador en marcha (para ajustar RV2 en banco)
#define DEPU_CW    0x08  // "[CW ...]": retraso de cada transicion del motor CW (medido: 0 ms)

#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <RadioLib.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>
#include <esp_sntp.h>  // Aviso de hora sincronizada por NTP y parada del NTP cuando la hora viene del GPS
#include "qrcode.h"  // Componente qrcode de ESP-IDF, incluido en el core (esp_qrcode_generate)
#include <nvs.h>
#include <esp_sleep.h>
#include <driver/gpio.h>
#include <driver/rtc_io.h>
#include <driver/adc.h>  // ADC por DMA (decodificador CW de la pantalla TRX)
#include "certificados_github.h"  // Raices TLS para la actualizacion OTA

volatile uint8_t depuracion = 0;  // Bits DEPU_*; la lee tambien la tarea del motor CW
inline bool depuActiva(uint8_t bit) { return (depuracion & bit) != 0; }

// El resto del firmware esta repartido por areas en src/parts/*.inc, incluidos aqui en su orden: el
// compilador ve un solo archivo, como antes de dividirlo (cada parte usa lo de las anteriores).
#include "parts/pines.inc"
#include "parts/morse.inc"
#include "parts/globales.inc"
#include "parts/audio.inc"
#include "parts/motor_cw.inc"
#include "parts/keyer.inc"
#include "parts/estadisticas.inc"
#include "parts/echo.inc"
#include "parts/bme280.inc"
#include "parts/memoria_nvs.inc"
#include "parts/bateria.inc"
#include "parts/oled_comun.inc"
#include "parts/lora.inc"
#include "parts/decodificador.inc"
#include "parts/trx.inc"
#include "parts/wifi.inc"
#include "parts/ota.inc"
#include "parts/datos_web.inc"
#include "parts/gps.inc"
#include "parts/pant_inicio_cw.inc"
#include "parts/pant_lora_trx.inc"
#include "parts/pant_prop_pota.inc"
#include "parts/sol.inc"
#include "parts/conf.inc"
#include "parts/estados_cw.inc"
#include "parts/cara.inc"
#include "parts/entrada.inc"
#include "parts/eventos.inc"

// ============================================================================
// SETUP Y BUCLE PRINCIPAL
// ============================================================================
void encenderPerifericos() {
  pinMode(VEXT_CTRL, OUTPUT);
  digitalWrite(VEXT_CTRL, LOW);   // Vext encendido
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  pinMode(KEYER_PADDLE_DIT, INPUT_PULLUP);
  pinMode(KEYER_PADDLE_DAH, INPUT_PULLUP);

  ledcSetup(CANAL_PWM_AUDIO, FREC_AUDIO_HZ, RES_PWM_AUDIO);
  ledcAttachPin(SPEAKER_PWM_PIN, CANAL_PWM_AUDIO);
  ledcWrite(CANAL_PWM_AUDIO, 0);

  delay(100);
}

/**
 * Reenvia a la OLED bomba de carga (0x8D 0x14) y encendido (0xAF), una vez, tras display.begin()
 * (arreglo de la v0.4e). Tras algunos reinicios cortos la OLED contesta por I2C y dice estar encendida,
 * pero el panel se queda negro. En la placa 1 aun pasa a veces: ver docs/NOTAS.md (pruebas de las v0.7u-z).
 */
void reafirmarOled() {
  const uint8_t cmdsOled[][2] = { {0x8D, 0x14}, {0xAF, 0x00} };  // Bomba de carga on, pantalla on
  for (uint8_t i = 0; i < 2; i++) {
    Wire.beginTransmission(0x3C);
    Wire.write((uint8_t)0x00);  // Byte de control: siguen comandos
    Wire.write(cmdsOled[i][0]);
    if (cmdsOled[i][0] == 0x8D) Wire.write(cmdsOled[i][1]);
    uint8_t err = Wire.endTransmission();
    if (err != 0) Serial.printf("[ERROR] OLED CMD 0x%02X: I2C %u\n", cmdsOled[i][0], err);
  }
}

// ============================================================================
// DORMIR (DEEP SLEEP) Y APAGADO AUTOMATICO
// ============================================================================
// PRG largo, o apagadoAutoMin minutos sin tocar nada (se elige en CONF), pone la placa en deep sleep: OLED, Vext, LoRa, WiFi
// y LED apagados. Se despierta pulsando PRG (GPIO0, pin RTC: wakeup ext0 a nivel bajo) o el eje del encoder
// (GPIO20, pin RTC: wakeup ext1 a nivel bajo) y arranca de nuevo como al encenderla (la configuracion esta en NVS).

void dormirChispas(const char* motivo) {
#ifdef SIN_ENCODER
  Serial.printf("[SUENO] %s: a dormir. PRG para despertar\n", motivo);
#else
  Serial.printf("[SUENO] %s: a dormir. PRG o el eje del encoder para despertar\n", motivo);
#endif
  motorCWDetener();
  keyerActivo = false;
  if (estadoWifi != WIFI_APAGADO) apagarWifi();
  radio.sleep();

  display.dim(false);
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(22, 16); display.print(F("CHISPAS DUERME"));
#ifdef SIN_ENCODER
  display.setCursor(10, 34); display.print(F("PULSA PRG PARA"));
  display.setCursor(10, 44); display.print(F("DESPERTARLO"));
#else
  display.setCursor(4, 34); display.print(F("PULSA PRG O EL MANDO"));
  display.setCursor(16, 44); display.print(F("PARA DESPERTARLO"));
#endif
  display.display();
  unsigned long inicioMensaje = millis();
  dormirGpsAntesDeSueno();  // Hasta ~1,5 s, mientras se lee el mensaje
  unsigned long transcurrido = millis() - inicioMensaje;
  if (transcurrido < 1500) delay(1500 - transcurrido);
  display.ssd1306_command(SSD1306_DISPLAYOFF);

  // Salidas en reposo y retenidas durante el sueno (si no, quedan al aire): KEY en BAJO, Vext
  // apagada (ALTO: OLED sin alimentacion) y divisor de bateria desactivado
  digitalWrite(LED_PIN, LOW);
  ledcWrite(CANAL_PWM_AUDIO, 0);
  digitalWrite(KEY_OUT_PIN, LOW);
  digitalWrite(AMP_SD_PIN, LOW);  // Amplificador apagado: el 3V3 sigue vivo durante el sueno
  digitalWrite(VEXT_CTRL, HIGH);
  digitalWrite(ADC_CTRL_PIN, LOW);
  gpio_hold_en((gpio_num_t)KEY_OUT_PIN);
  gpio_hold_en((gpio_num_t)AMP_SD_PIN);
  gpio_hold_en((gpio_num_t)VEXT_CTRL);
  gpio_hold_en((gpio_num_t)ADC_CTRL_PIN);
  gpio_deep_sleep_hold_en();

  // Despertar con PRG: GPIO0 a nivel bajo, con su pull-up activo durante el sueno
  rtc_gpio_pullup_en(GPIO_NUM_0);
  rtc_gpio_pulldown_dis(GPIO_NUM_0);
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);
#ifndef SIN_ENCODER
  // Y con el eje del encoder (sin pull-up en el modulo: el del RTC, que sigue alimentado por ext0)
  rtc_gpio_pullup_en((gpio_num_t)ENC_SW_PIN);
  rtc_gpio_pulldown_dis((gpio_num_t)ENC_SW_PIN);
  esp_sleep_enable_ext1_wakeup(1ULL << ENC_SW_PIN, ESP_EXT1_WAKEUP_ANY_LOW);
#endif

  Serial.flush();
  esp_deep_sleep_start();
}

/**
 * Apagado automatico tras apagadoAutoMin minutos sin tocar nada (0 = nunca), salvo en las pantallas CW y
 * LoRa (Koch, keyer, Echo, Duo, QSO: se puede estar usando sin tocar nada) o con el portal abierto.
 */
void procesarApagadoAuto() {
  if (apagadoAutoMin == 0) return;  // "nunca"
  if (millis() - ultimaActividadMs < apagadoAutoMin * 60000UL) return;
  // En CW y LoRa (y sus subpantallas) nunca: son las de practicar. Tampoco con el portal abierto
  if (pantallaActual == PANTALLA_CW || pantallaActual == PANTALLA_LORA || pantallaActual == PANTALLA_TRX ||
      estadoWifi == WIFI_PORTAL) return;
  dormirChispas("SIN USO");
}

void setup() {
  // Al despertar del deep sleep siguen retenidos los pines que se fijaron al dormir: se liberan
  gpio_deep_sleep_hold_dis();
  gpio_hold_dis((gpio_num_t)KEY_OUT_PIN);
  gpio_hold_dis((gpio_num_t)AMP_SD_PIN);
  gpio_hold_dis((gpio_num_t)VEXT_CTRL);
  gpio_hold_dis((gpio_num_t)ADC_CTRL_PIN);
#ifndef SIN_ENCODER
  rtc_gpio_deinit((gpio_num_t)ENC_SW_PIN);  // Tras despertar por ext1 sigue en el RTC: se devuelve a GPIO
#endif

  // Lo primero: la salida KEY en BAJO, para que la emisora no transmita mientras arranca la placa
  pinMode(KEY_OUT_PIN, OUTPUT);
  digitalWrite(KEY_OUT_PIN, LOW);
  pinMode(AMP_SD_PIN, OUTPUT);  // Amplificador apagado hasta entrar en una pantalla que suene
  digitalWrite(AMP_SD_PIN, LOW);

  Serial.begin(115200);
  delay(1000);
  Serial.println(F("\n[INIT] Chispas EA4ILN v" FW_VERSION "..."));
  cpuSegunWifi(false);  // Ahorro: 80 MHz mientras el WiFi este apagado
#ifdef SIN_ENCODER
  Serial.println(F("[INIT] MODO SIN ENCODER: PRG corto = mover, largo = ejecutar, muy largo = dormir"));
#endif
  
  encenderPerifericos();
  iniciarMotorCW();
  iniciarKeyer();
  iniciarDecodificador();
  iniciarEncoder();

  pinMode(BUTTON_PIN, INPUT_PULLUP);

  Serial.printf("[INIT] MOTIVO DEL REINICIO: %d%s\n", (int)esp_reset_reason(),
                esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0 ? " (despierta con PRG)" :
                esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT1 ? " (despierta con el encoder)" : "");

  // display.begin() no comprueba que la OLED responda (solo falla si no hay memoria).
  // Se comprueba el ACK en I2C tras un pulso de reset y, si no contesta, se corta Vext y se reintenta.
  // Nota (v0.7u-z): en la placa 1, tras algunos reinicios cortos, el panel queda negro aunque el
  // controlador contesta y dice estar encendido. Se probaron reenvios periodicos (parpadeaban), cortar
  // Vext al arrancar (seguia fallando a veces) y otras variantes; se mantiene esta secuencia (la de la
  // v0.4e). Sospecha: caidas de 3,3 V (la OLED parpadea al arrancar la LoRa); sin comprobar con otra
  // alimentacion. Detalles en docs/NOTAS.md.
  Wire.begin(OLED_SDA, OLED_SCL);
  bool oledResponde = false;
  pinMode(OLED_RST, OUTPUT);
  for (uint8_t intento = 1; intento <= 3 && !oledResponde; intento++) {
    digitalWrite(OLED_RST, LOW);   // Mientras RST esta bajo el SSD1306 no contesta
    delay(10);
    digitalWrite(OLED_RST, HIGH);
    delay(10);
    Wire.beginTransmission(0x3C);
    oledResponde = (Wire.endTransmission() == 0);
    if (!oledResponde) {
      Serial.printf("[ERROR] OLED NO RESPONDE (intento %u)\n", intento);
      digitalWrite(VEXT_CTRL, HIGH);  // Vext apagado
      delay(200);
      digitalWrite(VEXT_CTRL, LOW);   // Vext encendido
      delay(200);
    }
  }
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("[ERROR] OLED: SIN MEMORIA PARA EL BUFFER"));
  }

  reafirmarOled();

  randomSeed(esp_random());  // GPIO0 no es ADC en el S3: la semilla anterior era casi fija
  seqTx = random(256);       // Si la otra placa se reinicia, que no repita el ultimo seq

  cargarDatosNVS();
  ajustarApagado(0);  // Si lo guardado no es un valor de la lista, lo corrige
  aplicarFrecTono(false);
  cargarEstacionNVS();
  iniciarBME();  // Sensor ambiental opcional en Wire1 (GPIO41/42)
  iniciarGps();  // GPS opcional en UART1 (GPIO47/39): se detecta en unos segundos en procesarGps()
  setenv("TZ", ZONA_HORARIA, 1);  // Para que la hora manual del portal se interprete como local
  tzset();
  bateriaActual = leerBateria();
  actualizarPantalla();

  Serial.println(F("[LORA] INICIANDO SX1262..."));
  int state = radio.begin(LORA_FREC_MHZ, LORA_BW_KHZ, LORA_SF, LORA_CR, LORA_SYNC_WORD,
                          LORA_POTENCIA_DBM, LORA_PREAMBULO, LORA_TCXO_V);
  if (state == RADIOLIB_ERR_NONE) {
    estadoLoraStr = String(LORA_FREC_MHZ, 1) + "MHZ SF" + String(LORA_SF);  // Cabe en 21 caracteres con "LORA: "
    Serial.println(F("[LORA] MODULO OK. VINCULANDO ISR EN DIO1..."));
    
    radio.setPacketReceivedAction(cbInterrupcionLora);
    radio.sleep();  // Ahorro: solo escucha en Duo y QSO (al entrar se llama a startReceive)
  } else {
    estadoLoraStr = "ERR (" + String(state) + ")";
    Serial.print(F("[LORA ERROR] CODE: "));
    Serial.println(state);
  }

  actualizarPantalla();
}

/**
 * Lee la bateria cada INTERVALO_BATERIA_MS y redibuja solo si algo ha cambiado.
 * Sustituye al refresco fijo cada 3 s. Con el motor CW en su tarea, puede hacerse
 * aunque suene CW.
 */
void procesarBateriaYPantalla() {
  static unsigned long ultimaLecturaBat = 0;
  // Con el decodificador en marcha el ADC1 es suyo (DMA): la bateria se deja como estaba
  if (!decodActivo && millis() - ultimaLecturaBat >= INTERVALO_BATERIA_MS) {
    ultimaLecturaBat = millis();
    BateriaInfo nueva = leerBateria();
    bool cambia = nueva.estado != bateriaActual.estado ||
                  nueva.porcentaje != bateriaActual.porcentaje ||
                  (pantallaActual == PANTALLA_CONFIG && fabs(nueva.voltaje - bateriaActual.voltaje) >= 0.01);
    bateriaActual = nueva;
    if (cambia) pantallaPendiente = true;
  }

  // El reloj de la cabecera cambia cada minuto
  static int ultimoMinuto = -1;
  struct tm local;
  if (horaLocalValida(local) && local.tm_min != ultimoMinuto) {
    ultimoMinuto = local.tm_min;
    pantallaPendiente = true;
  }

  if (pantallaPendiente) {
    pantallaPendiente = false;
    actualizarPantalla();
  }
}

/**
 * Diagnostico por serie cada DIAG_INTERVALO_MS: memoria (libre, minimo historico y mayor bloque, para
 * vigilar la fragmentacion), pila libre de cada tarea, uso de la NVS y frecuencia de la CPU
 */
#define DIAG_INTERVALO_MS 60000UL

void procesarDiagnostico() {
  static unsigned long ultimoMs = 0;
  if (millis() - ultimoMs < DIAG_INTERVALO_MS) return;
  ultimoMs = millis();
  nvs_stats_t nvs;
  bool hayNvs = nvs_get_stats(NULL, &nvs) == ESP_OK;
  Serial.printf("[DIAG] %lu min | heap libre %u, minimo %u, mayor bloque %u | pila libre: loop %u, motorCW %u, keyer %u | "
                "NVS %u/%u entradas | CPU %u MHz\n",
                millis() / 60000, ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap(),
                uxTaskGetStackHighWaterMark(NULL), uxTaskGetStackHighWaterMark(tareaMotorCW),
                uxTaskGetStackHighWaterMark(tareaKeyer), hayNvs ? nvs.used_entries : 0,
                hayNvs ? nvs.total_entries : 0, getCpuFrequencyMhz());
}

/**
 * Ahorro: la OLED pasa al minimo de brillo (display.dim: contraste 0) tras BRILLO_TENUE_MS sin tocar
 * nada (o con la cara dormida) en cualquier pantalla, y vuelve al normal con cualquier giro, pulsacion o
 * manipulacion con las palas
 */
#define BRILLO_TENUE_MS 60000UL

void procesarBrillo() {
  static bool tenue = false;
  bool debeSerTenue = millis() - ultimaActividadMs >= BRILLO_TENUE_MS || (caraActiva && expresionCara == EXP_DORMIDA);
  if (debeSerTenue != tenue) {
    tenue = debeSerTenue;
    display.dim(tenue);
  }
}

// Ordenes por serie para capturas y pruebas desde el PC. 'p' vuelca el buffer de la OLED (1024 bytes, formato
// del SSD1306: byte = x + (y / 8) * 128, bit = y % 8) en hexadecimal entre dos marcas; docs/captura_oled.py lo
// guarda en PNG (es la copia en RAM: no ve si el panel esta negro o atenuado). 'd' derecha, 'i' izquierda,
// 'k' pulsar, 'l' pulsacion larga del eje y 'a' atras (PRG corto) simulan el encoder
void procesarOrdenesSerie() {
  if (!Serial.available()) return;
  int c = Serial.read();
  switch (c) {
    case 'd': eventoSerie = ENC_DERECHA; return;
    case 'i': eventoSerie = ENC_IZQUIERDA; return;
    case 'k': eventoSerie = ENC_PULSA; return;
    case 'l': eventoSerie = ENC_LARGA; return;
    case 'a': eventoSerie = BOTON_CORTA; return;
  }
  if (c != 'p') return;
  const uint8_t* buf = display.getBuffer();
  Serial.println(F("[CAPTURA] INICIO"));
  char hex[3];
  for (int i = 0; i < 1024; i++) {
    snprintf(hex, sizeof(hex), "%02X", buf[i]);
    Serial.print(hex);
    if ((i & 63) == 63) Serial.println();
  }
  Serial.println(F("[CAPTURA] FIN"));
}

void loop() {
  if (depuActiva(DEPU_SERIE)) procesarOrdenesSerie();
  procesarBoton();
  procesarAmplificador();
  procesarTonoPrueba();
  procesarTextoKeyer();
  procesarEcho();
  procesarCara();
  procesarMaquinaEstadosCW();
  procesarFinEmisionDuo();
  procesarRecepcionLoRa();
  procesarColaRxDuo();
  procesarQSO();
  procesarTrx();
  procesarWifi();
  procesarOta();
  procesarGps();
  procesarBME();
  procesarBateriaYPantalla();
  if (depuActiva(DEPU_DIAG)) procesarDiagnostico();
  procesarStatus();
  procesarBrillo();
  procesarApagadoAuto();

  // Ahorro: 1 ms de descanso por vuelta. Sin el, loop() tenia un nucleo al 100 %; con el, la CPU espera
  // en reposo. Las entradas siguen respondiendo (encoder por interrupcion; pulsadores cada ~1 ms) y el
  // CW no se ve afectado (motor y keyer van en sus propias tareas)
  vTaskDelay(1);
}