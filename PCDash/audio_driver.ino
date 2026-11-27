// audio_driver.ino — 多硬件模式声效驱动 (蜂鸣器 / I2S MAX98357A / CX-SP5T 串口 MP3)

#include "config.h"
#include "data_model.h"
#include <ESP_I2S.h>
#include <math.h>

extern AppState app;
extern void settings_saveHardwareMode(uint8_t mode);
extern void settings_saveAudioVolume(uint8_t vol);
extern void cc_led_setup(void);

static I2SClass s_i2s;
static bool     s_i2s_running = false;
static bool     s_mp3_running = false;
static QueueHandle_t s_audio_queue = NULL;

// ── CX-SP5T 模块状态 ──────────────────────────────────────────
struct CXModuleStatus {
    bool online;
    uint8_t play_state;  // 00=停止, 01=播放, 02=暂停
    uint8_t disk;        // 00=USB, 01=SD/TF, 02=FLASH, FF=无设备
    uint8_t volume;      // 0~30
    unsigned long last_rx;
};
static CXModuleStatus s_cx_status = {false, 0, 0xFF, 25, 0};

// ── CX-SP5T 协议指令帧发送: FE [LEN] FF FF [CMD] [DATA...] [SUM] BE ─
static void _send_cx_cmd(uint8_t cmd, const uint8_t* data, uint8_t data_len) {
    if (!s_mp3_running) return;
    uint8_t frame_len = 7 + data_len;
    uint8_t buf[24];
    buf[0] = 0xFE;
    buf[1] = frame_len;
    buf[2] = 0xFF;  // 广播地址高字节
    buf[3] = 0xFF;  // 广播地址低字节
    buf[4] = cmd;
    for (uint8_t i = 0; i < data_len; i++) {
        buf[5 + i] = data[i];
    }
    uint16_t sum = 0;
    for (uint8_t i = 0; i < 5 + data_len; i++) {
        sum += buf[i];
    }
    buf[5 + data_len] = (uint8_t)(sum & 0xFF);
    buf[6 + data_len] = 0xBE;
    Serial2.write(buf, frame_len);
    Serial2.flush();
}

// 设置播放模式为单曲播放停止 (模式 6, 循环 1 次)，杜绝出厂默认循环播放
static void _cx_set_single_stop_mode() {
    uint8_t d[3] = {0x06, 0x00, 0x01};
    _send_cx_cmd(0x18, d, 3);
}

// 设置音量 (0~30 级)
static void _cx_set_volume(uint8_t vol) {
    if (vol > 30) vol = 30;
    uint8_t d[1] = {vol};
    _send_cx_cmd(0x13, d, 1);
}

// 指定曲目播放 (0x07 指令)
static void _cx_play_track(uint16_t track) {
    uint8_t d[2] = {(uint8_t)(track >> 8), (uint8_t)(track & 0xFF)};
    _send_cx_cmd(0x07, d, 2);
}

// 停止播放 (0x04 指令)
static void _cx_stop() {
    _send_cx_cmd(0x04, NULL, 0);
}

// 查询播放状态 (0x01 指令)
static void _cx_query_status() {
    _send_cx_cmd(0x01, NULL, 0);
}

// 查询在线盘符 (0x09 指令)
static void _cx_query_disk() {
    _send_cx_cmd(0x09, NULL, 0);
}

// ── CX-SP5T 双向串口接收解析 ─────────────────────────────────
static void _cx_poll_rx() {
    if (!s_mp3_running) return;
    static uint8_t rx_buf[32];
    static uint8_t rx_idx = 0;
    static unsigned long last_byte_time = 0;

    while (Serial2.available() > 0) {
        uint8_t b = Serial2.read();
        unsigned long now = millis();
        if (now - last_byte_time > 80) rx_idx = 0;  // 字节超时复位
        last_byte_time = now;

        if (rx_idx == 0) {
            if (b == 0xFE) rx_buf[rx_idx++] = b;
        } else {
            rx_buf[rx_idx++] = b;
            if (rx_idx >= sizeof(rx_buf)) rx_idx = 0;

            if (rx_idx >= 7 && rx_buf[rx_idx - 1] == 0xBE) {
                uint8_t frame_len = rx_buf[1];
                if (rx_idx == frame_len) {
                    uint16_t sum = 0;
                    for (uint8_t i = 0; i < frame_len - 2; i++) {
                        sum += rx_buf[i];
                    }
                    if ((uint8_t)(sum & 0xFF) == rx_buf[frame_len - 2]) {
                        s_cx_status.online = true;
                        s_cx_status.last_rx = now;
                        uint8_t cmd = rx_buf[4];
                        if (cmd == 0x01 && frame_len >= 8) {
                            s_cx_status.play_state = rx_buf[5];  // 00=停止, 01=播放, 02=暂停
                        } else if ((cmd == 0x09 || cmd == 0x0A) && frame_len >= 8) {
                            s_cx_status.disk = rx_buf[5];        // 01=SD卡
                        } else if (cmd == 0x16 && frame_len >= 8) {
                            s_cx_status.volume = rx_buf[5];
                        }
                    }
                }
                rx_idx = 0;
            }
        }
    }

    // 超过 10 秒未收到模块任何回传视为离线
    if (s_cx_status.online && (millis() - s_cx_status.last_rx > 10000)) {
        s_cx_status.online = false;
    }
}

// ── I2S 正弦波合成输出 ────────────────────────────────────────
static void _play_sine_i2s(int freq, int duration_ms, int amplitude = 4000) {
    if (!s_i2s_running || amplitude <= 0) return;
    int total_samples = (16000 * duration_ms) / 1000;
    int16_t chunk[128];
    float phase_inc = 2.0f * (float)M_PI * (float)freq / 16000.0f;
    float phase = 0.0f;
    int samples_left = total_samples;

    while (samples_left > 0) {
        int n = samples_left > 128 ? 128 : samples_left;
        for (int i = 0; i < n; i++) {
            // 平滑渐入渐出，消除爆音 (Pop Click)
            float env = 1.0f;
            int cur_idx = total_samples - samples_left + i;
            if (cur_idx < 160) {
                env = (float)cur_idx / 160.0f;
            } else if (cur_idx > total_samples - 160) {
                env = (float)(total_samples - cur_idx) / 160.0f;
            }
            chunk[i] = (int16_t)(sinf(phase) * (float)amplitude * env);
            phase += phase_inc;
            if (phase >= 2.0f * (float)M_PI) phase -= 2.0f * (float)M_PI;
        }
        s_i2s.write((uint8_t*)chunk, n * sizeof(int16_t));
        samples_left -= n;
    }
}

// ── 音频后台异步播放任务 ──────────────────────────────────────
static void audio_task(void* pvParameters) {
    AudioNotifyType type;
    unsigned long last_query = 0;

    for (;;) {
        // 100ms 超时等待，兼顾 RX 状态轮询
        if (xQueueReceive(s_audio_queue, &type, pdMS_TO_TICKS(100)) == pdTRUE) {
            if (app.audioEnabled && app.hardwareMode != HW_MODE_LEGACY && type != NOTIFY_NONE) {
                int i2s_amp = (4000 * (int)app.audioVolume) / 30;

                if (app.hardwareMode == HW_MODE_BUZZER) {
                    // 模式 2: 无源蜂鸣器 (PWM)
                    if (type == NOTIFY_SUCCESS) {
                        tone(PIN_BUZZER_PWM, 2000, 100);
                        vTaskDelay(pdMS_TO_TICKS(130));
                        tone(PIN_BUZZER_PWM, 2600, 160);
                    } else if (type == NOTIFY_BUSY) {
                        tone(PIN_BUZZER_PWM, 1500, 90);
                    } else if (type == NOTIFY_ALARM) {
                        // 告警急促双嘟循环 3 次
                        for (int c = 0; c < 3; c++) {
                            tone(PIN_BUZZER_PWM, 1000, 100);
                            vTaskDelay(pdMS_TO_TICKS(150));
                            tone(PIN_BUZZER_PWM, 1000, 100);
                            if (c < 2) vTaskDelay(pdMS_TO_TICKS(220));
                        }
                    }
                } else if (app.hardwareMode == HW_MODE_I2S_LEGACY || app.hardwareMode == HW_MODE_I2S_WS2812) {
                    // 模式 3 / 4: MAX98357A I2S 功放
                    if (type == NOTIFY_SUCCESS) {
                        _play_sine_i2s(1500, 100, i2s_amp);
                        vTaskDelay(pdMS_TO_TICKS(40));
                        _play_sine_i2s(2200, 160, i2s_amp);
                    } else if (type == NOTIFY_BUSY) {
                        _play_sine_i2s(1300, 80, i2s_amp);
                    } else if (type == NOTIFY_ALARM) {
                        // 告警急促双音循环 3 轮 (与 00003.mp3 节奏一致)
                        for (int c = 0; c < 3; c++) {
                            _play_sine_i2s(800, 120, i2s_amp);
                            vTaskDelay(pdMS_TO_TICKS(80));
                            _play_sine_i2s(800, 120, i2s_amp);
                            if (c < 2) vTaskDelay(pdMS_TO_TICKS(220));
                        }
                    }
                } else if (app.hardwareMode == HW_MODE_UART_MP3) {
                    // 模式 5: CX-SP5T 模块 (00001=成功, 00002=进行中, 00003=告警3轮)
                    if (type == NOTIFY_SUCCESS) {
                        _cx_play_track(1);
                    } else if (type == NOTIFY_BUSY) {
                        _cx_play_track(2);
                    } else if (type == NOTIFY_ALARM) {
                        _cx_play_track(3);
                    }
                }
            }
        }

        // 轮询双向 RX 回传
        if (s_mp3_running) {
            _cx_poll_rx();
            unsigned long now = millis();
            // 每 3 秒定期查询一次模块状态
            if (now - last_query >= 3000) {
                last_query = now;
                _cx_query_status();
                _cx_query_disk();
            }
        }
    }
}

// ── 初始化外设驱动 ────────────────────────────────────────────
void audio_driver_init() {
    if (s_audio_queue == NULL) {
        s_audio_queue = xQueueCreate(8, sizeof(AudioNotifyType));
        xTaskCreatePinnedToCore(audio_task, "audio_task", 3072, NULL, 1, NULL, 0);
    }

    uint8_t mode = app.hardwareMode;
    Serial.printf("[Audio] 初始化硬件模式: %d (音量: %d/30)\n", mode, app.audioVolume);

    if (mode == HW_MODE_BUZZER) {
        pinMode(PIN_BUZZER_PWM, OUTPUT);
        digitalWrite(PIN_BUZZER_PWM, LOW);
    } else if (mode == HW_MODE_I2S_LEGACY) {
        s_i2s.setPins(PIN_I2S_BCLK, PIN_I2S_LRC, PIN_I2S_DIN_M3);
        s_i2s_running = s_i2s.begin(I2S_MODE_STD, 16000, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
        Serial.printf("[Audio] I2S (M3 借用CAM_PCLK=13) 状态: %s\n", s_i2s_running ? "成功" : "失败");
    } else if (mode == HW_MODE_I2S_WS2812) {
        s_i2s.setPins(PIN_I2S_BCLK, PIN_I2S_LRC, PIN_I2S_DIN_M4);
        s_i2s_running = s_i2s.begin(I2S_MODE_STD, 16000, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
        Serial.printf("[Audio] I2S (M4 DIN=39, CAM完好) 状态: %s\n", s_i2s_running ? "成功" : "失败");
    } else if (mode == HW_MODE_UART_MP3) {
        Serial2.begin(9600, SERIAL_8N1, PIN_MP3_RX, PIN_MP3_TX);
        s_mp3_running = true;
        delay(60);
        _cx_set_single_stop_mode();      // 强制设为单曲播放停止模式
        _cx_set_volume(app.audioVolume); // 同步初始音量
        _cx_query_disk();
        Serial.println("[Audio] CX-SP5T 串口 MP3 模块 (TX=35, RX=36) 初始化就绪 (单曲停止模式)");
    }
}

// ── 运行时动态切换模式 ────────────────────────────────────────
void audio_driver_switchMode(uint8_t new_mode) {
    if (new_mode < 1 || new_mode > 5) return;
    if (new_mode == app.hardwareMode) {
        Serial.printf("[Audio] 模式已经是 %d，无需切换\n", new_mode);
        return;
    }

    Serial.printf("[Audio] 切换硬件模式: %d -> %d\n", app.hardwareMode, new_mode);

    // 1. 释放前一模式的外设
    if (s_i2s_running) {
        s_i2s.end();
        s_i2s_running = false;
    }
    if (app.hardwareMode == HW_MODE_BUZZER) {
        noTone(PIN_BUZZER_PWM);
        pinMode(PIN_BUZZER_PWM, INPUT);
    }
    if (s_mp3_running) {
        _cx_stop();
        Serial2.end();
        s_mp3_running = false;
        s_cx_status.online = false;
    }
    if (app.hardwareMode == HW_MODE_I2S_WS2812) {
        rgbLedWrite(PIN_WS2812_DIN, 0, 0, 0);
    }

    // 2. 更新状态并持久化
    app.hardwareMode = new_mode;
    settings_saveHardwareMode(new_mode);

    // 3. 重新初始化灯光引脚（模式4 WS2812 vs 模式1/2/3/5分立LED）
    cc_led_setup();

    // 4. 初始化新外设
    if (new_mode == HW_MODE_BUZZER) {
        pinMode(PIN_BUZZER_PWM, OUTPUT);
        digitalWrite(PIN_BUZZER_PWM, LOW);
    } else if (new_mode == HW_MODE_I2S_LEGACY) {
        s_i2s.setPins(PIN_I2S_BCLK, PIN_I2S_LRC, PIN_I2S_DIN_M3);
        s_i2s_running = s_i2s.begin(I2S_MODE_STD, 16000, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
    } else if (new_mode == HW_MODE_I2S_WS2812) {
        s_i2s.setPins(PIN_I2S_BCLK, PIN_I2S_LRC, PIN_I2S_DIN_M4);
        s_i2s_running = s_i2s.begin(I2S_MODE_STD, 16000, I2S_DATA_BIT_WIDTH_16BIT, I2S_SLOT_MODE_MONO);
    } else if (new_mode == HW_MODE_UART_MP3) {
        Serial2.begin(9600, SERIAL_8N1, PIN_MP3_RX, PIN_MP3_TX);
        s_mp3_running = true;
        delay(60);
        _cx_set_single_stop_mode();      // 强制单曲播放停止
        _cx_set_volume(app.audioVolume); // 同步音量
        _cx_query_disk();
    }

    // 切换成功提示音 (如果发声已启用)
    audio_driver_notify(NOTIFY_SUCCESS);
}

// ── 设置系统播报音量 (0~30) ──────────────────────────────────
void audio_driver_setVolume(uint8_t vol) {
    if (vol > 30) vol = 30;
    settings_saveAudioVolume(vol);
    if (s_mp3_running) {
        _cx_set_volume(vol);
    }
    Serial.printf("[Audio] 系统音量已设置: %d/30\n", vol);
    audio_driver_notify(NOTIFY_BUSY); // 播放试听提示音
}

// ── 获取 CX-SP5T 模块运行状态 ─────────────────────────────────
void audio_driver_getMP3Status(bool* online, uint8_t* play_state, uint8_t* disk, uint8_t* vol) {
    if (online)     *online     = s_cx_status.online;
    if (play_state) *play_state = s_cx_status.play_state;
    if (disk)       *disk       = s_cx_status.disk;
    if (vol)        *vol        = s_cx_status.volume;
}

// ── 手动点播测试曲目 ──────────────────────────────────────────
void audio_driver_playTrack(uint16_t track) {
    if (app.hardwareMode == HW_MODE_UART_MP3) {
        Serial.printf("[CMD] 手动点播 MP3 曲目 %d (%05d.mp3)\n", track, track);
        _cx_play_track(track);
    } else {
        Serial.printf("[CMD] 模式 %d 触发测试提示音: %d\n", app.hardwareMode, track);
        if (track == 1) audio_driver_notify(NOTIFY_SUCCESS);
        else if (track == 2) audio_driver_notify(NOTIFY_BUSY);
        else audio_driver_notify(NOTIFY_ALARM);
    }
}

// ── CX-SP5T 硬件连接与通信诊断 ────────────────────────────────
void audio_driver_testMP3() {
    if (app.hardwareMode != HW_MODE_UART_MP3) {
        Serial.println("[TEST] 当前非模式 5，临时初始化 Serial2 (TX:35, RX:36)...");
        Serial2.begin(9600, SERIAL_8N1, PIN_MP3_RX, PIN_MP3_TX);
    }
    // 清空接收缓冲区
    while (Serial2.available()) Serial2.read();

    Serial.println("\n[TEST] ══════ CX-SP5T 硬件连接与通信诊断 ══════");
    Serial.printf("[TEST] 当前引脚映射: ESP32 TX=GPIO%d (接模块RXD), ESP32 RX=GPIO%d (接模块TXD)\n", PIN_MP3_TX, PIN_MP3_RX);
    Serial.println("[TEST] 1. 尝试发送【查询状态】指令帧: FE 07 FF FF 01 04 BE");

    uint8_t q_cmd[] = {0xFE, 0x07, 0xFF, 0xFF, 0x01, 0x04, 0xBE};
    Serial2.write(q_cmd, 7);
    Serial2.flush();

    // 监听回包并打印原始十六进制数据
    unsigned long start = millis();
    int rx_count = 0;
    Serial.print("[TEST] 监听回包 (800ms): ");
    while (millis() - start < 800) {
        if (Serial2.available()) {
            uint8_t b = Serial2.read();
            Serial.printf("0x%02X ", b);
            rx_count++;
        }
        delay(5);
    }
    Serial.println();

    if (rx_count == 0) {
        Serial.println("[TEST] ❌ 未收到任何返回字节！排查建议：");
        Serial.println("       ① TXD/RXD 线序接反：请尝试对调 GPIO35 和 GPIO36 的连接线；");
        Serial.println("       ② 排针位置有误：P6 排针上 GPIO36 是第 12 脚，GPIO35 是第 13 脚（注意不是顶部的 TXD0/RXD0）；");
        Serial.println("       ③ Type-C 数据线未拔下：模块若连着电脑 USB 会被强制识别为 U 盘，串口被禁用；");
        Serial.println("       ④ 供电共地：模块 GND 必须与底板 GND 连通。");
    } else {
        Serial.printf("[TEST] ✅ 成功收到 %d 字节回包！硬件串口双向通信正常。\n", rx_count);
        s_cx_status.online = true;
        s_cx_status.last_rx = millis();
    }

    Serial.println("[TEST] 2. 发送【播放曲目 1】指令: FE 09 FF FF 07 00 01 0D BE");
    uint8_t p_cmd[] = {0xFE, 0x09, 0xFF, 0xFF, 0x07, 0x00, 0x01, 0x0D, 0xBE};
    Serial2.write(p_cmd, 9);
    Serial2.flush();
    Serial.println("[TEST] 请观察：模块上 State 播放指示灯是否点亮？喇叭是否发出声音？");
    Serial.println("[TEST] ══════════════════════════════════════════\n");
}

// ── 触发声音播报通知 ──────────────────────────────────────────
void audio_driver_notify(AudioNotifyType type) {
    if (!app.audioEnabled || app.hardwareMode == HW_MODE_LEGACY || type == NOTIFY_NONE) return;
    if (s_audio_queue != NULL) {
        xQueueSend(s_audio_queue, &type, 0);
    }
}

// ── 提供外部直接写入 I2S PCM 接口 (供未来服务端 TTS 音频流使用) ─
size_t audio_i2s_write(const uint8_t* data, size_t len) {
    if (s_i2s_running) {
        return s_i2s.write(data, len);
    }
    return 0;
}
