// serial_recv.ino — 串口数据接收与 JSON 解析

extern CCStatus ccStatus;

// 前置声明（serial_receive 先于 serial_parseJSON 定义，Arduino 自动原型生成不可靠）
int serial_parseJSON(const char* json, byte source, const char* sourceIP = nullptr);

// 固定大小的接收缓冲（避免 String 动态分配导致堆碎片）
static char serialBuf[JSON_BUF_SIZE + 1];
static int serialBufPos = 0;

void serial_receive() {
  if (!DATA_SERIAL.available()) return;

  int maxRead = 64;
  while (DATA_SERIAL.available() && maxRead-- > 0) {
    char c = DATA_SERIAL.read();
    if (c == '\n') {
      serialBuf[serialBufPos] = '\0';
      // 修剪尾部空白
      while (serialBufPos > 0 && serialBuf[serialBufPos - 1] <= ' ') {
        serialBufPos--;
        serialBuf[serialBufPos] = '\0';
      }
      // 修剪前导空白
      int start = 0;
      while (start < serialBufPos && serialBuf[start] <= ' ') start++;
      if (start > 0) {
        serialBufPos -= start;
        memmove(serialBuf, serialBuf + start, serialBufPos + 1);
      }

      if (serialBufPos > 2) {
        if (serialBuf[0] == '{') {
          serial_parseJSON(serialBuf, 2);
        } else {
          wifi_handleCommand(String(serialBuf));
        }
      }
      serialBufPos = 0;
    } else if (c != '\r') {
      if (serialBufPos < JSON_BUF_SIZE) {
        serialBuf[serialBufPos++] = c;
      } else {
        serialBufPos = 0;
      }
    }
  }
}

// 解析 JSON 数据并存入 MonitorData，返回 pcData 槽位索引（-1=失败）
int serial_parseJSON(const char* json, byte source, const char* sourceIP) {
  JsonDocument doc;
  DeserializationError err = deserializeJson(doc, json);

  if (err) {
    Serial.printf("[PJ] JSON 失败: %s\n", err.c_str());
    return -1;
  }

  const char* id = doc["id"] | "Unknown";

  // JSON 解析完成后才加锁，减少锁持有时间
  xSemaphoreTake(g_data_mutex, portMAX_DELAY);

  int idx = datahub_findOrCreate(id);
  if (idx < 0) {
    xSemaphoreGive(g_data_mutex);
    return -1;
  }

  MonitorData *d = &pcData[idx];

  strlcpy(d->id, id, sizeof(d->id));
  d->cpu      = doc["cpu"]   | 0.0f;
  d->cpuTemp  = doc["cpu_t"] | -1;
  d->cpuFreq  = doc["cpu_f"] | 0;
  d->cpuPower = doc["cpu_w"] | 0;

  JsonObject mem = doc["mem"];
  if (!mem.isNull()) {
    d->mem.pct   = mem["pct"]   | 0.0f;
    d->mem.used  = mem["used"]  | 0.0f;
    d->mem.total = mem["total"] | 0.0f;
  }

  JsonObject net = doc["net"];
  if (!net.isNull()) {
    d->net.up = net["up"] | 0L;
    d->net.dn = net["dn"] | 0L;
  }

  JsonObject gpu = doc["gpu"];
  if (!gpu.isNull()) {
    d->gpu.usage    = gpu["usage"]   | 0;
    d->gpu.temp     = gpu["temp"]    | 0;
    d->gpu.mem_pct  = gpu["mem_pct"] | 0;
    d->gpu.gpuPower = gpu["watt"]    | 0;
    const char* gname = gpu["name"] | "";
    strlcpy(d->gpu.name, gname, sizeof(d->gpu.name));
    d->gpuValid = true;
  } else {
    d->gpuValid = false;
  }

  strlcpy(d->cpuName, doc["cpu_name"] | "", sizeof(d->cpuName));
  strlcpy(d->timeStr, doc["time"]   | "", sizeof(d->timeStr));
  strlcpy(d->dateStr, doc["date"]   | "", sizeof(d->dateStr));
  strlcpy(d->bgHash,  doc["bgHash"] | "", sizeof(d->bgHash));

  JsonObject wth = doc["wth"];
  if (!wth.isNull()) {
    d->wth.temp     = wth["temp"]     | 0;
    d->wth.humidity = wth["humidity"] | 0;
    strlcpy(d->wth.icon, wth["icon"] | "", sizeof(d->wth.icon));
    strlcpy(d->wth.desc, wth["desc"] | "", sizeof(d->wth.desc));
    strlcpy(d->wth.city, wth["city"] | "", sizeof(d->wth.city));
    strlcpy(d->wth.wind, wth["wind"] | "", sizeof(d->wth.wind));
    d->wthValid = true;
  } else {
    d->wthValid = false;
  }

  JsonObject lun = doc["lun"];
  if (!lun.isNull()) {
    strlcpy(d->lun.gz_y, lun["gz_y"] | "", sizeof(d->lun.gz_y));
    strlcpy(d->lun.gz_m, lun["gz_m"] | "", sizeof(d->lun.gz_m));
    strlcpy(d->lun.gz_d, lun["gz_d"] | "", sizeof(d->lun.gz_d));
    strlcpy(d->lun.mo,   lun["mo"]   | "", sizeof(d->lun.mo));
    strlcpy(d->lun.dy,   lun["dy"]   | "", sizeof(d->lun.dy));
    strlcpy(d->lun.jq,   lun["jq"]   | "", sizeof(d->lun.jq));
    d->lunValid = true;
  } else {
    d->lunValid = false;
  }

  // CC 状态（仅 WiFi 轮询时服务端会注入此字段）
  // 更新规则：谁有 CC 谁说了算（ownerPC）
  //   - 本次 count>0：成为/维持 owner，写入 ccStatus
  //   - 本次 count==0 且是 owner：CC 结束，释放 owner
  //   - 本次 count==0 且非 owner：忽略，不覆盖有效数据
  JsonObject cc = doc["cc"];
  if (!cc.isNull()) {
    int ccCount = cc["count"] | 0;
    if (ccCount > 0) {
      // 本台有活跃 CC session → 成为 owner
      ccStatus.ownerPC = idx;
      strlcpy(ccStatus.state, cc["state"] | "unknown", sizeof(ccStatus.state));
      ccStatus.count = ccCount;
      ccStatus.valid = true;
      ccStatus.session_count = 0;
      JsonArray sArr = cc["sessions"];
      for (JsonObject s : sArr) {
        if (ccStatus.session_count >= MAX_CC_SESSIONS) break;
        strlcpy(ccStatus.sessions[ccStatus.session_count].id,
                s["id"] | "?", sizeof(CCSession::id));
        strlcpy(ccStatus.sessions[ccStatus.session_count].state,
                s["state"] | "?", sizeof(CCSession::state));
        ccStatus.session_count++;
      }
    } else if (ccStatus.ownerPC == idx) {
      // owner 本台 CC 全部结束 → 释放 owner，清空状态
      ccStatus.ownerPC = -1;
      ccStatus.count = 0;
      ccStatus.valid = false;
      ccStatus.session_count = 0;
      strlcpy(ccStatus.state, "off", sizeof(ccStatus.state));
    }
    // 非 owner 且 count==0 → 忽略，保持 ccStatus 不变
  }

  d->lastUpdate = millis();
  d->connected  = true;
  d->source     = source;
  // WiFi 来源时记录服务端 IP，Serial 来源清空（不适用）
  if (source == 1 && sourceIP && sourceIP[0]) {
    strlcpy(d->sourceIP, sourceIP, sizeof(d->sourceIP));
  } else if (source != 1) {
    d->sourceIP[0] = '\0';
  }

  if (app.sysState == STATE_WAITING_DATA || app.sysState == STATE_DATA_TIMEOUT) {
    app.sysState = STATE_RUNNING;
    app.currentPC = idx;
    app.forceRedraw = true;
  }

  xSemaphoreGive(g_data_mutex);

  // Serial.printf("[PJ] OK: %s CPU=%.1f%%\n", d->id, d->cpu);
  return idx;
}
