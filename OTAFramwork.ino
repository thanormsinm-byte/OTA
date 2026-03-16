#include <WiFi.h>
#include <WiFiManager.h> 
#include <ArduinoOTA.h>
#include <HTTPUpdateServer.h>
#include <WebServer.h>
#include <HTTPClient.h>
#include <HTTPUpdate.h>
#include <WiFiClientSecure.h> 
#include <Update.h> 
#include <Wire.h> 
#include <LiquidCrystal_I2C.h>

// --- [ ส่วนที่ 1: การตั้งค่าระบบ ] ---
float currentVersion = 1.0; 
const String versionURL  = "https://raw.githubusercontent.com/thanormsinm-byte/OTA/main/version.json";
const String firmwareURL = "https://raw.githubusercontent.com/thanormsinm-byte/OTA/main/OTA.bin";

WebServer server(80);
HTTPUpdateServer httpUpdater;
LiquidCrystal_I2C lcd(0x27, 20, 4); 

unsigned long previousMillis = 0;
unsigned long blinkMillis = 0;
unsigned long buttonPressTime = 0;
unsigned long lastUpdateCheck = 0; // สำหรับคุมเวลาเช็ค Update ทุก 60 วิ

int displayPage = 0; 
bool blinkState = false; 
bool isOnline = false; 
String fullHostname = "";
String macAddrStr = "";

const int LedHeartBeat = 8; 
const int ledPin = 10;        
const int configButton = 9; 

// --- [ ส่วนที่ 2: ฟังก์ชันเสริม (Helper Functions) ] ---

String getEfuseMac() {
  uint64_t mac = ESP.getEfuseMac(); char macBuf[18];
  sprintf(macBuf, "%02X:%02X:%02X:%02X:%02X:%02X", (uint8_t)(mac >> 0), (uint8_t)(mac >> 8), (uint8_t)(mac >> 16), (uint8_t)(mac >> 24), (uint8_t)(mac >> 32), (uint8_t)(mac >> 40));
  return String(macBuf);
}

// ฟังก์ชันสร้าง Hostname จาก Mac 6 หลักสุดท้าย
void setupHostname() {
  uint64_t chipid = ESP.getEfuseMac(); 
  uint8_t b3 = (uint8_t)(chipid >> 24);
  uint8_t b4 = (uint8_t)(chipid >> 32);
  uint8_t b5 = (uint8_t)(chipid >> 40);
  char hexStr[7];
  sprintf(hexStr, "%02X%02X%02X", b5, b4, b3); // ดึง 3 byte ท้าย (6 ตัวอักษร)
  fullHostname = "ESP32-" + String(hexStr);
}

String getSharedHTML(bool isUpdatePage) {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>body { font-family: sans-serif; text-align: center; color: #333; margin: 0; padding-top: 50px; }";
  html += ".container { max-width: 400px; margin: auto; padding: 20px; } h2 { font-size: 16px; font-weight: bold; }";
  html += "table { width: 100%; margin-top: 30px; border-collapse: collapse; } td { padding: 10px; border-bottom: 1px solid #eee; text-align: left; font-size: 13px; }";
  html += ".val { text-align: right; font-weight: bold; color: #f39c12; } </style></head><body>";
  html += "<div class='container'><h2>" + String(isUpdatePage ? "UPDATE FIRMWARE" : "SYSTEM DASHBOARD") + "</h2>";
  html += "<table><tr><td>SSID</td><td class='val'>" + WiFi.SSID() + "</td></tr><tr><td>IP</td><td class='val'>" + WiFi.localIP().toString() + "</td></tr>";
  html += "<tr><td>ID</td><td class='val'>" + fullHostname + "</td></tr><tr><td>Ver</td><td class='val'>" + String(currentVersion, 1) + "</td></tr></table></div></body></html>";
  return html;
}

void update_progress(int cur, int total) {
  static int dotCount = 0;
  static unsigned long lastProgUpdate = 0;
  yield(); 
  if (millis() - lastProgUpdate > 500) {
    lastProgUpdate = millis();
    dotCount++;
    if (dotCount > 11) dotCount = 0; 
    lcd.setCursor(8, 3); 
    lcd.print("            "); 
    lcd.setCursor(8, 3);
    for (int i = 0; i < dotCount; i++) { lcd.print("."); }
  }
}

void checkGitHubUpdate() {
  if (WiFi.status() != WL_CONNECTED) return;
  
  WiFiClientSecure client; 
  client.setInsecure(); 
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  http.begin(client, versionURL);
  int httpCode = http.GET();
  
  if (httpCode == HTTP_CODE_OK) { 
    String payload = http.getString();
    int verKeyPos = payload.indexOf("\"version\":");
    int buildKeyPos = payload.indexOf("\"Build\":");
    
    if (verKeyPos != -1) {
      int firstQuote = payload.indexOf("\"", verKeyPos + 10); 
      int secondQuote = payload.indexOf("\"", firstQuote + 1);
      float newVersion = payload.substring(firstQuote + 1, secondQuote).toFloat();
   
      String buildDate = "N/A";
      if (buildKeyPos != -1) {
        int bFirstQuote = payload.indexOf("\"", buildKeyPos + 8);
        int bSecondQuote = payload.indexOf("\"", bFirstQuote + 1);
        buildDate = payload.substring(bFirstQuote + 1, bSecondQuote);
      }

      Serial.println("[OTA] Check: v." + String(currentVersion, 1) + " -> Last: v." + String(newVersion, 1));

      if (newVersion > currentVersion) {
        Serial.println("[OTA] >>> Found New Fw. <<<");
        lcd.clear();
        lcd.setCursor(0, 0); lcd.print("    ----OTA----");
        lcd.setCursor(0, 1); lcd.print("Ver.:" + String(newVersion, 1));
        lcd.setCursor(0, 2); lcd.print("Bld.:" + buildDate);
        lcd.setCursor(0, 3); lcd.print("Updating"); 

        httpUpdate.onProgress(update_progress);
        httpUpdate.rebootOnUpdate(true);
        t_httpUpdate_return ret = httpUpdate.update(client, firmwareURL);
      
        if (ret == HTTP_UPDATE_FAILED) {
          Serial.printf("[OTA] Update Error (%d): %s\n", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
          lcd.setCursor(0, 3); lcd.print("Update Failed!      ");
          delay(2000);
        }
      } else {
        Serial.println("[OTA] Firmware is up to date.");
      }
    }
  }
  http.end();
}

void startWiFiManager() {
  server.stop(); 
  delay(500); 

  lcd.clear(); 
  lcd.setCursor(0, 0); lcd.print("--- CHANGE WIFI ---");
  lcd.setCursor(0, 1); lcd.print("1.Connect WiFi Name");
  lcd.setCursor(0, 2); lcd.print(">> " + fullHostname); 

  for(int i=0; i<3; i++) {
    lcd.setCursor(0, 3); lcd.print("2.Open: 192.168.4.1"); delay(500);
    lcd.setCursor(8, 3); lcd.print("            "); delay(500);
  }
  lcd.setCursor(0, 3); lcd.print("2.Open: 192.168.4.1");
  
  WiFiManager wm; 
  wm.setConfigPortalTimeout(180); 
  if (!wm.startConfigPortal(fullHostname.c_str())) {
    delay(1000);
  }
  ESP.restart(); 
}

// --- [ ส่วนที่ 3: Setup & Loop ] ---

void setup() {
  Serial.begin(115200);
  delay(500);
    
  lcd.begin(); 
  lcd.clear();
  lcd.backlight(); 
  lcd.setCursor(0, 0); lcd.print("--- SYSTEM BOOT ---");
  lcd.setCursor(0, 1); lcd.print("    Hardware Init.");

  Serial.println("[SYS] Booting...");
  Serial.println("[SYS] Hardware Init.");
  
  pinMode(LedHeartBeat, OUTPUT); 
  pinMode(ledPin, OUTPUT); 
  pinMode(configButton, INPUT_PULLUP);

  macAddrStr = getEfuseMac();
  setupHostname(); // ตั้งชื่อ ESP32-XXXXXX

  WiFi.mode(WIFI_STA);
  WiFi.begin(); 
  
  int retry = 0;
  while (WiFi.SSID() == "" && retry < 10) { delay(100); retry++; }

  lcd.setCursor(0, 2); lcd.print("Connect to WiFi...");
  lcd.setCursor(0, 3);
  if (WiFi.SSID() != "") lcd.print("SSID: " + WiFi.SSID().substring(0, 14));
  else lcd.print("SSID: Not Found!");   
    WiFiManager wm;
    wm.setConfigPortalTimeout(60); 
    wm.setAPCallback([](WiFiManager *myWM) {
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("--- CONFIG MODE ---");
    lcd.setCursor(0, 1); lcd.print("1.Connect WiFi Name");
    lcd.setCursor(0, 2); lcd.print(">> " + fullHostname); 
    lcd.setCursor(0, 3); lcd.print("2.Open: 192.168.4.1");
  });

  if (wm.autoConnect(fullHostname.c_str())) {
    isOnline = true;
    ArduinoOTA.setHostname(fullHostname.c_str()); 
    ArduinoOTA.begin();

    // พิมพ์สถานะ Booting
    Serial.println("\n==============================");
    Serial.print("[SYS] Wifi Name : \""); Serial.print(WiFi.SSID()); Serial.println("\"");
    Serial.print("[SYS] IP Address: "); Serial.println(WiFi.localIP());
    Serial.print("[SYS] Hostname  : "); Serial.println(fullHostname);
    Serial.print("[SYS] Full MAC  : "); Serial.println(macAddrStr);
    Serial.println("==============================\n");

    server.on("/", HTTP_GET, []() { server.send(200, "text/html", getSharedHTML(false)); });
    server.on("/update", HTTP_GET, []() { server.send(200, "text/html", getSharedHTML(true)); });
    httpUpdater.setup(&server, "/update"); 
    server.begin();
    
    checkGitHubUpdate(); // เช็คครั้งแรกตอนเปิดเครื่อง
    lastUpdateCheck = millis();
  }
  lcd.clear();
}

void loop() {
  // 1. ตรวจสอบปุ่ม Config
  if (digitalRead(configButton) == LOW) {
    if (buttonPressTime == 0) buttonPressTime = millis();
    if (millis() - buttonPressTime > 1500) { startWiFiManager(); } 
  } else { buttonPressTime = 0; }

  // 2. จัดการ Network Services
  if (WiFi.status() == WL_CONNECTED) {
    if (!isOnline) isOnline = true;
    ArduinoOTA.handle(); 
    server.handleClient(); 
    
    // --- แยก Logic เช็ค Update ทุก 60 วินาทีออกมาที่นี่ ---
    if (millis() - lastUpdateCheck >= 60000) {
      checkGitHubUpdate();
      lastUpdateCheck = millis();
    }
  } else {
    isOnline = false;
  }

  // 3. Heartbeat LED
  if (millis() - blinkMillis >= 500) {
    blinkMillis = millis(); 
    blinkState = !blinkState; 
    digitalWrite(LedHeartBeat, blinkState); 
  }

  // 4. ระบบสลับหน้าจอ LCD ทุก 5 วินาที
  if (millis() - previousMillis >= 5000) {
    previousMillis = millis(); 
    displayPage = (displayPage >= 1) ? 0 : displayPage + 1;
    lcd.clear(); 
  }

  if (displayPage == 0) { 
    lcd.setCursor(0, 0); lcd.print("System Monitor V" + String(currentVersion, 1));
    lcd.setCursor(0, 1); lcd.print("      ----------      ");
    lcd.setCursor(0, 2); lcd.print("By  : Thanormsin.M");
    lcd.setCursor(0, 3); lcd.print("Tel.: 081-906 5291");
  } else if (displayPage == 1) { 
    lcd.setCursor(0, 0); lcd.print("Dev.  : "); lcd.print(fullHostname);
    if (isOnline) {
      lcd.setCursor(0, 1); lcd.print("WiFi  : "); lcd.print(WiFi.SSID().substring(0,12));
      lcd.setCursor(0, 2); lcd.print("IP    : "); lcd.print(WiFi.localIP().toString());
    } else {
      lcd.setCursor(0, 1); lcd.print("WiFi : Disconnected");
    }
    lcd.setCursor(0, 3); lcd.print("MC:"); lcd.print(macAddrStr); 
  }
}
