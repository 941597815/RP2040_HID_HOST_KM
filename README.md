# RP2040_HID_KM

使用 RP2040 制作 HID 键鼠&HOST,可通过 Python 或其它编程语言控制调用

将tinyusb放在host_hid目录下,编译:

 mkdir build
 cd build
 cmake -G Ninja -DBOARD=raspberry_pi_pico ..
 ninja

通过 dll 调用时需要把 arduino.dll 与 hidapi.dll 放在同一目录


