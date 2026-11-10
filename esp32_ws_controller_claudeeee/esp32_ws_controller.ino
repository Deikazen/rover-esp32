#include <WiFi.h>
#include <WebSocketsServer.h>
#include <LiquidCrystal_I2C.h>
#include <Wire.h>

LiquidCrystal_I2C lcd(0x27, 16, 2);

// ===== WIFI =====
const char *ssid = "ROVER-LINK";
const char *password = "rover123";

// ===== WEBSOCKET =====
WebSocketsServer webSocket(81);

// ===== PIN STEERING (L298N-style) =====
#define ENA 14
#define IN1 27
#define IN2 26

// ===== PIN MAIN DRIVER (BTS7960) =====
#define R_EN 23
#define L_EN 4
#define RPWM 19
#define LPWM 18

// ===== PWM CONFIG =====
#define PWM_FREQ 20000
#define PWM_RES 8

// ===== TUNING =====
// Polaritas wiring: IN2 HIGH = fisik KIRI di rover ini, jadi default true.
// Kalau setelah semua patch belok MASIH kebalik, ubah true -> false.
#define STEER_INVERT true

#define MAX_PWM_BTS 150
#define MIN_PWM_BTS 80
#define STEER_PWM 250     // kekuatan motor steering (0-255)
#define STEER_PULSE_MS 85 // durasi pulse untuk perintah L / R
#define FAILSAFE_MS 1500  // stop kalau tidak ada pesan (heartbeat 'h' tiap 250ms dari HTML)

// Ramp halus supaya arus tidak drop mendadak (brownout = lag WiFi)
#define RAMP_INTERVAL_MS 10
#define RAMP_UP_STEP 15 // 0 -> 150 sekitar 100ms
#define RAMP_DOWN_STEP 30

#define LCD_INTERVAL_MS 250
#define RPM_INTERVAL_MS 200

// ===== STATE =====
int speedBTS = MAX_PWM_BTS; // dipakai tombol w/s
int targetSpeed = 0;
bool targetForward = true;
int currentPwm = 0;
bool currentForward = true;

int steerState = 0; // arah yang sedang aktif di motor
int holdDir = 0;    // arah 'tahan' (a/d); pulse L/R ditumpuk di atasnya
bool pulseActive = false;
unsigned long pulseEndTime = 0;

unsigned long lastMsgTime = 0;
bool failsafeOn = false;

// ===== STEERING =====
// Satu-satunya tempat yang menyentuh IN1/IN2 -> arah selalu konsisten.
// dir: -1 = kiri, +1 = kanan, 0 = off
void setSteer(int dir)
{
    steerState = dir;
    if (dir == 0)
    {
        ledcWrite(ENA, 0);
        digitalWrite(IN1, LOW);
        digitalWrite(IN2, LOW);
        return;
    }
    bool in1 = (dir < 0); // kiri: IN1 HIGH, kanan: IN2 HIGH
    if (STEER_INVERT)
        in1 = !in1;
    digitalWrite(IN1, in1 ? HIGH : LOW);
    digitalWrite(IN2, in1 ? LOW : HIGH);
    ledcWrite(ENA, STEER_PWM);
}

void pulseSteer(int dir)
{
    setSteer(dir);
    pulseActive = true;
    pulseEndTime = millis() + STEER_PULSE_MS;
}

void holdSteer(int dir)
{
    holdDir = dir;
    pulseActive = false; // hold membatalkan pulse
    setSteer(dir);
}

// ===== DRIVE =====
void setDrive(bool fwd, int pwm)
{
    targetForward = fwd;
    targetSpeed = constrain(pwm, 0, MAX_PWM_BTS);
}

void hardStop()
{
    targetSpeed = 0;
    currentPwm = 0;
    ledcWrite(RPWM, 0);
    ledcWrite(LPWM, 0);
}

void updateMotor()
{
    static unsigned long last = 0;
    static int lastPwmOut = -1;
    static bool lastFwdOut = true;

    unsigned long now = millis();
    if (now - last < RAMP_INTERVAL_MS)
        return;
    last = now;

    int target = targetSpeed;
    if (target > 0 && target < MIN_PWM_BTS)
        target = MIN_PWM_BTS;

    // Ganti arah: turun ke 0 dulu baru balik
    if (currentPwm > 0 && currentForward != targetForward)
        target = 0;

    if (currentPwm < target)
        currentPwm = min(currentPwm + RAMP_UP_STEP, target);
    else if (currentPwm > target)
        currentPwm = max(currentPwm - RAMP_DOWN_STEP, target);

    if (currentPwm == 0)
        currentForward = targetForward;

    if (currentPwm != lastPwmOut || currentForward != lastFwdOut)
    {
        if (currentForward)
        {
            ledcWrite(RPWM, currentPwm);
            ledcWrite(LPWM, 0);
        }
        else
        {
            ledcWrite(RPWM, 0);
            ledcWrite(LPWM, currentPwm);
        }
        lastPwmOut = currentPwm;
        lastFwdOut = currentForward;
    }
}

// ===== WEBSOCKET =====
// Cari suffix a/d. Tidak ada suffix = steering 'tahan' dilepas
// (pulse L/R yang sedang jalan tidak dibatalkan).
void applySteerSuffix(const char *m, size_t len)
{
    for (size_t i = 1; i < len; i++)
    {
        if (m[i] == 'a')
        {
            holdSteer(-1);
            return;
        }
        if (m[i] == 'd')
        {
            holdSteer(+1);
            return;
        }
    }
    holdDir = 0;
    if (!pulseActive)
        setSteer(0);
}

// Format: F<pwm>[a|d]  atau  B<pwm>[a|d]
void handleDriveMsg(const char *m, size_t len)
{
    bool fwd = (m[0] == 'F');
    int val = atoi(m + 1);
    setDrive(fwd, val);

    applySteerSuffix(m, len);
}

void webSocketEvent(uint8_t num, WStype_t type, uint8_t *payload, size_t length)
{
    if (type == WStype_DISCONNECTED)
    {
        hardStop();
        holdSteer(0);
        return;
    }
    if (type != WStype_TEXT || length == 0)
        return;

    lastMsgTime = millis();
    const char *m = (const char *)payload;
    char cmd = m[0];

    switch (cmd)
    {
    case 'F':
    case 'B':
        handleDriveMsg(m, length);
        break;
    case 'v':
    {
        int v = atoi(m + 1);
        if (v >= 0 && v <= 255)
            speedBTS = min(v, MAX_PWM_BTS);
        break;
    }
    case 'h':
        break; // heartbeat
    case 'w':
        setDrive(true, speedBTS);
        break;
    case 's':
        setDrive(false, speedBTS);
        break;
    case 'a':
        holdSteer(-1);
        break;
    case 'd':
        holdSteer(+1);
        break;
    case 'L':
        pulseSteer(-1);
        break;
    case 'R':
        pulseSteer(+1);
        break;
    case 'c':
        holdSteer(0);
        break; // center / lepas steering
    case 'x':
        hardStop();
        if (length > 1)
            applySteerSuffix(m, length);
        break; // 'x' / 'xa' / 'xd'
    case 'X':
        hardStop();
        holdSteer(0);
        break; // stop total
    default:
        break;
    }
}

// ===== LCD (rate-limited, hanya tulis kalau berubah) =====
void updateLCD()
{
    static unsigned long last = 0;
    static char prev1[17] = "";
    static char prev2[17] = "";

    unsigned long now = millis();
    if (now - last < LCD_INTERVAL_MS)
        return;
    last = now;

    char l1[24], l2[24];
    int clients = webSocket.connectedClients();
    if (clients > 0)
        snprintf(l1, sizeof(l1), "Client: %-8d", clients);
    else
        snprintf(l1, sizeof(l1), "%-16s", WiFi.localIP().toString().c_str());

    const char *act;
    if (targetSpeed > 0)
        act = targetForward ? "MAJU" : "MUNDUR";
    else if (steerState < 0)
        act = "KIRI";
    else if (steerState > 0)
        act = "KANAN";
    else
        act = "STOP";
    snprintf(l2, sizeof(l2), "%-6s Spd:%-5d", act, targetSpeed);

    l1[16] = '\0';
    l2[16] = '\0';

    if (strcmp(l1, prev1) != 0)
    {
        lcd.setCursor(0, 0);
        lcd.print(l1);
        strcpy(prev1, l1);
    }
    if (strcmp(l2, prev2) != 0)
    {
        lcd.setCursor(0, 1);
        lcd.print(l2);
        strcpy(prev2, l2);
    }
}

// ===== SETUP =====
void setup()
{
    Serial.begin(115200);

    Wire.begin();
    lcd.init();
    Wire.setClock(400000); // I2C lebih cepat -> LCD tidak nge-block lama
    lcd.backlight();
    lcd.setCursor(0, 0);
    lcd.print("Initializing...");

    pinMode(IN1, OUTPUT);
    pinMode(IN2, OUTPUT);
    pinMode(R_EN, OUTPUT);
    pinMode(L_EN, OUTPUT);

    ledcAttach(ENA, PWM_FREQ, PWM_RES);
    ledcAttach(RPWM, PWM_FREQ, PWM_RES);
    ledcAttach(LPWM, PWM_FREQ, PWM_RES);

    digitalWrite(R_EN, HIGH);
    digitalWrite(L_EN, HIGH);
    hardStop();
    setSteer(0);

    lcd.clear();
    lcd.print("Connecting WiFi");

    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false); // PENTING: matikan WiFi power-save (sumber lag 100-300ms)
    WiFi.setAutoReconnect(true);
    WiFi.begin(ssid, password);

    unsigned long t0 = millis();
    while (WiFi.status() != WL_CONNECTED)
    {
        delay(250);
        Serial.print(".");
        if (millis() - t0 > 20000)
        {
            Serial.println("\nWiFi gagal, restart...");
            ESP.restart();
        }
    }

    Serial.println("\nWiFi Connected");
    Serial.println(WiFi.localIP());

    lcd.clear();
    lcd.setCursor(0, 0);
    lcd.print("IP Address:");
    lcd.setCursor(0, 1);
    lcd.print(WiFi.localIP());
    delay(2000);
    lcd.clear();

    webSocket.begin();
    webSocket.onEvent(webSocketEvent);
    webSocket.enableHeartbeat(10000, 3000, 2); // deteksi client mati

    lastMsgTime = millis();
}

// ===== LOOP =====
void loop()
{
    webSocket.loop();
    updateMotor();

    // Akhiri pulse steering
    if (pulseActive && (long)(millis() - pulseEndTime) >= 0)
    {
        pulseActive = false;
        setSteer(holdDir); // balik ke hold (atau off kalau tidak ada)
    }

    // Failsafe
    if (millis() - lastMsgTime > FAILSAFE_MS)
    {
        if (!failsafeOn)
        {
            hardStop();
            holdSteer(0);
            failsafeOn = true;
            Serial.println("FAILSAFE: Active");
        }
    }
    else
    {
        failsafeOn = false;
    }

    // Telemetry RPM
    static unsigned long lastRpm = 0;
    if (millis() - lastRpm >= RPM_INTERVAL_MS)
    {
        lastRpm = millis();
        if (webSocket.connectedClients() > 0)
        {
            char buf[16];
            snprintf(buf, sizeof(buf), "r%d", currentPwm * 20);
            webSocket.broadcastTXT(buf);
        }
    }

    updateLCD();
    delay(1);
}
