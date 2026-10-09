// Local timer state machine (Step 2).
// The server is the source of truth, but the ESP32 keeps counting on its own
// between polls (and while the server is unreachable).
#pragma once
#include <string.h>
#include "net_task.h"

enum TimerState { T_IDLE, T_RUNNING, T_FINISHED };

#define DONE_SHOW_MS 30000   // how long the "Done!" moment lasts (used in later steps)

struct LocalTimer {
  TimerState state = T_IDLE;
  unsigned long endMs = 0;          // millis() value when the running timer hits zero
  unsigned long totalMs = 0;        // full length of the current timer (for the progress bar)
  unsigned long finishedAt = 0;     // millis() when it finished
  unsigned long lastSeenPoll = 0;   // BotData.lastOkAt we already processed
};

static LocalTimer lt;

static const char* timerStateName(TimerState s) {
  return s == T_RUNNING ? "RUNNING" : (s == T_FINISHED ? "FINISHED" : "IDLE");
}

// Call once per loop with a fresh copy of BotData.
static void updateTimer(unsigned long now, const BotData &d) {
  // 1. A new successful poll arrived: re-sync with the server.
  if (d.serverOk && d.lastOkAt != lt.lastSeenPoll) {
    lt.lastSeenPoll = d.lastOkAt;

    if (strcmp(d.timerStatus, "running") == 0 && d.timerRemaining > 0) {
      // Right after finishing, the server can still say "running, 1 s left" because
      // of rounding. Only accept a running timer in that case if it's clearly a new one.
      bool tooCloseToEnd = (lt.state == T_FINISHED && d.timerRemaining <= 3);
      if (!tooCloseToEnd) {
        lt.endMs = d.lastOkAt + (unsigned long)d.timerRemaining * 1000UL;
        lt.totalMs = (unsigned long)d.timerTotal * 1000UL;
        lt.state = T_RUNNING;
      }
    } else if (strcmp(d.timerStatus, "finished") == 0) {
      if (lt.state != T_FINISHED) { lt.state = T_FINISHED; lt.finishedAt = now; }
    } else {
      lt.state = T_IDLE;                         // stopped / nothing running
    }
  }

  // 2. Local countdown: works even if the server is unreachable.
  if (lt.state == T_RUNNING && (long)(now - lt.endMs) >= 0) {
    lt.state = T_FINISHED;
    lt.finishedAt = now;
  }
}

// Whole seconds left (0 unless running).
static int timerRemainingSec(unsigned long now) {
  if (lt.state != T_RUNNING) return 0;
  long ms = (long)(lt.endMs - now);
  return ms <= 0 ? 0 : (int)((ms + 999) / 1000);
}

// True for DONE_SHOW_MS after the timer finishes.
static bool timerDoneFresh(unsigned long now) {
  return lt.state == T_FINISHED && (now - lt.finishedAt) < DONE_SHOW_MS;
}