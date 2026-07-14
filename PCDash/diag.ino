// diag.ino — 安全守护（WDT + 死因记录 + 堆监控）

#include <esp_task_wdt.h>
#include <esp_system.h>

// LVGL 堆监控（编译失败则改为 0）
#define DIAG_USE_LV_MEM_MONITOR  1

// ── RTC 内存（重启不清零）──
RTC_NOINIT_ATTR uint32_t _rtc_death_count;

static unsigned long _diag_start     = 0;
static unsigned long _diag_last_heap = 0;

// ── 初始化：死因检查 + WDT 启用 ──
void diag_setup() {
    _diag_start = millis();

    // 检查上次重启原因
    esp_reset_reason_t reason = esp_reset_reason();
    if (reason == ESP_RST_TASK_WDT || reason == ESP_RST_PANIC ||
        reason == ESP_RST_INT_WDT  || reason == ESP_RST_WDT) {
        _rtc_death_count++;
        Serial.printf("\n[DIAG] 上次异常重启! 原因=%d 累计=%u次\n",
                      (int)reason, _rtc_death_count);
    } else if (reason == ESP_RST_POWERON) {
        _rtc_death_count = 0;
    }

    // 启用 Task WDT: 10 秒超时，触发 panic 输出调用栈
#if defined(ESP_IDF_VERSION_MAJOR) && ESP_IDF_VERSION_MAJOR >= 5
    esp_task_wdt_config_t wdt_cfg = {
        .timeout_ms     = 10000,
        .idle_core_mask = 0,
        .trigger_panic  = true
    };
    if (esp_task_wdt_reconfigure(&wdt_cfg) != ESP_OK) {
        esp_task_wdt_init(&wdt_cfg);
    }
#else
    esp_task_wdt_init(10, true);
#endif
    esp_task_wdt_add(NULL);
    Serial.println("[DIAG] WDT 已启用 (10s)");
}

// ── 堆状态（每 5 分钟）──
void diag_heap_tick() {
    unsigned long now = millis();
    if (now - _diag_last_heap < 300000) return;
    _diag_last_heap = now;

    unsigned long sec = (now - _diag_start) / 1000;

#if DIAG_USE_LV_MEM_MONITOR
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    Serial.printf("[DIAG] T=%lus ESP:free=%u,max=%u LVGL:free=%u/%u,frag=%u%%\n",
                  sec, ESP.getFreeHeap(), ESP.getMaxAllocHeap(),
                  mon.free_size, mon.total_size, (unsigned)mon.frag_pct);
#else
    Serial.printf("[DIAG] T=%lus ESP:free=%u,max=%u\n",
                  sec, ESP.getFreeHeap(), ESP.getMaxAllocHeap());
#endif

    // WiFi + 数据通道状态
    Serial.printf("[DIAG] WiFi=%s RSSI=%d State=%d PC:%d/%d",
                  wifi_isConnected() ? "OK" : "断开",
                  wifi_isConnected() ? WiFi.RSSI() : 0,
                  (int)app.sysState,
                  app.currentPC, app.pcCount);
    for (int i = 0; i < app.pcCount; i++) {
        unsigned long age = now - pcData[i].lastUpdate;
        Serial.printf(" [%d:%s:%s:%lus]", i, pcData[i].id,
                      pcData[i].connected ? "ON" : "OFF", age / 1000);
    }
    Serial.println();
}

// ── 喂狗 ──
void diag_wdt_reset() {
    esp_task_wdt_reset();
}
