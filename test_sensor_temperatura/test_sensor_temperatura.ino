//======================================================================================//

#include <Arduino.h>
#include <Adafruit_AHTX0.h>

//======================================================================================//
// Global variables

Adafruit_AHTX0 dht; // Create a new sensor object.

//======================================================================================//
// Forward declarations

bool initDHT20();
void readSensor();

//======================================================================================//

void setup() {
  Serial.begin (115200);
  Serial.println();
  Serial.println ("Asair DHT20 - Temperature & Humidity Sensor Example");
  Serial.println();

  initDHT20();
}

//======================================================================================//

bool initDHT20() {
  Serial.println (F("initDHT20 [INFO]: Initializing DHT20 sensor.."));

  if (!dht.begin()) {
    Serial.println ("initDHT20 [ERROR]: Couldn't find the DHT20 sensor. Check your wiring and pull-up resistors.");
    while (1) yield();
  }

  Serial.println();
  return true;
}

//======================================================================================//

void loop() {
  readSensor();
  delay (1000);
}

//======================================================================================//

void readSensor() {
  sensors_event_t humidity, temp;
  dht.getEvent (&humidity, &temp); // Read the temperature and humidity

  Serial.print ("Temperature: ");
  Serial.print (temp.temperature);
  Serial.println (" °C");

  Serial.print ("Humidity: ");
  Serial.print (humidity.relative_humidity);
  Serial.println (" % RH");
  Serial.println();
}

//======================================================================================//