/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include "usb_descriptors.h"
#include "bsp/board_api.h"
#include "tusb.h"

/* A combination of interfaces must have a unique product id, since PC will save device driver after the first plug.
 * Same VID/PID with different interface e.g MSC (first), then CDC (later) will possibly cause system error on PC.
 *
 * Auto ProductID layout's Bitmap:
 *   [MSB]         HID | MSC | CDC          [LSB]
 */
#define _PID_MAP(itf, n) ((CFG_TUD_##itf) << (n))
// #define USB_PID (0x4000 | (CFG_TUD_CDC << 0) | (CFG_TUD_HID << 2))

// #define USB_VID 0xCafe
#define USB_BCD 0x0200

//--------------------------------------------------------------------+
// Device Descriptors
//--------------------------------------------------------------------+
tusb_desc_device_t const desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0xEF,// Composite
    .bDeviceSubClass = 0x02,
    .bDeviceProtocol = 0x01,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = USB_VID,
    .idProduct = USB_PID,
    .bcdDevice = USB_FIRMWARE_VERSION,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01};

// Invoked when received GET DEVICE DESCRIPTOR
// Application return pointer to descriptor
uint8_t const *tud_descriptor_device_cb(void) {
  return (uint8_t const *) &desc_device;
}

//--------------------------------------------------------------------+
// HID Report Descriptor
//--------------------------------------------------------------------+

// 键盘报告描述符
uint8_t const hid_report_desc_keyboard[] = {
    TUD_HID_REPORT_DESC_KEYBOARD()};

// 鼠标报告描述符
uint8_t const hid_report_desc_mouse[] = {
    TUD_HID_REPORT_DESC_MOUSE()};

// 游戏手柄报告描述符
uint8_t const hid_report_desc_gamepad[] = {
    TUD_HID_REPORT_DESC_GAMEPAD()};

// 自定义 IN/OUT 报告描述符（64字节）
uint8_t const hid_report_desc_custom[] = {
    TUD_HID_REPORT_DESC_GENERIC_INOUT(64)};


// Invoked when received GET HID REPORT DESCRIPTOR
// Application return pointer to descriptor
// Descriptor contents must exist long enough for transfer to complete
uint8_t const *tud_hid_descriptor_report_cb(uint8_t itf) {
  // return hid_report_desc_custom;
  if (itf == REPORT_ID_KEYBOARD) {
    return hid_report_desc_keyboard;
  } else if (itf == REPORT_ID_MOUSE) {
    return hid_report_desc_mouse;
  } else if (itf == REPORT_ID_GAMEPAD) {
    return hid_report_desc_gamepad;
  } else if (itf == REPORT_ID_CUSTOM) {
    return hid_report_desc_custom;
  }
  return NULL;
}

//--------------------------------------------------------------------+
// Configuration Descriptor
//--------------------------------------------------------------------+

enum {
#if USE_CDC
  ITF_NUM_CDC,
  ITF_NUM_CDC_DATA,
#endif
  ITF_NUM_KEYBOARD,
  ITF_NUM_MOUSE,
  ITF_NUM_GAMEPAD,
  ITF_NUM_CUSTOM,
  ITF_NUM_TOTAL
};

#define CONFIG_TOTAL_LEN \
  (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN * 3 + TUD_HID_INOUT_DESC_LEN + (USE_CDC ? TUD_CDC_DESC_LEN : 0))


#if USE_CDC
  #define EPNUM_CDC_NOTIF 0x81
  #define EPNUM_CDC_OUT 0x02
  #define EPNUM_CDC_IN 0x82
#endif

// HID 端点（每个接口独立）
#define EPNUM_KEYBOARD 0x83
#define EPNUM_MOUSE 0x84
#define EPNUM_GAMEPAD 0x85
#define EPNUM_CUSTOM_IN 0x86
#define EPNUM_CUSTOM_OUT 0x06


uint8_t const desc_configuration[] = {
    // 配置描述符头
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, USB_MAX_POWER_MA),

#if USE_CDC
    // CDC 接口
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, EPNUM_CDC_NOTIF, 8, EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
#endif

    // 键盘 HID 接口（Boot Interface, IN only）
    TUD_HID_DESCRIPTOR(ITF_NUM_KEYBOARD, 0, HID_ITF_PROTOCOL_KEYBOARD,
                       sizeof(hid_report_desc_keyboard), EPNUM_KEYBOARD, 8, INTERVAL_MS),

    // 鼠标 HID 接口（Boot Interface, IN only）
    TUD_HID_DESCRIPTOR(ITF_NUM_MOUSE, 0, HID_ITF_PROTOCOL_MOUSE,
                       sizeof(hid_report_desc_mouse), EPNUM_MOUSE, 8, INTERVAL_MS),

    // 游戏手柄 HID 接口（Generic, IN only）
    TUD_HID_DESCRIPTOR(ITF_NUM_GAMEPAD, 0, HID_ITF_PROTOCOL_NONE,
                       sizeof(hid_report_desc_gamepad), EPNUM_GAMEPAD, 16, INTERVAL_MS),

    // 自定义 HID 接口（IN + OUT）
    TUD_HID_INOUT_DESCRIPTOR(ITF_NUM_CUSTOM, 0, HID_ITF_PROTOCOL_NONE,
                             sizeof(hid_report_desc_custom), EPNUM_CUSTOM_IN, EPNUM_CUSTOM_OUT, 64, INTERVAL_MS),
};

#if TUD_OPT_HIGH_SPEED
// Per USB specs: high speed capable device must report device_qualifier and other_speed_configuration

// other speed configuration
uint8_t desc_other_speed_config[CONFIG_TOTAL_LEN];

// device qualifier is mostly similar to device descriptor since we don't change configuration based on speed
tusb_desc_device_qualifier_t const desc_device_qualifier =
    {
        .bLength = sizeof(tusb_desc_device_qualifier_t),
        .bDescriptorType = TUSB_DESC_DEVICE_QUALIFIER,
        .bcdUSB = USB_BCD,

        .bDeviceClass = 0x00,
        .bDeviceSubClass = 0x00,
        .bDeviceProtocol = 0x00,

        .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
        .bNumConfigurations = 0x01,
        .bReserved = 0x00};

// Invoked when received GET DEVICE QUALIFIER DESCRIPTOR request
// Application return pointer to descriptor, whose contents must exist long enough for transfer to complete.
// device_qualifier descriptor describes information about a high-speed capable device that would
// change if the device were operating at the other speed. If not highspeed capable stall this request.
uint8_t const *tud_descriptor_device_qualifier_cb(void) {
  return (uint8_t const *) &desc_device_qualifier;
}

// Invoked when received GET OTHER SEED CONFIGURATION DESCRIPTOR request
// Application return pointer to descriptor, whose contents must exist long enough for transfer to complete
// Configuration descriptor in the other speed e.g if high speed then this is for full speed and vice versa
uint8_t const *tud_descriptor_other_speed_configuration_cb(uint8_t index) {
  (void) index;// for multiple configurations

  // other speed config is basically configuration with type = OHER_SPEED_CONFIG
  memcpy(desc_other_speed_config, desc_configuration, CONFIG_TOTAL_LEN);
  desc_other_speed_config[1] = TUSB_DESC_OTHER_SPEED_CONFIG;

  // this example use the same configuration for both high and full speed mode
  return desc_other_speed_config;
}

#endif// highspeed

// Invoked when received GET CONFIGURATION DESCRIPTOR
// Application return pointer to descriptor
// Descriptor contents must exist long enough for transfer to complete
uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
  (void) index;// for multiple configurations

  // This example use the same configuration for both high and full speed mode
  return desc_configuration;
}

//--------------------------------------------------------------------+
// String Descriptors
//--------------------------------------------------------------------+

// String Descriptor Index
enum {
  STRID_LANGID = 0,
  STRID_MANUFACTURER,
  STRID_PRODUCT,
  STRID_SERIAL,
};

// array of pointer to string descriptors
char const *string_desc_arr[] =
    {
        (const char[]) {0x09, 0x04},// 0: is supported language is English (0x0409)
        USB_MANUFACTURER,           // 1: Manufacturer
        USB_PRODUCT,                // 2: Product
        USB_SERIAL_NUMBER,          // 3: Serials, should use chip ID
};

static uint16_t _desc_str[32 + 1];

// Invoked when received GET STRING DESCRIPTOR request
// Application return pointer to descriptor, whose contents must exist long enough for transfer to complete
// uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
//   (void) langid;
//   size_t chr_count;

//   switch (index) {
//     case STRID_LANGID:
//       memcpy(&_desc_str[1], string_desc_arr[0], 2);
//       chr_count = 1;
//       break;

//     case STRID_SERIAL:
//       chr_count = board_usb_get_serial(_desc_str + 1, 32);

//       break;

//     default:
//       // Note: the 0xEE index string is a Microsoft OS 1.0 Descriptors.
//       // https://docs.microsoft.com/en-us/windows-hardware/drivers/usbcon/microsoft-defined-usb-descriptors

//       if (!(index < sizeof(string_desc_arr) / sizeof(string_desc_arr[0]))) return NULL;

//       const char *str = string_desc_arr[index];

//       // Cap at max char
//       chr_count = strlen(str);
//       size_t const max_count = sizeof(_desc_str) / sizeof(_desc_str[0]) - 1;// -1 for string type
//       if (chr_count > max_count) chr_count = max_count;

//       // Convert ASCII string into UTF-16
//       for (size_t i = 0; i < chr_count; i++) {
//         _desc_str[1 + i] = str[i];
//       }
//       break;
//   }

//   // first byte is length (including header), second byte is string type
//   _desc_str[0] = (uint16_t) ((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));

//   return _desc_str;
// }

// 修改字符串描述符回调函数
uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
  (void) langid;
  size_t chr_count = 0;
  const char *str = NULL;

  // 清空描述符缓冲区
  memset(_desc_str, 0, sizeof(_desc_str));

  switch (index) {
    case STRID_LANGID:
      // 语言ID特殊处理（保持不变）
      memcpy(&_desc_str[1], string_desc_arr[0], 2);
      chr_count = 1;
      break;

    case STRID_SERIAL:
      // 序列号：使用数组中的定义，确保字符有效
      str = string_desc_arr[STRID_SERIAL];
      if (str != NULL && strlen(str) > 0) {
        chr_count = strlen(str);
        // 验证字符有效性（可选，但推荐）
        for (size_t i = 0; i < chr_count; i++) {
          uint8_t ch = (uint8_t) str[i];
          if (ch < 0x20 || ch > 0x7F || ch == 0x2C) {// 0x2C是逗号，无效
            chr_count = 0;                           // 无效序列号
            break;
          }
        }
      }
      break;

    default:
      // 其他字符串（制造商、产品名等）
      if (index < (sizeof(string_desc_arr) / sizeof(string_desc_arr[0]))) {
        str = string_desc_arr[index];
        chr_count = strlen(str);
      }
      break;
  }

  // 将ASCII字符串转换为UTF-16LE（适用于所有非语言ID字符串）
  if (index != STRID_LANGID && str != NULL && chr_count > 0) {
    // 限制最大长度
    size_t max_chars = (sizeof(_desc_str) / sizeof(_desc_str[0])) - 1;
    if (chr_count > max_chars) chr_count = max_chars;

    // 转换为UTF-16LE
    for (size_t i = 0; i < chr_count; i++) {
      _desc_str[1 + i] = (uint16_t) str[i];
    }
  }

  // 设置描述符头（长度和类型）
  if (chr_count > 0) {
    _desc_str[0] = (uint16_t) ((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return _desc_str;
  }

  return NULL;// 无效索引或空字符串
}