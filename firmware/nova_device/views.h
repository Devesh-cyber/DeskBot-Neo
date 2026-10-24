// Text helpers for the OLED (Step 3).
// Default Adafruit font: 6x8 px per character at size 1, 12x16 at size 2.
//   size 1 -> 21 chars per line, size 2 -> 10 chars per line (on a 128 px wide screen)
#pragma once
#include <string.h>
#include <stdio.h>

#define WRAP_LINE_LEN 22     // 21 chars + terminating 0

// Word-wrap `text` into out[][WRAP_LINE_LEN]. Returns number of lines used (0 if empty).
// Very long words are split; if the text doesn't fit in maxLines it ends with "...".
static int wrapText(const char *text, int maxChars, int maxLines, char out[][WRAP_LINE_LEN]) {
  if (!text || !text[0] || maxChars < 4 || maxChars >= WRAP_LINE_LEN) return 0;

  int n = 0, col = 0;
  out[0][0] = 0;
  bool truncated = false;
  const char *p = text;

  while (*p && !truncated) {
    while (*p == ' ') p++;
    if (!*p) break;

    const char *w = p;
    int len = 0;
    while (p[len] && p[len] != ' ') len++;
    p += len;

    while (len > 0) {
      int chunk = len > maxChars ? maxChars : len;
      int needed = (col == 0 ? 0 : 1) + chunk;
      if (col + needed > maxChars) {               // doesn't fit: go to the next line
        if (n + 1 >= maxLines) { truncated = true; break; }
        n++; col = 0; out[n][0] = 0;
        continue;
      }
      if (col > 0) out[n][col++] = ' ';
      memcpy(&out[n][col], w, chunk);
      col += chunk;
      out[n][col] = 0;
      w += chunk;
      len -= chunk;
    }
  }

  if (!truncated && *p) {                           // text left over after the loop
    while (*p == ' ') p++;
    if (*p) truncated = true;
  }
  if (truncated) {
    int l = strlen(out[n]);
    if (l + 3 > maxChars) l = maxChars - 3;
    strcpy(&out[n][l], "...");
  }
  return n + 1;
}

// Countdown text. Returns the text size to draw it with (3 = big MM:SS, 2 = H:MM:SS).
static int formatCountdown(int secs, char *buf, size_t n) {
  if (secs < 0) secs = 0;
  if (secs >= 3600) {
    snprintf(buf, n, "%d:%02d:%02d", secs / 3600, (secs / 60) % 60, secs % 60);
    return 2;
  }
  snprintf(buf, n, "%02d:%02d", secs / 60, secs % 60);
  return 3;
}
