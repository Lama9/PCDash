// ntp.ino — NTP 时间同步（基于 TimeLib + WiFiUdp）

#include <WiFiUdp.h>
#include <TimeLib.h>

static WiFiUDP ntpUdp;
static const int NTP_PKT_SIZE = 48;
static byte ntpBuf[NTP_PKT_SIZE];

static void ntp_sendPacket(IPAddress &addr) {
  memset(ntpBuf, 0, NTP_PKT_SIZE);
  ntpBuf[0]  = 0b11100011;
  ntpBuf[1]  = 0;
  ntpBuf[2]  = 6;
  ntpBuf[3]  = 0xEC;
  ntpBuf[12] = 49;
  ntpBuf[13] = 0x4E;
  ntpBuf[14] = 49;
  ntpBuf[15] = 52;
  ntpUdp.beginPacket(addr, 123);
  ntpUdp.write(ntpBuf, NTP_PKT_SIZE);
  ntpUdp.endPacket();
}

static time_t ntp_getTime() {
  IPAddress serverIP;
  while (ntpUdp.parsePacket() > 0);
  WiFi.hostByName(NTP_SERVER, serverIP);
  ntp_sendPacket(serverIP);

  uint32_t t0 = millis();
  while (millis() - t0 < 1500) {
    if (ntpUdp.parsePacket() >= NTP_PKT_SIZE) {
      ntpUdp.read(ntpBuf, NTP_PKT_SIZE);
      unsigned long secs = (unsigned long)ntpBuf[40] << 24
                         | (unsigned long)ntpBuf[41] << 16
                         | (unsigned long)ntpBuf[42] << 8
                         |  ntpBuf[43];
      time_t t = secs - 2208988800UL + NTP_TIMEZONE * SECS_PER_HOUR;
      Serial.printf("[NTP] 同步成功: %02d:%02d:%02d\n", hour(t), minute(t), second(t));
      return t;
    }
  }
  Serial.println("[NTP] 同步失败");
  return 0;
}

void ntp_init() {
  ntpUdp.begin(NTP_LOCAL_PORT);
  setSyncProvider(ntp_getTime);
  setSyncInterval(NTP_SYNC_INTERVAL);
  Serial.printf("[NTP] 已启动，服务器: %s\n", NTP_SERVER);
}

bool ntp_ready() {
  return timeStatus() != timeNotSet;
}
