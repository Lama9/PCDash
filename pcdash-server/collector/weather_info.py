"""天气信息采集 — 自动定位 + 多渠道天气接口"""

import time
import logging
import threading
import requests
import urllib3


logger = logging.getLogger(__name__)

_IP_API_URL     = "https://ip9.com.cn/get"
_OWM_API_URL    = "https://api.openweathermap.org/data/2.5/weather"
_OPEN_METEO_URL = "https://api.open-meteo.com/v1/forecast"
_QWEATHER_PATH  = "/v7/weather/now"

# WMO 天气码 → OWM 风格图标
_WMO_ICON = {
    0: "01d", 1: "01d", 2: "02d", 3: "04d",
    45: "50d", 48: "50d",
    51: "09d", 53: "09d", 55: "09d", 56: "09d", 57: "09d",
    61: "10d", 63: "10d", 65: "10d", 66: "10d", 67: "10d",
    71: "13d", 73: "13d", 75: "13d", 77: "13d",
    80: "10d", 81: "10d", 82: "10d",
    85: "13d", 86: "13d",
    95: "11d", 96: "11d", 99: "11d",
}
# WMO 天气码 → 描述文字
_WMO_DESC = {
    0: "Clear", 1: "Clear", 2: "Partly Cloudy", 3: "Overcast",
    45: "Fog", 48: "Fog",
    51: "Drizzle", 53: "Drizzle", 55: "Drizzle",
    61: "Rain", 63: "Rain", 65: "Rain",
    71: "Snow", 73: "Snow", 75: "Snow",
    80: "Showers", 81: "Showers", 82: "Showers",
    95: "Thunderstorm", 96: "Thunderstorm", 99: "Thunderstorm",
}


_WIND_DIR = ["北", "东北", "东", "东南", "南", "西南", "西", "西北"]

def _deg_to_dir(deg: float) -> str:
    """角度转8方位中文"""
    return _WIND_DIR[int((deg + 22.5) / 45) % 8]

def _ms_to_beaufort(ms: float) -> int:
    """米/秒转蒲福风力等级"""
    for level, threshold in enumerate([0.3, 1.6, 3.4, 5.5, 8.0, 10.8, 13.9, 17.2, 20.8, 24.5, 28.5, 32.7]):
        if ms < threshold:
            return level
    return 12


class WeatherCollector:
    def __init__(self, config: dict):
        self._cfg      = config
        self._cache: dict | None = None
        self._last_fetch: float  = 0
        self._interval = config.get("interval", 1800)
        self._location: dict | None = None
        self._provider  = config.get("provider", "openweathermap")
        self._fetching  = False   # 后台线程是否正在请求中

        # 各渠道独立配置（不共用 key）
        self._owm_cfg  = dict(config.get("openweathermap", {}))
        self._qwth_cfg = dict(config.get("qweather", {}))

        # 向后兼容旧格式（api_key / api_host 在 weather 顶层）
        old_key      = config.get("api_key", "")
        old_provider = config.get("api_provider", "")
        if old_provider == "qweather":
            if not self._qwth_cfg.get("api_key"):
                self._qwth_cfg["api_key"]  = old_key
            if not self._qwth_cfg.get("api_host"):
                self._qwth_cfg["api_host"] = config.get("api_host", "")
        elif old_key and not self._owm_cfg.get("api_key"):
            self._owm_cfg["api_key"] = old_key

    def _auto_locate(self) -> dict | None:
        """通过 ip-api.com 自动定位"""
        try:
            resp = requests.get(_IP_API_URL, timeout=5)
            resp.raise_for_status()
            data = resp.json()
            if data.get("ret") == 200:
                d = data.get("data", {})
                loc = {
                    "city": d.get("city", ""),
                    "lat": float(d.get("lat", 0)),
                    "lon": float(d.get("lng", 0)),
                }
                logger.info("自动定位成功: %s (%.2f, %.2f)",
                            loc["city"], loc["lat"], loc["lon"])
                return loc
        except Exception as e:
            logger.warning("自动定位失败: %s", e)
        return None

    def _get_location(self) -> dict | None:
        """获取位置信息（优先配置文件，其次自动定位）"""
        if self._location:
            return self._location

        # 配置文件手动覆盖
        if self._cfg.get("lat") and self._cfg.get("lon"):
            self._location = {
                "lat": self._cfg["lat"],
                "lon": self._cfg["lon"],
                "city": self._cfg.get("city", ""),
            }
            return self._location

        if self._cfg.get("city") and self._cfg.get("location") != "auto":
            self._location = {"city": self._cfg["city"], "lat": None, "lon": None}
            return self._location

        # 自动定位
        loc = self._auto_locate()
        if loc:
            self._location = loc
        return self._location

    def _fetch_owm(self, api_key: str) -> dict | None:
        """从 OpenWeatherMap 获取天气（需要 API Key）"""
        loc = self._get_location()
        if not loc:
            return None

        units = self._cfg.get("units", "metric")
        params = {"appid": api_key, "units": units}

        if loc.get("lat") and loc.get("lon"):
            params["lat"] = loc["lat"]
            params["lon"] = loc["lon"]
        elif loc.get("city"):
            params["q"] = loc["city"]
        else:
            return None

        try:
            resp = requests.get(_OWM_API_URL, params=params, timeout=10)
            resp.raise_for_status()
            data = resp.json()
            weather = data.get("weather", [{}])[0]
            main = data.get("main", {})
            wind = data.get("wind", {})
            spd = float(wind.get("speed", 0))
            deg = float(wind.get("deg", 0))
            wind_str = f"{_deg_to_dir(deg)}风 {_ms_to_beaufort(spd)}级"
            return {
                "temp": int(main.get("temp", 0)),
                "humidity": int(main.get("humidity", 0)),
                "icon": weather.get("icon", ""),
                "desc": weather.get("main", ""),
                "city": data.get("name", loc.get("city", "")),
                "wind": wind_str,
            }
        except Exception as e:
            logger.warning("OpenWeatherMap 请求失败: %s", e)
            return None

    def _fetch_openmeteo(self) -> dict | None:
        """从 Open-Meteo 获取天气（免费，无需 API Key）"""
        loc = self._get_location()
        if not loc or not loc.get("lat") or not loc.get("lon"):
            return None
        try:
            params = {
                "latitude": loc["lat"],
                "longitude": loc["lon"],
                "current": "temperature_2m,weather_code,relative_humidity_2m,wind_speed_10m,wind_direction_10m",
                "timezone": "auto",
            }
            resp = requests.get(_OPEN_METEO_URL, params=params, timeout=10)
            resp.raise_for_status()
            data = resp.json()
            current = data.get("current", {})
            wmo = int(current.get("weather_code", 0))
            spd = float(current.get("wind_speed_10m", 0))      # km/h
            deg = float(current.get("wind_direction_10m", 0))
            wind_str = f"{_deg_to_dir(deg)}风 {_ms_to_beaufort(spd / 3.6)}级"
            return {
                "temp": int(round(current.get("temperature_2m", 0))),
                "humidity": int(round(current.get("relative_humidity_2m", 0))),
                "icon": _WMO_ICON.get(wmo, "01d"),
                "desc": _WMO_DESC.get(wmo, "Clear"),
                "city": loc.get("city", ""),
                "wind": wind_str,
            }
        except Exception as e:
            logger.warning("Open-Meteo 请求失败: %s", e)
            return None

    def _fetch_qweather(self) -> dict | None:
        """从和风天气获取实时天气"""
        api_key  = self._qwth_cfg.get("api_key", "")
        api_host = self._qwth_cfg.get("api_host", "")
        if not api_key or not api_host:
            logger.warning("和风天气未配置 api_key 或 api_host")
            return None

        loc = self._get_location()
        if not loc:
            return None

        if loc.get("lat") and loc.get("lon"):
            location_param = f"{loc['lon']},{loc['lat']}"
        elif loc.get("city"):
            location_param = loc["city"]
        else:
            return None

        url    = f"https://{api_host}{_QWEATHER_PATH}"
        params = {"location": location_param, "key": api_key, "lang": "zh"}
        try:
            resp = requests.get(url, params=params, timeout=10)
            resp.raise_for_status()
            data = resp.json()
            if data.get("code") != "200":
                logger.warning("和风天气返回错误码: %s", data.get("code"))
                return None
            now = data.get("now", {})
            wind_dir   = now.get("windDir", "")
            wind_scale = now.get("windScale", "")
            wind_str   = f"{wind_dir} {wind_scale}级" if wind_dir else ""
            return {
                "temp":     int(now.get("temp", 0)),
                "humidity": int(now.get("humidity", 0)),
                "icon":     now.get("icon", ""),
                "desc":     now.get("text", ""),
                "city":     loc.get("city", ""),
                "wind":     wind_str,
            }
        except Exception as e:
            logger.warning("和风天气请求失败: %s", e)
            return None

    def _fetch_weather(self) -> dict | None:
        """根据配置的渠道获取天气"""
        if self._provider == "qweather":
            return self._fetch_qweather()
        if self._provider == "open_meteo":
            return self._fetch_openmeteo()
        # openweathermap（默认），无 Key 时降级到 Open-Meteo
        api_key = self._owm_cfg.get("api_key", "")
        if api_key:
            result = self._fetch_owm(api_key)
            if result:
                return result
            logger.warning("OWM 失败，降级到 Open-Meteo")
        else:
            logger.debug("OWM 未配置 API Key，降级到 Open-Meteo")
        return self._fetch_openmeteo()

    def _bg_fetch(self) -> None:
        """后台线程：刷新天气缓存，不阻塞主循环"""
        logger.info("天气后台刷新开始 (渠道: %s)", self._provider)
        try:
            result = self._fetch_weather()
            if result:
                self._cache = result
                self._last_fetch = time.time()
                logger.info("天气刷新完成: %s %d°C 湿度%d%%",
                            result.get("city", ""), result.get("temp", 0),
                            result.get("humidity", 0))
            else:
                logger.warning("天气刷新失败，返回 None")
        except Exception as e:
            logger.warning("天气后台刷新异常: %s", e)
        finally:
            self._fetching = False

    def collect(self) -> dict | None:
        """获取天气数据（带缓存）— 异步刷新，立即返回缓存值"""
        now = time.time()
        if not self._fetching and (self._cache is None or
                                   now - self._last_fetch >= self._interval):
            self._fetching = True
            threading.Thread(target=self._bg_fetch, daemon=True,
                             name="weather-fetch").start()
        return self._cache
