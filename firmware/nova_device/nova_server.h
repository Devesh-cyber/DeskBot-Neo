// Tiny HTTP server on port 80 so the laptop can push Nova's state to the display.
//   POST /display    {"state": "...", "text": "...", "ttl_ms": N, "step": N, "steps": N}   (token)
//   POST /heartbeat  (no body)                                                              (token)
//   GET  /health     -> {"ok":true,"uptime_s":N,"state":"idle","fw":"nova-0.1"}             (open)
// "token" = header  Authorization: Bearer <DEVICE_TOKEN>
#pragma once
#include <WebServer.h>
#include <ArduinoJson.h>
#include "secrets.h"
#include "nova_state.h"

static_assert(sizeof(DEVICE_TOKEN) >= 17,
              "DEVICE_TOKEN in secrets.h must be at least 16 characters (same text as NOVA_TOKEN in .env)");

#define NOVA_FW_VERSION "nova-0.1"
#define MAX_BODY_BYTES  600

static WebServer httpServer(80);

static void sendJson(int code, const char *json) {
  httpServer.send(code, "application/json", json);
}

// Constant-time comparison, so response timing does not leak how much of a guess was right.
static bool tokenOk() {
  if (!httpServer.hasHeader("Authorization")) return false;
  String h = httpServer.header("Authorization");
  if (!h.startsWith("Bearer ")) return false;
  const char *got = h.c_str() + 7;
  const char *want = DEVICE_TOKEN;
  size_t gl = strlen(got), wl = strlen(want);
  uint8_t diff = (gl != wl) ? 1 : 0;
  for (size_t i = 0; i < wl; i++) diff |= (uint8_t)(got[i < gl ? i : 0] ^ want[i]);
  return diff == 0;
}

static void handleDisplay() {
  if (!tokenOk()) { sendJson(401, "{\"error\":\"unauthorized\",\"message\":\"missing or wrong token\"}"); return; }
  if (!httpServer.hasArg("plain")) { sendJson(400, "{\"error\":\"bad_request\",\"message\":\"JSON body required\"}"); return; }
  const String &body = httpServer.arg("plain");
  if (body.length() > MAX_BODY_BYTES) { sendJson(413, "{\"error\":\"too_large\",\"message\":\"body too big\"}"); return; }

  JsonDocument doc;
  if (deserializeJson(doc, body)) { sendJson(400, "{\"error\":\"invalid_json\",\"message\":\"body is not valid JSON\"}"); return; }

  NovaState s;
  const char *stateName = doc["state"] | "";
  if (!parseNovaState(stateName, s)) {
    sendJson(400, "{\"error\":\"unknown_state\",\"message\":\"state must be idle, listening, thinking, speaking, working, saved, error or disconnected\"}");
    return;
  }

  char text[NOVA_TEXT_MAX + 1];
  sanitizeText(doc["text"] | "", text, sizeof(text));
  int ttlRaw = doc["ttl_ms"] | 0;
  uint32_t ttl = ttlRaw > 0 ? (uint32_t)ttlRaw : 0;
  int step = doc["step"] | 0;
  int steps = doc["steps"] | 0;
  if (steps < 0 || steps > 99 || step < 0 || step > steps) { step = 0; steps = 0; }

  novaApplyPush(s, text, ttl, step, steps);
  Serial.printf("push: state=%s text='%s' ttl=%u step=%d/%d\n", novaStateName(s), text, (unsigned)ttl, step, steps);

  char out[64];
  snprintf(out, sizeof(out), "{\"ok\":true,\"state\":\"%s\"}", novaStateName(s));
  sendJson(200, out);
}

static void handleHeartbeat() {
  if (!tokenOk()) { sendJson(401, "{\"error\":\"unauthorized\",\"message\":\"missing or wrong token\"}"); return; }
  novaMarkContact();
  sendJson(200, "{\"ok\":true}");
}

static void handleHealth() {
  NovaView v;
  novaGetView((nms)millis(), v);
  char out[96];
  snprintf(out, sizeof(out), "{\"ok\":true,\"uptime_s\":%lu,\"state\":\"%s\",\"fw\":\"%s\"}",
           (unsigned long)(millis() / 1000UL), novaStateName(v.state), NOVA_FW_VERSION);
  sendJson(200, out);
}

static void handleNotFound() {
  sendJson(404, "{\"error\":\"not_found\",\"message\":\"unknown path\"}");
}

static void httpTask(void *param) {
  while (WiFi.status() != WL_CONNECTED) vTaskDelay(pdMS_TO_TICKS(500));   // wait for Wi-Fi first

  static const char *headerKeys[] = {"Authorization"};
  httpServer.collectHeaders(headerKeys, 1);
  httpServer.on("/display", HTTP_POST, handleDisplay);
  httpServer.on("/heartbeat", HTTP_POST, handleHeartbeat);
  httpServer.on("/health", HTTP_GET, handleHealth);
  httpServer.onNotFound(handleNotFound);
  httpServer.begin();
  Serial.println("HTTP server ready on port 80");

  for (;;) {
    httpServer.handleClient();
    vTaskDelay(pdMS_TO_TICKS(2));
  }
}

static void startHttpServer() {
  xTaskCreatePinnedToCore(httpTask, "http", 10240, NULL, 1, NULL, 0);
}
