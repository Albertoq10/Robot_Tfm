// TUTORIAL INGTELECTO PRO - ROBOT ESP32 L298N

// Control de Dirección y Velocidad
// --- DEFINICIÓN DE PINES ---

const int enA = 14;  // Pin de Velocidad (PWM)
const int in1 = 27;  // Dirección 1
const int in2 = 26;  // Dirección 2

// Configuración PWM
const int freq = 1000;
const int pwmChannelA = 0;
const int resolution = 8; // 0 - 255

void setup() {
  Serial.begin(115200);

  pinMode(in1, OUTPUT);
  pinMode(in2, OUTPUT);


  ledcAttachChannel(enA, freq, resolution, pwmChannelA);

  Serial.println("Motor A girando al máximo");

 
  digitalWrite(in1, HIGH);
  digitalWrite(in2, LOW);


  ledcWrite(enA, 255);
}

void loop() {
  
}