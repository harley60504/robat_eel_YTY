#include "wifi_init.h"
#include "config.h"
#include <WiFi.h>

void initWiFi(){

  WiFi.mode(WIFI_AP_STA);

  WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASS);
  Serial.print("AP IP: ");
  Serial.print("STA IP: ");
  WiFi.begin(WIFI_STA_SSID, WIFI_STA_PASS);
  Serial.print("Connecting STA");
  while (WiFi.status() != WL_CONNECTED)
  {
    Serial.print(".");
    delay(200);
  }
  Serial.println();
  Serial.print("STA IP: ");
  Serial.println(WiFi.localIP());
}
