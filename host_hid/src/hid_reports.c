// hid_reports.c
#include "hid_reports.h"
#include "usb_descriptors.h"

/* -----------------------------------------------------------------
 -----------------        HID REPORT API   ------------------------------
 -----------------------------------------------------------------*/
bool send_keyboard_report(uint8_t modifier, const uint8_t keycode[6])
{
  return tud_hid_n_keyboard_report(REPORT_ID_KEYBOARD, 0, modifier, keycode);
}

bool send_mouse_report(uint8_t buttons, int8_t x, int8_t y, int8_t vertical, int8_t horizontal)
{
  return tud_hid_n_mouse_report(REPORT_ID_MOUSE, 0, buttons, x, y, vertical, horizontal);
}

bool send_gamepad_report(int8_t x, int8_t y, int8_t z, int8_t rz, int8_t rx, int8_t ry, uint8_t hat, uint32_t buttons)
{
  return tud_hid_n_gamepad_report(REPORT_ID_GAMEPAD, 0, x, y, z, rz, rx, ry, hat, buttons);
}
bool send_gamepad_report_data(const hid_gamepad_report_t *report, uint16_t len)
{
  return tud_hid_n_report(REPORT_ID_GAMEPAD, 0, report, len);
}

/* -----------------------------------------------------------------
 -----------------        MOUSE API   ------------------------------
 -----------------------------------------------------------------*/
// 全局鼠标状态
static hid_mouse_state_t g_mouse_state = {0};

bool mouse_press(uint8_t button)
{
  g_mouse_state.buttons |= button;
  return send_mouse_report(g_mouse_state.buttons, g_mouse_state.x, g_mouse_state.y, g_mouse_state.wheel, g_mouse_state.pan);
}

bool mouse_release(uint8_t button)
{
  g_mouse_state.buttons &= ~button;
  return send_mouse_report(g_mouse_state.buttons, g_mouse_state.x, g_mouse_state.y, g_mouse_state.wheel, g_mouse_state.pan);
}

// bool mouse_click(uint8_t button) {
//   bool ret = mouse_press(button);
//   if (!ret) return false;
//   return mouse_release(button);// 注意：这会在同一帧内发送两次报告,报告丢失
// }

bool mouse_move(int8_t x, int8_t y)
{
  // 保存位移
  g_mouse_state.x = x;
  g_mouse_state.y = y;

  // 发送报告
  bool ret = send_mouse_report(g_mouse_state.buttons, x, y, g_mouse_state.wheel, g_mouse_state.pan);

  // 发送后清零（因为是相对位移，避免重复移动）
  g_mouse_state.x = 0;
  g_mouse_state.y = 0;

  return ret;
}

bool mouse_scroll(int8_t vertical, int8_t horizontal)
{
  // 保存滚轮值
  g_mouse_state.wheel = vertical;
  g_mouse_state.pan = horizontal;

  // 发送报告
  bool ret = send_mouse_report(g_mouse_state.buttons, g_mouse_state.x, g_mouse_state.y, vertical, horizontal);

  // 发送后清零（避免重复滚动）
  g_mouse_state.wheel = 0;
  g_mouse_state.pan = 0;

  return ret;
}
