"""PCDash PC 端服务 — 入口点

采集 CPU/MEM/DISK/NET/GPU/天气数据，通过 HTTP + 串口双通道发送给 ESP 显示终端。
Windows 下以系统托盘方式运行。
"""

import sys
import os
import hashlib
import socket
import threading
import logging
from datetime import datetime

# 确保项目根目录在 sys.path 中
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from config.config_manager import load_config
from collector.system_info import SystemCollector
from collector.gpu_info import init_gpu, collect_gpu, shutdown_gpu
from collector.weather_info import WeatherCollector
from collector.lunar_info import get_lunar
from transport.http_server import start_server, update_data, set_background, get_bg_hash
from transport.serial_sender import SerialSender

# 日志配置
logging.basicConfig(
    level=logging.INFO,
    format="%(asctime)s [%(levelname)s] %(name)s: %(message)s",
    datefmt="%H:%M:%S",
)
logger = logging.getLogger("pcdash")

# 全局停止事件
_stop_event = threading.Event()


def _load_background(cfg: dict) -> None:
    """从配置路径加载背景图并更新 HTTP 服务端缓存"""
    bg_cfg = cfg.get("background", {})
    if not bg_cfg.get("enabled", False):
        return
    path = bg_cfg.get("image_path", "").strip()
    if not path or not os.path.isfile(path):
        if path:
            logger.warning("背景图路径无效: %s", path)
        return
    with open(path, "rb") as f:
        data = f.read()
    img_hash = hashlib.md5(data).hexdigest()[:8]
    set_background(data, img_hash)


def _build_payload(sys_data: dict, gpu_data: dict | None,
                   weather_data: dict | None, device_name: str) -> dict:
    """组装符合协议的 JSON 数据"""
    now = datetime.now()
    payload = {
        "id": device_name,
        "cpu": sys_data.get("cpu", 0),
        "cpu_name": sys_data.get("cpu_name", ""),
        "cpu_t": sys_data.get("cpu_t"),
        "cpu_f": sys_data.get("cpu_f"),
        "cpu_w": sys_data.get("cpu_w"),
        "mem": sys_data.get("mem"),
        "net": sys_data.get("net"),
        "gpu": gpu_data,
        "time": now.strftime("%H:%M:%S"),
        "date": now.strftime("%Y-%m-%d"),
        "lun": get_lunar(now.date()),
        "wth": weather_data,
        "bgHash": get_bg_hash(),  # 背景图哈希，空串表示无背景
    }
    return payload


def _collector_loop(cfg: dict, serial_sender: SerialSender):
    """数据采集主循环"""
    device_name = cfg.get("device_name") or socket.gethostname()
    interval = cfg.get("serial", {}).get("interval", 2)
    gpu_index = cfg.get("gpu", {}).get("device_index", 0)

    sys_collector = SystemCollector()

    # GPU 初始化
    gpu_enabled = cfg.get("gpu", {}).get("enabled", True)
    if gpu_enabled:
        init_gpu(gpu_index)

    # 天气初始化
    weather_collector = None
    wth_cfg = cfg.get("weather", {})
    if wth_cfg.get("enabled", True):
        weather_collector = WeatherCollector(wth_cfg)

    logger.info("数据采集循环启动 (间隔 %ds)", interval)

    lunar = get_lunar()
    if lunar:
        jq_suffix = f" 今日节气: {lunar['jq']}" if lunar["jq"] else ""
        logger.info("农历信息: %s%s %s%s%s",
                    lunar["gz_y"], lunar["gz_m"], lunar["mo"], lunar["dy"], jq_suffix)
    else:
        logger.warning("农历信息计算失败")
    # _loop_count = 0

    while not _stop_event.is_set():
        try:
            # _loop_count += 1
            # if _loop_count % 10 == 1:
            #     logger.info("采集循环第 %d 次 (天气缓存: %s)",
            #                 _loop_count, "有" if weather_collector and weather_collector._cache else "无")

            # 采集系统数据
            sys_data = sys_collector.collect()

            # 采集 GPU
            gpu_data = collect_gpu(gpu_index) if gpu_enabled else None

            # 采集天气
            weather_data = weather_collector.collect() if weather_collector else None

            # 组装 JSON
            payload = _build_payload(sys_data, gpu_data, weather_data, device_name)

            # HTTP 通道：更新最新数据
            if cfg.get("wifi", {}).get("enabled", True):
                update_data(payload)

            # 串口通道：主动推送
            if cfg.get("serial", {}).get("enabled", True) and serial_sender.connected:
                serial_sender.send(payload)

        except Exception as e:
            logger.error("采集循环异常: %s", e, exc_info=True)

        _stop_event.wait(interval)


def _run_headless(cfg: dict):
    """无界面运行（Linux 或命令行模式）"""
    serial_sender = _start_services(cfg)
    try:
        _collector_loop(cfg, serial_sender)
    except KeyboardInterrupt:
        logger.info("收到 Ctrl+C，正在退出...")
    finally:
        _cleanup(serial_sender)


def _start_services(cfg: dict) -> SerialSender:
    """启动 HTTP 和串口服务"""
    # HTTP 服务
    if cfg.get("wifi", {}).get("enabled", True):
        host = cfg["wifi"].get("host", "0.0.0.0")
        port = cfg["wifi"].get("port", 8266)
        start_server(host, port)

    # 背景图加载（服务启动后立即加载，使 /api/background 可用）
    _load_background(cfg)

    # 串口
    serial_sender = SerialSender(
        port=cfg.get("serial", {}).get("port", "auto"),
        baud=cfg.get("serial", {}).get("baud", 115200),
    )
    if cfg.get("serial", {}).get("enabled", True):
        serial_sender.connect()
        # 串口重连线程
        t = threading.Thread(
            target=serial_sender.reconnect_loop,
            args=(5.0, _stop_event),
            daemon=True, name="serial-reconnect",
        )
        t.start()

    return serial_sender


def _cleanup(serial_sender: SerialSender):
    """清理资源"""
    _stop_event.set()
    serial_sender.close()
    shutdown_gpu()
    logger.info("PCDash 已退出")


def _run_with_tray(cfg: dict):
    """Windows 托盘模式运行"""
    from ui.tray_app import TrayApp
    from ui.config_dialog import show_config_dialog

    serial_sender = _start_services(cfg)

    # 采集线程
    collector_thread = threading.Thread(
        target=_collector_loop,
        args=(cfg, serial_sender),
        daemon=True, name="collector",
    )
    collector_thread.start()

    def _get_status() -> dict:
        http_str = "未启用"
        if cfg.get("wifi", {}).get("enabled", True):
            port = cfg["wifi"].get("port", 8266)
            http_str = f"运行中 (:{port})"
        serial_str = serial_sender.port_name if serial_sender.connected else "未连接"
        return {"http": http_str, "serial": serial_str}

    def _on_quit():
        _cleanup(serial_sender)
        os._exit(0)

    tray = TrayApp(
        on_open_config=lambda: show_config_dialog(),
        on_quit=_on_quit,
        get_status=_get_status,
    )

    logger.info("PCDash 以托盘模式启动")
    # 托盘在主线程运行（pystray 要求）
    tray.run()


def main():
    logger.info("=" * 40)
    logger.info("PCDash Monitor Service 启动")
    logger.info("=" * 40)

    now_local = datetime.now().astimezone()
    logger.info("系统时间: %s (时区: %s, UTC偏移: %s)",
                now_local.strftime("%Y-%m-%d %H:%M:%S %A"),
                now_local.tzname(), now_local.strftime("%z"))

    cfg = load_config()
    resolved_name = cfg.get("device_name") or socket.gethostname()
    logger.info("设备名称: %s", resolved_name)

    # Windows 下默认托盘模式，其他平台或传入 --headless 参数时为无界面模式
    headless = "--headless" in sys.argv

    if sys.platform == "win32" and not headless:
        _run_with_tray(cfg)
    else:
        _run_headless(cfg)


if __name__ == "__main__":
    main()
