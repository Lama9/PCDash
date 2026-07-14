"""配置管理模块 — 加载、保存、验证配置"""

import os
import sys
import yaml
import copy
import logging

logger = logging.getLogger(__name__)

# 默认配置模板路径
_DEFAULT_CFG_PATH = os.path.join(os.path.dirname(__file__), "default_config.yaml")

# 用户配置文件路径（与 main.py 同级目录）
_USER_CFG_NAME = "pcdash_config.yaml"


def _get_user_config_path() -> str:
    """获取用户配置文件路径"""
    if getattr(sys, "frozen", False):
        base = os.path.dirname(sys.executable)
    else:
        base = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    return os.path.join(base, _USER_CFG_NAME)


def _deep_merge(base: dict, override: dict) -> dict:
    """递归合并字典，override 覆盖 base"""
    result = copy.deepcopy(base)
    for k, v in override.items():
        if k in result and isinstance(result[k], dict) and isinstance(v, dict):
            result[k] = _deep_merge(result[k], v)
        else:
            result[k] = copy.deepcopy(v)
    return result


def load_default() -> dict:
    """加载默认配置"""
    with open(_DEFAULT_CFG_PATH, "r", encoding="utf-8") as f:
        return yaml.safe_load(f)


def load_config() -> dict:
    """加载用户配置，不存在则用默认配置"""
    default = load_default()
    user_path = _get_user_config_path()

    if os.path.isfile(user_path):
        try:
            with open(user_path, "r", encoding="utf-8") as f:
                user_cfg = yaml.safe_load(f) or {}
            cfg = _deep_merge(default, user_cfg)
            logger.info("已加载用户配置: %s", user_path)
            return cfg
        except Exception as e:
            logger.warning("用户配置文件损坏，使用默认配置: %s", e)
            return default
    else:
        logger.info("未找到用户配置文件，使用默认配置")
        return default


def save_config(cfg: dict) -> None:
    """保存配置到用户文件"""
    user_path = _get_user_config_path()
    with open(user_path, "w", encoding="utf-8") as f:
        yaml.dump(cfg, f, default_flow_style=False, allow_unicode=True)
    logger.info("配置已保存: %s", user_path)


def get_user_config_path() -> str:
    return _get_user_config_path()
