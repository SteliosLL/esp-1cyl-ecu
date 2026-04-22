#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WebSocketsServer.h>
#include <LittleFS.h>
#include <EEPROM.h>

struct TuningSettings {
  float maxAdvance;
  int rpmLimit;
  //
  bool launchControl;
  int launchRPM;
  //
  bool idleAdjust;
  int idleStyle;
  //
  bool engineLocked;
};

TuningSettings settings;
TuningSettings defaultSettings;

// --- Pins ---
const byte sensorPin = 12;   
const byte ignitionPin = 14; 

// Variables for engine math
volatile unsigned long lastMicros = 0;
volatile unsigned long pulseInterval = 0;
volatile bool engineRunning = false;
volatile float currentRPM = 0;
volatile float currentTiming = 0;
volatile float engineTemp = 0;

const String AP_name = "ECU_Tuning_Port";
const String AP_passwd = "1234";

ESP8266WebServer server(80);
WebSocketsServer webSocket = WebSocketsServer(81);

void webSocketEvent(uint8_t num, WStype_t type, uint8_t * payload, size_t length) {
    //We don't necessarily need to handle incoming data from the phone, but the library requires this callback function.
}

void saveSettings() {
  EEPROM.put(0, settings);
  EEPROM.commit();
  Serial.println("Settings saved to EEPROM");
}

void loadSettings() {
  EEPROM.get(0, settings);
  //If EEPROM is empty/garbage, set defaults
  if (isnan(settings.maxAdvance) || settings.rpmLimit < 1000) {
    settings = defaultSettings;
    saveSettings();
    Serial.println("EEPROM settings are garbage. System Reset to Factory Defaults");
  }
}

// --- Web Handlers ---
void handleSave() {
    settings.maxAdvance = server.arg("ignitionTimingDegTxtBox").toFloat();
    settings.rpmLimit = server.arg("rpmLimiterTxtBox").toInt();

    settings.launchControl = (server.arg("launchCtrlChckBox") == "true");
    settings.launchRPM = server.arg("launchCtrlRPMTxtBox").toInt();

    settings.idleAdjust = (server.arg("idleAdjustChckBox") == "true");
    settings.idleStyle = server.arg("idleStyleDropDown").toInt();;

    saveSettings(); 

    // Debug print
    Serial.println("--- Tuning Updated ---");
    Serial.printf("Timing: %.2f | Limit: %d\n", settings.maxAdvance, settings.rpmLimit);
    Serial.printf("Launch: %s (%d RPM)\n", settings.launchControl ? "ON" : "OFF", settings.launchRPM);
   // Serial.printf("Idle Adj: %s | Style: %s\n", settings.idleAdjust ? "ON" : "OFF", settings.idleStyle.c_str());

    server.send(200, "text/plain", "OK");
}

void handleResetDefault() {
  settings = defaultSettings;
  saveSettings();
  
  Serial.println("System Reset to Factory Defaults");

  server.send(200, "text/plain", "Defaults Loaded");
}

void handleGetSettings() {
  String json = "{";
  json += "\"maxAdvance\":" + String(settings.maxAdvance) + ",";
  json += "\"rpmLimit\":" + String(settings.rpmLimit) + ",";
  
  json += "\"launchControl\":" + String(settings.launchControl ? "true" : "false") + ",";
  json += "\"launchRPM\":" + String(settings.launchRPM) + ",";
  
  json += "\"idleAdjustEn\":" + String(settings.idleAdjust ? "true" : "false") + ",";
  
  json += "\"idleStyle\":" + String(settings.idleStyle); 
  
  json += "}";
  
  server.send(200, "application/json", json);
}
void handleLiveStats() {
    String json = "{";
    json += "\"rpm\":" + String(currentRPM) + ",";
    json += "\"timing\":" + String(currentTiming) + ",";
    json += "\"temp\":" + String(engineTemp) + ",";
    json += "\"limit\":" + String(settings.rpmLimit);
    json += "}";
    server.send(200, "application/json", json);
}

// --- Ignition Logic  ---
void IRAM_ATTR fireSpark() {
  digitalWrite(ignitionPin, HIGH); 
  delayMicroseconds(500); 
  digitalWrite(ignitionPin, LOW); 
  timer1_disable();
}

void IRAM_ATTR handlePulse() {
  unsigned long now = micros();
  unsigned long delta = now - lastMicros;
  if (delta < 4000) return;

  pulseInterval = delta;
  lastMicros = now;
  engineRunning = true;

  currentRPM = 60000000.0 / delta;
  engineTemp = 12;

  // --- RPM LIMITER CHECK ---
  if (currentRPM > settings.rpmLimit) 
  {
    digitalWrite(LED_BUILTIN, HIGH);
    delay(50);
    digitalWrite(LED_BUILTIN, LOW);
    return;
  }

  float targetAdvance;
  // Use dynamic settings.maxAdvance instead of hardcoded 28.0
  if (currentRPM < 500) targetAdvance = 0.0;
  else if (currentRPM < 1500) targetAdvance = 10.0;
  else if (currentRPM < 4000) targetAdvance = 10.0 + (currentRPM - 1500) * ((settings.maxAdvance - 10.0) / 2500.0);
  else targetAdvance = settings.maxAdvance;

  float degreesToWait = 35.0 - targetAdvance;
  unsigned long delayUs = (unsigned long)(degreesToWait * (delta / 360.0));
  currentTiming = targetAdvance;

  if (delayUs > 50) {
    timer1_write(delayUs * 5); 
    timer1_enable(TIM_DIV16, TIM_EDGE, TIM_SINGLE);
  } else {
    fireSpark();
  }
}


void handleEngineLock() {
  if (server.uri() == "/lockEngine") {
    settings.engineLocked = true;
    detachInterrupt(digitalPinToInterrupt(sensorPin));
    Serial.println("engine locked");
  } else if (server.uri() == "/unlockEngine") {
    settings.engineLocked = false;
    attachInterrupt(digitalPinToInterrupt(sensorPin), handlePulse, RISING);
    Serial.println("engine unlocked");
  }
  saveSettings();

  server.send(200, "text/plain", "OK");
}

void setup() {
  Serial.begin(115200);
  EEPROM.begin(512); // Allocate 512 bytes for settings
  //default settings
  defaultSettings.engineLocked = false;
  //
  defaultSettings.maxAdvance = 28.0;
  defaultSettings.rpmLimit = 7000;
  //
  defaultSettings.launchControl = false;
  defaultSettings.launchRPM = 3000;
  //
  defaultSettings.idleAdjust = false;
  defaultSettings.idleStyle = 1;
  // Get saved info on boot
  loadSettings();

  LittleFS.begin();
  WiFi.mode(WIFI_AP);
  WiFi.softAP(AP_name, AP_passwd);

  // Serve the HTML file
  server.on("/", HTTP_GET, [](){
    File file = LittleFS.open("/index.html", "r");
    server.streamFile(file, "text/html");
    file.close();
  });
  server.on("/resetDefault", HTTP_POST, handleResetDefault);
  server.on("/getSettings", HTTP_GET, handleGetSettings);
  server.on("/lockEngine", HTTP_POST, handleEngineLock);
  server.on("/unlockEngine", HTTP_POST, handleEngineLock);
  server.on("/getLiveStats",HTTP_GET, handleLiveStats);

  server.on("/save", HTTP_POST, handleSave);

  server.begin();
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);

  pinMode(ignitionPin, OUTPUT);
  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(ignitionPin, LOW);
  pinMode(sensorPin, INPUT);
  timer1_isr_init();
  timer1_attachInterrupt(fireSpark);
  if (defaultSettings.engineLocked)
  {
    detachInterrupt(digitalPinToInterrupt(sensorPin));
  }
  else
  {
    attachInterrupt(digitalPinToInterrupt(sensorPin), handlePulse, RISING);
  }

  Serial.println("ECU has booted");

}

void loop() {
  static bool wifiIsOn = true;
  static bool wifiLockedOut = false;
  static unsigned long lastUpdate = 0;

  // Safety Watchdog
  if (micros() - lastMicros > 500000) {
    engineRunning = false;
  }

  // WiFi Management
  if (engineRunning) {
    if (wifiIsOn && WiFi.softAPgetStationNum() == 0) {
      WiFi.mode(WIFI_OFF);
      WiFi.forceSleepBegin(); 
      wifiIsOn = false;
      wifiLockedOut = true;
      Serial.println("Ride Mode: WiFi disabled.");
    }
  } else {
    if (!wifiIsOn) {
      wifiLockedOut = false; 
      WiFi.forceSleepWake();
      delay(1); 
      WiFi.mode(WIFI_AP);
      WiFi.softAP(AP_name, AP_passwd);
      wifiIsOn = true;
      Serial.println("Tuning Mode Enabled.");
    }
  }

  if (wifiIsOn && !wifiLockedOut) {
    static unsigned long lastWSUpdate = 0;
    if (millis() - lastWSUpdate > 100) {
        lastWSUpdate = millis();
        
        // Format: RPM,Timing,Limit
        String msg = String(currentRPM) + "," + String(currentTiming) + "," + String(settings.rpmLimit);
        webSocket.broadcastTXT(msg);
    }
    
    webSocket.loop();
    server.handleClient();
  }

  // Serial Diagnostics
  if (millis() - lastUpdate > 250) {
    if (engineRunning) {
      noInterrupts();
      unsigned long interval = pulseInterval;
      interrupts();
      Serial.printf("RPM: %lu\n", (unsigned long)(60000000 / interval));
    } 
    lastUpdate = millis();
  }
}