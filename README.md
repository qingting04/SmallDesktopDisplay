# SmallDesktopDisplay

ESP8266 + 1.54 寸 240×240 方屏的桌面天气时钟。显示时间、日期、农历、天气图标、温湿度、日出日落，
并提供一套深色中文 Web 设置页（含 OTA 升级）。

## 硬件

| 项目 | 值 |
|---|---|
| 主控 | ESP8266（ESP-12E / NodeMCU 模块） |
| 屏幕 | ST7789 1.54" 240×240，无 CS 也可工作，本固件按有 CS 接 |
| Flash | 4 MB（`eagle.flash.4m1m.ld`：app 槽 1044464 B + LittleFS 1024000 B @0x300000） |

引脚（以 `libraries/TFT_eSPI/User_Setup.h` 为准，与板型定义无关）：

| 信号 | 引脚 | GPIO |
|---|---|---|
| SCLK | D5 | 14 |
| MOSI | D7 | 13 |
| CS | D8 | 15 |
| DC | D3 | 0 |
| RST | D4 | 2 |
| BL | D1 | 5 |

背光低电平点亮，由 GPIO5 软件 PWM 调光。

## 目录

```
SDD15_pio/            PlatformIO 工程（主开发树）
  src/main.cpp        固件主体
  src/webui.h         两个网页；共用 CSS/JS 各一份，由 sendPageParts() 分段发送
  src/img/  src/font/ 图标与字模的 PROGMEM 头文件
  data/               LittleFS 素材（UI_THEME 1 时才用，默认不启用）
SmallDesktopDisplay/  Arduino IDE 副本，与 SDD15_pio 内容等价（.ino = main.cpp 去掉首行 include）
libraries/            TFT_eSPI、TJpg_Decoder、Time、ArduinoJson（已按本项目改过 User_Setup.h）
make-assets/          素材生成脚本（meteocons SVG → JPEG 头文件、数字点阵、农历表校验）
```

## 编译与烧录

```bash
cd SDD15_pio
pio run                    # 编译
pio run -t upload          # 串口烧录（需先接 USB）
```

环境已锁死，改动前先看 `platformio.ini` 里的注释：

- `platform = espressif8266@2.6.3`，框架 arduino core **2.7.4**。升到 3.x 后新核心的软件 PWM
  受 WiFi 中断干扰，背光只剩最高档附近才亮。
- `board_build.filesystem = littlefs` + `-DUSE_LITTLEFS=1`，TJpg_Decoder 走 LittleFS。
- `TJpg_Decoder.cpp` 打过一处补丁（`#ifdef USE_LITTLEFS` 时 include `<LittleFS.h>`），否则编译报
  `LittleFS was not declared`。

烧录地址与参数（单文件 app 镜像，含 EBoot，从 0x0 起写）：

```bash
esptool.py --chip esp8266 --port /dev/ttyUSB0 --baud 460800 \
  --before default_reset --after hard_reset \
  write_flash --flash_mode dio --flash_freq 40m --flash_size 4MB \
  0x0 firmware.bin
```

只写 0x0 起的 app 区，碰不到 0x300000 的 LittleFS 与 0x3FB000 的 EEPROM，已配的 WiFi 与设置保留。

## Web 设置页

设备联网后访问其内网 IP；连不上已保存 WiFi 时自动开开放热点 `SDD-xxxx`，访问 `192.168.4.1` 配网。

- **状态卡**：模式、IP、信号、SSID、运行时长、固件版本、天气更新时效、日出日落、可用内存与碎片、Flash 占用。
- **设置**：亮度、分时段亮度、夜间亮度与时段、屏幕方向、城市代码、天气更新间隔。
- **OTA 升级**：选 `.bin` 直接上传，页面与屏幕同步显示进度。路由 `/update`，用 `Updater` 而
  不是 `ArduinoOTA`——任何浏览器都能刷，不需要 mDNS 或 IDE。配网页也带升级入口，未配网也能刷。
- **Captive portal**：AP 模式下 DNS 通配劫持（`dnsServer.start(53,"*",softAPIP())`）+ 各系统探测
  路径（`/generate_204`、`/hotspot-detect.html`、`/connecttest.txt`、`/ncsi.txt`）302 到配网页，
  手机连上热点自动弹窗。
- **字段级错误码**：保存失败返回 `{"ok":0,"err":"cc"}`，网页按 `errText()` 翻成具体中文提示，
  而不是笼统一句"保存失败"。

串口命令行保留：`0x01`-`0x05` 设置项，`0x05` 清空已保存 WiFi 并重启。

## 版本历史

| 版本 | 内容 |
|---|---|
| V1.5 | 移除 DHT11；去掉 WiFiManager 改自写配网（连不上自动开热点 `SDD-xxxx`）；Web 页深色卡片式中文重排；屏幕 UI 布局坐标集中定义 |
| V1.6 | Web 页 OTA 上传升级，写入 flash 末尾暂存区由 EBoot 拷回；配网页同样带升级入口 |
| V1.7 | 屏幕 UI 可走 LittleFS 素材（`UI_THEME 1`），换图换字只重传文件系统 |
| V2.0 | 修上电雪花屏（背光提前于面板初始化点亮，露出显存随机值）→ 先关背光、清屏后渐亮；内置 JPEG 去黑底噪点（亮度≤12 压黑 + q100 4:4:4 重编码） |
| V2.1 | 温度按一位小数；温湿度改「左图标 + 右对齐数值」；滚动条加日出日落 |
| V2.2 | 设置保存改字段级错误码；Captive portal；`/api/status` 补诊断字段 |
| V2.3 | 修湿度显示：`"100%"` 被按前两位截成 10（数值对、条形只剩 1/10）→ 取整串数字，1/2/3 位都对；缺 `SD` 回退小写 `sd`；认得不出数字显示 `--`；湿度条形按 0-100 等比且不再改写全局；`/api/status` 与状态卡加「天气读数」 |
| V2.4 | 代码结构整理：设置保存改表格驱动（加设置项只写一行）；EEPROM 块读写/温湿度进度条各收成一个公共函数；网页两页 CSS/JS 抽成共用常量（省 4.3 KB flash）；删历史注释代码、加分区标题；修串口 `0x02` 设置城市码赋给了从未更新的变量 |
| V2.5 | 对时加固：NTP 失败时原来没有任何兜底，设备靠晶振自走（几十 ppm，几周就差几十分钟，两块表会对不上）→ 改成 5 台服务器轮换重试 + 校验回包（模式/层数/时间戳/值域）；天气请求成功后借回包 `Date` 头兜底校正，UDP 123 被封也能对时；`/api/status` 与状态卡增加「设备时间」「对时」 |
| V2.6 | 修 V2.5 的对时回归：① 回包校验拿 `originate` 比了同包的 `transmit`（两个不同时间戳，永远不等），每个回包都被丢弃；改为比「我们发出去的 transmit」。② `header("Date")` 前没 `collectHeaders()`，核心库此时不保存响应头，兜底从未生效。另加“严格校验全挂时用宽松候选”的保险 |
| V2.7 | 修「下雨却显示扬沙」：实况编码 `d301`(雨)/`d302`(雪) 是三位数，旧代码 `atoi(substring(1,3))` 固定取两位把 301 截成 30（扬沙），下雨下雪都画成沙尘暴 → 改为取字母后整串数字，解析不出时用「未知」图标而非冒充晴天；`/api/status` 与状态卡增加实况编码/文字 |

## 已知限制

- 上电最初约 200 ms 仍可能闪白：那段在 bootloader 里，GPIO5 悬空，软件改不动。
- 时钟只靠 NTP 与 HTTP 两种网络对时，没有 RTC 芯片：**断网后就靠晶振自走**，精度几十 ppm（一天几秒）。
  两块表长期断网必然对不上，属硬件限制；只要联网，V2.5 起每次天气刷新都会顺手校正，偏差超 60 秒就拨正。
- 部分路由器/运营商会封或改写 UDP 123，此时 NTP 一直失败——这正是两块表差几十分钟的常见原因。
  状态卡「对时」一行若显示 `HTTP对时` 说明走的是兜底通道，功能正常但精度是整秒级。
  要彻底消除需在背光三极管栅极加 10 kΩ 下拉，或换一个默认低电平的引脚。
- 温度接口 `d1.weather.com.cn` 给的是小数，湿度是带 `%` 的字符串（`"93%"`、雨天会到 `"100%"`），两者都要容错解析。
  湿度是气象站实测值，雨天/雾天长期 90% 以上是正常的，不是显示卡住。
- 日出日落来自 `t.weather.itboy.net` 的另一个接口（`weather_index` 里没有这两个字段），
  两处都用同一个 7 位城市代码。
- 字库 `ZdyLwFont_20.h` 共 702 字，缺「出」「落」等字，所以滚动条写「白天/黑夜」而不是「日出/日落」。

## 作者与维护

| 角色 | 署名 |
|---|---|
| 原作 | Misaka |
| 修改 | 微车游 |
| V1.5–V2.2 维护 | qt（[github.com/qingting04](https://github.com/qingting04)） |

## 声明

原作 SmallDesktopDisplay 的版权归原作者所有。
本仓库中 `libraries/` 下的第三方库遵循各自许可证（TFT_eSPI、ArduinoJson、Time、TJpg_Decoder，
详见各目录）。

`SDD15_pio/data/` 目录下的字体（苹方、阿里妈妈等）与图标属于第三方商业资产，**不随仓库分发**，
`.gitignore` 已排除；默认构建不依赖它们。
