// settings.ino — 配置持久化（EEPROM）

// EEPROM 存储布局
#define EEPROM_MAGIC_ADDR    0     // 有效标记 (0xAB)
#define EEPROM_BL_ADDR       1     // 背光亮度
#define EEPROM_THEME_ADDR    2     // 主题 (0=暗, 1=亮)
#define EEPROM_AUTOSCRL_ADDR 3     // 自动轮播 (0/1)
#define EEPROM_SCRLTIME_ADDR 4     // 自动轮播时间 (uint16_t，秒，地址 4-5)
#define EEPROM_HWMODE_ADDR   6     // 硬件模式 (1~5: 1=无声, 2=蜂鸣器, 3=I2S+3色, 4=I2S+单线, 5=MP3)
#define EEPROM_AUDIOEN_ADDR  7     // 全局发声开关 (0=关/静音, 1=开)
#define EEPROM_VOLUME_ADDR   8     // 提示音音量 (0~30，默认 25)
#define EEPROM_SSID_ADDR     10    // WiFi SSID (32 bytes)
#define EEPROM_PASS_ADDR     42    // WiFi 密码 (32 bytes)
#define EEPROM_PC1_IP_ADDR   80    // PC1 IP (16 bytes)
#define EEPROM_PC2_IP_ADDR   96    // PC2 IP
#define EEPROM_PC3_IP_ADDR   112   // PC3 IP
#define EEPROM_PCCOUNT_ADDR  128   // PC 数量
#define EEPROM_PORTS_ADDR    129   // 每槽位 HTTP 端口 (MAX_PC_COUNT * 2 bytes)

#define EEPROM_MAGIC_VALUE   0xAB

// WiFi 凭据（从 EEPROM 加载）
char wifi_ssid[33] = "";
char wifi_pass[33] = "";

// PC IP 列表（WiFi 模式下轮询用）
char pc_ips[MAX_PC_COUNT][16] = { "", "", "" };
byte pc_ip_count = 0;
uint16_t pc_ports[MAX_PC_COUNT] = { DEFAULT_HTTP_PORT, DEFAULT_HTTP_PORT, DEFAULT_HTTP_PORT };

void settings_load() {
  // 检查 EEPROM 有效标记
  if (EEPROM.read(EEPROM_MAGIC_ADDR) != EEPROM_MAGIC_VALUE) {
    Serial.println("[Settings] EEPROM 无有效数据，使用默认配置");
    app.darkTheme = true;
    app.autoScroll = false;
    app.autoScrollSec = 3;
    app.hardwareMode = HW_MODE_LEGACY;
    app.audioEnabled = true;
    app.audioVolume = 25;
    return;
  }

  app.darkTheme   = (EEPROM.read(EEPROM_THEME_ADDR) == 0);
  app.autoScroll  = (EEPROM.read(EEPROM_AUTOSCRL_ADDR) == 1);
  uint16_t scrlSec = EEPROM.read(EEPROM_SCRLTIME_ADDR) | (EEPROM.read(EEPROM_SCRLTIME_ADDR + 1) << 8);
  app.autoScrollSec = (scrlSec == 0 || scrlSec == 0xFFFF) ? 3 : scrlSec;

  uint8_t hwMode = EEPROM.read(EEPROM_HWMODE_ADDR);
  if (hwMode < 1 || hwMode > 5) hwMode = HW_MODE_LEGACY;
  app.hardwareMode = hwMode;

  uint8_t audioEn = EEPROM.read(EEPROM_AUDIOEN_ADDR);
  // 0xFF (未初始化) 或 1 均默认为开，仅显式写入 0 为静音
  app.audioEnabled = (audioEn != 0);

  uint8_t vol = EEPROM.read(EEPROM_VOLUME_ADDR);
  app.audioVolume = (vol > 30) ? 25 : vol;

  // 读取 WiFi 凭据
  for (int i = 0; i < 32; i++) {
    wifi_ssid[i] = EEPROM.read(EEPROM_SSID_ADDR + i);
    wifi_pass[i] = EEPROM.read(EEPROM_PASS_ADDR + i);
  }
  wifi_ssid[32] = '\0';
  wifi_pass[32] = '\0';

  // 读取 PC IP 列表
  pc_ip_count = constrain(EEPROM.read(EEPROM_PCCOUNT_ADDR), 0, MAX_PC_COUNT);
  for (int p = 0; p < MAX_PC_COUNT; p++) {
    int base = EEPROM_PC1_IP_ADDR + p * 16;
    for (int i = 0; i < 15; i++) {
      pc_ips[p][i] = EEPROM.read(base + i);
    }
    pc_ips[p][15] = '\0';
    // IP 必须以数字开头（如 192.x / 10.x），否则是垃圾数据
    if (pc_ips[p][0] < '0' || pc_ips[p][0] > '9') {
      memset(pc_ips[p], 0, 16);
    }
  }
  // 重算有效槽位数（最后一个非空槽的下标+1）
  pc_ip_count = 0;
  for (int p = 0; p < MAX_PC_COUNT; p++) {
    if (strlen(pc_ips[p]) > 0) pc_ip_count = p + 1;
  }

  // 每槽位 HTTP 端口
  for (int p = 0; p < MAX_PC_COUNT; p++) {
    int addr = EEPROM_PORTS_ADDR + p * 2;
    uint16_t port = EEPROM.read(addr) | (EEPROM.read(addr + 1) << 8);
    pc_ports[p] = (port == 0 || port == 0xFFFF) ? DEFAULT_HTTP_PORT : port;
  }

  Serial.printf("[Settings] 已加载: 主题=%s 轮播=%s (间隔%u秒) 模式=%d 发声=%s 音量=%d\n",
                app.darkTheme ? "暗" : "亮",
                app.autoScroll ? "开" : "关",
                app.autoScrollSec,
                app.hardwareMode,
                app.audioEnabled ? "开" : "静音",
                app.audioVolume);
  Serial.printf("[Settings] WiFi SSID: [%s]\n", wifi_ssid);
  Serial.printf("[Settings] WiFi PASS: [%s]\n", strlen(wifi_pass) > 0 ? "****" : "");
  Serial.printf("[Settings] PC IP 数量: %d\n", pc_ip_count);
  for (int i = 0; i < MAX_PC_COUNT; i++) {
    Serial.printf("[Settings] PC%d: IP=[%s] 端口=%d%s\n",
                  i, pc_ips[i], pc_ports[i],
                  (i < pc_ip_count) ? "" : " (未配置)");
  }
}

void settings_save() {
  EEPROM.write(EEPROM_MAGIC_ADDR, EEPROM_MAGIC_VALUE);
  EEPROM.write(EEPROM_THEME_ADDR, app.darkTheme ? 0 : 1);
  EEPROM.write(EEPROM_AUTOSCRL_ADDR, app.autoScroll ? 1 : 0);
  EEPROM.write(EEPROM_SCRLTIME_ADDR,     app.autoScrollSec & 0xFF);
  EEPROM.write(EEPROM_SCRLTIME_ADDR + 1, (app.autoScrollSec >> 8) & 0xFF);
  EEPROM.write(EEPROM_HWMODE_ADDR,       app.hardwareMode);
  EEPROM.write(EEPROM_AUDIOEN_ADDR,      app.audioEnabled ? 1 : 0);
  EEPROM.write(EEPROM_VOLUME_ADDR,       app.audioVolume);
  EEPROM.commit();
  yield();
  Serial.println("[Settings] 配置已保存");
}

void settings_saveHardwareMode(uint8_t mode) {
  if (mode < 1 || mode > 5) return;
  app.hardwareMode = mode;
  EEPROM.write(EEPROM_MAGIC_ADDR,  EEPROM_MAGIC_VALUE);
  EEPROM.write(EEPROM_HWMODE_ADDR, mode);
  EEPROM.commit();
  yield();
  Serial.printf("[Settings] 硬件模式已保存: %d\n", mode);
}

void settings_saveAudioEnabled(bool en) {
  app.audioEnabled = en;
  EEPROM.write(EEPROM_MAGIC_ADDR,   EEPROM_MAGIC_VALUE);
  EEPROM.write(EEPROM_AUDIOEN_ADDR, en ? 1 : 0);
  EEPROM.commit();
  yield();
  Serial.printf("[Settings] 全局发声开关已保存: %s\n", en ? "开" : "静音");
}

void settings_saveAudioVolume(uint8_t vol) {
  if (vol > 30) vol = 30;
  app.audioVolume = vol;
  EEPROM.write(EEPROM_MAGIC_ADDR,   EEPROM_MAGIC_VALUE);
  EEPROM.write(EEPROM_VOLUME_ADDR,  vol);
  EEPROM.commit();
  yield();
  Serial.printf("[Settings] 音量已保存: %d/30\n", vol);
}

void settings_saveWiFi(const char* ssid, const char* pass) {
  for (int i = 0; i < 32; i++) {
    EEPROM.write(EEPROM_SSID_ADDR + i, (i < strlen(ssid)) ? ssid[i] : 0);
    EEPROM.write(EEPROM_PASS_ADDR + i, (i < strlen(pass)) ? pass[i] : 0);
  }
  // 首次写入时初始化所有字段，防止未初始化区域残留垃圾值
  if (EEPROM.read(EEPROM_MAGIC_ADDR) != EEPROM_MAGIC_VALUE) {
    EEPROM.write(EEPROM_PCCOUNT_ADDR, 0);
    for (int p = 0; p < MAX_PC_COUNT; p++) {
      int base = EEPROM_PC1_IP_ADDR + p * 16;
      for (int i = 0; i < 16; i++) EEPROM.write(base + i, 0);
    }
    for (int p = 0; p < MAX_PC_COUNT; p++) {
      int addr = EEPROM_PORTS_ADDR + p * 2;
      EEPROM.write(addr,     DEFAULT_HTTP_PORT & 0xFF);
      EEPROM.write(addr + 1, (DEFAULT_HTTP_PORT >> 8) & 0xFF);
    }
  }
  EEPROM.write(EEPROM_MAGIC_ADDR, EEPROM_MAGIC_VALUE);
  EEPROM.commit();
  yield();

  strlcpy(wifi_ssid, ssid, sizeof(wifi_ssid));
  strlcpy(wifi_pass, pass, sizeof(wifi_pass));
  Serial.printf("[Settings] WiFi 已保存: %s\n", ssid);
  Serial.flush();
}

void settings_savePC_IP(byte index, const char* ip) {
  if (index >= MAX_PC_COUNT) return;
  int base = EEPROM_PC1_IP_ADDR + index * 16;
  for (int i = 0; i < 15; i++) {
    EEPROM.write(base + i, (i < strlen(ip)) ? ip[i] : 0);
  }
  strlcpy(pc_ips[index], ip, 16);
  // 写 IP 时同步将端口重置为当前默认值，清除 DELPC 移位可能遗留的旧端口
  pc_ports[index] = DEFAULT_HTTP_PORT;
  int portAddr = EEPROM_PORTS_ADDR + index * 2;
  EEPROM.write(portAddr,     DEFAULT_HTTP_PORT & 0xFF);
  EEPROM.write(portAddr + 1, (DEFAULT_HTTP_PORT >> 8) & 0xFF);
  if (index >= pc_ip_count) {
    pc_ip_count = index + 1;
    EEPROM.write(EEPROM_PCCOUNT_ADDR, pc_ip_count);
  }
  EEPROM.commit();
  yield();
}

void settings_savePort(byte index, uint16_t port) {
  if (index >= MAX_PC_COUNT) return;
  int addr = EEPROM_PORTS_ADDR + index * 2;
  EEPROM.write(addr,     port & 0xFF);
  EEPROM.write(addr + 1, (port >> 8) & 0xFF);
  pc_ports[index] = port;
  EEPROM.commit();
  yield();
}

// 删除指定槽位，后续槽位前移，同步更新 IP / PORT / EEPROM
void settings_deletePC(byte index) {
  if (index >= pc_ip_count) return;

  // 内存前移
  for (byte i = index; i < MAX_PC_COUNT - 1; i++) {
    strlcpy(pc_ips[i], pc_ips[i + 1], 16);
    pc_ports[i] = pc_ports[i + 1];
  }
  // 清空末位槽
  memset(pc_ips[MAX_PC_COUNT - 1], 0, 16);
  pc_ports[MAX_PC_COUNT - 1] = DEFAULT_HTTP_PORT;
  if (pc_ip_count > 0) pc_ip_count--;

  // 全量写回 EEPROM
  for (byte p = 0; p < MAX_PC_COUNT; p++) {
    int base = EEPROM_PC1_IP_ADDR + p * 16;
    for (int i = 0; i < 15; i++) {
      EEPROM.write(base + i, (i < (int)strlen(pc_ips[p])) ? (uint8_t)pc_ips[p][i] : 0);
    }
    int addr = EEPROM_PORTS_ADDR + p * 2;
    EEPROM.write(addr,     pc_ports[p] & 0xFF);
    EEPROM.write(addr + 1, (pc_ports[p] >> 8) & 0xFF);
  }
  EEPROM.write(EEPROM_PCCOUNT_ADDR, pc_ip_count);
  EEPROM.commit();
  yield();
  Serial.printf("[Settings] 已删除槽位 %d，剩余 %d 台\n", index, pc_ip_count);
}
