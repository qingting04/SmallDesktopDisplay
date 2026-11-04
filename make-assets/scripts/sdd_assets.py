"""SDD 素材生成公共函数：JPEG -> PROGMEM 头文件（与原固件头文件字节格式一致）"""
import io
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]        # make-assets/
REPO = ROOT.parent                                # 项目根目录
SKETCH = REPO / "SmallDesktopDisplay"
SVG_DIR = ROOT / "vendor" / "meteocons_fill"


def fit_rgba(im: Image.Image, canvas: int, pad: int = 1) -> Image.Image:
    """RGBA 图按 bbox 裁切后等比放大填满 canvas（留 pad 黑边），合成到黑底 RGB"""
    box = im.getbbox()
    if box:
        im = im.crop(box)
    w, h = im.size
    avail = canvas - 2 * pad
    scale = avail / max(w, h)
    nw, nh = max(1, round(w * scale)), max(1, round(h * scale))
    im = im.resize((nw, nh), Image.LANCZOS)
    out = Image.new("RGB", (canvas, canvas), (0, 0, 0))
    out.paste(im, ((canvas - nw) // 2, (canvas - nh) // 2), im)
    return out


def jpeg_bytes(im: Image.Image) -> bytes:
    buf = io.BytesIO()
    # TJpgDec 不支持 progressive；subsampling=0 避免色度二次采样糊边
    im.save(buf, "JPEG", quality=90, optimize=True, progressive=False, subsampling=0)
    return buf.getvalue()


def header_text(var: str, data: bytes) -> str:
    items = [f"0x{b:02X}" for b in data]
    lines = [", ".join(items[i:i + 16]) + (", " if i + 16 < len(items) else "")
             for i in range(0, len(items), 16)]
    return ("#include <pgmspace.h>\r\n"
            f"const uint8_t {var}[] PROGMEM = {{\r\n"
            + "\r\n".join(lines) + "\r\n};\r\n")


def write_header(path: Path, var: str, im: Image.Image):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(header_text(var, jpeg_bytes(im)).encode("ascii"))
    print(f"wrote {path} ({path.stat().st_size} B)")
