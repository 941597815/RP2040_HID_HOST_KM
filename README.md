# HID_KM_HOST

使用 RP2040/RP2350 制作 HID 键鼠&HOST,可通过 Python 或其它编程语言控制调用

​	rp2040_host_dll文件夹为dll源代码，通过 dll 调用时需要把 rp2040_host_dll.dll 与 hidapi.dll 放在同一目录

​	host_hid文件夹为固件源代码



## 编译固件:

##### 配置编译环境

1. 将tinyusb@0.18.0放在 host_hid 目录下并重命名为 tinyusb：https://github.com/hathach/tinyusb/archive/refs/tags/0.18.0.zip

​		在项目根目录打开终端，输入以下命初始化tinyusb工具

```bash
cd host_hid/tinyusb
python tools/get_deps.py rp2040
```

2. 下载并设置pico-sdk ：https://github.com/raspberrypi/pico-sdk/archive/refs/tags/2.2.0.zip

​		添加系统环境变量   变量名：PICO_SDK_PATH    变量值：解压后的pico-sdk根目录

3. 安装arm-none-eabi-gcc编译工具链：https://github.com/xpack-dev-tools/arm-none-eabi-gcc-xpack/releases/download/v14.2.1-1.1/xpack-arm-none-eabi-gcc-14.2.1-1.1-win32-x64.zip

​		解压后将bin目录添加到系统环境变量path中

4. 安装ninja：https://github.com/ninja-build/ninja/releases/download/v1.13.1/ninja-win.zip

​		解压后将ninja目录添加到系统环境变量path中



##### 编译固件

在项目根目录打开终端

```bash
 cd host_hid
 mkdir build
 cd build
 #如果使用rp2040
 cmake -G Ninja -DBOARD=raspberry_pi_pico ..
 #如果使用rp2350，同时需要修改RP2040_HID_HOST_KM\host_hid\tinyusb\hw\bsp\rp2040目录下的board.h #define PICO_DEFAULT_PIO_USB_DP_PIN 16 --> 12
 cmake -G Ninja -DBOARD=raspberry_pi_pico2 ..
 ninja
```





如果鼠标无法正常工作（按键错乱，移动不正确），你需要在main.c这个函数中tuh_hid_report_received_cb解析你的鼠标报告

你可以修改tusb_config.h中的以下内容来设置设备信息，以及开启cdc调试：

// ------------usersetting----begin--------------//
#define USE_CDC 0     /* 1 = 调试模式带CDC，0 = 生产模式纯HID */
#define INTERVAL_MS 1 // devices speed 1=1khz,2=500hz,4=250hz,8=125hz

// USB Vendor ID (VID) and Product ID (PID)
// These should be obtained from USB-IF for a unique identification
#define USB_VID 0x046D // Example VID
#define USB_PID 0xC08B // Example PID

// Device name, manufacturer, and SN
#define USB_MANUFACTURER "Logitech"          // 生产商
#define USB_PRODUCT "G502 HERO Gaming Mouse" // 设备名
#define USB_SERIAL_NUMBER "S1F8M-2Q5KL-7CD"  // SN

// Maximum power consumption in milliamps
#define USB_MAX_POWER_MA 500 // max <= 500

// Firmware version
#define USB_FIRMWARE_VERSION 0x0106 // 固件版本
// ------------usersetting----end--------------//



不要修改其他内容，除非你知道自己在干什么

