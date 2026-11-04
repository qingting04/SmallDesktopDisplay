"""天气图标（60x60）与温湿度图标（24x24）：meteocons fill 风格 SVG -> img/ 头文件

用法（在 make-assets/ 下）:
  uv run python scripts/gen_icons.py            # 生成到 out/
  uv run python scripts/gen_icons.py --inplace  # 直接写入 SmallDesktopDisplay/img/
"""
import argparse
import io
import sys
from pathlib import Path

from cairosvg import svg2png
from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parent))
from sdd_assets import SVG_DIR, SKETCH, fit_rgba, write_header

# 与 weathernum.cpp printfweather() 的分组对应：t26.h 未被固件引用，不再生成
WEATHER = {
    "t0":  "clear-day",
    "t1":  "cloudy",
    "t2":  "partly-cloudy-day",
    "t3":  "overcast",
    "t4":  "partly-cloudy-day-rain",
    "t5":  "thunderstorms-day",
    "t6":  "sleet",
    "t7":  "rain",
    "t9":  "extreme-rain",
    "t11": "thunderstorms-extreme-rain",
    "t13": "overcast-snow",
    "t14": "partly-cloudy-day-snow",
    "t15": "snow",
    "t16": "extreme-snow",
    "t18": "fog",
    "t19": "extreme-sleet",
    "t20": "dust-day",
    "t29": "dust",
    "t30": "wind-dust",
    "t31": "cyclone",
    "t53": "haze",
    "t99": "weather-alert",
}
SMALL24 = {
    "temperature": "thermometer",
    "humidity": "humidity",
}


def svg_to_rgba(name: str) -> Image.Image:
    png = svg2png(url=str(SVG_DIR / f"{name}.svg"), output_width=512, output_height=512)
    return Image.open(io.BytesIO(png)).convert("RGBA")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--inplace", action="store_true", help=f"写入 {SKETCH}/img/ 而非 out/")
    args = ap.parse_args()
    out_root = SKETCH if args.inplace else Path(__file__).resolve().parents[1] / "out"

    for var, svg in WEATHER.items():
        write_header(out_root / "img" / "tianqi" / f"{var}.h", var, fit_rgba(svg_to_rgba(svg), 60))
    for var, svg in SMALL24.items():
        write_header(out_root / "img" / f"{var}.h", var, fit_rgba(svg_to_rgba(svg), 24))


if __name__ == "__main__":
    main()
