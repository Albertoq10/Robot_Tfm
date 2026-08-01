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
bool tof_left_ok  = false;

const int enA = 32;
const int in1 = 13;
const int in2 = 14;

const int enB = 33;
const int in3 = 18;
const int in4 = 19;

const int freq = 1000;
const int resolution = 8;

const int pwmChannelA = 0;
const int pwmChannelB = 1;

const int SPEED_NORMAL = 100;
const int SPEED_TURN   = 180;
const int SPEED_BACK   = 150;

const int FRONT_LIMIT_MM = 500;
const int SIDE_LIMIT_MM  = 300;

enum RobotState {
  STATE_NORMAL,
  STATE_CORRECT_LEFT,
  STATE_CORRECT_RIGHT,
  STATE_BACKWARD,
  STATE_TURN_RIGHT,
  STATE_TURN_LEFT
};

RobotState robotState = STATE_NORMAL;

unsigned long stateStartTime = 0;
unsigned long lastSensorPrint = 0;
unsigned long lastHttpSend = 0;

const unsigned long CORRECT_TIME = 250;
const unsigned long BACK_TIME = 1200;
const unsigned long ESCAPE_TURN_TIME = 1300;
const unsigned long SENSOR_PRINT_TIME = 300;
const unsigned long HTTP_SEND_INTERVAL = 2000;

bool escapeTurnRight = true;

WiFiClient wifi;

int frontDistanceMm = -1;
int rightDistanceMm = -1;
int leftDistanceMm  = -1;

bool frontTofValid = false;
bool rightTofValid = false;
bool leftTofValid  = false;

float temperatureC = NAN;
float humidityPct = NAN;

float shuntvoltage = 0;
float busvoltage = 0;
float current_mA = 0;
float loadvoltage = 0;
float power_mW = 0;

void startState(RobotState newState) {
  robotState = newState;
  stateStartTime = millis();
}

const char* getRobotStateName() {
  switch (robotState) {
    case STATE_NORMAL:
      return "normal";
    case STATE_CORRECT_LEFT:
      return "correct_left";
    case STATE_CORRECT_RIGHT:
      return "correct_right";
    case STATE_BACKWARD:
      return "backward";
    case STATE_TURN_RIGHT:
      return "turn_right";
    case STATE_TURN_LEFT:
      return "turn_left";
    default:
      return "unknown";
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

void stopMotors() {
  digitalWrite(in1, LOW);
  digitalWrite(in2, LOW);
  digitalWrite(in3, LOW);
  digitalWrite(in4, LOW);

  ledcWrite(enA, 0);
  ledcWrite(enB, 0);
}

void moveForward(int speedValue) {
  digitalWrite(in1, HIGH);
  digitalWrite(in2, LOW);

  digitalWrite(in3, HIGH);
  digitalWrite(in4, LOW);

  ledcWrite(enA, speedValue);
  ledcWrite(enB, speedValue);
}

void moveBackward(int speedValue) {
  digitalWrite(in1, LOW);
  digitalWrite(in2, HIGH);

  digitalWrite(in3, LOW);
  digitalWrite(in4, HIGH);

  ledcWrite(enA, speedValue);
  ledcWrite(enB, speedValue);
}

void turnRight(int speedValue) {
  digitalWrite(in1, LOW);
  digitalWrite(in2, HIGH);

  digitalWrite(in3, LOW);
  digitalWrite(in4, HIGH);

  ledcWrite(enA, speedValue);
  ledcWrite(enB, speedValue);
}

void turnLeft(int speedValue) {
  digitalWrite(in1, HIGH);
  digitalWrite(in2, LOW);

  digitalWrite(in3, HIGH);
  digitalWrite(in4, LOW);

  ledcWrite(enA, speedValue);
  ledcWrite(enB, speedValue);
}

void initToFSensors() {
  pinMode(XSHUT_FRONT, OUTPUT);
  pinMode(XSHUT_RIGHT, OUTPUT);
  pinMode(XSHUT_LEFT, OUTPUT);

  digitalWrite(XSHUT_FRONT, LOW);
  digitalWrite(XSHUT_RIGHT, LOW);
  digitalWrite(XSHUT_LEFT, LOW);
  delay(100);

  digitalWrite(XSHUT_FRONT, HIGH);
  delay(100);
  tof_front_ok = tofFront.begin(TOF_FRONT_ADDR, false, &Wire);
  Serial.println(tof_front_ok ? "VL53L0X frontal OK" : "VL53L0X frontal NO detectado");

  digitalWrite(XSHUT_RIGHT, HIGH);
  delay(100);
  tof_right_ok = tofRight.begin(TOF_RIGHT_ADDR, false, &Wire);
  Serial.println(tof_right_ok ? "VL53L0X derecho OK" : "VL53L0X derecho NO detectado");

  digitalWrite(XSHUT_LEFT, HIGH);
  delay(100);
  tof_left_ok = tofLeft.begin(TOF_LEFT_ADDR, false, &Wire);
  Serial.println(tof_left_ok ? "VL53L0X izquierdo OK" : "VL53L0X izquierdo NO detectado");
}

void readEnvironmentalSensors() {
  if (dht_ok) {
    sensors_event_t humidity, temp;
    dht.getEvent(&humidity, &temp);

    temperatureC = temp.temperature;
    humidityPct = humidity.relative_humidity;
  }

  if (ina_ok) {
    shuntvoltage = ina219.getShuntVoltage_mV();
    busvoltage = ina219.getBusVoltage_V();
    current_mA = ina219.getCurrent_mA();
    power_mW = ina219.getPower_mW();
    loadvoltage = busvoltage + (shuntvoltage / 1000);
  }
}

void sendDataToFlask() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi desconectado");
    return;
  }

  readEnvironmentalSensors();

  StaticJsonDocument<1024> doc;

  doc["device_id"] = DEVICE_ID;
  doc["robot_state"] = getRobotStateName();

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

  String url = String(SERVER_BASE_URL) + "/sensor_values";

  Serial.println();
  Serial.println("========== ENVIO A FLASK ==========");
  Serial.print("URL: ");
  Serial.println(url);
  Serial.print("JSON: ");
  Serial.println(json_string);

  HTTPClient http;
  http.begin(wifi, url);
  http.setConnectTimeout(1000);
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

  Serial.println("===================================");
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("Robot ESP32 + 3 VL53L0X + L298N + DHT20 + INA219 + Flask");

  Wire.begin(I2C_SDA, I2C_SCL);

  pinMode(in1, OUTPUT);
  pinMode(in2, OUTPUT);
  pinMode(in3, OUTPUT);
  pinMode(in4, OUTPUT);

  ledcAttachChannel(enA, freq, resolution, pwmChannelA);
  ledcAttachChannel(enB, freq, resolution, pwmChannelB);

  stopMotors();

  if (!dht.begin()) {
    Serial.println("No se encontro el sensor DHT20/AHT20.");
    dht_ok = false;
  } else {
    Serial.println("Sensor DHT20/AHT20 detectado correctamente.");
    dht_ok = true;
  }

  if (!ina219.begin()) {
    Serial.println("Failed to find INA219 chip");
    ina_ok = false;
  } else {
    ina219.setCalibration_32V_2A();
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

  Serial.println("Sistema listo.");
}

void loop() {
  unsigned long now = millis();

  frontDistanceMm = readToF(tofFront, tof_front_ok, frontTofValid);
  rightDistanceMm = readToF(tofRight, tof_right_ok, rightTofValid);
  leftDistanceMm  = readToF(tofLeft,  tof_left_ok,  leftTofValid);

  if (now - lastSensorPrint >= SENSOR_PRINT_TIME) {
    lastSensorPrint = now;

    Serial.println("----- Lecturas ToF -----");

    Serial.print("Frontal: ");
    Serial.print(frontDistanceMm);
    Serial.print(" mm | valido: ");
    Serial.println(frontTofValid);

    Serial.print("Derecho: ");
    Serial.print(rightDistanceMm);
    Serial.print(" mm | valido: ");
    Serial.println(rightTofValid);

    Serial.print("Izquierdo: ");
    Serial.print(leftDistanceMm);
    Serial.print(" mm | valido: ");
    Serial.println(leftTofValid);

    Serial.print("Estado: ");
    Serial.println(getRobotStateName());
  }

  bool frontBlocked = frontTofValid && frontDistanceMm < FRONT_LIMIT_MM;

  bool rightTooClose = rightTofValid && rightDistanceMm < SIDE_LIMIT_MM;
  bool leftTooClose  = leftTofValid  && leftDistanceMm  < SIDE_LIMIT_MM;

  bool rightFree = rightTofValid && rightDistanceMm > SIDE_LIMIT_MM;
  bool leftFree  = leftTofValid  && leftDistanceMm  > SIDE_LIMIT_MM;

  if (frontBlocked && robotState == STATE_NORMAL) {
    stopMotors();
    startState(STATE_BACKWARD);
  }

  if (robotState == STATE_CORRECT_LEFT) {
    turnLeft(SPEED_TURN);

    if (now - stateStartTime >= CORRECT_TIME) {
      stopMotors();
      startState(STATE_NORMAL);
    }
  }
  else if (robotState == STATE_CORRECT_RIGHT) {
    turnRight(SPEED_TURN);

    if (now - stateStartTime >= CORRECT_TIME) {
      stopMotors();
      startState(STATE_NORMAL);
    }
  }
  else if (robotState == STATE_BACKWARD) {
    moveBackward(SPEED_BACK);

    if (now - stateStartTime >= BACK_TIME) {
      stopMotors();

      if (rightFree && !leftFree) {
        startState(STATE_TURN_RIGHT);
      } 
      else if (leftFree && !rightFree) {
        startState(STATE_TURN_LEFT);
      } 
      else if (rightFree && leftFree) {
        if (rightDistanceMm > leftDistanceMm) {
          startState(STATE_TURN_RIGHT);
        } else {
          startState(STATE_TURN_LEFT);
        }
      } 
      else {
        if (escapeTurnRight) {
          startState(STATE_TURN_RIGHT);
        } else {
          startState(STATE_TURN_LEFT);
        }

        escapeTurnRight = !escapeTurnRight;
      }
    }
  }
  else if (robotState == STATE_TURN_RIGHT) {
    turnRight(SPEED_TURN);

    if (now - stateStartTime >= ESCAPE_TURN_TIME) {
      stopMotors();
      startState(STATE_NORMAL);
    }
  }
  else if (robotState == STATE_TURN_LEFT) {
    turnLeft(SPEED_TURN);

    if (now - stateStartTime >= ESCAPE_TURN_TIME) {
      stopMotors();
      startState(STATE_NORMAL);
    }
  }
  else {
    if (!frontBlocked && !rightTooClose && !leftTooClose) {
      moveForward(SPEED_NORMAL);
    }
    else if (!frontBlocked && rightTooClose && !leftTooClose) {
      startState(STATE_CORRECT_LEFT);
    }
    else if (!frontBlocked && leftTooClose && !rightTooClose) {
      startState(STATE_CORRECT_RIGHT);
    }
    else if (!frontBlocked && rightTooClose && leftTooClose) {
      moveForward(70);
    }
    else {
      stopMotors();
      startState(STATE_BACKWARD);
    }
  }

  if (now - lastHttpSend >= HTTP_SEND_INTERVAL) {
    lastHttpSend = now;
    sendDataToFlask();
  }
}