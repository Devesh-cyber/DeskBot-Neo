// Drawing helpers for Nova's state faces (128x64 SSD1306). Pure drawing, no state.
#pragma once
#include <math.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// Adafruit_GFX has no thick lines: draw three slightly offset ones.
static void thickLine(Adafruit_SSD1306 &d, int x0, int y0, int x1, int y1) {
  d.drawLine(x0, y0, x1, y1, SSD1306_WHITE);
  d.drawLine(x0 + 1, y0, x1 + 1, y1, SSD1306_WHITE);
  d.drawLine(x0, y0 + 1, x1, y1 + 1, SSD1306_WHITE);
}

// "X" eye (error). s = half-size in pixels.
static void drawXEye(Adafruit_SSD1306 &d, int cx, int cy, int s) {
  thickLine(d, cx - s, cy - s, cx + s, cy + s);
  thickLine(d, cx - s, cy + s, cx + s, cy - s);
}

// "^" eye (saved / happy).
static void drawHappyEye(Adafruit_SSD1306 &d, int cx, int cy, int s) {
  thickLine(d, cx - s, cy + s / 2, cx, cy - s / 2);
  thickLine(d, cx, cy - s / 2, cx + s, cy + s / 2);
}

static void drawCheck(Adafruit_SSD1306 &d, int cx, int cy) {
  thickLine(d, cx - 10, cy, cx - 4, cy + 7);
  thickLine(d, cx - 4, cy + 7, cx + 10, cy - 7);
}

static void drawMouth(Adafruit_SSD1306 &d, int cx, int cy, int w, int h) {
  int r = h / 2;
  if (r > 6) r = 6;
  d.fillRoundRect(cx - w / 2, cy - h / 2, w, h, r, SSD1306_WHITE);
}

// Talking mouth: smooth pseudo-random opening, 4..14 px, quantised to even numbers.
static int mouthHeight(uint32_t tick) {
  float a = 0.5f * (1.0f + sinf(tick * 0.9f));
  float b = 0.6f + 0.4f * sinf(tick * 0.37f);
  int h = 4 + (int)(10.0f * a * b);
  return h & ~1;
}

// Listening: three dots, the active one is bigger. active 0..2 (3 = none).
static void drawListeningDots(Adafruit_SSD1306 &d, int active) {
  for (int i = 0; i < 3; i++) d.fillCircle(52 + i * 12, 58, i == active ? 3 : 1, SSD1306_WHITE);
}

// Thinking: three dots at the top right, the active one jumps up. active 0..2 (3 = none).
static void drawThinkingDots(Adafruit_SSD1306 &d, int active) {
  for (int i = 0; i < 3; i++) d.fillCircle(100 + i * 8, i == active ? 3 : 7, 2, SSD1306_WHITE);
}

// Working: with steps -> filled bar (step/steps); without -> a block sweeping left and right.
static void drawWorkBar(Adafruit_SSD1306 &d, int step, int steps, uint32_t tick) {
  d.drawRect(0, 58, 128, 6, SSD1306_WHITE);
  if (steps > 0) {
    d.fillRect(2, 60, (124 * step) / steps, 2, SSD1306_WHITE);
  } else {
    const int block = 28, span = 124 - block;
    int p = (int)((tick * 3) % (uint32_t)(2 * span));
    if (p > span) p = 2 * span - p;
    d.fillRect(2 + p, 60, block, 2, SSD1306_WHITE);
  }
}

static void drawNoLinkMark(Adafruit_SSD1306 &d) {      // small x in the top-right corner
  d.drawLine(120, 2, 126, 8, SSD1306_WHITE);
  d.drawLine(126, 2, 120, 8, SSD1306_WHITE);
}
