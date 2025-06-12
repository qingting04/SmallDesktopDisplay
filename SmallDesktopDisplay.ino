#define Version  "SDD V2.0"

// 内存优化编译选项
#pragma GCC optimize("Os")

#include "ArduinoJson.h"
#include <TimeLib.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>
#include <ESP8266mDNS.h>
#include <TFT_eSPI.h> 
#include <SPI.h>
#include <TJpg_Decoder.h>
#include <LittleFS.h>  // 使用LittleFS替代SPIFFS节省内存
#include "qr.h"
#include "number.h"
#include "weathernum.h"

// Blinker Library
#define BLINKER_WIFI
#define BLINKER_ALIGENIE_SENSOR
#include <Blinker.h>

// 配置标志 - 保留全部功能
#define WM_EN   1
#define WebSever_EN  1
#define DHT_EN  1
#define imgAst_EN 1

#if WM_EN
#include <WiFiManager.h>
WiFiManager wm;
#endif

#if DHT_EN
#include "DHT.h"
#define DHTPIN  12
#define DHTTYPE DHT22
DHT dht(DHTPIN,DHTTYPE);
#endif

// 图片库
#include "font/ZdyLwFont_20.h"
#include "img/misaka.h"
#include "img/temperature.h"
#include "img/humidity.h"

#if imgAst_EN
#include "img/pangzi/i0.h"
#include "img/pangzi/i1.h"
#include "img/pangzi/i2.h"
#include "img/pangzi/i3.h"
#include "img/pangzi/i4.h"
#include "img/pangzi/i5.h"
#include "img/pangzi/i6.h"
#include "img/pangzi/i7.h"
#include "img/pangzi/i8.h"
#include "img/pangzi/i9.h"

int Anim = 0;
int AprevTime = 0;
#endif

// 配置结构体
struct DeviceConfig {
  String blinker_auth;
  String city_code;
  int lcd_brightness;
  int weather_update_time;
  int lcd_rotation;
  int dht_enabled;
  int show_animation;
};

DeviceConfig config;

// 显示设置
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite clk = TFT_eSprite(&tft);
#define LCD_BL_PIN 5
uint16_t bgColor = 0x0000;

// 状态标志
uint8_t Wifi_en = 1;
uint8_t UpdateWeater_en = 0;
int prevTime = 0;

time_t prevDisplay = 0;
unsigned long weaterTime = 0;
String SMOD = "";

Number dig;
WeatherNum wrat;
uint32_t targetTime = 0;

int tempnum = 0;
int huminum = 0;
int tempcol =0xffff;
int humicol =0xffff;

ESP8266WebServer server(80);

static const char ntpServerName[] = "ntp6.aliyun.com";
const int timeZone = 8;

WiFiUDP Udp;
WiFiClient wificlient;
unsigned int localPort = 8000;

// 函数声明
time_t getNtpTime();
void digitalClockDisplay(int reflash_en);
void sendNTPpacket(IPAddress &address);
void LCD_reflash(int en);
void Web_Sever_Init();
void Web_Sever();
bool loadConfig();
bool saveConfig();
void resetConfig();

// 配置文件管理 - 使用LittleFS
bool loadConfig() {
  if (!LittleFS.begin()) {
    Serial.println(F("LittleFS Mount Failed"));
    return false;
  }
  
  if (!LittleFS.exists("/config.json")) {
    Serial.println(F("Config file not found, using defaults"));
    config.blinker_auth = "";
    config.city_code = "101250101";
    config.lcd_brightness = 50;
    config.weather_update_time = 10;
    config.lcd_rotation = 0;
    config.dht_enabled = 0;
    config.show_animation = 1;
    return saveConfig();
  }
  
  File configFile = LittleFS.open("/config.json", "r");
  if (!configFile) {
    Serial.println(F("Failed to open config file"));
    return false;
  }
  
  String content = configFile.readString();
  configFile.close();
  
  DynamicJsonDocument doc(1024);
  deserializeJson(doc, content);
  
  config.blinker_auth = doc["blinker_auth"].as<String>();
  config.city_code = doc["city_code"].as<String>();
  config.lcd_brightness = doc["lcd_brightness"] | 50;
  config.weather_update_time = doc["weather_update_time"] | 10;
  config.lcd_rotation = doc["lcd_rotation"] | 0;
  config.dht_enabled = doc["dht_enabled"] | 0;
  config.show_animation = doc["show_animation"] | 1;
  
  Serial.println(F("Config loaded successfully"));
  return true;
}

bool saveConfig() {
  DynamicJsonDocument doc(1024);
  
  doc["blinker_auth"] = config.blinker_auth;
  doc["city_code"] = config.city_code;
  doc["lcd_brightness"] = config.lcd_brightness;
  doc["weather_update_time"] = config.weather_update_time;
  doc["lcd_rotation"] = config.lcd_rotation;
  doc["dht_enabled"] = config.dht_enabled;
  doc["show_animation"] = config.show_animation;
  
  File configFile = LittleFS.open("/config.json", "w");
  if (!configFile) {
    Serial.println(F("Failed to open config file for writing"));
    return false;
  }
  
  serializeJson(doc, configFile);
  configFile.close();
  
  Serial.println(F("Config saved successfully"));
  return true;
}

void resetConfig() {
  if (LittleFS.exists("/config.json")) {
    LittleFS.remove("/config.json");
    Serial.println(F("Config file deleted"));
  }
}

// Blinker回调函数
void aligenieQuery(int32_t queryCode) {
    BLINKER_LOG("AliGenie Query codes: ", queryCode);
    
    float temp = 20.0;
    float humi = 50.0;
    
#if DHT_EN
    if (config.dht_enabled != 0) {
        temp = dht.readTemperature();
        humi = dht.readHumidity();
        if (isnan(temp)) temp = 20.0;
        if (isnan(humi)) humi = 50.0;
    }
#endif

    switch (queryCode) {
        case BLINKER_CMD_QUERY_ALL_NUMBER :
            BLINKER_LOG("AliGenie Query All");
            BlinkerAliGenie.temp(temp);
            BlinkerAliGenie.humi(humi);
            BlinkerAliGenie.pm25(50);
            BlinkerAliGenie.print();
            break;
        default :
            BlinkerAliGenie.temp(temp);
            BlinkerAliGenie.humi(humi);
            BlinkerAliGenie.pm25(50);
            BlinkerAliGenie.print();
            break;
    }
}

void dataRead(const String & data) {
    BLINKER_LOG("Blinker readString: ", data);
    Blinker.vibrate();
    uint32_t BlinkerTime = millis();
    Blinker.print("millis", BlinkerTime);
}

bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  if ( y >= tft.height() ) return 0;
  tft.pushImage(x, y, w, h, bitmap);
  return 1;
}

byte loadNum = 6;
void loading(byte delayTime) {
  clk.setColorDepth(8);
  clk.createSprite(200, 100);
  clk.fillSprite(0x0000);
  clk.drawRoundRect(0,0,200,16,8,0xFFFF);
  clk.fillRoundRect(3,3,loadNum,10,5,0xFFFF);
  clk.setTextDatum(CC_DATUM);
  clk.setTextColor(TFT_GREEN, 0x0000); 
  clk.drawString(F("Connecting to WiFi......"),100,40,2);
  clk.setTextColor(TFT_WHITE, 0x0000); 
  clk.drawRightString(Version,180,60,2);
  clk.pushSprite(20,120);
  clk.deleteSprite();
  loadNum += 1;
  delay(delayTime);
}

void humidityWin() {
  clk.setColorDepth(8);
  huminum = huminum/2;
  clk.createSprite(52, 6);
  clk.fillSprite(0x0000);
  clk.drawRoundRect(0,0,52,6,3,0xFFFF);
  clk.fillRoundRect(1,1,huminum,4,2,humicol);
  clk.pushSprite(45,222);
  clk.deleteSprite();
}

void tempWin() {
  clk.setColorDepth(8);
  clk.createSprite(52, 6);
  clk.fillSprite(0x0000);
  clk.drawRoundRect(0,0,52,6,3,0xFFFF);
  clk.fillRoundRect(1,1,tempnum,4,2,tempcol);
  clk.pushSprite(45,192);
  clk.deleteSprite();
}

#if DHT_EN
void IndoorTem() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  
  clk.setColorDepth(8);
  clk.loadFont(ZdyLwFont_20);
  
  clk.createSprite(58, 30);
  clk.fillSprite(bgColor);
  clk.setTextDatum(CC_DATUM);
  clk.setTextColor(TFT_WHITE, bgColor);
  clk.drawString(F("内温"),29,16);
  clk.pushSprite(172,150);
  clk.deleteSprite();
  
  clk.createSprite(60, 24); 
  clk.fillSprite(bgColor);
  clk.setTextDatum(CC_DATUM);
  clk.setTextColor(TFT_WHITE, bgColor); 
  clk.drawFloat(t,1,20,13);
  clk.drawString(F("℃"),50,13);
  clk.pushSprite(170,184);
  clk.deleteSprite();
  
  clk.createSprite(60, 24); 
  clk.fillSprite(bgColor);
  clk.setTextDatum(CC_DATUM);
  clk.setTextColor(TFT_WHITE, bgColor); 
  clk.drawFloat(h,1,20,13);
  clk.drawString(F("%"),50,13);
  clk.pushSprite(170,214);
  clk.deleteSprite();
}
#endif

#if !WM_EN
void SmartConfig(void) {
  WiFi.mode(WIFI_STA);
  tft.pushImage(0, 0, 240, 240, qr);
  Serial.println(F("\r\nWait for Smartconfig..."));
  WiFi.beginSmartConfig();
  while (1) {
    Serial.print(F("."));
    delay(100);
    if (WiFi.smartConfigDone()) {
    Serial.println(F("SmartConfig Success"));
    Serial.printf("SSID:%s\r\n", WiFi.SSID().c_str());
    Serial.printf("PSW:%s\r\n", WiFi.psk().c_str());
    break;
    }
  }
  loadNum = 194;
}
#endif

// Web服务器功能 - 使用F()宏优化内存
#if WebSever_EN
void handleconfig() {
  String msg = "";
  bool configChanged = false;

  if (server.hasArg("web_ccode") || server.hasArg("web_bl") || 
      server.hasArg("web_upwe_t") || server.hasArg("web_DHT11_en") || 
      server.hasArg("web_set_rotation") || server.hasArg("web_animation") ||
      server.hasArg("web_blinker_auth")) {
    
    // 获取表单值
    String new_blinker_auth = server.arg("web_blinker_auth");
    String new_city_code = server.arg("web_ccode");
    int new_brightness = server.arg("web_bl").toInt();
    int new_update_time = server.arg("web_upwe_t").toInt();
    int new_rotation = server.arg("web_set_rotation").toInt();
    int new_dht_enabled = server.arg("web_DHT11_en").toInt();
    int new_animation = server.arg("web_animation").toInt();
    
    // 验证并更新Blinker认证
    if (new_blinker_auth.length() > 0 && new_blinker_auth != config.blinker_auth) {
      config.blinker_auth = new_blinker_auth;
      configChanged = true;
      msg += F("Blinker密钥已更新<br>");
      Serial.println("Blinker Auth: " + config.blinker_auth);
    }
    
    // 验证并更新城市代码
    int cc = new_city_code.toInt();
    if (cc >= 101000000 && cc <= 102000000) {
      config.city_code = new_city_code;
      configChanged = true;
      msg += F("城市代码已更新<br>");
      Serial.println("City Code: " + config.city_code);
    }
    
    // 验证并更新亮度
    if (new_brightness > 0 && new_brightness <= 100) {
      config.lcd_brightness = new_brightness;
      analogWrite(LCD_BL_PIN, 1023 - (config.lcd_brightness * 10));
      configChanged = true;
      msg += F("亮度已调整<br>");
      Serial.println("Brightness: " + String(config.lcd_brightness));
    }
    
    // 验证并更新天气更新时间
    if (new_update_time > 0 && new_update_time <= 60) {
      config.weather_update_time = new_update_time;
      configChanged = true;
      msg += F("天气更新时间已设置<br>");
      Serial.println("Update Time: " + String(config.weather_update_time));
    }

    // 更新动画设置
    if (new_animation != config.show_animation) {
      config.show_animation = new_animation;
      configChanged = true;
      msg += F("显示模式已切换<br>");
      tft.fillScreen(0x0000);
      LCD_reflash(1);
      UpdateWeater_en = 1;
      TJpgDec.drawJpg(15,183,temperature, sizeof(temperature));
      TJpgDec.drawJpg(15,213,humidity, sizeof(humidity));
      Serial.println("Animation: " + String(config.show_animation ? "动画" : "室内温度"));
    }

    // 更新DHT设置
    if (new_dht_enabled != config.dht_enabled) {
      config.dht_enabled = new_dht_enabled;
      configChanged = true;
      msg += F("DHT传感器设置已更新<br>");
      tft.fillScreen(0x0000);
      LCD_reflash(1);
      UpdateWeater_en = 1;
      TJpgDec.drawJpg(15,183,temperature, sizeof(temperature));
      TJpgDec.drawJpg(15,213,humidity, sizeof(humidity));
    }

    // 更新旋转设置
    if (new_rotation != config.lcd_rotation) {
      config.lcd_rotation = new_rotation;
      configChanged = true;
      msg += F("屏幕方向已调整<br>");
      tft.setRotation(config.lcd_rotation);
      tft.fillScreen(0x0000);
      LCD_reflash(1);
      UpdateWeater_en = 1;
      TJpgDec.drawJpg(15,183,temperature, sizeof(temperature));
      TJpgDec.drawJpg(15,213,humidity, sizeof(humidity));
    }
    
    // 如果配置有更改则保存
    if (configChanged) {
      if (saveConfig()) {
        msg += F("配置已保存！<br>");
        
        // 如果认证密钥有更改则重启Blinker
        if (config.blinker_auth.length() > 0) {
          msg += F("正在重新初始化Blinker...<br>");
        }
      } else {
        msg += F("配置保存失败！<br>");
      }
    }
  }

  // 分段发送Web界面以节省RAM
  server.setContentLength(CONTENT_LENGTH_UNKNOWN);
  server.send(200, F("text/html"), "");
  
  server.sendContent(F("<!DOCTYPE html><html><head><meta charset='UTF-8'>"));
  server.sendContent(F("<meta name='viewport' content='width=device-width,initial-scale=1.0'>"));
  server.sendContent(F("<title>小型桌面显示器配置</title>"));
  server.sendContent(F("<style>*{box-sizing:border-box}"));
  server.sendContent(F("body{background:linear-gradient(135deg,#667eea 0%,#764ba2 100%);color:#fff;font-family:'Segoe UI',Arial,sans-serif;margin:0;padding:20px;min-height:100vh}"));
  server.sendContent(F(".container{max-width:500px;margin:0 auto;background:rgba(255,255,255,0.1);padding:30px;border-radius:15px;backdrop-filter:blur(10px);box-shadow:0 8px 32px rgba(0,0,0,0.3)}"));
  server.sendContent(F("h2{text-align:center;margin-bottom:30px;font-size:24px}"));
  server.sendContent(F(".group{margin:20px 0;padding:15px;background:rgba(255,255,255,0.05);border-radius:10px;border:1px solid rgba(255,255,255,0.1)}"));
  server.sendContent(F("label{display:block;margin-bottom:8px;font-weight:600}"));
  server.sendContent(F("input[type='text']{width:100%;padding:12px;border:none;border-radius:8px;background:rgba(255,255,255,0.9);color:#333;font-size:14px}"));
  server.sendContent(F("input[type='radio']{margin:8px 8px 8px 0;transform:scale(1.2)}"));
  server.sendContent(F(".radio-group{display:flex;flex-wrap:wrap;gap:15px;margin-top:10px}"));
  server.sendContent(F(".radio-option{display:flex;align-items:center}"));
  server.sendContent(F("input[type='submit']{width:100%;padding:15px;background:linear-gradient(45deg,#fff,#f0f0f0);color:#333;border:none;border-radius:10px;font-size:16px;font-weight:bold;cursor:pointer;transition:all 0.3s ease}"));
  server.sendContent(F("input[type='submit']:hover{transform:translateY(-2px);box-shadow:0 5px 15px rgba(0,0,0,0.2)}"));
  server.sendContent(F(".message{background:rgba(76,175,80,0.2);border:1px solid rgba(76,175,80,0.5);padding:10px;border-radius:5px;margin:10px 0}"));
  server.sendContent(F(".status{text-align:center;margin-top:20px;font-size:12px;opacity:0.8}"));
  server.sendContent(F("</style></head><body>"));
  
  server.sendContent(F("<div class='container'>"));
  server.sendContent(F("<h2>🖥️ 小型桌面显示器配置</h2>"));
  
  if (msg.length() > 0) {
    server.sendContent(F("<div class='message'>"));
    server.sendContent(msg);
    server.sendContent(F("</div>"));
  }
  
  server.sendContent(F("<form action='/' method='POST'>"));
  
  server.sendContent(F("<div class='group'>"));
  server.sendContent(F("<label>🔑 Blinker密钥 (天猫精灵):</label>"));
  server.sendContent(F("<input type='text' name='web_blinker_auth' placeholder='从Blinker APP获取设备密钥' value='"));
  server.sendContent(config.blinker_auth);
  server.sendContent(F("'></div>"));
  
  server.sendContent(F("<div class='group'>"));
  server.sendContent(F("<label>🏙️ 城市代码:</label>"));
  server.sendContent(F("<input type='text' name='web_ccode' placeholder='例如: 101250101 (长沙)' value='"));
  server.sendContent(config.city_code);
  server.sendContent(F("'></div>"));
  
  server.sendContent(F("<div class='group'>"));
  server.sendContent(F("<label>💡 屏幕亮度 (1-100):</label>"));
  server.sendContent(F("<input type='text' name='web_bl' placeholder='50' value='"));
  server.sendContent(String(config.lcd_brightness));
  server.sendContent(F("'></div>"));
  
  server.sendContent(F("<div class='group'>"));
  server.sendContent(F("<label>⏰ 天气更新间隔 (分钟):</label>"));
  server.sendContent(F("<input type='text' name='web_upwe_t' placeholder='10' value='"));
  server.sendContent(String(config.weather_update_time));
  server.sendContent(F("'></div>"));
  
  server.sendContent(F("<div class='group'>"));
  server.sendContent(F("<label>🎭 右下角显示:</label>"));
  server.sendContent(F("<div class='radio-group'>"));
  server.sendContent(F("<div class='radio-option'><input type='radio' name='web_animation' value='1'"));
  if (config.show_animation) server.sendContent(F(" checked"));
  server.sendContent(F("> 🚀 动画</div>"));
  server.sendContent(F("<div class='radio-option'><input type='radio' name='web_animation' value='0'"));
  if (!config.show_animation) server.sendContent(F(" checked"));
  server.sendContent(F("> 🌡️ 室内温度</div>"));
  server.sendContent(F("</div></div>"));
  
  #if DHT_EN
  server.sendContent(F("<div class='group'>"));
  server.sendContent(F("<label>🌡️ DHT11传感器:</label>"));
  server.sendContent(F("<div class='radio-group'>"));
  server.sendContent(F("<div class='radio-option'><input type='radio' name='web_DHT11_en' value='0'"));
  if (!config.dht_enabled) server.sendContent(F(" checked"));
  server.sendContent(F("> 禁用</div>"));
  server.sendContent(F("<div class='radio-option'><input type='radio' name='web_DHT11_en' value='1'"));
  if (config.dht_enabled) server.sendContent(F(" checked"));
  server.sendContent(F("> 启用</div>"));
  server.sendContent(F("</div></div>"));
  #endif
  
  server.sendContent(F("<div class='group'>"));
  server.sendContent(F("<label>🔄 屏幕方向:</label>"));
  server.sendContent(F("<div class='radio-group'>"));
  server.sendContent(F("<div class='radio-option'><input type='radio' name='web_set_rotation' value='0'"));
  if (config.lcd_rotation == 0) server.sendContent(F(" checked"));
  server.sendContent(F("> USB朝下</div>"));
  server.sendContent(F("<div class='radio-option'><input type='radio' name='web_set_rotation' value='1'"));
  if (config.lcd_rotation == 1) server.sendContent(F(" checked"));
  server.sendContent(F("> USB朝右</div>"));
  server.sendContent(F("<div class='radio-option'><input type='radio' name='web_set_rotation' value='2'"));
  if (config.lcd_rotation == 2) server.sendContent(F(" checked"));
  server.sendContent(F("> USB朝上</div>"));
  server.sendContent(F("<div class='radio-option'><input type='radio' name='web_set_rotation' value='3'"));
  if (config.lcd_rotation == 3) server.sendContent(F(" checked"));
  server.sendContent(F("> USB朝左</div>"));
  server.sendContent(F("</div></div>"));
  
  server.sendContent(F("<input type='submit' value='💾 保存设置'>"));
  server.sendContent(F("</form>"));
  
  server.sendContent(F("<div class='status'>"));
  server.sendContent(F("💬 支持天猫精灵语音控制 | IP: "));
  server.sendContent(WiFi.localIP().toString());
  server.sendContent(F("</div>"));
  
  server.sendContent(F("</div></body></html>"));
  server.sendContent("");
}

void handleReset() {
  resetConfig();
  String content = F("<html><head><meta charset='UTF-8'><meta http-equiv='refresh' content='3;url=/'></head>");
  content += F("<body style='background:#667eea;color:#fff;text-align:center;font-family:Arial;padding:50px;'>");
  content += F("<h2>配置已重置</h2><p>3秒后自动跳转...</p></body></html>");
  server.send(200, "text/html", content);
  delay(100);
  ESP.restart();
}

void handleNotFound() {
  String message = F("File Not Found\n\nURI: ");
  message += server.uri();
  message += F("\nMethod: ");
  message += (server.method() == HTTP_GET ? F("GET") : F("POST"));
  message += F("\nArguments: ");
  message += String(server.args());
  message += F("\n");
  for (uint8_t i = 0; i < server.args(); i++) {
    message += F(" ");
    message += server.argName(i);
    message += F(": ");
    message += server.arg(i);
    message += F("\n");
  }
  server.send(404, "text/plain", message);
}

void Web_Sever_Init() {
  uint32_t counttime = 0;
  Serial.println(F("mDNS responder building..."));
  counttime = millis();
  while (!MDNS.begin("sdd")) {
    if(millis() - counttime > 30000) ESP.restart();
  }

  Serial.println(F("mDNS responder started"));
  server.on("/", handleconfig);
  server.on("/reset", handleReset);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println(F("HTTP服务器已开启"));
  Serial.println(F("配置页面: http://sdd.local"));
  Serial.println(F("重置配置: http://sdd.local/reset"));
  Serial.print(F("本地IP： "));
  Serial.println(WiFi.localIP());
  MDNS.addService("http", "tcp", 80);
}

void Web_Sever() {
  MDNS.update();
  server.handleClient();
}

void Web_sever_Win() {
  clk.setColorDepth(8);
  clk.createSprite(200, 80);
  clk.fillSprite(0x0000);
  clk.setTextDatum(CC_DATUM);
  clk.setTextColor(TFT_GREEN, 0x0000); 
  clk.drawString(F("配置页面已开启"),100,15,2);
  clk.setTextColor(TFT_WHITE, 0x0000); 
  clk.drawString(F("http://sdd.local"),100,35,2);
  clk.drawString("或 " + WiFi.localIP().toString(),100,55,2);
  clk.setTextColor(TFT_YELLOW, 0x0000);
  clk.drawString(F("支持天猫精灵控制"),100,70,2);
  clk.pushSprite(20,30);
  clk.deleteSprite();
}
#endif

#if WM_EN
void Web_win() {
  clk.setColorDepth(8);
  clk.createSprite(200, 60);
  clk.fillSprite(0x0000);
  clk.setTextDatum(CC_DATUM);
  clk.setTextColor(TFT_GREEN, 0x0000); 
  clk.drawString(F("WiFi配网中..."),100,15,2);
  clk.drawString(F("SSID: SmallDisplay"),100,35,2);
  clk.pushSprite(20,50);
  clk.deleteSprite();
}

void Webconfig() {
  WiFi.mode(WIFI_STA);
  delay(3000);
  wm.resetSettings();
  
  // WiFiManager配置
  WiFiManagerParameter custom_blinker("blinker_auth","Blinker密钥","",32);
  WiFiManagerParameter custom_cc("CityCode","城市代码","101250101",9);
  WiFiManagerParameter custom_bl("LCDBL","屏幕亮度(1-100)","50",3);
  WiFiManagerParameter custom_weatertime("WeaterUpdateTime","更新间隔(分钟)","10",3);
  WiFiManagerParameter p_lineBreak("<p></p>");

  wm.addParameter(&p_lineBreak);
  wm.addParameter(&custom_blinker);
  wm.addParameter(&p_lineBreak);
  wm.addParameter(&custom_cc);
  wm.addParameter(&p_lineBreak);
  wm.addParameter(&custom_bl);
  wm.addParameter(&p_lineBreak);
  wm.addParameter(&custom_weatertime);
  wm.setSaveParamsCallback(saveParamCallback);
  
  std::vector<const char *> menu = {"wifi","restart"};
  wm.setMenu(menu);
  wm.setClass("invert");
  wm.setMinimumSignalQuality(20);

  bool res;
  res = wm.autoConnect("SmallDisplay");
  while(!res);
}

String getParam(String name) {
  String value;
  if(wm.server->hasArg(name)) {
    value = wm.server->arg(name);
  }
  return value;
}

void saveParamCallback() {
  Serial.println(F("[CALLBACK] saveParamCallback fired"));
  
  // 从WiFiManager获取参数
  config.blinker_auth = getParam("blinker_auth");
  config.city_code = getParam("CityCode");
  config.lcd_brightness = getParam("LCDBL").toInt();
  config.weather_update_time = getParam("WeaterUpdateTime").toInt();

  // 验证城市代码
  int cc = config.city_code.toInt();
  if (cc < 101000000 || cc > 102000000) {
    config.city_code = "101250101";
  }
  
  // 验证亮度
  if (config.lcd_brightness < 1 || config.lcd_brightness > 100) {
    config.lcd_brightness = 50;
  }
  
  // 验证更新时间
  if (config.weather_update_time < 1 || config.weather_update_time > 60) {
    config.weather_update_time = 10;
  }
  
  // 保存配置
  saveConfig();
  
  // 立即应用设置
  analogWrite(LCD_BL_PIN, 1023 - (config.lcd_brightness * 10));
  
  Serial.println(F("Configuration saved:"));
  Serial.println("Blinker Auth: " + config.blinker_auth);
  Serial.println("City Code: " + config.city_code);
  Serial.println("Brightness: " + String(config.lcd_brightness));
  Serial.println("Update Time: " + String(config.weather_update_time) + " minutes");
}
#endif

void setup() {
  Serial.begin(115200);
  
  // 初始化文件系统
  if (!LittleFS.begin()) {
    Serial.println(F("LittleFS Mount Failed"));
    Serial.println(F("Formatting LittleFS..."));
    LittleFS.format();
    LittleFS.begin();
  }
  
  // 加载配置
  loadConfig();
  
 #if DHT_EN
  dht.begin();
 #endif
 
  // 应用加载的配置
  pinMode(LCD_BL_PIN, OUTPUT);
  analogWrite(LCD_BL_PIN, 1023 - (config.lcd_brightness * 10));

  tft.begin();
  tft.invertDisplay(1);
  tft.setRotation(config.lcd_rotation);
  tft.fillScreen(0x0000);
  tft.setTextColor(TFT_BLACK, bgColor);

  targetTime = millis() + 1000;
  
  TJpgDec.setJpgScale(1);
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(tft_output);

  // 尝试连接到保存的WiFi或启动配置门户
  WiFi.begin();
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    loading(30);
    attempts++;
    if(loadNum>=194) {
      #if WM_EN
      Web_win();
      Webconfig();
      #endif
      #if !WM_EN
      SmartConfig();
      #endif   
      break;
    }
  }
  
  delay(10); 
  while(loadNum < 194) {
    loading(1);
  }

  if(WiFi.status() == WL_CONNECTED) {
    Serial.println(F("WiFi connected successfully"));
    
    // 如果有认证密钥则初始化Blinker
    if (config.blinker_auth.length() > 0) {
      char auth[33];
      char ssid_c[33];
      char pswd_c[65];
      
      config.blinker_auth.toCharArray(auth, 33);
      WiFi.SSID().toCharArray(ssid_c, 33);
      WiFi.psk().toCharArray(pswd_c, 65);
      
      Blinker.begin(auth, ssid_c, pswd_c);
      Blinker.attachData(dataRead);
      BlinkerAliGenie.attachQuery(aligenieQuery);
      Serial.println(F("Blinker initialized with AliGenie support"));
      Serial.println("Auth: " + config.blinker_auth);
    } else {
      Serial.println(F("Blinker auth key not configured"));
    }
    
    #if WebSever_EN
    Web_Sever_Init();
    Web_sever_Win();
    delay(5000);
    #endif
  }

  Serial.println(F("启动UDP"));
  Udp.begin(localPort);
  Serial.println(F("等待同步..."));
  setSyncProvider(getNtpTime);
  setSyncInterval(300);

  TJpgDec.setJpgScale(1);
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(tft_output);
  
  // 使用配置的城市代码或自动获取
  if (config.city_code.length() > 0) {
    int cc = config.city_code.toInt();
    if (cc >= 101000000 && cc <= 102000000) {
      // 有效的城市代码
    } else {
      getCityCode();
    }
  } else {
    getCityCode();
  }
    
  tft.fillScreen(TFT_BLACK);
  TJpgDec.drawJpg(15,183,temperature, sizeof(temperature));
  TJpgDec.drawJpg(15,213,humidity, sizeof(humidity));

  getCityWeater();
#if DHT_EN
  if(config.dht_enabled != 0 && config.show_animation == 0)
    IndoorTem();
#endif
}

void loop() {
  // 如果初始化了Blinker则运行
  if (config.blinker_auth.length() > 0) {
    Blinker.run();
  }
  
  #if WebSever_EN
  Web_Sever();
  #endif
  LCD_reflash(0);
}

void LCD_reflash(int en) {
  if (now() != prevDisplay || en == 1) {
    prevDisplay = now();
    digitalClockDisplay(en);
    prevTime=0;  
  }
  
  if(second()%2 ==0&& prevTime == 0 || en == 1) {
#if DHT_EN
    if(config.dht_enabled != 0 && config.show_animation == 0)
      IndoorTem();
#endif
    scrollBanner();
  }
  
#if imgAst_EN
  if(config.show_animation == 1)
    imgAnim();
#endif

  if(millis() - weaterTime > (60000 * config.weather_update_time) || en == 1 || UpdateWeater_en != 0) {
    if(Wifi_en == 0) {
      WiFi.forceSleepWake();
      Serial.println(F("WIFI恢复......"));
      Wifi_en = 1;
    }

    if(WiFi.status() == WL_CONNECTED) {
      getCityWeater();
      if(UpdateWeater_en != 0) UpdateWeater_en = 0;
      weaterTime = millis();
      getNtpTime();
      #if !WebSever_EN
      WiFi.forceSleepBegin();
      Serial.println(F("WIFI休眠......"));
      Wifi_en = 0;
      #endif
    }
  }
}

// 天气功能
void getCityCode(){
 String URL = "http://wgeo.weather.com.cn/ip/?_="+String(now());
  HTTPClient httpClient;
  httpClient.begin(wificlient, URL);
  httpClient.setUserAgent(F("Mozilla/5.0 (iPhone; CPU iPhone OS 11_0 like Mac OS X) AppleWebKit/604.1.38 (KHTML, like Gecko) Version/11.0 Mobile/15A372 Safari/604.1"));
  httpClient.addHeader(F("Referer"), F("http://www.weather.com.cn/"));
  
  int httpCode = httpClient.GET();
  Serial.print(F("Send GET request to URL: "));
  Serial.println(URL);
  
  if (httpCode == HTTP_CODE_OK) {
    String str = httpClient.getString();
    int aa = str.indexOf("id=");
    if(aa>-1) {
       config.city_code = str.substring(aa+4,aa+4+9);
       Serial.println("Auto detected city: " + config.city_code);
       saveConfig();
       getCityWeater();
    } else {
      Serial.println(F("获取城市代码失败"));  
    }
  } else {
    Serial.println(F("请求城市代码错误："));
    Serial.println(httpCode);
  }
  httpClient.end();
}

void getCityWeater(){
 String URL = "http://d1.weather.com.cn/weather_index/" + config.city_code + ".html?_="+String(now());
  HTTPClient httpClient;
  httpClient.begin(wificlient, URL);
  httpClient.setUserAgent(F("Mozilla/5.0 (iPhone; CPU iPhone OS 11_0 like Mac OS X) AppleWebKit/604.1.38 (KHTML, like Gecko) Version/11.0 Mobile/15A372 Safari/604.1"));
  httpClient.addHeader(F("Referer"), F("http://www.weather.com.cn/"));
  
  int httpCode = httpClient.GET();
  Serial.println(F("正在获取天气数据"));
  Serial.println(URL);
  
  if (httpCode == HTTP_CODE_OK) {
    String str = httpClient.getString();
    int indexStart = str.indexOf("weatherinfo\":");
    int indexEnd = str.indexOf("};var alarmDZ");

    String jsonCityDZ = str.substring(indexStart+13,indexEnd);
    indexStart = str.indexOf("dataSK =");
    indexEnd = str.indexOf(";var dataZS");
    String jsonDataSK = str.substring(indexStart+8,indexEnd);
    
    indexStart = str.indexOf("\"f\":[");
    indexEnd = str.indexOf(",{\"fa");
    String jsonFC = str.substring(indexStart+5,indexEnd);
    
    weaterData(&jsonCityDZ,&jsonDataSK,&jsonFC);
    Serial.println(F("获取成功"));
  } else {
    Serial.println(F("请求城市天气错误："));
    Serial.print(httpCode);
  }
  httpClient.end();
}

String scrollText[7];

void weaterData(String *cityDZ,String *dataSK,String *dataFC) {
  DynamicJsonDocument doc(1024);
  deserializeJson(doc, *dataSK);
  JsonObject sk = doc.as<JsonObject>();

  clk.setColorDepth(8);
  clk.loadFont(ZdyLwFont_20);
  
  // 温度
  clk.createSprite(58, 24); 
  clk.fillSprite(bgColor);
  clk.setTextDatum(CC_DATUM);
  clk.setTextColor(TFT_WHITE, bgColor); 
  clk.drawString(sk["temp"].as<String>()+F("℃"),28,13);
  clk.pushSprite(100,184);
  clk.deleteSprite();
  tempnum = sk["temp"].as<int>();
  tempnum = tempnum+10;
  if(tempnum<10)
    tempcol=0x00FF;
  else if(tempnum<28)
    tempcol=0x0AFF;
  else if(tempnum<34)
    tempcol=0x0F0F;
  else if(tempnum<41)
    tempcol=0xFF0F;
  else if(tempnum<49)
    tempcol=0xF00F;
  else {
    tempcol=0xF00F;
    tempnum=50;
  }
  tempWin();
  
  // 湿度
  clk.createSprite(58, 24); 
  clk.fillSprite(bgColor);
  clk.setTextDatum(CC_DATUM);
  clk.setTextColor(TFT_WHITE, bgColor); 
  clk.drawString(sk["SD"].as<String>(),28,13);
  clk.pushSprite(100,214);
  clk.deleteSprite();
  huminum = atoi((sk["SD"].as<String>()).substring(0,2).c_str());
  
  if(huminum>90)
    humicol=0x00FF;
  else if(huminum>70)
    humicol=0x0AFF;
  else if(huminum>40)
    humicol=0x0F0F;
  else if(huminum>20)
    humicol=0xFF0F;
  else
    humicol=0xF00F;
  humidityWin();

  // 城市名称
  clk.createSprite(94, 30); 
  clk.fillSprite(bgColor);
  clk.setTextDatum(CC_DATUM);
  clk.setTextColor(TFT_WHITE, bgColor); 
  clk.drawString(sk["cityname"].as<String>(),44,16);
  clk.pushSprite(15,15);
  clk.deleteSprite();

  // 空气质量
  uint16_t pm25BgColor = tft.color565(156,202,127);
  String aqiTxt = F("优");
  int pm25V = sk["aqi"];
  if(pm25V>200){
    pm25BgColor = tft.color565(136,11,32);
    aqiTxt = F("重度");
  }else if(pm25V>150){
    pm25BgColor = tft.color565(186,55,121);
    aqiTxt = F("中度");
  }else if(pm25V>100){
    pm25BgColor = tft.color565(242,159,57);
    aqiTxt = F("轻度");
  }else if(pm25V>50){
    pm25BgColor = tft.color565(247,219,100);
    aqiTxt = F("良");
  }
  clk.createSprite(56, 24); 
  clk.fillSprite(bgColor);
  clk.fillRoundRect(0,0,50,24,4,pm25BgColor);
  clk.setTextDatum(CC_DATUM);
  clk.setTextColor(0x0000); 
  clk.drawString(aqiTxt,25,13);
  clk.pushSprite(104,18);
  clk.deleteSprite();
  
  // 设置滚动显示文本
  scrollText[0] = F("实时天气 ")+sk["weather"].as<String>();
  scrollText[1] = F("空气质量 ")+aqiTxt;
  scrollText[2] = F("风向 ")+sk["WD"].as<String>()+sk["WS"].as<String>();
  
  // 解析其他天气信息
  deserializeJson(doc, *cityDZ);
  JsonObject dz = doc.as<JsonObject>();
  scrollText[3] = F("今日")+dz["weather"].as<String>();
  
  deserializeJson(doc, *dataFC);
  JsonObject fc = doc.as<JsonObject>();
  
  scrollText[4] = F("最低温度")+fc["fd"].as<String>()+F("℃");
  scrollText[5] = F("最高温度")+fc["fc"].as<String>()+F("℃");
  
  // IP地址信息
  if(WiFi.status() == WL_CONNECTED) {
    scrollText[6] = F("设备IP ") + WiFi.localIP().toString();
  } else {
    scrollText[6] = F("WiFi未连接");
  }

  wrat.printfweather(170,15,atoi((sk["weathercode"].as<String>()).substring(1,3).c_str()));
  
  clk.unloadFont();
}

int currentIndex = 0;
TFT_eSprite clkb = TFT_eSprite(&tft);

void scrollBanner(){
    if(scrollText[currentIndex]) {
      clkb.setColorDepth(8);
      clkb.loadFont(ZdyLwFont_20);
      clkb.createSprite(150, 30); 
      clkb.fillSprite(bgColor);
      clkb.setTextWrap(false);
      clkb.setTextDatum(CC_DATUM);
      clkb.setTextColor(TFT_WHITE, bgColor); 
      clkb.drawString(scrollText[currentIndex],74, 16);
      clkb.pushSprite(10,45);
       
      clkb.deleteSprite();
      clkb.unloadFont();
      
      if(currentIndex>=6)
        currentIndex = 0;
      else
        currentIndex += 1;
    }
    prevTime = 1;
}

#if imgAst_EN
void imgAnim() {
  int x=160,y=160;
  if(millis() - AprevTime > 37) {
    Anim++;
    AprevTime = millis();
  }
  if(Anim==10)
    Anim=0;

  switch(Anim) {
    case 0: TJpgDec.drawJpg(x,y,i0, sizeof(i0)); break;
    case 1: TJpgDec.drawJpg(x,y,i1, sizeof(i1)); break;
    case 2: TJpgDec.drawJpg(x,y,i2, sizeof(i2)); break;
    case 3: TJpgDec.drawJpg(x,y,i3, sizeof(i3)); break;
    case 4: TJpgDec.drawJpg(x,y,i4, sizeof(i4)); break;
    case 5: TJpgDec.drawJpg(x,y,i5, sizeof(i5)); break;
    case 6: TJpgDec.drawJpg(x,y,i6, sizeof(i6)); break;
    case 7: TJpgDec.drawJpg(x,y,i7, sizeof(i7)); break;
    case 8: TJpgDec.drawJpg(x,y,i8, sizeof(i8)); break;
    case 9: TJpgDec.drawJpg(x,y,i9, sizeof(i9)); break;
    default: Serial.println(F("显示Anim错误")); break;
  }
}
#endif

unsigned char Hour_sign   = 60;
unsigned char Minute_sign = 60;
unsigned char Second_sign = 60;

void digitalClockDisplay(int reflash_en) { 
  int timey=82;
  if(hour()!=Hour_sign || reflash_en == 1) {
    dig.printfW3660(20,timey,hour()/10);
    dig.printfW3660(60,timey,hour()%10);
    Hour_sign = hour();
  }
  if(minute()!=Minute_sign  || reflash_en == 1) {
    dig.printfO3660(101,timey,minute()/10);
    dig.printfO3660(141,timey,minute()%10);
    Minute_sign = minute();
  }
  if(second()!=Second_sign  || reflash_en == 1) {
    dig.printfW1830(182,timey+30,second()/10);
    dig.printfW1830(202,timey+30,second()%10);
    Second_sign = second();
  }
  
  if(reflash_en == 1) reflash_en = 0;
  
  clk.setColorDepth(8);
  clk.loadFont(ZdyLwFont_20);
   
  // 星期
  clk.createSprite(58, 30);
  clk.fillSprite(bgColor);
  clk.setTextDatum(CC_DATUM);
  clk.setTextColor(TFT_WHITE, bgColor);
  clk.drawString(week(),29,16);
  clk.pushSprite(102,150);
  clk.deleteSprite();
  
  // 月日
  clk.createSprite(95, 30);
  clk.fillSprite(bgColor);
  clk.setTextDatum(CC_DATUM);
  clk.setTextColor(TFT_WHITE, bgColor);  
  clk.drawString(monthDay(),49,16);
  clk.pushSprite(5,150);
  clk.deleteSprite();
  
  clk.unloadFont();
}

String week() {
  String wk[7] = {F("日"),F("一"),F("二"),F("三"),F("四"),F("五"),F("六")};
  String s = F("周") + wk[weekday()-1];
  return s;
}

String monthDay() {
  String s = String(month());
  s = s + F("月") + day() + F("日");
  return s;
}

const int NTP_PACKET_SIZE = 48;
byte packetBuffer[NTP_PACKET_SIZE];

time_t getNtpTime() {
  IPAddress ntpServerIP;
  while (Udp.parsePacket() > 0) ;
  WiFi.hostByName(ntpServerName, ntpServerIP);
  sendNTPpacket(ntpServerIP);
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500) {
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      Serial.println(F("Receive NTP Response"));
      Udp.read(packetBuffer, NTP_PACKET_SIZE);
      unsigned long secsSince1900;
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      return secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR;
    }
  }
  Serial.println(F("No NTP Response :-("));
  return 0;
}

void sendNTPpacket(IPAddress &address) {
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  packetBuffer[0] = 0b11100011;
  packetBuffer[1] = 0;
  packetBuffer[2] = 6;
  packetBuffer[3] = 0xEC;
  packetBuffer[12] = 49;
  packetBuffer[13] = 0x4E;
  packetBuffer[14] = 49;
  packetBuffer[15] = 52;
  Udp.beginPacket(address, 123);
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}