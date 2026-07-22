# PRD：Claude Code 状态感知显示功能

**文档版本**：v0.4  
**创建日期**：2026-06-11  
**最后更新**：2026-06-15  
**所属项目**：PCDash — ESP32 PC 监控仪表盘  
**文档状态**：已完成（v1.4 随版本发布）

---

## 一、产品背景与定义

### 1.1 背景

Claude Code（CC）是 Anthropic 提供的 AI 编程助手 CLI 工具。在实际使用中，用户无法直观感知 CC 当前所处的工作状态（思考中、执行工具中、等待确认、已完成等），只能盯着终端界面判断。

本功能借助 PCDash 已有的 ESP32 硬件显示能力，将 CC 的实时工作状态映射为可感知的视觉信号，让用户在不看屏幕的情况下也能感知 CC 的工作状态。

### 1.2 产品定义

**CC 状态感知显示功能**：通过 Claude Code 的 Hooks 机制捕获 CC 生命周期事件，经由 Python 服务端聚合后，由 ESP32 端通过屏幕（或外接设备）展示当前 CC 工作状态。

### 1.3 目标用户

使用 Claude Code 进行日常开发、同时桌面上有 PCDash 设备的开发者。

---

## 二、产品需求

### 2.1 功能需求

| 需求编号 | 需求描述 | 优先级 |
|---------|---------|-------|
| F-01 | CC 状态变化后，ESP32 端在 2 秒内反映最新状态 | P0 |
| F-02 | 支持 7 种 CC 状态的识别与区分展示 | P0 |
| F-03 | 同一 PC 上多个 CC 终端并发运行时，按优先级聚合状态 | P1 |
| F-04 | 支持多台 PC 各自维护独立的 CC 状态 | P1 |
| F-05 | CC 会话结束或 PC 离线时，CC 状态区域清空或显示离线 | P1 |
| F-06 | Hook 命令执行失败（网络不通等）不影响 CC 正常工作 | P0 |
| F-07 | ESP32 端 CC 状态区域与现有 PC 监控数据区域共存，互不干扰 | P0 |

### 2.2 非功能需求

- Hook 脚本执行耗时 **不超过 500ms**（非阻塞，后台执行）
- Python 服务端 CC 状态接口响应时间 **< 100ms**
- ESP 端轮询间隔 **2 秒**，延迟可接受
- Hook 脚本**无状态**，每次调用独立执行，无持久化依赖

### 2.3 边界与排除

- **不在本期实现**：ESP 端具体的 UI 布局、颜色方案、动效设计（待后续决策）
- **不在本期实现**：基于 CC 状态触发的外部设备联动（红绿灯硬件等）
- **本期只需**：ESP 端能正确接收状态数据、解析、在屏幕上有基本展示即可

---

## 三、状态定义

参考 demo 实现，定义 7 种 CC 状态：

| 状态值 | 语义 | 触发场景 |
|--------|------|---------|
| `thinking` | 思考/分析中 | UserPromptSubmit、PostToolUse、SubagentStop |
| `busy` | 执行工具中 | PreToolUse、TaskCreated、PreCompact |
| `success` | 任务完成（瞬态） | Stop（且无后台任务/cron） |
| `idle` | CC 空闲，等待下一条指令 | Notification(idle\_prompt)，约 Stop 后 1 分钟触发 |
| `error` | 出错 | StopFailure、PostToolUseFailure、PermissionDenied |
| `alarm` | 需要用户确认 | Notification(permission\_prompt / elicitation\_dialog) |
| `off` | 会话真正结束 | SessionEnd、TeammateIdle |
| `unknown` | 状态未知/初始 | 服务端无数据或数据过期 |

> `success` 与 `idle` 均显示绿灯，区别在于语义：success 是"刚完成"的瞬态确认信号（持续约 1 分钟），idle 是"空闲等待"的持续态（绿灯一直亮直到下一条指令触发 thinking）。

### 3.1 状态优先级（多 CC 实例聚合时使用）

```
alarm > error > busy > thinking > success > idle > off > unknown
```

---

## 四、技术架构

### 4.1 整体架构图

```
┌─────────────────────────────────┐
│           开发者 PC              │
│                                 │
│  ┌─────────┐   stdin JSON       │
│  │CC 实例 1 │──→ Hook 脚本       │
│  └─────────┘       │            │
│                    │ HTTP POST  │
│  ┌─────────┐       │ /api/cc-status
│  │CC 实例 2 │──→ Hook 脚本       │
│  └─────────┘       │            │
│                    ▼            │
│           ┌────────────────┐   │
│           │  Python 服务端  │   │
│           │                │   │
│           │ 现有: /api/status│  │
│           │ 新增: /api/cc-status│
│           └────────────────┘   │
└─────────────────────────────────┘
                 │
                 │ HTTP GET /api/cc-status（2s 轮询）
                 │
         ┌───────────────┐
         │   ESP32 端    │
         │  (PCDash)     │
         │               │
         │ 屏幕展示状态   │
         └───────────────┘
```

### 4.2 各层职责

| 层 | 组件 | 职责 |
|----|------|------|
| 事件捕获层 | CC Hooks + Hook 脚本 | 监听 CC 生命周期事件，解析 stdin payload，POST 到 Python 服务端 |
| 数据聚合层 | Python 服务端（新增路由） | 维护各 CC 实例状态，对外暴露聚合后的状态查询接口 |
| 显示层 | ESP32 PCDash（新增模块） | 轮询 Python 服务端，解析状态数据，屏幕展示 |

---

## 五、协议定义

### 5.1 Hook → Python 服务端（状态上报）

**接口**：`POST /api/cc-status`  
**调用方**：Hook 脚本（每次 CC 事件触发时）

**请求体（JSON）**：

```json
{
  "session_id": "abc123",
  "state": "busy",
  "event": "PreToolUse",
  "cwd": "C:\\Users\\user\\myproject",
  "label": "myproject"
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `session_id` | string | CC 会话唯一标识，从 stdin payload 中提取 |
| `state` | string | 状态值，见第三节 |
| `event` | string | 原始 Hook 事件名，用于调试 |
| `cwd` | string | CC 工作目录，用于标识和展示 |
| `label` | string | 可读标签（取 cwd 最后一段目录名） |

**响应**：

```json
{"ok": true}
```

---

### 5.2 ESP32 → Python 服务端（状态查询）

**接口**：`GET /api/cc-status`  
**调用方**：ESP32 WiFi 轮询（每 2 秒）

**响应体（JSON）**：

```json
{
  "state": "busy",
  "count": 2,
  "sessions": [
    {
      "session_id": "abc123",
      "state": "busy",
      "label": "myproject",
      "age_ms": 800
    },
    {
      "session_id": "def456",
      "state": "thinking",
      "label": "another-repo",
      "age_ms": 1200
    }
  ]
}
```

| 字段 | 类型 | 说明 |
|------|------|------|
| `state` | string | 聚合后的最高优先级状态 |
| `count` | int | 当前活跃 CC 实例数 |
| `sessions` | array | 各实例详情（按优先级降序排列） |
| `age_ms` | int | 该实例最后一次状态上报距今毫秒数 |

**过期规则**：某 session 超过 **30 秒** 未上报，自动从列表中移除（视为会话结束）。

---

## 六、数据流详解

### 6.1 正常工作流

```
sequenceDiagram
    participant H as Hook 脚本
    participant P as Python 服务端
    participant E as ESP32

    Note over H: CC 触发 PreToolUse
    H->>H: 从 stdin 读取 payload JSON
    H->>H: 推断 state = "busy"
    H->>P: POST /api/cc-status (后台非阻塞)
    P->>P: 更新 session 状态表
    
    Note over E: 2s 轮询
    E->>P: GET /api/cc-status
    P->>E: {"state":"busy", "count":1, ...}
    E->>E: 解析并更新显示
```

### 6.2 Hook 脚本工作流

```
CC 触发事件
    │
    ▼
读取 stdin → 解析 JSON payload
    │
    ├─ hook_event_name == "Notification"?
    │       └─ 读取 notification_type 细分状态
    │
    ├─ hook_event_name == "Stop"?
    │       └─ 检查 background_tasks / session_crons
    │              ├─ 有后台任务 → "busy"
    │              └─ 无后台任务 → "success"
    │
    └─ 其他事件 → 查 EVENT_TO_STATE 映射表
    │
    ▼
POST /api/cc-status（直接调用，内部 HTTP timeout=1s，总耗时 < 2s）
```

---

## 七、多 CC 实例处理

### 7.1 问题描述

开发者可能同时在多个终端中运行 CC，例如：
- 终端 A：在 `myproject/` 目录下执行文件修改（`busy`）
- 终端 B：在 `another-repo/` 目录下等待用户输入（`success`）
- 终端 C：在 `scripts/` 目录下遇到权限确认（`alarm`）

ESP32 端需要决定展示什么。

### 7.2 会话标识方案

每个 CC 实例通过以下字段唯一标识：

```
session_id = CC 的 session_id（从 stdin payload 中读取）
```

CC 的 Hook payload 中包含 `session_id` 字段，天然支持多实例区分，**无需额外配置**。

### 7.3 聚合策略

Python 服务端维护一个字典：

```python
# key: session_id, value: {state, label, cwd, updated_at}
sessions: dict[str, SessionInfo]
```

对外暴露聚合结果时：

1. **过滤过期 session**（超过 30s 未更新）
2. **按优先级排序**所有活跃 session
3. `state` 字段取**最高优先级**状态
4. `sessions` 数组包含全部活跃实例详情

### 7.4 ESP32 端展示策略（待定）

ESP32 端在收到多实例数据时，展示策略暂不做定论，仅要求：
- 能正确解析 `state`（聚合状态）
- 能正确解析 `count`（实例数量）
- 能正确解析 `sessions` 数组（各实例状态）

具体 UI 交互形式（用最高优先级状态、显示实例列表、角标数量等）留待后续设计。

---

## 八、CC Hooks 配置

### 8.1 配置文件路径

```
C:\Users\{用户名}\.claude\settings.json
```

---

### 8.2 两种实现方案对比

| 维度 | 方案一：简化版（纯 PowerShell） | 方案二：精确版（Python 脚本） |
|------|-------------------------------|---------------------------|
| 是否需要额外文件 | 否，直接写入 settings.json | 需要 `cc_hook.py` |
| 能否读取 stdin payload | 否，写死固定状态 | 是，完整解析事件数据 |
| Notification 细分 | 无法区分，统一当 `alarm` | 可区分 `permission_prompt` / `idle_prompt` 等 |
| Stop 后台任务判断 | 无法判断，统一当 `success` | 可检查 `background_tasks` 字段 |
| 多实例 session 区分 | 无法区分，所有事件共用固定 ID | 可从 payload 提取真实 `session_id` |
| 适用场景 | 快速验证、单 CC 实例、简单需求 | 生产使用、多实例、状态准确性要求高 |

---

### 8.3 方案一：简化版（纯 PowerShell）

每个 Hook 直接在 `command` 中写死对应的状态值，无需任何 Python 脚本。

**原理**：不读 stdin，直接用 `Invoke-RestMethod` 向服务端 POST 固定状态。  
**限制**：`Notification` 无法细分子类型，`Stop` 无法判断是否有后台任务。

#### 各事件固定状态映射

| Hook 事件 | 写死状态 | 说明 |
|-----------|---------|------|
| `UserPromptSubmit` | `thinking` | 用户提交后 CC 开始思考 |
| `PreToolUse` | `busy` | CC 正在调用工具 |
| `PostToolUse` | `thinking` | 工具完成，CC 继续分析 |
| `Notification` | `alarm` | 保守处理，统一视为需要关注 |
| `Stop` | `success` | CC 完成等待输入（忽略后台任务情况）|

#### 完整 settings.json 配置

```json
{
  "hooks": {
    "UserPromptSubmit": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "Start-Job { Invoke-RestMethod 'http://127.0.0.1:8266/api/cc-status' -Method Post -Body '{\"session_id\":\"default\",\"state\":\"thinking\",\"event\":\"UserPromptSubmit\",\"label\":\"\"}' -ContentType 'application/json' -TimeoutSec 1 -ErrorAction SilentlyContinue } | Out-Null",
            "timeout": 2
          }
        ]
      }
    ],
    "PreToolUse": [
      {
        "matcher": "*",
        "hooks": [
          {
            "type": "command",
            "command": "Start-Job { Invoke-RestMethod 'http://127.0.0.1:8266/api/cc-status' -Method Post -Body '{\"session_id\":\"default\",\"state\":\"busy\",\"event\":\"PreToolUse\",\"label\":\"\"}' -ContentType 'application/json' -TimeoutSec 1 -ErrorAction SilentlyContinue } | Out-Null",
            "timeout": 2
          }
        ]
      }
    ],
    "PostToolUse": [
      {
        "matcher": "*",
        "hooks": [
          {
            "type": "command",
            "command": "Start-Job { Invoke-RestMethod 'http://127.0.0.1:8266/api/cc-status' -Method Post -Body '{\"session_id\":\"default\",\"state\":\"thinking\",\"event\":\"PostToolUse\",\"label\":\"\"}' -ContentType 'application/json' -TimeoutSec 1 -ErrorAction SilentlyContinue } | Out-Null",
            "timeout": 2
          }
        ]
      }
    ],
    "Notification": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "Start-Job { Invoke-RestMethod 'http://127.0.0.1:8266/api/cc-status' -Method Post -Body '{\"session_id\":\"default\",\"state\":\"alarm\",\"event\":\"Notification\",\"label\":\"\"}' -ContentType 'application/json' -TimeoutSec 1 -ErrorAction SilentlyContinue } | Out-Null",
            "timeout": 2
          }
        ]
      }
    ],
    "Stop": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "Start-Job { Invoke-RestMethod 'http://127.0.0.1:8266/api/cc-status' -Method Post -Body '{\"session_id\":\"default\",\"state\":\"success\",\"event\":\"Stop\",\"label\":\"\"}' -ContentType 'application/json' -TimeoutSec 1 -ErrorAction SilentlyContinue } | Out-Null",
            "timeout": 2
          }
        ]
      }
    ]
  }
}
```

#### 关键字段说明

| 字段 | 说明 |
|------|------|
| `Start-Job { ... } \| Out-Null` | 后台异步执行，立即返回不阻塞 CC |
| `-TimeoutSec 1` | HTTP 请求超时 1 秒，防止服务端无响应时卡住后台 Job |
| `-ErrorAction SilentlyContinue` | 静默忽略错误（网络不通等），不产生异常输出 |
| `session_id: "default"` | 简化版无法读 stdin，所有事件共用固定 ID，多 CC 实例无法区分 |

---

### 8.4 方案二：精确版（Python 脚本 cc_hook.py）

使用 `pcdash-server/cc_hook.py`，读取 CC 注入 stdin 的完整事件 payload，精确推断状态。

**文件位置**：`pcdash-server\cc_hook.py`（已创建）

#### 完整 settings.json 配置

五个 Hook 事件调用同一个脚本，由脚本内部通过 stdin payload 区分事件类型。

> **注意**：不使用 `Start-Job`，因为 `Start-Job` 会创建新进程导致 CC 注入的 stdin 无法传递给 Python 脚本。直接调用 Python，cc_hook.py 内部 HTTP timeout=1s，整体耗时 < 2s，不影响 CC 正常响应。

```json
{
  "hooks": {
    "UserPromptSubmit": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "python \"X:\\esp\\PCDash\\pcdash-server\\cc_hook.py\" --server http://127.0.0.1:8266",
            "timeout": 3
          }
        ]
      }
    ],
    "PreToolUse": [
      {
        "matcher": "*",
        "hooks": [
          {
            "type": "command",
            "command": "python \"X:\\esp\\PCDash\\pcdash-server\\cc_hook.py\" --server http://127.0.0.1:8266",
            "timeout": 3
          }
        ]
      }
    ],
    "PostToolUse": [
      {
        "matcher": "*",
        "hooks": [
          {
            "type": "command",
            "command": "python \"X:\\esp\\PCDash\\pcdash-server\\cc_hook.py\" --server http://127.0.0.1:8266",
            "timeout": 3
          }
        ]
      }
    ],
    "Notification": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "python \"X:\\esp\\PCDash\\pcdash-server\\cc_hook.py\" --server http://127.0.0.1:8266",
            "timeout": 3
          }
        ]
      }
    ],
    "Stop": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "python \"X:\\esp\\PCDash\\pcdash-server\\cc_hook.py\" --server http://127.0.0.1:8266",
            "timeout": 3
          }
        ]
      }
    ],
    "SessionEnd": [
      {
        "hooks": [
          {
            "type": "command",
            "command": "python \"X:\\esp\\PCDash\\pcdash-server\\cc_hook.py\" --server http://127.0.0.1:8266",
            "timeout": 3
          }
        ]
      }
    ]
  }
}
```

> **补充说明（2026-07-22 新增）**：早期版本只配置了前 5 个 Hook，未配置 `SessionEnd`，导致 `cc_hook.py` 里 `SessionEnd → "off"` 的映射从未被触发——正常退出 CC 也不会清除服务端 session，只能靠超时兜底（`success`/`busy` 等非 alarm 状态的兜底 TTL 长达 8 小时）。补上 `SessionEnd` 后，正常退出可以立即清除，无需等待超时。

#### cc_hook.py 工作流程

```
CC 触发 Hook
    │
    ├─ 启动命令进程
    ├─ 将事件 JSON 写入进程 stdin
    │
    ▼
cc_hook.py 运行
    │
    ├─ 读取 stdin → 解析 JSON payload
    │
    ├─ hook_event_name == "Notification"?
    │       └─ 读 notification_type → NOTIFICATION_TO_STATE
    │
    ├─ hook_event_name == "Stop"?
    │       ├─ 有 background_tasks/session_crons → "busy"
    │       └─ 无 → "success"
    │
    └─ 其他事件 → EVENT_TO_STATE 映射表
    │
    ▼
POST /api/cc-status（含 session_id、state、label 等）
    │
    ▼
静默退出（成功或失败均不影响 CC）
```

---

### 8.5 Hook 事件 → 状态映射表（精确版参考）

```python
EVENT_TO_STATE = {
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
    # Stop 事件特殊处理：
    #   有 background_tasks 或 session_crons → "busy"
    #   否则 → "success"
}

NOTIFICATION_TO_STATE = {
    "permission_prompt":    "alarm",
    "elicitation_dialog":   "alarm",
    "idle_prompt":          "idle",    # CC 空闲等待输入，绿灯持续常亮
    "auth_success":         "success",
    "elicitation_complete": "thinking",
    "elicitation_response": "thinking",
    # 未知子类型默认 → "alarm"（保守处理）
}
```

---

## 九、与现有架构的集成点

### 9.1 Python 服务端改动（已实现）

| 改动项 | 文件 | 说明 |
|-------|------|------|
| 新增 `POST /api/cc-status` 路由 | `transport/http_server.py` | 接收 Hook 脚本上报，维护内存 session 字典 |
| 新增 `GET /api/cc-status` 路由 | `transport/http_server.py` | 供调试用，ESP 端不直接调用 |
| **CC 状态嵌入 `/api/status` 响应** | `transport/http_server.py` | `api_status()` 在返回前注入 `cc` 字段，ESP 无需额外轮询 |
| alarm/error 状态 TTL 延长至 720s（12分钟） | `transport/http_server.py` | 防止等待 permission 时过早过期消失（`_CC_ALARM_TTL`） |
| **心跳续期改为按 session 的 pid 判断存活（2026-07-22）** | `cc_hook.py` + `transport/http_server.py` | 原实现用 `tasklist` 检测"电脑上是否存在任意一个 claude.exe 进程"，只要存在就把所有 alarm/error session 无差别续期——导致触发报警的会话早已退出，但只要机器上还有别的 CC 在跑，红灯就永远续命清不掉。修复后：`cc_hook.py` 用 `os.getppid()` 把触发本次事件的 CC 进程 pid 一并上报；`_heartbeat_loop` 用 `psutil.pid_exists(pid)` 逐个 session 判断，只续期真正还活着的那个会话，死掉的会话不再续期，最多 12 分钟内自然过期 |

`/api/status` 响应中 `cc` 字段结构：
```json
"cc": {
  "state": "busy",
  "count": 2,
  "sessions": [{"id": "abc12345", "state": "busy"}, {"id": "def67890", "state": "thinking"}]
}
```

### 9.2 ESP32 端改动（已实现）

| 改动项 | 文件 | 说明 |
|-------|------|------|
| `CCStatus` / `CCSession` 数据结构 | `data_model.h` | MAX_CC_SESSIONS=6；嵌套 CCSession 数组 |
| CC 字段解析（WiFi 路径） | `wifi_client.ino` | `wifi_pollPC()` 解析 `/api/status` 响应中的 `cc` 字段 |
| CC 字段解析（串口路径） | `serial_recv.ino` | `serial_parseJSON()` 解析串口 JSON 中的 `cc` 字段 |
| 三色 CC LED 驱动 | `cc_led.ino`（新建） | GPIO38 红/GPIO39 黄/GPIO40 绿；状态→闪烁/常亮/熄灭 |
| CC 状态按钮绑定 | `app_binddata.cpp` | `bind_cc_status()` 更新 `ui_ButCCStatus` 颜色 + `ui_lblCCStatus` 文本 |
| CC 详情页 | `cc_screen.cpp`（新建） | B 方案：骨架/数据分离；滚动卡片列表（MAX 6）；底部常驻摘要；Montserrat 字体 |
| 触摸事件绑定 | `ui_events.cpp` | CC 按钮点击 → 打开详情页；返回按钮 → 主页 |
| 触摸坐标修正 | `drivers/lv_port_indev.cpp` | 180° 翻转：`x = (SCREEN_W-1)-touch.X(); y = (SCREEN_H-1)-touch.Y()` |
| 按键缩减至 K1-K3 | `drivers/lv_port_indev.cpp` `config.h` | K4（GPIO38）已复用为 CC LED 红色，驱动数组改为 3 元素 |

### 9.3 新增文件

| 文件 | 位置 | 说明 |
|------|------|------|
| `cc_hook.py` | `pcdash-server/cc_hook.py` | CC Hook 脚本（精确版），无状态，每次事件调用一次 |
| `cc_led.ino` | `PCDash/cc_led.ino` | 三色 LED 状态机驱动 |
| `cc_screen.cpp` | `PCDash/cc_screen.cpp` | CC 详情屏幕，B 方案骨架/热刷新 |

---

## 十、关键设计决策记录

| 决策 | 选项 | 选择 | 理由 |
|------|------|------|------|
| ESP 通信方式 | 串口推送 vs HTTP 轮询 | **HTTP 轮询** | 2s 延迟可接受；ESP 已有 WiFi；与现有架构统一 |
| Hook → 服务端通信 | TCP 直连 vs HTTP POST | **HTTP POST** | 复用 Python 服务端；无需 ESP 作为 TCP 服务器 |
| **CC 数据下发方式** | 单独 `/api/cc-status` 轮询 vs 嵌入 `/api/status` | **嵌入 `/api/status` `cc` 字段** | 减少 ESP 端一个轮询连接；CC 与系统数据同步下发，逻辑更简单 |
| 多实例聚合 | 最高优先级 vs 全部展示 | **最高优先级**（`state` 字段）+ **全部暴露**（`sessions` 字段） | 两者兼顾，ESP 端自行决定展示策略 |
| **最大并发会话数** | 3 / 6 / 20 | **6** | 3 个实际不够用；20 个冗余太多；6 个能覆盖多终端并发场景 |
| 状态过期 | 永久保留 vs 超时清除 | **30s 正常 / 300s alarm+error** | 防僵尸残留；alarm/error 需要等待用户处理，30s 会提前消失 |
| Hook 执行方式 | 阻塞 vs 非阻塞 | **直接调用（内部 1s timeout）** | Start-Job 会切断 stdin，导致 Python 读不到事件 payload |
| **CC 详情页刷新方案** | A 方案（销毁重建）vs B 方案（骨架/热刷新）| **B 方案** | A 方案每次数据变化都销毁重建，触发 LVGL 屏幕闪烁；B 方案骨架只建一次，仅更新文本/颜色 |
| **LED 引脚** | 新增引脚 vs 复用 K4 | **复用 K4 GPIO38 为 CC LED 红** | K4 功能较边缘化（亮度调节）；GPIO38-40 紧邻布局方便接线 |
| **触摸坐标方向** | 正常 vs 180° 翻转 | **180° 翻转** | 触摸传感器物理安装方向与屏幕显示方向相差 180°，LVGL 层需软件修正 |
| **详情页字体** | 自定义 fangsong/yahei vs LVGL 内置 Montserrat | **Montserrat（12/14/24）** | 详情页无中文需求；自定义字体产生 glyph 警告；Montserrat 已内置，零额外开销 |

---

## 十一、待决策事项

## 十二、实现进度

| 模块 | 状态 | 说明 |
|------|------|------|
| CC Hooks 配置（settings.json） | ✅ 已完成 | `C:\Users\mysong\.claude\settings.json` |
| cc_hook.py（事件推断 + 上报） | ✅ 已完成 | `pcdash-server\cc_hook.py` |
| Python 服务端 `POST /api/cc-status` | ✅ 已完成 | `transport\http_server.py` |
| Python 服务端 `GET /api/cc-status` | ✅ 已完成 | `transport\http_server.py`（调试用） |
| **CC 状态嵌入 `/api/status`** | ✅ 已完成 | `api_status()` 注入 `cc` 字段；alarm/error TTL=300s |
| 端到端链路验证 | ✅ 已验证 | 日志输出正确，5 种事件全部上报 |
| ESP32 `data_model.h` CCStatus 结构 | ✅ 已完成 | `MAX_CC_SESSIONS=6`；CCSession 数组 |
| ESP32 WiFi 路径 CC 解析 | ✅ 已完成 | `wifi_client.ino` 解析 `cc` 字段 |
| ESP32 串口路径 CC 解析 | ✅ 已完成 | `serial_recv.ino` 解析 `cc` 字段 |
| 三色 CC LED 驱动 | ✅ 已完成 | `cc_led.ino`；alarm=红快闪/error=红常/busy=黄常/thinking=黄慢闪/success=绿常 |
| CC 状态按钮 + 状态文字绑定 | ✅ 已完成 | `app_binddata.cpp` `bind_cc_status()` |
| CC 详情页（B 方案） | ✅ 已完成 | `cc_screen.cpp`；滚动卡片列表 + 底部常驻摘要 |
| 触摸事件绑定（CC 按钮） | ✅ 已完成 | `ui_events.cpp`；点击 → 详情页，返回 → 主页 |
| 触摸 180° 坐标修正 | ✅ 已完成 | `drivers/lv_port_indev.cpp` |
| K4 引脚复用（移除按键，改 LED）| ✅ 已完成 | `config.h` + `lv_port_indev.cpp` |
| 心跳续期改为按 pid 判断存活 | ✅ 已完成（2026-07-22） | `cc_hook.py` 上报 `pid`；`http_server.py` `_heartbeat_loop` 用 `psutil.pid_exists()` 逐 session 判断，替换原先的全局 `tasklist` 检测 |
| `SessionEnd` Hook 补全 | ✅ 已完成（2026-07-22） | 全局 `settings.json` 新增 `SessionEnd` 事件，正常退出可立即清除 session，无需等待超时 |

## 十三、后续优化方向（可选）

- [ ] 串口路径 CC 数据支持（当前串口 JSON 无 `cc` 字段，需 PC 端 serial_sender.py 注入）
- [ ] 多 PC 模式下各 PC 独立展示 CC 状态（当前仅展示当前选中 PC 的 CC 状态）
- [ ] CC 详情页支持显示会话工作目录（cwd / label 字段）
- [ ] 背光亮度调节功能（原 K4 功能）迁移到软件菜单或长按其他按键
