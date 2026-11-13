// Tiny QR code generator: version 2 (25 x 25 modules), error correction L,
// text made only of 0-9 A-Z space $ % * + - . / :   (up to 47 characters).
// That is enough for the pairing link  HTTP://192.168.4.1/<id><key>  and needs no library.
#pragma once
#include <stdint.h>
#include <string.h>

#define QR_SIZE 25

struct QrCode {
  uint8_t m[QR_SIZE][QR_SIZE];    // 1 = dark module
  uint8_t fn[QR_SIZE][QR_SIZE];   // 1 = function pattern (never masked)
};

static const char QR_ALNUM[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:";
static uint8_t qrExp[256], qrLog[256];

static void qrGfInit() {
  int x = 1;
  for (int i = 0; i < 255; i++) { qrExp[i] = x; qrLog[x] = i; x <<= 1; if (x & 0x100) x ^= 0x11D; }
  qrExp[255] = qrExp[0];
}
static uint8_t qrMul(uint8_t a, uint8_t b) { return (a && b) ? qrExp[(qrLog[a] + qrLog[b]) % 255] : 0; }

// 10 Reed-Solomon check bytes for the data bytes
static void qrReedSolomon(const uint8_t *data, int nd, uint8_t *res) {
  const int deg = 10;
  uint8_t div[10];
  for (int i = 0; i < deg - 1; i++) div[i] = 0;
  div[deg - 1] = 1;
  uint8_t root = 1;
  for (int i = 0; i < deg; i++) {
    for (int j = 0; j < deg; j++) { div[j] = qrMul(div[j], root); if (j + 1 < deg) div[j] ^= div[j + 1]; }
    root = qrMul(root, 2);
  }
  for (int i = 0; i < deg; i++) res[i] = 0;
  for (int k = 0; k < nd; k++) {
    uint8_t f = data[k] ^ res[0];
    for (int i = 0; i < deg - 1; i++) res[i] = res[i + 1];
    res[deg - 1] = 0;
    for (int i = 0; i < deg; i++) res[i] ^= qrMul(div[i], f);
  }
}

// text -> 34 data bytes (alphanumeric mode)
static bool qrEncodeData(const char *text, uint8_t *cw) {
  int n = (int)strlen(text);
  if (n > 47) return false;
  memset(cw, 0, 34);
  int nb = 0;
  const int cap = 34 * 8;
  struct Put { uint8_t *cw; int *nb; void bits(uint32_t v, int len) { for (int i = len - 1; i >= 0; i--) { if (*nb < 34 * 8 && ((v >> i) & 1)) cw[*nb >> 3] |= 0x80 >> (*nb & 7); (*nb)++; } } } put = {cw, &nb};
  put.bits(0x2, 4);                      // alphanumeric mode
  put.bits(n, 9);                        // character count
  for (int i = 0; i < n; i += 2) {
    const char *a = strchr(QR_ALNUM, text[i]);
    if (!a || !text[i]) return false;
    int va = a - QR_ALNUM;
    if (i + 1 < n) {
      const char *b = strchr(QR_ALNUM, text[i + 1]);
      if (!b) return false;
      put.bits(va * 45 + (b - QR_ALNUM), 11);
    } else put.bits(va, 6);
  }
  int term = cap - nb; if (term > 4) term = 4;
  put.bits(0, term);
  while (nb & 7) put.bits(0, 1);
  for (uint8_t pad = 0xEC; (nb >> 3) < 34; pad ^= (0xEC ^ 0x11)) put.bits(pad, 8);
  return true;
}

static void qrSetFn(QrCode *q, int x, int y, bool dark) { q->m[y][x] = dark; q->fn[y][x] = 1; }

static void qrFinder(QrCode *q, int cx, int cy) {
  for (int dy = -4; dy <= 4; dy++) for (int dx = -4; dx <= 4; dx++) {
    int x = cx + dx, y = cy + dy;
    if (x < 0 || y < 0 || x >= QR_SIZE || y >= QR_SIZE) continue;
    int d = (dx < 0 ? -dx : dx); int e = (dy < 0 ? -dy : dy); if (e > d) d = e;
    qrSetFn(q, x, y, d != 2 && d != 4);
  }
}

static void qrFormat(QrCode *q, int mask) {
  int data = (1 << 3) | mask;            // error correction L = 01
  int rem = data;
  for (int i = 0; i < 10; i++) rem = (rem << 1) ^ ((rem >> 9) * 0x537);
  int bits = ((data << 10) | rem) ^ 0x5412;
  for (int i = 0; i <= 5; i++) qrSetFn(q, 8, i, (bits >> i) & 1);
  qrSetFn(q, 8, 7, (bits >> 6) & 1);
  qrSetFn(q, 8, 8, (bits >> 7) & 1);
  qrSetFn(q, 7, 8, (bits >> 8) & 1);
  for (int i = 9; i < 15; i++) qrSetFn(q, 14 - i, 8, (bits >> i) & 1);
  for (int i = 0; i < 8; i++) qrSetFn(q, QR_SIZE - 1 - i, 8, (bits >> i) & 1);
  for (int i = 8; i < 15; i++) qrSetFn(q, 8, QR_SIZE - 15 + i, (bits >> i) & 1);
  qrSetFn(q, 8, QR_SIZE - 8, true);      // the always-dark module
}

static void qrApplyMask(QrCode *q, int mask) {
  for (int y = 0; y < QR_SIZE; y++) for (int x = 0; x < QR_SIZE; x++) {
    bool inv;
    switch (mask) {
      case 0: inv = (x + y) % 2 == 0; break;
      case 1: inv = y % 2 == 0; break;
      case 2: inv = x % 3 == 0; break;
      case 3: inv = (x + y) % 3 == 0; break;
      case 4: inv = (x / 3 + y / 2) % 2 == 0; break;
      case 5: inv = x * y % 2 + x * y % 3 == 0; break;
      case 6: inv = (x * y % 2 + x * y % 3) % 2 == 0; break;
      default: inv = ((x + y) % 2 + x * y % 3) % 2 == 0; break;
    }
    if (inv && !q->fn[y][x]) q->m[y][x] ^= 1;
  }
}

static long qrPenalty(const QrCode *q) {
  long p = 0; int dark = 0;
  for (int y = 0; y < QR_SIZE; y++) for (int x = 0; x < QR_SIZE; x++) dark += q->m[y][x];
  for (int pass = 0; pass < 2; pass++) {                       // rows, then columns
    for (int a = 0; a < QR_SIZE; a++) {
      int run = 1;
      for (int b = 1; b <= QR_SIZE; b++) {
        int cur = b < QR_SIZE ? (pass ? q->m[b][a] : q->m[a][b]) : -1;
        int prev = pass ? q->m[b - 1][a] : q->m[a][b - 1];
        if (cur == prev) run++; else { if (run >= 5) p += 3 + (run - 5); run = 1; }
      }
      for (int b = 0; b + 11 <= QR_SIZE; b++) {                // finder-like 1:1:3:1:1 with 4 light on a side
        static const uint8_t A[11] = {1,0,1,1,1,0,1,0,0,0,0}, B[11] = {0,0,0,0,1,0,1,1,1,0,1};
        bool ma = true, mb = true;
        for (int k = 0; k < 11; k++) { int v = pass ? q->m[b + k][a] : q->m[a][b + k]; if (v != A[k]) ma = false; if (v != B[k]) mb = false; }
        if (ma) p += 40;
        if (mb) p += 40;
      }
    }
  }
  for (int y = 0; y + 1 < QR_SIZE; y++) for (int x = 0; x + 1 < QR_SIZE; x++) {
    int c = q->m[y][x];
    if (c == q->m[y][x + 1] && c == q->m[y + 1][x] && c == q->m[y + 1][x + 1]) p += 3;
  }
  int total = QR_SIZE * QR_SIZE;
  int diff = dark * 20 - total * 10; if (diff < 0) diff = -diff;
  int k = (diff + total - 1) / total - 1; if (k < 0) k = 0;
  return p + 10L * k;
}

// Returns false if the text has a character that is not in QR_ALNUM or is too long.
static bool qrMake(const char *text, QrCode *q) {
  uint8_t cw[44];
  if (!qrEncodeData(text, cw)) return false;
  qrGfInit();
  qrReedSolomon(cw, 34, cw + 34);
  memset(q, 0, sizeof *q);
  for (int i = 0; i < QR_SIZE; i++) { qrSetFn(q, 6, i, i % 2 == 0); qrSetFn(q, i, 6, i % 2 == 0); }
  qrFinder(q, 3, 3); qrFinder(q, QR_SIZE - 4, 3); qrFinder(q, 3, QR_SIZE - 4);
  for (int dy = -2; dy <= 2; dy++) for (int dx = -2; dx <= 2; dx++) {   // the one alignment pattern of version 2
    int d = (dx < 0 ? -dx : dx); int e = (dy < 0 ? -dy : dy); if (e > d) d = e;
    qrSetFn(q, 18 + dx, 18 + dy, d != 1);
  }
  qrFormat(q, 0);
  int bit = 0;
  for (int right = QR_SIZE - 1; right >= 1; right -= 2) {
    if (right == 6) right = 5;
    for (int vert = 0; vert < QR_SIZE; vert++) for (int j = 0; j < 2; j++) {
      int x = right - j;
      bool up = ((right + 1) & 2) == 0;
      int y = up ? QR_SIZE - 1 - vert : vert;
      if (!q->fn[y][x] && bit < 44 * 8) { q->m[y][x] = (cw[bit >> 3] >> (7 - (bit & 7))) & 1; bit++; }
    }
  }
  int best = 0; long bestP = 1L << 30;
  for (int mask = 0; mask < 8; mask++) {
    qrApplyMask(q, mask); qrFormat(q, mask);
    long p = qrPenalty(q);
    if (p < bestP) { bestP = p; best = mask; }
    qrApplyMask(q, mask);
  }
  qrApplyMask(q, best); qrFormat(q, best);
  return true;
}
