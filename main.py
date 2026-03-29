import lcd
import image
import time
import uos
import utime
import sys
from pmu import axp192

pmu = axp192()
pmu.enablePMICSleepMode(True)

lcd.init()
lcd.rotation(2)

try:
    img = image.Image("/flash/startup.jpg")
    lcd.display(img)
except:
    lcd.draw_string(lcd.width()//2-100,lcd.height()//2-4, "Error: Cannot find start.jpg", lcd.WHITE, lcd.RED)

from Maix import I2S, GPIO
import audio
from Maix import GPIO
from fpioa_manager import *

fm.register(board_info.SPK_SD, fm.fpioa.GPIO0)
spk_sd = GPIO(GPIO.GPIO0, GPIO.OUT)
spk_sd.value(1)

fm.register(board_info.SPK_DIN,   fm.fpioa.I2S0_OUT_D1)
fm.register(board_info.SPK_BCLK,  fm.fpioa.I2S0_SCLK)
fm.register(board_info.SPK_LRCLK, fm.fpioa.I2S0_WS)

wav_dev = I2S(I2S.DEVICE_0)
try:
    player = audio.Audio(path="/flash/ding.wav")
    player.volume(100)
    wav_info = player.play_process(wav_dev)
    wav_dev.channel_config(wav_dev.CHANNEL_1, I2S.TRANSMITTER,
                           resolution=I2S.RESOLUTION_16_BIT,
                           align_mode=I2S.STANDARD_MODE)
    wav_dev.set_sample_rate(wav_info[1])
    while True:
        ret = player.play()
        if ret == None or ret == 0:
            break
    player.finish()
except:
    pass

fm.register(board_info.BUTTON_A, fm.fpioa.GPIO1)
but_a = GPIO(GPIO.GPIO1, GPIO.IN, GPIO.PULL_UP)

fm.register(board_info.BUTTON_B, fm.fpioa.GPIO2)
but_b = GPIO(GPIO.GPIO2, GPIO.IN, GPIO.PULL_UP)

fm.register(board_info.LED_W, fm.fpioa.GPIO3)
led_w = GPIO(GPIO.GPIO3, GPIO.OUT)
led_w.value(1)

time.sleep(0.5)

import sensor

err_counter = 0
while 1:
    try:
        sensor.reset()
        break
    except:
        err_counter += 1
        if err_counter == 20:
            lcd.draw_string(lcd.width()//2-100, lcd.height()//2-4,
                            "Error: Sensor Init Failed", lcd.WHITE, lcd.RED)
        time.sleep(0.1)

sensor.set_pixformat(sensor.RGB565)
sensor.set_framesize(sensor.QVGA)
sensor.set_vflip(1)
sensor.run(1)
sensor.skip_frames(time=300)

# Grove UART — CONNEXT_A (pin 35) = TX, CONNEXT_B (pin 34) = RX
uart = None
try:
    from machine import UART
    fm.register(35, fm.fpioa.UART2_TX, force=True)
    fm.register(34, fm.fpioa.UART2_RX, force=True)
    uart = UART(UART.UART2, 115200)
    print("UART ready on Grove @ 115200")
except Exception as e:
    print("UART init failed:", e)



# SD card
sd_ok = False
try:
    uos.listdir("/sd")
    try:
        uos.mkdir("/sd/photos")
    except OSError:
        pass
    sd_ok = True
except Exception as e:
    print("SD err:", e)

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

def last_filename():
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
    if not nums:
        return None
    return "/sd/photos/img_{:04d}.jpg".format(max(nums))

def show_last_photo():
    fname = last_filename()
    if fname is None:
        print("No photos yet")
        return
    try:
        print("Showing:", fname)
        img = image.Image(fname)
        lcd.display(img)
    except Exception as e:
        print("show_last err:", e)

def send_photo_uart(filename):
    if uart is None:
        print("UART not available")
        return
    try:
        with open(filename, 'rb') as f:
            data = f.read()
        chunk = 64
        uart.write("STICKV_IMG:{}\n".format(len(data)).encode())
        # Send in small chunks so sensor.snapshot() can still run between them
        for i in range(0, len(data), chunk):
            uart.write(data[i:i+chunk])
        uart.write(b"STICKV_END\n")
        print("UART sent {} bytes".format(len(data)))
    except Exception as e:
        print("UART send err:", e)

def shutter_flash():
    led_w.value(0)
    utime.sleep_ms(80)
    led_w.value(1)

# Read actual button state — avoids ghost press on boot
photo_count = 0
preview_on  = True
btn_a_prev  = but_a.value()
btn_b_prev  = but_b.value()

while True:
    try:
        img = sensor.snapshot()

        btn_a_now = but_a.value()
        btn_b_now = but_b.value()

        if btn_b_prev == 1 and btn_b_now == 0:
            # Show last saved photo from SD (file image, not sensor buffer)
            show_last_photo()
            utime.sleep_ms(2000)   # hold it on screen 2 seconds
            utime.sleep_ms(50)

        if btn_a_prev == 1 and btn_a_now == 0:
            if sd_ok:
                fname = next_filename()
                img.save(fname, quality=85)
                photo_count += 1
                print("Saved:", fname)
                shutter_flash()
                send_photo_uart(fname)
            utime.sleep_ms(50)

        btn_a_prev = btn_a_now
        btn_b_prev = btn_b_now

        if preview_on:
            pass  # live sensor preview not working yet — screen stays blank

    except Exception as e:
        print("loop err:", e)
        utime.sleep_ms(200)
