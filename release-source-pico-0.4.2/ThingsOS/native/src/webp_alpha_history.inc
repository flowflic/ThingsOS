/* SPDX-License-Identifier: BSD-3-Clause
 * Compact, exact history for libwebp's ALPH decoder. The surrounding decoder
 * retains its upstream BSD license. Each entry preserves the complete ARGB
 * residual, including color-cache values and inverse-transform inputs.
 * Only history is packed; filtering and pixel reconstruction stay upstream.
 */
typedef struct {
  uint32_t colors[512];
  uint16_t slots[1024];
  unsigned count;
  unsigned bits, page_count;
  uint32_t* expanded;
  uint32_t* wide;
  size_t pixels;
  uint8_t* pages[80];
} ThingsAlphaHistory;

/* If a stream exceeds the compact palette, preserve every value by promoting
   the history to ordinary 32-bit words, subject to the same memory budget. */
static unsigned ThingsAlphaByte(const ThingsAlphaHistory* h, size_t off) {
  return h->pages[off >> 15][off & 32767];
}
static void ThingsAlphaSetByte(ThingsAlphaHistory* h, size_t off, unsigned value) {
  h->pages[off >> 15][off & 32767] = (uint8_t)value;
}
static unsigned ThingsAlphaIndex(const ThingsAlphaHistory* h, int pos) {
  const size_t bit = (size_t)pos * h->bits;
  const size_t off = bit >> 3;
  return (ThingsAlphaByte(h,off) | (ThingsAlphaByte(h,off+1) << 8)) >> (bit & 7) & ((1u << h->bits)-1);
}
static void ThingsAlphaSetIndex(ThingsAlphaHistory* h, int pos, unsigned index) {
  const size_t bit = (size_t)pos * h->bits;
  const size_t off = bit >> 3;
  const unsigned shift = bit & 7;
  unsigned word = ThingsAlphaByte(h,off) | (ThingsAlphaByte(h,off+1) << 8);
  word = (word & ~(((1u << h->bits)-1) << shift)) | (index << shift);
  ThingsAlphaSetByte(h,off,word);ThingsAlphaSetByte(h,off+1,word >> 8);
}
static int ThingsAlphaPages(ThingsAlphaHistory* h, unsigned bits) {
  const size_t bytes = (h->pixels * bits + 7) / 8 + 1;
  const unsigned count = (unsigned)((bytes + 32767) / 32768);
  if (count > 80) return 0;
  while (h->page_count < count) {
    uint8_t* p = (uint8_t*)WebPSafeCalloc(32768, 1);
    if (!p) return 0;
    h->pages[h->page_count++] = p;
  }
  return 1;
}
static uint32_t ThingsAlphaGet(const ThingsAlphaHistory* h, int pos) {
  if (h->wide) return h->wide[pos];
  return h->colors[ThingsAlphaIndex(h, pos)];
}
static int ThingsAlphaSet(ThingsAlphaHistory* h, int pos, uint32_t value) {
  if (h->wide) { h->wide[pos] = value; return 1; }
  unsigned slot = (value * 0x9e3779b1u) >> 22;
  while (h->slots[slot]) {
    unsigned index = h->slots[slot] - 1;
    if (h->colors[index] == value) { ThingsAlphaSetIndex(h, pos, index); return 1; }
    slot = (slot + 1) & 1023;
  }
  if (h->count == 512) {
    uint32_t* wide = (uint32_t*)WebPSafeMalloc(h->pixels, sizeof(*wide));
    if (!wide) return 0;
    for (int i = 0; i < pos; ++i) wide[i] = ThingsAlphaGet(h, i);
    h->wide = wide; wide[pos] = value; return 1;
  }
  if (h->count == 256 && h->bits == 8) {
    if (!ThingsAlphaPages(h,9)) return 0;
    h->bits = 9;
    /* Widen backwards so the source eight-bit indices remain untouched
       until each one has been read. No second full history is needed. */
    for (int i = pos - 1; i >= 0; --i) {
      unsigned index = ThingsAlphaByte(h,(size_t)i);
      ThingsAlphaSetIndex(h,i,index);
    }
  }
  h->colors[h->count] = value;
  h->slots[slot] = (uint16_t)(h->count + 1);
  ThingsAlphaSetIndex(h, pos, h->count++);
  return 1;
}
static void ThingsAlphaCopy(ThingsAlphaHistory* h, int dest, int src) {
  if (h->wide) h->wide[dest] = h->wide[src];
  else ThingsAlphaSetIndex(h, dest, ThingsAlphaIndex(h, src));
}
static void ThingsAlphaClear(VP8LDecoder* dec) {
  if (dec->thingsos_packed_alpha && dec->pixels) {
    ThingsAlphaHistory* h = (ThingsAlphaHistory*)dec->pixels;
    WebPSafeFree(h->wide);
    for (unsigned i = 0; i < h->page_count; ++i) WebPSafeFree(h->pages[i]);
    dec->thingsos_packed_alpha = 0;
  }
}
static int ThingsAlphaAllocate(VP8LDecoder* dec, int final_width) {
  const uint64_t pixels = (uint64_t)dec->width * dec->height;
  const uint64_t rows = (uint64_t)final_width;
  const uint64_t cache = rows * 2 + final_width;
  const uint64_t bytes = sizeof(ThingsAlphaHistory) + cache * sizeof(uint32_t);
  ThingsAlphaHistory* h = (ThingsAlphaHistory*)WebPSafeCalloc(bytes, 1);
  if (!h) return VP8LSetError(dec, VP8_STATUS_OUT_OF_MEMORY);
  h->pixels = (size_t)pixels;
  h->bits = 8;
  h->expanded = (uint32_t*)(h + 1);
  dec->pixels = (uint32_t*)h;
  dec->thingsos_packed_alpha = 1;
  dec->argb_cache = h->expanded + rows + final_width;
  if (!ThingsAlphaPages(h,8)) {
    ThingsAlphaClear(dec);WebPSafeFree(h);dec->pixels=NULL;dec->argb_cache=NULL;
    return VP8LSetError(dec,VP8_STATUS_OUT_OF_MEMORY);
  }
  return 1;
}
