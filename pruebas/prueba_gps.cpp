/**
 * Prueba del GPS NEO-6M: leer el NMEA y probar a dormirlo por UBX (sin transistor de enable).
 * Conexiones: TX del GPS -> GPIO47 (J2-13), RX del GPS <- GPIO39 (J3-10), VCC a 3V3, GND. UART a 9600 bd.
 *
 * Cada 5 s imprime un resumen: bytes recibidos, fijo, satelites, hora UTC y posicion (de GGA y RMC).
 * Ordenes por el serie (una letra; con serie.py: -e v, -e d...):
 *   n  NMEA crudo si/no (por defecto no: solo el resumen)
 *   v  pedir la version (UBX-MON-VER): comprueba el cable GPIO39 -> RX del GPS
 *   d  dormir: UBX-RXM-PMREQ en backup sin limite de tiempo
 *   w  despertar: rafaga de 0xFF por el RX del GPS
 *   p  parar el GNSS: UBX-CFG-RST, resetMode 0x08 (sigue el UART)
 *   a  arrancar el GNSS: UBX-CFG-RST, resetMode 0x09
 * Las respuestas UBX (ACK, NAK, MON-VER) salen por el serie.
 * Sin ordenador (para medir el consumo con un medidor USB): la OLED muestra el estado y PRG corto
 * alterna dormir / despertar el GPS.
 * Se carga con: pio run -e prueba_gps -t upload   (monitor a 115200)
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright (C) 2026 EA4ILN. Software libre, sin garantia: ver el archivo LICENSE.
 */
#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

#define GPS_RX_PIN 47  // Llega el TX del GPS
#define GPS_TX_PIN 39  // Va al RX del GPS
#define GPS_BAUD   9600
#define BUTTON_PIN 0   // PRG
#define VEXT_CTRL  36  // BAJO = Vext (OLED) encendido
#define OLED_SDA   17
#define OLED_SCL   18
#define OLED_RST   21

Adafruit_SSD1306 display(128, 64, &Wire, OLED_RST);
bool gpsDormido = false;
uint32_t bytesUltimoPeriodo = 0;

bool nmeaCrudo = false;
uint32_t bytesPeriodo = 0;
unsigned long ultimoResumen = 0;

// Ultimos datos de GGA y RMC
char horaUtc[11] = "-";
char latitud[16] = "-";
char longitud[16] = "-";
int calidadFijo = 0;   // GGA campo 6: 0 sin fijo, 1 GPS, 2 DGPS
int satelites = 0;     // GGA campo 7
char estadoRmc = 'V';  // RMC campo 2: A valido, V no

// ---------------------------------------------------------------------------
// UBX: enviar con checksum Fletcher de 8 bits y reconocer las respuestas
// ---------------------------------------------------------------------------
void enviarUbx(uint8_t clase, uint8_t id, const uint8_t* datos, uint16_t largo) {
  uint8_t cab[6] = { 0xB5, 0x62, clase, id, (uint8_t)(largo & 0xFF), (uint8_t)(largo >> 8) };
  uint8_t ckA = 0, ckB = 0;
  for (int i = 2; i < 6; i++) { ckA += cab[i]; ckB += ckA; }
  for (uint16_t i = 0; i < largo; i++) { ckA += datos[i]; ckB += ckA; }
  Serial1.write(cab, 6);
  if (largo) Serial1.write(datos, largo);
  Serial1.write(ckA);
  Serial1.write(ckB);
  Serial.printf("[GPS] UBX enviado %02X-%02X (%u bytes)\n", clase, id, largo);
}

// Lector de tramas UBX entrantes (maquina de estados)
uint8_t ubxBuf[256];
uint16_t ubxPos = 0, ubxLargo = 0;
enum { U_ESPERA, U_SYNC2, U_CABECERA, U_DATOS } ubxEstado = U_ESPERA;

void procesarTramaUbx() {
  uint8_t clase = ubxBuf[0], id = ubxBuf[1];
  const uint8_t* d = ubxBuf + 4;
  if (clase == 0x05) {
    Serial.printf("[GPS] %s de %02X-%02X\n", id == 0x01 ? "ACK" : "NAK", d[0], d[1]);
  } else if (clase == 0x0A && id == 0x04) {
    // MON-VER: swVersion (30 bytes), hwVersion (10 bytes) y extensiones de 30 bytes
    Serial.printf("[GPS] MON-VER: sw '%.30s' hw '%.10s'\n", (const char*)d, (const char*)d + 30);
    for (uint16_t i = 40; i + 30 <= ubxLargo; i += 30) Serial.printf("[GPS]   %.30s\n", (const char*)d + i);
  } else {
    Serial.printf("[GPS] UBX recibido %02X-%02X (%u bytes)\n", clase, id, ubxLargo);
  }
}

// Devuelve true si el byte es parte de una trama UBX (y no hay que tratarlo como NMEA)
bool leerByteUbx(uint8_t b) {
  switch (ubxEstado) {
    case U_ESPERA:
      if (b == 0xB5) { ubxEstado = U_SYNC2; return true; }
      return false;
    case U_SYNC2:
      ubxEstado = (b == 0x62) ? U_CABECERA : U_ESPERA;
      ubxPos = 0;
      return true;
    case U_CABECERA:
      ubxBuf[ubxPos++] = b;
      if (ubxPos == 4) {
        ubxLargo = ubxBuf[2] | (ubxBuf[3] << 8);
        if (ubxLargo + 6 > sizeof(ubxBuf)) ubxEstado = U_ESPERA;
        else ubxEstado = U_DATOS;
      }
      return true;
    case U_DATOS:
      ubxBuf[ubxPos++] = b;
      if (ubxPos == 4 + ubxLargo + 2) {  // Datos y checksum
        uint8_t ckA = 0, ckB = 0;
        for (uint16_t i = 0; i < 4 + ubxLargo; i++) { ckA += ubxBuf[i]; ckB += ckA; }
        if (ckA == ubxBuf[4 + ubxLargo] && ckB == ubxBuf[5 + ubxLargo]) procesarTramaUbx();
        else Serial.println(F("[GPS] UBX con checksum mal"));
        ubxEstado = U_ESPERA;
      }
      return true;
  }
  return false;
}

// ---------------------------------------------------------------------------
// NMEA: GGA y RMC por campos separados por comas
// ---------------------------------------------------------------------------
char linea[100];
uint8_t largoLinea = 0;

// Copia el campo n (0 = tipo de frase) de la linea
void campo(const char* l, int n, char* dest, size_t tam) {
  int i = 0;
  while (n > 0 && *l) { if (*l == ',') n--; l++; }
  while (*l && *l != ',' && *l != '*' && i < (int)tam - 1) dest[i++] = *l++;
  dest[i] = 0;
}

void procesarLinea(const char* l) {
  if (nmeaCrudo) Serial.println(l);
  char tmp[16];
  if (strncmp(l + 3, "GGA", 3) == 0) {
    campo(l, 1, tmp, sizeof(tmp)); if (tmp[0]) { strncpy(horaUtc, tmp, sizeof(horaUtc) - 1); }
    campo(l, 6, tmp, sizeof(tmp)); calidadFijo = atoi(tmp);
    campo(l, 7, tmp, sizeof(tmp)); satelites = atoi(tmp);
    char lat[12], ns[2], lon[13], ew[2];
    campo(l, 2, lat, sizeof(lat)); campo(l, 3, ns, sizeof(ns));
    campo(l, 4, lon, sizeof(lon)); campo(l, 5, ew, sizeof(ew));
    if (lat[0]) { snprintf(latitud, sizeof(latitud), "%s%s", lat, ns); snprintf(longitud, sizeof(longitud), "%s%s", lon, ew); }
  } else if (strncmp(l + 3, "RMC", 3) == 0) {
    campo(l, 2, tmp, sizeof(tmp)); estadoRmc = tmp[0] ? tmp[0] : 'V';
  }
}

void leerGps() {
  while (Serial1.available()) {
    uint8_t b = Serial1.read();
    bytesPeriodo++;
    if (leerByteUbx(b)) continue;
    if (b == '$') largoLinea = 0;
    if (b == '\r') continue;
    if (b == '\n') {
      linea[largoLinea] = 0;
      if (largoLinea > 6 && linea[0] == '$') procesarLinea(linea);
      largoLinea = 0;
    } else if (largoLinea < sizeof(linea) - 1) {
      linea[largoLinea++] = b;
    }
  }
}

// ---------------------------------------------------------------------------
// Ordenes
// ---------------------------------------------------------------------------
void orden(char c) {
  switch (c) {
    case 'n':
      nmeaCrudo = !nmeaCrudo;
      Serial.printf("[GPS] NMEA crudo %s\n", nmeaCrudo ? "SI" : "NO");
      break;
    case 'v':
      enviarUbx(0x0A, 0x04, nullptr, 0);
      break;
    case 'd': {
      // RXM-PMREQ: duracion 0 (sin limite) y flags bit 1 = backup
      const uint8_t datos[8] = { 0, 0, 0, 0, 0x02, 0, 0, 0 };
      enviarUbx(0x02, 0x41, datos, sizeof(datos));
      Serial.println(F("[GPS] Pedido dormir (backup). Medir ahora el consumo"));
      gpsDormido = true;
      break;
    }
    case 'w': {
      for (int i = 0; i < 16; i++) Serial1.write(0xFF);
      Serial.println(F("[GPS] Rafaga de 0xFF enviada: si despierta, vuelve el NMEA"));
      gpsDormido = false;
      break;
    }
    case 'p': {
      const uint8_t datos[4] = { 0x00, 0x00, 0x08, 0x00 };  // navBbrMask 0 (caliente), GNSS stop
      enviarUbx(0x06, 0x04, datos, sizeof(datos));
      break;
    }
    case 'a': {
      const uint8_t datos[4] = { 0x00, 0x00, 0x09, 0x00 };  // GNSS start
      enviarUbx(0x06, 0x04, datos, sizeof(datos));
      break;
    }
    case '\r': case '\n': case ' ':
      break;
    default:
      Serial.println(F("[GPS] Ordenes: n NMEA, v version, d dormir, w despertar, p parar GNSS, a arrancar GNSS"));
  }
}

void dibujar() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.print(gpsDormido ? F("GPS DORMIDO") : F("GPS DESPIERTO"));
  display.setCursor(0, 12);
  display.printf("fijo %d (%c)  sat %d", calidadFijo, estadoRmc, satelites);
  display.setCursor(0, 22);
  display.printf("UTC %s", horaUtc);
  display.setCursor(0, 32);
  display.print(latitud);
  display.setCursor(0, 42);
  display.print(longitud);
  display.setCursor(0, 56);
  display.printf("%lu B/5s  PRG=%s", (unsigned long)bytesUltimoPeriodo, gpsDormido ? "despertar" : "dormir");
  display.display();
}

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  pinMode(VEXT_CTRL, OUTPUT);
  digitalWrite(VEXT_CTRL, LOW);
  delay(500);
  Wire.begin(OLED_SDA, OLED_SCL);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  Serial1.begin(GPS_BAUD, SERIAL_8N1, GPS_RX_PIN, GPS_TX_PIN);
  Serial.println(F("\n[GPS] Prueba del GPS: RX en GPIO47, TX en GPIO39, 9600 bd"));
  Serial.println(F("[GPS] Ordenes: n NMEA, v version, d dormir, w despertar, p parar GNSS, a arrancar GNSS"));
}

void loop() {
  leerGps();
  while (Serial.available()) orden(Serial.read());
  // PRG corto: dormir o despertar (se actua al soltar, con antirrebote)
  static bool prgAntes = HIGH;
  static unsigned long prgInicio = 0;
  bool prg = digitalRead(BUTTON_PIN);
  if (prgAntes == HIGH && prg == LOW) prgInicio = millis();
  if (prgAntes == LOW && prg == HIGH && millis() - prgInicio > 50) {
    orden(gpsDormido ? 'w' : 'd');
    dibujar();
  }
  prgAntes = prg;
  if (millis() - ultimoResumen >= 5000) {
    ultimoResumen = millis();
    Serial.printf("[GPS] %4lu B/5s  fijo %d (%c)  sat %2d  UTC %s  %s %s\n", (unsigned long)bytesPeriodo,
                  calidadFijo, estadoRmc, satelites, horaUtc, latitud, longitud);
    bytesUltimoPeriodo = bytesPeriodo;
    bytesPeriodo = 0;
    dibujar();
  }
}
