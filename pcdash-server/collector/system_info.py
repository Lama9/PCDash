"""系统信息采集 — CPU / 内存 / 磁盘 / 网络 (psutil)"""

import sys
import time
import re
import platform
import psutil
import requests
import logging

logger = logging.getLogger(__name__)

# LibreHardwareMonitor 内置 Web Server 地址（Options → Start web server，默认端口 8085）
_LHM_WEB_URL = "http://localhost:8085/data.json"
# _LHM_WEB_URL = "http://localhost:18085/data.json"

# Linux Intel RAPL 功率计算状态（存上次能量值和时间戳）
_last_rapl: dict = {"energy": None, "time": None}
_RAPL_PATH = "/sys/class/powercap/intel-rapl/intel-rapl:0/energy_uj"


def _lhm_walk(node: dict, sensors: list, category: str = "") -> None:
    """递归遍历 LHM data.json 树，收集叶子传感器到 sensors 列表。"""
    text = node.get("Text", "")
    children = node.get("Children", [])
    _SENSOR_GROUPS = {"Temperatures", "Powers", "Clocks", "Load",
                      "Fans", "Voltages", "Controls", "Throughput", "Data"}
    if children:
        cat = text if text in _SENSOR_GROUPS else category
        for child in children:
            _lhm_walk(child, sensors, cat)
    else:
        val = node.get("Value", "")
        if val:
            sensors.append({"name": text, "cat": category, "val": val})


def _lhm_parse_float(val: str) -> float | None:
    try:
        return float(val.split()[0].replace(",", "."))
    except Exception:
        return None


def _query_lhm_web() -> tuple[int | None, int | None]:
    """通过 LHM 内置 Web Server 读取 CPU Package 温度和功率。
    LHM 需在 Options → Start web server 中启用（默认端口 8085），无需管理员权限。
    返回 (cpu_temp_°C, cpu_power_W)，不可用时对应值为 None。
    """
    try:
        resp = requests.get(_LHM_WEB_URL, timeout=1.0)
        resp.raise_for_status()
        sensors: list = []
        _lhm_walk(resp.json(), sensors)

        cpu_temps = [v for v in (_lhm_parse_float(s["val"]) for s in sensors
                     if s["cat"] == "Temperatures" and "Package" in s["name"]) if v is not None]
        cpu_watts = [v for v in (_lhm_parse_float(s["val"]) for s in sensors
                     if s["cat"] == "Powers" and "Package" in s["name"]) if v is not None]

        return (int(max(cpu_temps)) if cpu_temps else None,
                int(max(cpu_watts)) if cpu_watts else None)
    except Exception as e:
        logger.debug("LHM Web API 不可用: %s", e)
        return None, None


def _get_cpu_name() -> str:
    """获取 CPU 型号简短字符串（如 'i7-12700KF'），最长 31 字节"""
    try:
        raw = ""
        if sys.platform == "win32":
            import winreg
            key = winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE,
                r"HARDWARE\DESCRIPTION\System\CentralProcessor\0")
            raw, _ = winreg.QueryValueEx(key, "ProcessorNameString")
            winreg.CloseKey(key)
        elif sys.platform == "linux":
            with open("/proc/cpuinfo") as f:
                for line in f:
                    if line.startswith("model name"):
                        raw = line.split(":", 1)[1].strip()
                        break
        if not raw:
            raw = platform.processor()
        # 去掉 (R) (TM) 及频率部分
        raw = re.sub(r"\(R\)|\(TM\)", "", raw)
        raw = re.split(r"\s+@", raw)[0].strip()
        # 提取核心型号关键字
        m = re.search(r"(i[3579]-\w+|Ryzen\s+\d+\s+\w+|Xeon\s+\w+|Core Ultra\s+\d+\s+\w+)", raw)
        return m.group(0) if m else raw[:31]
    except Exception:
        return ""


def _get_cpu_power_linux_rapl() -> int | None:
    """通过 Intel RAPL sysfs 读取 CPU Package 功率（W）。
    首次调用无法计算差值，返回 None；后续每次返回上两次采样之间的平均功率。
    """
    global _last_rapl
    try:
        with open(_RAPL_PATH) as f:
            energy = int(f.read().strip())
        now = time.time()
        last_e, last_t = _last_rapl["energy"], _last_rapl["time"]
        _last_rapl["energy"] = energy
        _last_rapl["time"] = now
        if last_e is None or last_t is None:
            return None
        dt = now - last_t
        if dt <= 0:
            return None
        de = energy - last_e
        if de < 0:
            return None   # 计数器回绕，丢弃本次
        return int(de / dt / 1_000_000)
    except Exception:
        return None


class SystemCollector:
    def __init__(self):
        self._last_net = psutil.net_io_counters()
        self._last_net_time = time.time()
        self._cpu_name = _get_cpu_name()
        # 首次调用 cpu_percent 初始化基线
        psutil.cpu_percent(interval=None)

    def collect(self) -> dict:
        """采集一次系统数据，返回 cpu / mem / net 字段"""
        data = {}

        # CPU 型号（一次性缓存，不重复采集）
        data["cpu_name"] = self._cpu_name

        # CPU 使用率
        data["cpu"] = round(psutil.cpu_percent(interval=None), 1)

        # CPU 温度 + 功率：Windows 通过 LHM Web Server 获取，Linux 走 psutil/RAPL
        if sys.platform == "win32":
            data["cpu_t"], data["cpu_w"] = _query_lhm_web()
        else:
            # 温度：Linux/macOS 走 psutil.sensors_temperatures()
            try:
                sensor_map = psutil.sensors_temperatures() or {}
                cpu_temp = None
                for key in ('coretemp', 'k10temp', 'cpu-thermal', 'cpu_thermal', 'acpitz'):
                    if key in sensor_map and sensor_map[key]:
                        cpu_temp = int(max(t.current for t in sensor_map[key]))
                        break
                data["cpu_t"] = cpu_temp
            except Exception:
                data["cpu_t"] = None
            # 功率：Linux Intel RAPL，其他平台 None
            if sys.platform == "linux":
                data["cpu_w"] = _get_cpu_power_linux_rapl()
            else:
                data["cpu_w"] = None

        # CPU 主频（MHz）
        try:
            freq = psutil.cpu_freq()
            data["cpu_f"] = int(freq.current) if freq else None
        except Exception:
            data["cpu_f"] = None

        # 内存
        mem = psutil.virtual_memory()
        data["mem"] = {
            "pct": round(mem.percent, 1),
            "used": round(mem.used / (1024 ** 3), 1),
            "total": round(mem.total / (1024 ** 3), 1),
        }

        # 网络速度
        now = time.time()
        cur_net = psutil.net_io_counters()
        dt = now - self._last_net_time
        if dt > 0:
            data["net"] = {
                "up": int((cur_net.bytes_sent - self._last_net.bytes_sent) / dt),
                "dn": int((cur_net.bytes_recv - self._last_net.bytes_recv) / dt),
            }
        else:
            data["net"] = {"up": 0, "dn": 0}
        self._last_net = cur_net
        self._last_net_time = now

        return data
