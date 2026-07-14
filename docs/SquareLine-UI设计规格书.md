# PCDash SquareLine Studio UI 设计规格书

> 目标分辨率: 320×240 | LVGL 9.3 | Arduino + TFT_eSPI

---

## 零、重要约定

### 0.1 控件命名规则

SquareLine Studio 会**自动去掉名称中的下划线**。  
规格书中使用下划线写法（便于阅读），实际录入后效果如下：

| 规格书写法 | SquareLine 实际存储 | 导出代码变量名 |
|-----------|-------------------|--------------|
| `lbl_net_up` | `lblnetup` | `ui_lblnetup` |
| `bar_cpu_usg` | `barcpuusg` | `ui_barcpuusg` |
| `pnl_statusbar` | `pnlstatusbar` | `ui_pnlstatusbar` |
| `lbl_time_big` | `lbltimebig` | `ui_lbltimebig` |

> 后续业务代码（`app_binddata.cpp`）引用控件时，统一使用**去掉下划线**的变量名。

### 0.2 颜色参考表

SquareLine 使用标准 **6位 RGB888 十六进制**颜色值（如 `#00FF00`）。  
下表是项目原 RGB565 颜色对应的 RGB888 值：

| 颜色名称 | RGB565 原值 | SquareLine 填写值 | 用途 |
|---------|-----------|-----------------|------|
| 绿色 | `0x07E0` | `#00FF00` | 低负载进度条、上传速度 |
| 青色 | `0x07FF` | `#00FFFF` | CPU标题、下载速度、功耗值 |
| 黄色 | `0xFFE0` | `#FFFF00` | 节气文字、中负载进度条 |
| 红色 | `0xF800` | `#FF0000` | 高负载进度条、高温 |
| 白色 | `0xFFFF` | `#FFFFFF` | 主要文字 |
| 黑色 | `0x0000` | `#000000` | 背景 |
| 深灰 | `0x4208` | `#424242` | 进度条背景、分割线 |
| 浅灰 | `0xC618` | `#C5C5C5` | 次要文字（设备名补充信息）|
| 深蓝 | `0x000B` | `#00005A` | 状态栏背景 |

---

## 一、新建项目参数

打开 SquareLine Studio 1.6.x → **Create** → 顶部 Tab 选 **Arduino**

### 1.1 模板选择

顶部下拉先选 **LVGL 9.3**，然后点选卡片：

```
✅ Arduino with TFT_eSPI
   Language: C | Res.: 320x240 | Color: 16, 16 sw | LVGL: 9.3
```

> 旁边的 `v2.3.0` 是模板版本号，保持默认不用改。

### 1.2 右侧 PROJECT SETTINGS 填写

| 字段 | 值 | 备注 |
|------|----|------|
| Project name | `PCDash` | |
| 保存路径 | `X:\esp\PCDash\PCDash\` | 点文件夹图标选择 |
| Resolution | 320 × 240 | 已预填，不用改 |
| Rotation | 0 degree | |
| Color depth | **`16 sw`** | ⚠️ **必须选 swap 版本！** |
| LVGL version | 9.3 | 已预填 |

填好后点 **CREATE** 按钮。

> **为什么必须选 `16 sw`（swap bytes）？**  
> TFT_eSPI 库内部以大端字节序传输 RGB565 数据。若不勾选 swap，  
> LVGL 渲染出的颜色字节序与硬件不匹配，屏幕颜色全部错乱。

---

## 二、字体准备（建项目前先做）

SquareLine Studio 中，字体在 **Assets → Font** 面板管理。

### 2.1 需要的字体清单

| 用途 | 字体名 | 来源 | 大小 |
|------|--------|------|------|
| 时钟大字 HH:MM | Montserrat | **LVGL 内置，直接用** | 32px |
| 状态栏/数值/进度条 | Montserrat | LVGL 内置 | 12px |
| 中文（星期/天气/农历） | 微软雅黑 | Windows 系统字体 | 14px |
| 中文小号（城市/节气） | 微软雅黑 | Windows 系统字体 | 12px |

### 2.2 获取微软雅黑 TTF 文件

微软雅黑在 Windows 中以 `.ttc`（字体集合）格式存储，需要先提取 TTF：

```
字体文件位置: C:\Windows\Fonts\msyh.ttc  (微软雅黑 Regular)
              C:\Windows\Fonts\msyhbd.ttc (微软雅黑 Bold)
```

**提取方法（PowerShell 脚本）：**

```powershell
# 复制到项目 docs 目录
Copy-Item "C:\Windows\Fonts\msyh.ttc" "X:\esp\PCDash\docs\msyh.ttc"
```

SquareLine Studio 可以直接导入 `.ttc` 文件（会提示选择哪个字重，选 Regular）。

### 2.3 中文字符子集

导入字体时，**不要选 Full Unicode**（会生成几 MB 的字体文件）。  
在 SquareLine 字体设置中选 **Characters**，粘贴以下字符串：

```
星期一二三四五六日年月甲乙丙丁戊己庚辛壬癸子丑寅卯辰巳午未申酉戌亥正腊冬初廿三十立春雨水惊蛰分清谷夏满芒至暑处秋白露寒霜降小大雪晴阴多云雾霾风暴雷电热好转湿温度待连接时超北京东京昌平
```

```
星期一二三四五六日年月甲乙丙丁戊己庚辛壬癸子丑寅卯辰巳午未申酉戌亥正腊冬初廿三十立春雨水惊蛰分清谷夏满芒至暑处秋白露寒霜降小大雪晴阴多云雾霾风暴雷电热好转湿温度待连接时超七八九闰明种中阵夹冰雹沙尘扬浮台飓强轻重宜忌嫁娶纳采祭祀祈福开市动土修造移徙入宅安葬破出行交易床衣财
```

> 如有城市名显示乱码，将城市对应汉字加入上方字符串后重新导出字体即可。

### 2.4 在 SquareLine 中添加字体

1. 左侧面板 → **Assets** → **Fonts** → **+Add Font**
2. 选择 `msyh.ttc` → 字重选 Regular
3. Size 填 `14` → Characters 选 **Custom** → 粘贴上方字符串
4. Name 填 `yahei_14`
5. 同理再添加一个 Size=`12`，Name=`yahei_12`

---

## 三、图片资源准备

需要两张 PNG 图标（温度计、湿度水滴），推荐尺寸 **24×24 px**。

**获取方式（任选一种）：**

- 从 [flaticon.com](https://flaticon.com) 搜索 "thermometer" / "humidity" 下载 PNG 24px
- 或在 SquareLine 中用 `lv_label` + emoji 字符代替图标（更简单）

保存到：

```
X:\esp\PCDash\PCDash\ui\images\icon_temp.png
X:\esp\PCDash\PCDash\ui\images\icon_humi.png
```

在 SquareLine → Assets → Images → +Add Image 导入。

---

## 四、界面布局总览

```
┌─────────────────────────────────────────┐  Y=0
│  状态栏 pnl_statusbar        320×16    │  H=16
│  [上传↑速度] [设备名居中] [连接类型]     │
├────────────────────┬────────────────────┤  Y=16
│   CPU 面板         │   GPU 面板          │  H=112
│   pnl_cpu (左半)   │   pnl_gpu (右半)   │
│                    │                    │
│  标题行 CPU        │  标题行 GPU         │  +16
│  ─────────────── │ ─────────────────  │
│  USG [=====] 45%  │  USG [=====] 60%   │  +24 (行1)
│  TMP [====] 62°C  │  TMP [====] 70°C   │  +24 (行2)
│  PWR [===] 85W    │  PWR [====] 150W   │  +24 (行3)
│  MEM [=====] 67%  │  VRM [===] 45%     │  +24 (行4)
├────────────────────┼────────────────────┤  Y=128
│  时钟面板          │  农历面板           │  H=104
│  pnl_clock (左)   │  pnl_lunar (右)    │
│                   │                    │
│  2026/06/12 星期五 │  丙午年             │  +16
│                   │  甲午月壬午日        │  +26
│    14:30          │  五月十八            │  +26
│                   │  芒种               │  +26
│  🌡28°C  深圳      │                    │  +18
│  💧65%   晴       │                    │  +18
└────────────────────┴────────────────────┘  Y=232
│         底部指示点 pnl_dots  320×8      │  H=8
└─────────────────────────────────────────┘  Y=240
```

---

## 五、完整控件树（照此在 SquareLine 中创建）

### Screen: `scr_main`

背景色: `#000000`（黑色）

---

### 5.1 状态栏

```
pnl_statusbar
  类型: lv_obj
  位置: x=0, y=0
  尺寸: w=320, h=16
  背景色: #00005A（深蓝）
  边框: 无
  圆角: 0
  Padding: 0
  
  ├── lbl_net_up
  │     类型: lv_label
  │     位置: x=2, y=0  对齐: LEFT_MID
  │     文字: "^0B/s"
  │     字体: Montserrat 12
  │     颜色: #00FF00（绿色）
  │
  ├── lbl_net_dn
  │     类型: lv_label
  │     位置: x=42, y=0  对齐: LEFT_MID
  │     文字: "v0B/s"
  │     字体: Montserrat 12
  │     颜色: #00FFFF（青色）
  │
  ├── lbl_device_name
  │     类型: lv_label
  │     位置: x=160, y=0  对齐: CENTER
  │     文字: "PC-001"
  │     字体: Montserrat 12
  │     颜色: #FFFFFF（白色）
  │     长文本模式: LV_LABEL_LONG_CLIP
  │
  └── lbl_conn_type
        类型: lv_label
        位置: x=318, y=0  对齐: RIGHT_MID
        文字: "---"
        字体: Montserrat 12
        颜色: #C5C5C5（浅灰）
```

---

### 5.2 上半区容器

```
pnl_upper
  类型: lv_obj
  位置: x=0, y=16
  尺寸: w=320, h=112
  背景: 透明（bg_opa=0）
  边框: 无
  Padding: 0
```

#### 5.2.1 CPU 面板（左半）

```
pnl_cpu  （pnl_upper 的子控件）
  类型: lv_obj
  位置: x=0, y=0（相对父容器）
  尺寸: w=160, h=112
  背景: 透明
  边框: 无
  Padding: 0

  ├── lbl_cpu_title
  │     类型: lv_label
  │     位置: x=4, y=0  对齐: LEFT_MID  父容器高度16px
  │     文字: "CPU"
  │     字体: Montserrat 12
  │     颜色: #00FFFF（青色）
  │
  ├── [数据行 1 — USG]
  │   ├── lbl_cpu_usg_tag    x=2  y=16  lv_label "USG"  Montserrat 12  白色
  │   ├── bar_cpu_usg        x=30 y=21  w=86 h=8  lv_bar  0-100
  │   │     bg色: #424242（深灰）  指示器色: #00FF00（绿，运行时动态改）
  │   └── lbl_cpu_usg_val    x=158 y=16  lv_label "0%"  Montserrat 12  右对齐  绿色
  │
  ├── [数据行 2 — TMP]
  │   ├── lbl_cpu_tmp_tag    x=2  y=40  lv_label "TMP"  白色
  │   ├── bar_cpu_tmp        x=30 y=45  w=86 h=8  lv_bar
  │   └── lbl_cpu_tmp_val    x=158 y=40  lv_label "0°C"  右对齐
  │
  ├── [数据行 3 — PWR]
  │   ├── lbl_cpu_pwr_tag    x=2  y=64  lv_label "PWR"  白色
  │   ├── bar_cpu_pwr        x=30 y=69  w=86 h=8  lv_bar
  │   └── lbl_cpu_pwr_val    x=158 y=64  lv_label "0W"  右对齐  青色
  │
  └── [数据行 4 — MEM]
      ├── lbl_mem_tag        x=2  y=88  lv_label "MEM"  白色
      ├── bar_mem            x=30 y=93  w=86 h=8  lv_bar
      └── lbl_mem_val        x=158 y=88  lv_label "0%"  右对齐
```

> 行 Y 坐标 = 标题高(16) + 行号×24，即：行1=16, 行2=40, 行3=64, 行4=88

#### 5.2.2 中间竖线

```
line_upper_divider  （pnl_upper 的子控件）
  类型: lv_obj（或直接用 lv_line）
  位置: x=159, y=0
  尺寸: w=2, h=112
  背景色: #424242（深灰）
  边框: 无
```

#### 5.2.3 GPU 面板（右半）

```
pnl_gpu  （pnl_upper 的子控件）
  类型: lv_obj
  位置: x=161, y=0
  尺寸: w=159, h=112
  背景: 透明

  ├── lbl_gpu_title          x=4  "GPU"  Montserrat 12  绿色 #00FF00
  │
  ├── [数据行 1 — USG]
  │   ├── lbl_gpu_usg_tag    x=163 y=16  "USG"
  │   ├── bar_gpu_usg        x=191 y=21  w=85 h=8
  │   └── lbl_gpu_usg_val    x=317 y=16  "0%"  右对齐
  │
  ├── [数据行 2 — TMP]
  │   ├── lbl_gpu_tmp_tag    x=163 y=40  "TMP"
  │   ├── bar_gpu_tmp        x=191 y=45  w=85 h=8
  │   └── lbl_gpu_tmp_val    x=317 y=40  "0°C"
  │
  ├── [数据行 3 — PWR]
  │   ├── lbl_gpu_pwr_tag    x=163 y=64  "PWR"
  │   ├── bar_gpu_pwr        x=191 y=69  w=85 h=8
  │   └── lbl_gpu_pwr_val    x=317 y=64  "0W"  青色
  │
  └── [数据行 4 — VRM（显存）]
      ├── lbl_gpu_vrm_tag    x=163 y=88  "VRM"
      ├── bar_gpu_vrm        x=191 y=93  w=85 h=8
      └── lbl_gpu_vrm_val    x=317 y=88  "0%"
```

> GPU 面板控件 X 坐标 = 161 + CPU 面板对应偏移值

---

### 5.3 水平分割线

```
line_h_divider
  类型: lv_obj
  位置: x=0, y=127
  尺寸: w=320, h=1
  背景色: #424242（深灰）
```

---

### 5.4 下半区容器

```
pnl_lower
  类型: lv_obj
  位置: x=0, y=128
  尺寸: w=320, h=104
  背景: 透明
  Padding: 0
```

#### 5.4.1 时钟/天气面板（左半）

```
pnl_clock  （pnl_lower 子控件）
  位置: x=0, y=0（相对父容器）
  尺寸: w=160, h=104

  ├── lbl_date
  │     位置: x=2, y=0  对齐: LEFT_MID  h=16
  │     文字: "2026/06/12"
  │     字体: Montserrat 12
  │     颜色: #C5C5C5（浅灰）
  │
  ├── lbl_weekday
  │     位置: x=158, y=0  对齐: RIGHT_MID  h=16
  │     文字: "星期五"
  │     字体: yahei_12
  │     颜色: #FFFFFF
  │
  ├── lbl_time_big
  │     位置: x=80, y=42  对齐: CENTER  h=52
  │     文字: "14:30"
  │     字体: Montserrat 32
  │     颜色: #FFFFFF
  │     （整个左半居中显示）
  │
  ├── img_temp_icon
  │     位置: x=2, y=68  对齐: LEFT_MID  (天气区上行)
  │     图片: icon_temp.png (24×24)
  │
  ├── lbl_wth_temp
  │     位置: x=28, y=68  对齐: LEFT_MID
  │     文字: "28°C"
  │     字体: Montserrat 12
  │     颜色: #00FF00（绿，运行时按温度动态改色）
  │
  ├── lbl_wth_city
  │     位置: x=158, y=68  对齐: RIGHT_MID
  │     文字: "深圳"
  │     字体: yahei_12
  │     颜色: #C5C5C5
  │
  ├── img_humi_icon
  │     位置: x=2, y=86  对齐: LEFT_MID  (天气区下行)
  │     图片: icon_humi.png
  │
  ├── lbl_wth_humi
  │     位置: x=28, y=86  对齐: LEFT_MID
  │     文字: "65%"
  │     字体: Montserrat 12
  │     颜色: #00FFFF（青色）
  │
  └── lbl_wth_desc
        位置: x=158, y=86  对齐: RIGHT_MID
        文字: "晴"
        字体: yahei_12
        颜色: #C5C5C5
```

#### 5.4.2 中间竖线（下半区）

```
line_lower_divider
  位置: x=159, y=128（相对屏幕）或 x=159, y=0（相对 pnl_lower）
  尺寸: w=2, h=104
  背景色: #424242
```

#### 5.4.3 农历面板（右半）

```
pnl_lunar  （pnl_lower 子控件）
  位置: x=161, y=0
  尺寸: w=159, h=104

  ├── lbl_gz_year       y=13   "丙午年"    yahei_14  白色  居中
  ├── lbl_gz_md         y=39   "甲午月壬午日"  yahei_12  浅灰  居中
  ├── lbl_lunar_date    y=65   "五月十八"   yahei_14  白色  居中
  └── lbl_jieqi         y=91   "芒种"      yahei_14  #FFFF00（黄色）  居中
```

> 4 行各 26px：行中心 Y = 13, 39, 65, 91

---

### 5.5 底部指示点

```
pnl_dots
  类型: lv_obj
  位置: x=0, y=232
  尺寸: w=320, h=8
  背景: 透明

  ├── dot_0    lv_obj  7×7  圆角=4  背景色: #FFFFFF（当前设备）
  ├── dot_1    lv_obj  7×7  圆角=4  背景色: #424242（未用）
  └── dot_2    lv_obj  7×7  圆角=4  背景色: #424242（未用）
```

三个点居中排列，间距 10px（dot宽7 + gap3 = 10），总宽 = 3×7 + 2×3 = 27px  
起始 X = (320-27)/2 = 146.5 → 取 146

```
dot_0: x=146
dot_1: x=156
dot_2: x=166
```

---

## 六、需要添加的事件

在 SquareLine 中，选中 `pnl_upper`，Events 面板添加：

| 触发器 | 事件动作 | 回调函数名 |
|--------|----------|-----------|
| GESTURE（左滑） | Call Function | `on_upper_swipe_left` |
| GESTURE（右滑） | Call Function | `on_upper_swipe_right` |
| LONG_PRESSED | Call Function | `on_long_press_toggle_scroll` |

在 `pnl_lower` 上同样添加滑动手势（可选，让下半区也能滑动切换）。

> **操作步骤**：选中控件 → 右侧 Events 面板 → "+" → 选触发器 → Action 选 "Call Function" → 填函数名

---

## 七、导出设置

**File → Export UI Files**

```
Export path: X:\esp\PCDash\PCDash\ui\
```

勾选：

- ✅ Export UI source files
- ✅ Export fonts
- ✅ Export images

导出后的目录结构：

```
PCDash\ui\
├── ui.h
├── ui.c
├── ui_helpers.h
├── ui_helpers.c
├── components\        (如有自定义组件)
├── fonts\
│   └── ui_font_yahei_14.c
│   └── ui_font_yahei_12.c
└── images\
    └── ui_img_icon_temp.c
    └── ui_img_icon_humi.c
```

---

## 八、重要提示

1. **控件命名大小写**：SquareLine 导出的控件变量名为 `ui_` + 你设置的名字，例如 `lbl_time_big` → `ui_lbl_time_big`，业务代码中引用时加 `ui_` 前缀

2. **不要手动修改 `ui/` 目录下的文件**：每次重新导出会覆盖。业务逻辑写在 `app/` 目录

3. **勿修改 `ui_events.c`（若有）**：SquareLine 可能生成此文件的骨架，事件回调实现写在 `app/ui_events.cpp`

4. **字体文件路径**：SquareLine 导出的字体是 `.c` 文件，不是 `.h`，编译时会自动包含
