// data_hub.ino — 数据汇聚（管理多 PC 数据槽位、超时检测）

extern CCStatus ccStatus;

// 查找已有 PC 或分配新槽位
int datahub_findOrCreate(const char* id) {
  for (int i = 0; i < app.pcCount; i++) {
    if (strcmp(pcData[i].id, id) == 0) return i;
  }
  if (app.pcCount < MAX_PC_COUNT) {
    int idx = app.pcCount;
    app.pcCount++;
    memset(&pcData[idx], 0, sizeof(MonitorData));
    strlcpy(pcData[idx].id, id, sizeof(pcData[idx].id));
    Serial.printf("[DH] 新 PC: %s (槽位 %d)\n", id, idx);
    return idx;
  }
  Serial.println("[DH] 槽位满");
  return -1;
}

// 超时检测
void datahub_checkTimeout() {
  xSemaphoreTake(g_data_mutex, portMAX_DELAY);
  unsigned long now = millis();  // 锁内读取，确保 now >= lastUpdate（任何 lastUpdate 都在锁内写入）

  for (int i = 0; i < app.pcCount; i++) {
    if (pcData[i].connected) {
      unsigned long age = now - pcData[i].lastUpdate;
      if (age > DATA_TIMEOUT_MS) {
        pcData[i].connected = false;
        pcData[i].source = 0;
        Serial.printf("[DataHub] PC%d '%s' 超时 (age=%lums, WiFi=%s)\n",
                      i, pcData[i].id, age,
                      wifi_isConnected() ? "OK" : "断开");
        // owner 机器离线 → 释放 CC owner，清空状态
        if (ccStatus.ownerPC == i) {
          ccStatus.ownerPC = -1;
          ccStatus.count = 0;
          ccStatus.valid = false;
          ccStatus.session_count = 0;
          strlcpy(ccStatus.state, "off", sizeof(ccStatus.state));
          Serial.printf("[DataHub] CC owner PC%d 离线，ccStatus 已清空\n", i);
        }
      }
    }
  }

  // 当前显示的 PC 断开时，切换到有数据的 PC
  if (app.sysState == STATE_RUNNING) {
    if (!pcData[app.currentPC].connected) {
      bool found = false;
      for (int i = 0; i < app.pcCount; i++) {
        if (pcData[i].connected) {
          Serial.printf("[DataHub] 当前PC%d离线，切换到PC%d '%s'\n",
                        app.currentPC, i, pcData[i].id);
          app.currentPC = i;
          app.forceRedraw = true;
          found = true;
          break;
        }
      }
      if (!found) {
        Serial.printf("[DataHub] 所有PC离线，进入 DATA_TIMEOUT 状态\n");
        app.sysState = STATE_DATA_TIMEOUT;
        app.forceRedraw = true;
      }
    }
  }

  xSemaphoreGive(g_data_mutex);
}

// 获取当前 PC 数据
MonitorData* datahub_currentData() {
  if (app.currentPC < app.pcCount) {
    return &pcData[app.currentPC];
  }
  return &pcData[0];
}

// 切换到上一台 PC
void datahub_prevPC() {
  if (app.pcCount <= 1) return;
  xSemaphoreTake(g_data_mutex, portMAX_DELAY);
  for (int i = 1; i <= app.pcCount; i++) {
    byte idx = (app.currentPC + app.pcCount - i) % app.pcCount;
    if (pcData[idx].connected) {
      app.currentPC = idx;
      app.forceRedraw = true;
      Serial.printf("[DataHub] 切换到 PC: %s\n", pcData[idx].id);
      xSemaphoreGive(g_data_mutex);
      return;
    }
  }
  xSemaphoreGive(g_data_mutex);
}

// 按 sourceIP 精准查找并删除 pcData 槽位（必须在 g_data_mutex 持有期间调用）
void datahub_deleteByIP(const char* ip) {
  byte found = 255;
  for (byte i = 0; i < app.pcCount; i++) {
    if (strcmp(pcData[i].sourceIP, ip) == 0) { found = i; break; }
  }
  if (found == 255) {
    Serial.printf("[DataHub] IP=%s 的设备未在线，将由超时机制自动清理\n", ip);
    return;
  }

  // 后续槽位前移
  for (byte i = found; i < (byte)(app.pcCount - 1); i++) {
    memcpy(&pcData[i], &pcData[i + 1], sizeof(MonitorData));
  }
  // 清空末位槽
  memset(&pcData[app.pcCount - 1], 0, sizeof(MonitorData));
  strlcpy(pcData[app.pcCount - 1].id, "---", sizeof(pcData[0].id));

  app.pcCount--;

  if (app.currentPC >= found && app.currentPC > 0) app.currentPC--;
  if (app.pcCount > 0 && app.currentPC >= app.pcCount) app.currentPC = app.pcCount - 1;

  app.forceRedraw = true;
  Serial.printf("[DataHub] IP=%s 已删除（槽位%d），剩余 %d 台\n", ip, found, app.pcCount);
}

// 切换到下一台 PC
void datahub_nextPC() {
  if (app.pcCount <= 1) return;
  xSemaphoreTake(g_data_mutex, portMAX_DELAY);
  for (int i = 0; i < app.pcCount; i++) {
    byte idx = (app.currentPC + 1 + i) % app.pcCount;
    if (pcData[idx].connected) {
      app.currentPC = idx;
      app.forceRedraw = true;
      Serial.printf("[DataHub] 切换到 PC: %s\n", pcData[idx].id);
      xSemaphoreGive(g_data_mutex);
      return;
    }
  }
  xSemaphoreGive(g_data_mutex);
}
