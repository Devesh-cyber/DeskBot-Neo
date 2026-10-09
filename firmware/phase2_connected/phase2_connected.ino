/*
  Desk Bot - Phase 2, Step 4: focus layout
    - Normal: big eyes, with a short "peek" of your next task every so often
    - Timer running: eyes shrink to the top, big countdown + progress bar below
  Files in this folder: phase2_connected.ino, net_task.h, timer_state.h, views.h,
                        secrets.h (you create it from secrets.example.h)
  Libraries: Adafruit SSD1306, Adafruit GFX, ArduinoJson (v7)
  Serial Monitor: 115200 baud
*/

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "net_task.h"
#include "timer_state.h"
#include "views.h"

// ---------- Hardware config ----------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET   -1
#define OLED_ADDR    0x3C
#define SDA_PIN      21
#define SCL_PIN      22
#define I2C_SPEED    800000   // try 400000 if the screen glitches

// ---------- Eye look: normal layout ----------
#define EYE_W        32
#define EYE_H        38
#define EYE_R        12
#define LEFT_EYE_X   36
#define RIGHT_EYE_X  92
#define EYE_Y        32
#define LOOK_RANGE   12

// ---------- Eye look: focus layout (timer running) ----------
#define FOCUS_EYE_W       22
#define FOCUS_EYE_H       24
#define FOCUS_EYE_R        8
#define FOCUS_LEFT_X      44
#define FOCUS_RIGHT_X     84
#define FOCUS_EYE_Y       15
#define LAYOUT_TAU_MS    110   // how fast eyes shrink/grow between layouts

// ---------- Timing ----------
#define BLINK_MIN_MS    2000
#define BLINK_MAX_MS    6000
#define BLINK_HOLD_MS   90
#define WINK_HOLD_MS    350
#define WINK_CHANCE     30
#define CLOSE_TAU_MS    30
#define OPEN_TAU_MS     45
#define LOOK_MIN_MS     1200
#define LOOK_MAX_MS     3000
#define LOOK_TAU_MS     70
#define STATUS_PRINT_MS 1000

// ---------- Peek view timing ----------
#define EYES_VIEW_MS   15000   // eyes this long between peeks (use 30000 for daily use)
#define FOCUS_PEEK_MS  60000   // while a timer runs, peek less often
#define PEEK_VIEW_MS    4000   // how long the task is shown

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// ---------- Eye state ----------
float lookX = 0, lookY = 0, lookTX = 0, lookTY = 0;
unsigned long nextLookAt = 0;

float blinkL = 0, blinkR = 0;
float targetL = 0, targetR = 0;
bool closeL = false, closeR = false;
enum BlinkPhase { B_WAIT, B_CLOSING, B_HOLD, B_OPENING };
BlinkPhase phase = B_WAIT;
unsigned long nextBlinkAt = 0;
unsigned long holdMs = BLINK_HOLD_MS;
unsigned long holdUntil = 0;

float layoutT = 0;                 // 0 = normal eyes, 1 = focus layout (animated)

unsigned long lastUpdate = 0;

// Everything that decides how a frame looks. If it didn't change, we skip the I2C transfer.
struct Frame { int lx, rx, cy, w, r, hL, hR, secs, bar; };
Frame lastFrame;
bool frameValid = false;

// ---------- Helpers ----------
float approachDt(float cur, float target, float tauMs, float dtMs) {
  float k = 1.0f - expf(-dtMs / tauMs);
  return cur + (target - cur) * k;
}

int lerpI(int a, int b, float t) { return (int)roundf(a + (b - a) * t); }

int eyeHeight(float blinkAmt, int baseH) {
  return (int)roundf(fmaxf(3.0f, baseH * (1.0f - blinkAmt)));
}

// ---------- Drawing: eyes + countdown ----------
void drawEye(int cx, int cy, int w, int hh, int r) {
  r = min(r, min(w, hh) / 2);
  display.fillRoundRect(cx - w / 2, cy - hh / 2, w, hh, r, SSD1306_WHITE);
}

void render(unsigned long now) {
  float t = layoutT;
  float lookScale = 1.0f - 0.5f * t;                 // eyes wander less in focus mode
  int dx = (int)roundf(lookX * lookScale);
  int baseH = lerpI(EYE_H, FOCUS_EYE_H, t);

  Frame f;
  f.w  = lerpI(EYE_W, FOCUS_EYE_W, t);
  f.r  = lerpI(EYE_R, FOCUS_EYE_R, t);
  f.cy = lerpI(EYE_Y, FOCUS_EYE_Y, t) + (int)roundf(lookY * lookScale);
  f.lx = lerpI(LEFT_EYE_X,  FOCUS_LEFT_X,  t) + dx;
  f.rx = lerpI(RIGHT_EYE_X, FOCUS_RIGHT_X, t) + dx;
  f.hL = eyeHeight(blinkL, baseH);
  f.hR = eyeHeight(blinkR, baseH);

  // countdown + progress bar only once the eyes have (almost) finished shrinking
  bool showTimer = (t > 0.85f && lt.state == T_RUNNING);
  long totalS = (long)(lt.totalMs / 1000UL);
  f.secs = showTimer ? timerRemainingSec(now) : -1;
  f.bar = 0;
  if (showTimer && totalS > 0) {
    long bar = (totalS - f.secs) * SCREEN_WIDTH / totalS;
    f.bar = (int)(bar < 0 ? 0 : (bar > SCREEN_WIDTH ? SCREEN_WIDTH : bar));
  }

  if (frameValid && memcmp(&f, &lastFrame, sizeof(Frame)) == 0) return;   // nothing changed
  lastFrame = f;
  frameValid = true;

  display.clearDisplay();
  drawEye(f.lx, f.cy, f.w, f.hL, f.r);
  drawEye(f.rx, f.cy, f.w, f.hR, f.r);

  if (f.secs >= 0) {
    char buf[12];
    int size = formatCountdown(f.secs, buf, sizeof(buf));
    display.setTextColor(SSD1306_WHITE);
    display.setTextSize(size);
    int tw = (int)strlen(buf) * 6 * size;
    display.setCursor((SCREEN_WIDTH - tw) / 2, size == 3 ? 34 : 38);
    display.print(buf);
    if (totalS > 0) {
      display.drawRect(0, 61, SCREEN_WIDTH, 3, SSD1306_WHITE);
      display.fillRect(0, 61, f.bar, 3, SSD1306_WHITE);
    }
  }
  display.display();
}

// ---------- Behaviors ----------
void updateBlink(unsigned long now, float dt) {
  switch (phase) {
    case B_WAIT:
      if (now >= nextBlinkAt) {
        if (random(100) < WINK_CHANCE) {
          bool left = (random(2) == 0);
          closeL = left; closeR = !left;
          holdMs = WINK_HOLD_MS;
        } else {
          closeL = true; closeR = true;
          holdMs = BLINK_HOLD_MS;
        }
        targetL = closeL ? 1 : 0;
        targetR = closeR ? 1 : 0;
        phase = B_CLOSING;
      }
      break;
    case B_CLOSING:
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
    int pick = random(3);
    if (pick == 0)      lookTX = -LOOK_RANGE;
    else if (pick == 1) lookTX =  LOOK_RANGE;
    else                lookTX = 0;
    lookTY = random(-2, 3);
    nextLookAt = now + random(LOOK_MIN_MS, LOOK_MAX_MS);
  }
  lookX = approachDt(lookX, lookTX, LOOK_TAU_MS, dt);
  lookY = approachDt(lookY, lookTY, LOOK_TAU_MS, dt);
}

// ---------- Peek view: show the next task for a few seconds ----------
enum View { V_EYES, V_PEEK };
View view = V_EYES;
unsigned long viewSince = 0;
bool peekDirty = false;
bool prevRunning = false;
char lastTask[48] = "";
char shownTask[48] = "";
int  shownPending = 0;

void drawPeek() {
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  display.setTextSize(1);
  display.setCursor(0, 0);
  display.print("NEXT TASK");
  char hdr[16];
  snprintf(hdr, sizeof(hdr), "%d pending", shownPending);
  display.setCursor(SCREEN_WIDTH - 6 * (int)strlen(hdr), 0);
  display.print(hdr);
  display.drawFastHLine(0, 11, SCREEN_WIDTH, SSD1306_WHITE);

  bool big = strlen(shownTask) <= 20;
  int size     = big ? 2 : 1;
  int maxChars = big ? 10 : 21;
  int maxLines = big ? 2 : 5;
  int lineH    = big ? 17 : 10;

  char lines[5][WRAP_LINE_LEN];
  int n = wrapText(shownTask, maxChars, maxLines, lines);
  display.setTextSize(size);
  for (int i = 0; i < n; i++) {
    display.setCursor(0, 16 + i * lineH);
    display.print(lines[i]);
  }
  display.display();
}

void enterPeek(unsigned long now, const BotData &d) {
  view = V_PEEK;
  viewSince = now;
  strlcpy(shownTask, d.task, sizeof(shownTask));
  shownPending = d.pending;
  peekDirty = true;
}

void leaveToEyes(unsigned long now) {
  view = V_EYES;
  viewSince = now;
  frameValid = false;                               // force the eyes to redraw
}

void updateView(unsigned long now, const BotData &d) {
  bool running = (lt.state == T_RUNNING);
  bool hasTask = d.serverOk && d.task[0];
  bool taskChanged = hasTask && strcmp(d.task, lastTask) != 0;

  if (view == V_EYES) {
    if (running && !prevRunning) viewSince = now;   // timer just started: first peek is a while away
    unsigned long every = running ? FOCUS_PEEK_MS : EYES_VIEW_MS;
    if (hasTask && (taskChanged || now - viewSince >= every)) enterPeek(now, d);
  } else {
    if (taskChanged || (hasTask && d.pending != shownPending)) {   // updated while showing
      strlcpy(shownTask, d.task, sizeof(shownTask));
      shownPending = d.pending;
      peekDirty = true;
    }
    if (now - viewSince >= PEEK_VIEW_MS) leaveToEyes(now);
  }

  prevRunning = running;
  if (d.serverOk) strlcpy(lastTask, d.task, sizeof(lastTask));
}

// ---------- Print server data + local timer (1x per second) ----------
void printStatus(unsigned long now, const BotData &d) {
  static unsigned long lastPrint = 0;
  static bool ipShown = false;
  if (now - lastPrint < STATUS_PRINT_MS) return;
  lastPrint = now;

  if (d.wifiOk && !ipShown) {
    ipShown = true;
    Serial.print("Wi-Fi connected, ESP32 IP: ");
    Serial.println(WiFi.localIP());
  }
  if (!d.wifiOk) ipShown = false;

  Serial.printf("wifi=%d server=%d pending=%d task='%s' | server timer=%s %d/%d | LOCAL timer=%s %ds\n",
                d.wifiOk, d.serverOk, d.pending, d.task,
                d.timerStatus, d.timerRemaining, d.timerTotal,
                timerStateName(lt.state), timerRemainingSec(now));
}

// ---------- Arduino entry points ----------
void setup() {
  Serial.begin(115200);
  Wire.begin(SDA_PIN, SCL_PIN);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
    Serial.println("OLED not found - check wiring (SDA=21, SCL=22, addr 0x3C)");
    for (;;) delay(1000);
  }
  Wire.setClock(I2C_SPEED);

  randomSeed(esp_random());
  display.clearDisplay();
  display.display();

  lastUpdate  = millis();
  viewSince   = lastUpdate;
  nextBlinkAt = lastUpdate + 2000;
  nextLookAt  = lastUpdate + 1500;

  startNetTask();                                   // Wi-Fi + polling on core 0
}

void loop() {
  unsigned long now = millis();
  float dt = (float)(now - lastUpdate);
  if (dt < 8) { delay(1); return; }
  if (dt > 120) dt = 120;
  lastUpdate = now;

  BotData d;
  getBotData(d);
  updateTimer(now, d);

  updateBlink(now, dt);
  updateLook(now, dt);
  updateView(now, d);
  layoutT = approachDt(layoutT, lt.state == T_RUNNING ? 1.0f : 0.0f, LAYOUT_TAU_MS, dt);

  if (view == V_PEEK) {
    if (peekDirty) { drawPeek(); peekDirty = false; }
  } else {
    render(now);
  }
  printStatus(now, d);
}