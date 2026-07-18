#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_AHTX0.h>


#define DEVICE_ID "robot_01"// mas esp32 requiere otra id


#define I2C_SDA 21
#define I2C_SCL 22

const char* WIFI_SSID = "iPhoneBETO";
const char* WIFI_PASSWORD = "12345678";


const char* SERVER_BASE_URL = "http://172.20.10.7:6000";//ip de pc 

Adafruit_AHTX0 dht;

unsigned long lastHttpSend = 0;
const unsigned long HTTP_SEND_INTERVAL = 2000; // enviar cada 2 segundos

WiFiClient wifi;

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("DHT20/AHT20 - Envio de temperatura y humedad a Flask");

 
  Wire.begin(I2C_SDA, I2C_SCL);

  
  if (!dht.begin()) {
    Serial.println("No se encontro el sensor DHT20/AHT20. Revisa conexiones.");
    while (1) {
      delay(10);
    }
  }

  Serial.println("Sensor DHT20/AHT20 detectado correctamente.");


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

      sensors_event_t humidity, temp;
      dht.getEvent(&humidity, &temp);

      float temperatureC = temp.temperature;
      float humidityPct = humidity.relative_humidity;

      StaticJsonDocument<256> doc;

      doc["device_id"] = DEVICE_ID;
      doc["temperature_c"] = temperatureC;
      doc["humidity_pct"] = humidityPct;

      String json_string;
      serializeJson(doc, json_string);

      Serial.print("Enviando JSON: ");
      Serial.println(json_string);

      HTTPClient http;
      String url = String(SERVER_BASE_URL) + "/sensor_values";

      http.begin(wifi, url);
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