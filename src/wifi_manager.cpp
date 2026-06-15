#include "globals.h"
#include <WiFi.h>

void loadWiFiCredentials()
{
  String ssid = g_prefs.getString("wifi_ssid", WIFI_SSID);
  String pass = g_prefs.getString("wifi_pass", WIFI_PASSWORD);
  strncpy(g_wifi_ssid, ssid.c_str(), sizeof(g_wifi_ssid) - 1);
  g_wifi_ssid[sizeof(g_wifi_ssid) - 1] = '\0';
  strncpy(g_wifi_pass, pass.c_str(), sizeof(g_wifi_pass) - 1);
  g_wifi_pass[sizeof(g_wifi_pass) - 1] = '\0';
}

bool connectWiFi(uint32_t timeoutMs)
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(g_wifi_ssid, g_wifi_pass);

  Serial.printf("Connecting to WiFi SSID '%s'", g_wifi_ssid);
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

bool reconnectWiFi(const char *ssid, const char *pass, uint32_t timeoutMs)
{
  WiFi.disconnect(true);
  delay(100);
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, pass);

  Serial.printf("Reconnecting to WiFi SSID '%s'", ssid);
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < timeoutMs) {
    delay(250);
    Serial.print(".");
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    g_wifi_connected = true;
    g_wifi_ip = WiFi.localIP().toString();
    Serial.printf("WiFi reconnected, IP=%s\n", g_wifi_ip.c_str());
    return true;
  }

  Serial.println("WiFi reconnect failed, turning radio off");
  g_wifi_connected = false;
  g_wifi_ip = "";
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
