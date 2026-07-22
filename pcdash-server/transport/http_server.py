"""HTTP 服务 — Flask 提供 /api/status 和 /api/background 接口供 ESP 轮询"""

import time
import threading
import logging
import psutil
from flask import Flask, jsonify, Response, request

logger = logging.getLogger(__name__)

_app = Flask(__name__)
_latest_data: dict = {}
_bg_image_bytes: bytes = b""
_bg_hash: str = ""
_lock = threading.Lock()

# CC 状态相关
_cc_sessions: dict = {}          # key: session_id → {state, event, label, cwd, updated_at}
_CC_SESSION_TTL = 28800          # 8 小时僵尸兜底（CC 崩溃未发 Stop 时）；正常状态由 Hook 事件驱动，不依赖超时
_CC_ALARM_TTL   = 720            # alarm/error 12 分钟自动过期（2× 心跳间隔；CC 存活时心跳持续刷新）
_CC_PRIORITY = ["alarm", "error", "busy", "thinking", "success", "idle", "off", "unknown"]

# 禁用 Flask 默认的请求日志（太吵）
log = logging.getLogger("werkzeug")
log.setLevel(logging.WARNING)


def update_data(data: dict) -> None:
    """更新最新数据（线程安全）"""
    global _latest_data
    with _lock:
        _latest_data = data


def set_background(image_bytes: bytes, img_hash: str) -> None:
    """设置背景图数据（线程安全）"""
    global _bg_image_bytes, _bg_hash
    with _lock:
        _bg_image_bytes = image_bytes
        _bg_hash = img_hash
    logger.info("背景图已更新 hash=%s size=%d bytes", img_hash, len(image_bytes))


def get_bg_hash() -> str:
    """返回当前背景图哈希（无背景时返回空串）"""
    return _bg_hash


@_app.route("/api/status")
def api_status():
    now = time.time()
    with _lock:
        data = dict(_latest_data)
        # session 持久有效，直到 Hook 主动上报新状态（含 "off"）或 8 小时僵尸兜底
        # alarm/error 额外限制 5 分钟，防止权限弹窗未响应时永久卡红灯
        active = {sid: s for sid, s in _cc_sessions.items()
                  if now - s["updated_at"] < (
                      _CC_ALARM_TTL if s["state"] in ("alarm", "error") else _CC_SESSION_TTL
                  )}

    def _priority(s: dict) -> int:
        try:
            return _CC_PRIORITY.index(s["state"])
        except ValueError:
            return len(_CC_PRIORITY)

    sorted_sessions = sorted(active.items(), key=lambda x: _priority(x[1]))
    top_state = sorted_sessions[0][1]["state"] if sorted_sessions else "off"
    data["cc"] = {
        "state":    top_state,
        "count":    len(sorted_sessions),
        "sessions": [{"id": sid[:8], "state": s["state"]} for sid, s in sorted_sessions[:6]],
    }
    return jsonify(data)


@_app.route("/api/background")
def api_background():
    """返回当前背景图（JPEG 二进制），无背景时返回 404"""
    with _lock:
        data = _bg_image_bytes
    if not data:
        return "", 404
    return Response(data, mimetype="image/jpeg")


@_app.route("/api/ping")
def api_ping():
    return jsonify({"status": "ok"})


@_app.route("/api/cc-status", methods=["POST"])
def api_cc_status_post():
    data = request.get_json(silent=True) or {}
    session_id = data.get("session_id") or "unknown"
    state      = data.get("state", "unknown")
    event      = data.get("event", "")
    label      = data.get("label", "")
    cwd        = data.get("cwd", "")
    pid        = data.get("pid")

    with _lock:
        if state == "off":
            # CC 会话正常结束，主动删除 session；不再等超时
            _cc_sessions.pop(session_id, None)
        elif state == "heartbeat":
            # 心跳：只刷新 updated_at，不改变已有状态；session 不存在则忽略
            if session_id in _cc_sessions:
                _cc_sessions[session_id]["updated_at"] = time.time()
        else:
            _cc_sessions[session_id] = {
                "state":      state,
                "event":      event,
                "label":      label,
                "cwd":        cwd,
                "pid":        pid,
                "updated_at": time.time(),
            }

    logger.info("[CC] session=%-24s  state=%-10s  event=%-20s  label=%s",
                session_id[:24], state, event, label or cwd)
    return jsonify({"ok": True})


@_app.route("/api/cc-status", methods=["GET"])
def api_cc_status_get():
    now = time.time()
    with _lock:
        active = {
            sid: s for sid, s in _cc_sessions.items()
            if now - s["updated_at"] < (
                _CC_ALARM_TTL if s["state"] in ("alarm", "error") else _CC_SESSION_TTL
            )
        }

    def _priority(s: dict) -> int:
        try:
            return _CC_PRIORITY.index(s["state"])
        except ValueError:
            return len(_CC_PRIORITY)

    sorted_sessions = sorted(active.items(), key=lambda x: _priority(x[1]))
    top_state = sorted_sessions[0][1]["state"] if sorted_sessions else "unknown"

    sessions_out = [
        {
            "session_id": sid,
            "state":      s["state"],
            "label":      s["label"] or s["cwd"],
            "age_ms":     int((now - s["updated_at"]) * 1000),
        }
        for sid, s in sorted_sessions
    ]

    return jsonify({
        "state":    top_state,
        "count":    len(sessions_out),
        "sessions": sessions_out,
    })


_CC_HEARTBEAT_INTERVAL = 300     # 5 分钟检测一次


def _heartbeat_loop() -> None:
    """后台线程：每 5 分钟按 session 各自的 pid 判断是否还存活，只续期真正活着的 alarm/error session"""
    while True:
        time.sleep(_CC_HEARTBEAT_INTERVAL)
        now = time.time()
        with _lock:
            for sid, s in _cc_sessions.items():
                if s["state"] not in ("alarm", "error"):
                    continue
                pid = s.get("pid")
                if pid and psutil.pid_exists(pid):
                    s["updated_at"] = now
                    logger.debug("[CC] 心跳续期 session=%.8s state=%s pid=%s", sid, s["state"], pid)


def start_server(host: str = "0.0.0.0", port: int = 8266) -> threading.Thread:
    """在后台线程启动 Flask 服务"""
    def _run():
        logger.info("HTTP 服务启动: http://%s:%d/api/status", host, port)
        _app.run(host=host, port=port, threaded=True, use_reloader=False)

    t = threading.Thread(target=_run, daemon=True, name="http-server")
    t.start()
    hb = threading.Thread(target=_heartbeat_loop, daemon=True, name="cc-heartbeat")
    hb.start()
    return t
