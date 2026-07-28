#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Wire.h>
#include <Adafruit_AHTX0.h>
#include "Adafruit_VL53L0X.h"
#include <Adafruit_INA219.h>

#define DEVICE_ID "robot_01"

#define I2C_SDA 21
#define I2C_SCL 22

//XSHUT para cada VL53L0X
#define XSHUT_FRONT 25
#define XSHUT_RIGHT 26
#define XSHUT_LEFT  27


#define TOF_FRONT_ADDR 0x30
#define TOF_RIGHT_ADDR 0x31
#define TOF_LEFT_ADDR  0x32

const char* WIFI_SSID = "iPhoneBETO";
const char* WIFI_PASSWORD = "12345678";

const char* SERVER_BASE_URL = "http://172.20.10.7:6000";

Adafruit_AHTX0 dht;
Adafruit_INA219 ina219;

Adafruit_VL53L0X tofFront = Adafruit_VL53L0X();
Adafruit_VL53L0X tofRight = Adafruit_VL53L0X();
Adafruit_VL53L0X tofLeft  = Adafruit_VL53L0X();

bool dht_ok = false;
bool ina_ok = false;

bool tof_front_ok = false;
bool tof_right_ok = false;
bool tof_left_ok = false;

unsigned long lastHttpSend = 0;
const unsigned long HTTP_SEND_INTERVAL = 2000;

WiFiClient wifi;


void initToFSensors() {
  pinMode(XSHUT_FRONT, OUTPUT);
  pinMode(XSHUT_RIGHT, OUTPUT);
  pinMode(XSHUT_LEFT, OUTPUT);

  // Apagar todos los sensores
  digitalWrite(XSHUT_FRONT, LOW);
  digitalWrite(XSHUT_RIGHT, LOW);
  digitalWrite(XSHUT_LEFT, LOW);
  delay(100);


  // Sensor frontal

  digitalWrite(XSHUT_FRONT, HIGH);
  delay(100);

  if (!tofFront.begin(TOF_FRONT_ADDR, false, &Wire)) {
    Serial.println("No se encontro el VL53L0X frontal. Revisa conexiones.");
    tof_front_ok = false;
  } else {
    Serial.println("VL53L0X frontal detectado correctamente en 0x30.");
    tof_front_ok = true;
  }

  

  digitalWrite(XSHUT_RIGHT, HIGH);
  delay(100);

  if (!tofRight.begin(TOF_RIGHT_ADDR, false, &Wire)) {
    Serial.println("No se encontro el VL53L0X derecho. Revisa conexiones.");
    tof_right_ok = false;
  } else {
    Serial.println("VL53L0X derecho detectado correctamente en 0x31.");
    tof_right_ok = true;
  }

 
  digitalWrite(XSHUT_LEFT, HIGH);
  delay(100);

  if (!tofLeft.begin(TOF_LEFT_ADDR, false, &Wire)) {
    Serial.println("No se encontro el VL53L0X izquierdo. Revisa conexiones.");
    tof_left_ok = false;
  } else {
    Serial.println("VL53L0X izquierdo detectado correctamente en 0x32.");
    tof_left_ok = true;
  }
}



int readToF(Adafruit_VL53L0X &sensor, bool sensor_ok, bool &valid) {
  if (!sensor_ok) {
    valid = false;
    return -1;
  }

  VL53L0X_RangingMeasurementData_t measure;
  sensor.rangingTest(&measure, false);

  if (measure.RangeStatus != 4) {
    valid = true;
    return measure.RangeMilliMeter;
  } else {
    valid = false;
    return -1;
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("DHT20 + 3 VL53L0X + INA219 - Envio de datos a Flask e InfluxDB");

  Wire.begin(I2C_SDA, I2C_SCL);

  
  if (!dht.begin()) {
    Serial.println("No se encontro el sensor DHT20/AHT20. Revisa conexiones.");
    dht_ok = false;
  } else {
    Serial.println("Sensor DHT20/AHT20 detectado correctamente.");
    dht_ok = true;
  }


  if (!ina219.begin()) {
    Serial.println("Failed to find INA219 chip");
    ina_ok = false;
  } else {
    ina219.setCalibration_16V_400mA();
    Serial.println("INA219 detectado correctamente.");
    ina_ok = true;
  }

  
  initToFSensors();

  
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
      int rightDistanceMm = -1;
      int leftDistanceMm = -1;

      bool frontTofValid = false;
      bool rightTofValid = false;
      bool leftTofValid = false;

      float shuntvoltage = 0;
      float busvoltage = 0;
      float current_mA = 0;
      float loadvoltage = 0;
      float power_mW = 0;

  
      if (dht_ok) {
        sensors_event_t humidity, temp;
        dht.getEvent(&humidity, &temp);

        temperatureC = temp.temperature;
        humidityPct = humidity.relative_humidity;
      }

    
      frontDistanceMm = readToF(tofFront, tof_front_ok, frontTofValid);
      rightDistanceMm = readToF(tofRight, tof_right_ok, rightTofValid);
      leftDistanceMm  = readToF(tofLeft,  tof_left_ok,  leftTofValid);

      
      if (ina_ok) {
        shuntvoltage = ina219.getShuntVoltage_mV();
        busvoltage = ina219.getBusVoltage_V();
        current_mA = ina219.getCurrent_mA();
        power_mW = ina219.getPower_mW();
        loadvoltage = busvoltage + (shuntvoltage / 1000);
      }

  
   
      StaticJsonDocument<768> doc;

      doc["device_id"] = DEVICE_ID;

      if (dht_ok) {
        doc["temperature_c"] = temperatureC;
        doc["humidity_pct"] = humidityPct;
      }

      if (tof_front_ok) {
        doc["front_distance_mm"] = frontDistanceMm;
        doc["front_tof_valid"] = frontTofValid;
      }

      if (tof_right_ok) {
        doc["right_distance_mm"] = rightDistanceMm;
        doc["right_tof_valid"] = rightTofValid;
      }

      if (tof_left_ok) {
        doc["left_distance_mm"] = leftDistanceMm;
        doc["left_tof_valid"] = leftTofValid;
      }

      if (ina_ok) {
        doc["bus_voltage_v"] = busvoltage;
        doc["shunt_voltage_mv"] = shuntvoltage;
        doc["load_voltage_v"] = loadvoltage;
        doc["current_ma"] = current_mA;
        doc["power_mw"] = power_mW;
      }

      String json_string;
      serializeJson(doc, json_string);

     
      Serial.println();
      Serial.println("========== LECTURA DE SENSORES ==========");

      if (dht_ok) {
        Serial.print("Temperatura: ");
        Serial.print(temperatureC);
        Serial.println(" °C");

        Serial.print("Humedad: ");
        Serial.print(humidityPct);
        Serial.println(" %");
      }

      Serial.println("--- VL53L0X ---");

      Serial.print("Frontal: ");
      Serial.print(frontDistanceMm);
      Serial.print(" mm | valido: ");
      Serial.println(frontTofValid ? "SI" : "NO");

      Serial.print("Derecho: ");
      Serial.print(rightDistanceMm);
      Serial.print(" mm | valido: ");
      Serial.println(rightTofValid ? "SI" : "NO");

      Serial.print("Izquierdo: ");
      Serial.print(leftDistanceMm);
      Serial.print(" mm | valido: ");
      Serial.println(leftTofValid ? "SI" : "NO");

      if (ina_ok) {
        Serial.println("--- INA219 ---");

        Serial.print("Bus voltage: ");
        Serial.print(busvoltage);
        Serial.println(" V");

        Serial.print("Shunt voltage: ");
        Serial.print(shuntvoltage);
        Serial.println(" mV");

        Serial.print("Load voltage: ");
        Serial.print(loadvoltage);
        Serial.println(" V");

        Serial.print("Current: ");
        Serial.print(current_mA);
        Serial.println(" mA");

        Serial.print("Power: ");
        Serial.print(power_mW);
        Serial.println(" mW");
      }

      Serial.print("URL: ");
      String url = String(SERVER_BASE_URL) + "/sensor_values";
      Serial.println(url);

      Serial.print("Enviando JSON: ");
      Serial.println(json_string);

    
      HTTPClient http;

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

      Serial.println("==========================================");
    }
  } else {
    Serial.println("WiFi desconectado");
  }

  delay(100);
}