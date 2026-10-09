/*
  Desk Bot - Phase 1: eyes only (v2: smoother, frame-rate independent)
  Hardware: ESP32 Dev Module + 0.96" SSD1306 OLED (I2C, 128x64, addr 0x3C)
  Wiring:   OLED VCC->3V3, GND->GND, SDA->GPIO21, SCL->GPIO22
  Libraries: Adafruit SSD1306, Adafruit GFX

  Behaviors (no moods, no inputs):
    - Blinks (both eyes) at random intervals
    - Occasionally winks (one eye, random side)
    - Looks left / right / center with smooth, quick movement

  What's different from v1:
    - Motion is time-based, so it keeps the same speed even if the frame rate
      is low (Wokwi simulates the OLED slowly; real hardware is much faster)
    - A blink always shows at least one fully closed frame, even at low fps
    - The screen is only sent to the OLED when something actually changed
*/

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// ---------- Hardware config ----------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET   -1
#define OLED_ADDR    0x3C
#define SDA_PIN      21
#define SCL_PIN      22
#define I2C_SPEED    400000   // try 400000 if the screen glitches on real hardware

// ---------- Eye look (tweak these) ----------
#define EYE_W        32     // eye width
#define EYE_H        38     // eye height
#define EYE_R        12     // corner radius
#define LEFT_EYE_X   36     // left eye center
#define RIGHT_EYE_X  92     // right eye center
#define EYE_Y        32     // vertical center
#define LOOK_RANGE   12     // max pixels the eyes slide left/right

// ---------- Timing (tweak these) ----------
#define BLINK_MIN_MS    2000
#define BLINK_MAX_MS    6000
#define BLINK_HOLD_MS   90    // time a normal blink stays fully closed
#define WINK_HOLD_MS    350   // time a wink stays closed
#define WINK_CHANCE     30    // % of blink events that are winks
#define CLOSE_TAU_MS    30    // smaller = faster closing
#define OPEN_TAU_MS     45    // smaller = faster opening
#define LOOK_MIN_MS     1200
#define LOOK_MAX_MS     3000
#define LOOK_TAU_MS     70    // smaller = snappier eye movement

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ---------- State ----------
float lookX = 0, lookY = 0, lookTX = 0, lookTY = 0;
unsigned long nextLookAt = 0;

float blinkL = 0, blinkR = 0;              // 0 = open, 1 = closed
float targetL = 0, targetR = 0;
bool closeL = false, closeR = false;
enum BlinkPhase { B_WAIT, B_CLOSING, B_HOLD, B_OPENING };
BlinkPhase phase = B_WAIT;
unsigned long nextBlinkAt = 0;
unsigned long holdMs = BLINK_HOLD_MS;
unsigned long holdUntil = 0;

unsigned long lastUpdate = 0;

// last values actually drawn (to skip redundant OLED updates)
int lastDx = -999, lastCy = -999, lastHL = -999, lastHR = -999;

// ---------- Helpers ----------
// Frame-rate independent smoothing: same speed no matter how slow the loop is
float approachDt(float cur, float target, float tauMs, float dtMs) {
  float k = 1.0f - expf(-dtMs / tauMs);
  return cur + (target - cur) * k;
}

int eyeHeight(float blinkAmt) {
  return (int)roundf(fmaxf(3.0f, EYE_H * (1.0f - blinkAmt)));
}

// ---------- Drawing ----------
void drawEye(int cx, int cy, int hh) {
  int w = EYE_W;
  int r = min(EYE_R, min(w, hh) / 2);
  display.fillRoundRect(cx - w / 2, cy - hh / 2, w, hh, r, SSD1306_WHITE);
}

void render() {
  int dx = (int)roundf(lookX);
  int cy = EYE_Y + (int)roundf(lookY);
  int hL = eyeHeight(blinkL);
  int hR = eyeHeight(blinkR);

  // nothing visibly changed -> don't spend time sending the frame over I2C
  if (dx == lastDx && cy == lastCy && hL == lastHL && hR == lastHR) return;
  lastDx = dx; lastCy = cy; lastHL = hL; lastHR = hR;

  display.clearDisplay();
  drawEye(LEFT_EYE_X + dx,  cy, hL);
  drawEye(RIGHT_EYE_X + dx, cy, hR);
  display.display();
}

// ---------- Behaviors ----------
void updateBlink(unsigned long now, float dt) {
  switch (phase) {
    case B_WAIT:
      if (now >= nextBlinkAt) {
        if (random(100) < WINK_CHANCE) {            // wink: one eye, a bit longer
          bool left = (random(2) == 0);
          closeL = left; closeR = !left;
          holdMs = WINK_HOLD_MS;
        } else {                                    // normal blink: both eyes
          closeL = true; closeR = true;
          holdMs = BLINK_HOLD_MS;
        }
        targetL = closeL ? 1 : 0;
        targetR = closeR ? 1 : 0;
        phase = B_CLOSING;
      }
      break;

    case B_CLOSING:                                 // wait until visibly closed
      if ((!closeL || blinkL > 0.9f) && (!closeR || blinkR > 0.9f)) {
        holdUntil = now + holdMs;
        phase = B_HOLD;
      }
      break;

    case B_HOLD:
      if (now >= holdUntil) {
        targetL = 0; targetR = 0;
        phase = B_OPENING;
      }
      break;

    case B_OPENING:
      if (blinkL < 0.05f && blinkR < 0.05f) {
        phase = B_WAIT;
        nextBlinkAt = now + random(BLINK_MIN_MS, BLINK_MAX_MS);
      }
      break;
  }

  blinkL = approachDt(blinkL, targetL, targetL > blinkL ? CLOSE_TAU_MS : OPEN_TAU_MS, dt);
  blinkR = approachDt(blinkR, targetR, targetR > blinkR ? CLOSE_TAU_MS : OPEN_TAU_MS, dt);
}

void updateLook(unsigned long now, float dt) {
  if (now >= nextLookAt) {
    int pick = random(3);                           // 0 = left, 1 = right, 2 = center
    if (pick == 0)      lookTX = -LOOK_RANGE;
    else if (pick == 1) lookTX =  LOOK_RANGE;
    else                lookTX = 0;
    lookTY = random(-2, 3);                         // tiny vertical drift
    nextLookAt = now + random(LOOK_MIN_MS, LOOK_MAX_MS);
  }
  lookX = approachDt(lookX, lookTX, LOOK_TAU_MS, dt);
  lookY = approachDt(lookY, lookTY, LOOK_TAU_MS, dt);
}

// ---------- Arduino entry points ----------
void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED not found - check wiring (SDA=21, SCL=22, addr 0x3C)");
    for (;;) delay(1000);
  }
  Wire.setClock(I2C_SPEED);                         // faster I2C = smoother animation

  randomSeed(esp_random());
  display.clearDisplay();
  display.display();

  lastUpdate  = millis();
  nextBlinkAt = lastUpdate + 2000;
  nextLookAt  = lastUpdate + 1500;
}

void loop() {
  unsigned long now = millis();
  float dt = (float)(now - lastUpdate);
  if (dt < 8) { delay(1); return; }                 // avoid zero-length steps
  if (dt > 120) dt = 120;                           // never jump after a long stall
  lastUpdate = now;

  updateBlink(now, dt);
  updateLook(now, dt);
  render();
}