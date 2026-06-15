#include "globals.h"
#include <WiFi.h>

bool connectWiFi(uint32_t timeoutMs)
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  Serial.printf("Connecting to WiFi SSID '%s'", WIFI_SSID);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("WiFi connected, IP=%s, RSSI=%ddBm\n", WiFi.localIP().toString().c_str(), WiFi.RSSI());
    return true;
  }

  Serial.println("WiFi connect failed, turning radio off");
  WiFi.disconnect(true);
  WiFi.mode(WIFI_OFF);
  return false;
}

bool isWiFiConnected()
{
  return WiFi.status() == WL_CONNECTED;
}

String getWiFiIP()
{
  if (!isWiFiConnected()) return "";
  return WiFi.localIP().toString();
}
