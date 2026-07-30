#include <Arduino.h>
#include <Wire.h>
#include "Adafruit_VL53L0X.h"


#define I2C_SDA 21
#define I2C_SCL 22

#define XSHUT_FRONT 25
#define XSHUT_RIGHT 26
#define XSHUT_LEFT  27


#define TOF_FRONT_ADDR 0x30
#define TOF_RIGHT_ADDR 0x31
#define TOF_LEFT_ADDR  0x32


Adafruit_VL53L0X tofFront = Adafruit_VL53L0X();
Adafruit_VL53L0X tofRight = Adafruit_VL53L0X();
Adafruit_VL53L0X tofLeft  = Adafruit_VL53L0X();

bool tof_front_ok = false;
bool tof_right_ok = false;
bool tof_left_ok  = false;


// Motor izquierdo
const int enA = 32;  
const int in1 = 13;
const int in2 = 14;

// Motor derecho
const int enB = 33;
const int in3 = 18;
const int in4 = 19;

/
const int freq = 1000;
const int resolution = 8; // 0 - 255

const int pwmChannelA = 0;
const int pwmChannelB = 1;

// =========================
// PARÁMETROS DE MOVIMIENTO
// =========================
const int SPEED_NORMAL = 160;   // velocidad normal
const int SPEED_TURN   = 150;   // velocidad para girar
const int SPEED_BACK   = 130;   // velocidad para retroceder

const int FRONT_LIMIT_MM = 250; // si hay obstáculo a menos de 25 cm, evita
const int SIDE_LIMIT_MM  = 180; // lateral mínimo aceptable


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
  // Motor izquierdo avanza, motor derecho retrocede
  digitalWrite(in1, HIGH);
  digitalWrite(in2, LOW);

  digitalWrite(in3, LOW);
  digitalWrite(in4, HIGH);

  ledcWrite(enA, speedValue);
  ledcWrite(enB, speedValue);
}

void turnLeft(int speedValue) {
  // Motor izquierdo retrocede, motor derecho avanza
  digitalWrite(in1, LOW);
  digitalWrite(in2, HIGH);

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

  // Encender izquierdo y asignar dirección
  digitalWrite(XSHUT_LEFT, HIGH);
  delay(100);
  tof_left_ok = tofLeft.begin(TOF_LEFT_ADDR, false, &Wire);
  Serial.println(tof_left_ok ? "VL53L0X izquierdo OK" : "VL53L0X izquierdo NO detectado");
}

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("Robot ESP32 + 3 VL53L0X + L298N");

  Wire.begin(I2C_SDA, I2C_SCL);

  // Motores
  pinMode(in1, OUTPUT);
  pinMode(in2, OUTPUT);
  pinMode(in3, OUTPUT);
  pinMode(in4, OUTPUT);

  ledcAttachChannel(enA, freq, resolution, pwmChannelA);
  ledcAttachChannel(enB, freq, resolution, pwmChannelB);

  stopMotors();

  // Sensores ToF
  initToFSensors();

  Serial.println("Sistema listo.");
}

void loop() {
  bool frontValid = false;
  bool rightValid = false;
  bool leftValid  = false;

  int frontDistance = readToF(tofFront, tof_front_ok, frontValid);
  int rightDistance = readToF(tofRight, tof_right_ok, rightValid);
  int leftDistance  = readToF(tofLeft,  tof_left_ok,  leftValid);

  Serial.println("----- Lecturas ToF -----");
  Serial.print("Frontal: ");
  Serial.print(frontDistance);
  Serial.print(" mm | valido: ");
  Serial.println(frontValid);

  Serial.print("Derecho: ");
  Serial.print(rightDistance);
  Serial.print(" mm | valido: ");
  Serial.println(rightValid);

  Serial.print("Izquierdo: ");
  Serial.print(leftDistance);
  Serial.print(" mm | valido: ");
  Serial.println(leftValid);

  

  bool frontBlocked = frontValid && frontDistance < FRONT_LIMIT_MM;
  bool rightFree = rightValid && rightDistance > SIDE_LIMIT_MM;
  bool leftFree  = leftValid  && leftDistance  > SIDE_LIMIT_MM;

  if (!frontBlocked) {
    Serial.println("Accion: avanzar");
    moveForward(SPEED_NORMAL);
  } 
  else {
    Serial.println("Obstaculo frontal detectado");

    stopMotors();
    delay(150);

    if (rightFree && !leftFree) {
      Serial.println("Accion: girar derecha");
      turnRight(SPEED_TURN);
      delay(450);
    } 
    else if (leftFree && !rightFree) {
      Serial.println("Accion: girar izquierda");
      turnLeft(SPEED_TURN);
      delay(450);
    } 
    else if (rightFree && leftFree) {
      // Si ambos lados están libres, elegir el lado con más espacio
      if (rightDistance > leftDistance) {
        Serial.println("Accion: girar derecha, hay mas espacio");
        turnRight(SPEED_TURN);
      } else {
        Serial.println("Accion: girar izquierda, hay mas espacio");
        turnLeft(SPEED_TURN);
      }
      delay(450);
    } 
    else {
      // Si todo estacerrado, retrocede un poco y gira
      Serial.println("Accion: retroceder y girar");
      moveBackward(SPEED_BACK);
      delay(400);

      stopMotors();
      delay(150);

      turnRight(SPEED_TURN);
      delay(500);
    }
  }

  delay(100);
}