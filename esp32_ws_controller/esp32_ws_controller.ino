#include <WiFi.h>
#include <WebSocketsServer.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);

// ===== WIFI =====
const char* ssid     = "ROVER-LINK";
const char* password = "rover123";

// ===== WEBSOCKET =====
WebSocketsServer webSocket(81);

#define MAX_PWM_BTS 150   // 🔥 batas maksimal (coba 150–200)

// ===== PIN LN (Steering Driver) =====
#define ENA 14
#define IN1 27
#define IN2 26

// ===== PIN BTS (Main Driver) =====
#define R_EN 23
#define L_EN 4
#define RPWM 19
#define LPWM 18

// ===== PWM CONFIG =====
#define PWM_FREQ    20000  // 20kHz
#define PWM_RES     8

int speedLN  = 250;
int speedBTS = MAX_PWM_BTS;  // default = MAX_PWM_BTS (100) agar aman

// ===== RAMPS & SMOOTHING (DISABLED FOR TESTING) =====
int targetSpeedBTS = 0;
#define MIN_PWM_BTS  80     // harus lebih kecil dari MAX_PWM_BTS

// ===== NON-BLOCKING STEERING PULSES =====
unsigned long steerPulseEndTime = 0;

// Global state for direction
bool drivingForward = true;

// ===== LCD STATE TRACKING =====
int lastDisplayedClients = -1;
int lastDisplayedSpeed = -1;

// STATE LCD
String currentAction = "STOP";
String lastAction = "";

// ===== FAILSAFE CONFIG =====
unsigned long lastMsgTime = 0;
const unsigned long FAILSAFE_TIMEOUT = 2000; 

void maju() {
  digitalWrite(R_EN, HIGH);
  digitalWrite(L_EN, HIGH);
  drivingForward = true;   // MAJU = forward
  targetSpeedBTS = min(speedBTS, MAX_PWM_BTS);  // cap ke MAX_PWM_BTS
  currentAction = "MAJU";
}

void mundur() {
  digitalWrite(R_EN, HIGH);
  digitalWrite(L_EN, HIGH);
  drivingForward = false;  // MUNDUR = backward
  targetSpeedBTS = min(speedBTS, MAX_PWM_BTS);  // cap ke MAX_PWM_BTS
  currentAction = "MUNDUR";
}

void kiri() {
  digitalWrite(IN1, HIGH);
  digitalWrite(IN2, LOW);
  ledcWrite(ENA, speedLN);
  currentAction = "KIRI";
}

void kanan() {
  digitalWrite(IN1, LOW);
  digitalWrite(IN2, HIGH);
  ledcWrite(ENA, speedLN);
  currentAction = "KANAN";
}

void stopAll() {
  targetSpeedBTS = 0;
  ledcWrite(RPWM, 0);
  ledcWrite(LPWM, 0);
  currentAction = "STOP";
}

void pulseSteer(bool isRight) {
  if (isRight) {
    digitalWrite(IN1, HIGH);
    digitalWrite(IN2, LOW);
  } else {
    digitalWrite(IN1, LOW);
    digitalWrite(IN2, HIGH);
  }
  steerPulseEndTime = millis() + 85; // High torque burst
  ledcWrite(ENA, speedLN);
  Serial.println(isRight ? "Pulse: R" : "Pulse: L");
}

// ===== FUNGSI UPDATE LCD =====
void updateLCD(int clients) {
  // ===== BARIS 1 (Client Count / IP) =====
  if (clients != lastDisplayedClients) {
    lastDisplayedClients = clients;

    lcd.setCursor(0, 0);
    lcd.print("                "); // Clear baris 1

    lcd.setCursor(0, 0);
    if (clients > 0) {
      lcd.print("Client: ");
      lcd.print(clients);
    } else {
      // Menampilkan IP ESP32 jika belum ada WebSocket Client terhubung
      lcd.print(WiFi.localIP());
    }
  }

  // ===== BARIS 2 (Status Aksi & Speed) =====
  if (currentAction != lastAction || targetSpeedBTS != lastDisplayedSpeed) {
    lastAction = currentAction;
    lastDisplayedSpeed = targetSpeedBTS;

    lcd.setCursor(0, 1);
    lcd.print("                "); // Clear baris 2

    lcd.setCursor(0, 1);
    lcd.print(currentAction);
    lcd.print(" Spd:");
    lcd.print(targetSpeedBTS);
  }
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
  if (type == WStype_TEXT) {
    lastMsgTime = millis();
    
    String msg = String((char*)payload);
    char cmd = msg[0];

    if (cmd == 'F' || cmd == 'B') {
      int steerPos = -1;
      if (msg.indexOf('a') > 0) steerPos = msg.indexOf('a');
      else if (msg.indexOf('d') > 0) steerPos = msg.indexOf('d');

      if (steerPos > 0) {
        char steer = msg[steerPos];
        if (steer == 'a') kiri(); else if (steer == 'd') kanan();
      }

      int steerIndex = (steerPos > 0) ? steerPos : msg.length();
      int val = msg.substring(1, steerIndex).toInt();
      targetSpeedBTS = min(val, MAX_PWM_BTS);  // cap ke MAX_PWM_BTS
      drivingForward = (cmd == 'F');
      currentAction = drivingForward ? "MAJU" : "MUNDUR";
      digitalWrite(R_EN, HIGH);
      digitalWrite(L_EN, HIGH);
      Serial.printf("Drive: %c %d | Steer: %s\n", cmd, val, steerPos > 0 ? (msg[steerPos] == 'a' ? "LEFT" : "RIGHT") : "KEEP");
    }
    else if (cmd == 'v') {
      int newVal = msg.substring(1).toInt();
      if (newVal >= 0 && newVal <= 255) speedBTS = min(newVal, MAX_PWM_BTS);  // clamp ke MAX_PWM_BTS
    } 
    else if (cmd == 'h') {
        // Heartbeat
    }
    else {
      switch (cmd) {
        case 'w': maju(); break;
        case 's': mundur(); break;
        case 'a': kiri(); break;
        case 'd': kanan(); break;
        case 'L': pulseSteer(false); break;
        case 'R': pulseSteer(true); break;
        case 'x': stopAll(); break;
        case 'c': ledcWrite(ENA, 0); Serial.println("Steer: CENTER"); break;
        default: Serial.printf("Other: %s\n", msg.c_str()); break;
      }
    }
  }
}

void setup() {
  Serial.begin(115200);

  lcd.init();                      // Inisialisasi LCD
  lcd.backlight();                 // Nyalakan lampu latar (backlight)
  
  lcd.setCursor(0, 0);
  lcd.print("Initializing...");

  pinMode(IN1, OUTPUT);
  pinMode(IN2, OUTPUT);
  pinMode(R_EN, OUTPUT);
  pinMode(L_EN, OUTPUT);
  pinMode(RPWM, OUTPUT);
  pinMode(LPWM, OUTPUT);
  pinMode(ENA, OUTPUT);

  ledcAttach(ENA, PWM_FREQ, PWM_RES);
  ledcAttach(RPWM, PWM_FREQ, PWM_RES);
  ledcAttach(LPWM, PWM_FREQ, PWM_RES);
  
  digitalWrite(R_EN, HIGH);
  digitalWrite(L_EN, HIGH);
  stopAll();

  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Connecting WiFi");

  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi Connected");
  Serial.println(WiFi.localIP());

  // Tampilkan IP pada LCD saat baru berhasil terhubung
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("IP Address:");
  lcd.setCursor(0, 1);
  lcd.print(WiFi.localIP());
  delay(2000); // Tampilkan IP selama 2 detik sebelum masuk ke mode siap

  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  lastMsgTime = millis();
}

void loop() {
  webSocket.loop();

  // Update tampilan LCD secara efisien tanpa flickering
  int currentClients = webSocket.connectedClients();
  updateLCD(currentClients);

  // --- Broadcast RPM Telemetry (Every 100ms) ---
  static unsigned long lastRpmTime = 0;
  if (millis() - lastRpmTime > 100) {
    lastRpmTime = millis();
    int rpm = targetSpeedBTS * 20; 
    String rpmMsg = "r" + String(rpm);
    webSocket.broadcastTXT(rpmMsg); 
  }

  // Apply to hardware DIRECTLY (No Ramp)
  int finalPwm = min(targetSpeedBTS, MAX_PWM_BTS);  // safety clamp terakhir

  if (targetSpeedBTS > 0 && finalPwm < MIN_PWM_BTS) {
    finalPwm = MIN_PWM_BTS;
  }

  if (drivingForward) {
    ledcWrite(RPWM, finalPwm);
    ledcWrite(LPWM, 0);
  } else {
    ledcWrite(RPWM, 0);
    ledcWrite(LPWM, finalPwm);
  }

  // DEBUG Telemetry
  static int lastDebugPwm = -1;
  if (finalPwm != lastDebugPwm) {
    Serial.print("PWM Output: "); 
    Serial.println(finalPwm);
    lastDebugPwm = finalPwm;
  }

  // Handle Steering Pulse Timeout
  if (millis() >= steerPulseEndTime && steerPulseEndTime != 0) {
    ledcWrite(ENA, 0);
    steerPulseEndTime = 0;
  }

  // Failsafe Check
  static bool timeoutActive = false;
  if (millis() - lastMsgTime > FAILSAFE_TIMEOUT) {
    if (!timeoutActive) {
      targetSpeedBTS = 0;
      ledcWrite(ENA, 0);
      ledcWrite(RPWM, 0);
      ledcWrite(LPWM, 0);
      Serial.println("FAILSAFE: Active");
      timeoutActive = true;
    }
  } else {
    timeoutActive = false; 
  }
  
  delay(5); 
}