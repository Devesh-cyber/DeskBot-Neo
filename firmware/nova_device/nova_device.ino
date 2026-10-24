/*
  Nova device - Phase 1: the face with 8 states.
    idle          eyes blink / look around / peek at your next task; timer running -> countdown layout
    listening     wide-open eyes, pulsing dots
    thinking      eyes glance up and side to side, jumping dots
    speaking      small eyes + talking mouth
    working       squinting eyes, step text, progress bar
    saved         happy ^ ^ eyes + check mark (goes back to idle by itself)
    error         X eyes + message (goes back to idle by itself)
    disconnected  sleepy eyes (laptop silent for 15 s); the local timer keeps running

  The laptop sets a state with  POST /display  (see nova_server.h and docs/api-contracts.md).
  Files in this folder: nova_device.ino, nova_logic.h, nova_state.h, nova_server.h, net_task.h,
                        timer_state.h, views.h, faces.h, secrets.h (you create it from secrets.example.h)
  Libraries: Adafruit SSD1306, Adafruit GFX, ArduinoJson (v7)
  Serial Monitor: 115200 baud
*/

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <string.h>
#include "nova_state.h"
#include "net_task.h"
#include "timer_state.h"
#include "views.h"
#include "faces.h"
#include "nova_server.h"

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
#define LAYOUT_TAU_MS     80    // how fast the timer layout fades in and out
#define SHAPE_TAU_MS      80    // how fast eyes morph between states

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

// Types used by function signatures live up here (Arduino adds function prototypes below this point).
struct Shape { float w, h, r, lx, rx, cy; };                      // eye geometry, tweened between states
struct Frame { int lx, rx, cy, w, r, hL, hR, secs, bar, a1; uint32_t seq; uint8_t st; };

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
Shape cur = { EYE_W, EYE_H, EYE_R, LEFT_EYE_X, RIGHT_EYE_X, EYE_Y };

unsigned long lastUpdate = 0;
NovaState prevState = S_IDLE;

Frame lastFrame;                   // if nothing changed we skip the I2C transfer
bool frameValid = false;

// ---------- Helpers ----------
float approachDt(float cur, float target, float tauMs, float dtMs) {
  float k = 1.0f - expf(-dtMs / tauMs);
  return cur + (target - cur) * k;
}

int lerpI(int a, int b, float t) { return (int)roundf(a + (b - a) * t); }

int eyeHeight(float blinkAmt, float baseH) {
  return (int)roundf(fmaxf(3.0f, baseH * (1.0f - blinkAmt)));
}

bool blinkAllowed(NovaState st) {
  return st == S_IDLE || st == S_THINKING || st == S_SPEAKING || st == S_WORKING;
}

// Where the eyes want to be for each state.
Shape shapeFor(NovaState st, float t) {
  switch (st) {
    case S_LISTENING:  return { 34, 44, 14, 36, 92, 31 };
    case S_THINKING:   return { 30, 30, 10, 36, 92, 34 };
    case S_SPEAKING:   return { 26, 26, 9, 40, 88, 16 };
    case S_WORKING:    return { 28, 14, 4, 38, 90, 14 };
    case S_SAVED:
    case S_ERROR:      return { 26, 26, 9, 40, 88, 16 };          // eyes are drawn as ^ or X; park neutral
    default: break;                                                // idle and disconnected
  }
  if (st == S_DISCONNECTED && lt.state != T_RUNNING)
    return { 34, 8, 3, 36, 92, 26 };                              // sleepy slits
  return { (float)lerpI(EYE_W, FOCUS_EYE_W, t), (float)lerpI(EYE_H, FOCUS_EYE_H, t),
           (float)lerpI(EYE_R, FOCUS_EYE_R, t), (float)lerpI(LEFT_EYE_X, FOCUS_LEFT_X, t),
           (float)lerpI(RIGHT_EYE_X, FOCUS_RIGHT_X, t), (float)lerpI(EYE_Y, FOCUS_EYE_Y, t) };
}

float lookScaleFor(NovaState st, float t) {
  if (st == S_IDLE) return 1.0f - 0.5f * t;                       // wander less in focus mode
  if (st == S_WORKING) return 0.5f;
  return 1.0f;
}

// ---------- Drawing ----------
void drawEye(int cx, int cy, int w, int hh, int r) {
  r = min(r, min(w, hh) / 2);
  display.fillRoundRect(cx - w / 2, cy - hh / 2, w, hh, r, SSD1306_WHITE);
}

void drawCentered(const char *s, int y, int size) {
  display.setTextSize(size);
  int tw = (int)strlen(s) * 6 * size;
  display.setCursor((SCREEN_WIDTH - tw) / 2, y);
  display.print(s);
}

// Wrap `text` and print it left-aligned at y, size 1. Returns lines printed.
int drawWrapped(const char *text, int y, int maxLines, int lineH) {
  char lines[4][WRAP_LINE_LEN];
  if (maxLines > 4) maxLines = 4;
  int n = wrapText(text, 21, maxLines, lines);
  display.setTextSize(1);
  for (int i = 0; i < n; i++) {
    display.setCursor(0, y + i * lineH);
    display.print(lines[i]);
  }
  return n;
}

void render(unsigned long now, NovaState st, const NovaView &nv) {
  float scale = lookScaleFor(st, layoutT);
  int dx = (int)roundf(lookX * scale);
  int dy = (int)roundf(lookY * scale);
  bool blinkOn = blinkAllowed(st);
  uint32_t tick = now / 40;                                       // animations step at most 25x per second

  Frame f;
  memset(&f, 0, sizeof(f));                                       // so memcmp below ignores padding
  f.st  = (uint8_t)st;
  f.seq = nv.seq;
  f.w   = (int)roundf(cur.w);
  f.r   = (int)roundf(cur.r);
  f.cy  = (int)roundf(cur.cy) + dy;
  f.lx  = (int)roundf(cur.lx) + dx;
  f.rx  = (int)roundf(cur.rx) + dx;
  f.hL  = eyeHeight(blinkOn ? blinkL : 0, cur.h);
  f.hR  = eyeHeight(blinkOn ? blinkR : 0, cur.h);

  bool timerLayout = (st == S_IDLE || st == S_DISCONNECTED);
  bool showTimer = (timerLayout && layoutT > 0.85f && lt.state == T_RUNNING);
  long totalS = (long)(lt.totalMs / 1000UL);
  f.secs = showTimer ? timerRemainingSec(now) : -1;
  if (showTimer && totalS > 0) {
    long bar = (totalS - f.secs) * SCREEN_WIDTH / totalS;
    f.bar = (int)(bar < 0 ? 0 : (bar > SCREEN_WIDTH ? SCREEN_WIDTH : bar));
  }

  switch (st) {                                                   // per-state animation phase
    case S_LISTENING: f.a1 = (int)((tick / 6) % 4); break;
    case S_THINKING:  f.a1 = (int)((tick / 8) % 4); break;
    case S_SPEAKING:  f.a1 = mouthHeight(tick); break;
    case S_WORKING:   f.a1 = (int)(nv.steps > 0 ? 0 : tick); break;
    default: break;
  }

  if (frameValid && memcmp(&f, &lastFrame, sizeof(Frame)) == 0) return;   // nothing changed
  lastFrame = f;
  frameValid = true;

  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);

  switch (st) {
    case S_SAVED:
      drawHappyEye(display, 40, 22, 11);
      drawHappyEye(display, 88, 22, 11);
      drawCheck(display, 64, 46);
      if (nv.text[0]) drawWrapped(nv.text, 56, 1, 10);
      break;

    case S_ERROR:
      drawXEye(display, 40, 16, 9);
      drawXEye(display, 88, 16, 9);
      if (nv.text[0]) drawWrapped(nv.text, 34, 3, 10);
      else drawCentered("ERROR", 38, 2);
      break;

    default:
      drawEye(f.lx, f.cy, f.w, f.hL, f.r);
      drawEye(f.rx, f.cy, f.w, f.hR, f.r);
      break;
  }

  switch (st) {
    case S_LISTENING:
      drawListeningDots(display, f.a1);
      break;

    case S_THINKING:
      drawThinkingDots(display, f.a1);
      break;

    case S_SPEAKING:
      drawMouth(display, 64, 46, 32, f.a1);
      break;

    case S_WORKING: {
      int n = nv.text[0] ? drawWrapped(nv.text, 28, 2, 10) : 0;
      (void)n;
      if (nv.steps > 0) {
        char lbl[12];
        snprintf(lbl, sizeof(lbl), "%d/%d", nv.step, nv.steps);
        display.setTextSize(1);
        display.setCursor(SCREEN_WIDTH - 6 * (int)strlen(lbl), 48);
        display.print(lbl);
      }
      drawWorkBar(display, nv.step, nv.steps, tick);
      break;
    }

    case S_DISCONNECTED:
      if (f.secs >= 0) {
        drawNoLinkMark(display);                                  // timer layout, with a "no link" mark
      } else {
        display.setTextSize(2); display.setCursor(100, 2); display.print("Z");
        display.setTextSize(1); display.setCursor(116, 16); display.print("z");
        drawCentered("Nova offline", 46, 1);
      }
      break;

    default: break;
  }

  if (f.secs >= 0) {                                              // countdown + progress bar
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

void updateLook(unsigned long now, float dt, NovaState st) {
  if (st == S_THINKING) {                                         // glance up, left, right, left...
    lookTX = ((now / 800) % 2) ? LOOK_RANGE : -LOOK_RANGE;
    lookTY = -6;
  } else if (st == S_IDLE || st == S_WORKING) {                   // wander
    if (now >= nextLookAt) {
      int pick = random(3);
      if (pick == 0)      lookTX = -LOOK_RANGE;
      else if (pick == 1) lookTX =  LOOK_RANGE;
      else                lookTX = 0;
      lookTY = random(-2, 3);
      nextLookAt = now + random(LOOK_MIN_MS, LOOK_MAX_MS);
    }
  } else {                                                        // listening, speaking, saved, error, disconnected
    lookTX = 0;
    lookTY = 0;
  }
  lookX = approachDt(lookX, lookTX, LOOK_TAU_MS, dt);
  lookY = approachDt(lookY, lookTY, LOOK_TAU_MS, dt);
}

// ---------- Peek view: show the next task for a few seconds (idle only) ----------
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

// ---------- Print status (1x per second) ----------
void printStatus(unsigned long now, const BotData &d, const NovaView &nv) {
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

  Serial.printf("wifi=%d server=%d state=%s contact=%lus pending=%d task='%s' | LOCAL timer=%s %ds\n",
                d.wifiOk, d.serverOk, novaStateName(nv.state), (unsigned long)(nv.contactAgeMs / 1000UL),
                d.pending, d.task, timerStateName(lt.state), timerRemainingSec(now));
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

  novaInit((nms)lastUpdate);                        // boot counts as contact (15 s grace)
  startNetTask();                                   // Wi-Fi + polling on core 0
  startHttpServer();                                // display commands from the laptop, core 0
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

  NovaView nv;
  novaGetView((nms)now, nv);
  NovaState st = nv.state;
  if (st != prevState) {
    Serial.printf("state: %s -> %s\n", novaStateName(prevState), novaStateName(st));
    if (view == V_PEEK && st != S_IDLE) leaveToEyes(now);
    if (st == S_IDLE) viewSince = now;
    prevState = st;
  }

  updateBlink(now, dt);
  updateLook(now, dt, st);

  bool timerLayout = (st == S_IDLE || st == S_DISCONNECTED);
  layoutT = approachDt(layoutT, (lt.state == T_RUNNING && timerLayout) ? 1.0f : 0.0f, LAYOUT_TAU_MS, dt);

  Shape tgt = shapeFor(st, layoutT);
  cur.w  = approachDt(cur.w,  tgt.w,  SHAPE_TAU_MS, dt);
  cur.h  = approachDt(cur.h,  tgt.h,  SHAPE_TAU_MS, dt);
  cur.r  = approachDt(cur.r,  tgt.r,  SHAPE_TAU_MS, dt);
  cur.lx = approachDt(cur.lx, tgt.lx, SHAPE_TAU_MS, dt);
  cur.rx = approachDt(cur.rx, tgt.rx, SHAPE_TAU_MS, dt);
  cur.cy = approachDt(cur.cy, tgt.cy, SHAPE_TAU_MS, dt);

  if (st == S_IDLE) updateView(now, d);

  if (st == S_IDLE && view == V_PEEK) {
    if (peekDirty) { drawPeek(); peekDirty = false; }
  } else {
    render(now, st, nv);
  }
  printStatus(now, d, nv);
}
