// Pure logic for Nova's display states. No Arduino dependencies, so it is unit-tested on a PC
// (tests/firmware/nova_logic_test.cpp).
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <string.h>

enum NovaState : uint8_t {
  S_IDLE = 0, S_LISTENING, S_THINKING, S_SPEAKING, S_WORKING, S_SAVED, S_ERROR, S_DISCONNECTED, S_COUNT
};

static const char *const NOVA_STATE_NAMES[S_COUNT] = {
  "idle", "listening", "thinking", "speaking", "working", "saved", "error", "disconnected"
};

#define DISCONNECT_AFTER_MS 15000UL   // no contact from the laptop for this long -> disconnected
#define MAX_TTL_MS          600000UL  // a pushed state never lasts longer than 10 minutes
#define NOVA_TEXT_MAX       40

typedef uint32_t nms;                 // milliseconds, wraps after ~49 days (handled below)

static inline const char *novaStateName(NovaState s) {
  return s < S_COUNT ? NOVA_STATE_NAMES[s] : "idle";
}

static inline char lowerAscii(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

// Case-insensitive. Returns false (and leaves `out` alone) for anything that is not a state name.
static inline bool parseNovaState(const char *name, NovaState &out) {
  if (!name) return false;
  for (int i = 0; i < S_COUNT; i++) {
    const char *a = name, *b = NOVA_STATE_NAMES[i];
    while (*a && *b && lowerAscii(*a) == *b) { a++; b++; }
    if (!*a && !*b) { out = (NovaState)i; return true; }
  }
  return false;
}

// How long a pushed state stays on screen if the laptop does not refresh it.
// A laptop that crashes mid-task must not leave the device stuck on "working" forever.
static inline uint32_t defaultTtlMs(NovaState s) {
  switch (s) {
    case S_LISTENING:    return 15000;
    case S_THINKING:     return 30000;
    case S_SPEAKING:     return 30000;
    case S_WORKING:      return 60000;
    case S_SAVED:        return 2500;
    case S_ERROR:        return 8000;
    case S_DISCONNECTED: return 30000;
    default:             return 0;      // idle never expires
  }
}

static inline uint32_t clampTtlMs(NovaState s, uint32_t requested) {
  if (s == S_IDLE) return 0;
  if (requested == 0) return defaultTtlMs(s);
  return requested > MAX_TTL_MS ? (uint32_t)MAX_TTL_MS : requested;
}

// Printable ASCII only (anything else becomes '?'), at most NOVA_TEXT_MAX characters.
static inline void sanitizeText(const char *in, char *out, size_t cap) {
  if (cap == 0) return;
  size_t n = 0;
  if (in) {
    for (; *in && n + 1 < cap && n < NOVA_TEXT_MAX; in++) {
      unsigned char c = (unsigned char)*in;
      out[n++] = (c >= 32 && c < 127) ? (char)c : '?';
    }
  }
  out[n] = 0;
}

static inline bool timeReached(nms now, nms target) { return (int32_t)(now - target) >= 0; }

static inline bool linkUp(nms now, nms lastContact) {
  return (nms)(now - lastContact) < DISCONNECT_AFTER_MS;
}

// What the screen should show right now.
//  - a pushed state that has not expired wins (a push also proves the laptop is alive)
//  - otherwise: idle if the laptop was heard from recently, disconnected if not
static inline NovaState resolveState(NovaState pushed, nms expiresAt, nms now, nms lastContact) {
  if (pushed != S_IDLE && !timeReached(now, expiresAt)) return pushed;
  return linkUp(now, lastContact) ? S_IDLE : S_DISCONNECTED;
}
