// hid_reports.h
#ifndef HID_REPORTS_H_
#define HID_REPORTS_H_

#include "tusb.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

   /*================================================= DEVICE API ================================================*/
  bool send_keyboard_report(uint8_t modifier, const uint8_t keycode[6]);
  bool send_mouse_report(uint8_t buttons, int8_t x, int8_t y, int8_t vertical, int8_t horizontal);
  bool send_gamepad_report(int8_t x, int8_t y, int8_t z, int8_t rz, int8_t rx, int8_t ry, uint8_t hat, uint32_t buttons);
  bool send_gamepad_report_data(const hid_gamepad_report_t *report, uint16_t len);

// 鼠标按钮定义（标准）
#define MOUSE_BUTTON_LEFT (1 << 0)
#define MOUSE_BUTTON_RIGHT (1 << 1)
#define MOUSE_BUTTON_MIDDLE (1 << 2)
#define MOUSE_BUTTON_4 (1 << 3) // 浏览器后退
#define MOUSE_BUTTON_5 (1 << 4) // 浏览器前进

  // 鼠标状态结构体（维护当前状态）
  typedef struct
  {
    uint8_t buttons; // 当前按下的按键 bitmask
    int8_t x;        // X 轴相对位移
    int8_t y;        // Y 轴相对位移
    int8_t wheel;    // 垂直滚轮（通常 -127 ~ +127）
    int8_t pan;      // 水平滚轮（可选）
  } hid_mouse_state_t;

  // 按下按键
  bool mouse_press(uint8_t button);

  // 释放按键
  bool mouse_release(uint8_t button);

  // 点击（按下 + 释放）
  bool mouse_click(uint8_t button);

  // 移动（相对位移）
  bool mouse_move(int8_t x, int8_t y);

  // 滚轮（垂直 + 水平）
  bool mouse_scroll(int8_t vertical, int8_t horizontal);

#ifdef __cplusplus
}
#endif

#endif // HID_REPORTS_H_