#define MQ2_AO_PIN 35

const unsigned long MQ2_WARMUP_TIME = 20000;  
const unsigned long MQ2_READ_INTERVAL = 1000;  

unsigned long mq2StartTime = 0;
unsigned long lastMq2Read = 0;

bool mq2Ready = false;

int gasRaw = -1;
float gasVoltageEsp = 0.0;
float gasVoltageMq2 = 0.0;
bool gasAlert = false;

const int GAS_THRESHOLD = 2200;  

void setup() {
  Serial.begin(115200);

  analogSetAttenuation(ADC_11db);

  mq2StartTime = millis();

  Serial.println("MQ-2 calentando...");
}

void loop() {
  unsigned long now = millis();

  readMq2Sensor(now);

 
}

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

    
    gasVoltageEsp = gasRaw * (3.3 / 4095.0);

 
    gasVoltageMq2 = gasVoltageEsp * 1.5;

    gasAlert = gasRaw > GAS_THRESHOLD;

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