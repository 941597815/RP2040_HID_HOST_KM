# RP2040_HID_KM

使用 RP2040 制作 HID 键鼠&HOST,可通过 Python 或其它编程语言控制调用

将tinyusb@0.18.0放在 host_hid 目录下并重命名为 tinyusb,编译:

```bash
 mkdir build
 cd build
 cmake -G Ninja -DBOARD=raspberry_pi_pico ..
 ninja
```

通过 dll 调用时需要把 rp2040_host_dll.dll 与 hidapi.dll 放在同一目录
