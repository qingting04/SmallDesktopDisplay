# LittleFS 素材

仅在 `main.cpp` 里 `#define UI_THEME 1` 时使用；默认 `UI_THEME 0` 走编译进固件的内置素材，本目录不参与运行。

```
data/font/W0365.vlw    大号数字（时钟）
data/font/WD100.vlw    超大数字
data/font/pingfang70.vlw
data/font/Alimama55.vlw
data/font/Alimama38.vlw
data/wx/w0..w6.jpg     天气图标 60×60
```

这些文件是第三方商业字体与图标（苹方、阿里妈妈等），授权受限，**不随仓库分发**，见根目录 `.gitignore`。
要启用这套主题，把同名文件自行放进 `data/font` 与 `data/wx`，然后：

```bash
pio run -t buildfs
pio run -t uploadfs
```

素材缺失时固件会自动退回内置素材，不会白屏。
