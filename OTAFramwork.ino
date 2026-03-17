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
float currentVersion = 0.9; 
const String versionURL  = "https://raw.githubusercontent.com/thanormsinm-byte/OTA/main/version.json";
const String firmwareURL = "https://raw.githubusercontent.com/thanormsinm-byte/OTA/main/OTA.bin";

const int SDA_PIN = 9;  
const int SCL_PIN = 10; 

WebServer server(80);
HTTPUpdateServer httpUpdater;
LiquidCrystal_I2C lcd(0x27, 20, 4); 

unsigned long previousMillis = 0;
unsigned long blinkMillis = 0;
unsigned long buttonPressTime = 0;
unsigned long lastUpdateCheck = 0;

int displayPage = 0; 
bool blinkState = false; 
bool isOnline = false; 
String fullHostname = "";
String macAddrStr = "";

const int LedHeartBeat = 8; 
const int ledPin = 6;        
const int configButton = 2; 

// --- [ ส่วนที่ 2: ฟังก์ชันเสริม ] ---

String getEfuseMac() {
  uint64_t mac = ESP.getEfuseMac(); char macBuf[18];
  sprintf(macBuf, "%02X:%02X:%02X:%02X:%02X:%02X", (uint8_t)(mac >> 0), (uint8_t)(mac >> 8), (uint8_t)(mac >> 16), (uint8_t)(mac >> 24), (uint8_t)(mac >> 32), (uint8_t)(mac >> 40));
  return String(macBuf);
}

void setupHostname() {
  uint64_t chipid = ESP.getEfuseMac(); 
  uint8_t b4 = (uint8_t)(chipid >> 32);
  uint8_t b5 = (uint8_t)(chipid >> 40);
  char hexStr[5];
  sprintf(hexStr, "%02X%02X", b4, b5); 
  fullHostname = "ESP32-" + String(hexStr);
}

// ฟังก์ชัน Progress Bar ที่คุณต้องการเพิ่ม (แก้ไข WDT และล้างจอเฉพาะจุด)
void update_progress(int cur, int total) {
  static int dotCount = 0;
  static unsigned long lastUpdate = 0;
  
  yield(); // คืนเวลาให้ระบบจัดการ WiFi/Network ป้องกันการหลุดขณะโหลด

  if (millis() - lastUpdate > 500) {
    lastUpdate = millis();
    dotCount++;
    if (dotCount > 11) dotCount = 0; 
    
    // ล้างเฉพาะพื้นที่หลังคำว่า Updating (ตำแหน่งที่ 8-20)
    lcd.setCursor(8, 3); 
    lcd.print("            "); 
    
    lcd.setCursor(8, 3);
    String dots = "";
    for (int i = 0; i < dotCount; i++) {
      dots += ".";
    }
    lcd.print(dots);
  }
}

void updateLCD() {
  lcd.clear();
  if (displayPage == 0) {  
    lcd.setCursor(0, 0); lcd.print("System Monitor V" + String(currentVersion, 1));
    lcd.setCursor(0, 1); lcd.print("      ----------      ");
    lcd.setCursor(0, 2); lcd.print("By  : Thanormsin.M");
    lcd.setCursor(0, 3); lcd.print("Tel.: 081-906 5291");
  } else if (displayPage == 1) {  
    lcd.setCursor(0, 0); lcd.print("Dev. : "); lcd.print(fullHostname);
    if (isOnline) {
      lcd.setCursor(0, 1); lcd.print("WiFi : "); lcd.print(WiFi.SSID().substring(0,12));
      lcd.setCursor(0, 2); lcd.print("IP   : "); lcd.print(WiFi.localIP().toString());
    } else {
      lcd.setCursor(0, 1); lcd.print("WiFi : Disconnected");
      lcd.setCursor(0, 2); lcd.print("IP   : 0.0.0.0");
    }
    lcd.setCursor(0, 3); lcd.print("MC:"); lcd.print(macAddrStr);  


    
  }
}

String getSharedHTML(bool isUpdatePage) {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>body { font-family: sans-serif; text-align: center; color: #333; background-color: #fff; margin: 0; padding-top: 50px; }";
  html += ".container { max-width: 400px; margin: auto; padding: 20px; } h2 { font-size: 16px; font-weight: bold; }";
  html += ".upload-area { border: 1.5px solid #e0e0e0; border-radius: 30px; padding: 10px 15px; cursor: pointer; display: inline-flex; margin-bottom: 10px; width: 50%; justify-content: center; background: #fafafa; font-size: 13px; }";
  html += ".btn-submit { background: #007bff; color: #fff; border: none; padding: 12px; width: 85%; border-radius: 25px; cursor: pointer; display: none; margin: 20px auto; font-size: 16px; font-weight: bold; transition: 0.3s; }";
  html += "table { width: 100%; margin-top: 30px; border-collapse: collapse; }";
  html += "td.lbl { padding: 10px; border-bottom: 1px solid #eee; text-align: left; font-size: 13px; color: #007bff; }";
  html += "td.val { padding: 10px; border-bottom: 1px solid #eee; text-align: right; font-size: 13px; font-weight: bold; color: #f39c12; }";
  html += "input[type='file'] { display: none; }</style></head><body>";
  html += "<div class='container'><h2>" + String(isUpdatePage ? "UPDATE FIRMWARE" : "SYSTEM DASHBOARD") + "</h2>";
  html += "<form method='POST' action='/update' enctype='multipart/form-data' id='upload_form'>";
  html += "<label for='file_input' class='upload-area'>📄 Select .bin File</label>";
  html += "<input type='file' name='update' id='file_input' accept='.bin' onchange='checkFile()'>";
  html += "<button type='button' class='btn-submit' id='submit_btn' onclick='startUpdate()'>Start Update Now</button></form>";
  html += "<table>";
  html += "<tr><td class='lbl'>SSID</td><td class='val'>" + WiFi.SSID() + "</td></tr>";
  html += "<tr><td class='lbl'>Dev.Name</td><td class='val'>" + fullHostname + "</td></tr>";
  html += "<tr><td class='lbl'>IP. Addr.</td><td class='val'>" + WiFi.localIP().toString() + "</td></tr>";
  html += "<tr><td class='lbl'>Ver.</td><td class='val'>" + String(currentVersion, 1) + "</td></tr>";
  html += "</table></div>";
  html += "<script>function checkFile(){var f=document.getElementById('file_input'),b=document.getElementById('submit_btn');if(f.value){b.style.display='block';}}";
  html += "function startUpdate(){var f=document.getElementById('file_input'),b=document.getElementById('submit_btn'),d=new FormData();d.append('update',f.files[0]);var x=new XMLHttpRequest();x.upload.addEventListener('progress',function(e){if(e.lengthComputable){var p=Math.round((e.loaded/e.total)*100);b.disabled=true;b.innerHTML='Uploading: '+p+'%';}});x.onload=function(){if(x.status===200){b.innerHTML='Update Success - Restarting...';setTimeout(function(){location.href='/';},5000);}else{b.disabled=false;b.innerHTML='Update Failed!';}};x.open('POST','/update');x.send(d);}</script></body></html>";
  return html;
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

      Serial.print("\n[OTA] Curr("); Serial.print(currentVersion, 1);
      Serial.print(") <--> Last("); Serial.print(newVersion, 1);
      Serial.println(")");

      if (newVersion > currentVersion) {
        Serial.println("[OTA] >>> Found New Fw. <<<");
        Serial.println("[OTA] Update in progress");
        lcd.clear();
        lcd.setCursor(0, 0); lcd.print("    ----OTA----");
        lcd.setCursor(0, 1); lcd.print("Ver.:" + String(newVersion, 1));
        lcd.setCursor(0, 2); lcd.print("Bld.:" + buildDate);
        lcd.setCursor(0, 3); lcd.print("Updating"); 

        httpUpdate.onProgress(update_progress); // เรียกใช้ฟังก์ชันที่ปรับปรุงใหม่
        httpUpdate.rebootOnUpdate(true);
        t_httpUpdate_return ret = httpUpdate.update(client, firmwareURL);
      
        if (ret == HTTP_UPDATE_FAILED) {
          Serial.printf("[OTA] Update Error (%d): %s\n", httpUpdate.getLastError(), httpUpdate.getLastErrorString().c_str());
          lcd.setCursor(0, 3); lcd.print("Update Failed!      ");
        }
      } else {
        Serial.println("[OTA] Firmware is up to date.");
      }
    }
  }
  http.end();
}

void startWiFiManager() {
  server.stop(); delay(500); 
  WiFi.disconnect(); // ตัดการเชื่อมต่อเดิมก่อน
  delay(500);
  
  lcd.clear(); 
  lcd.setCursor(0, 0); lcd.print("--- CHANGE WIFI ---");
  lcd.setCursor(0, 1); lcd.print("1.Connect WiFi Name");
  lcd.setCursor(0, 2); lcd.print(">> " + fullHostname); 

  for(int i=0; i<6; i++) {
    lcd.setCursor(0, 3); lcd.print("2.Open: 192.168.4.1"); delay(500);
    lcd.setCursor(8, 3); lcd.print("           "); delay(500);
  }
  lcd.setCursor(0, 3); lcd.print("2.Open: 192.168.4.1");

  WiFiManager wm; 
  wm.setConfigPortalTimeout(180); 
  if (!wm.startConfigPortal(fullHostname.c_str())) { delay(1000); }
  ESP.restart(); 
}

// --- [ ส่วนที่ 3: Setup & Loop ] ---

void setup() {
  Serial.begin(115200);
  delay(500);

  Wire.begin(SDA_PIN, SCL_PIN);
  Wire.setTimeOut(100); 
    
  lcd.begin(); 
  lcd.clear();
  lcd.backlight(); 
  lcd.setCursor(0, 0); lcd.print("--- SYSTEM BOOT ---");
  lcd.setCursor(0, 1); lcd.print("    Hardware Init.");

  pinMode(LedHeartBeat, OUTPUT); 
  pinMode(ledPin, OUTPUT); 
  pinMode(configButton, INPUT_PULLUP);

  macAddrStr = getEfuseMac();
  setupHostname();

  WiFi.mode(WIFI_STA);
  WiFi.begin(); 

  //กระพริบแจ้งให้ทราบ
  for(int i=0; i<3; i++) {
    lcd.setCursor(0, 3); lcd.print("    -> Ready <-"); delay(500);
    lcd.setCursor(0, 3); lcd.print("               "); delay(500);
  }
  
  WiFiManager wm;
  wm.setConfigPortalTimeout(60); 
  wm.setAPCallback([](WiFiManager *myWM) {
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("--- CONFIG MODE ---");
    lcd.setCursor(0, 1); lcd.print("1.Connect WiFi Name");
    lcd.setCursor(0, 2); lcd.print(">> " + fullHostname); 
    
    for(int i=0; i<6; i++) {
      lcd.setCursor(0, 3); lcd.print("2.Open: 192.168.4.1"); delay(500);
      lcd.setCursor(8, 3); lcd.print("           "); delay(500);
    }
    lcd.setCursor(0, 3); lcd.print("2.Open: 192.168.4.1");
  });

  //เช็ค Update Fw. ครั้งแรก
  if (wm.autoConnect(fullHostname.c_str())) {
    isOnline = true;
    ArduinoOTA.setHostname(fullHostname.c_str()); 
    ArduinoOTA.begin();

    server.on("/", HTTP_GET, []() { server.send(200, "text/html", getSharedHTML(false)); });
    server.on("/update", HTTP_GET, []() { server.send(200, "text/html", getSharedHTML(true)); });
    httpUpdater.setup(&server, "/update"); 
    server.begin();
    
    checkGitHubUpdate();
    lastUpdateCheck = millis();
  } else {
    isOnline = false;
  }

  lcd.clear(); 
  displayPage = 0;
  updateLCD();
  previousMillis = millis(); 
}

void loop() {
  yield(); 
  
  if (digitalRead(configButton) == LOW) {
    if (buttonPressTime == 0) buttonPressTime = millis();
    if (millis() - buttonPressTime > 1500) { startWiFiManager(); } 
  } else { buttonPressTime = 0; }

  if (WiFi.status() == WL_CONNECTED) {
    if (!isOnline) isOnline = true;
    ArduinoOTA.handle(); 
    server.handleClient(); 
    if (millis() - lastUpdateCheck >= 60000) {
      checkGitHubUpdate();
      lastUpdateCheck = millis();
    }
  } else {
    isOnline = false;
  }

  if (millis() - blinkMillis >= 500) {
    blinkMillis = millis(); 
    blinkState = !blinkState; 
    digitalWrite(LedHeartBeat, blinkState); 
  }

  if (millis() - previousMillis >= 5000) {
    previousMillis = millis(); 
    displayPage = (displayPage >= 1) ? 0 : 1;
    lcd.clear(); 
    updateLCD();
  }
}
