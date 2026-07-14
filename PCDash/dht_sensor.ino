// dht_sensor.ino — DHT11 室内温湿度采样（GPIO37，P8 跳帽接入）

#include <DHT11.h>
#include "config.h"
#include "data_model.h"

extern AppState app;

static DHT11 s_dht(DHT11_PIN);

void dht_sensor_setup(void) {
    app.roomValid     = false;
    app.roomTemp      = 0;
    app.roomHumidity  = 0;
    s_dht.setDelay(0);  // 禁用库内置 500ms 阻塞延迟（PCDash 外部已控制 10s 间隔）
    Serial.printf("[DHT11] 初始化，引脚 GPIO%d\n", DHT11_PIN);
}

void dht_sensor_poll(void) {
    static unsigned long s_last = 0;
    unsigned long now = millis();
    if (now - s_last < DHT11_INTERVAL_MS) return;
    s_last = now;

    int temp = 0, hum = 0;
    int err = -1;
    // 最多重试 3 次，应对偶发的时序干扰
    for (int retry = 0; retry < 3; retry++) {
        err = s_dht.readTemperatureHumidity(temp, hum);
        if (err == 0) break;
        delay(100);  // 短暂等待后重试
    }
    if (err == 0) {
        app.roomTemp     = temp - 2;  // DHT11 偏高修正
        app.roomHumidity = hum;
        app.roomValid    = true;
        // Serial.printf("[DHT11] 温度=%d°C 湿度=%d%%\n", temp, hum);
    } else {
        Serial.printf("[DHT11] 读取失败 err=%d（已重试3次）\n", err);
    }
}
