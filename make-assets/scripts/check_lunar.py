"""校验固件内嵌农历表：解析 SmallDesktopDisplay.ino 的 lunarInfo[]，逐日比对 lunardate

用法（在 make-assets/ 下）:  uv run python scripts/check_lunar.py [起始年=2026] [结束年=2049]
"""
import datetime
import re
import sys
from pathlib import Path

from lunardate import LunarDate

sys.path.insert(0, str(Path(__file__).resolve().parent))
from sdd_assets import SKETCH

INO = SKETCH / "SmallDesktopDisplay.ino"
EPOCH = datetime.date(1900, 1, 31)


def load_table():
    txt = INO.read_text(encoding="utf-8")
    m = re.search(r"lunarInfo\[\]\s*PROGMEM\s*=\s*\{(.*?)\};", txt, re.S)
    return [int(v, 16) for v in re.findall(r"0x([0-9A-Fa-f]{4,5})", m.group(1))]


def leap_m(i):
    return i & 0xF


def leap_d(i):
    return (30 if i & 0x10000 else 29) if leap_m(i) else 0


def month_d(i, m):
    return 30 if i & (0x10000 >> m) else 29


def year_d(i):
    s = 348
    bit = 0x8000
    while bit > 0x8:  # 仅 bit15..4 共12个月的大小位
        s += 1 if i & bit else 0
        bit >>= 1
    return s + leap_d(i)


def to_lunar(info, date):
    offset = date.toordinal() - EPOCH.toordinal()
    idx = 0
    while offset >= year_d(info[idx]):
        offset -= year_d(info[idx])
        idx += 1
    i = info[idx]
    months = []
    for mm in range(1, 13):
        months.append((mm, month_d(i, mm), False))
        if leap_m(i) == mm:
            months.append((mm, leap_d(i), True))
    for mm, days, is_leap in months:
        if offset < days:
            return 1900 + idx, mm, offset + 1, is_leap
        offset -= days
    raise ValueError(f"overflow {date}")


def main():
    y0 = int(sys.argv[1]) if len(sys.argv) > 1 else 2026
    y1 = int(sys.argv[2]) if len(sys.argv) > 2 else 2049
    info = load_table()
    if len(info) < y1 - 1900 + 1:
        sys.exit(f"表只有{len(info)}项，不够覆盖到{y1}年")
    bad = 0
    day, end = datetime.date(y0, 1, 1), datetime.date(y1, 12, 31)
    while day <= end:
        yy, mm, dd, leap = to_lunar(info, day)
        ref = LunarDate.from_solar_date(day.year, day.month, day.day)
        if (yy, mm, dd, bool(leap)) != (ref.year, ref.month, ref.day, ref.is_leap_month):
            bad += 1
            if bad <= 5:
                print(f"MISMATCH {day}: table=({yy},{mm},{dd},leap={leap}) ref=({ref.year},{ref.month},{ref.day},leap={ref.is_leap_month})")
        day += datetime.timedelta(days=1)
    print(f"checked {y0}..{y1}: {'ALL OK' if bad == 0 else str(bad) + ' mismatches'}")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
