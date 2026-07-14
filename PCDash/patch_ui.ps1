# patch_ui.ps1 — SquareLine Studio 导出后 C++ 兼容性补丁
# 使用: 在项目根目录运行  .\patch_ui.ps1
# 幂等: 可重复运行，已打补丁的行自动跳过
#
# ═══════════════════════════════════════════════════════════════
# 背景：LVGL v9.3 移植记录（详见 docs\LVGL_v9_移植记录.md）
# ═══════════════════════════════════════════════════════════════
#
# SquareLine Studio 1.6.1 导出的是 C 文件（.c），但通过根目录桥接
# 文件（ui_src.cpp）以 #include 方式拉入，实际按 C++ 模式编译。
# C++ 比 C 更严格，导出文件需要以下三类修复：
#
# 【1】未启用的 Widget API — 用 #if LV_USE_xxx 保护函数体
#     SquareLine 生成的 ui_helpers.c 包含所有 widget 类型的辅助函数，
#     但本项目 lv_conf.h 只启用了 bar/label/obj。未启用的 widget
#     对应的函数（dropdown/roller/slider/keyboard/textarea/spinbox）
#     在编译时找不到符号，用预处理宏保护函数体使其条件编译。
#     注意：lv_textarea_cursor_up/right/down/left 在 LVGL v9 中已
#     彻底删除，即使启用 LV_USE_TEXTAREA 也无法恢复，只能保护跳过。
#
# 【2】void* 隐式转换 — 加显式强制转换
#     C 允许 void* 赋给任意指针类型，C++ 不允许。
#     受影响：lv_event_get_user_data()、lv_event_get_param()、
#             lv_malloc() 的返回值赋给具体指针类型时均需显式强转。
#
# 【3】枚举类型不匹配 — 加显式强制转换
#     C 允许整数赋给枚举，C++ 不允许。
#     受影响：lv_obj_flag_t（_ui_flag_modify 的 int32_t 参数）、
#             lv_event_code_t（LV_EVENT_GET_COMP_CHILD 是 uint32_t）。
#
# SquareLine 每次重新导出会覆盖 ui/ 目录，本脚本用于重新应用补丁。
# 新增组件文件（ui_comp_*.c）由第3节通配处理，无需手动添加。
# ═══════════════════════════════════════════════════════════════

param([string]$UIDir = (Join-Path $PSScriptRoot "ui"))

$script:cntOK = 0; $script:cntSkip = 0; $script:cntWarn = 0

# ── 核心辅助函数 ──────────────────────────────────────────────
function Patch-File {
    param([string]$File, [string]$Old, [string]$New, [string]$Desc)
    if (-not (Test-Path $File)) {
        Write-Host "  [!!] 文件缺失: $(Split-Path $File -Leaf)" -ForegroundColor Red
        $script:cntWarn++; return
    }
    # 统一转 LF 再比较，避免 CRLF/LF 差异导致误判
    $raw     = [System.IO.File]::ReadAllText($File, [System.Text.Encoding]::UTF8)
    $hasCRLF = $raw.Contains("`r`n")
    $c = $raw -replace "`r`n", "`n"
    $o = $Old -replace "`r`n", "`n"
    $n = $New -replace "`r`n", "`n"

    if ($c.Contains($n)) {
        Write-Host "  [--] 已有，跳过: $Desc"
        $script:cntSkip++; return
    }
    if ($c.Contains($o)) {
        $c = $c.Replace($o, $n)
        if ($hasCRLF) { $c = $c -replace "(?<!\r)`n", "`r`n" }
        [System.IO.File]::WriteAllText($File, $c, [System.Text.UTF8Encoding]::new($false))
        Write-Host "  [OK] $Desc" -ForegroundColor Green
        $script:cntOK++
    } else {
        Write-Host "  [!!] 未找到目标(SL版本变化?): $Desc" -ForegroundColor Yellow
        $script:cntWarn++
    }
}

# ═══════════════════════════════════════════════════════════════
# 1. ui_helpers.c
# ═══════════════════════════════════════════════════════════════
$helpers = Join-Path $UIDir "ui_helpers.c"
Write-Host "`n[1/3] ui_helpers.c"

# 1-1 dropdown
Patch-File $helpers `
@'
    if(id == _UI_DROPDOWN_PROPERTY_SELECTED) lv_dropdown_set_selected(target, val);
'@ `
@'
#if LV_USE_DROPDOWN
    if(id == _UI_DROPDOWN_PROPERTY_SELECTED) lv_dropdown_set_selected(target, val);
#endif
'@ `
"dropdown: #if LV_USE_DROPDOWN 保护"

# 1-2 roller
Patch-File $helpers `
@'
    if(id == _UI_ROLLER_PROPERTY_SELECTED_WITH_ANIM) lv_roller_set_selected(target, val, LV_ANIM_ON);
    if(id == _UI_ROLLER_PROPERTY_SELECTED) lv_roller_set_selected(target, val, LV_ANIM_OFF);
'@ `
@'
#if LV_USE_ROLLER
    if(id == _UI_ROLLER_PROPERTY_SELECTED_WITH_ANIM) lv_roller_set_selected(target, val, LV_ANIM_ON);
    if(id == _UI_ROLLER_PROPERTY_SELECTED) lv_roller_set_selected(target, val, LV_ANIM_OFF);
#endif
'@ `
"roller: #if LV_USE_ROLLER 保护"

# 1-3 slider_set_property
Patch-File $helpers `
@'
    if(id == _UI_SLIDER_PROPERTY_VALUE_WITH_ANIM) lv_slider_set_value(target, val, LV_ANIM_ON);
    if(id == _UI_SLIDER_PROPERTY_VALUE) lv_slider_set_value(target, val, LV_ANIM_OFF);
'@ `
@'
#if LV_USE_SLIDER
    if(id == _UI_SLIDER_PROPERTY_VALUE_WITH_ANIM) lv_slider_set_value(target, val, LV_ANIM_ON);
    if(id == _UI_SLIDER_PROPERTY_VALUE) lv_slider_set_value(target, val, LV_ANIM_OFF);
#endif
'@ `
"slider_set_property: #if LV_USE_SLIDER 保护"

# 1-4 slider_increment (三行唯一组合)
Patch-File $helpers `
@'
    int old = lv_slider_get_value(target);
    lv_slider_set_value(target, old + val, anm);
    lv_obj_send_event(target, LV_EVENT_VALUE_CHANGED, 0);
'@ `
@'
#if LV_USE_SLIDER
    int old = lv_slider_get_value(target);
    lv_slider_set_value(target, old + val, anm);
    lv_obj_send_event(target, LV_EVENT_VALUE_CHANGED, 0);
#endif
'@ `
"slider_increment: #if LV_USE_SLIDER 保护"

# 1-5 keyboard
Patch-File $helpers `
@'
    lv_keyboard_set_textarea(keyboard, textarea);
'@ `
@'
#if LV_USE_KEYBOARD
    lv_keyboard_set_textarea(keyboard, textarea);
#endif
'@ `
"keyboard: #if LV_USE_KEYBOARD 保护"

# 1-6 _ui_flag_modify: int32_t → lv_obj_flag_t 强转（4处）
Patch-File $helpers `
"        if(lv_obj_has_flag(target, flag)) lv_obj_remove_flag(target, flag);" `
"        if(lv_obj_has_flag(target, (lv_obj_flag_t)flag)) lv_obj_remove_flag(target, (lv_obj_flag_t)flag);" `
"flag_modify toggle: lv_obj_flag_t 强转"

Patch-File $helpers `
"        else lv_obj_add_flag(target, flag);" `
"        else lv_obj_add_flag(target, (lv_obj_flag_t)flag);" `
"flag_modify toggle-else: lv_obj_flag_t 强转"

Patch-File $helpers `
"    else if(value == _UI_MODIFY_FLAG_ADD) lv_obj_add_flag(target, flag);" `
"    else if(value == _UI_MODIFY_FLAG_ADD) lv_obj_add_flag(target, (lv_obj_flag_t)flag);" `
"flag_modify ADD: lv_obj_flag_t 强转"

Patch-File $helpers `
"    else lv_obj_remove_flag(target, flag);" `
"    else lv_obj_remove_flag(target, (lv_obj_flag_t)flag);" `
"flag_modify REMOVE: lv_obj_flag_t 强转"

# 1-7 textarea cursor（5行连续，全局唯一）
Patch-File $helpers `
@'
    if(val == UI_MOVE_CURSOR_UP) lv_textarea_cursor_up(target);
    if(val == UI_MOVE_CURSOR_RIGHT) lv_textarea_cursor_right(target);
    if(val == UI_MOVE_CURSOR_DOWN) lv_textarea_cursor_down(target);
    if(val == UI_MOVE_CURSOR_LEFT) lv_textarea_cursor_left(target);
    lv_obj_add_state(target, LV_STATE_FOCUSED);
'@ `
@'
#if LV_USE_TEXTAREA
    if(val == UI_MOVE_CURSOR_UP) lv_textarea_cursor_up(target);
    if(val == UI_MOVE_CURSOR_RIGHT) lv_textarea_cursor_right(target);
    if(val == UI_MOVE_CURSOR_DOWN) lv_textarea_cursor_down(target);
    if(val == UI_MOVE_CURSOR_LEFT) lv_textarea_cursor_left(target);
    lv_obj_add_state(target, LV_STATE_FOCUSED);
#endif
'@ `
"textarea cursor: #if LV_USE_TEXTAREA 保护"

# 1-8 scr_unloaded_delete_cb: void* → 函数指针强转
Patch-File $helpers `
"    screen_destroy_cb_t destroy_cb = lv_event_get_user_data(e);" `
"    screen_destroy_cb_t destroy_cb = (screen_destroy_cb_t)lv_event_get_user_data(e);" `
"scr_unloaded_delete_cb: void* 强转"

# 1-9 _ui_slider_set_text_value（含中间空行的两行块）
Patch-File $helpers `
@'
    lv_snprintf(buf, sizeof(buf), "%s%d%s", prefix, (int)lv_slider_get_value(src), postfix);

    lv_label_set_text(trg, buf);
'@ `
@'
#if LV_USE_SLIDER
    lv_snprintf(buf, sizeof(buf), "%s%d%s", prefix, (int)lv_slider_get_value(src), postfix);
    lv_label_set_text(trg, buf);
#endif
'@ `
"slider_set_text_value: #if LV_USE_SLIDER 保护"

# 1-10 _ui_spinbox_step（整函数体包裹）
Patch-File $helpers `
@'
void _ui_spinbox_step(lv_obj_t * target, int val)

{

    if(val > 0) lv_spinbox_increment(target);

    else lv_spinbox_decrement(target);


    lv_obj_send_event(target, LV_EVENT_VALUE_CHANGED, 0);
}
'@ `
@'
void _ui_spinbox_step(lv_obj_t * target, int val)

{
#if LV_USE_SPINBOX
    if(val > 0) lv_spinbox_increment(target);

    else lv_spinbox_decrement(target);


    lv_obj_send_event(target, LV_EVENT_VALUE_CHANGED, 0);
#endif
}
'@ `
"spinbox: #if LV_USE_SPINBOX 保护"

# ═══════════════════════════════════════════════════════════════
# 2. ui_comp.c
# ═══════════════════════════════════════════════════════════════
$comp = Join-Path $UIDir "ui_comp.c"
Write-Host "`n[2/3] ui_comp.c"

# 2-1 lv_obj_send_event: uint32_t → lv_event_code_t 强转
Patch-File $comp `
"    lv_obj_send_event(comp, LV_EVENT_GET_COMP_CHILD, &info);" `
"    lv_obj_send_event(comp, (lv_event_code_t)LV_EVENT_GET_COMP_CHILD, &info);" `
"ui_comp_get_child: lv_event_code_t 强转"

# 2-2 void* → lv_obj_t** 强转（get 和 del 两个回调，Replace 会全部替换）
Patch-File $comp `
"    lv_obj_t ** c = lv_event_get_user_data(e);" `
"    lv_obj_t ** c = (lv_obj_t **)lv_event_get_user_data(e);" `
"event_cb: void* → lv_obj_t** 强转"

# 2-3 void* → ui_comp_get_child_t* 强转
Patch-File $comp `
"    ui_comp_get_child_t * info = lv_event_get_param(e);" `
"    ui_comp_get_child_t * info = (ui_comp_get_child_t *)lv_event_get_param(e);" `
"get_component_child_event_cb: void* → ui_comp_get_child_t* 强转"

# ═══════════════════════════════════════════════════════════════
# 3. 所有 ui_comp_*.c 组件文件
#    （SquareLine 每增加一个组件就会生成一个新文件，统一处理）
# ═══════════════════════════════════════════════════════════════
Write-Host "`n[3/3] ui_comp_*.c 组件文件"
$compFiles = Get-ChildItem -Path $UIDir -Filter "ui_comp_*.c"
if ($compFiles.Count -eq 0) {
    Write-Host "  [--] 没有 ui_comp_*.c 文件"
} else {
    foreach ($cf in $compFiles) {
        Write-Host "  --> $($cf.Name)"

        # 3-1 lv_malloc 返回 void* → lv_obj_t** 强转
        Patch-File $cf.FullName `
        "    lv_obj_t ** children = lv_malloc(" `
        "    lv_obj_t ** children = (lv_obj_t **)lv_malloc(" `
        "lv_malloc: void* 强转"

        # 3-2 LV_EVENT_GET_COMP_CHILD 作为 add_event_cb 参数需强转
        Patch-File $cf.FullName `
        ", LV_EVENT_GET_COMP_CHILD, children);" `
        ", (lv_event_code_t)LV_EVENT_GET_COMP_CHILD, children);" `
        "add_event_cb: lv_event_code_t 强转"
    }
}

# ═══════════════════════════════════════════════════════════════
# 汇总
# ═══════════════════════════════════════════════════════════════
Write-Host "`n──────────────────────────────────────"
Write-Host "完成: OK=$($script:cntOK)  跳过=$($script:cntSkip)  警告=$($script:cntWarn)"
if ($script:cntWarn -gt 0) {
    Write-Host "有警告项，请检查 SquareLine 版本是否升级改变了生成格式。" -ForegroundColor Yellow
}
