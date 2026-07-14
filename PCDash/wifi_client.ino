// wifi_client.ino — WiFi 连接、HTTP 轮询多 PC

extern CCStatus ccStatus;
extern AppState app;

static bool wifiEnabled = false;
static bool wifiConnected = false;
static unsigned long wifiDisconnectTime = 0;  // WiFi 断开时刻
static unsigned long httpFailCount = 0;       // HTTP 连续失败计数

bool wifi_isConnected() { return wifiConnected; }

static TaskHandle_t s_pollHandles[MAX_PC_COUNT];  // Poll Task 句柄

void wifi_init() {
  // 如果没有 WiFi 凭据则跳过
  if (strlen(wifi_ssid) == 0) {
    Serial.println("[WiFi] 无 SSID 配置，WiFi 功能禁用");
    Serial.println("[WiFi] 可通过串口命令配置: WIFI:ssid,password");
    wifiEnabled = false;
    return;
  }

  wifiEnabled = true;
  app.sysState = STATE_WIFI_CONNECTING;

  Serial.printf("[WiFi] 连接到 %s ...\n", wifi_ssid);
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifi_ssid, wifi_pass);

  unsigned long startTime = millis();
  while (WiFi.status() != WL_CONNECTED) {
    delay(100);
    if (millis() - startTime > WIFI_TIMEOUT_MS) {
      Serial.println("[WiFi] 连接超时");
      wifiConnected = false;
      return;
    }
  }

  wifiConnected = true;
  Serial.print("[WiFi] 已连接，IP: ");
  Serial.println(WiFi.localIP());
  ntp_init();
}

void wifi_poll() {
  if (!wifiEnabled) return;

  // WiFi 断开 → 检测重连
  if (WiFi.status() != WL_CONNECTED) {
    if (wifiConnected) {
      // 刚刚断开
      wifiConnected = false;
      wifiDisconnectTime = millis();
      Serial.println("[WiFi] 连接断开，尝试重连...");
      WiFi.reconnect();
    } else if (millis() - wifiDisconnectTime > 30000) {
      // 断开超过 30 秒，再次尝试重连
      Serial.println("[WiFi] 重连超时，再次尝试...");
      WiFi.reconnect();
      wifiDisconnectTime = millis();
    }
    return;
  }

  // WiFi 已连接但标志还是 false → 说明刚恢复
  if (!wifiConnected) {
    wifiConnected = true;
    httpFailCount = 0;
    Serial.printf("[WiFi] 重连成功，IP: %s (断开 %lums)\n",
                  WiFi.localIP().toString().c_str(),
                  millis() - wifiDisconnectTime);
  }

  // 轮询由独立 Poll Task 负责，此处仅管理 WiFi 连接状态
}

void wifi_pollPC(byte index) {
  if (index >= pc_ip_count) return;
  if (strlen(pc_ips[index]) == 0) return;

  String url = "http://" + String(pc_ips[index]) + ":" + String(pc_ports[index]) + "/api/status";

  WiFiClient wifiClient;
  HTTPClient http;
  http.begin(wifiClient, url);
  http.setTimeout(HTTP_TIMEOUT_MS);

  int httpCode = http.GET();
  if (httpCode == HTTP_CODE_OK) {
    httpFailCount = 0;
    String payload = http.getString();
    int pcIdx = serial_parseJSON(payload.c_str(), 1, pc_ips[index]);  // 传入 IP 供 sourceIP 存储
    if (pcIdx >= 0) {
      bg_checkAndDownload(pc_ips[index], pc_ports[index], pcData[pcIdx].bgHash);
    }
  } else {
    httpFailCount++;
    Serial.printf("[WiFi] PC%d (%s) HTTP失败: %d (连续%lu次, RSSI=%d)\n",
                  index, pc_ips[index], httpCode,
                  httpFailCount, WiFi.RSSI());
    if (index == app.currentPC) {
      memset(&ccStatus, 0, sizeof(CCStatus));
      strcpy(ccStatus.state, "off");
    }
  }
  http.end();
}

// 串口命令处理（配置 WiFi 和 PC IP）
void wifi_handleCommand(const String &cmd) {
  // WIFI:ssid,password
  if (cmd.startsWith("WIFI:")) {
    String params = cmd.substring(5);
    int comma = params.indexOf(',');
    if (comma > 0) {
      String ssid = params.substring(0, comma);
      String pass = params.substring(comma + 1);
      settings_saveWiFi(ssid.c_str(), pass.c_str());
      Serial.printf("[CMD] WIFI OK: ssid=%s\n", ssid.c_str());
    } else {
      Serial.println("[CMD] WIFI 格式错误，应为 WIFI:ssid,password");
    }
    Serial.flush();
  }
  // PCIP:index,ip
  else if (cmd.startsWith("PCIP:")) {
    String params = cmd.substring(5);
    int comma = params.indexOf(',');
    if (comma > 0) {
      int idx = params.substring(0, comma).toInt();
      String ip = params.substring(comma + 1);
      settings_savePC_IP(idx, ip.c_str());
      Serial.printf("[CMD] PCIP OK: PC%d=%s\n", idx, ip.c_str());
    } else {
      Serial.println("[CMD] PCIP 格式错误，应为 PCIP:0,192.168.1.x");
    }
    Serial.flush();
  }
  // PORT:index,port — 设置槽位端口（如 PORT:0,18266）
  else if (cmd.startsWith("PORT:")) {
    String params = cmd.substring(5);
    int comma = params.indexOf(',');
    if (comma > 0) {
      int idx = params.substring(0, comma).toInt();
      uint16_t port = params.substring(comma + 1).toInt();
      if (idx < 0 || idx >= MAX_PC_COUNT) {
        Serial.printf("[CMD] PORT 槽位超范围 (0-%d)\n", MAX_PC_COUNT - 1);
      } else if (port == 0) {
        Serial.println("[CMD] PORT 端口号无效");
      } else {
        settings_savePort(idx, port);
        Serial.printf("[CMD] PORT OK: PC%d=%d\n", idx, port);
      }
    } else {
      Serial.println("[CMD] PORT 格式: PORT:index,port (如 PORT:0,18266)");
    }
    Serial.flush();
  }
  // DELPC:index — 删除槽位（IP+PORT 同步清除，后续槽位前移，Task 重启）
  else if (cmd.startsWith("DELPC:")) {
    String idxStr = cmd.substring(6);
    if (idxStr.length() == 0 || !isDigit(idxStr[0])) {
      Serial.println("[CMD] DELPC 格式错误，应为 DELPC:index (如 DELPC:0)");
      Serial.flush();
      return;
    }
    int idx = idxStr.toInt();
    if (pc_ip_count == 0) {
      Serial.println("[CMD] DELPC 无已配置设备");
    } else if (idx < 0 || idx >= (int)pc_ip_count) {
      Serial.printf("[CMD] DELPC 槽位超范围，当前 0-%d\n", pc_ip_count - 1);
    } else {
      char delIP[16];
      strlcpy(delIP, pc_ips[idx], sizeof(delIP));  // 在删除前记录 IP
      Serial.printf("[CMD] 删除 PC%d: %s:%d\n", idx, delIP, pc_ports[idx]);
      wifi_stopPollTasks();                         // 先停所有 Task
      settings_deletePC((byte)idx);                 // 更新 EEPROM 和 pc_ips[]
      xSemaphoreTake(g_data_mutex, portMAX_DELAY);
      datahub_deleteByIP(delIP);                    // 按 IP 精准匹配 pcData 槽位
      xSemaphoreGive(g_data_mutex);
      wifi_startPollTasks();                        // 按新配置重启 Task
      Serial.printf("[CMD] DELPC OK: 剩余 %d 台\n", pc_ip_count);
    }
    Serial.flush();
  }
  // STATUS — 打印系统全状态
  else if (cmd.equalsIgnoreCase("STATUS")) {
    unsigned long sec = millis() / 1000;
    unsigned long h = sec / 3600, m = (sec % 3600) / 60, s = sec % 60;

    static const char* stateNames[] = {
      "BOOT", "WIFI_CONNECTING", "WAITING_DATA", "RUNNING", "DATA_TIMEOUT"
    };
    const char* stName = (app.sysState <= STATE_DATA_TIMEOUT)
                         ? stateNames[app.sysState] : "UNKNOWN";

    Serial.println("\n====== PCDash STATUS ======");
    Serial.printf("  运行时长: %luh%lum%lus\n", h, m, s);
    Serial.printf("  系统状态: %s (%d)\n", stName, app.sysState);
    Serial.printf("  自动切换: %s\n", app.autoScroll ? "ON" : "OFF");

    // WiFi
    Serial.println("------ WiFi ------");
    Serial.printf("  SSID: %s\n", wifi_ssid);
    Serial.printf("  状态: %s\n", wifiConnected ? "已连接" : "断开");
    if (wifiConnected) {
      Serial.printf("  IP:   %s\n", WiFi.localIP().toString().c_str());
      Serial.printf("  RSSI: %d dBm\n", WiFi.RSSI());
    }
    Serial.printf("  HTTP连续失败: %lu\n", httpFailCount);

    // NTP
    Serial.println("------ NTP ------");
    if (timeStatus() != timeNotSet) {
      Serial.printf("  状态: 已同步  %04d-%02d-%02d %02d:%02d:%02d\n",
                    year(), month(), day(), hour(), minute(), second());
    } else {
      Serial.println("  状态: 未同步");
    }

    // EEPROM 配置（DELPC:N 操作的是此表）
    Serial.println("------ EEPROM 配置（DELPC:N 删此行）------");
    for (int i = 0; i < MAX_PC_COUNT; i++) {
      if (i < pc_ip_count && strlen(pc_ips[i]) > 0) {
        Serial.printf("  [%d] IP=%-16s 端口=%d\n", i, pc_ips[i], pc_ports[i]);
      } else {
        Serial.printf("  [%d] (空)\n", i);
      }
    }
    // 运行时设备（pcData 顺序 = 首次响应顺序，与 EEPROM 索引无关）
    Serial.println("------ 运行时设备 ------");
    Serial.printf("  已注册: %d  当前显示: PC%d\n", app.pcCount, app.currentPC);
    for (int i = 0; i < app.pcCount; i++) {
      unsigned long age = millis() - pcData[i].lastUpdate;
      const char* src = pcData[i].source == 1 ? "WiFi" :
                        pcData[i].source == 2 ? "Serial" : "无";
      Serial.printf("  [%d] ID=%-12s 连接=%s 来源=%-6s IP=%-16s 年龄=%lus%s\n",
                    i, pcData[i].id,
                    pcData[i].connected ? "ON " : "OFF",
                    src,
                    pcData[i].sourceIP[0] ? pcData[i].sourceIP : "-",
                    age / 1000,
                    (i == app.currentPC) ? " ★当前" : "");
    }

    // DHT11
    Serial.println("------ DHT11 ------");
    if (app.roomValid) {
      Serial.printf("  温度: %d°C  湿度: %d%%\n", app.roomTemp, app.roomHumidity);
    } else {
      Serial.println("  状态: 无效");
    }

    // 内存
    Serial.println("------ 内存 ------");
    Serial.printf("  ESP堆: free=%u max_block=%u\n",
                  ESP.getFreeHeap(), ESP.getMaxAllocHeap());
#if DIAG_USE_LV_MEM_MONITOR
    lv_mem_monitor_t mon;
    lv_mem_monitor(&mon);
    Serial.printf("  LVGL: free=%u/%u frag=%u%%\n",
                  mon.free_size, mon.total_size, (unsigned)mon.frag_pct);
#endif

    Serial.println("===========================\n");
    Serial.flush();
  }
  else {
    Serial.printf("[CMD] 未知命令: %s\n", cmd.c_str());
    Serial.flush();
  }
}

// 停止所有 Poll Task（重配置槽位前调用）
static void wifi_stopPollTasks() {
  for (byte i = 0; i < MAX_PC_COUNT; i++) {
    if (s_pollHandles[i] != nullptr) {
      vTaskDelete(s_pollHandles[i]);
      s_pollHandles[i] = nullptr;
    }
  }
}

// ── FreeRTOS Poll Task：每台 PC 独立运行于 Core 0 ─────────────
static void poll_task(void* param) {
  byte idx = (byte)(uintptr_t)param;
  for (;;) {
    if (wifi_isConnected() && idx < pc_ip_count && strlen(pc_ips[idx]) > 0) {
      wifi_pollPC(idx);
    }
    vTaskDelay(pdMS_TO_TICKS(POLL_INTERVAL_MS));
  }
}

// 为每台已配置 PC 启动独立 Poll Task（在 wifi_init 之后调用）
void wifi_startPollTasks() {
  memset(s_pollHandles, 0, sizeof(s_pollHandles));
  if (!wifiEnabled || pc_ip_count == 0) {
    Serial.println("[WiFi] Poll Task 未启动（WiFi未启用或无IP配置）");
    return;
  }
  for (byte i = 0; i < pc_ip_count && i < MAX_PC_COUNT; i++) {
    char name[16];
    snprintf(name, sizeof(name), "poll_pc%d", i);
    xTaskCreatePinnedToCore(
      poll_task, name, POLL_TASK_STACK,
      (void*)(uintptr_t)i,
      1, &s_pollHandles[i], 0  // Core 0，优先级 1
    );
    Serial.printf("[WiFi] Poll Task PC%d 已启动 (Core0 栈=%dB)\n", i, POLL_TASK_STACK);
  }
}
