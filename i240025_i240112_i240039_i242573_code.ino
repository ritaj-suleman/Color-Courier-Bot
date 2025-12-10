#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>

// ===== WiFi Credentials =====
const char* ssid = "FAST Faculty";
const char* password = "";

// ===== MQTT Broker Settings =====
const char* mqtt_server = "broker.hivemq.com";  
const int mqtt_port = 1883;
const char* mqtt_topic = "linefollow/colormatch";
const char* mqtt_client_id = "ESP32_LineFollower";

WiFiClient espClient;
PubSubClient mqtt(espClient);

// ===== SERVO SETTINGS =====
Servo myServo;
#define SERVO_PIN 13
#define SERVO_REST_POS 0
#define SERVO_ACTIVE_POS 90
#define SERVO_ACTIVE_TIME 3000  // 3 seconds in milliseconds

bool servoActive = false;
unsigned long servoActivatedAt = 0;

// ===== IR SENSOR PINS =====
#define ir1 32
#define ir2 25
#define ir3 34
#define ir4 35
#define ir5 23

// ===== MOTOR DRIVER PINS =====
#define IN1  4
#define IN2  5
#define ENA  2
#define IN3  18
#define IN4  19
#define ENB  21

int speed = 200;
bool colorMatch = false;
bool botStopped = false;
unsigned long lastMqttMessage = 0;

const uint8_t SAMPLES = 5;
const uint8_t SAMPLE_DELAY_MS = 2;

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  Serial.print("Message arrived [");
  Serial.print(topic);
  Serial.print("]: ");
  
  String message;
  for (int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  Serial.println(message);
  
  if (message == "1") {
    colorMatch = true;
    Serial.println("✓ COLOR MATCH - Bot stopping & Servo activating for 3 seconds");
    activateServo();
  } else if (message == "0") {
    colorMatch = false;
    Serial.println("✓ NO MATCH - Bot continuing");
  }
  
  lastMqttMessage = millis();
}

void activateServo() {
  myServo.write(SERVO_ACTIVE_POS);
  servoActive = true;
  servoActivatedAt = millis();
  Serial.print("🔄 Servo moved to: ");
  Serial.print(SERVO_ACTIVE_POS);
  Serial.println("° for 3 seconds");
}

void checkServoTimer() {
  if (servoActive && (millis() - servoActivatedAt >= SERVO_ACTIVE_TIME)) {
    myServo.write(SERVO_REST_POS);
    servoActive = false;
    Serial.print("🔄 Servo reset to: ");
    Serial.print(SERVO_REST_POS);
    Serial.println("° (3 seconds elapsed)");
  }
}

void reconnectMQTT() {
  while (!mqtt.connected()) {
    Serial.print("Attempting MQTT connection...");
    
    if (mqtt.connect(mqtt_client_id)) {
      Serial.println("connected!");
      mqtt.subscribe(mqtt_topic);
      Serial.print("Subscribed to: ");
      Serial.println(mqtt_topic);
    } else {
      Serial.print("failed, rc=");
      Serial.print(mqtt.state());
      Serial.println(" trying again in 5 seconds");
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n=== Line Follower Bot (MQTT + Timed Servo) Starting ===");
  
  // Setup Servo
  myServo.attach(SERVO_PIN);
  myServo.write(SERVO_REST_POS);
  Serial.print("✓ Servo initialized on pin ");
  Serial.print(SERVO_PIN);
  Serial.print(" at ");
  Serial.print(SERVO_REST_POS);
  Serial.println("°");
  
  // Connect to WiFi
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  Serial.print("Connecting to WiFi");
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\n✓ WiFi Connected!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\n✗ WiFi Connection Failed!");
  }
  
  // Setup MQTT
  mqtt.setServer(mqtt_server, mqtt_port);
  mqtt.setCallback(mqttCallback);
  
  // Motor Direction Pins
  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(IN3, OUTPUT);
  pinMode(IN4, OUTPUT);
  pinMode(ENA, OUTPUT);
  pinMode(ENB, OUTPUT);
  
  // Sensor Pins
  pinMode(ir1, INPUT_PULLUP);
  pinMode(ir2, INPUT_PULLUP);
  pinMode(ir3, INPUT_PULLUP);
  pinMode(ir4, INPUT_PULLUP);
  pinMode(ir5, INPUT_PULLUP);
}

void loop() {
  // Maintain MQTT connection
  if (!mqtt.connected()) {
    reconnectMQTT();
  }
  mqtt.loop();
  
  // Check if servo timer has elapsed
  checkServoTimer();
  
  // If color matches, stop the bot
  if (colorMatch) {
    if (!botStopped) {
      stopMotors();
      botStopped = true;
      Serial.println("⏸ COLOR MATCH! Bot stopped.");
    }
    return;
  }
  
  botStopped = false;
  
  // Read sensors
  int s1 = readSensor(ir1);
  int s2 = readSensor(ir2);
  int s3 = readSensor(ir3);
  int s4 = readSensor(ir4);
  int s5 = readSensor(ir5);
  
  static unsigned long lastPrint = 0;
  if (millis() - lastPrint > 1000) {
    Serial.printf("Sensors: [%d][%d][%d][%d][%d]\n", s1, s2, s3, s4, s5);
    lastPrint = millis();
  }
  
  if (s3 && !s2 && !s4) {
    moveForward();
  }
  else if (s2 && !s4) {
    turnLeft();
  }
  else if (s4 && !s2) {
    turnRight();
  }
  else if (s2 && s3) {
    turnLeft();
  }
  else if (s3 && s4) {
    turnRight();
  }
  else if (s1) {
    sharpLeft();
  }
  else if (s5) {
    sharpRight();
  }
  else {
    stopMotors();
  }
  
  delay(50);
}

int readSensor(uint8_t pin) {
  uint8_t hits = 0;
  for (uint8_t i = 0; i < SAMPLES; ++i) {
    if (digitalRead(pin) == LOW) hits++;
    delay(SAMPLE_DELAY_MS);
  }
  return (hits > (SAMPLES / 2)) ? 1 : 0;
}

void moveForward() {
  analogWrite(ENA, speed); 
  analogWrite(ENB, speed);
  digitalWrite(IN1, HIGH); 
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, HIGH); 
  digitalWrite(IN4, LOW);
}

void turnLeft() {
  analogWrite(ENA, 140);
  analogWrite(ENB, speed);
  digitalWrite(IN1, HIGH); 
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, HIGH); 
  digitalWrite(IN4, LOW);
}

void turnRight() {
  analogWrite(ENA, speed); 
  analogWrite(ENB, 140);
  digitalWrite(IN1, HIGH); 
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, HIGH); 
  digitalWrite(IN4, LOW);
}

void sharpLeft() {
  analogWrite(ENA, 0); 
  analogWrite(ENB, speed);
  digitalWrite(IN1, LOW); 
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, HIGH); 
  digitalWrite(IN4, LOW);
}

void sharpRight() {
  analogWrite(ENA, speed); 
  analogWrite(ENB, 0);
  digitalWrite(IN1, HIGH); 
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW); 
  digitalWrite(IN4, LOW);
}

void stopMotors() {
  analogWrite(ENA, 0); 
  analogWrite(ENB, 0);
  digitalWrite(IN1, LOW); 
  digitalWrite(IN2, LOW);
  digitalWrite(IN3, LOW); 
  digitalWrite(IN4, LOW);
}