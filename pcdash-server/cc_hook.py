"""CC Hook 脚本 — 将 Claude Code 事件转为状态并上报到 PCDash 服务端

用法：
    python cc_hook.py --server http://127.0.0.1:8266

CC 触发 Hook 时会将事件 JSON 写入本进程 stdin，本脚本读取后推断状态并 POST 到服务端。
本脚本无状态，每次 Hook 触发时独立运行一次，静默失败不影响 CC 正常工作。
"""

import argparse
import json
import os
import sys
import urllib.error
import urllib.request
from pathlib import Path

# CC Hook 事件 → 状态映射
EVENT_TO_STATE: dict[str, str] = {
    "UserPromptSubmit":    "thinking",
    "UserPromptExpansion": "thinking",
    "PreToolUse":          "busy",
    "PostToolUse":         "thinking",
    "PostToolUseFailure":  "error",
    "PostToolBatch":       "thinking",
    "PermissionDenied":    "error",
    "PermissionRequest":   "alarm",
    "SubagentStart":       "thinking",
    "SubagentStop":        "thinking",
    "TaskCreated":         "busy",
    "TaskCompleted":       "thinking",
    "PreCompact":          "busy",
    "PostCompact":         "thinking",
    "StopFailure":         "error",
    "TeammateIdle":        "off",
    "SessionStart":        "off",
    "SessionEnd":          "off",
}

# Notification 子类型 → 状态映射
NOTIFICATION_TO_STATE: dict[str, str] = {
    "permission_prompt":    "alarm",
    "elicitation_dialog":   "alarm",
    "idle_prompt":          "idle",
    "auth_success":         "success",
    "elicitation_complete": "thinking",
    "elicitation_response": "thinking",
}


def infer_state(payload: dict) -> str:
    """从 Hook payload 推断 CC 状态"""
    event = payload.get("hook_event_name", "")

    if event == "Notification":
        ntype = payload.get("notification_type", "")
        return NOTIFICATION_TO_STATE.get(ntype, "alarm")

    if event == "Stop":
        # 有后台任务或定时任务时仍为 busy，否则真正完成
        if payload.get("background_tasks") or payload.get("session_crons"):
            return "busy"
        return "success"

    return EVENT_TO_STATE.get(event, "unknown")


def post_status(server: str, payload: dict, state: str) -> None:
    """将状态上报到 PCDash 服务端，失败时静默忽略"""
    cwd = payload.get("cwd", "")
    label = Path(cwd).name if cwd else "unknown"

    body = json.dumps({
        "session_id": payload.get("session_id", "unknown"),
        "state":      state,
        "event":      payload.get("hook_event_name", ""),
        "cwd":        cwd,
        "label":      label,
        "pid":        os.getppid(),  # 父进程 = 触发本次 Hook 的 CC 会话进程，供服务端判断该会话是否还存活
    }).encode("utf-8")

    req = urllib.request.Request(
        f"{server}/api/cc-status",
        data=body,
        headers={"Content-Type": "application/json"},
        method="POST",
    )
    try:
        with urllib.request.urlopen(req, timeout=1):
            pass
    except (urllib.error.URLError, OSError):
        pass  # 服务端不可用时静默失败


def main() -> int:
    parser = argparse.ArgumentParser(description="Claude Code Hook → PCDash 状态上报")
    parser.add_argument("--server", default="http://127.0.0.1:8266", help="PCDash 服务端地址")
    args = parser.parse_args()

    try:
        raw = sys.stdin.read()
        payload: dict = json.loads(raw) if raw.strip() else {}
    except Exception:
        payload = {}

    state = infer_state(payload)
    if state != "unknown":
        post_status(args.server, payload, state)

    return 0


if __name__ == "__main__":
    sys.exit(main())
