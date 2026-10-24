// Shared state between the HTTP server task (core 0, writes) and the display loop (core 1, reads).
#pragma once
#include <Arduino.h>
#include "nova_logic.h"

struct NovaPush {
  NovaState state = S_IDLE;
  nms expiresAt = 0;
  char text[NOVA_TEXT_MAX + 1] = "";
  int step = 0, steps = 0;
  uint32_t seq = 0;                // bumps on every push so the screen redraws even for identical data
};

// What the display loop needs for one frame.
struct NovaView {
  NovaState state = S_IDLE;
  char text[NOVA_TEXT_MAX + 1] = "";
  int step = 0, steps = 0;
  uint32_t seq = 0;
  nms contactAgeMs = 0;
};

static NovaPush novaPush;
static nms novaLastContact = 0;
static portMUX_TYPE novaMux = portMUX_INITIALIZER_UNLOCKED;

// Call once in setup(): boot counts as "contact", so "disconnected" only appears after 15 s of silence.
static void novaInit(nms now) {
  portENTER_CRITICAL(&novaMux);
  novaLastContact = now;
  portEXIT_CRITICAL(&novaMux);
}

// Any sign of life from the laptop: a good poll, a heartbeat, or a display push.
static void novaMarkContact() {
  nms now = (nms)millis();
  portENTER_CRITICAL(&novaMux);
  novaLastContact = now;
  portEXIT_CRITICAL(&novaMux);
}

static void novaApplyPush(NovaState s, const char *text, uint32_t ttlMs, int step, int steps) {
  nms now = (nms)millis();
  portENTER_CRITICAL(&novaMux);
  novaPush.state = s;
  novaPush.expiresAt = now + clampTtlMs(s, ttlMs);
  strlcpy(novaPush.text, text ? text : "", sizeof(novaPush.text));
  novaPush.step = step;
  novaPush.steps = steps;
  novaPush.seq++;
  novaLastContact = now;
  portEXIT_CRITICAL(&novaMux);
}

static void novaGetView(nms now, NovaView &v) {
  portENTER_CRITICAL(&novaMux);
  v.state = resolveState(novaPush.state, novaPush.expiresAt, now, novaLastContact);
  v.contactAgeMs = (nms)(now - novaLastContact);
  v.seq = novaPush.seq;
  if (v.state == novaPush.state) {              // text/steps only belong to the pushed state
    strlcpy(v.text, novaPush.text, sizeof(v.text));
    v.step = novaPush.step;
    v.steps = novaPush.steps;
  } else {
    v.text[0] = 0;
    v.step = v.steps = 0;
  }
  portEXIT_CRITICAL(&novaMux);
}
