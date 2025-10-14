#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsp/board_api.h"
// #include "class/hid/hid_host.h"
#include "device/usbd_pvt.h"
#include "tusb.h"

#include "hid_reports.h"
#include "usb_descriptors.h"

// 添加多核支持
#include "pico/multicore.h"
#include "pico/mutex.h"
#include "pico/util/queue.h" // 添加队列支持

//--------------------------------------------------------------------+
// MACRO CONSTANT TYPEDEF PROTYPES
//--------------------------------------------------------------------+

/* Blink pattern
 * - 250 ms  : device not mounted
 * - 1000 ms : device mounted
 * - 2500 ms : device is suspended
 */
enum
{
  BLINK_INIT_MOUNTED = 50,
  BLINK_NOT_MOUNTED = 250,
  BLINK_MOUNTED = 1000,
  BLINK_SUSPENDED = 2500,
};
static uint32_t blink_interval_ms = BLINK_NOT_MOUNTED;
void led_blinking_task(void);

// 自定义设备上下文
typedef struct
{
  uint8_t dev_addr;
  uint8_t idx;
  uint8_t protocol;
} hid_device_t;
hid_device_t hid_devices[CFG_TUH_HID];

// 双核共享变量
static int mouse_to_gamepade = 0;

// 互斥锁保护共享变量
// static mutex_t usb_host_mutex;
static mutex_t mouse_to_gamepade_mutex;

// 键盘报告结构体
typedef struct
{
  uint8_t modifier;   // 修饰键 (Ctrl, Shift, Alt 等)
  uint8_t keycode[6]; // 普通按键码
} keyboard_report_t;

// 鼠标报告结构
typedef struct
{
  uint8_t buttons;
  int8_t x;
  int8_t y;
  int8_t wheel;
} mouse_report_t;

// 游戏手柄报告结构体
typedef struct
{
  int8_t x;         // 左摇杆X轴 (-127 到 127)
  int8_t y;         // 左摇杆Y轴 (-127 到 127)
  int8_t z;         // 右摇杆X轴 (-127 到 127)
  int8_t rz;        // 右摇杆Y轴 (-127 到 127)
  int8_t rx;        // 左触发器 (-127 到 127)
  int8_t ry;        // 右触发器 (-127 到 127)
  uint8_t hat;      // 方向键状态 (0-8)
  uint32_t buttons; // 按钮状态 (32位掩码)
} gamepad_report_t;

// 全局变量
#define IDLE_TIMEOUT_MS 10 // 50ms无操作后回中
static bool should_center = false;
static uint32_t last_mouse_move_time = 0;
mouse_report_t last_rpt = {0};

// 创建队列（大小根据需求调整）
#define KEYBOARD_QUEUE_SIZE 8
#define MOUSE_QUEUE_SIZE 8
#define GAMEPAD_QUEUE_SIZE 8
static queue_t keyboard_report_queue;
static queue_t mouse_report_queue;
static queue_t gamepad_report_queue;

// cdc
#if USE_CDC
// 声明
static void __attribute__((format(printf, 1, 2)))
cdc_debug_print(const char *fmt, ...);

// 定义
static void cdc_debug_print(const char *fmt, ...)
{
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (n <= 0)
    return;

  /* 不等待连接，不阻塞主线 */
  if (!tud_cdc_connected())
    return;

  uint32_t len = (uint32_t)n;
  const uint8_t *p = (const uint8_t *)buf;

  while (len)
  {
    uint32_t avail = tud_cdc_write_available();
    if (avail == 0)
    {
      tud_task(); // tinyusb device task
      avail = tud_cdc_write_available();
      if (avail == 0)
        break;
    }
    uint32_t chunk = (len > avail) ? avail : len;
    tud_cdc_write(p, chunk);
    p += chunk;
    len -= chunk;
  }
  tud_cdc_write_flush(); // 把已写入部分推出去
}

/* 可变参宏：支持 0 个或更多参数 */
#define CDC_LOG(...) cdc_debug_print(__VA_ARGS__)

#else
#define CDC_LOG(...) ((void)0)
#endif

// 核心1入口函数 - 运行主机任务
void core1_entry(void)
{
  // 初始化主机栈
  tusb_rhport_init_t host_init = {
      .role = TUSB_ROLE_HOST,
      .speed = TUSB_SPEED_FULL};
  tusb_init(BOARD_TUH_RHPORT, &host_init);

  // 主机任务循环
  while (1)
  {
    // mutex_enter_blocking(&usb_host_mutex);
    tuh_task(); // 主机任务运行在核心1
    // mutex_exit(&usb_host_mutex);
    led_blinking_task();
  }
}
// 全局变量（或在main函数内声明为static）
void check_hz(uint32_t *count)
{
  static uint32_t last_rate_print = 0;

  uint32_t now = time_us_32();

  if (now - last_rate_print > 1000000)
  { // 1秒
    CDC_LOG("Processed report rate: %lu Hz\n", (unsigned long)*count);
    *count = 0;
    last_rate_print = now;
  }
}
void log_endpoint_status(uint8_t ep_addr)
{
  (void)ep_addr;
  CDC_LOG("端点 0x%02X 状态: ", ep_addr);
  CDC_LOG("就绪=%d, ", usbd_edpt_ready(0, ep_addr));
  CDC_LOG("繁忙=%d, ", usbd_edpt_busy(0, ep_addr));
  CDC_LOG("停滞=%d\n", usbd_edpt_stalled(0, ep_addr));
}
/*------------- MAIN -------------*/
int main(void)
{
  board_init();

  // 初始化互斥锁
  mutex_init(&mouse_to_gamepade_mutex);
  // mutex_init(&usb_host_mutex);
  // 初始化队列
  queue_init(&keyboard_report_queue, sizeof(keyboard_report_t), KEYBOARD_QUEUE_SIZE);
  queue_init(&mouse_report_queue, sizeof(mouse_report_t), MOUSE_QUEUE_SIZE);
  queue_init(&gamepad_report_queue, sizeof(gamepad_report_t), GAMEPAD_QUEUE_SIZE);

  // 初始化设备栈
  tusb_rhport_init_t dev_init = {
      .role = TUSB_ROLE_DEVICE,
      .speed = TUSB_SPEED_FULL};
  tusb_init(BOARD_TUD_RHPORT, &dev_init);

  if (board_init_after_tusb)
  {
    board_init_after_tusb();
  }

  // 启动核心1运行主机任务
  multicore_launch_core1(core1_entry);

  // 核心0运行设备任务、CDC 和 LED
  while (1)
  {
    tud_task(); // tinyusb device task

    // 处理键盘报告队列
    keyboard_report_t kbd_report;
    if (usbd_edpt_ready(0, 0x83) && queue_try_remove(&keyboard_report_queue, &kbd_report))
    {
      // 转发键盘报告到 USB 设备
      bool success = tud_hid_n_keyboard_report(REPORT_ID_KEYBOARD, 0, kbd_report.modifier, kbd_report.keycode);

      if (!success)
      {
        CDC_LOG("Failed to send keyboard report, re-queuing.\n");
        // 发送失败，尝试重新放回队列（如果队列未满）
        queue_try_add(&keyboard_report_queue, &kbd_report);
      }
      else
      {
        // CDC_LOG("Keyboard report forwarded successfully.\n");
      }
    }

    // 处理鼠标报告队列
    mouse_report_t rpt;
    // 先获取锁
    mutex_enter_blocking(&mouse_to_gamepade_mutex);
    int convert_to_gamepad = mouse_to_gamepade;
    mutex_exit(&mouse_to_gamepade_mutex);
    if (usbd_edpt_ready(0, 0x84) && queue_try_remove(&mouse_report_queue, &rpt))
    {
      // uint32_t queue_level = queue_get_level(&mouse_report_queue);
      // uint32_t queue_capacity = MOUSE_QUEUE_SIZE;

      // // 修复格式说明符 - 统一使用 PRIu32
      // CDC_LOG("取出前队列: %" PRIu32 "/%" PRIu32 "\n", queue_level + 1, queue_capacity);
      // log_endpoint_status(0x84);

      // 同步发送到CUSTOM IN/OUT端点
      uint8_t custom_report[64] = {0};     // 初始化为全0
      custom_report[0] = REPORT_ID_CUSTOM; // 设置Report ID
      // 将鼠标数据复制到自定义报告中
      memcpy(&custom_report[1], &rpt, sizeof(mouse_report_t)); // 从第2字节开始复制
      // 发送64字节报告
      tud_hid_n_report(REPORT_ID_CUSTOM, 0, custom_report, 64);

      bool success = false;

      if (convert_to_gamepad == 1)
      {
        // 映射到手柄
        tud_hid_n_gamepad_report(REPORT_ID_GAMEPAD, 0, 0, 0, (rpt.x > 0) ? 127 : ((rpt.x == 0) ? 0 : -127), (rpt.y > 0) ? 127 : ((rpt.y == 0) ? 0 : -127), 0, 0, 0, 0);
        rpt.x = 0;
        rpt.y = 0;
        success = tud_hid_n_mouse_report(REPORT_ID_MOUSE, 0, rpt.buttons, rpt.x, rpt.y, rpt.wheel, 0);
        last_mouse_move_time = board_millis();
      }
      else if (convert_to_gamepad == 2)
      {
        // 手动映射
        success = true;
      }
      else
      {
        success = tud_hid_n_mouse_report(REPORT_ID_MOUSE, 0, rpt.buttons, rpt.x, rpt.y, rpt.wheel, 0);
      }

      // 更新鼠标最后状态last_rpt
      last_rpt = rpt;

      if (!success)
      {
        CDC_LOG("发送失败，尝试重新放回队列\n");

        // 发送失败时重新放回队列
        if (!queue_try_add(&mouse_report_queue, &rpt))
        {
          CDC_LOG("警告：队列已满，无法放回报告\n");
        }
        else
        {
          CDC_LOG("报告已重新放回队列\n");
        }
      }
      else
      {
        // CDC_LOG("发送成功\n");
        // processed_count++;
      }

      // 取出后打印当前队列状态 - 同样修复格式说明符
      // uint32_t current_level = queue_get_level(&mouse_report_queue);
      // CDC_LOG("取出后队列: %" PRIu32 "/%" PRIu32 "\n", current_level, queue_capacity);
    }
    else
    {
      // 鼠标不再移动，手柄摇杆回中
      uint32_t now = board_millis();
      if (now - last_mouse_move_time > IDLE_TIMEOUT_MS)
      {
        should_center = true;
        last_mouse_move_time = now;
      }

      if (convert_to_gamepad == 1 && should_center && usbd_edpt_ready(0, 0x85))
      {
        tud_hid_n_gamepad_report(REPORT_ID_GAMEPAD, 0, 0, 0, 0, 0, 0, 0, 0, 0);
        should_center = false;
      }
    }

    // 处理手柄报告队列
    gamepad_report_t gmp_report;
    if (usbd_edpt_ready(0, 0x85) && queue_try_remove(&gamepad_report_queue, &gmp_report))
    {
      // 转发手柄报告到 USB 设备
      bool success = tud_hid_n_gamepad_report(
          REPORT_ID_GAMEPAD,
          0,
          gmp_report.x, gmp_report.y, gmp_report.z, gmp_report.rz,
          gmp_report.rx, gmp_report.ry, gmp_report.hat, gmp_report.buttons);

      if (!success)
      {
        CDC_LOG("Failed to send gamepad report, re-queuing.\n");
        // 发送失败，尝试重新放回队列（如果队列未满）
        if (!queue_try_add(&gamepad_report_queue, &gmp_report))
        {
          CDC_LOG("Warning: Gamepad queue full, report dropped.\n");
        }
      }
      else
      {
        CDC_LOG("Gamepad report forwarded: X=%d Y=%d Z=%d RZ=%d RX=%d RY=%d HAT=%d BTNS=0x%08" PRIX32 "\n",
                gmp_report.x, gmp_report.y, gmp_report.z, gmp_report.rz,
                gmp_report.rx, gmp_report.ry, gmp_report.hat, gmp_report.buttons);
      }
    }
    // check_hz(&processed_count);
  }

  return 0;
}

//--------------------------------------------------------------------+
// USB HID
//--------------------------------------------------------------------+

// 64位in/out命令解析
void process_hid_report(uint8_t const *report, uint16_t len)
{
  (void)len;
  uint8_t cmd_id = report[0];          // 第1字节是命令ID
  const uint8_t *payload = &report[1]; // 从第2字节开始是数据
  // size_t payload_len = 62;

  switch (cmd_id)
  {
  case HID_ITF_PROTOCOL_MOUSE:
  { // Mouse Report
    // CDC_LOG("MOUSE\n");
    uint8_t buttons = payload[0];
    int8_t x = (int8_t)payload[1];
    int8_t y = (int8_t)payload[2];
    int8_t wheel = (int8_t)payload[3];
    // int8_t pan = (int8_t) payload[4];
    // tud_hid_mouse_report(REPORT_ID_MOUSE, buttons, x, y, wheel, pan);
    // tud_hid_n_mouse_report(REPORT_ID_MOUSE, 0, buttons, x, y, wheel, pan);
    mouse_report_t rpt;
    if (queue_try_remove(&mouse_report_queue, &rpt))
    {
      buttons = rpt.buttons | buttons;
      x = rpt.x + x;
      y = rpt.y + y;
      wheel = rpt.wheel + wheel;
    }
    else
    {
      // 保持物理鼠标最后的按键状态
      buttons = last_rpt.buttons;
    }
    // tud_hid_n_mouse_report(REPORT_ID_MOUSE, 0, buttons, x, y, wheel, pan);
    // 4. 将报告加入队列
    mouse_report_t rpt_ = {
        .buttons = buttons,
        .x = x,
        .y = y,
        .wheel = wheel};

    if (!queue_try_add(&mouse_report_queue, &rpt_))
    {
      CDC_LOG("WARN: Mouse queue full, report dropped.\n");
    }
    else
    {
      CDC_LOG("Mouse report queued: buttons=%d x=%d y=%d wheel=%d\n", rpt.buttons, rpt.x, rpt.y, rpt.wheel);
    }

    break;
  }
  case HID_ITF_PROTOCOL_KEYBOARD:
  { // Keyboard Report
    // CDC_LOG("KEYBOARD\n");
    uint8_t modifier = payload[0];
    uint8_t keycode[6] = {0};
    memcpy(keycode, &payload[1], 6);
    tud_hid_n_keyboard_report(REPORT_ID_KEYBOARD, 0, modifier, keycode);
    break;
  }
  case HID_ITF_PROTOCOL_NONE:
  { // GAMEPAD
    // CDC_LOG("GAMEPAD\n");

    // 严格按照 hid_gamepad_report_t 结构解析
    int8_t x = (int8_t)payload[0];  // 字节0: 左摇杆X轴 (-127 到 127)
    int8_t y = (int8_t)payload[1];  // 字节1: 左摇杆Y轴 (-127 到 127)
    int8_t z = (int8_t)payload[2];  // 字节2: 右摇杆X轴 (-127 到 127)
    int8_t rz = (int8_t)payload[3]; // 字节3: 右摇杆Y轴 (-127 到 127)
    int8_t rx = (int8_t)payload[4]; // 字节4: 左触发器 (-127 到 127)
    int8_t ry = (int8_t)payload[5]; // 字节5: 右触发器 (-127 到 127)
    uint8_t hat = payload[6];       // 字节6: 方向键状态 (0-8)

    // 按钮状态（4字节小端序，与Python完全对应）
    uint32_t buttons = (uint32_t)payload[7] |         // 字节7: 按钮位0-7
                       ((uint32_t)payload[8] << 8) |  // 字节8: 按钮位8-15
                       ((uint32_t)payload[9] << 16) | // 字节9: 按钮位16-23
                       ((uint32_t)payload[10] << 24); // 字节10: 按钮位24-31

    // 调试输出，检查解析结果
    CDC_LOG("解析结果: X=%d, Y=%d, Z=%d, RZ=%d, RX=%d, RY=%d, HAT=%d, Buttons=0x%08" PRIX32 "\n",
            x, y, z, rz, rx, ry, hat, buttons);

    tud_hid_n_gamepad_report(REPORT_ID_GAMEPAD, 0, x, y, z, rz, rx, ry, hat, buttons);
    break;
  }
  case 3:
  {
    CDC_LOG("MOUSE_TO_GAMEPAD%d\n", payload[0]);

    mutex_enter_blocking(&mouse_to_gamepade_mutex);
    mouse_to_gamepade = (int)payload[0];
    mutex_exit(&mouse_to_gamepade_mutex);
    break;
  }
  default:
    break;
  }
}

// Invoked when received GET_REPORT control request
// Application must fill buffer report's content and return its length.
// Return zero will cause the stack to STALL request
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen)
{
  // TODO not Implemented
  (void)instance;
  (void)report_id;
  (void)report_type;
  (void)buffer;
  (void)reqlen;

  return 0;
}

// Invoked when received SET_REPORT control request or
// received data on OUT endpoint ( Report ID = 0, Type = 0 )
// received data on OUT endpoint (Report ID > 0, Type: 0 (Output), 1 (Feature))
void tud_hid_set_report_cb(uint8_t itf, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize)
{
  (void)itf; // 👈 显式忽略(void) instance;
  (void)report_id;
  (void)report_type;
  // (void) buffer;

  // 🚨 无条件打印收到的 report_id 和 report_type
  char tmp[256]; // 增加缓冲区大小以容纳更多数据
  snprintf(tmp, sizeof(tmp), "📡 RX: itf=%u, rpt_id=0x%02X, type=%u, len=%u\r\n", itf, report_id, report_type, bufsize);
  CDC_LOG(tmp);

  if (itf == REPORT_ID_CUSTOM && bufsize == 64)
  {
    // 收到 PC 发来的 64 字节数据
    char tmp[128]; // 增加缓冲区大小以容纳更多数据
    snprintf(tmp, sizeof(tmp), "Custom HID OUT: ");
    CDC_LOG(tmp);
    for (uint16_t i = 0; i < bufsize; i++)
    {
      snprintf(tmp, sizeof(tmp), "%02X ", buffer[i]); // 格式化每个字节为两位十六进制数
      CDC_LOG(tmp);
    }
    CDC_LOG("\r\n"); // 输出换行符

    process_hid_report(buffer, bufsize);
  }
}

//--------------------------------------------------------------------+
// Host HID
//--------------------------------------------------------------------+

// void tuh_mount_cb(uint8_t daddr) {
//   uint32_t t = board_millis();
//   CDC_LOG("[%lu ms] tuh_mount_cb called for device address %u\n", t, daddr);
//   blink_interval_ms = BLINK_INIT_MOUNTED;
// }

void tuh_umount_cb(uint8_t dev_addr)
{
  char tempbuf[256];
  sprintf(tempbuf, "Device %u is unmounted\r\n", dev_addr);
  CDC_LOG(tempbuf);

  // ✅ 检查是否还有任何设备挂载
  bool any_device_mounted = false;
  for (uint8_t addr = 1; addr <= CFG_TUH_DEVICE_MAX; addr++)
  {
    if (addr == dev_addr)
      continue; // 跳过刚卸载的设备
    if (tuh_mounted(addr))
    {
      any_device_mounted = true;
      break;
    }
  }

  if (!any_device_mounted)
  {
    CDC_LOG(">>> ALL USB DEVICES DISCONNECTED <<<\r\n");
    // 可以在这里做：关闭 LED、进入低功耗、重置状态机等
    blink_interval_ms = BLINK_NOT_MOUNTED;
  }
  else
  {
    CDC_LOG(">>> Other USB devices still connected <<<\r\n");
  }
}
// Invoked when device with hid interface is mounted
// Report descriptor is also available for use. tuh_hid_parse_report_descriptor()
// can be used to parse common/simple enough descriptor.
// Note: if report descriptor length > CFG_TUH_ENUMERATION_BUFSIZE, it will be skipped
// therefore report_desc = NULL, desc_len = 0

// 在 tuh_hid_mount_cb 函数中添加
void tuh_hid_mount_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *desc_report, uint16_t desc_len)
{
  (void)desc_report;
  (void)desc_len;

  CDC_LOG("HID Report Descriptor (Len=%u):\n", desc_len);
  for (uint16_t i = 0; i < desc_len; i++)
  {
    CDC_LOG("%02X ", desc_report[i]);
  }
  CDC_LOG("\n");

  // uint32_t t = board_millis();
  // CDC_LOG("[%lu ms] tuh_hid_mount_cb called for device address %u, instance %u\n", t, dev_addr, instance);
  blink_interval_ms = BLINK_MOUNTED;

  uint8_t proto = tuh_hid_interface_protocol(dev_addr, instance);
  CDC_LOG("[HID] Mounted: dev=%u, instance=%u, proto=%u, desc_len=%u\r\n",
          dev_addr, instance, proto, desc_len);
  // 保存设备信息到设备上下文
  hid_devices[instance].dev_addr = dev_addr;
  hid_devices[instance].idx = instance;
  hid_devices[instance].protocol = HID_PROTOCOL_REPORT; // 默认为REPORT协议

  // ========== 1. 设备类型检测 ==========
  if (proto == HID_ITF_PROTOCOL_NONE)
  {
    CDC_LOG("  Device Type: HID NONE \r\n");
  }
  else if (proto == HID_ITF_PROTOCOL_KEYBOARD)
  {
    CDC_LOG("  Device Type: HID KEYBOARD \r\n");
  }
  else if (proto == HID_ITF_PROTOCOL_MOUSE)
  {
    CDC_LOG("  Device Type: HID Mouse \r\n");
    // 尝试设置BOOT协议
    bool success = tuh_hid_set_protocol(dev_addr, instance, HID_PROTOCOL_BOOT);
    if (!success)
    {
      CDC_LOG("  Failed to set BOOT protocol, using REPORT protocol\n");
      // 设置失败，直接使用REPORT协议并开始接收报告
      hid_devices[instance].protocol = HID_PROTOCOL_REPORT;
    }
    else
    {
      // 设置请求已发出，等待回调结果
      CDC_LOG("  BOOT protocol set request sent, awaiting confirmation...\n");
    }
  }
  else
  {
    CDC_LOG("  Device Type: Unknown HID Type (proto=%u)\r\n", proto);
  }

  // ========== 2. 根据协议类型请求报告 ==========
  if (proto == HID_ITF_PROTOCOL_KEYBOARD ||
      proto == HID_ITF_PROTOCOL_MOUSE)
  {

    if (!tuh_hid_receive_report(dev_addr, instance))
    {
      CDC_LOG("  Error: cannot request report\r\n");
    }
  }
}
// 添加协议设置完成回调
void tuh_hid_set_protocol_complete_cb(uint8_t dev_addr, uint8_t instance, uint8_t protocol)
{
  (void)dev_addr;
  CDC_LOG("HID protocol set complete: dev=%u, instance=%u, proto=%s\n",
          dev_addr, instance,
          (protocol == HID_PROTOCOL_BOOT) ? "BOOT" : (protocol == HID_PROTOCOL_REPORT) ? "REPORT"
                                                                                       : "UNKNOWN/NONE");

  // 更新设备协议状态
  hid_devices[instance].protocol = protocol;
}
// Invoked when device with hid interface is un-mounted
void tuh_hid_umount_cb(uint8_t dev_addr, uint8_t instance)
{
  char tempbuf[256];
  sprintf(tempbuf, "[%u] HID Interface%u is unmounted\r\n", dev_addr, instance);
  CDC_LOG(tempbuf);
}

// look up new key in previous keys
static inline bool find_key_in_report(hid_keyboard_report_t const *report, uint8_t keycode)
{
  for (uint8_t i = 0; i < 6; i++)
  {
    if (report->keycode[i] == keycode)
      return true;
  }

  return false;
}

// Invoked when received report from device via interrupt endpoint
// 修改 tuh_hid_report_received_cb 函数

void tuh_hid_report_received_cb(uint8_t dev_addr, uint8_t instance, uint8_t const *report, uint16_t len)
{

  // static uint32_t last_report_time = 0;
  // static uint32_t report_count = 0;
  // static uint32_t last_rate_print = 0;

  // uint32_t now = time_us_32();

  // // 计算报告间隔
  // if (last_report_time != 0) {
  //   uint32_t interval_us = now - last_report_time;
  //   if (interval_us > 1000) {// 超过3ms间隔
  //     // CDC_LOG("Long interval: %luus\n", (unsigned long) interval_us);
  //   }
  // }
  // last_report_time = now;

  // report_count++;

  // // 每秒打印一次报告速率
  // if (now - last_rate_print > 1000000) {// 1秒
  //   CDC_LOG("Report rate: %lu Hz\n", (unsigned long) report_count);
  //   report_count = 0;
  //   last_rate_print = now;
  // }

  CDC_LOG("\r\n[HID Report] dev_addr=%u instance=%u len=%u data=",
          dev_addr, instance, len);

  for (uint16_t i = 0; i < len; ++i)
  {
    CDC_LOG("%02X ", report[i]);
  }
  CDC_LOG("\r\n");

  if (len == 0)
  {
    // ✅ 检查是否还有任何设备挂载
    bool any_device_mounted = false;
    for (uint8_t addr = 1; addr <= CFG_TUH_DEVICE_MAX; addr++)
    {
      if (addr == dev_addr)
        continue; // 跳过刚卸载的设备
      if (tuh_mounted(addr))
      {
        any_device_mounted = true;
        break;
      }
    }
    if (any_device_mounted)
    {
      // 等待设备卸载后清理完成
      board_delay(1000);
      tuh_hid_receive_report(dev_addr, instance);
    }
    return;
  }

  uint8_t proto = tuh_hid_interface_protocol(dev_addr, instance);
  // 获取当前设备的协议状态
  uint8_t protocol = hid_devices[instance].protocol;
  // ========== 1. 设备类型检测 ==========
  if (proto == HID_ITF_PROTOCOL_NONE)
  {
    // 游戏手柄或通用HID设备处理
    if (len >= 11)
    { // 至少需要11字节来解析完整游戏手柄报告
      gamepad_report_t gmp_report = {
          .x = (int8_t)report[0],
          .y = (int8_t)report[1],
          .z = (int8_t)report[2],
          .rz = (int8_t)report[3],
          .rx = (int8_t)report[4],
          .ry = (int8_t)report[5],
          .hat = report[6],
          .buttons = (uint32_t)report[7] | ((uint32_t)report[8] << 8) |
                     ((uint32_t)report[9] << 16) | ((uint32_t)report[10] << 24)};

      // 将报告加入队列
      if (!queue_try_add(&gamepad_report_queue, &gmp_report))
      {
        CDC_LOG("WARN: Gamepad queue full, report dropped.\n");
      }
      else
      {
        CDC_LOG("Gamepad report forwarded: X=%d Y=%d Z=%d RZ=%d RX=%d RY=%d HAT=%d BTNS=0x%08" PRIX32 "\n",
                gmp_report.x, gmp_report.y, gmp_report.z, gmp_report.rz,
                gmp_report.rx, gmp_report.ry, gmp_report.hat, gmp_report.buttons);
      }
    }
    else
    {
      CDC_LOG("WARN: Gamepad report too short (%u bytes).\n", len);
    }
  }
  else if (proto == HID_ITF_PROTOCOL_KEYBOARD)
  {
    // 基本长度检查：标准键盘报告通常为8字节
    if (len < 8)
    {
      CDC_LOG("WARN: Keyboard report too short (%u bytes).\n", len);
      tuh_hid_receive_report(dev_addr, instance);
      return;
    }

    // 3. 解析键盘报告
    keyboard_report_t kbd_report;
    kbd_report.modifier = report[0]; // 第一个字节是修饰键

    // 接下来的6个字节是普通按键码
    for (int i = 0; i < 6; i++)
    {
      kbd_report.keycode[i] = report[2 + i]; // 通常report[1]是保留位
    }

    // 4. 将报告加入队列
    if (!queue_try_add(&keyboard_report_queue, &kbd_report))
    {
      CDC_LOG("WARN: Keyboard queue full, report dropped.\n");
    }
    else
    {
      CDC_LOG("Keyboard report queued: modifier=0x%02X, keys=%02X %02X %02X %02X %02X %02X\n",
              kbd_report.modifier,
              kbd_report.keycode[0], kbd_report.keycode[1], kbd_report.keycode[2],
              kbd_report.keycode[3], kbd_report.keycode[4], kbd_report.keycode[5]);
    }
  }
  else if (proto == HID_ITF_PROTOCOL_MOUSE)
  {
    uint8_t buttons = 0;
    int8_t x = 0;
    int8_t y = 0;
    int8_t wheel = 0;
    // ===== 鼠标设备处理 =====
    if (protocol == HID_PROTOCOL_BOOT)
    {
      // 使用BOOT协议解析鼠标报告
      CDC_LOG("  Parsing mouse report (BOOT protocol)\n");

      // BOOT鼠标报告固定格式: [按钮], [X位移], [Y位移], [滚轮]
      if (len >= 4)
      {
        buttons = report[0];
        x = (int8_t)report[1];
        y = (int8_t)report[2];
        wheel = (int8_t)report[3];
      }

      // 特殊鼠标，手动解析
      if (len == 7)
      {
        // 游戏鼠标/自定义设备 (7+字节)
        buttons = report[0];
        x = (int8_t)(report[1] | (report[2] << 8));
        y = (int8_t)(report[3] | (report[4] << 8));
        wheel = (int8_t)report[5];
      }
    }
    else
    {
      // 使用REPORT协议解析（原有解析方式）
      CDC_LOG("  Parsing mouse report (REPORT protocol)\n");
      if (len == 7)
      {
        // 游戏鼠标/自定义设备 (7+字节)
        buttons = report[0];
        x = (int8_t)(report[1] | (report[2] << 8));
        y = (int8_t)(report[3] | (report[4] << 8));
        wheel = (int8_t)report[5];
      }
    }

    // if (extract_mouse_data(report, len, &g_mouse_info, &buttons, &x, &y, &wheel)) {
    // } else {
    // }
    // // ========== 2. 动态解析（根据报告长度） ==========
    // uint8_t buttons = report[0];
    // int8_t x = 0, y = 0, wheel = 0;

    // if (len == 6) {
    //   // 标准 Boot Mouse (6字节)
    //   x = (int8_t) report[1];
    //   y = (int8_t) report[2];
    //   wheel = (int8_t) report[3];
    // } else if (len >= 7) {
    //   // 游戏鼠标/自定义设备 (7+字节)
    //   x = (int8_t) (report[1] | (report[2] << 8));
    //   y = (int8_t) (report[3] | (report[4] << 8));
    //   wheel = (int8_t) report[5];
    // } else {
    //   CDC_LOG("ERROR: Unsupported report length %u\r\n", len);
    //   tuh_hid_receive_report(dev_addr, instance);
    //   return;
    // }

    // ========== 3. 安全打印（避免溢出） ==========
    // CDC_LOG("解析报告: buttons=0x%02X, x=%d, y=%d, wheel=%d\r\n",
    //         buttons, x, y, wheel);

    // ========== 4. 将报告加入队列 ==========
    mouse_report_t rpt = {
        .buttons = buttons,
        .x = x,
        .y = y,
        .wheel = wheel};

    // 尝试加入队列（如果队列满则丢弃）

    // 4. 将报告加入队列
    if (!queue_try_add(&mouse_report_queue, &rpt))
    {
      CDC_LOG("WARN: Keyboard queue full, report dropped.\n");
    }
    else
    {
      CDC_LOG("Mouse report queued: buttons=%d x=%d y=%d wheel=%d\n", rpt.buttons, rpt.x, rpt.y, rpt.wheel);
    }
  }
  else
  {
    CDC_LOG("  Device: Unknown HID Type (proto=%u)\r\n", proto);
  }

  // 继续请求报告
  if (!tuh_hid_receive_report(dev_addr, instance))
  {
    CDC_LOG("Error: cannot request report\r\n");
  }
}

//--------------------------------------------------------------------+
// Blinking Task
//--------------------------------------------------------------------+
void led_blinking_task(void)
{
  static uint32_t start_ms = 0;
  static bool led_state = false;

  uint32_t interval;
  interval = blink_interval_ms;

  // Blink every interval ms
  if (board_millis() - start_ms < interval)
    return; // not enough time
  start_ms += interval;

  board_led_write(led_state);
  led_state = 1 - led_state; // toggle
}