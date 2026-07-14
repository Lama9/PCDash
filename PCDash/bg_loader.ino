// bg_loader.ino — 背景图下载（WiFi HTTP）、FFat 缓存、JPEG 渲染

// settings.ino 中定义的变量（字母序排在本文件之后，需显式声明）
extern char     pc_ips[MAX_PC_COUNT][16];
extern byte     pc_ip_count;
extern uint16_t pc_ports[MAX_PC_COUNT];

static char  _savedBgHash[9] = "";  // 当前已缓存的图片哈希
static bool  _bgLoaded       = false;
static bool  _spiffsReady    = false;

// TJpgDec 解码回调：将解码后的 tile 直接推送到屏幕
static uint16_t _jpegTileCount = 0;
static bool _jpegRenderCb(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  if (y >= SCREEN_H) return false;
  tft.setSwapBytes(false);   // TJpgDec 已处理字节序，避免双重交换导致色彩错误
  tft.pushImage(x, y, w, h, bitmap);
  tft.setSwapBytes(true);    // 恢复全局设置
  _jpegTileCount++;
  return true;
}

// 启动时初始化：挂载 FFat，读取 EEPROM 中保存的哈希
void bg_init() {
  if (!FFat.begin(true)) {
    Serial.println("[BG] FFat 挂载失败 — 请在 Arduino IDE 工具菜单确认分区方案含 FFat");
    return;
  }
  _spiffsReady = true;
  Serial.printf("[BG] FFat 就绪  总=%uKB  已用=%uKB\n",
                (unsigned)(FFat.totalBytes() / 1024),
                (unsigned)(FFat.usedBytes()  / 1024));

  // 从 EEPROM 读取已保存的 hash（允许 0-9 a-f 的 hex 字符）
  for (int i = 0; i < 8; i++) {
    char c = (char)EEPROM.read(EEPROM_BGHASH_ADDR + i);
    _savedBgHash[i] = ((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f')) ? c : '\0';
  }
  _savedBgHash[8] = '\0';

  _bgLoaded = FFat.exists(BG_SPIFFS_PATH);
  if (_bgLoaded) {
    File f = FFat.open(BG_SPIFFS_PATH, FILE_READ);
    Serial.printf("[BG] 背景图就绪 hash=%s size=%d\n", _savedBgHash, (int)f.size());
    f.close();
  }

  TJpgDec.setJpgScale(1);
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(_jpegRenderCb);
}

bool bg_isActive() {
  return _bgLoaded;
}

// 将 FFat 中的背景 JPEG 渲染到屏幕（0,0）— 直接流式解码，不需要 RAM 缓冲
void bg_drawBackground() {
  if (!_bgLoaded) return;

  _jpegTileCount = 0;
  JRESULT res = TJpgDec.drawFsJpg(0, 0, BG_SPIFFS_PATH, FFat);

  if (res != JDR_OK) {
    Serial.printf("[BG] JPEG 解码失败 code=%d\n", (int)res);
    _bgLoaded = false;
  } else {
    Serial.printf("[BG] 渲染完成 tiles=%d\n", _jpegTileCount);
  }
}

// 比较哈希，不同时从服务端下载新背景并保存到 FFat
// ip/port 直接传入，不再依赖 EEPROM 槽位索引
void bg_checkAndDownload(const char* ip, uint16_t port, const char* newHash) {
  if (!_spiffsReady) return;
  if (!wifi_isConnected()) return;

  // 服务端禁用背景（空哈希）→ 清除本地缓存并重绘
  if (!newHash || newHash[0] == '\0') {
    if (_bgLoaded || _savedBgHash[0] != '\0') {
      _bgLoaded = false;
      _savedBgHash[0] = '\0';
      if (FFat.exists(BG_SPIFFS_PATH)) {
        FFat.remove(BG_SPIFFS_PATH);
        Serial.println("[BG] 已删除 FFat 背景缓存");
      }
      for (int i = 0; i < 8; i++) EEPROM.write(EEPROM_BGHASH_ADDR + i, 0);
      EEPROM.commit();
      app.forceRedraw = true;
      Serial.println("[BG] 背景已禁用");
    }
    return;
  }

  if (strcmp(_savedBgHash, newHash) == 0) return;  // 哈希未变，无需更新

  Serial.printf("[BG] hash 变化 [%s]->[%s]，开始下载\n", _savedBgHash, newHash);

  if (!ip || ip[0] == '\0') return;

  String url = "http://" + String(ip) + ":" + String(port) + "/api/background";

  WiFiClient wifiClient;
  HTTPClient http;
  http.begin(wifiClient, url);
  http.setTimeout(BG_DOWNLOAD_TIMEOUT);

  int code = http.GET();
  if (code != HTTP_CODE_OK) {
    Serial.printf("[BG] 下载失败: HTTP %d\n", code);
    http.end();
    return;
  }

  int total = http.getSize();
  if (total <= 0 || total > 500000) {
    Serial.printf("[BG] 异常 Content-Length: %d\n", total);
    http.end();
    return;
  }

  // 分配下载缓冲（优先 PSRAM，不可用则 SRAM）
#if HAS_PSRAM
  uint8_t* dlBuf = (uint8_t*)ps_malloc(total);
  if (!dlBuf) dlBuf = (uint8_t*)malloc(total);
#else
  uint8_t* dlBuf = (uint8_t*)malloc(total);
#endif
  if (!dlBuf) {
    Serial.printf("[BG] 无法分配缓冲 %d bytes\n", total);
    http.end();
    return;
  }

  WiFiClient* stream = http.getStreamPtr();
  stream->setTimeout(BG_DOWNLOAD_TIMEOUT / 1000);
  int written = stream->readBytes(dlBuf, total);
  http.end();

  if (written != total) {
    Serial.printf("[BG] 数据不完整: %d/%d bytes\n", written, total);
    free(dlBuf);
    return;
  }

  // 验证 JPEG 文件头 FF D8 FF
  if (written < 3 || dlBuf[0] != 0xFF || dlBuf[1] != 0xD8 || dlBuf[2] != 0xFF) {
    Serial.printf("[BG] 非合法 JPEG，头: %02X %02X %02X\n",
                  dlBuf[0], dlBuf[1], dlBuf[2]);
    free(dlBuf);
    return;
  }

  File f = FFat.open(BG_SPIFFS_PATH, FILE_WRITE);
  if (!f) {
    Serial.println("[BG] 无法写入 FFat");
    free(dlBuf);
    return;
  }
  f.write(dlBuf, written);
  f.close();
  free(dlBuf);

  // 持久化哈希到 EEPROM
  for (int i = 0; i < 8; i++) {
    EEPROM.write(EEPROM_BGHASH_ADDR + i,
                 (i < (int)strlen(newHash)) ? (uint8_t)newHash[i] : 0);
  }
  EEPROM.commit();
  strlcpy(_savedBgHash, newHash, sizeof(_savedBgHash));

  _bgLoaded = true;
  app.forceRedraw = true;
  Serial.printf("[BG] 下载完成 %d bytes\n", written);
}
