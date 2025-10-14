import hid
import time


VID_PID = (0x046D, 0xC08B)  # 你的设备 VID/PID


def precise_sleep(duration, precision: float = 0.0001, get_now=time.perf_counter):
    end = get_now() + duration
    while True:
        remaining = end - get_now()
        if remaining <= 0:
            break
        if remaining > precision:
            time.sleep(remaining - precision)


class HIDDevice:
    class Mouse:
        LEFT = 1  # bit0
        RIGHT = 2  # bit1
        MIDDLE = 4  # bit2
        BACK = 8  # bit3 (0x08)
        FORWARD = 16  # bit4 (0x10)

        def __init__(self, parent):
            self.parent = parent

        def _send_report(self, buttons, x, y, wheel, pan=0):
            """发送标准 HID 鼠标报告"""
            report = bytearray(
                [
                    buttons & 0xFF,  # Buttons bitmask
                    x & 0xFF,
                    y & 0xFF,
                    wheel & 0xFF,  # ← -3 变成 253
                    pan & 0xFF,
                ]
            )
            self.parent._send(report, 0x02)

        def press(self, button=LEFT):
            self.parent.g_mouse_buttons |= button
            self._send_report(self.parent.g_mouse_buttons, 0, 0, 0, 0)

        def release(self, button=LEFT):
            self.parent.g_mouse_buttons &= ~button
            self._send_report(self.parent.g_mouse_buttons, 0, 0, 0, 0)

        def click(self, button=LEFT):
            self.press(button)
            time.sleep(0.05)
            self.release(button)

        def move(self, x, y):
            while abs(x) > 127 or abs(y) > 127:
                step_x = 127 if x > 0 else -127 if x < 0 else 0
                step_y = 127 if y > 0 else -127 if y < 0 else 0
                if abs(x) <= 127:
                    step_x = x
                if abs(y) <= 127:
                    step_y = y
                self._send_report(self.parent.g_mouse_buttons, step_x, step_y, 0, 0)
                x -= step_x
                y -= step_y
                time.sleep(0.001)  # TinyUSB 需要间隔

            if x != 0 or y != 0:
                self._send_report(self.parent.g_mouse_buttons, x, y, 0, 0)

        def wheel(self, scroll, pan=0):
            self._send_report(self.parent.g_mouse_buttons, 0, 0, scroll, pan)

        def drag(self, x, y, button=LEFT, steps=10, delay=0.01):
            self.press(button)
            time.sleep(0.1)
            dx = x // steps
            dy = y // steps
            for _ in range(steps):
                self.move(dx, dy)
                time.sleep(delay)
            remainder_x = x % steps
            remainder_y = y % steps
            if remainder_x or remainder_y:
                self.move(remainder_x, remainder_y)
            self.release(button)

        def release_all(self):
            self._send_report(0, 0, 0, 0, 0)

    class Keyboard:
        # HID Usage Page 0x07 键盘键值 (USB HID Usage Tables 1.4)
        A = 0x04
        B = 0x05
        C = 0x06
        D = 0x07
        E = 0x08
        F = 0x09
        G = 0x0A
        H = 0x0B
        I = 0x0C
        J = 0x0D
        K = 0x0E
        L = 0x0F
        M = 0x10
        N = 0x11
        O = 0x12
        P = 0x13
        Q = 0x14
        R = 0x15
        S = 0x16
        T = 0x17
        U = 0x18
        V = 0x19
        W = 0x1A
        X = 0x1B
        Y = 0x1C
        Z = 0x1D

        KEY_1 = 0x1E
        KEY_2 = 0x1F
        KEY_3 = 0x20
        KEY_4 = 0x21
        KEY_5 = 0x22
        KEY_6 = 0x23
        KEY_7 = 0x24
        KEY_8 = 0x25
        KEY_9 = 0x26
        KEY_0 = 0x27

        ENTER = 0x28
        ESC = 0x29
        BACKSPACE = 0x2A
        TAB = 0x2B
        SPACE = 0x2C
        MINUS = 0x2D
        EQUAL = 0x2E
        LEFTBRACE = 0x2F
        RIGHTBRACE = 0x30
        BACKSLASH = 0x31
        SEMICOLON = 0x33
        QUOTE = 0x34
        TILDE = 0x35
        COMMA = 0x36
        PERIOD = 0x37
        SLASH = 0x38

        CAPSLOCK = 0x39
        F1 = 0x3A
        F2 = 0x3B
        F3 = 0x3C
        F4 = 0x3D
        F5 = 0x3E
        F6 = 0x3F
        F7 = 0x40
        F8 = 0x41
        F9 = 0x42
        F10 = 0x43
        F11 = 0x44
        F12 = 0x45

        PRINTSCREEN = 0x46
        SCROLLLOCK = 0x47
        PAUSE = 0x48
        INSERT = 0x49
        HOME = 0x4A
        PAGEUP = 0x4B
        DELETE = 0x4C
        END = 0x4D
        PAGEDOWN = 0x4E
        RIGHT = 0x4F
        LEFT = 0x50
        DOWN = 0x51
        UP = 0x52

        KP0 = 0x62
        KP1 = 0x59
        KP2 = 0x5A
        KP3 = 0x5B
        KP4 = 0x5C
        KP5 = 0x5D
        KP6 = 0x5E
        KP7 = 0x5F
        KP8 = 0x60
        KP9 = 0x61
        KPENTER = 0x58
        KPDECIMAL = 0x63

        LCTRL = 0xE0
        LSHIFT = 0xE1
        LALT = 0xE2
        LGUI = 0xE3
        RCTRL = 0xE4
        RSHIFT = 0xE5
        RALT = 0xE6
        RGUI = 0xE7

        MOD_LCTRL = 0x01
        MOD_LSHIFT = 0x02
        MOD_LALT = 0x04
        MOD_LGUI = 0x08
        MOD_RCTRL = 0x10
        MOD_RSHIFT = 0x20
        MOD_RALT = 0x40
        MOD_RGUI = 0x80

        def __init__(self, parent):
            self.parent = parent

        def _send_report(self, modifier, *keys):
            """发送标准 HID 键盘报告"""
            keycodes = list(keys[:6]) + [0] * (6 - len(keys))
            report = bytearray(
                [
                    modifier,  # Modifier keys
                    0x00,  # Reserved
                ]
                + keycodes[:6]
            )
            self.parent._send(report, 0x01)

        def press(self, key):
            if isinstance(key, str):
                key = getattr(self, key.upper(), 0)
            self.parent.g_keyboard_keys.add(key)
            self._update_keyboard_report()

        def release(self, key):
            if isinstance(key, str):
                key = getattr(self, key.upper(), 0)
            self.parent.g_keyboard_keys.discard(key)
            self._update_keyboard_report()

        def _update_keyboard_report(self):
            keys = list(self.parent.g_keyboard_keys)[:6]
            mod = 0
            # 提取修饰键并从 keys 中移除
            mod_keys = {
                self.LCTRL,
                self.LSHIFT,
                self.LALT,
                self.LGUI,
                self.RCTRL,
                self.RSHIFT,
                self.RALT,
                self.RGUI,
            }
            actual_keys = []
            for k in keys:
                if k in mod_keys:
                    if k == self.LCTRL:
                        mod |= self.MOD_LCTRL
                    elif k == self.LSHIFT:
                        mod |= self.MOD_LSHIFT
                    elif k == self.LALT:
                        mod |= self.MOD_LALT
                    elif k == self.LGUI:
                        mod |= self.MOD_LGUI
                    elif k == self.RCTRL:
                        mod |= self.MOD_RCTRL
                    elif k == self.RSHIFT:
                        mod |= self.MOD_RSHIFT
                    elif k == self.RALT:
                        mod |= self.MOD_RALT
                    elif k == self.RGUI:
                        mod |= self.MOD_RGUI
                else:
                    actual_keys.append(k)
            # 补齐到6个键
            actual_keys = actual_keys[:6] + [0] * (6 - len(actual_keys))
            self._send_report(mod, *actual_keys)

        def click(self, key):
            self.press(key)
            time.sleep(0.05)
            self.release(key)

        def hotkey(self, *keys, delay=0.05):
            for k in keys:
                self.press(k)
                time.sleep(delay)
            time.sleep(0.1)
            for k in reversed(keys):
                self.release(k)
                time.sleep(delay)

        def send_combo(self, mods, *keys):
            """直接发送组合键报告（不维护状态）"""
            keycodes = [
                getattr(self, k.upper()) if isinstance(k, str) else k for k in keys[:6]
            ]
            self._send_report(mods, *keycodes)

        def release_all(self):
            self._send_report(0, *([0] * 6))

    class Gamepad:
        # 游戏手柄按钮位掩码定义（与hid_gamepad_button_bm_t完全对应）
        BUTTON_0 = 1 << 0  # GAMEPAD_BUTTON_A / GAMEPAD_BUTTON_SOUTH
        BUTTON_1 = 1 << 1  # GAMEPAD_BUTTON_B / GAMEPAD_BUTTON_EAST
        BUTTON_2 = 1 << 2  # GAMEPAD_BUTTON_C
        BUTTON_3 = 1 << 3  # GAMEPAD_BUTTON_X / GAMEPAD_BUTTON_NORTH
        BUTTON_4 = 1 << 4  # GAMEPAD_BUTTON_Y / GAMEPAD_BUTTON_WEST
        BUTTON_5 = 1 << 5  # GAMEPAD_BUTTON_Z
        BUTTON_6 = 1 << 6  # GAMEPAD_BUTTON_TL (左肩键)
        BUTTON_7 = 1 << 7  # GAMEPAD_BUTTON_TR (右肩键)
        BUTTON_8 = 1 << 8  # GAMEPAD_BUTTON_TL2 (左扳机)
        BUTTON_9 = 1 << 9  # GAMEPAD_BUTTON_TR2 (右扳机)
        BUTTON_10 = 1 << 10  # GAMEPAD_BUTTON_SELECT
        BUTTON_11 = 1 << 11  # GAMEPAD_BUTTON_START
        BUTTON_12 = 1 << 12  # GAMEPAD_BUTTON_MODE
        BUTTON_13 = 1 << 13  # GAMEPAD_BUTTON_THUMBL (左摇杆按下)
        BUTTON_14 = 1 << 14  # GAMEPAD_BUTTON_THUMBR (右摇杆按下)
        BUTTON_15 = 1 << 15  # 保留按钮
        BUTTON_16 = 1 << 16  # 保留按钮
        BUTTON_17 = 1 << 17  # 保留按钮
        BUTTON_18 = 1 << 18  # 保留按钮
        BUTTON_19 = 1 << 19  # 保留按钮
        BUTTON_20 = 1 << 20  # 保留按钮
        BUTTON_21 = 1 << 21  # 保留按钮
        BUTTON_22 = 1 << 22  # 保留按钮
        BUTTON_23 = 1 << 23  # 保留按钮
        BUTTON_24 = 1 << 24  # 保留按钮
        BUTTON_25 = 1 << 25  # 保留按钮
        BUTTON_26 = 1 << 26  # 保留按钮
        BUTTON_27 = 1 << 27  # 保留按钮
        BUTTON_28 = 1 << 28  # 保留按钮
        BUTTON_29 = 1 << 29  # 保留按钮
        BUTTON_30 = 1 << 30  # 保留按钮
        BUTTON_31 = 1 << 31  # 保留按钮

        # 常用按钮别名（与Linux输入事件代码对应）
        BUTTON_A = BUTTON_0  # 南按钮
        BUTTON_B = BUTTON_1  # 东按钮
        BUTTON_X = BUTTON_3  # 北按钮
        BUTTON_Y = BUTTON_4  # 西按钮
        BUTTON_SOUTH = BUTTON_0  # 南按钮
        BUTTON_EAST = BUTTON_1  # 东按钮
        BUTTON_NORTH = BUTTON_3  # 北按钮
        BUTTON_WEST = BUTTON_4  # 西按钮
        BUTTON_TL = BUTTON_6  # 左肩键
        BUTTON_TR = BUTTON_7  # 右肩键
        BUTTON_TL2 = BUTTON_8  # 左扳机
        BUTTON_TR2 = BUTTON_9  # 右扳机
        BUTTON_SELECT = BUTTON_10  # 选择键
        BUTTON_START = BUTTON_11  # 开始键
        BUTTON_MODE = BUTTON_12  # 模式键
        BUTTON_THUMBL = BUTTON_13  # 左摇杆按下
        BUTTON_THUMBR = BUTTON_14  # 右摇杆按下

        # 方向键状态（与hid_gamepad_hat_t完全对应）
        HAT_CENTERED = 0
        HAT_UP = 1
        HAT_UP_RIGHT = 2
        HAT_RIGHT = 3
        HAT_DOWN_RIGHT = 4
        HAT_DOWN = 5
        HAT_DOWN_LEFT = 6
        HAT_LEFT = 7
        HAT_UP_LEFT = 8

        def __init__(self, parent):
            self.parent = parent
            self.buttons_state = 0  # 32位按钮状态位掩码
            self.hat_state = self.HAT_CENTERED
            # 模拟轴初始值（与hid_gamepad_report_t结构体完全对应）
            self.x = 0  # 左摇杆X轴 (-127 到 127)
            self.y = 0  # 左摇杆Y轴 (-127 到 127)
            self.z = 0  # 右摇杆X轴 (-127 到 127)
            self.rz = 0  # 右摇杆Y轴 (-127 到 127)
            self.rx = 0  # 左触发器 (-127 到 127)
            self.ry = 0  # 右触发器 (-127 到 127)

        def _send_report(self):
            """发送符合TinyUSB hid_gamepad_report_t结构的HID报告"""
            # 构建报告数据：严格按照C结构体字段顺序和大小
            report = bytearray(
                [
                    # 6个模拟轴（各1字节有符号）
                    self._to_signed_byte(self.x),  # 字节0: 左摇杆X
                    self._to_signed_byte(self.y),  # 字节1: 左摇杆Y
                    self._to_signed_byte(self.z),  # 字节2: 右摇杆X
                    self._to_signed_byte(self.rz),  # 字节3: 右摇杆Y
                    self._to_signed_byte(self.rx),  # 字节4: 左触发器
                    self._to_signed_byte(self.ry),  # 字节5: 右触发器
                    # 方向键（1字节无符号）
                    self.hat_state,  # 字节6: 方向键
                    # 按钮状态（4字节小端序）
                    (self.buttons_state >> 0) & 0xFF,  # 字节7: 按钮位0-7
                    (self.buttons_state >> 8) & 0xFF,  # 字节8: 按钮位8-15
                    (self.buttons_state >> 16) & 0xFF,  # 字节9: 按钮位16-23
                    (self.buttons_state >> 24) & 0xFF,  # 字节10: 按钮位24-31
                ]
            )

            # 发送报告（报告ID为0x00表示游戏手柄报告）
            self.parent._send(report, 0x00)

        def _to_signed_byte(self, value):
            """将有符号整数转换为HID标准的二进制补码形式"""
            value = max(-127, min(127, value))
            if value < 0:
                return (256 + value) & 0xFF
            return value & 0xFF

        def button_press(self, button):
            """按下指定按钮（使用位操作，与C枚举对应）"""
            self.buttons_state |= button
            self._send_report()

        def button_release(self, button):
            """释放指定按钮"""
            self.buttons_state &= ~button
            self._send_report()

        def button_click(self, button, duration=0.1):
            """点击按钮（按下后释放）"""
            self.button_press(button)
            time.sleep(duration)
            self.button_release(button)

        def set_left_stick(self, x, y):
            """设置左摇杆位置（对应结构体的x和y字段）"""
            self.x = max(-127, min(127, x))
            self.y = max(-127, min(127, y))
            self._send_report()

        def set_right_stick(self, x, y):
            """设置右摇杆位置（对应结构体的z和rz字段）"""
            self.z = max(-127, min(127, x))
            self.rz = max(-127, min(127, y))
            self._send_report()

        def set_triggers(self, left, right):
            """设置触发器值（-127到127，对应结构体的rx和ry字段）"""
            self.rx = max(-127, min(127, left))
            self.ry = max(-127, min(127, right))
            self._send_report()

        def set_hat(self, direction):
            """设置方向键状态（使用hid_gamepad_hat_t枚举值）"""
            if direction in range(0, 9):  # 0-8有效
                self.hat_state = direction
                self._send_report()

        def hat_click(self, direction, duration=0.1):
            self.set_hat(direction)
            time.sleep(duration)
            self.set_hat(self.HAT_CENTERED)

        # 便捷方法：方向键操作
        def hat_up(self):
            self.set_hat(self.HAT_UP)

        def hat_down(self):
            self.set_hat(self.HAT_DOWN)

        def hat_left(self):
            self.set_hat(self.HAT_LEFT)

        def hat_right(self):
            self.set_hat(self.HAT_RIGHT)

        def hat_center(self):
            self.set_hat(self.HAT_CENTERED)

        def hat_up_right(self):
            self.set_hat(self.HAT_UP_RIGHT)

        def hat_down_right(self):
            self.set_hat(self.HAT_DOWN_RIGHT)

        def hat_down_left(self):
            self.set_hat(self.HAT_DOWN_LEFT)

        def hat_up_left(self):
            self.set_hat(self.HAT_UP_LEFT)

        def release_all(self):
            """释放所有按钮和摇杆"""
            self.buttons_state = 0
            self.hat_state = self.HAT_CENTERED
            self.x = self.y = self.z = self.rz = self.rx = self.ry = 0
            self._send_report()

    def __init__(self, vid_pid=None):
        if vid_pid is None:
            vid_pid = VID_PID
        self.vid_pid = vid_pid
        self.last_time = time.perf_counter()
        devices = hid.enumerate(vid_pid[0], vid_pid[1])
        if not devices:
            raise RuntimeError("No HID device found")
        custom = next(d for d in devices if d["usage_page"] == 0xFF00)
        self.dev = hid.device()
        self.dev.open_path(custom["path"])

        self.g_mouse_buttons = 0
        self.g_keyboard_keys = set()
        self.mouse = self.Mouse(self)
        self.keyboard = self.Keyboard(self)
        self.gamepad = self.Gamepad(self)

    def release_all(self):
        """发送清空所有按键命令"""
        # 释放鼠标：buttons=0
        self.mouse.release_all()
        # 释放键盘：modifier=0, keycodes 全0
        self.keyboard.release_all()
        # 释放游戏手柄
        self.gamepad.release_all()

    def close(self):
        self.release_all()
        self.dev.close()

    def _send(self, data: bytes, cmd_id: int = 0x00):
        """
        发送自定义 HID 报告，格式：
            [Report ID: 0x04] + [Command ID: 1字节] + [data] + [padding to 64 bytes]

        :param data: 有效载荷（不包含 Command ID）
        :param cmd_id: 命令类型 ID，如 0x01=鼠标, 0x02=键盘, 0x03=释放所有等
        """
        # 限制 data 长度：最多 62 字节（因为 Report ID 占1字节，Command ID 占1字节）
        if len(data) > 62:
            data = data[:62]

        # 构造完整 payload: Command ID + data
        payload = bytes([cmd_id]) + data

        # 构造完整报告: Report ID (任意id) + payload + padding to 64
        report = b"\x00" + payload
        report = report.ljust(64, b"\x00")

        # 避免发送过快
        if time.perf_counter() - self.last_time <= 0.001:
            precise_sleep(0.001)

        # 发送
        self.dev.write(report)
        self.last_time = time.perf_counter()

    def _read(self):
        received_data = self.dev.read(64)
        # 解析:标准5键鼠标报告格式

        return received_data

    def _parse_mouse_report(self, raw_data):
        """
        解析原始HID鼠标报告数据
        Args:
            raw_data: 从dev.read(64)获取的原始字节数据
        Returns:
            dict: 解析后的鼠标事件数据
        """
        if len(raw_data) < 4:
            return None

        # 检查是否有Report ID（根据您的设备描述符确定）
        has_report_id = True  # 或False，根据实际情况调整

        if has_report_id:
            # 如果包含Report ID，数据从第2个字节开始
            report_id = raw_data[0]
            buttons = raw_data[1]
            x_move = self._to_signed_byte(raw_data[2])  # 转换为有符号数
            y_move = self._to_signed_byte(raw_data[3])
            wheel_move = self._to_signed_byte(raw_data[4]) if len(raw_data) > 4 else 0
        else:
            # 如果不包含Report ID，数据从第1个字节开始
            report_id = None
            buttons = raw_data[0]
            x_move = self._to_signed_byte(raw_data[1])
            y_move = self._to_signed_byte(raw_data[2])
            wheel_move = self._to_signed_byte(raw_data[3]) if len(raw_data) > 3 else 0

        # 解析按键状态
        left_button = bool(buttons & 0x01)  # 第0位：左键
        right_button = bool(buttons & 0x02)  # 第1位：右键
        middle_button = bool(buttons & 0x04)  # 第2位：中键

        return {
            "report_id": report_id,
            "buttons": {
                "left": left_button,
                "right": right_button,
                "middle": middle_button,
            },
            "x": x_move,
            "y": y_move,
            "wheel": wheel_move,
        }

    def _to_signed_byte(self, value):
        """将无符号字节转换为有符号整数"""
        return value if value < 128 else value - 256


# ----------------- 示例 -----------------
if __name__ == "__main__":
    dev = HIDDevice()
    time.sleep(2)

    print("测试鼠标移动")
    dev.mouse.move(100, 50)
    time.sleep(1)

    print("测试鼠标点击")
    dev.mouse.click(dev.mouse.LEFT)
    time.sleep(1)

    print("测试鼠标滚轮")
    dev.mouse.wheel(-3)
    time.sleep(1)

    print("测试键盘输入 A")
    dev.keyboard.click(dev.keyboard.A)
    time.sleep(1)

    # print("测试组合键 Ctrl+Alt+Del")
    # dev.keyboard.hotkey(dev.keyboard.LCTRL, dev.keyboard.LALT, dev.keyboard.DELETE)
    # time.sleep(2)

    print("测试释放所有")
    dev.release_all()
    try:
        while True:
            # 尝试读取最多64字节数据，这里会阻塞直到收到数据
            received_data = dev._read()
            result = dev._parse_mouse_report(received_data)
            if received_data:
                print(f"收到数据: {received_data}")
                if result:
                    print(f"Report ID: 0x{result['report_id']:02X}")
                    print("按键状态:")
                    for key, value in result["buttons"].items():
                        print(f"  - {key}: {'按下' if value else '释放'}")
                    print(f"X轴移动: {result['x']} (正值向右)")
                    print(f"Y轴移动: {result['y']} (正值向下)")
                    print(f"滚轮: {result['wheel']} (正值向上)")
                # 这里可以添加你的数据处理逻辑，例如解析数据包
                # 例如：根据你的协议解析Report ID和数据内容
            # time.sleep(0.001)  # 短暂睡眠避免CPU占用过高
    except KeyboardInterrupt:
        print("用户中断监听")
    finally:
        dev.close()

    dev.close()
