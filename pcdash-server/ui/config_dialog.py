"""配置界面 — tkinter 实现"""

import tkinter as tk
from tkinter import ttk, messagebox, filedialog
import logging

from config.config_manager import load_config, save_config

logger = logging.getLogger(__name__)


def show_config_dialog(on_save_callback=None):
    """弹出配置编辑窗口"""
    cfg = load_config()
    root = tk.Tk()
    root.title("PCDash 配置")
    root.geometry("460x520")
    root.resizable(False, False)

    notebook = ttk.Notebook(root)
    notebook.pack(fill="both", expand=True, padx=8, pady=8)

    # === 基本设置 ===
    frame_basic = ttk.Frame(notebook, padding=12)
    notebook.add(frame_basic, text="基本设置")

    ttk.Label(frame_basic, text="设备名称:").grid(row=0, column=0, sticky="w", pady=4)
    var_name = tk.StringVar(value=cfg.get("device_name", "My-PC"))
    ttk.Entry(frame_basic, textvariable=var_name, width=30).grid(row=0, column=1, pady=4)

    # === HTTP 设置 ===
    frame_http = ttk.Frame(notebook, padding=12)
    notebook.add(frame_http, text="HTTP 服务")

    var_http_enabled = tk.BooleanVar(value=cfg.get("wifi", {}).get("enabled", True))
    ttk.Checkbutton(frame_http, text="启用 HTTP 服务", variable=var_http_enabled).grid(
        row=0, column=0, columnspan=2, sticky="w", pady=4
    )
    ttk.Label(frame_http, text="监听地址:").grid(row=1, column=0, sticky="w", pady=4)
    var_host = tk.StringVar(value=cfg.get("wifi", {}).get("host", "0.0.0.0"))
    ttk.Entry(frame_http, textvariable=var_host, width=20).grid(row=1, column=1, pady=4)

    ttk.Label(frame_http, text="端口:").grid(row=2, column=0, sticky="w", pady=4)
    var_port = tk.StringVar(value=str(cfg.get("wifi", {}).get("port", 8266)))
    ttk.Entry(frame_http, textvariable=var_port, width=10).grid(row=2, column=1, sticky="w", pady=4)

    # === 串口设置 ===
    frame_serial = ttk.Frame(notebook, padding=12)
    notebook.add(frame_serial, text="串口")

    var_serial_enabled = tk.BooleanVar(value=cfg.get("serial", {}).get("enabled", False))
    ttk.Checkbutton(frame_serial, text="启用 USB 串口通道", variable=var_serial_enabled).grid(
        row=0, column=0, columnspan=2, sticky="w", pady=4
    )
    ttk.Label(frame_serial, text="串口 (auto=自动):").grid(row=1, column=0, sticky="w", pady=4)
    var_serial_port = tk.StringVar(value=cfg.get("serial", {}).get("port", "auto"))
    ttk.Entry(frame_serial, textvariable=var_serial_port, width=15).grid(row=1, column=1, sticky="w", pady=4)

    ttk.Label(frame_serial, text="波特率:").grid(row=2, column=0, sticky="w", pady=4)
    var_baud = tk.StringVar(value=str(cfg.get("serial", {}).get("baud", 115200)))
    ttk.Entry(frame_serial, textvariable=var_baud, width=10).grid(row=2, column=1, sticky="w", pady=4)

    ttk.Label(frame_serial, text="推送间隔 (秒):").grid(row=3, column=0, sticky="w", pady=4)
    var_interval = tk.StringVar(value=str(cfg.get("serial", {}).get("interval", 2)))
    ttk.Entry(frame_serial, textvariable=var_interval, width=10).grid(row=3, column=1, sticky="w", pady=4)

    # === 天气设置 ===
    frame_weather = ttk.Frame(notebook, padding=12)
    notebook.add(frame_weather, text="天气")

    wth_cfg = cfg.get("weather", {})

    var_wth_enabled = tk.BooleanVar(value=wth_cfg.get("enabled", True))
    ttk.Checkbutton(frame_weather, text="启用天气", variable=var_wth_enabled).grid(
        row=0, column=0, columnspan=2, sticky="w", pady=4
    )

    ttk.Label(frame_weather, text="天气接口:").grid(row=1, column=0, sticky="w", pady=4)
    var_provider = tk.StringVar(value=wth_cfg.get("provider", "openweathermap"))
    provider_combo = ttk.Combobox(
        frame_weather, textvariable=var_provider, width=20,
        values=["openweathermap", "open_meteo", "qweather"], state="readonly"
    )
    provider_combo.grid(row=1, column=1, sticky="w", pady=4)

    ttk.Label(frame_weather, text="定位 (auto/城市名):").grid(row=2, column=0, sticky="w", pady=4)
    var_location = tk.StringVar(value=str(wth_cfg.get("location", "auto")))
    ttk.Entry(frame_weather, textvariable=var_location, width=20).grid(row=2, column=1, sticky="w", pady=4)

    # --- OpenWeatherMap 子框 ---
    owm_frame = ttk.LabelFrame(frame_weather, text="OpenWeatherMap", padding=8)
    owm_sub = wth_cfg.get("openweathermap", {})
    var_owm_key = tk.StringVar(value=owm_sub.get("api_key", ""))
    ttk.Label(owm_frame, text="API Key:").grid(row=0, column=0, sticky="w", pady=2)
    ttk.Entry(owm_frame, textvariable=var_owm_key, width=30).grid(row=0, column=1, pady=2)

    # --- 和风天气 QWeather 子框 ---
    qwth_frame = ttk.LabelFrame(frame_weather, text="和风天气 QWeather", padding=8)
    qwth_sub = wth_cfg.get("qweather", {})
    var_qwth_key  = tk.StringVar(value=qwth_sub.get("api_key", ""))
    var_qwth_host = tk.StringVar(value=qwth_sub.get("api_host", ""))
    ttk.Label(qwth_frame, text="API Key:").grid(row=0, column=0, sticky="w", pady=2)
    ttk.Entry(qwth_frame, textvariable=var_qwth_key, width=30).grid(row=0, column=1, pady=2)
    ttk.Label(qwth_frame, text="API Host:").grid(row=1, column=0, sticky="w", pady=2)
    ttk.Entry(qwth_frame, textvariable=var_qwth_host, width=30).grid(row=1, column=1, pady=2)

    # --- Open-Meteo 子框 ---
    openmeteo_frame = ttk.LabelFrame(frame_weather, text="Open-Meteo", padding=8)
    ttk.Label(
        openmeteo_frame,
        text="免费接口，无需 API Key\n（国内/日本网络可能不稳定）",
        foreground="gray", justify="left"
    ).grid(row=0, column=0, sticky="w")

    def _on_provider_change(*_):
        p = var_provider.get()
        owm_frame.grid_remove()
        qwth_frame.grid_remove()
        openmeteo_frame.grid_remove()
        if p == "openweathermap":
            owm_frame.grid(row=3, column=0, columnspan=2, sticky="ew", pady=4)
        elif p == "qweather":
            qwth_frame.grid(row=3, column=0, columnspan=2, sticky="ew", pady=4)
        else:
            openmeteo_frame.grid(row=3, column=0, columnspan=2, sticky="ew", pady=4)

    var_provider.trace_add("write", _on_provider_change)
    _on_provider_change()  # 初始显示

    # === 背景图片设置 ===
    frame_bg = ttk.Frame(notebook, padding=12)
    notebook.add(frame_bg, text="背景图片")

    bg_cfg = cfg.get("background", {})
    var_bg_enabled = tk.BooleanVar(value=bg_cfg.get("enabled", False))
    ttk.Checkbutton(frame_bg, text="启用自定义背景图", variable=var_bg_enabled).grid(
        row=0, column=0, columnspan=3, sticky="w", pady=4
    )

    ttk.Label(frame_bg, text="图片路径:").grid(row=1, column=0, sticky="w", pady=4)
    var_bg_path = tk.StringVar(value=bg_cfg.get("image_path", ""))
    ttk.Entry(frame_bg, textvariable=var_bg_path, width=26).grid(
        row=1, column=1, pady=4, padx=(0, 4)
    )

    def _browse_bg():
        path = filedialog.askopenfilename(
            title="选择背景图片",
            filetypes=[("JPEG 图片", "*.jpg *.jpeg"), ("所有文件", "*.*")],
        )
        if path:
            var_bg_path.set(path)

    ttk.Button(frame_bg, text="浏览", command=_browse_bg).grid(row=1, column=2, pady=4)

    ttk.Label(
        frame_bg,
        text="建议使用 320×240 分辨率的 JPEG 文件。\n更换图片后需重启服务生效。",
        foreground="gray",
        justify="left",
    ).grid(row=2, column=0, columnspan=3, sticky="w", pady=(8, 0))

    # === 按钮 ===
    btn_frame = ttk.Frame(root, padding=8)
    btn_frame.pack(fill="x")

    def _on_save():
        try:
            cfg["device_name"] = var_name.get()
            cfg["wifi"]["enabled"] = var_http_enabled.get()
            cfg["wifi"]["host"] = var_host.get()
            cfg["wifi"]["port"] = int(var_port.get())
            cfg["serial"]["enabled"] = var_serial_enabled.get()
            cfg["serial"]["port"] = var_serial_port.get()
            cfg["serial"]["baud"] = int(var_baud.get())
            cfg["serial"]["interval"] = int(var_interval.get())
            wth = cfg.setdefault("weather", {})
            wth["enabled"]  = var_wth_enabled.get()
            wth["provider"] = var_provider.get()
            loc_val = var_location.get().strip()
            wth["location"] = loc_val
            if loc_val != "auto":
                wth["city"] = loc_val
            wth.setdefault("openweathermap", {})["api_key"] = var_owm_key.get().strip()
            wth.setdefault("qweather", {})["api_key"]       = var_qwth_key.get().strip()
            wth.setdefault("qweather", {})["api_host"]      = var_qwth_host.get().strip()
            # 清理旧格式顶层字段
            for _k in ("api_key", "api_host", "api_provider"):
                wth.pop(_k, None)
            if "background" not in cfg:
                cfg["background"] = {}
            cfg["background"]["enabled"] = var_bg_enabled.get()
            cfg["background"]["image_path"] = var_bg_path.get().strip()
            save_config(cfg)
            messagebox.showinfo("PCDash", "配置已保存，部分设置需要重启生效。")
            if on_save_callback:
                on_save_callback(cfg)
            root.destroy()
        except ValueError as e:
            messagebox.showerror("错误", f"输入格式错误: {e}")

    ttk.Button(btn_frame, text="保存", command=_on_save).pack(side="right", padx=4)
    ttk.Button(btn_frame, text="取消", command=root.destroy).pack(side="right", padx=4)

    root.mainloop()
