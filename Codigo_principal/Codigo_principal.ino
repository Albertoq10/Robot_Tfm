
//librerias para funcionalidad correcta, sensores, comunicaocn , y propias de arduino 
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

#include <Wire.h>

#include <Adafruit_AHTX0.h>
#include "Adafruit_VL53L0X.h"
#include <Adafruit_INA219.h>

#define DEVICE_ID "robot_01"//pemirte escalabilidad

//pines de sensores, varios sensores usaron los mismo pines
#define I2C_SDA 21
#define I2C_SCL 22

#define XSHUT_FRONT 25//tres sneores del mismo
#define XSHUT_RIGHT 26
#define XSHUT_LEFT  27

//direcciones de sensores infra rojo
#define TOF_FRONT_ADDR 0x30
#define TOF_RIGHT_ADDR 0x31
#define TOF_LEFT_ADDR  0x32

#define MQ2_AO_PIN 35//pin analogivo

//timepo en segunods
const unsigned long MQ2_WARMUP_TIME = 20000;
const unsigned long MQ2_READ_INTERVAL = 1000;

unsigned long mq2StartTime = 0;
unsigned long lastMq2Read = 0;

bool mq2Ready = false;

int gasRaw = -1;
float gasVoltageEsp = 0.0;

float gasVoltageMq2 = 0.0;
bool gasAlert = false;

const int GAS_THRESHOLD = 2200;//umbral de gas, relamente no se hicieorn muchas pruebas


String previousRobotMode = "";// Guarda  modo anterior para detectar cambios 


unsigned long lastStationarySend = 0;//freucneua de envio de datos 
const unsigned long STATIONARY_INTERVAL = 15000; // 15 segundos

//red de movil
const char* WIFI_SSID = "iPhoneBETO";
const char* WIFI_PASSWORD = "12345678";
const char* SERVER_BASE_URL = "http://172.20.10.7:6000";



Adafruit_AHTX0 dht;
Adafruit_INA219 ina219;


//los tres sensores infra
Adafruit_VL53L0X tofFront = Adafruit_VL53L0X();

Adafruit_VL53L0X tofRight = Adafruit_VL53L0X();
Adafruit_VL53L0X tofLeft  = Adafruit_VL53L0X();


//para validar
bool dht_ok = false;
bool ina_ok = false;

bool tof_front_ok = false;
bool tof_right_ok = false;
bool tof_left_ok  = false;


//estos 6 pines occrepsonde al driver l298n
const int enA = 32;

const int in1 = 13;

const int in2 = 14;

const int enB = 33;
const int in3 = 18;
const int in4 = 19;

const int freq = 1000;
const int resolution = 8;

//dos canales pwm para los dos motores
const int pwmChannelA = 0;
const int pwmChannelB = 1;

//velocidades, mazimo es de 255
const int SPEED_NORMAL = 100;
const int SPEED_TURN   = 130;
const int SPEED_BACK   = 130;

const int FRONT_LIMIT_MM = 400;
const int SIDE_LIMIT_MM  = 350;


//posibles estados del robot
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
unsigned long lastCommandCheck = 0;

//timepo en segundos, esto se hizo por prueba y error
const unsigned long CORRECT_TIME = 250;
const unsigned long BACK_TIME = 600;
const unsigned long ESCAPE_TURN_TIME = 600;
const unsigned long SENSOR_PRINT_TIME = 1000;
const unsigned long HTTP_SEND_INTERVAL = 3000;
const unsigned long COMMAND_CHECK_INTERVAL = 80;


const int REMOTE_COMMAND_TIMEOUT_MS = 400;

bool escapeTurnRight = true;



WiFiClient wifi;


//
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


String robotMode = "autonomous";
String remoteCommand = "stop";
bool cameraEnabled = true;
int commandAgeMs = 9999;


void startState(RobotState newState) {

  robotState = newState;
  stateStartTime = millis();
}


//estaod del propio robot, esto depende de los umbrales de los sensores
const char* getRobotStateName() {
  switch (robotState) {
    case STATE_NORMAL:
      return "normal";

//
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

//para lecuturas del sensore infrarrojo
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


//la logica de LOW y HIGH se hizo a prueba y error porque dependia del sentido en el que se alimenta el motor y la manera como se conecto todo
//para motores
void stopMotors() {
  digitalWrite(in1, LOW);
  digitalWrite(in2, LOW);

  digitalWrite(in3, LOW);
  digitalWrite(in4, LOW);

  ledcWrite(enA, 0);
  ledcWrite(enB, 0);
}

void moveForward(int speedValue) {// mover robot
  digitalWrite(in1, HIGH);
  digitalWrite(in2, LOW);

  digitalWrite(in3, HIGH);
  digitalWrite(in4, LOW);

  ledcWrite(enA, speedValue);
  ledcWrite(enB, speedValue);
}


//hacia atras
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

  digitalWrite(in3, HIGH);
  digitalWrite(in4, LOW);

  ledcWrite(enA, speedValue);
  ledcWrite(enB, speedValue);
}

void turnLeft(int speedValue) {
  digitalWrite(in1, HIGH);
  digitalWrite(in2, LOW);

  digitalWrite(in3, LOW);
  digitalWrite(in4, HIGH);

  ledcWrite(enA, speedValue);
  ledcWrite(enB, speedValue);
}


//configuracion basica de los tres sensores de distancia
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
  //debug para temrinal de arduino
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

//el sensore de gas se debe calnetar un poco para funcioanra

void readMq2Sensor(unsigned long now) {


  if (!mq2Ready) {
    if (now - mq2StartTime >= MQ2_WARMUP_TIME) {

      mq2Ready = true;



      Serial.println("MQ-2 listo para medir.");

    } else {

      return;
    }

  }


  if (now - lastMq2Read >= MQ2_READ_INTERVAL) {
    lastMq2Read = now;

    gasRaw = analogRead(MQ2_AO_PIN);



    gasVoltageEsp = gasRaw * (3.3 / 4095.0);//3.3V, esto poruqe no se le puede meter voltajes mayores de 3.3 a tarjeta

    gasVoltageMq2 = gasVoltageEsp * 1.5;//5V

    gasAlert = gasRaw > GAS_THRESHOLD;
  }
}




void readEnvironmentalSensors() {
//revisa que este disponible el sensor de temperatura
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



void readCommandFromFlask() {
  //debe revisar la conexion

  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  String url = String(SERVER_BASE_URL) + "/command";

  HTTPClient http;

  http.begin(wifi, url);
  //tiempo de espera
  http.setConnectTimeout(120);
  http.setTimeout(120);

  int httpResponseCode = http.GET();


  if (httpResponseCode == 200) {//ok
    String response = http.getString();


    StaticJsonDocument<256> doc;//JSON
    DeserializationError error = deserializeJson(doc, response);

    if (!error) {
      const char* modeValue = doc["robot_mode"] | robotMode.c_str();

      const char* commandValue = doc["remote_command"] | remoteCommand.c_str();

      robotMode = String(modeValue);
      remoteCommand = String(commandValue);

      cameraEnabled = doc["camera_enabled"] | cameraEnabled;

      commandAgeMs = doc["command_age_ms"] | commandAgeMs;

    }

  }

  http.end();
}



void sendDataToFlask() {

  if (WiFi.status() != WL_CONNECTED) {

    Serial.println("WiFi desconectado");
    return;
  }

  readEnvironmentalSensors();

  StaticJsonDocument<1536> doc;//formato JSON
//esto es muy repetitivo para incluir la informacion el JSON
  doc["device_id"] = DEVICE_ID;
  doc["robot_state"] = getRobotStateName();

  doc["robot_mode"] = robotMode;

  doc["remote_command"] = remoteCommand;

  doc["camera_enabled"] = cameraEnabled;
//revisa que los sensores esten disponibles
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


  doc["mq2_ready"] = mq2Ready;


  if (mq2Ready) {
    doc["gas_raw"] = gasRaw;
    doc["gas_voltage_esp_v"] = gasVoltageEsp;




    doc["gas_voltage_v"] = gasVoltageMq2;
    doc["gas_alert"] = gasAlert;
  }

  String json_string;
  serializeJson(doc, json_string);


  String url = String(SERVER_BASE_URL) + "/sensor_values";

 


  HTTPClient http;
  http.begin(wifi, url);
//
  http.setConnectTimeout(150);
  http.setTimeout(150);

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


//paraque el robot se pueda mover solo
void handleAutonomousNavigation(
  unsigned long now,
  bool frontBlocked,
  bool rightTooClose,

  bool leftTooClose,

  bool rightFree,
  bool leftFree
) {


  //logica simple para mover robot
  //ningun algortimo complejo
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
    if (!frontBlocked && !rightTooClose && !leftTooClose) {//si hay espacio, seguir
      moveForward(SPEED_NORMAL);
    }

    else if (!frontBlocked && rightTooClose && !leftTooClose) {//correciones pequenas
      startState(STATE_CORRECT_LEFT);
    }
    else if (!frontBlocked && leftTooClose && !rightTooClose) {
      startState(STATE_CORRECT_RIGHT);
    }
    else if (!frontBlocked && rightTooClose && leftTooClose) {//velocidad lenta para espacios pequenos
      moveForward(70);
    }


    else {



      stopMotors();
      startState(STATE_BACKWARD);
    }
  }

}


//esta funcion funciona con la peticion GET, estos comandos vienen del flask

void handleRemoteControl(bool frontBlocked) {

  //si no se presiona nada, el robot no se detiene
  if (commandAgeMs > REMOTE_COMMAND_TIMEOUT_MS) {
    stopMotors();

    return;

  }

  if (remoteCommand == "forward") {
    if (!frontBlocked) {


      moveForward(SPEED_NORMAL);

    } else {
      stopMotors();
    }
  }

  else if (remoteCommand == "backward") {

    moveBackward(SPEED_BACK);

  }

  else if (remoteCommand == "left") {
    turnLeft(SPEED_TURN);


  }

  else if (remoteCommand == "right") {
    turnRight(SPEED_TURN);


  }
  else {
    stopMotors();
  }
}


//simplemnte para el robot
void handleStationaryMode() {

  stopMotors();


//para enviar datos cada cierto intervlao
//esto ayuda  reducir un poco el consumo 
  if (millis() - lastStationarySend >= STATIONARY_INTERVAL) {
    sendDataToFlask();
    lastStationarySend = millis();
  }

}


void updatePowerMode() {//para la gestion de bajo consumo
  
  if (robotMode != previousRobotMode) {

    if (robotMode == "stationary") {
      stopMotors();//no moverse

      
      WiFi.setSleep(true);//ahorro wifi

    
      setCpuFrequencyMhz(80);//freucencia de operacin menor


//debug
      Serial.println("Modo estacionario: reduccion de consumo activa");
      Serial.print("Frecuencia CPU: ");
      Serial.print(getCpuFrequencyMhz());

      Serial.println(" MHz");
    } 
    else {
    
      WiFi.setSleep(false);//desactivar sleep en los modos de movimiento 

      
      setCpuFrequencyMhz(240);//frecuencia de operacion default

      Serial.println("Modo activo: reduccion de consumo desactivada");
      Serial.print("Frecuencia CPU: ");


      Serial.print(getCpuFrequencyMhz());
      Serial.println(" MHz");
    }

    previousRobotMode = robotMode;//actualiza
  }

}
//configuracion basica 
void setup() {

  Serial.begin(115200);

  delay(1000);

  //los serial print son informativos para debuh en terminal

  Serial.println();

  Serial.println("Robot ESP32 + ToF + DHT20 + INA219 + MQ-2 + Flask + modos");

  Wire.begin(I2C_SDA, I2C_SCL);

  Wire.setClock(400000);

  analogSetAttenuation(ADC_11db);

  mq2StartTime = millis();
  Serial.println("MQ-2 calentando...");

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

  WiFi.setSleep(false);


  //intenta conectar a red
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);


  Serial.println("Conectando a WiFi...");

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.println("Conectando...");
  }

  WiFi.setSleep(true);
  


  Serial.println("Conectado a WiFi");
  Serial.print("IP ESP32: ");
  Serial.println(WiFi.localIP());

  Serial.println("Sistema listo.");
}

void loop() {
  unsigned long now = millis();

  readMq2Sensor(now);


  frontDistanceMm = readToF(tofFront, tof_front_ok, frontTofValid);

  rightDistanceMm = readToF(tofRight, tof_right_ok, rightTofValid);

  leftDistanceMm  = readToF(tofLeft,  tof_left_ok,  leftTofValid);


//umbrlaes para mover coche
  bool frontBlocked = frontTofValid && frontDistanceMm < FRONT_LIMIT_MM;

  bool rightTooClose = rightTofValid && rightDistanceMm < SIDE_LIMIT_MM;
  bool leftTooClose  = leftTofValid  && leftDistanceMm  < SIDE_LIMIT_MM;  

  bool rightFree = !rightTofValid || rightDistanceMm > SIDE_LIMIT_MM;



  bool leftFree  = !leftTofValid  || leftDistanceMm  > SIDE_LIMIT_MM; 
  if (now - lastCommandCheck >= COMMAND_CHECK_INTERVAL) {


    lastCommandCheck = now;
    readCommandFromFlask();
  }


  updatePowerMode();//activa/desactiva modo bajo consumo segun sea necesario

  if (robotMode == "stationary") {
    handleStationaryMode();
    return;
  }


  if (now - lastSensorPrint >= SENSOR_PRINT_TIME) {
    lastSensorPrint = now;

    Serial.println("----- Estado general -----");

    Serial.print("Modo: ");




    Serial.println(robotMode);

    Serial.print("Comando remoto: ");
    Serial.println(remoteCommand);



    Serial.print("Edad comando: ");



    Serial.print(commandAgeMs);
    Serial.println(" ms");


    Serial.print("Estado navegacion: ");

    Serial.println(getRobotStateName());

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

    Serial.print("MQ-2 listo: ");

    Serial.println(mq2Ready ? "SI" : "NO");

    if (mq2Ready) {
      Serial.print("MQ-2 raw: ");
      Serial.print(gasRaw);

      Serial.print(" | V ESP32: ");


      Serial.print(gasVoltageEsp);





      Serial.print(" V | V MQ2 estimado: ");
      Serial.print(gasVoltageMq2);

      Serial.print(" V | Alerta gas: ");
      Serial.println(gasAlert ? "SI" : "NO");
    }
  }

  if (robotMode == "stationary") {

    handleStationaryMode();
  }
  else if (robotMode == "remote") {
    handleRemoteControl(frontBlocked);


  }
  else {
    handleAutonomousNavigation(
      now,


      frontBlocked,

      rightTooClose,
      leftTooClose,
      rightFree,
      leftFree
    );
  }



  if (now - lastHttpSend >= HTTP_SEND_INTERVAL) {
    lastHttpSend = now;
    
    sendDataToFlask();
  }
}