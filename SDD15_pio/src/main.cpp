#include <Arduino.h>
/* *****************************************************************
 * 
 * SmallDesktopDisplay
 *    小型桌面显示器
 * 
 * 讨  论  群：811058758、887171863
 * 创 建 日 期：2021.07.19
 * 最后更改日期：2026.11.04
 * 更 改 说 明：V1.5    移除DHT11温湿度采集及相关代码；删除WiFiManager依赖，改为自写配网：
 *                    连不上已保存WiFi时自动开启开放热点 SDD-xxxx（无需密码），手机/电脑连上后访问
 *                    http://192.168.4.1 扫描并选择WiFi完成配网；配网成功后Web设置页面暴露在内网中，
 *                    同一局域网内直接访问设备IP进行设置（深色卡片式中文页面，含
 *                    设备状态、WiFi配网、亮度/方向/城市代码/天气更新时间设置）；屏幕UI布局坐标集中
 *                    定义并清爽重排，太空人动画常驻右下角。串口设置（0x01-0x05）保留。
 *            V1.6    Web页面增加OTA固件上传升级（选 .bin 文件直接升级，页面显示上传进度，
 *                    屏幕上同步画进度条；固件先写入flash末尾暂存区，重启时由EBoot拷回，
 *                    断电风险只落在重启拷贝那几秒）。路由 /update，不依赖 ArduinoOTA。
 *                    配网页（AP热点 192.168.4.1）同样带升级入口，未配网也能刷固件；
 *                    上传期间暂停“每30秒重试已保存WiFi”，避免切模式打断传输。
 *            V1.7    屏幕 UI 可用 LittleFS 素材（UI_THEME 1）：天气图标与平滑数字字库
 *                    放文件系统，换图换字只重传文件系统、不用重编固件；素材缺失自动退回内置。
 *            V2.0    修上电雪花屏：背光提前到面板初始化之前点亮，会把显存里的随机值显示出来。
 *                    现改为上电先关背光，面板初始化并清屏完成后再渐亮。
 *                    内置 JPEG 素材重编码去黑底噪点（亮度≤12 压黑 + q100 4:4:4）。
 *            V2.1    温度按一位小数显示（接口本来给的是 14.6 这类小数，之前被按整数处理），
 *                    温湿度改为“左图标 + 右对齐数值”版式（与网页设置页一致）；
 *                    滚动条新增日出/日落时间（取 t.weather.itboy.net 同城市码接口）。
 *            V2.2    设置保存改为字段级错误码（{"ok":0,"err":"cc"} 这类），网页直接说清哪项不合格；
 *                    加 Captive portal：AP 模式下 DNS 劫持 + 各系统探测路径 302，连上热点自动弹配网页；
 *                    /api/status 补诊断字段（可用内存/碎片/Flash/天气更新时效/日出日落），排障不用连串口。
 *            V2.3    修湿度显示：接口给的 "100%" 被 substring(0,2) 截成 10，三位数时数值对、条形只剩 1/10；
 *                    改为取整串数字（1/2/3 位都对），缺 SD 字段时回退小写 sd，认不出数字显示 "--"；
 *                    湿度条形改为按 0-100 等比且不再改写全局；/api/status 与网页状态卡增加“天气读数”。
 *
 * 引 脚 分 配： SCK  GPIO14
 *             MOSI  GPIO13
 *             RES   GPIO2
 *             DC    GPIO0
 *             LCDBL GPIO5
 * 
 *    感谢群友 @你别失望  提醒发现WiFi保存后无法重置的问题，目前已解决。详情查看更改说明！
 * *****************************************************************/
#define Version  "SDD V2.3"
/* *****************************************************************
 *  库文件、头文件
 * *****************************************************************/
#include "ArduinoJson.h"
#include <TimeLib.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>
#include <ESP8266WebServer.h>
#include <WiFiUdp.h>
#include <TFT_eSPI.h> 
#include <SPI.h>
#include <TJpg_Decoder.h>
#include <EEPROM.h>
#include <LittleFS.h>
#include <DNSServer.h>
#include "webui.h"
#include "number.h"
#include "weathernum.h"
/* *****************************************************************
 *  配置使能位
 * *****************************************************************/
//Web服务器使能标志位----打开后WiFi保持常连（内网设置页面依赖此项），关闭则不启动配网/设置网页
#define WebSever_EN  1
/* 屏幕 UI 主题
 *   0 = 原版：内置彩色天气图标 + 内置 36x60 粗数字
 *   1 = 素材版：LittleFS 里的航天员天气图标 + W0365 平滑数字（改图换字不用重编固件）
 * 资源缺失时自动退回 0 的行为，不会白屏。 */
#define UI_THEME  0
//太空人动画使能标志位（DHT11已移除，动画常驻右下角）
#define imgAst_EN 1
//AP配网热点参数（连不上已保存的WiFi时自动开启开放热点，手机连上后访问 http://192.168.4.1 配网）
#define AP_SSID_PREFIX  "SDD-"
//STA连接超时（毫秒）
#define STA_CONNECT_TIMEOUT  20000
//AP配网界面下自动重试已保存WiFi的间隔（毫秒）
#define AP_RETRY_INTERVAL  30000
/* *****************************************************************
 *  屏幕布局坐标（240x240，局部刷新精灵的推送原点集中定义，改布局只动这里）
 * *****************************************************************/
//顶部天气带
#define L_CITY_X    10   //城市名精灵 94x30
#define L_CITY_Y    8
#define L_AQI_X     107  //AQI徽章精灵 56x24
#define L_AQI_Y     13
#define L_WICON_X   174  //天气图标 60x60
#define L_WICON_Y   8
#define L_SCROLL_X  10   //滚动信息条精灵 150x30
#define L_SCROLL_Y  44
//时钟区
#define L_TIME_H1   20   //时-十位/个位 36x60
#define L_TIME_H2   60
#define L_TIME_M1   101  //分-十位/个位 36x60
#define L_TIME_M2   141
#define L_TIME_Y    84
#define L_SEC_X1    182  //秒-十位/个位 18x30
#define L_SEC_X2    202
#define L_SEC_Y     114
#define L_TODAY_X   178  //实时天气色块精灵 56x26（图标与秒数之间，原生字号）
#define L_TODAY_Y   76
//日期与室外温湿度
#define L_MD_X      10   //月日/农历轮播精灵 100x30
#define L_MD_Y      152
#define L_WK_X      110  //星期精灵 58x30
#define L_WK_Y      152
#define L_TI_ICON_X 10   //室外温度图标 24x24
#define L_TI_ICON_Y 186
#define L_TI_BAR_X  40   //温度进度条精灵 52x6
#define L_TI_BAR_Y  195
#define L_VAL_W     66   //温/湿度数值列宽（右对齐到 L_TI_VAL_X+L_VAL_W-2）
#define L_TI_VAL_X  98   //温度值精灵 66x24
#define L_TI_VAL_Y  186
#define L_HU_ICON_X 10   //室外湿度图标 24x24
#define L_HU_ICON_Y 214
#define L_HU_BAR_X  40   //湿度进度条精灵 52x6
#define L_HU_BAR_Y  223
#define L_HU_VAL_X  98   //湿度值精灵 58x24
#define L_HU_VAL_Y  214
#define L_AST_X     166  //太空人动画 70x70
#define L_AST_Y     166
/* *****************************************************************
 *  LittleFS 素材：天气图标 + 平滑字库（改图换字只重传文件系统，不重编固件）
 * ***************************************************************** */
#define UI_FONT_PATH  "/font/W0365.vlw"   //11 个字形：0-9 与冒号
#if UI_THEME
static bool fsOk = false;      //LittleFS 是否挂载成功
static bool uiFontOk = false;  //平滑字库是否可用
#endif
//挂载文件系统并检查素材；任何一步失败都退回内置资源，屏幕不会空
static void fsBegin()
{
#if UI_THEME
  fsOk = LittleFS.begin();
  Serial.printf("LittleFS: %s\r\n", fsOk ? "mounted" : "mount failed");
  if(fsOk)
  {
    uiFontOk = LittleFS.exists(UI_FONT_PATH);
    Serial.printf("UI font: %s\r\n", uiFontOk ? "ok" : "missing, use built-in digits");
    Serial.printf("wx icon w0: %s\r\n", LittleFS.exists("/wx/w0.jpg") ? "ok" : "missing, use built-in icon");
  }
#endif
}
#if UI_THEME
//天气编码 → LittleFS 图标序号（0晴 1多云 2阴 3雷阵雨 4雨 5雪 6雾霾/沙尘）
static uint8_t wxIconIndex(int code)
{
  if(code == 0) return 0;
  if(code == 1) return 1;
  if(code == 2) return 2;
  if(code >= 3 && code <= 5) return 3;
  if(code >= 6 && code <= 12) return 4;
  if(code >= 13 && code <= 17) return 5;
  if(code == 26 || code == 27 || code == 28 || code == 302) return 5;
  return 6;
}
#endif
/* *****************************************************************
 *  字库、图片库
 * *****************************************************************/
#include "font/ZdyLwFont_20.h"
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
int Anim = 0;           //太空人图标显示指针记录
int AprevTime = 0;      //太空人更新时间记录
#endif
/* *****************************************************************
 *  参数设置
 * *****************************************************************/
struct config_type
{
  char stassid[32];//定义配网得到的WIFI名长度(最大32字节)
  char stapsw[64];//定义配网得到的WIFI密码长度(最大64字节)
};
//---------------修改此处""内的信息--------------------
//如开启WEB配网则可不用设置这里的参数，前一个为wifi ssid，后一个为密码
config_type wificonf ={{""},{""}};
int updateweater_time = 10; //天气更新时间  X 分钟
int LCD_Rotation = 0;   //LCD屏幕方向
int LCD_BL_PWM = 50;//屏幕亮度0-100，默认50（分时段开启时为“白天亮度”）
uint8_t TBl_en = 0;//分时段亮度开关 0关1开
uint8_t nightBl = 2;//夜间亮度（默认很暗）
uint8_t nightStart = 22, nightEnd = 7;//夜间时段：22点~次日7点
int appliedBl = -1;//当前已生效亮度，-1表示未知
uint32_t wxOkAt = 0;   //最近一次天气拉取成功时刻（millis），0 = 从未成功
int      wxLastCode = 0;//最近一次天气请求的 HTTP 码（0 = 还没请求过）
String sunRise = "";//日出 HH:MM（来源 t.weather.itboy.net，与 weather.com.cn 同城市码）
String sunSet  = "";//日落 HH:MM
String cityCode = "101250101";  //天气城市代码 长沙:101250101株洲:101250301衡阳:101250401
//----------------------------------------------------
//LCD屏幕相关设置
TFT_eSPI tft = TFT_eSPI();  // 引脚请自行配置tft_espi库中的 User_Setup.h文件
TFT_eSprite clk = TFT_eSprite(&tft);
#define LCD_BL_PIN 5    //LCD背光引脚
uint16_t bgColor = 0x0000;
//其余状态标志位
uint8_t Wifi_en = 1; //wifi状态标志位  1：打开    0：关闭
uint8_t UpdateWeater_en = 0; //更新时间标志位
int prevTime = 0;       //滚动显示更新标志位
uint8_t repaintScrollArea = 0;//整屏重绘标记：置1时scrollBanner先以背景色清空滚动区
//EEPROM参数存储地址位
int BL_addr = 1;//被写入数据的EEPROM地址编号  1亮度
int Ro_addr = 2; //被写入数据的EEPROM地址编号  2 旋转方向
//地址3已废弃（原DHT11使能标志），不再读写
int UpWeT_addr = 4; //4 更新时间记录
int TBl_addr = 5; //5 分时段亮度开关 0/1
int NB_addr = 6;  //6 夜间亮度 1-100
int NS_addr = 7;  //7 夜间开始小时 0-23
int NE_addr = 8;  //8 夜间结束小时 0-23（次日，可跨0点）
int CC_addr = 10;//被写入数据的EEPROM地址编号  10城市
int wifi_addr = 30; //被写入数据的EEPROM地址编号  30起 wifi-ssid-psw
int CN_addr = 128; //128 自定义城市名：[0]字节长度 [1..]UTF-8内容（最长24字节）
String cityNameSet = "";//自定义城市名（空则显示天气接口返回的城市名）
String apSsidStr = "";//当前AP配网热点名称（含芯片ID后缀）
time_t prevDisplay = 0;       //显示时间显示记录
unsigned long weaterTime = 0; //天气更新时间记录
String SMOD = "";//串口数据存储
/*** Component objects ***/
Number      dig;
WeatherNum  wrat;
uint32_t targetTime = 0;   
int tempnum = 0;   //温度百分比
int huminum = -1;  //室外湿度百分比（-1 = 无有效读数）
String wxTempTxt = "--";//最近一次解析到的室外温度文本，供网页诊断
int tempcol =0xffff;   //温度显示颜色
int humicol =0xffff;   //湿度显示颜色
//Web网站服务器
ESP8266WebServer server(80);// 建立esp8266网站服务器对象
DNSServer dnsServer;// AP模式下的DNS劫持：任何域名都解析到本机，手机连上热点即可自动弹出配网页
//NTP服务器参数
static const char ntpServerName[] = "ntp6.aliyun.com";
const int timeZone = 8;     //东八区
//wifi连接UDP设置参数
WiFiUDP Udp;
WiFiClient wificlient;
unsigned int localPort = 8000;
float duty=0;
//函数声明
time_t getNtpTime();
void digitalClockDisplay(int reflash_en);
void printDigits(int digits);
String num2str(int digits);
void sendNTPpacket(IPAddress &address);
void LCD_reflash(int en);
void savewificonfig();
void readwificonfig();
void deletewificonfig();
void saveCityCodetoEEP(int * citycode);
void readCityCodefromEEP(int * citycode);
void saveCityName();
void readCityName();
void Serial_set();
void loading(byte delayTime);
void getCityCode();
void getCityWeater();
void fetchSunTimes();
String tempOneDecimal(const String &raw);
int  humFromString(const String &raw);
void humidityWin(int pct);
void weaterData(String *cityDZ,String *dataSK,String *dataFC);
void scrollBanner();
void updateIpScroll();
extern String scrollText[9];//前向声明（updateIpScroll 早于定义使用）
void imgAnim();
String week();
String monthDay();
String lunarDayStr();
#if WebSever_EN
void webInit();
void webPoll();
void handleRoot();
void handleSave();
void handleSaveWifi();
void handleStatus();
void timeBrightness();
void handleScan();
void handleNotFound();
void handleOtaUpload();
void handleOtaDone();
void handleCaptive();
void applyLcdRotation(uint8_t r);
#endif
enum NetMode : uint8_t { NET_STA = 0, NET_AP = 1 };
NetMode netMode = NET_AP;
bool wifiConnectSta(uint32_t timeoutMs);
void wifiStartAp();
bool apRetryWait();
void netSetup();
void resetWifiConfig();
void netShowStaWin();
void netShowApWin();
/* *****************************************************************
 *  函数
 * *****************************************************************/
//读取保存城市代码
void saveCityCodetoEEP(int * citycode)
{
  for(int cnum=0;cnum<5;cnum++)
  {
    EEPROM.write(CC_addr+cnum,*citycode%100);//城市地址写入城市代码
    EEPROM.commit();//保存更改的数据
    *citycode = *citycode/100;
    delay(5);
  }
}
void readCityCodefromEEP(int * citycode)
{
  for(int cnum=5;cnum>0;cnum--)
  {          
    *citycode = *citycode*100;
    *citycode += EEPROM.read(CC_addr+cnum-1); 
    delay(5);
  }
}
//保存/读取自定义城市名（EEPROM 128起）
void saveCityName()
{
  uint8_t len = cityNameSet.length();
  if(len > 24) len = 24;
  EEPROM.write(CN_addr,len);
  for(uint8_t i=0;i<len;i++)
    EEPROM.write(CN_addr+1+i,cityNameSet[i]);
  EEPROM.commit();
}
void readCityName()
{
  cityNameSet = "";
  uint8_t len = EEPROM.read(CN_addr);
  if(len>0 && len<=24)
  {
    for(uint8_t i=0;i<len;i++)
    {
      char c = EEPROM.read(CN_addr+1+i);
      if(c) cityNameSet += c;
    }
  }
}
//wifi ssid，psw保存到eeprom
void savewificonfig()
{
  //开始写入
  uint8_t *p = (uint8_t*)(&wificonf);
  for (int i = 0; i < sizeof(wificonf); i++)
  {
    EEPROM.write(i + wifi_addr, *(p + i)); //在闪存内模拟写入
  }
  delay(10);
  EEPROM.commit();//执行写入ROM
  delay(10);
}
//删除原有eeprom中的信息
void deletewificonfig()
{
  config_type deletewifi ={{""},{""}};
  uint8_t *p = (uint8_t*)(&deletewifi);
  for (int i = 0; i < sizeof(deletewifi); i++)
  {
    EEPROM.write(i + wifi_addr, *(p + i)); //在闪存内模拟写入
  }
  delay(10);
  EEPROM.commit();//执行写入ROM
  delay(10);
}
//从eeprom读取WiFi信息ssid，psw
void readwificonfig()
{
  uint8_t *p = (uint8_t*)(&wificonf);
  for (int i = 0; i < sizeof(wificonf); i++)
  {
    *(p + i) = EEPROM.read(i + wifi_addr);
  }
  wificonf.stassid[sizeof(wificonf.stassid)-1] = '\0';
  wificonf.stapsw[sizeof(wificonf.stapsw)-1] = '\0';
  Serial.printf("Read WiFi Config.....\r\n");
  Serial.printf("SSID:%s\r\n",wificonf.stassid);
  Serial.printf("PSW:%s\r\n",wificonf.stapsw);
}
//TFT屏幕输出函数
bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap)
{
  if ( y >= tft.height() ) return 0;
  tft.pushImage(x, y, w, h, bitmap);
  // Return 1 to decode next block
  return 1;
}
//进度条函数
byte loadNum = 6;
void loading(byte delayTime)//绘制进度条
{
  clk.setColorDepth(8);
  
  clk.createSprite(200, 100);//创建窗口
  clk.fillSprite(0x0000);   //填充率
  clk.drawRoundRect(0,0,200,16,8,0xFFFF);       //空心圆角矩形
  clk.fillRoundRect(3,3,loadNum,10,5,0xFFFF);   //实心圆角矩形
  clk.setTextDatum(CC_DATUM);   //设置文本数据
  clk.setTextColor(TFT_GREEN, 0x0000); 
  clk.drawString("Connecting to WiFi......",100,40,2);
  clk.setTextColor(TFT_WHITE, 0x0000); 
  clk.drawRightString(Version,180,60,2);
  clk.pushSprite(20,120);  //窗口位置
  
  //clk.setTextDatum(CC_DATUM);
  //clk.setTextColor(TFT_WHITE, 0x0000); 
  //clk.pushSprite(130,180);
  
  clk.deleteSprite();
  loadNum += 1;
  delay(delayTime);
}
//湿度图标显示函数（室外湿度进度条）
void humidityWin(int pct)
{
  //内框可用宽度 50px（x=1..50），按 0-100 等比映射；pct<0（无有效读数）画空条
  int w = (pct < 0) ? 0 : (pct > 100 ? 50 : pct / 2);
  clk.setColorDepth(8);
  
  clk.createSprite(52, 6);  //创建窗口
  clk.fillSprite(0x0000);    //填充率
  clk.drawRoundRect(0,0,52,6,3,0xFFFF);  //空心圆角矩形  起始位x,y,长度，宽度，圆弧半径，颜色
  if(w > 0) clk.fillRoundRect(1,1,w,4,2,humicol);   //实心圆角矩形
  clk.pushSprite(L_HU_BAR_X,L_HU_BAR_Y);  //窗口位置
  clk.deleteSprite();
}
//温度图标显示函数（室外温度进度条）
void tempWin()
{
  clk.setColorDepth(8);
  
  clk.createSprite(52, 6);  //创建窗口
  clk.fillSprite(0x0000);    //填充率
  clk.drawRoundRect(0,0,52,6,3,0xFFFF);  //空心圆角矩形  起始位x,y,长度，宽度，圆弧半径，颜色
  clk.fillRoundRect(1,1,tempnum,4,2,tempcol);   //实心圆角矩形
  clk.pushSprite(L_TI_BAR_X,L_TI_BAR_Y);  //窗口位置
  clk.deleteSprite();
}
//web服务启动后LCD提示：内网访问地址（无mDNS，直接显示IP）
void netShowStaWin()
{
  IPAddress IP_adr = WiFi.localIP();
  clk.setColorDepth(8);

  clk.createSprite(200, 52);//创建窗口
  clk.fillSprite(0x0000);   //填充率

  clk.setTextDatum(CC_DATUM);   //设置文本数据
  clk.setTextColor(TFT_GREEN, 0x0000);
  clk.drawString("LAN Config:",70,10,2);
  clk.setTextColor(TFT_WHITE, 0x0000);
  clk.drawString(IP_adr.toString(),100,34,4);
  clk.pushSprite(20,40);  //窗口位置

  clk.deleteSprite();
}

//AP配网模式下LCD提示：热点名称与配置地址（开放热点无密码）
void netShowApWin()
{
  clk.setColorDepth(8);

  clk.createSprite(200, 72);//创建窗口
  clk.fillSprite(0x0000);   //填充率

  clk.setTextDatum(CC_DATUM);
  clk.setTextColor(TFT_GREEN, 0x0000);
  clk.drawString("WiFi Config Mode",100,9,2);
  clk.setTextColor(TFT_WHITE, 0x0000);
  clk.drawString(apSsidStr,100,31,2);
  clk.setTextColor(TFT_GREEN, 0x0000);
  clk.drawString("192.168.4.1",100,55,2);
  clk.pushSprite(20,40);  //窗口位置

  clk.deleteSprite();
}
//把当前Web访问IP写入滚动条第7条（城市下方、时间上方的轮播文字）
void updateIpScroll()
{
  if(netMode == NET_STA && WiFi.status() == WL_CONNECTED)
    scrollText[8] = WiFi.localIP().toString();
  else
    scrollText[8] = "192.168.4.1";
}
//串口调试设置函数
void Serial_set()
{
  String incomingByte = "";
  if(Serial.available()>0)
  {
    
    while(Serial.available()>0)//监测串口缓存，当有数据输入时，循环赋值给incomingByte
    {
      incomingByte += char(Serial.read());//读取单个字符值，转换为字符，并按顺序一个个赋值给incomingByte
      delay(2);//不能省略，因为读取缓冲区数据需要时间
    }    
    if(SMOD=="0x01")//设置1亮度设置
    {
      int LCDBL = atoi(incomingByte.c_str());//int n = atoi(xxx.c_str());//String转int
      if(LCDBL>=0 && LCDBL<=100)
      {
        EEPROM.write(BL_addr, LCDBL);//亮度地址写入亮度值
        EEPROM.commit();//保存更改的数据
        delay(5);
        LCD_BL_PWM = EEPROM.read(BL_addr); 
        delay(5);
        SMOD = "";
        Serial.printf("亮度调整为：");
        timeBrightness();//经分时段策略生效（夜间时段内仍显示夜间亮度）
        Serial.println(LCD_BL_PWM);
        Serial.println("");
      }
      else
        Serial.println("亮度调整错误，请输入0-100");
    } 
    if(SMOD=="0x02")//设置2地址设置
    {
      int CityCODE = 0;
      int CityC = atoi(incomingByte.c_str());//int n = atoi(xxx.c_str());//String转int
      if(CityC>=101000000 && CityC<=102000000 || CityC == 0)
      {
        saveCityCodetoEEP(&CityC);
        // for(int cnum=0;cnum<5;cnum++)
        // {
        //   EEPROM.write(CC_addr+cnum,CityC%100);//城市地址写入城市代码
        //   EEPROM.commit();//保存更改的数据
        //   CityC = CityC/100;
        //   delay(5);
        // }
        readCityCodefromEEP(&CityC);
        // for(int cnum=5;cnum>0;cnum--)
        // {          
        //   CityCODE = CityCODE*100;
        //   CityCODE += EEPROM.read(CC_addr+cnum-1); 
        //   delay(5);
        // }
        
        cityCode = CityCODE;
        
        if(cityCode == "0")
        {
          Serial.println("城市代码调整为：自动");
          getCityCode();  //获取城市代码
        }
        else
        {
          Serial.printf("城市代码调整为：");
          Serial.println(cityCode);
        }
        Serial.println("");
        getCityWeater();//更新城市天气  
        SMOD = "";
      }
      else
        Serial.println("城市调整错误，请输入9位城市代码，自动获取请输入0");
    }   
    if(SMOD=="0x03")//设置3屏幕显示方向
    {
      int RoSet = atoi(incomingByte.c_str());
      if(RoSet >= 0 && RoSet <= 3)
      {
        EEPROM.write(Ro_addr, RoSet);//屏幕方向地址写入方向值
        EEPROM.commit();//保存更改的数据
        SMOD = "";
      //设置屏幕方向后重新刷屏并显示
        repaintScrollArea = 1;//滚动区需先清空，避免残留开机画面
        applyLcdRotation(RoSet);
        Serial.print("屏幕方向设置为：");
        Serial.println(RoSet);
      }
      else 
      {
        Serial.println("屏幕方向值错误，请输入0-3内的值");
      }
    }
    if(SMOD=="0x04")//设置天气更新时间
    {
      int wtup = atoi(incomingByte.c_str());//int n = atoi(xxx.c_str());//String转int
      if(wtup>=1 && wtup<=60)
      {
        EEPROM.write(UpWeT_addr, wtup);//亮度地址写入亮度值
        EEPROM.commit();//保存更改的数据
        delay(5);
        updateweater_time = wtup;
        SMOD = "";
        Serial.printf("天气更新时间更改为：");
        Serial.print(updateweater_time);
        Serial.println("分钟");
      }
      else
        Serial.println("更新时间太长，请重新设置（1-60）");
    } 
    else
    {
      SMOD = incomingByte;
      delay(2);
      if(SMOD=="0x01")
        Serial.println("请输入亮度值，范围0-100");
      else if(SMOD=="0x02")
        Serial.println("请输入9位城市代码，自动获取请输入0"); 
      else if(SMOD=="0x03")
      {
        Serial.println("请输入屏幕方向值，"); 
        Serial.println("0-USB接口朝下");
        Serial.println("1-USB接口朝右");
        Serial.println("2-USB接口朝上");
        Serial.println("3-USB接口朝左");
      }
      else if(SMOD=="0x04")
      {
        Serial.print("当前天气更新时间："); 
        Serial.print(updateweater_time);
        Serial.println("分钟");
        Serial.println("请输入天气更新时间（1-60）分钟"); 
      }
      else if(SMOD=="0x05")
      {
        Serial.println("重置WiFi设置中......");
        delay(10);
        deletewificonfig();
        delay(50);
        Serial.println("重置WiFi成功，重启进入AP配网模式");
        SMOD = "";
        ESP.restart();
      }
      else
      {
        Serial.println("");
        Serial.println("请输入需要修改的代码：");
        Serial.println("亮度设置输入        0x01");
        Serial.println("地址设置输入        0x02");
        Serial.println("屏幕方向设置输入    0x03");
        Serial.println("更改天气更新时间    0x04");
        Serial.println("重置WiFi(会重启)    0x05");
        Serial.println("");
      }
    }
  }
}
#if WebSever_EN
/* *****************************************************************
 *  Web服务：内网设置页面 + REST接口（页面见 webui.h）
 * *****************************************************************/
//应用屏幕方向并全屏重绘（串口、网页共用）
void applyLcdRotation(uint8_t r)
{
  LCD_Rotation = r;
  tft.setRotation(LCD_Rotation);
  tft.fillScreen(0x0000);
  LCD_reflash(1);//屏幕刷新程序
  UpdateWeater_en = 1;
  TJpgDec.drawJpg(L_TI_ICON_X,L_TI_ICON_Y,temperature, sizeof(temperature));  //温度图标
  TJpgDec.drawJpg(L_HU_ICON_X,L_HU_ICON_Y,humidity, sizeof(humidity));  //湿度图标
}
//web设置保存（亮度/方向/城市代码/城市名称/天气更新时间/分时段亮度）
//任一字段越界时返回 {"ok":0,"err":"<字段>"}，网页据此给出具体提示，不再一句“保存失败”糊过去
void handleSave()
{
  if(!server.hasArg("bl") && !server.hasArg("rot") && !server.hasArg("cc") && !server.hasArg("cn") && !server.hasArg("upt"))
  {
    server.send(400, "text/plain", "no args");
    return;
  }
  const char *err = NULL;   //首个越界字段，只报第一个
  bool refreshWeather = false;
  bool rotChanged = false;
  int newRot = LCD_Rotation;

  if(server.hasArg("bl"))
  {
    int v = server.arg("bl").toInt();
    if(v < 1 || v > 100) err = "bl";
    else if(v != LCD_BL_PWM)
    {
      LCD_BL_PWM = v;
      EEPROM.write(BL_addr, LCD_BL_PWM);
      EEPROM.commit();
      timeBrightness();
      Serial.printf("Web: bl=%d\r\n", LCD_BL_PWM);
    }
  }
  if(server.hasArg("upt"))
  {
    int v = server.arg("upt").toInt();
    if(v < 1 || v > 60)
    {
      if(!err) err = "upt";
    }
    else if(v != updateweater_time)
    {
      updateweater_time = v;
      EEPROM.write(UpWeT_addr, updateweater_time);
      EEPROM.commit();
      Serial.printf("Web: upt=%d\r\n", updateweater_time);
    }
  }
  if(server.hasArg("tbl"))//分时段亮度开关
  {
    int e = server.arg("tbl").toInt();
    if(e != 0 && e != 1)
    {
      if(!err) err = "tbl";
    }
    else if((uint8_t)e != TBl_en)
    {
      TBl_en = (uint8_t)e;
      EEPROM.write(TBl_addr, TBl_en);
      EEPROM.commit();
      timeBrightness();
      Serial.printf("Web: tbl=%d\r\n", TBl_en);
    }
  }
  if(server.hasArg("nbl"))//夜间亮度
  {
    int v = server.arg("nbl").toInt();
    if(v < 1 || v > 100)
    {
      if(!err) err = "nbl";
    }
    else if(v != nightBl)
    {
      nightBl = v;
      EEPROM.write(NB_addr, nightBl);
      EEPROM.commit();
      timeBrightness();
      Serial.printf("Web: nbl=%d\r\n", nightBl);
    }
  }
  if(server.hasArg("nst"))//夜间开始小时
  {
    int v = server.arg("nst").toInt();
    if(v < 0 || v > 23)
    {
      if(!err) err = "nst";
    }
    else if(v != nightStart)
    {
      nightStart = v;
      EEPROM.write(NS_addr, nightStart);
      EEPROM.commit();
      timeBrightness();
    }
  }
  if(server.hasArg("net"))//夜间结束小时
  {
    int v = server.arg("net").toInt();
    if(v < 0 || v > 23)
    {
      if(!err) err = "net";
    }
    else if(v != nightEnd)
    {
      nightEnd = v;
      EEPROM.write(NE_addr, nightEnd);
      EEPROM.commit();
      timeBrightness();
    }
  }
  if(server.hasArg("cc"))//城市代码：7 位数字且落在 weather.com.cn 的号段
  {
    int cc = server.arg("cc").toInt();
    if(cc < 101000000 || cc > 102000000)
    {
      if(!err) err = "cc";
    }
    else if(String(cc) != cityCode)
    {
      cityCode = cc;
      int ccw = cc;
      saveCityCodetoEEP(&ccw);
      Serial.printf("Web: city=%s\r\n", cityCode.c_str());
      refreshWeather = true;
    }
  }
  if(server.hasArg("cn"))
  {
    String cn = server.arg("cn");
    cn.trim();
    if(cn.length() > 24)
    {
      if(!err) err = "cn";
    }
    else if(cn != cityNameSet)
    {
      cityNameSet = cn;
      saveCityName();
      Serial.printf("Web: cityName=%s\r\n", cityNameSet.c_str());
      refreshWeather = true;//借天气刷新重绘城市名
    }
  }
  if(server.hasArg("rot"))
  {
    int v = server.arg("rot").toInt();
    if(v < 0 || v > 3)
    {
      if(!err) err = "rot";
    }
    else if(v != LCD_Rotation)
    {
      EEPROM.write(Ro_addr, v);
      EEPROM.commit();
      newRot = v;
      rotChanged = true;
    }
  }

  if(err)
  {
    char b[48];
    snprintf(b, sizeof(b), "{\"ok\":0,\"err\":\"%s\"}", err);
    server.send(200, "application/json", b);
    Serial.printf("Web: reject %s\r\n", err);
    return;
  }
  server.send(200, "application/json", rotChanged ? "{\"ok\":1,\"rot\":1}" : "{\"ok\":1}");
  if(refreshWeather)
    getCityWeater();//立即刷新天气/城市名显示
  if(rotChanged)
    applyLcdRotation(newRot);//先回应，再重绘屏幕
}
//wifi配网保存：写入EEPROM后重启生效
void handleSaveWifi()
{
  String ssid = server.arg("ssid");
  String psw  = server.arg("psw");
  ssid.trim();
  if(ssid.length() == 0 || ssid.length() > 31 || psw.length() > 63)
  {
    server.send(200, "application/json", "{\"ok\":0,\"msg\":\"ssid/psw length invalid\"}");
    return;
  }
  memset(&wificonf, 0, sizeof(wificonf));
  strncpy(wificonf.stassid, ssid.c_str(), sizeof(wificonf.stassid)-1);
  strncpy(wificonf.stapsw,  psw.c_str(),  sizeof(wificonf.stapsw)-1);
  savewificonfig();
  Serial.print("Web: WiFi saved, connect to ");
  Serial.println(ssid);
  server.send(200, "application/json", "{\"ok\":1}");
  delay(300);//等待响应发出
  ESP.restart();
}
//设备状态JSON
void handleStatus()
{
  StaticJsonDocument<1024> doc;
  doc["mode"] = (netMode == NET_STA) ? 0 : 1;
  doc["ip"]   = (netMode == NET_STA) ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
  doc["mac"]  = WiFi.macAddress();
  doc["ssid"] = (netMode == NET_STA) ? WiFi.SSID() : apSsidStr;
  doc["rssi"] = (netMode == NET_STA) ? WiFi.RSSI() : 0;
  doc["ver"]  = Version;
  doc["up"]   = (uint32_t)(millis()/1000);
  doc["bl"]   = LCD_BL_PWM;
  doc["rot"]  = LCD_Rotation;
  doc["city"] = cityCode;
  doc["cn"]   = cityNameSet;
  doc["upt"]  = updateweater_time;
  doc["tbl"]  = TBl_en;
  doc["nbl"]  = nightBl;
  doc["nst"]  = nightStart;
  doc["net"]  = nightEnd;
  //诊断信息：排查“天气不更新/内存不够”这类问题时不用再连串口
  doc["heap"] = ESP.getFreeHeap();
  doc["frag"] = ESP.getHeapFragmentation();
  doc["chip"] = ESP.getChipId();
  doc["flash"]= ESP.getFlashChipRealSize()/1024;
  doc["sketch"]=(uint32_t)(ESP.getSketchSize()/1024);
  doc["wxAge"]= wxOkAt ? (uint32_t)((millis()-wxOkAt)/1000) : 0xFFFFFFFF;//秒；0xFFFFFFFF=从未成功
  doc["wxErr"]= wxLastCode;
  doc["rise"]  = sunRise;
  doc["set"]   = sunSet;
  doc["temp"]  = wxTempTxt;//屏幕上的温度读数
  doc["hum"]   = (huminum < 0) ? String("-") : (String(huminum) + "%");//屏幕上的湿度读数
  char buf[768];//字段变多，留足
  size_t n = serializeJson(doc, buf);
  server.send(200, "application/json", buf, n);
}
//扫描周边热点（AP模式下短暂切换为AP+STA再扫描）
void handleScan()
{
  WiFiMode_t oldMode = WiFi.getMode();
  if(oldMode == WIFI_AP)
  {
    WiFi.mode(WIFI_AP_STA);
    delay(120);
  }
  int n = WiFi.scanNetworks(false, false);
  DynamicJsonDocument doc(1024);
  doc["ok"] = 1;
  JsonArray arr = doc.createNestedArray("list");
  for(int i = 0; i < n && i < 10; i++)
  {
    JsonObject o = arr.createNestedObject();
    String s = WiFi.SSID(i);
    o["ssid"] = s;
    o["rssi"] = WiFi.RSSI(i);
    o["ch"]   = WiFi.channel(i);
  }
  WiFi.scanDelete();
  if(oldMode == WIFI_AP)
    WiFi.mode(WIFI_AP);
  char buf[1024];
  size_t len = serializeJson(doc, buf);
  server.send(200, "application/json", buf, len);
}
/*------------- OTA 固件上传升级（POST /update，multipart/form-data） -------------*/
//固件槽上限：与 platformio.ini 用的 eagle.flash.4m1m.ld 对应（sketch 槽 1044464B，留点余量）
#define OTA_SKETCH_MAX  1040000UL
//上传状态：0未开始 1写入中 2成功 3失败
static uint8_t otaState = 0;
static uint8_t otaErrCode = 0;
static uint32_t otaWritten = 0;
static int8_t  otaShownPct = -1;
//是否正在上传固件（配网重试、其他页面请求据此避让）
static bool otaActive() { return otaState == 1; }
//升级进度画在屏幕上（内置字体，只写数字，不占中文字库内存）
static void otaScreen(int pct)
{
  if(pct < 0) pct = 0;
  if(pct > 100) pct = 100;
  const int w = 180;
  const int x = (tft.width() - w) / 2;
  const int y = 206;
  tft.fillRect(x - 2, y - 2, w + 4, 24, 0x0000);
  tft.drawRect(x, y, w, 20, 0xFFFF);
  if(pct > 0)
    tft.fillRect(x + 2, y + 2, (w - 4) * pct / 100, 16, 0x07E0);
  char b[8];
  snprintf(b, sizeof(b), "%d%%", pct);
  tft.setTextColor(0xFFFF, 0x0000);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(b, tft.width() / 2, y + 10, 2);
  tft.setTextDatum(TL_DATUM);
  otaShownPct = pct;
}
//上传数据回调（server.upload()）：分块把收到的固件写进flash末尾暂存区
void handleOtaUpload()
{
  HTTPUpload &up = server.upload();
  if(up.status == UPLOAD_FILE_START)
  {
    otaState = 0;
    otaErrCode = 0;
    otaWritten = 0;
    otaShownPct = -1;
    Serial.print("OTA: start, file=");
    Serial.println(up.filename);
    if(!up.filename.endsWith(".bin"))//只收 .bin 固件
    {
      otaState = 3;
      otaErrCode = 0xFD;
      Serial.println("OTA: reject, not a .bin file");
      return;
    }
    //暂存区大小与sketch槽取小者：暂存区不够写不下，槽太小则重启拷回时越界
    uint32_t stageMax = (ESP.getFreeSketchSpace() - 0x1000) & 0xFFFFF000;
    uint32_t maxSize  = (stageMax < OTA_SKETCH_MAX) ? stageMax : OTA_SKETCH_MAX;
    if(up.contentLength && up.contentLength > maxSize + 2048)//2048给multipart头部留量
    {
      otaState = 3;
      otaErrCode = 0xFC;
      Serial.print("OTA: too large, need ");
      Serial.println(up.contentLength);
      return;
    }
    if(!Update.begin(maxSize, U_FLASH))
    {
      otaState = 3;
      otaErrCode = Update.getError();
      Serial.print("OTA: begin failed, err=");
      Serial.println(otaErrCode);
      return;
    }
    otaState = 1;
    otaScreen(0);
  }
  else if(up.status == UPLOAD_FILE_WRITE)
  {
    if(otaState != 1 || up.currentSize == 0) return;
    if(otaWritten == 0 && up.buf[0] != 0xE9)//ESP8266固件头魔数，挡住选错文件
    {
      Update.end();//未完成即结束＝放弃本次升级
      otaState = 3;
      otaErrCode = 0xFB;
      Serial.println("OTA: bad image header, not ESP8266 firmware");
      return;
    }
    if(Update.write(up.buf, up.currentSize) != up.currentSize)
    {
      otaErrCode = Update.getError();
      Update.printError(Serial);
      Update.end();
      otaState = 3;
      return;
    }
    otaWritten += up.currentSize;
    int pct = up.contentLength ? (int)((uint64_t)otaWritten * 100 / up.contentLength) : 0;
    if(pct > 99) pct = 99;
    if(pct != otaShownPct) otaScreen(pct);
  }
  else if(up.status == UPLOAD_FILE_END)
  {
    if(otaState != 1) return;
    if(Update.end(true))
    {
      otaState = 2;
      Serial.print("OTA: staged ");
      Serial.print(otaWritten);
      Serial.println(" bytes");
    }
    else
    {
      otaErrCode = Update.getError();
      Update.printError(Serial);
      otaState = 3;
    }
  }
  else if(up.status == UPLOAD_FILE_ABORTED)
  {
    Update.end();
    if(otaState == 1)
    {
      otaState = 3;
      otaErrCode = 0xFE;
    }
    Serial.println("OTA: aborted by client");
  }
}
//上传收尾：成功则回响应后重启，由EBoot把暂存镜像拷到0地址
void handleOtaDone()
{
  server.sendHeader("Connection", "close");
  if(otaState == 2)
  {
    otaScreen(100);
    server.send(200, "application/json", "{\"ok\":1}");
    Serial.println("OTA: reboot to apply");
    delay(800);//先把响应发完再重启
    ESP.restart();
    return;
  }
  char b[64];
  snprintf(b, sizeof(b), "{\"ok\":0,\"code\":%u}", (unsigned)otaErrCode);
  server.send(200, "application/json", b);
}
//Captive portal 探测请求：一律 302 回首页；AP 模式下首页就是配网页
void handleCaptive()
{
  server.sendHeader("Location", "http://192.168.4.1/", true);
  server.send(302, "text/plain", "");
}
//设置主页：配网模式只给WiFi配网页；已联网模式给显示设置页（不再出现WiFi相关内容）
void handleRoot()
{
  //禁缓存：升级后浏览器不必手动强刷才能看到新页面
  server.sendHeader("Cache-Control", "no-store, must-revalidate");
  server.send_P(200, "text/html; charset=utf-8",
                (netMode == NET_STA) ? SDD_PAGE : SDD_PAGE_AP);
}
//404
void handleNotFound()
{
  server.send_P(404, "text/plain; charset=utf-8", PSTR("404 Not Found"));
}
//Web服务初始化：路由注册（STA内网IP或AP热点IP直接访问，不使用mDNS）
void webInit()
{
  server.on("/", HTTP_GET, handleRoot);
  server.on("/save", HTTP_POST, handleSave);
  server.on("/save_wifi", HTTP_POST, handleSaveWifi);
  server.on("/api/status", HTTP_GET, handleStatus);
  server.on("/api/scan", HTTP_GET, handleScan);
  server.on("/update", HTTP_POST, handleOtaDone, handleOtaUpload);//OTA固件上传
  //Captive portal：各家系统的连通性探测路径统一跳到配网页，连上热点即自动弹窗
  server.on("/generate_204", HTTP_GET, handleCaptive);//Android
  server.on("/gen_204", HTTP_GET, handleCaptive);
  server.on("/hotspot-detect.html", HTTP_GET, handleCaptive);//iOS / macOS
  server.on("/connecttest.txt", HTTP_GET, handleCaptive);//Windows
  server.on("/ncsi.txt", HTTP_GET, handleCaptive);
  server.on("/redirect", HTTP_GET, handleCaptive);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("HTTP server started");
  Serial.print("URL: http://");
  Serial.println(WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : WiFi.softAPIP().toString());
}
//loop中轮询web服务
void webPoll()
{
  server.handleClient();
  if(netMode == NET_AP) dnsServer.processNextRequest();//AP模式持续响应DNS查询，否则弹页会失败
}
/*------------- 联网流程（自写配网，不再依赖WiFiManager） -------------*/
//以EEPROM中保存的凭据连接路由器，成功后保存到EEPROM
bool wifiConnectSta(uint32_t timeoutMs)
{
  WiFi.persistent(false);//凭据由本程序自行保存到EEPROM，禁用SDK自动写flash
  WiFi.mode(WIFI_STA);
  WiFi.begin(wificonf.stassid, wificonf.stapsw);
  uint32_t startt = millis();
  while(WiFi.status() != WL_CONNECTED)
  {
    loading(30);//屏幕进度条动画（内含delay，兼喂狗）
    if(millis() - startt > timeoutMs)
    {
      WiFi.disconnect();
      return false;
    }
  }
  //连接成功后如EEPROM中的凭据与实际不符才写入，减少flash磨损
  if(strcmp(wificonf.stassid, WiFi.SSID().c_str()) != 0 || strcmp(wificonf.stapsw, WiFi.psk().c_str()) != 0)
  {
    strcpy(wificonf.stassid, WiFi.SSID().c_str());
    strcpy(wificonf.stapsw,  WiFi.psk().c_str());
    savewificonfig();
  }
  return true;
}
//无可用凭据或凭据错误时，清除保存的WiFi信息并重启
void resetWifiConfig()
{
  deletewificonfig();
  delay(50);
  ESP.restart();
}
//启动AP配网热点
void wifiStartAp()
{
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP);
  apSsidStr = String(AP_SSID_PREFIX) + String(ESP.getChipId() & 0xFFFF, HEX);
  WiFi.softAP(apSsidStr.c_str());//开放热点，免密直连
  dnsServer.start(53, "*", WiFi.softAPIP());//所有域名解析到本机，配合探测跳转实现自动弹页
  Serial.print("AP started: ");
  Serial.println(apSsidStr);
}
//联网总入口：先试连已保存WiFi，失败或无凭据则开AP配网；两种模式下Web服务均运行
void netSetup()
{
  readwificonfig();
  bool connected = false;
  if(strlen(wificonf.stassid) > 0)
    connected = wifiConnectSta(STA_CONNECT_TIMEOUT);
  while(loadNum < 194)//让开机动画走完
    loading(1);
  updateIpScroll();//滚动条先填入当前访问IP（天气拉取失败也能轮播显示）
  if(connected)
  {
    netMode = NET_STA;
    Serial.print("WiFi connected, SSID: ");
    Serial.println(WiFi.SSID());
    #if WebSever_EN
    webInit();
    netShowStaWin();
    delay(10000);//停留展示配置地址
    #endif
  }
  else
  {
    netMode = NET_AP;
    wifiStartAp();
    #if WebSever_EN
    webInit();
    #endif
    netShowApWin();
    Serial.println("Waiting WiFi config on AP mode...");
    //连不上WiFi时停留在配网界面：每30秒静默重试一次已保存的WiFi，成功后才进入主界面
    while(!apRetryWait())
      ;
    netMode = NET_STA;
    updateIpScroll();
  }
}
//AP界面下的等待-重试循环：期间持续响应Web配网请求；每隔30秒尝试连接已保存的WiFi
//返回true表示已连上（此时自动切回STA模式并关闭热点）
bool apRetryWait()
{
  uint32_t startt = millis();
  while(millis() - startt < AP_RETRY_INTERVAL)
  {
    webPoll();
    delay(10);
    yield();
  }
  //固件升级上传中：本轮不切WiFi模式、不重试STA连接，否则会把配网连接打断、上传作废
  if(otaActive())
    return false;
  if(strlen(wificonf.stassid) == 0)
    return false;
  WiFi.persistent(false);
  WiFi.mode(WIFI_AP_STA);
  WiFi.begin(wificonf.stassid, wificonf.stapsw);
  startt = millis();
  while(WiFi.status() != WL_CONNECTED)
  {
    webPoll();
    delay(10);
    yield();
    if(millis() - startt > STA_CONNECT_TIMEOUT)
    {
      WiFi.disconnect();//断开失败的STA尝试
      wifiStartAp();//恢复配网热点
      return false;
    }
  }
  dnsServer.stop();//关闭热点同时收起DNS劫持
  WiFi.mode(WIFI_STA);//已连上路由器，关闭AP热点
  return true;
}
#endif
/* 背光控制：板上恒流电路为低有效，analogWrite 值越大越暗，1023 = 全灭 */
static void blSet(int pct)
{
  if(pct < 0)   pct = 0;
  if(pct > 100) pct = 100;
  analogWrite(LCD_BL_PIN, 1023 - pct * 10);
  appliedBl = pct;
}
static void blOff()
{
  analogWrite(LCD_BL_PIN, 1023);
  appliedBl = -1;//标记未知，之后任何亮度都会重新写入
}
//渐亮：面板已出图后再拉背光，避免“啪”一下亮起的突兀感
static void blFadeOn(int pct)
{
  int target = pct;
  int from = target / 4;//从 1/4 亮度起步，弱到几乎看不出跳变
  for(int i = 0; i <= 8; i++)
  {
    blSet(from + (target - from) * i / 8);
    delay(14);
  }
  blSet(target);
}
void setup()
{
  //上电第一件事必须是关背光：面板此刻显存为随机值，背光早亮就是一屏雪花
  pinMode(LCD_BL_PIN, OUTPUT);
  blOff();
  Serial.begin(115200);
  EEPROM.begin(1024);
 //从eeprom读取背光亮度设置
  if(EEPROM.read(BL_addr)>0&&EEPROM.read(BL_addr)<100)
    LCD_BL_PWM = EEPROM.read(BL_addr); 
  //从eeprom读取分时段亮度设置（未配置过时为0xFF，越界则用默认值）
  if(EEPROM.read(TBl_addr) <= 1) TBl_en = EEPROM.read(TBl_addr);
  if(EEPROM.read(NB_addr) >= 1 && EEPROM.read(NB_addr) <= 100) nightBl = EEPROM.read(NB_addr);
  if(EEPROM.read(NS_addr) <= 23) nightStart = EEPROM.read(NS_addr);
  if(EEPROM.read(NE_addr) <= 23) nightEnd = EEPROM.read(NE_addr);
  //注意：此处不能点背光，等面板初始化并清屏完成后再亮（见下方 blFadeOn）
  //从eeprom读取屏幕方向设置
  if(EEPROM.read(Ro_addr)>=0&&EEPROM.read(Ro_addr)<=3)
    LCD_Rotation = EEPROM.read(Ro_addr);
  
  //从eeprom读取天气更新时间（未配置过时为0xFF，越界则用默认值）
  if(EEPROM.read(UpWeT_addr)>=1 && EEPROM.read(UpWeT_addr)<=60)
    updateweater_time = EEPROM.read(UpWeT_addr);
  readCityName();//自定义城市名
  fsBegin();//挂载 LittleFS 并检查贴图/字库素材
  
  
  tft.begin(); /* TFT init */
  tft.invertDisplay(1);//反转所有显示颜色：1反转，0正常
  tft.setRotation(LCD_Rotation);
  tft.fillScreen(0x0000);
  tft.fillScreen(0x0000);//清两遍，确保整帧黑场稳定
  tft.setTextColor(TFT_BLACK, bgColor);
  blFadeOn(LCD_BL_PWM);//面板已出图，此时才点亮背光；夜间亮度由 loop 里的 timeBrightness 接管
  targetTime = millis() + 1000; 
  TJpgDec.setJpgScale(1);
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(tft_output);
  #if WebSever_EN
  netSetup();//联网（内网STA或AP配网）并启动Web服务
  #endif
  Serial.println("启动UDP");
  Udp.begin(localPort);
  Serial.println("等待同步...");
  setSyncProvider(getNtpTime);
  setSyncInterval(300);
  
  int CityCODE = 0;
  readCityCodefromEEP(&CityCODE);
  if(CityCODE>=101000000 && CityCODE<=102000000) 
    cityCode = CityCODE;  
  else
    getCityCode();  //获取城市代码
   
  tft.fillScreen(TFT_BLACK);//清屏
  repaintScrollArea = 1;//滚动区残留开机画面，先清掉
  
  TJpgDec.drawJpg(L_TI_ICON_X,L_TI_ICON_Y,temperature, sizeof(temperature));  //温度图标
  TJpgDec.drawJpg(L_HU_ICON_X,L_HU_ICON_Y,humidity, sizeof(humidity));  //湿度图标
  getCityWeater();
}
void loop()
{
  webPoll();//内网Web设置服务
  timeBrightness();//分时段亮度
  LCD_reflash(0);
  Serial_set();
}
//夜间时段判断：nightStart点整 至 次日nightEnd点整；起止相同视为不启用
static bool inNightWindow(uint8_t h)
{
  if(nightStart == nightEnd) return false;
  if(nightStart < nightEnd)  return (h >= nightStart && h < nightEnd);
  return (h >= nightStart || h < nightEnd);//跨0点
}
//按分时段策略应用背光亮度（每轮询调用，仅亮度变化时写引脚）
void timeBrightness()
{
  int want = (TBl_en && inNightWindow((uint8_t)hour())) ? (int)nightBl : (int)LCD_BL_PWM;
  if(want != appliedBl)
    blSet(want);
}
void LCD_reflash(int en)
{
  if (now() != prevDisplay || en == 1) 
  {
    prevDisplay = now();
    digitalClockDisplay(en);
    prevTime=0;  
  }
  
  //两秒钟更新一次滚动信息
  if(second()%2 ==0&& prevTime == 0 || en == 1){
    scrollBanner();
  }
#if imgAst_EN
  imgAnim();//太空人动画常驻
#endif
  if(millis() - weaterTime > (60000*updateweater_time) || en == 1 || UpdateWeater_en != 0){ //按设置间隔更新天气
    if(Wifi_en == 0)
    {
      WiFi.forceSleepWake();//wifi on
      Serial.println("WIFI恢复......");
      Wifi_en = 1;
    }
    if(WiFi.status() == WL_CONNECTED)
    {
      getCityWeater();
      if(UpdateWeater_en != 0) UpdateWeater_en = 0;
      weaterTime = millis();
      getNtpTime();//顺带校时
    }
  }
}
// 发送HTTP请求并且将服务器响应通过串口输出
void getCityCode(){
 String URL = "http://wgeo.weather.com.cn/ip/?_="+String(now());
  //创建 HTTPClient 对象
  HTTPClient httpClient;
 
  //配置请求地址。此处也可以不使用端口号和PATH而单纯的
  httpClient.begin(wificlient,URL); 
  
  //设置请求头中的User-Agent
  httpClient.setUserAgent("Mozilla/5.0 (iPhone; CPU iPhone OS 11_0 like Mac OS X) AppleWebKit/604.1.38 (KHTML, like Gecko) Version/11.0 Mobile/15A372 Safari/604.1");
  httpClient.addHeader("Referer", "http://www.weather.com.cn/");
 
  //启动连接并发送HTTP请求
  int httpCode = httpClient.GET();
  Serial.print("Send GET request to URL: ");
  Serial.println(URL);
  
  //如果服务器响应OK则从服务器获取响应体信息并通过串口输出
  if (httpCode == HTTP_CODE_OK) {
    String str = httpClient.getString();
    
    int aa = str.indexOf("id=");
    if(aa>-1)
    {
       //cityCode = str.substring(aa+4,aa+4+9).toInt();
       cityCode = str.substring(aa+4,aa+4+9);
       Serial.println(cityCode); 
       getCityWeater();
    }
    else
    {
      Serial.println("获取城市代码失败");  
    }
    
    
  } else {
    Serial.println("请求城市代码错误：");
    Serial.println(httpCode);
  }
 
  //关闭ESP8266与服务器连接
  httpClient.end();
}
//把温度统一成一位小数：接口有时给 "14.6"，有时给 "15"，负数与缺省都要兜住
String tempOneDecimal(const String &raw)
{
  String t = raw;
  t.trim();
  if(t.length() == 0) return "--";
  int dot = t.indexOf('.');
  if(dot < 0) return t + ".0";
  //只保留一位小数
  String ip = t.substring(0, dot);
  String fp = t.substring(dot + 1);
  if(fp.length() == 0) fp = "0";
  return ip + "." + fp.substring(0, 1);
}
//湿度接口给的是 "93%" 这种带单位的字符串，取前面的整数并限制在 0-100；
//认不出数字返回 -1（屏幕显示 "--"、条形画空），不再让 "100%" 被截成 10 这种错值
int humFromString(const String &raw)
{
  int v = -1;
  for(size_t i = 0; i < raw.length(); i++)
  {
    char c = raw[i];
    if(c >= '0' && c <= '9')
    {
      if(v < 0) v = 0;
      v = v * 10 + (c - '0');
      if(v > 100) { v = 100; break; }//三位封顶，后面再有数字也不再算
    }
    else if(v >= 0)
      break;                        //数字后遇到 % 或其他字符即止
  }
  return v;
}
//取日出/日落：weather.com.cn weather_index 里没有这两个字段，改用同城市码的免费接口
void fetchSunTimes()
{
  String URL = "http://t.weather.itboy.net/api/weather/city/" + cityCode;
  HTTPClient httpClient;
  httpClient.begin(wificlient, URL);
  httpClient.setUserAgent("Mozilla/5.0");
  httpClient.setTimeout(5000);//取不到就放弃，不拖住屏幕刷新
  int code = httpClient.GET();
  if(code == HTTP_CODE_OK)
  {
    String body = httpClient.getString();
    int i = body.indexOf("\"sunrise\":\"");
    if(i > 0)
    {
      String v = body.substring(i + 11, body.indexOf('"', i + 11));
      if(v.length() == 5) sunRise = v;
    }
    i = body.indexOf("\"sunset\":\"");
    if(i > 0)
    {
      String v = body.substring(i + 10, body.indexOf('"', i + 10));
      if(v.length() == 5) sunSet = v;
    }
    Serial.printf("日出 %s 日落 %s\r\n", sunRise.c_str(), sunSet.c_str());
  }
  else
  {
    Serial.printf("取日出日落失败: %d\r\n", code);
  }
  httpClient.end();
}
// 获取城市天气
void getCityWeater(){
 //String URL = "http://d1.weather.com.cn/dingzhi/" + cityCode + ".html?_="+String(now());//新
 String URL = "http://d1.weather.com.cn/weather_index/" + cityCode + ".html?_="+String(now());//原来
  //创建 HTTPClient 对象
  HTTPClient httpClient;
  
  httpClient.begin(wificlient,URL); 
  
  //设置请求头中的User-Agent
  httpClient.setUserAgent("Mozilla/5.0 (iPhone; CPU iPhone OS 11_0 like Mac OS X) AppleWebKit/604.1.38 (KHTML, like Gecko) Version/11.0 Mobile/15A372 Safari/604.1");
  httpClient.addHeader("Referer", "http://www.weather.com.cn/");
 
  //启动连接并发送HTTP请求
  int httpCode = httpClient.GET();
  Serial.println("正在获取天气数据");
  Serial.println(URL);
  
  //如果服务器响应OK则从服务器获取响应体信息并通过串口输出
  if (httpCode == HTTP_CODE_OK) {
    String str = httpClient.getString();
    int indexStart = str.indexOf("weatherinfo\":");
    int indexEnd = str.indexOf("};var alarmDZ");
    String jsonCityDZ = str.substring(indexStart+13,indexEnd);
    //Serial.println(jsonCityDZ);
    indexStart = str.indexOf("dataSK =");
    indexEnd = str.indexOf(";var dataZS");
    String jsonDataSK = str.substring(indexStart+8,indexEnd);
    //Serial.println(jsonDataSK);
    
    indexStart = str.indexOf("\"f\":[");
    indexEnd = str.indexOf(",{\"fa");
    String jsonFC = str.substring(indexStart+5,indexEnd);
    //Serial.println(jsonFC);
    
    fetchSunTimes();//日出日落单取一次，随后由 weaterData 写进滚动条
    weaterData(&jsonCityDZ,&jsonDataSK,&jsonFC);
    wxOkAt = millis();
    Serial.println("获取成功");
    
  } else {
    wxLastCode = httpCode;
    Serial.println("请求城市天气错误：");
    Serial.print(httpCode);
  }
 
  //关闭ESP8266与服务器连接
  httpClient.end();
}
String scrollText[9];//天气信息存储（0今日 1日出 2日落 3风 4最低 5最高 6湿度 7备用 8IP）
//天气信息写到屏幕上
void weaterData(String *cityDZ,String *dataSK,String *dataFC)
{
  //解析第一段JSON（注意：deserializeJson会清空文档，各段必须用独立文档，否则后解析会使前面的JsonObject取到null）
  DynamicJsonDocument doc(1024);
  deserializeJson(doc, *dataSK);
  JsonObject sk = doc.as<JsonObject>();
  //TFT_eSprite clkb = TFT_eSprite(&tft);
  
  /***绘制相关文字***/
  clk.setColorDepth(8);
  clk.loadFont(ZdyLwFont_20);
  
  //温度（接口返回的是 14.6 这类一位小数，统一格式化后再显示；数值右对齐，与网页设置页的“标签—数值”一致）
  String tempRaw = sk["temp"].as<String>();
  String tempTxt = tempOneDecimal(tempRaw) + "℃";
  wxTempTxt = tempTxt;
  clk.createSprite(L_VAL_W, 24); 
  clk.fillSprite(bgColor);
  clk.setTextDatum(MR_DATUM);
  clk.setTextColor(TFT_WHITE, bgColor); 
  clk.drawString(tempTxt, L_VAL_W - 2, 12);
  clk.pushSprite(L_TI_VAL_X,L_TI_VAL_Y);
  tempnum = (int)atof(tempRaw.c_str());
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
  else
  {
    tempcol=0xF00F;
    tempnum=50;
  }
  tempWin();
  
  //湿度（接口给 "93%" 这种；个别城市只回小写 sd，或干脆没有该字段）
  String humRaw = sk["SD"].as<String>();
  if(humRaw.length() == 0 || humRaw == "null") humRaw = sk["sd"].as<String>();
  huminum = humFromString(humRaw);
  clk.createSprite(L_VAL_W, 24); 
  clk.fillSprite(bgColor);
  clk.setTextDatum(MR_DATUM);
  clk.setTextColor(TFT_WHITE, bgColor); 
  clk.drawString((huminum < 0) ? String("--") : (String(huminum) + "%"), L_VAL_W - 2, 12);
  clk.pushSprite(L_HU_VAL_X,L_HU_VAL_Y);
  
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
  humidityWin(huminum);
  
  //城市名称（网页设置了自定义名称则优先显示，否则用接口返回值；受字体库字符集限制）
  clk.createSprite(94, 30); 
  clk.fillSprite(bgColor);
  clk.setTextDatum(CC_DATUM);
  clk.setTextColor(TFT_WHITE, bgColor); 
  clk.drawString(cityNameSet.length() ? cityNameSet : sk["cityname"].as<String>(),44,16);
  clk.pushSprite(L_CITY_X,L_CITY_Y);
  clk.deleteSprite();
  //PM2.5空气指数
  uint16_t pm25BgColor = tft.color565(156,202,127);//优
  String aqiTxt = "优";
  int pm25V = sk["aqi"];
  if(pm25V>200){
    pm25BgColor = tft.color565(136,11,32);//重度
    aqiTxt = "重度";
  }else if(pm25V>150){
    pm25BgColor = tft.color565(186,55,121);//中度
    aqiTxt = "中度";
  }else if(pm25V>100){
    pm25BgColor = tft.color565(242,159,57);//轻
    aqiTxt = "轻度";
  }else if(pm25V>50){
    pm25BgColor = tft.color565(247,219,100);//良
    aqiTxt = "良";
  }
  clk.createSprite(56, 24); 
  clk.fillSprite(bgColor);
  clk.fillRoundRect(0,0,50,24,4,pm25BgColor);
  clk.setTextDatum(CC_DATUM);
  clk.setTextColor(0x0000); 
  clk.drawString(aqiTxt,25,13);
  clk.pushSprite(L_AQI_X,L_AQI_Y);
  clk.deleteSprite();
  
  //左上角滚动字幕内容在解析完预报JSON后统一填充（见下方）
  //scrollText[6] = atoi((sk["weathercode"].as<String>()).substring(1,3).c_str()) ;
  //天气图标
  {
    int wcode = atoi((sk["weathercode"].as<String>()).substring(1,3).c_str());
    bool drawn = false;
#if UI_THEME
    if(fsOk)
    {
      char wp[16];
      snprintf(wp, sizeof(wp), "/wx/w%d.jpg", wxIconIndex(wcode));
      drawn = (TJpgDec.drawFsJpg(L_WICON_X, L_WICON_Y, wp) == JDR_OK);
    }
#endif
    if(!drawn) wrat.printfweather(L_WICON_X, L_WICON_Y, wcode);//素材缺失用内置图标
  }
  
  //左上角滚动字幕（第二段、第三段JSON各自独立文档）
  DynamicJsonDocument docDZ(512);
  deserializeJson(docDZ, *cityDZ);
  JsonObject dz = docDZ.as<JsonObject>();
  String today = dz["weather"].as<String>();
  scrollText[0] = "今日 "+today;
  DynamicJsonDocument docFC(512);
  deserializeJson(docFC, *dataFC);
  JsonObject fc = docFC.as<JsonObject>();
  scrollText[1] = sunRise.length() ? ("白天 " + sunRise) : "";//字库无“出/落”，用“白天/黑夜”表达起止
  scrollText[2] = sunSet.length()  ? ("黑夜 " + sunSet)  : "";
  scrollText[3] = sk["WD"].as<String>()+sk["WS"].as<String>();//去掉“风向”前缀，防止过长溢出
  scrollText[4] = "最低温度 "+fc["fd"].as<String>()+"℃";
  scrollText[5] = "最高温度 "+fc["fc"].as<String>()+"℃";
  scrollText[6] = "湿度 "+sk["SD"].as<String>();
  scrollText[7] = "";//备用
  updateIpScroll();//滚动条最后一条：Web访问IP（随天气刷新同步更新）

  //实时天气色块（秒数上方，最多显示两个字：有“转”取转前，否则取前两字）
  String live = sk["weather"].as<String>();
  String chipTxt;
  int zh = live.indexOf("转");
  if(zh > 0) chipTxt = live.substring(0, zh);
  else chipTxt = (live.length() > 6) ? live.substring(0, 6) : live;
  uint16_t chipCol;
  if(live.indexOf("雷") >= 0)      chipCol = tft.color565(155, 89, 182);
  else if(live.indexOf("雪") >= 0) chipCol = tft.color565(205, 235, 255);
  else if(live.indexOf("雨") >= 0) chipCol = tft.color565(96, 160, 255);
  else if(live.indexOf("雾") >= 0 || live.indexOf("霾") >= 0) chipCol = tft.color565(150, 150, 150);
  else if(live.indexOf("阴") >= 0) chipCol = tft.color565(130, 136, 150);
  else if(live.indexOf("云") >= 0) chipCol = tft.color565(135, 206, 250);
  else if(live.indexOf("晴") >= 0) chipCol = tft.color565(255, 176, 59);
  else chipCol = tft.color565(156, 202, 127);
  clk.createSprite(56, 26);
  clk.fillSprite(bgColor);
  clk.fillRoundRect(2, 1, 52, 24, 4, chipCol);
  clk.setTextDatum(CC_DATUM);
  clk.setTextColor(0x0000);
  clk.drawString(chipTxt, 28, 13);
  clk.pushSprite(L_TODAY_X,L_TODAY_Y);
  clk.deleteSprite();
  
  //Serial.println(scrollText[0]);
  //Serial.println(scrollText[0]);
  
  clk.unloadFont();
}
int currentIndex = 0;
TFT_eSprite clkb = TFT_eSprite(&tft);
void scrollBanner(){
  //整屏重绘或开机首帧：即使天气尚未拉取成功，也先清掉滚动区，避免残留开机画面
  if(repaintScrollArea)
  {
    repaintScrollArea = 0;
    clkb.setColorDepth(8);
    clkb.createSprite(150, 30);
    clkb.fillSprite(bgColor);
    clkb.pushSprite(L_SCROLL_X,L_SCROLL_Y);
    clkb.deleteSprite();
  }
  //最多尝试跳过6个空条目（天气拉取失败时避免刷屏乱码）
  for(int guard = 0; guard < 9; guard++)
  {
    if(scrollText[currentIndex].length() > 0)
    {
      clkb.setColorDepth(8);
      clkb.loadFont(ZdyLwFont_20);
      clkb.createSprite(150, 30); 
      clkb.fillSprite(bgColor);
      clkb.setTextWrap(false);
      clkb.setTextDatum(CC_DATUM);
      clkb.setTextColor(TFT_WHITE, bgColor); 
      clkb.drawString(scrollText[currentIndex],74, 16);
      clkb.pushSprite(L_SCROLL_X,L_SCROLL_Y);
       
      clkb.deleteSprite();
      clkb.unloadFont();
      
      if(currentIndex>=8)
        currentIndex = 0;  //回第一个
      else
        currentIndex += 1;  //准备切换到下一个
      break;
    }
    currentIndex = (currentIndex+1)%9;
  }
    prevTime = 1;
}
#if imgAst_EN
//太空人动画（DHT移除后常驻）
void imgAnim()
{
  int x=L_AST_X,y=L_AST_Y;
  if(millis() - AprevTime > 50) //ms切换一次
  {
    Anim++;
    AprevTime = millis();
  }
  if(Anim==10)
    Anim=0;
  switch(Anim)
  {
    case 0:
      TJpgDec.drawJpg(x,y,i0, sizeof(i0));
      break;
    case 1:
      TJpgDec.drawJpg(x,y,i1, sizeof(i1));
      break;
    case 2:
      TJpgDec.drawJpg(x,y,i2, sizeof(i2));
      break;
    case 3:
      TJpgDec.drawJpg(x,y,i3, sizeof(i3));
      break;
    case 4:
      TJpgDec.drawJpg(x,y,i4, sizeof(i4));
      break;
    case 5:
      TJpgDec.drawJpg(x,y,i5, sizeof(i5));
      break;
    case 6:
      TJpgDec.drawJpg(x,y,i6, sizeof(i6));
      break;
    case 7:
      TJpgDec.drawJpg(x,y,i7, sizeof(i7));
      break;
    case 8: 
      TJpgDec.drawJpg(x,y,i8, sizeof(i8));
      break;
    case 9: 
      TJpgDec.drawJpg(x,y,i9, sizeof(i9));
      break;
    default:
      Serial.println("显示Anim错误");
      break;
  }
}
#endif
unsigned char Hour_sign   = 60;
unsigned char Minute_sign = 60;
unsigned char Second_sign = 60;
void digitalClockDisplay(int reflash_en)
{ 
#if UI_THEME
  if(uiFontOk)
  {
    //平滑字体：4 位数字一次画完（字库只在绘制期间装入，避免与中文精灵字体互相覆盖）
    if(hour()!=Hour_sign || minute()!=Minute_sign || reflash_en == 1)
    {
      tft.loadFont(UI_FONT_PATH, LittleFS);
      tft.setTextDatum(BC_DATUM);
      tft.setTextColor(TFT_WHITE, bgColor);
      tft.drawString(String(hour()/10),   L_TIME_H1+18, L_TIME_Y+60);
      tft.drawString(String(hour()%10),   L_TIME_H2+18, L_TIME_Y+60);
      tft.drawString(String(minute()/10), L_TIME_M1+18, L_TIME_Y+60);
      tft.drawString(String(minute()%10), L_TIME_M2+18, L_TIME_Y+60);
      tft.unloadFont();
      Hour_sign = hour();
      Minute_sign = minute();
    }
  }
  else
#endif
  {
  if(hour()!=Hour_sign || reflash_en == 1)//时钟刷新
  {
    dig.printfW3660(L_TIME_H1,L_TIME_Y,hour()/10);
    dig.printfW3660(L_TIME_H2,L_TIME_Y,hour()%10);
    Hour_sign = hour();
  }
  if(minute()!=Minute_sign  || reflash_en == 1)//分钟刷新
  {
    dig.printfO3660(L_TIME_M1,L_TIME_Y,minute()/10);
    dig.printfO3660(L_TIME_M2,L_TIME_Y,minute()%10);
    Minute_sign = minute();
  }
  }
  if(second()!=Second_sign  || reflash_en == 1)//秒刷新
  {
    dig.printfW1830(L_SEC_X1,L_SEC_Y,second()/10);
    dig.printfW1830(L_SEC_X2,L_SEC_Y,second()%10);
    Second_sign = second();
  }
  
  if(reflash_en == 1) reflash_en = 0;
  /***日期****/
  clk.setColorDepth(8);
  clk.loadFont(ZdyLwFont_20);
   
  //星期
  clk.createSprite(58, 30);
  clk.fillSprite(bgColor);
  clk.setTextDatum(CC_DATUM);
  clk.setTextColor(TFT_WHITE, bgColor);
  clk.drawString(week(),29,16);
  clk.pushSprite(L_WK_X,L_WK_Y);
  clk.deleteSprite();
  
  //月日/农历 每8秒轮换（前4秒公历，后4秒农历）
  clk.createSprite(100, 30);
  clk.fillSprite(bgColor);
  clk.setTextDatum(CC_DATUM);
  clk.setTextColor(TFT_WHITE, bgColor);  
  clk.drawString((second()%8 < 4) ? monthDay() : lunarDayStr(),50,16);
  clk.pushSprite(L_MD_X,L_MD_Y);
  clk.deleteSprite();
  
  clk.unloadFont();
  /***日期****/
}
//星期
String week()
{
  String wk[7] = {"日","一","二","三","四","五","六"};
  String s = "周" + wk[weekday()-1];
  return s;
}
//月日
String monthDay()
{
  String s = String(month());
  s = s + "月" + day() + "日";
  return s;
}
/*-------- 农历换算（数据表 1900-2049，经典 lunarInfo 编码：
   bit15..bit4 = 正月至十二月大小月(1=30天)，bit16 = 闰月30天，bit3..0 = 闰哪个月(0=无闰)
   已与 lunardate 库逐日核对 1901-2049 全部日期，仅该表公认自带的 1954/1978/1996 三年个别月份
   有±1天误差，对本设备（2026 年起使用）无影响 --------*/
static const uint32_t lunarInfo[] PROGMEM = {
  0x04bd8,0x04ae0,0x0a570,0x054d5,0x0d260,0x0d950,0x16554,0x056a0,0x09ad0,0x055d2,
  0x04ae0,0x0a5b6,0x0a4d0,0x0d250,0x1d255,0x0b540,0x0d6a0,0x0ada2,0x095b0,0x14977,
  0x04970,0x0a4b0,0x0b4b5,0x06a50,0x06d40,0x1ab54,0x02b60,0x09570,0x052f2,0x04970,
  0x06566,0x0d4a0,0x0ea50,0x06e95,0x05ad0,0x02b60,0x186e3,0x092e0,0x1c8d7,0x0c950,
  0x0d4a0,0x1d8a6,0x0b550,0x056a0,0x1a5b4,0x025d0,0x092d0,0x0d2b2,0x0a950,0x0b557,
  0x06ca0,0x0b550,0x15355,0x04da0,0x0a5b0,0x14573,0x052b0,0x0a9a8,0x0e950,0x06aa0,
  0x0aea6,0x0ab50,0x04b60,0x0aae4,0x0a570,0x05260,0x0f263,0x0d950,0x05b57,0x056a0,
  0x096d0,0x04dd5,0x04ad0,0x0a4d0,0x0d4d4,0x0d250,0x0d558,0x0b540,0x0b6a0,0x195a6,
  0x095b0,0x049b0,0x0a974,0x0a4b0,0x0b27a,0x06a50,0x06d40,0x0af46,0x0ab60,0x09570,
  0x04af5,0x04970,0x064b0,0x074a3,0x0ea50,0x06b58,0x055c0,0x0ab60,0x096d5,0x092e0,
  0x0c960,0x0d954,0x0d4a0,0x0da50,0x07552,0x056a0,0x0abb7,0x025d0,0x092d0,0x0cab5,
  0x0a950,0x0b4a0,0x0baa4,0x0ad50,0x055d9,0x04ba0,0x0a5b0,0x15176,0x052b0,0x0a930,
  0x07954,0x06aa0,0x0ad50,0x05b52,0x04b60,0x0a6e6,0x0a4e0,0x0d260,0x0ea65,0x0d530,
  0x05aa0,0x076a3,0x096d0,0x04afb,0x04ad0,0x0a4d0,0x1d0b6,0x0d250,0x0d520,0x0dd45,
  0x0b5a0,0x056d0,0x055b2,0x049b0,0x0a577,0x0a4b0,0x0aa50,0x1b255,0x06d20,0x0ada0
};
static const char lunarMonthName[][7] PROGMEM = {
  "正月","二月","三月","四月","五月","六月","七月","八月","九月","十月","冬月","腊月"
};
static const char lunarDayName[][7] PROGMEM = {
  "初一","初二","初三","初四","初五","初六","初七","初八","初九","初十",
  "十一","十二","十三","十四","十五","十六","十七","十八","十九","二十",
  "廿一","廿二","廿三","廿四","廿五","廿六","廿七","廿八","廿九","三十"
};
static uint8_t lunarLeapMonth(int y) { return pgm_read_dword(&lunarInfo[y-1900]) & 0xf; }
static uint8_t lunarLeapDays(int y)  { return (pgm_read_dword(&lunarInfo[y-1900]) & 0x10000) ? 30 : 29; }
static uint8_t lunarMonthDays(int y, uint8_t m) { return (pgm_read_dword(&lunarInfo[y-1900]) & (0x10000 >> m)) ? 30 : 29; }
static uint16_t lunarYearDays(int y)
{
  uint32_t v = pgm_read_dword(&lunarInfo[y-1900]);
  uint16_t s = 348;
  for(uint16_t b = 0x8000; b > 0x8; b >>= 1) if(v & b) s++;
  return s + ((v & 0xf) ? lunarLeapDays(y) : 0);
}
//公历日期 -> 距 1900-01-01 的天数
static long greg2num(int y, int m, int d)
{
  static const uint8_t monthLen[] PROGMEM = {31,28,31,30,31,30,31,31,30,31,30,31};
  long n = d;
  for(int i = 1900; i < y; i++)
    n += 365 + (((i%4==0 && i%100!=0) || i%400==0) ? 1 : 0);
  for(int i = 1; i < m; i++)
  {
    n += pgm_read_byte(&monthLen[i-1]);
    if(i == 2 && ((y%4==0 && y%100!=0) || y%400==0)) n++;
  }
  return n;
}
//当天农历字符串（如“闰二月初一”），按日缓存，一天只算一次
String lunarDayStr()
{
  static int cy = -1, cm = -1, cd = -1;
  static String cache;
  int y = year(), m = month(), d = day();
  if(cy == y && cm == m && cd == d) return cache;
  cy = y; cm = m; cd = d;
  cache = "";
  if(y < 1900 || y > 2049) return cache;
  long offset = greg2num(y, m, d) - greg2num(1900, 1, 31);
  int ly = 1900;
  while(ly <= 2049)
  {
    uint16_t dy = lunarYearDays(ly);
    if(offset < dy) break;
    offset -= dy; ly++;
  }
  if(ly > 2049) return cache;
  uint8_t leap = lunarLeapMonth(ly);
  uint8_t lm = 1;
  bool isLeap = false;
  while(lm <= 12)
  {
    uint8_t dm = lunarMonthDays(ly, lm);
    if(offset < dm) break;
    offset -= dm;
    if(leap > 0 && lm == leap)
    {
      uint8_t dl = lunarLeapDays(ly);
      if(offset < dl) { isLeap = true; break; }
      offset -= dl;
    }
    lm++;
  }
  char tmp[8];
  String s;
  if(isLeap) s = "闰";
  strcpy_P(tmp, lunarMonthName[lm-1]); s += tmp;
  strcpy_P(tmp, lunarDayName[(int)offset]); s += tmp;
  cache = s;
  return cache;
}
/*-------- NTP code ----------*/
const int NTP_PACKET_SIZE = 48; // NTP时间在消息的前48字节中
byte packetBuffer[NTP_PACKET_SIZE]; //buffer to hold incoming & outgoing packets
time_t getNtpTime()
{
  IPAddress ntpServerIP; // NTP server's ip address
  while (Udp.parsePacket() > 0) ; // discard any previously received packets
  //Serial.println("Transmit NTP Request");
  // get a random server from the pool
  WiFi.hostByName(ntpServerName, ntpServerIP);
  //Serial.print(ntpServerName);
  //Serial.print(": ");
  //Serial.println(ntpServerIP);
  sendNTPpacket(ntpServerIP);
  uint32_t beginWait = millis();
  while (millis() - beginWait < 1500) {
    int size = Udp.parsePacket();
    if (size >= NTP_PACKET_SIZE) {
      Serial.println("Receive NTP Response");
      Udp.read(packetBuffer, NTP_PACKET_SIZE);  // read packet into the buffer
      unsigned long secsSince1900;
      // convert four bytes starting at location 40 to a long integer
      secsSince1900 =  (unsigned long)packetBuffer[40] << 24;
      secsSince1900 |= (unsigned long)packetBuffer[41] << 16;
      secsSince1900 |= (unsigned long)packetBuffer[42] << 8;
      secsSince1900 |= (unsigned long)packetBuffer[43];
      //Serial.println(secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR);
      return secsSince1900 - 2208988800UL + timeZone * SECS_PER_HOUR;
    }
  }
  Serial.println("No NTP Response :-(");
  return 0; // 无法获取时间时返回0
}
// 向NTP服务器发送请求
void sendNTPpacket(IPAddress &address)
{
  // set all bytes in the buffer to 0
  memset(packetBuffer, 0, NTP_PACKET_SIZE);
  // Initialize values needed to form NTP request
  // (see URL above for details on the packets)
  packetBuffer[0] = 0b11100011;   // LI, Version, Mode
  packetBuffer[1] = 0;     // Stratum, or type of clock
  packetBuffer[2] = 6;     // Polling Interval
  packetBuffer[3] = 0xEC;  // Peer Clock Precision
  // 8 bytes of zero for Root Delay & Root Dispersion
  packetBuffer[12] = 49;
  packetBuffer[13] = 0x4E;
  packetBuffer[14] = 49;
  packetBuffer[15] = 52;
  // all NTP fields have been given values, now
  // you can send a packet requesting a timestamp:
  Udp.beginPacket(address, 123); //NTP requests are to port 123
  Udp.write(packetBuffer, NTP_PACKET_SIZE);
  Udp.endPacket();
}
