# Camera app — exec()'d by boot.py (lcd, sensor, time already imported/init'd)
import uos
import utime
from Maix import GPIO
from fpioa_manager import fm
from board import board_info

# Re-register pins
fm.register(board_info.BUTTON_A, fm.fpioa.GPIOHS21, force=True)
fm.register(board_info.BUTTON_B, fm.fpioa.GPIOHS22, force=True)
fm.register(board_info.LED_W,    fm.fpioa.GPIO3,    force=True)

btn_a = GPIO(GPIO.GPIOHS21, GPIO.IN, GPIO.PULL_UP)
btn_b = GPIO(GPIO.GPIOHS22, GPIO.IN, GPIO.PULL_UP)
led_w = GPIO(GPIO.GPIO3, GPIO.OUT, value=1)  # active-low: 1=off

# Sensor format (reset already done by boot.py)
sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.set_vflip(1)
sensor.run(1)
sensor.skip_frames(time=500)

# Re-init LCD AFTER sensor — this ordering matters on this firmware
lcd.init()
lcd.rotation(2)

# SD card
sd_ok = False
try:
    uos.listdir("/sd")
    try:
        uos.mkdir("/sd/photos")
    except OSError:
        pass
    sd_ok = True
except Exception:
    pass

def next_filename():
    try:
        existing = uos.listdir("/sd/photos")
    except Exception:
        existing = []
    nums = []
    for name in existing:
        if name.startswith("img_") and name.endswith(".jpg"):
            try:
                nums.append(int(name[4:8]))
            except ValueError:
                pass
    n = max(nums) + 1 if nums else 0
    return "/sd/photos/img_{:04d}.jpg".format(n)

def shutter_flash():
    led_w.value(0)
    utime.sleep_ms(80)
    led_w.value(1)

# Read actual button state before loop to avoid ghost press
photo_count = 0
preview_on  = True
btn_a_prev  = btn_a.value()
btn_b_prev  = btn_b.value()

while True:
    try:
        img = sensor.snapshot()

        btn_a_now = btn_a.value()
        btn_b_now = btn_b.value()

        # Button B: toggle preview
        if btn_b_prev == 1 and btn_b_now == 0:
            preview_on = not preview_on
            utime.sleep_ms(50)

        # Button A: snap photo
        if btn_a_prev == 1 and btn_a_now == 0 and sd_ok:
            fname = next_filename()
            img.save(fname, quality=85)
            photo_count += 1
            print("Saved:", fname)
            shutter_flash()
            utime.sleep_ms(300)

        btn_a_prev = btn_a_now
        btn_b_prev = btn_b_now

        if preview_on:
            lcd.display(img)
        else:
            lcd.clear(lcd.BLACK)

    except Exception as e:
        print("loop err:", e)
        utime.sleep_ms(200)
