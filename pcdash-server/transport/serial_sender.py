"""串口发送 — pyserial 主动推送 JSON 数据到 ESP"""

import json
import threading
import time
import logging

import serial
import serial.tools.list_ports

logger = logging.getLogger(__name__)

# CH340 / CP2104 常见 VID:PID
_KNOWN_DEVICES = {
    (0x1A86, 0x7523): "CH340",
    (0x1A86, 0x55D4): "CH9102",
    (0x10C4, 0xEA60): "CP2104",
    (0x303A, 0x1001): "ESP32-S3 CDC",
}


def auto_detect_port() -> str | None:
    """自动扫描串口，按 VID/PID 识别已知设备"""
    ports = serial.tools.list_ports.comports()
    for p in ports:
        if p.vid and p.pid:
            key = (p.vid, p.pid)
            if key in _KNOWN_DEVICES:
                logger.info("检测到 %s: %s (%s)",
                            _KNOWN_DEVICES[key], p.device, p.description)
                return p.device
    # 兜底：返回第一个有 VID 的串口
    for p in ports:
        if p.vid:
            logger.info("使用串口: %s (%s)", p.device, p.description)
            return p.device
    return None


class SerialSender:
    def __init__(self, port: str = "auto", baud: int = 115200):
        self._port_cfg = port
        self._baud = baud
        self._ser: serial.Serial | None = None
        self._lock = threading.Lock()
        self._connected = False
        self._read_thread: threading.Thread | None = None
        self._stop_read = threading.Event()

    @property
    def connected(self) -> bool:
        return self._connected

    @property
    def port_name(self) -> str:
        if self._ser and self._ser.is_open:
            return self._ser.port
        return ""

    def connect(self) -> bool:
        """连接串口"""
        with self._lock:
            if self._ser and self._ser.is_open:
                self._connected = True
                return True

            port = self._port_cfg
            if port == "auto":
                port = auto_detect_port()
                if not port:
                    logger.debug("未检测到可用串口")
                    self._connected = False
                    return False

            try:
                self._ser = serial.Serial(
                    port=port,
                    baudrate=self._baud,
                    timeout=1,
                    write_timeout=2,
                )
                self._connected = True
                logger.info("串口已连接: %s @ %d", port, self._baud)
                self._start_read_thread()
                return True
            except serial.SerialException as e:
                logger.warning("串口连接失败 (%s): %s", port, e)
                self._ser = None
                self._connected = False
                return False

    def send(self, data: dict) -> bool:
        """发送 JSON 数据（以换行符结尾）"""
        if not self._connected:
            return False

        with self._lock:
            try:
                line = json.dumps(data, separators=(",", ":")) + "\n"
                self._ser.write(line.encode("utf-8"))
                return True
            except (serial.SerialException, OSError) as e:
                logger.warning("串口发送失败: %s", e)
                self._close_locked()
                return False

    def _start_read_thread(self):
        """启动后台读线程，打印 ESP 调试日志"""
        self._stop_read.clear()
        self._read_thread = threading.Thread(
            target=self._read_loop, daemon=True, name="serial-read"
        )
        self._read_thread.start()

    def _read_loop(self):
        """读取串口输出；非 JSON 行作为 ESP 日志打印"""
        esp_logger = logging.getLogger("ESP")
        while not self._stop_read.is_set():
            try:
                ser = self._ser
                if ser is None or not ser.is_open:
                    break
                line = ser.readline()
                if not line:
                    continue
                text = line.decode("utf-8", errors="replace").rstrip()
                if text and not text.startswith("{"):
                    esp_logger.info("%s", text)
            except (serial.SerialException, OSError):
                break
            except Exception:
                pass

    def _close_locked(self):
        """关闭串口（已持有锁）"""
        self._connected = False
        self._stop_read.set()
        if self._ser:
            try:
                self._ser.close()
            except Exception:
                pass
            self._ser = None

    def close(self):
        """关闭串口"""
        with self._lock:
            self._close_locked()

    def reconnect_loop(self, interval: float = 5.0, stop_event: threading.Event = None):
        """后台重连循环"""
        while not (stop_event and stop_event.is_set()):
            if not self._connected:
                self.connect()
            if stop_event:
                stop_event.wait(interval)
            else:
                time.sleep(interval)
