// Wi-Fi + background polling of the desk bot server (to-dos and timer).
// Runs on core 0 so slow or failed HTTP requests never freeze the eyes (core 1).
#pragma once
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>       // install "ArduinoJson" (v7) from Library Manager
#include "secrets.h"
#include "nova_state.h"

#define POLL_MS          2000  // how often to ask the server
#define HTTP_TIMEOUT_MS  1500  // give up quickly if the laptop is asleep/off

// Everything the rest of the sketch needs to know about the server.
struct BotData {
  bool wifiOk   = false;       // connected to the router
  bool serverOk = false;       // last poll succeeded
  char task[48] = "";          // next pending todo ("" if none)
  int  pending  = 0;           // number of pending todos
  char timerStatus[12] = "idle";   // "idle" | "running" | "finished"
  int  timerRemaining = 0;     // seconds left (as of last poll)
  int  timerTotal     = 0;
  unsigned long lastOkAt = 0;  // millis() of last successful poll
};

static BotData botData;                                   // written by net task
static portMUX_TYPE botMux = portMUX_INITIALIZER_UNLOCKED;

// Safe copy for the main loop to read.
static void getBotData(BotData &out) {
  portENTER_CRITICAL(&botMux);
  out = botData;
  portEXIT_CRITICAL(&botMux);
}

static void publishBotData(const BotData &d) {
  portENTER_CRITICAL(&botMux);
  botData = d;
  portEXIT_CRITICAL(&botMux);
}

// One request. Only touches `out` if everything parsed fine.
static bool pollOnce(BotData &out) {
  HTTPClient http;
  http.setConnectTimeout(HTTP_TIMEOUT_MS);
  http.setTimeout(HTTP_TIMEOUT_MS);
  if (!http.begin(SERVER_URL)) {
    Serial.println("poll failed: http.begin() rejected the URL (check SERVER_URL)");
    return false;
  }

  int code = http.GET();
  if (code != 200) {
    Serial.printf("poll failed: %s -> HTTP %d (%s)\n",
                  SERVER_URL, code, HTTPClient::errorToString(code).c_str());
    http.end();
    return false;
  }
  String body = http.getString();
  http.end();

  JsonDocument doc;
  if (deserializeJson(doc, body)) {                    // true = parse error
    Serial.println("poll failed: server reply was not valid JSON");
    return false;
  }

  strlcpy(out.task, doc["task"] | "", sizeof(out.task));
  out.pending        = doc["pending"] | 0;
  strlcpy(out.timerStatus, doc["timer"]["status"] | "idle", sizeof(out.timerStatus));
  out.timerRemaining = doc["timer"]["remaining"] | 0;
  out.timerTotal     = doc["timer"]["total"] | 0;
  return true;
}

static void netTask(void *param) {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);                // keeps incoming display commands fast (no modem-sleep lag)
  WiFi.setAutoReconnect(true);
#if USE_STATIC_IP
  WiFi.config(IPAddress(STATIC_IP), IPAddress(GATEWAY_IP), IPAddress(SUBNET_MASK), IPAddress(DNS_IP));
#endif
  WiFi.begin(WIFI_SSID, WIFI_PASS);

  BotData local;
  unsigned long lastReconnect = millis();

  for (;;) {
    local.wifiOk = (WiFi.status() == WL_CONNECTED);

    if (local.wifiOk) {
      local.serverOk = pollOnce(local);
      if (local.serverOk) {
        local.lastOkAt = millis();
        novaMarkContact();             // the laptop answered: it is alive
      }
    } else {
      local.serverOk = false;
      if (millis() - lastReconnect > 10000) {              // nudge Wi-Fi every 10 s
        lastReconnect = millis();
        WiFi.reconnect();
      }
    }

    publishBotData(local);
    vTaskDelay(pdMS_TO_TICKS(POLL_MS));
  }
}

static void startNetTask() {
  xTaskCreatePinnedToCore(netTask, "net", 10240, NULL, 1, NULL, 0);
}
