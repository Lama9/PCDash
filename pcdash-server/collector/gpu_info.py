"""GPU 信息采集 — NVIDIA GPU (pynvml)"""

import logging

logger = logging.getLogger(__name__)

_nvml_available = False
_pynvml = None


def init_gpu(device_index: int = 0) -> bool:
    """初始化 NVML，返回是否成功"""
    global _nvml_available, _pynvml
    try:
        import pynvml  # nvidia-ml-py 包提供
        _pynvml = pynvml
        pynvml.nvmlInit()
        count = pynvml.nvmlDeviceGetCount()
        if count == 0:
            logger.warning("未检测到 NVIDIA GPU")
            return False
        if device_index >= count:
            logger.warning("GPU 索引 %d 超出范围 (共 %d 个)", device_index, count)
            return False
        _nvml_available = True
        logger.info("NVML 初始化成功，检测到 %d 个 GPU", count)
        return True
    except Exception as e:
        logger.warning("NVML 初始化失败（无 NVIDIA 显卡或未装驱动）: %s", e)
        _nvml_available = False
        return False


def collect_gpu(device_index: int = 0) -> dict | None:
    """采集 GPU 数据，不可用时返回 None"""
    if not _nvml_available or _pynvml is None:
        return None
    try:
        handle = _pynvml.nvmlDeviceGetHandleByIndex(device_index)
        util = _pynvml.nvmlDeviceGetUtilizationRates(handle)
        temp = _pynvml.nvmlDeviceGetTemperature(
            handle, _pynvml.NVML_TEMPERATURE_GPU
        )
        mem_info = _pynvml.nvmlDeviceGetMemoryInfo(handle)
        name = _pynvml.nvmlDeviceGetName(handle)
        if isinstance(name, bytes):
            name = name.decode("utf-8")
        # 剥离厂商前缀，保留具体型号（如 "GTX 1660"、"RX 7900 XTX"）
        for prefix in ("NVIDIA GeForce ", "NVIDIA ", "AMD Radeon RX ", "AMD Radeon ", "Intel "):
            if name.lower().startswith(prefix.lower()):
                name = name[len(prefix):]
                break

        mem_pct = int(mem_info.used * 100 / mem_info.total) if mem_info.total > 0 else 0

        try:
            watt = int(_pynvml.nvmlDeviceGetPowerUsage(handle) / 1000)
        except Exception:
            watt = None

        return {
            "usage": int(util.gpu),
            "temp": int(temp),
            "mem_pct": mem_pct,
            "name": name,
            "watt": watt,
        }
    except Exception as e:
        logger.warning("GPU 数据采集失败: %s", e)
        return None


def shutdown_gpu():
    """关闭 NVML"""
    global _nvml_available
    if _nvml_available and _pynvml is not None:
        try:
            _pynvml.nvmlShutdown()
        except Exception:
            pass
        _nvml_available = False
