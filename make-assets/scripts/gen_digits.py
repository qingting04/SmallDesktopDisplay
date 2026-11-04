"""时钟数字点阵：Bahnschrift -> W_3660/O_3660（36x60）、W_1830（18x30）头文件

用法（在 make-assets/ 下）:
  uv run python scripts/gen_digits.py            # 生成到 out/
  uv run python scripts/gen_digits.py --inplace  # 直接写入 SmallDesktopDisplay/font/
需要 Windows 字体盘可访问（/mnt/c），或用 --font 指定 ttf 路径。
"""
import argparse
import io
import sys
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont

sys.path.insert(0, str(Path(__file__).resolve().parent))
from sdd_assets import SKETCH, header_text

DEFAULT_FONT = Path("/mnt/c/Windows/Fonts/bahnschrift.ttf")
COLORS = {"W": (255, 255, 255), "O": (240, 150, 40)}   # O 色值取自原固件数字橙色
SETS = [("3660", (36, 60), ("W", "O")), ("1830", (18, 30), ("W",))]
RENDER_PX = 300


def digit_img(ch: str, color, size, font_path):
    """大字渲染 -> bbox 裁切 -> 等比填满 size 画布（四周留1px）-> 彩色贴黑底"""
    w, h = size
    gray = Image.new("L", (RENDER_PX * 2, RENDER_PX * 2), 0)
    fnt = ImageFont.truetype(str(font_path), RENDER_PX)
    ImageDraw.Draw(gray).text((RENDER_PX // 2, RENDER_PX // 4), ch, fill=255, font=fnt)
    im = gray.crop(gray.getbbox())
    scale = min((w - 2) / im.width, (h - 2) / im.height)
    nw, nh = max(1, round(im.width * scale)), max(1, round(im.height * scale))
    mask = im.resize((nw, nh), Image.LANCZOS)
    out = Image.new("RGB", (w, h), (0, 0, 0))
    out.paste(Image.new("RGB", (nw, nh), color), ((w - nw) // 2, (h - nh) // 2), mask)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--inplace", action="store_true", help=f"写入 {SKETCH}/font/ 而非 out/")
    ap.add_argument("--font", default=str(DEFAULT_FONT))
    args = ap.parse_args()
    font_path = Path(args.font)
    if not font_path.exists():
        sys.exit(f"字体不存在: {font_path}（用 --font 指定）")
    out_root = SKETCH if args.inplace else Path(__file__).resolve().parents[1] / "out"

    for tag, size, colors in SETS:
        for c in colors:
            for d in range(10):
                var = f"{c}_{tag}_i{d}"
                im = digit_img(str(d), COLORS[c], size, font_path)
                buf = io.BytesIO()
                im.save(buf, "JPEG", quality=90, optimize=True, progressive=False, subsampling=0)
                p = out_root / "font" / f"{var}.h"
                p.parent.mkdir(parents=True, exist_ok=True)
                p.write_bytes(header_text(var, buf.getvalue()).encode("ascii"))
                print(f"wrote {p} ({p.stat().st_size} B)")


if __name__ == "__main__":
    main()
