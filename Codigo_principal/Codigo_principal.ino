#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_AHTX0.h>
#include "Adafruit_VL53L0X.h"

// ID
#define DEVICE_ID "robot_01"

#define I2C_SDA 21
#define I2C_SCL 22

const char* WIFI_SSID = "iPhoneBETO";
const char* WIFI_PASSWORD = "12345678";

const char* SERVER_BASE_URL = "http://172.20.10.7:6000";

Adafruit_AHTX0 dht;
Adafruit_VL53L0X lox = Adafruit_VL53L0X();

bool dht_ok = false;
bool tof_ok = false;

unsigned long lastHttpSend = 0;
const unsigned long HTTP_SEND_INTERVAL = 2000; // enviar cada 2 segundos

WiFiClient wifi;

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("DHT20 + VL53L0X - Envio de datos a Flask e InfluxDB");

  // Iniciar I2C
  Wire.begin(I2C_SDA, I2C_SCL);

  // Iniciar DHT20/AHT20
  if (!dht.begin()) {
    Serial.println("No se encontro el sensor DHT20/AHT20. Revisa conexiones.");
    dht_ok = false;
  } else {
    Serial.println("Sensor DHT20/AHT20 detectado correctamente.");
    dht_ok = true;
  }

  // Iniciar VL53L0X
  if (!lox.begin()) {
    Serial.println("No se encontro el sensor VL53L0X. Revisa conexiones.");
    tof_ok = false;
  } else {
    Serial.println("Sensor VL53L0X detectado correctamente.");
    tof_ok = true;
  }

  //wifi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.println("Conectando a WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.println("Conectando...");
  }

  Serial.println("Conectado a WiFi");
  Serial.print("IP ESP32: ");
  Serial.println(WiFi.localIP());
}

void loop() {
  if (WiFi.status() == WL_CONNECTED) {
    unsigned long now = millis();

    if (now - lastHttpSend >= HTTP_SEND_INTERVAL) {
      lastHttpSend = now;

      float temperatureC = NAN;
      float humidityPct = NAN;
      int frontDistanceMm = -1;
      bool tofValid = false;

      
      if (dht_ok) {
        sensors_event_t humidity, temp;
        dht.getEvent(&humidity, &temp);

        temperatureC = temp.temperature;
        humidityPct = humidity.relative_humidity;
      }

     
      if (tof_ok) {
        VL53L0X_RangingMeasurementData_t measure;
        lox.rangingTest(&measure, false);

        if (measure.RangeStatus != 4) {
          frontDistanceMm = measure.RangeMilliMeter;
          tofValid = true;
        } else {
          frontDistanceMm = -1;
          tofValid = false;
        }
      }

      StaticJsonDocument<384> doc;

      doc["device_id"] = DEVICE_ID;

      if (dht_ok) {
        doc["temperature_c"] = temperatureC;
        doc["humidity_pct"] = humidityPct;
      }

      if (tof_ok) {
        doc["front_distance_mm"] = frontDistanceMm;
        doc["tof_valid"] = tofValid;
      }

      String json_string;
      serializeJson(doc, json_string);

      HTTPClient http;
      String url = String(SERVER_BASE_URL) + "/sensor_values";

      Serial.print("URL: ");
      Serial.println(url);

      Serial.print("Enviando JSON: ");
      Serial.println(json_string);

      http.begin(wifi, url);
      http.setConnectTimeout(5000);
      http.addHeader("Content-Type", "application/json");

      int httpResponseCode = http.POST(json_string);

      if (httpResponseCode > 0) {
        String response = http.getString();

        Serial.print("HTTP ");
        Serial.print(httpResponseCode);
        Serial.print(": ");
        Serial.println(response);
      } else {
        Serial.print("Error HTTP: ");
        Serial.println(http.errorToString(httpResponseCode));
      }

      http.end();
    }
  } else {
    Serial.println("WiFi desconectado");
  }

  delay(100);
}