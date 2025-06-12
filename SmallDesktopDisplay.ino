#define Version  "SDD V2.0"

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
#include <FS.h>
#include "qr.h"
#include "number.h"
#include "weathernum.h"

// Blinker Library
#define BLINKER_WIFI
#define BLINKER_ALIGENIE_SENSOR
#include <Blinker.h>

// Configuration flags
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

// Image libraries
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

// Configuration structure for JSON storage
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

// Display settings
TFT_eSPI tft = TFT_eSPI();
TFT_eSprite clk = TFT_eSprite(&tft);
#define LCD_BL_PIN 5
uint16_t bgColor = 0x0000;

// Status flags
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

// Function declarations
time_t getNtpTime();
void digitalClockDisplay(int reflash_en);
void sendNTPpacket(IPAddress &address);
void LCD_reflash(int en);
void Web_Sever_Init();
void Web_Sever();
bool loadConfig();
bool saveConfig();
void resetConfig();

// Configuration file management
bool loadConfig() {
  if (!SPIFFS.begin()) {
    Serial.println("SPIFFS Mount Failed");
    return false;
  }
  
  if (!SPIFFS.exists("/config.json")) {
    Serial.println("Config file not found, using defaults");
    // Set default values
    config.blinker_auth = "";
    config.city_code = "101250101";
    config.lcd_brightness = 50;
    config.weather_update_time = 10;
    config.lcd_rotation = 0;
    config.dht_enabled = 0;
    config.show_animation = 1;
    return saveConfig(); // Save defaults
  }
  
  File configFile = SPIFFS.open("/config.json", "r");
  if (!configFile) {
    Serial.println("Failed to open config file");
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
  
  Serial.println("Config loaded successfully");
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
  
  File configFile = SPIFFS.open("/config.json", "w");
  if (!configFile) {
    Serial.println("Failed to open config file for writing");
    return false;
  }
  
  serializeJson(doc, configFile);
  configFile.close();
  
  Serial.println("Config saved successfully");
  return true;
}

void resetConfig() {
  if (SPIFFS.exists("/config.json")) {
    SPIFFS.remove("/config.json");
    Serial.println("Config file deleted");
  }
}

// Blinker callback functions
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
  clk.drawString("Connecting to WiFi......",100,40,2);
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
  String s = "内温";
  
  clk.setColorDepth(8);
  clk.loadFont(ZdyLwFont_20);
  
  clk.createSprite(58, 30);
  clk.fillSprite(bgColor);
  clk.setTextDatum(CC_DATUM);
  clk.setTextColor(TFT_WHITE, bgColor);
  clk.drawString(s,29,16);
  clk.pushSprite(172,150);
  clk.deleteSprite();
  
  clk.createSprite(60, 24); 
  clk.fillSprite(bgColor);
  clk.setTextDatum(CC_DATUM);
  clk.setTextColor(TFT_WHITE, bgColor); 
  clk.drawFloat(t,1,20,13);
  clk.drawString("℃",50,13);
  clk.pushSprite(170,184);
  clk.deleteSprite();
  
  clk.createSprite(60, 24); 
  clk.fillSprite(bgColor);
  clk.setTextDatum(CC_DATUM);
  clk.setTextColor(TFT_WHITE, bgColor); 
  clk.drawFloat(h,1,20,13);
  clk.drawString("%",50,13);
  clk.pushSprite(170,214);
  clk.deleteSprite();
}
#endif

#if !WM_EN
void SmartConfig(void) {
  WiFi.mode(WIFI_STA);
  tft.pushImage(0, 0, 240, 240, qr);
  Serial.println("\r\nWait for Smartconfig...");
  WiFi.beginSmartConfig();
  while (1) {
    Serial.print(".");
    delay(100);
    if (WiFi.smartConfigDone()) {
    Serial.println("SmartConfig Success");
    Serial.printf("SSID:%s\r\n", WiFi.SSID().c_str());
    Serial.printf("PSW:%s\r\n", WiFi.psk().c_str());
    break;
    }
  }
  loadNum = 194;
}
#endif

// Web server functions
#if WebSever_EN
void handleconfig() {
  String msg = "";
  bool configChanged = false;

  if (server.hasArg("web_ccode") || server.hasArg("web_bl") || 
      server.hasArg("web_upwe_t") || server.hasArg("web_DHT11_en") || 
      server.hasArg("web_set_rotation") || server.hasArg("web_animation") ||
      server.hasArg("web_blinker_auth")) {
    
    // Get form values
    String new_blinker_auth = server.arg("web_blinker_auth");
    String new_city_code = server.arg("web_ccode");
    int new_brightness = server.arg("web_bl").toInt();
    int new_update_time = server.arg("web_upwe_t").toInt();
    int new_rotation = server.arg("web_set_rotation").toInt();
    int new_dht_enabled = server.arg("web_DHT11_en").toInt();
    int new_animation = server.arg("web_animation").toInt();
    
    // Validate and update Blinker auth
    if (new_blinker_auth.length() > 0 && new_blinker_auth != config.blinker_auth) {
      config.blinker_auth = new_blinker_auth;
      configChanged = true;
      msg += "Blinker密钥已更新<br>";
      Serial.println("Blinker Auth: " + config.blinker_auth);
    }
    
    // Validate and update city code
    int cc = new_city_code.toInt();
    if (cc >= 101000000 && cc <= 102000000) {
      config.city_code = new_city_code;
      configChanged = true;
      msg += "城市代码已更新<br>";
      Serial.println("City Code: " + config.city_code);
    }
    
    // Validate and update brightness
    if (new_brightness > 0 && new_brightness <= 100) {
      config.lcd_brightness = new_brightness;
      analogWrite(LCD_BL_PIN, 1023 - (config.lcd_brightness * 10));
      configChanged = true;
      msg += "亮度已调整<br>";
      Serial.println("Brightness: " + String(config.lcd_brightness));
    }
    
    // Validate and update weather update time
    if (new_update_time > 0 && new_update_time <= 60) {
      config.weather_update_time = new_update_time;
      configChanged = true;
      msg += "天气更新时间已设置<br>";
      Serial.println("Update Time: " + String(config.weather_update_time));
    }

    // Update animation setting
    if (new_animation != config.show_animation) {
      config.show_animation = new_animation;
      configChanged = true;
      msg += "显示模式已切换<br>";
      tft.fillScreen(0x0000);
      LCD_reflash(1);
      UpdateWeater_en = 1;
      TJpgDec.drawJpg(15,183,temperature, sizeof(temperature));
      TJpgDec.drawJpg(15,213,humidity, sizeof(humidity));
      Serial.println("Animation: " + String(config.show_animation ? "动画" : "室内温度"));
    }

    // Update DHT setting
    if (new_dht_enabled != config.dht_enabled) {
      config.dht_enabled = new_dht_enabled;
      configChanged = true;
      msg += "DHT传感器设置已更新<br>";
      tft.fillScreen(0x0000);
      LCD_reflash(1);
      UpdateWeater_en = 1;
      TJpgDec.drawJpg(15,183,temperature, sizeof(temperature));
      TJpgDec.drawJpg(15,213,humidity, sizeof(humidity));
    }

    // Update rotation
    if (new_rotation != config.lcd_rotation) {
      config.lcd_rotation = new_rotation;
      configChanged = true;
      msg += "屏幕方向已调整<br>";
      tft.setRotation(config.lcd_rotation);
      tft.fillScreen(0x0000);
      LCD_reflash(1);
      UpdateWeater_en = 1;
      TJpgDec.drawJpg(15,183,temperature, sizeof(temperature));
      TJpgDec.drawJpg(15,213,humidity, sizeof(humidity));
    }
    
    // Save configuration if changed
    if (configChanged) {
      if (saveConfig()) {
        msg += "配置已保存！<br>";
        
        // Restart Blinker if auth key changed
        if (config.blinker_auth.length() > 0) {
          msg += "正在重新初始化Blinker...<br>";
          // Note: In practice, you might need to restart the device for Blinker changes
        }
      } else {
        msg += "配置保存失败！<br>";
      }
    }
  }

  // Enhanced web interface
  String content = "<html><head><meta charset='UTF-8'>";
  content += "<meta name='viewport' content='width=device-width, initial-scale=1.0'>";
  content += "<title>小型桌面显示器配置</title>";
  content += "<style>";
  content += "* { box-sizing: border-box; }";
  content += "body { background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); color: #fff; font-family: 'Segoe UI', Arial, sans-serif; margin: 0; padding: 20px; min-height: 100vh; }";
  content += ".container { max-width: 500px; margin: 0 auto; background: rgba(255,255,255,0.1); padding: 30px; border-radius: 15px; backdrop-filter: blur(10px); box-shadow: 0 8px 32px rgba(0,0,0,0.3); }";
  content += "h2 { text-align: center; margin-bottom: 30px; font-size: 24px; }";
  content += ".group { margin: 20px 0; padding: 15px; background: rgba(255,255,255,0.05); border-radius: 10px; border: 1px solid rgba(255,255,255,0.1); }";
  content += "label { display: block; margin-bottom: 8px; font-weight: 600; }";
  content += "input[type='text'] { width: 100%; padding: 12px; border: none; border-radius: 8px; background: rgba(255,255,255,0.9); color: #333; font-size: 14px; }";
  content += "input[type='radio'] { margin: 8px 8px 8px 0; transform: scale(1.2); }";
  content += ".radio-group { display: flex; flex-wrap: wrap; gap: 15px; margin-top: 10px; }";
  content += ".radio-option { display: flex; align-items: center; }";
  content += "input[type='submit'] { width: 100%; padding: 15px; background: linear-gradient(45deg, #fff, #f0f0f0); color: #333; border: none; border-radius: 10px; font-size: 16px; font-weight: bold; margin-top: 20px; cursor: pointer; transition: all 0.3s; }";
  content += "input[type='submit']:hover { transform: translateY(-2px); box-shadow: 0 5px 15px rgba(0,0,0,0.2); }";
  content += ".message { background: rgba(76, 175, 80, 0.2); border: 1px solid rgba(76, 175, 80, 0.5); padding: 10px; border-radius: 5px; margin: 10px 0; }";
  content += ".status { text-align: center; margin-top: 20px; font-size: 12px; opacity: 0.8; }";
  content += "</style></head><body>";
  
  content += "<div class='container'>";
  content += "<h2>🖥️ 小型桌面显示器配置</h2>";
  
  if (msg.length() > 0) {
    content += "<div class='message'>" + msg + "</div>";
  }
  
  content += "<form action='/' method='POST'>";
  
  content += "<div class='group'>";
  content += "<label>🔑 Blinker密钥 (天猫精灵):</label>";
  content += "<input type='text' name='web_blinker_auth' placeholder='从Blinker APP获取设备密钥' value='" + config.blinker_auth + "'>";
  content += "</div>";
  
  content += "<div class='group'>";
  content += "<label>🏙️ 城市代码:</label>";
  content += "<input type='text' name='web_ccode' placeholder='例如: 101250101 (长沙)' value='" + config.city_code + "'>";
  content += "</div>";
  
  content += "<div class='group'>";
  content += "<label>💡 屏幕亮度 (1-100):</label>";
  content += "<input type='text' name='web_bl' placeholder='50' value='" + String(config.lcd_brightness) + "'>";
  content += "</div>";
  
  content += "<div class='group'>";
  content += "<label>⏰ 天气更新间隔 (分钟):</label>";
  content += "<input type='text' name='web_upwe_t' placeholder='10' value='" + String(config.weather_update_time) + "'>";
  content += "</div>";
  
  content += "<div class='group'>";
  content += "<label>🎭 右下角显示:</label>";
  content += "<div class='radio-group'>";
  content += "<div class='radio-option'><input type='radio' name='web_animation' value='1'" + (config.show_animation ? " checked" : "") + "> 🚀 动画</div>";
  content += "<div class='radio-option'><input type='radio' name='web_animation' value='0'" + (!config.show_animation ? " checked" : "") + "> 🌡️ 室内温度</div>";
  content += "</div></div>";
  
  #if DHT_EN
  content += "<div class='group'>";
  content += "<label>🌡️ DHT11传感器:</label>";
  content += "<div class='radio-group'>";
  content += "<div class='radio-option'><input type='radio' name='web_DHT11_en' value='0'" + (!config.dht_enabled ? " checked" : "") + "> 禁用</div>";
  content += "<div class='radio-option'><input type='radio' name='web_DHT11_en' value='1'" + (config.dht_enabled ? " checked" : "") + "> 启用</div>";
  content += "</div></div>";
  #endif
  
  content += "<div class='group'>";
  content += "<label>🔄 屏幕方向:</label>";
  content += "<div class='radio-group'>";
  content += "<div class='radio-option'><input type='radio' name='web_set_rotation' value='0'" + (config.lcd_rotation == 0 ? " checked" : "") + "> USB朝下</div>";
  content += "<div class='radio-option'><input type='radio' name='web_set_rotation' value='1'" + (config.lcd_rotation == 1 ? " checked" : "") + "> USB朝右</div>";
  content += "<div class='radio-option'><input type='radio' name='web_set_rotation' value='2'" + (config.lcd_rotation == 2 ? " checked" : "") + "> USB朝上</div>";
  content += "<div class='radio-option'><input type='radio' name='web_set_rotation' value='3'" + (config.lcd_rotation == 3 ? " checked" : "") + "> USB朝左</div>";
  content += "</div></div>";
  
  content += "<input type='submit' value='💾 保存设置'>";
  content += "</form>";
  
  content += "<div class='status'>";
  content += "💬 支持天猫精灵语音控制 | IP: " + WiFi.localIP().toString();
  content += "</div>";
  
  content += "</div></body></html>";
  
  server.send(200, "text/html", content);
}

void handleReset() {
  resetConfig();
  String content = "<html><head><meta charset='UTF-8'><meta http-equiv='refresh' content='3;url=/'></head>";
  content += "<body style='background:#667eea;color:#fff;text-align:center;font-family:Arial;padding:50px;'>";
  content += "<h2>配置已重置</h2><p>3秒后自动跳转...</p></body></html>";
  server.send(200, "text/html", content);
  delay(100);
  ESP.restart();
}

void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: " + server.uri();
  message += "\nMethod: " + (server.method() == HTTP_GET ? "GET" : "POST");
  message += "\nArguments: " + String(server.args()) + "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}

void Web_Sever_Init() {
  uint32_t counttime = 0;
  Serial.println("mDNS responder building...");
  counttime = millis();
  while (!MDNS.begin("sdd")) {
    if(millis() - counttime > 30000) ESP.restart();
  }

  Serial.println("mDNS responder started");
  server.on("/", handleconfig);
  server.on("/reset", handleReset);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("HTTP服务器已开启");
  Serial.println("配置页面: http://sdd.local");
  Serial.println("重置配置: http://sdd.local/reset");
  Serial.print("本地IP： ");
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
  clk.drawString("配置页面已开启",100,15,2);
  clk.setTextColor(TFT_WHITE, 0x0000); 
  clk.drawString("http://sdd.local",100,35,3);
  clk.drawString("或 " + WiFi.localIP().toString(),100,55,2);
  clk.setTextColor(TFT_YELLOW, 0x0000);
  clk.drawString("支持天猫精灵控制",100,70,2);
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
  clk.drawString("WiFi配网中...",100,15,2);
  clk.drawString("SSID: SmallDisplay",100,35,2);
  clk.pushSprite(20,50);
  clk.deleteSprite();
}

void Webconfig() {
  WiFi.mode(WIFI_STA);
  delay(3000);
  wm.resetSettings();
  
  // Enhanced WiFiManager configuration with Blinker support
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
  Serial.println("[CALLBACK] saveParamCallback fired");
  
  // Get parameters from WiFiManager
  config.blinker_auth = getParam("blinker_auth");
  config.city_code = getParam("CityCode");
  config.lcd_brightness = getParam("LCDBL").toInt();
  config.weather_update_time = getParam("WeaterUpdateTime").toInt();

  // Validate city code
  int cc = config.city_code.toInt();
  if (cc < 101000000 || cc > 102000000) {
    config.city_code = "101250101"; // Default to Changsha
  }
  
  // Validate brightness
  if (config.lcd_brightness < 1 || config.lcd_brightness > 100) {
    config.lcd_brightness = 50;
  }
  
  // Validate update time
  if (config.weather_update_time < 1 || config.weather_update_time > 60) {
    config.weather_update_time = 10;
  }
  
  // Save configuration
  saveConfig();
  
  // Apply settings immediately
  analogWrite(LCD_BL_PIN, 1023 - (config.lcd_brightness * 10));
  
  Serial.println("Configuration saved:");
  Serial.println("Blinker Auth: " + config.blinker_auth);
  Serial.println("City Code: " + config.city_code);
  Serial.println("Brightness: " + String(config.lcd_brightness));
  Serial.println("Update Time: " + String(config.weather_update_time) + " minutes");
}
#endif

void setup() {
  Serial.begin(115200);
  
  // Initialize filesystem
  if (!SPIFFS.begin()) {
    Serial.println("SPIFFS Mount Failed");
    // Format SPIFFS if mount failed
    Serial.println("Formatting SPIFFS...");
    SPIFFS.format();
    SPIFFS.begin();
  }
  
  // Load configuration
  loadConfig();
  
 #if DHT_EN
  dht.begin();
 #endif
 
  // Apply loaded configuration
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

  // Try to connect to saved WiFi or start config portal
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
    Serial.println("WiFi connected successfully");
    
    // Initialize Blinker if auth key is available
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
      Serial.println("Blinker initialized with AliGenie support");
      Serial.println("Auth: " + config.blinker_auth);
    } else {
      Serial.println("Blinker auth key not configured");
    }
    
    #if WebSever_EN
    Web_Sever_Init();
    Web_sever_Win();
    delay(5000);
    #endif
  }

  Serial.println("启动UDP");
  Udp.begin(localPort);
  Serial.println("等待同步...");
  setSyncProvider(getNtpTime);
  setSyncInterval(300);

  TJpgDec.setJpgScale(1);
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(tft_output);
  
  // Use configured city code or get automatically
  if (config.city_code.length() > 0) {
    int cc = config.city_code.toInt();
    if (cc >= 101000000 && cc <= 102000000) {
      // Valid city code
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
  // Run Blinker if initialized
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
      Serial.println("WIFI恢复......");
      Wifi_en = 1;
    }

    if(WiFi.status() == WL_CONNECTED) {
      getCityWeater();
      if(UpdateWeater_en != 0) UpdateWeater_en = 0;
      weaterTime = millis();
      getNtpTime();
      #if !WebSever_EN
      WiFi.forceSleepBegin();
      Serial.println("WIFI休眠......");
      Wifi_en = 0;
      #endif
    }
  }
}

// Weather functions (keeping existing implementation)
void getCityCode(){
 String URL = "http://wgeo.weather.com.cn/ip/?_="+String(now());
  HTTPClient httpClient;
  httpClient.begin(wificlient,URL); 
  httpClient.setUserAgent("Mozilla/5.0 (iPhone; CPU iPhone OS 11_0 like Mac OS X) AppleWebKit/604.1.38 (KHTML, like Gecko) Version/11.0 Mobile/15A372 Safari/604.1");
  httpClient.addHeader("Referer", "http://www.weather.com.cn/");
  
  int httpCode = httpClient.GET();
  Serial.print("Send GET request to URL: ");
  Serial.println(URL);
  
  if (httpCode == HTTP_CODE_OK) {
    String str = httpClient.getString();
    int aa = str.indexOf("id=");
    if(aa>-1) {
       config.city_code = str.substring(aa+4,aa+4+9);
       Serial.println("Auto detected city: " + config.city_code);
       saveConfig(); // Save the detected city code
       getCityWeater();
    } else {
      Serial.println("获取城市代码失败");  
    }
  } else {
    Serial.println("请求城市代码错误：");
    Serial.println(httpCode);
  }
  httpClient.end();
}

void getCityWeater(){
 String URL = "http://d1.weather.com.cn/weather_index/" + config.city_code + ".html?_="+String(now());
  HTTPClient httpClient;
  httpClient.begin(URL); 
  httpClient.setUserAgent("Mozilla/5.0 (iPhone; CPU iPhone OS 11_0 like Mac OS X) AppleWebKit/604.1.38 (KHTML, like Gecko) Version/11.0 Mobile/15A372 Safari/604.1");
  httpClient.addHeader("Referer", "http://www.weather.com.cn/");
  
  int httpCode = httpClient.GET();
  Serial.println("正在获取天气数据");
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
    Serial.println("获取成功");
  } else {
    Serial.println("请求城市天气错误：");
    Serial.print(httpCode);
  }
  httpClient.end();
}

String scrollText[7]; // 修改回7个滚动项

void weaterData(String *cityDZ,String *dataSK,String *dataFC) {
  DynamicJsonDocument doc(1024);
  deserializeJson(doc, *dataSK);
  JsonObject sk = doc.as<JsonObject>();

  clk.setColorDepth(8);
  clk.loadFont(ZdyLwFont_20);
  
  // Temperature
  clk.createSprite(58, 24); 
  clk.fillSprite(bgColor);
  clk.setTextDatum(CC_DATUM);
  clk.setTextColor(TFT_WHITE, bgColor); 
  clk.drawString(sk["temp"].as<String>()+"℃",28,13);
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
  
  // Humidity
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

  // City name
  clk.createSprite(94, 30); 
  clk.fillSprite(bgColor);
  clk.setTextDatum(CC_DATUM);
  clk.setTextColor(TFT_WHITE, bgColor); 
  clk.drawString(sk["cityname"].as<String>(),44,16);
  clk.pushSprite(15,15);
  clk.deleteSprite();

  // Air Quality
  uint16_t pm25BgColor = tft.color565(156,202,127);
  String aqiTxt = "优";
  int pm25V = sk["aqi"];
  if(pm25V>200){
    pm25BgColor = tft.color565(136,11,32);
    aqiTxt = "重度";
  }else if(pm25V>150){
    pm25BgColor = tft.color565(186,55,121);
    aqiTxt = "中度";
  }else if(pm25V>100){
    pm25BgColor = tft.color565(242,159,57);
    aqiTxt = "轻度";
  }else if(pm25V>50){
    pm25BgColor = tft.color565(247,219,100);
    aqiTxt = "良";
  }
  clk.createSprite(56, 24); 
  clk.fillSprite(bgColor);
  clk.fillRoundRect(0,0,50,24,4,pm25BgColor);
  clk.setTextDatum(CC_DATUM);
  clk.setTextColor(0x0000); 
  clk.drawString(aqiTxt,25,13);
  clk.pushSprite(104,18);
  clk.deleteSprite();
  
  // 设置滚动显示文本，包含IP地址但去掉sdd.local
  scrollText[0] = "实时天气 "+sk["weather"].as<String>();
  scrollText[1] = "空气质量 "+aqiTxt;
  scrollText[2] = "风向 "+sk["WD"].as<String>()+sk["WS"].as<String>();
  
  // 解析其他天气信息
  deserializeJson(doc, *cityDZ);
  JsonObject dz = doc.as<JsonObject>();
  scrollText[3] = "今日"+dz["weather"].as<String>();
  
  deserializeJson(doc, *dataFC);
  JsonObject fc = doc.as<JsonObject>();
  
  scrollText[4] = "最低温度"+fc["fd"].as<String>()+"℃";
  scrollText[5] = "最高温度"+fc["fc"].as<String>()+"℃";
  
  // 只添加IP地址信息到滚动显示，去掉访问地址
  if(WiFi.status() == WL_CONNECTED) {
    scrollText[6] = "设备IP " + WiFi.localIP().toString();
  } else {
    scrollText[6] = "WiFi未连接";
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
      
      // 修改循环逻辑，现在只有7个项目（0-6）
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
    default: Serial.println("显示Anim错误"); break;
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
   
  // Week
  clk.createSprite(58, 30);
  clk.fillSprite(bgColor);
  clk.setTextDatum(CC_DATUM);
  clk.setTextColor(TFT_WHITE, bgColor);
  clk.drawString(week(),29,16);
  clk.pushSprite(102,150);
  clk.deleteSprite();
  
  // Month and Day
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
  String wk[7] = {"日","一","二","三","四","五","六"};
  String s = "周" + wk[weekday()-1];
  return s;
}

String monthDay() {
  String s = String(month());
  s = s + "月" + day() + "日";
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
      Serial.println("Receive NTP Response");
      Udp.read(packetBuffer, NTP_PACKET_SIZE);
      unsigned long secsSince1900;
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      return secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR;
    }
  }
  Serial.println("No NTP Response :-(");
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
