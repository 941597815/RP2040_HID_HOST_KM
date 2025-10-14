// HIDDevice.cpp
#include <cstring>
#include <thread>
#include <chrono>
#include <vector>
#include <unordered_set>
#include <hidapi.h>
#include <Windows.h>

#pragma comment(lib, "winmm.lib")
struct TimeHelper { TimeHelper() { timeBeginPeriod(1); } ~TimeHelper() { timeEndPeriod(1); } };
static TimeHelper _th;

#ifdef _WIN32
#define EXPORT extern "C" __declspec(dllexport)
#else
#define EXPORT extern "C"
#endif

static hid_device* g_dev = nullptr;
static uint64_t g_last_time = 0;
static uint8_t g_mouse_buttons = 0;
static std::unordered_set<uint8_t> g_keyboard_keys;

// 游戏手柄状态
static uint32_t g_gamepad_buttons = 0;
static uint8_t g_gamepad_hat = 0;
static int8_t g_gamepad_x = 0, g_gamepad_y = 0, g_gamepad_z = 0, g_gamepad_rz = 0;
static int8_t g_gamepad_rx = 0, g_gamepad_ry = 0;

/* ---------- 辅助函数 ---------- */
static uint64_t get_current_time_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

static void precise_delay(double ms) {
    using clock = std::chrono::steady_clock;
    const auto target = clock::now() +
        std::chrono::microseconds(static_cast<int64_t>(ms * 1000));

    // 1. 先让出 CPU，直到离目标 < 200 µs
    while (true) {
        auto left = std::chrono::duration<double, std::micro>(target - clock::now()).count();
        if (left <= 200.0) break;
        if (left > 1000.0)
            std::this_thread::sleep_for(std::chrono::microseconds(static_cast<int64_t>(left * 0.75)));
        else
            std::this_thread::yield();
    }

    // 2. 忙等直到精确时刻
    while (clock::now() < target) { /* spin */ }
}

static int8_t to_signed_byte(int value) {
    value = (value < -127) ? -127 : (value > 127) ? 127 : value;
    return static_cast<int8_t>(value);
}

static bool _send_report(const uint8_t* data, size_t data_length, uint8_t cmd_id) {
    if (!g_dev || data_length > 62) return false;

    // 计算需要等待的剩余时间（毫秒）
    uint64_t now = get_current_time_us();
    uint64_t elapsed = now - g_last_time;

    if (elapsed < 1000) {  // 如果距离上次发送不足1000µs
        double remaining_ms = (1000 - elapsed) / 1000.0;
        precise_delay(remaining_ms);  // 精确等待剩余时间
    }

    uint8_t report[64] = { 0 };
    report[0] = 0x00; // Report ID
    report[1] = cmd_id; // Command ID

    // 复制数据
    memcpy(report + 2, data, data_length);

    int result = hid_write(g_dev, report, 64);
    g_last_time = get_current_time_us();

    return result != -1;
}

/* ---------- 通用函数 ---------- */
EXPORT bool HID_Open(unsigned short vid, unsigned short pid) {
    if (hid_init() != 0) return false;

    // 查找特定设备（Usage Page 0xFF00）
    hid_device_info* devices = hid_enumerate(vid, pid);
    hid_device_info* current = devices;
    hid_device* found_dev = nullptr;

    while (current) {
        if (current->usage_page == 0xFF00) {
            found_dev = hid_open_path(current->path);
            break;
        }
        current = current->next;
    }

    hid_free_enumeration(devices);
    g_dev = found_dev;

    // 初始化状态
    g_mouse_buttons = 0;
    g_keyboard_keys.clear();
    g_gamepad_buttons = 0;
    g_gamepad_hat = 0;
    g_gamepad_x = g_gamepad_y = g_gamepad_z = g_gamepad_rz = g_gamepad_rx = g_gamepad_ry = 0;
    g_last_time = get_current_time_us();

    return g_dev != nullptr;
}


/* ---------- 鼠标功能 ---------- */
// 鼠标按钮定义
#define MOUSE_LEFT    0x01
#define MOUSE_RIGHT   0x02
#define MOUSE_MIDDLE  0x04
#define MOUSE_BACK    0x08
#define MOUSE_FORWARD 0x10

EXPORT bool Mouse_Press(uint8_t button) {
    g_mouse_buttons |= button;
    uint8_t report[] = { g_mouse_buttons, 0, 0, 0, 0 };
    return _send_report(report, 5, 0x02);
}

EXPORT bool Mouse_Release(uint8_t button) {
    g_mouse_buttons &= ~button;
    uint8_t report[] = { g_mouse_buttons, 0, 0, 0, 0 };
    return _send_report(report, 5, 0x02);
}

EXPORT bool Mouse_Click(uint8_t button) {
    if (!Mouse_Press(button)) return false;
    precise_delay(50);
    return Mouse_Release(button);
}

EXPORT bool Mouse_Move(int x, int y) {
    if (x == 0 && y == 0) return true;

    while (abs(x) > 127 || abs(y) > 127) {
        int step_x = (x > 0) ? 127 : (x < 0) ? -127 : 0;
        int step_y = (y > 0) ? 127 : (y < 0) ? -127 : 0;

        if (abs(x) <= 127) step_x = x;
        if (abs(y) <= 127) step_y = y;

        uint8_t report[] = {
            g_mouse_buttons,
            static_cast<uint8_t>(step_x & 0xFF),
            static_cast<uint8_t>(step_y & 0xFF),
            0, 0
        };

        if (!_send_report(report, 5, 0x02)) return false;

        x -= step_x;
        y -= step_y;
        precise_delay(1);
    }

    if (x != 0 || y != 0) {
        uint8_t report[] = {
            g_mouse_buttons,
            static_cast<uint8_t>(x & 0xFF),
            static_cast<uint8_t>(y & 0xFF),
            0, 0
        };
        return _send_report(report, 5, 0x02);
    }

    return true;
}

EXPORT bool Mouse_Wheel(int scroll, int pan) {
    uint8_t report[] = { g_mouse_buttons, 0, 0, static_cast<uint8_t>(scroll & 0xFF), static_cast<uint8_t>(pan & 0xFF) };
    return _send_report(report, 5, 0x02);
}

EXPORT bool Mouse_Drag(int x, int y, uint8_t button, int steps, int delay_ms) {
    if (!Mouse_Press(button)) return false;
    precise_delay(100);

    int dx = x / steps;
    int dy = y / steps;

    for (int i = 0; i < steps; ++i) {
        if (!Mouse_Move(dx, dy)) {
            Mouse_Release(button);
            return false;
        }
        precise_delay(delay_ms);
    }

    int remainder_x = x % steps;
    int remainder_y = y % steps;
    if (remainder_x != 0 || remainder_y != 0) {
        if (!Mouse_Move(remainder_x, remainder_y)) {
            Mouse_Release(button);
            return false;
        }
    }

    return Mouse_Release(button);
}

EXPORT bool Mouse_ReleaseAll() {
    g_mouse_buttons = 0;
    uint8_t report[] = { 0, 0, 0, 0, 0 };
    return _send_report(report, 5, 0x02);
}

/* ---------- 键盘功能 ---------- */
// 键盘键值定义（与Python代码保持一致）
#define KEY_A 0x04
#define KEY_B 0x05
#define KEY_C 0x06
#define KEY_D 0x07
#define KEY_E 0x08
#define KEY_F 0x09
#define KEY_G 0x0A
#define KEY_H 0x0B
#define KEY_I 0x0C
#define KEY_J 0x0D
#define KEY_K 0x0E
#define KEY_L 0x0F
#define KEY_M 0x10
#define KEY_N 0x11
#define KEY_O 0x12
#define KEY_P 0x13
#define KEY_Q 0x14
#define KEY_R 0x15
#define KEY_S 0x16
#define KEY_T 0x17
#define KEY_U 0x18
#define KEY_V 0x19
#define KEY_W 0x1A
#define KEY_X 0x1B
#define KEY_Y 0x1C
#define KEY_Z 0x1D

#define KEY_1 0x1E
#define KEY_2 0x1F
#define KEY_3 0x20
#define KEY_4 0x21
#define KEY_5 0x22
#define KEY_6 0x23
#define KEY_7 0x24
#define KEY_8 0x25
#define KEY_9 0x26
#define KEY_0 0x27

#define KEY_ENTER 0x28
#define KEY_ESC 0x29
#define KEY_BACKSPACE 0x2A
#define KEY_TAB 0x2B
#define KEY_SPACE 0x2C
#define KEY_MINUS 0x2D
#define KEY_EQUAL 0x2E
#define KEY_LEFTBRACE 0x2F
#define KEY_RIGHTBRACE 0x30
#define KEY_BACKSLASH 0x31
#define KEY_SEMICOLON 0x33
#define KEY_QUOTE 0x34
#define KEY_TILDE 0x35
#define KEY_COMMA 0x36
#define KEY_PERIOD 0x37
#define KEY_SLASH 0x38

#define KEY_CAPSLOCK 0x39
#define KEY_F1 0x3A
#define KEY_F2 0x3B
#define KEY_F3 0x3C
#define KEY_F4 0x3D
#define KEY_F5 0x3E
#define KEY_F6 0x3F
#define KEY_F7 0x40
#define KEY_F8 0x41
#define KEY_F9 0x42
#define KEY_F10 0x43
#define KEY_F11 0x44
#define KEY_F12 0x45

#define KEY_PRINTSCREEN 0x46
#define KEY_SCROLLLOCK 0x47
#define KEY_PAUSE 0x48
#define KEY_INSERT 0x49
#define KEY_HOME 0x4A
#define KEY_PAGEUP 0x4B
#define KEY_DELETE 0x4C
#define KEY_END 0x4D
#define KEY_PAGEDOWN 0x4E
#define KEY_RIGHT 0x4F
#define KEY_LEFT 0x50
#define KEY_DOWN 0x51
#define KEY_UP 0x52

#define KEY_KP0 0x62
#define KEY_KP1 0x59
#define KEY_KP2 0x5A
#define KEY_KP3 0x5B
#define KEY_KP4 0x5C
#define KEY_KP5 0x5D
#define KEY_KP6 0x5E
#define KEY_KP7 0x5F
#define KEY_KP8 0x60
#define KEY_KP9 0x61
#define KEY_KPENTER 0x58
#define KEY_KPDECIMAL 0x63

#define KEY_LCTRL 0xE0
#define KEY_LSHIFT 0xE1
#define KEY_LALT 0xE2
#define KEY_LGUI 0xE3
#define KEY_RCTRL 0xE4
#define KEY_RSHIFT 0xE5
#define KEY_RALT 0xE6
#define KEY_RGUI 0xE7

#define MOD_LCTRL 0x01
#define MOD_LSHIFT 0x02
#define MOD_LALT 0x04
#define MOD_LGUI 0x08
#define MOD_RCTRL 0x10
#define MOD_RSHIFT 0x20
#define MOD_RALT 0x40
#define MOD_RGUI 0x80

static void _update_keyboard_report() {
    uint8_t modifier = 0;
    uint8_t keys[6] = { 0 };
    int key_index = 0;

    // 处理修饰键和普通键
    for (uint8_t key : g_keyboard_keys) {
        if (key >= 0xE0 && key <= 0xE7) { // 修饰键
            switch (key) {
            case KEY_LCTRL: modifier |= MOD_LCTRL; break;
            case KEY_LSHIFT: modifier |= MOD_LSHIFT; break;
            case KEY_LALT: modifier |= MOD_LALT; break;
            case KEY_LGUI: modifier |= MOD_LGUI; break;
            case KEY_RCTRL: modifier |= MOD_RCTRL; break;
            case KEY_RSHIFT: modifier |= MOD_RSHIFT; break;
            case KEY_RALT: modifier |= MOD_RALT; break;
            case KEY_RGUI: modifier |= MOD_RGUI; break;
            }
        }
        else if (key_index < 6) { // 普通键
            keys[key_index++] = key;
        }
    }

    uint8_t report[8] = { modifier, 0x00 };
    memcpy(report + 2, keys, 6);
    _send_report(report, 8, 0x01);
}

EXPORT bool Key_Press(uint8_t key) {
    g_keyboard_keys.insert(key);
    _update_keyboard_report();
    return true;
}

EXPORT bool Key_Release(uint8_t key) {
    g_keyboard_keys.erase(key);
    _update_keyboard_report();
    return true;
}

EXPORT bool Key_Click(uint8_t key) {
    if (!Key_Press(key)) return false;
    precise_delay(50);
    return Key_Release(key);
}

EXPORT bool Key_Hotkey(const uint8_t* keys, int count, int delay_ms) {
    for (int i = 0; i < count; ++i) {
        if (!Key_Press(keys[i])) return false;
        precise_delay(delay_ms);
    }

    precise_delay(100);

    for (int i = count - 1; i >= 0; --i) {
        if (!Key_Release(keys[i])) return false;
        precise_delay(delay_ms);
    }

    return true;
}

EXPORT bool Key_ReleaseAll() {
    g_keyboard_keys.clear();
    uint8_t report[8] = { 0 };
    return _send_report(report, 8, 0x01);
}

/* ---------- 游戏手柄功能 ---------- */
// 游戏手柄按钮定义
#define GAMEPAD_BUTTON_0  (1 << 0)
#define GAMEPAD_BUTTON_1  (1 << 1)
#define GAMEPAD_BUTTON_2  (1 << 2)
#define GAMEPAD_BUTTON_3  (1 << 3)
#define GAMEPAD_BUTTON_4  (1 << 4)
#define GAMEPAD_BUTTON_5  (1 << 5)
#define GAMEPAD_BUTTON_6  (1 << 6)
#define GAMEPAD_BUTTON_7  (1 << 7)
#define GAMEPAD_BUTTON_8  (1 << 8)
#define GAMEPAD_BUTTON_9  (1 << 9)
#define GAMEPAD_BUTTON_10 (1 << 10)
#define GAMEPAD_BUTTON_11 (1 << 11)
#define GAMEPAD_BUTTON_12 (1 << 12)
#define GAMEPAD_BUTTON_13 (1 << 13)
#define GAMEPAD_BUTTON_14 (1 << 14)
#define GAMEPAD_BUTTON_15 (1 << 15)

// 方向键状态
#define HAT_CENTERED 0
#define HAT_UP 1
#define HAT_UP_RIGHT 2
#define HAT_RIGHT 3
#define HAT_DOWN_RIGHT 4
#define HAT_DOWN 5
#define HAT_DOWN_LEFT 6
#define HAT_LEFT 7
#define HAT_UP_LEFT 8

static bool _send_gamepad_report() {
    uint8_t report[11] = {
        static_cast<uint8_t>(g_gamepad_x),   // 左摇杆X
        static_cast<uint8_t>(g_gamepad_y),   // 左摇杆Y
        static_cast<uint8_t>(g_gamepad_z),   // 右摇杆X
        static_cast<uint8_t>(g_gamepad_rz),  // 右摇杆Y
        static_cast<uint8_t>(g_gamepad_rx),  // 左触发器
        static_cast<uint8_t>(g_gamepad_ry),  // 右触发器
        g_gamepad_hat,                       // 方向键
        static_cast<uint8_t>(g_gamepad_buttons >> 0),   // 按钮字节0
        static_cast<uint8_t>(g_gamepad_buttons >> 8),   // 按钮字节1
        static_cast<uint8_t>(g_gamepad_buttons >> 16),  // 按钮字节2
        static_cast<uint8_t>(g_gamepad_buttons >> 24)   // 按钮字节3
    };
    return _send_report(report, 11, 0x00);
}

EXPORT bool Gamepad_ButtonPress(uint32_t button) {
    g_gamepad_buttons |= button;
    return _send_gamepad_report();
}

EXPORT bool Gamepad_ButtonRelease(uint32_t button) {
    g_gamepad_buttons &= ~button;
    return _send_gamepad_report();
}

EXPORT bool Gamepad_ButtonClick(uint32_t button, int duration_ms) {
    if (!Gamepad_ButtonPress(button)) return false;
    precise_delay(duration_ms);
    return Gamepad_ButtonRelease(button);
}

EXPORT bool Gamepad_SetLeftStick(int x, int y) {
    g_gamepad_x = to_signed_byte(x);
    g_gamepad_y = to_signed_byte(y);
    return _send_gamepad_report();
}

EXPORT bool Gamepad_SetRightStick(int x, int y) {
    g_gamepad_z = to_signed_byte(x);
    g_gamepad_rz = to_signed_byte(y);
    return _send_gamepad_report();
}

EXPORT bool Gamepad_SetTriggers(int left, int right) {
    g_gamepad_rx = to_signed_byte(left);
    g_gamepad_ry = to_signed_byte(right);
    return _send_gamepad_report();
}

EXPORT bool Gamepad_SetHat(uint8_t direction) {
    if (direction > 8) return false;
    g_gamepad_hat = direction;
    return _send_gamepad_report();
}

EXPORT bool Gamepad_ReleaseAll() {
    g_gamepad_buttons = 0;
    g_gamepad_hat = HAT_CENTERED;
    g_gamepad_x = g_gamepad_y = g_gamepad_z = g_gamepad_rz = g_gamepad_rx = g_gamepad_ry = 0;
    return _send_gamepad_report();
}

EXPORT bool Mouse_To_Gamepad(const uint8_t* data, uint8_t cmd_id) {
    return _send_report(data,sizeof(data),cmd_id);
}

/* ---------- 释放所有设备 ---------- */
EXPORT bool HID_ReleaseAll() {
    bool result = true;
    result &= Mouse_ReleaseAll();
    result &= Key_ReleaseAll();
    result &= Gamepad_ReleaseAll();
    return result;
}

/* ---------- 数据读取功能 ---------- */
EXPORT int HID_Read(uint8_t* buffer, int buffer_size) {
    if (!g_dev || buffer_size < 64) return -1;
    return hid_read(g_dev, buffer, buffer_size);
}

EXPORT bool HID_ParseMouseReport(const uint8_t* data, int data_size,
    int* report_id, int* buttons,
    int* x, int* y, int* wheel) {
    if (data_size < 4) return false;

    // 检查是否有Report ID
    bool has_report_id = (data[0] != 0x00); // 根据实际情况调整

    if (has_report_id && data_size >= 5) {
        *report_id = data[0];
        *buttons = data[1];
        *x = (data[2] > 127) ? (int)data[2] - 256 : data[2];
        *y = (data[3] > 127) ? (int)data[3] - 256 : data[3];
        *wheel = (data_size > 4) ? ((data[4] > 127) ? (int)data[4] - 256 : data[4]) : 0;
    }
    else {
        *report_id = 0;
        *buttons = data[0];
        *x = (data[1] > 127) ? (int)data[1] - 256 : data[1];
        *y = (data[2] > 127) ? (int)data[2] - 256 : data[2];
        *wheel = (data_size > 3) ? ((data[3] > 127) ? (int)data[3] - 256 : data[3]) : 0;
    }

    return true;
}

EXPORT void HID_Close() {
    if (g_dev) {
        HID_ReleaseAll();
        hid_close(g_dev);
        g_dev = nullptr;
    }
    hid_exit();
}
