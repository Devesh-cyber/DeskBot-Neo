// Host-side unit tests for firmware/nova_device/nova_logic.h. Built and run by tests/test_firmware_logic.py.
#include <cassert>
#include <cstdio>
#include "../../firmware/nova_device/nova_logic.h"

static int checks = 0;
#define CHECK(x) do { ++checks; if (!(x)) { printf("FAILED line %d: %s\n", __LINE__, #x); return 1; } } while (0)

int main() {
  NovaState s;

  // parsing
  for (int i = 0; i < S_COUNT; i++) { CHECK(parseNovaState(NOVA_STATE_NAMES[i], s)); CHECK(s == (NovaState)i); }
  CHECK(parseNovaState("LISTENING", s) && s == S_LISTENING);
  CHECK(parseNovaState("Working", s) && s == S_WORKING);
  s = S_ERROR;
  CHECK(!parseNovaState("", s) && s == S_ERROR);          // unchanged on failure
  CHECK(!parseNovaState("idl", s));
  CHECK(!parseNovaState("idle ", s));
  CHECK(!parseNovaState("idlee", s));
  CHECK(!parseNovaState(nullptr, s));

  // ttl
  CHECK(clampTtlMs(S_IDLE, 5000) == 0);
  CHECK(clampTtlMs(S_SAVED, 0) == 2500);
  CHECK(clampTtlMs(S_WORKING, 0) == 60000);
  CHECK(clampTtlMs(S_WORKING, 1234) == 1234);
  CHECK(clampTtlMs(S_WORKING, 99999999u) == MAX_TTL_MS);
  for (int i = 1; i < S_COUNT; i++) CHECK(defaultTtlMs((NovaState)i) > 0);   // nothing sticks forever

  // text
  char out[NOVA_TEXT_MAX + 1];
  sanitizeText("Hello\n\x01 w\xc3\xa9rld", out, sizeof(out));
  CHECK(strcmp(out, "Hello?? w??rld") == 0);
  sanitizeText(nullptr, out, sizeof(out));
  CHECK(out[0] == 0);
  char longIn[100]; memset(longIn, 'a', 99); longIn[99] = 0;
  sanitizeText(longIn, out, sizeof(out));
  CHECK(strlen(out) == NOVA_TEXT_MAX);
  char tiny[4];
  sanitizeText("abcdef", tiny, sizeof(tiny));
  CHECK(strcmp(tiny, "abc") == 0);

  // link + resolve
  CHECK(linkUp(14999, 0) && !linkUp(15000, 0));
  CHECK(resolveState(S_IDLE, 0, 1000, 900) == S_IDLE);
  CHECK(resolveState(S_IDLE, 0, 20000, 900) == S_DISCONNECTED);
  CHECK(resolveState(S_WORKING, 5000, 4999, 0) == S_WORKING);          // not expired
  CHECK(resolveState(S_WORKING, 5000, 5000, 4900) == S_IDLE);          // expired, laptop still around
  CHECK(resolveState(S_WORKING, 5000, 30000, 0) == S_DISCONNECTED);    // expired, laptop gone
  CHECK(resolveState(S_ERROR, 100000, 50000, 0) == S_ERROR);           // an unexpired push wins even when silent

  // millisecond counter wrap-around (every ~49.7 days)
  nms nearWrap = 0xFFFFFF00u;
  CHECK(timeReached(nearWrap + 0x200, nearWrap + 0x100));              // 'now' has wrapped past the target
  CHECK(!timeReached(nearWrap, nearWrap + 0x100));
  CHECK(linkUp(nearWrap + 0x1000, nearWrap));                          // contact just before the wrap
  CHECK(resolveState(S_SAVED, nearWrap + 2500, nearWrap + 100, nearWrap) == S_SAVED);
  CHECK(resolveState(S_SAVED, nearWrap + 2500, nearWrap + 2600, nearWrap + 2000) == S_IDLE);

  printf("nova_logic: %d checks passed\n", checks);
  return 0;
}
