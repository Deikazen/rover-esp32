/*
  STANDALONE MOTOR TEST (DIAGNOSTICS)
  Gunakan ini untuk tes murni hardware tanpa WiFi/Dashboard.
*/

#define RPWM 19
#define LPWM 18
#define R_EN 23
#define L_EN 22 // Pastikan pin ini benar di board Anda

void setup() {
  Serial.begin(115200);
  Serial.println("--- MOTOR STANDALONE TEST ---");

  pinMode(RPWM, OUTPUT);
  pinMode(LPWM, OUTPUT);
  pinMode(R_EN, OUTPUT);
  pinMode(L_EN, OUTPUT);

  // Aktifkan Driver
  digitalWrite(R_EN, HIGH);
  digitalWrite(L_EN, HIGH);
  
  // Tes tanpa PWM dulu (Digital High) untuk power maksimal
  Serial.println("Testing Status: Drivers Enabled");
}

void loop() {
  Serial.println("Moving: FORWARD (Full Power)");
  digitalWrite(RPWM, HIGH);
  digitalWrite(LPWM, LOW);
  delay(2000);

  Serial.println("Moving: STOP");
  digitalWrite(RPWM, LOW);
  digitalWrite(LPWM, LOW);
  delay(1000);

  Serial.println("Moving: BACKWARD (Full Power)");
  digitalWrite(RPWM, LOW);
  digitalWrite(LPWM, HIGH);
  delay(2000);

  Serial.println("Moving: STOP");
  digitalWrite(RPWM, LOW);
  digitalWrite(LPWM, LOW);
  delay(1000);
}