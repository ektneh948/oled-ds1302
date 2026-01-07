from luma.core.interface.serial import spi, i2c
from luma.oled.device import ssd1306
from luma.core.render import canvas
from PIL import ImageFont

import RPi.GPIO as GPIO

import os
import sys
import errno
import select
import time
import math
from dataclasses import dataclass
from enum import Enum, auto


# =========================
# Configuration
# =========================
DEBUG = 1

DEVICE_NAME = "/dev/my_custom_device_driver"

IDLE_TO_SCREENSAVER_SEC = 10.0


# =========================
# Time Data
# + DS1302 Date Time Structure
# =========================
@dataclass
class DS1302DateTime:
    seconds: int = 30
    minutes: int = 20
    hours: int = 10
    date: int = 29
    month: int = 12
    dayofweek: int = 2   # 1: SUN, 2: MON ...
    year: int = 25
    ampm: int = 0        # 1: PM, 2: AM
    hourmode: int = 0    # 0: 24hr, 1: 12hr
    
def days_in_month(year_yy: int, month: int) -> int:
    # yy(00~99) 기준. 윤년 간단 처리(2000~2099라고 가정하면 yy%4==0 윤년이 대체로 맞음)
    # 정확히 하려면 기준 연도(20yy)를 잡아야 하는데, UI 용도로는 충분.
    if month in (1, 3, 5, 7, 8, 10, 12):
        return 31
    if month in (4, 6, 9, 11):
        return 30
    # February
    leap = (year_yy % 4 == 0)
    return 29 if leap else 28
    
def time_to_str(t: DS1302DateTime) -> str:
    """C의 snprintf("%02d%02d%02d%02d%02d%02d\\n", ...) 대응"""
    return f"{t.year:02d}{t.month:02d}{t.date:02d}{t.hours:02d}{t.minutes:02d}{t.seconds:02d}\n"

def clamp(v: int, lo: int, hi: int) -> int:
    return max(lo, min(hi, v))


# =========================
# Device Driver
# =========================
def open_with_retry(path: str) -> int:
    """C의 while(open<0){sleep(2)} 와 동일"""
    fd = -1
    while fd < 0:
        try:
            fd = os.open(path, os.O_RDWR | os.O_NONBLOCK)
        except:
            print(f"  [error] open : {path}", file=sys.stderr)
            time.sleep(2)
    return fd

def write_time(fd: int, t: DS1302DateTime) -> int:
    """write time"""
    msg = time_to_str(t)
    if DEBUG:
        print(f"  [write] msg: {msg}", end="")
    data = msg.encode("ascii")

    try:
        ret = os.write(fd, data)
    except OSError as e:
        print(f"  [error] write: {e}", file=sys.stderr)
        return -1

    if DEBUG:
        print(f"  [write] ret: {ret}")
    return ret

def read_time_ipnut(fd: int) -> str:
    """
    드라이버 출력 예:
    YYMMDDhhmmssRK\\n
    예) 25122910203010\\n

    반환:
    "25122910203010"
    """
    try:
        data = os.read(fd, 256)
    except BlockingIOError:
        return ""
    except OSError:
        return ""

    if not data:
        return ""

    text = data.decode("utf-8", errors="replace")

    # 줄 단위 분리 → 빈 줄 제거
    lines = [ln.strip() for ln in text.splitlines() if ln.strip()]

    if not lines:
        return ""

    # 여러 줄일 경우 마지막 라인만 사용
    return lines[-1]

def parse_time_input(line: str):
    """
    input: "YYMMDDhhmmssRK"
    """
    if len(line) != 14:
        return None

    return {
        "year":    int(line[0:2]),
        "month":   int(line[2:4]),
        "date":    int(line[4:6]),
        "hours":   int(line[6:8]),
        "minutes": int(line[8:10]),
        "seconds": int(line[10:12]),
        "rotary":  int(line[12]),
        "key":     int(line[13]),
    }


# =========================
# OLED helpers
# =========================
def oled_hw_reset(mode: str):
    if mode == "spi":
        rst_pin = 24
    elif mode == "i2c":
        rst_pin = 4
    else:
        return
    GPIO.setmode(GPIO.BCM)
    GPIO.setup(rst_pin, GPIO.OUT)
    GPIO.output(rst_pin, 0)
    time.sleep(0.05)
    GPIO.output(rst_pin, 1)
    time.sleep(0.05)

def draw_analog_clock(draw, cx: int, cy: int, r: int, t: DS1302DateTime) -> None:
    # 원 + 시/분/초 바늘
    draw.ellipse((cx - r, cy - r, cx + r, cy + r), outline="white", fill="black")

    # ticks (12개)
    for k in range(12):
        ang = (k / 12.0) * 2.0 * math.pi - math.pi / 2
        x1 = cx + int((r - 2) * math.cos(ang))
        y1 = cy + int((r - 2) * math.sin(ang))
        x2 = cx + int(r * math.cos(ang))
        y2 = cy + int(r * math.sin(ang))
        draw.line((x1, y1, x2, y2), fill="white")

    sec = t.seconds % 60
    minute = t.minutes % 60
    hour = t.hours % 24

    # 각도
    sec_ang = (sec / 60.0) * 2 * math.pi - math.pi / 2
    min_ang = ((minute + sec / 60.0) / 60.0) * 2 * math.pi - math.pi / 2
    hour_ang = (((hour % 12) + minute / 60.0) / 12.0) * 2 * math.pi - math.pi / 2

    # 바늘 길이
    sx = cx + int((r - 2) * math.cos(sec_ang))
    sy = cy + int((r - 2) * math.sin(sec_ang))
    mx = cx + int((r - 4) * math.cos(min_ang))
    my = cy + int((r - 4) * math.sin(min_ang))
    hx = cx + int((r - 7) * math.cos(hour_ang))
    hy = cy + int((r - 7) * math.sin(hour_ang))

    # hour/min thicker 느낌: 같은 라인을 2번 약간 이동해서
    draw.line((cx, cy, hx, hy), fill="white")
    draw.line((cx+1, cy, hx, hy), fill="white")

    draw.line((cx, cy, mx, my), fill="white")
    draw.line((cx, cy+1, mx, my), fill="white")

    # seconds
    draw.line((cx, cy, sx, sy), fill="white")

    # center dot
    draw.ellipse((cx-1, cy-1, cx+1, cy+1), outline="white", fill="white")

def render_active(device, t: DS1302DateTime) -> None:
    date_str = f"{t.year:02d}/{t.month:02d}/{t.date:02d}"
    time_str = f"{t.hours:02d}:{t.minutes:02d}:{t.seconds:02d}"
    
    with canvas(device) as draw:
        draw.rectangle(device.bounding_box, outline="white", fill="black")
        # draw.text((2, 2), "ACTIVE", fill="white")
        # draw.text((60, 2), date_str, fill="white")
        draw.text((2, 2), date_str, fill="white")

        # 아날로그 시계는 오른쪽 아래에
        draw_analog_clock(draw, cx=96, cy=40, r=22, t=t)

        # 디지털은 왼쪽 아래
        draw.text((2, 26), time_str, fill="white")
        draw.text((2, 44), "OK:SETTING", fill="white")


# =========================
# Main
# =========================.
class UIState(Enum):
    ACTIVE = auto()
    SCREENSAVER = auto()
    SETTING = auto()
    
def main() -> None:
    # OLED init
    oled_hw_reset("spi")
    serial_spi = spi(port=0, device=0, gpio_DC=25, gpio_RST=24)
    oled_hw_reset("i2c")
    serial_i2c = i2c(port=1, address=0x3c)
    device_spi = ssd1306(serial_spi, width=128, height=64, rotate=0)
    device_i2c = ssd1306(serial_i2c, width=128, height=64, rotate=0)
    
    # ===== 화면 테스트 =====
    try:
        while True:
            with canvas(device_spi) as draw:
            # with canvas(device_i2c) as draw:
                # draw.rectangle(device.bounding_box, outline="white", fill="black")
                # draw.text((20, 10), "SPI OLED", fill="white")
                # draw.text((10, 30), "SSD1306 OK", fill="white")
                draw.point((2, 2), fill="white")
            time.sleep(0.1)
    except KeyboardInterrupt:
        pass
    finally:
        device_spi.clear()
        device_i2c.clear()
        device_spi.cleanup()
        device_i2c.cleanup()

if __name__ == "__main__":
    main()
