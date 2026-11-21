"""Generate a grayscale progressive-JPEG derivative without editing pinned stb."""
from pathlib import Path
import sys

def generate(source, output):
    s=Path(source).read_text(encoding='utf-8')
    s='#include <stdint.h>\n'+s
    def replace(before,after,count=None):
        nonlocal s
        assert before in s,before
        s=s.replace(before,after) if count is None else s.replace(before,after,count)
    replace('   int            progressive;','   int            thingsos_want_gray, thingsos_luma;\n   int            progressive;',1)
    replace('   int            thingsos_want_gray, thingsos_luma;',
            '   stbi_uc* thingsos_reader_pixels;\n   int thingsos_reader_width, thingsos_reader_height;\n   int            thingsos_want_gray, thingsos_luma;',1)
    replace('      short   *coeff;   // progressive only','      int thingsos_coeff_pages;\n      short   *coeff;   // progressive only',1)
    replace('   // load a jpeg image from whichever source, but leave in YCbCr format',
            '   z->thingsos_reader_pixels = NULL;\n   z->thingsos_want_gray = req_comp == 1 || req_comp == 2;\n\n   // load a jpeg image from whichever source, but leave in YCbCr format',1)
    replace('   // compute interleaved mcu info',
            '   z->thingsos_luma = z->progressive && z->thingsos_want_gray && (s->img_n == 1 || (s->img_n == 3 &&\n'
            '      !(z->rgb == 3 || (z->app14_color_transform == 0 && !z->jfif))));\n\n'
            '   // compute interleaved mcu info',1)
    replace('stbi__malloc_mad2(z->img_comp[i].w2, z->img_comp[i].h2, 15)',
            '(z->thingsos_luma && (i > 0 || z->thingsos_reader_pixels) ? stbi__malloc_mad2(1, 1, 15) : stbi__malloc_mad2(z->img_comp[i].w2, z->img_comp[i].h2, 15))')
    replace('stbi__malloc_mad3(z->img_comp[i].w2, z->img_comp[i].h2, sizeof(short), 15)',
            'thingsos_jpeg_alloc_coeff(z, i)')
    replace('      z->img_comp[i].raw_coeff = 0;', '      z->img_comp[i].raw_coeff = 0;\n      z->img_comp[i].thingsos_coeff_pages = 0;')
    replace('      j->img_comp[m].raw_coeff = NULL;', '      j->img_comp[m].raw_coeff = NULL;\n      j->img_comp[m].thingsos_coeff_pages = 0;')
    replace('         z->img_comp[i].coeff = (short*) (((size_t) z->img_comp[i].raw_coeff + 15) & ~15);',
            '         z->img_comp[i].coeff = z->img_comp[i].thingsos_coeff_pages ? NULL : (short*) (((size_t) z->img_comp[i].raw_coeff + 15) & ~15);\n'
            '         if (z->thingsos_luma && i > 0) memset(z->img_comp[i].coeff, 0, (size_t)z->img_comp[i].coeff_w * z->img_comp[i].coeff_h * 8);')
    # Unused chroma planes need only the nonzero pattern to consume successive
    # AC refinement bits. Their magnitudes and signs never affect luma output.
    helpers='''
static void* thingsos_jpeg_alloc_coeff(stbi__jpeg* z, int component)
{
   if (z->thingsos_luma && component == 0) {
      size_t bytes = (size_t)z->img_comp[component].w2 * z->img_comp[component].h2 * sizeof(short);
      int count = (int)((bytes + 32767) / 32768), i;
      void** pages = (void**)STBI_MALLOC((size_t)count * sizeof(*pages));
      if (!pages) return NULL;
      memset(pages, 0, (size_t)count * sizeof(*pages));
      z->img_comp[component].raw_coeff = pages;
      z->img_comp[component].thingsos_coeff_pages = count;
      for (i = 0; i < count; ++i) {
         size_t n = bytes < 32768 ? bytes : 32768;
         pages[i] = STBI_MALLOC(n);
         if (!pages[i]) { for (int k = 0; k < i; ++k) STBI_FREE(pages[k]); STBI_FREE(pages); z->img_comp[component].thingsos_coeff_pages = 0; return NULL; }
         bytes -= n;
      }
      return pages;
   }
   if (z->thingsos_luma && component > 0)
      return stbi__malloc_mad3(z->img_comp[component].coeff_w, z->img_comp[component].coeff_h, 8, 15);
   return stbi__malloc_mad3(z->img_comp[component].w2, z->img_comp[component].h2, sizeof(short), 15);
}
static void thingsos_jpeg_free_coeff(stbi__jpeg* z, int component)
{
   if (z->img_comp[component].thingsos_coeff_pages) {
      void** pages = (void**)z->img_comp[component].raw_coeff;
      for (int i = 0; i < z->img_comp[component].thingsos_coeff_pages; ++i) STBI_FREE(pages[i]);
      z->img_comp[component].thingsos_coeff_pages = 0;
   }
   STBI_FREE(z->img_comp[component].raw_coeff);
}
static void thingsos_jpeg_emit(stbi__jpeg* z, const stbi_uc block[64], int left, int top)
{
   unsigned sw = z->s->img_x, sh = z->s->img_y, w = z->thingsos_reader_width, h = z->thingsos_reader_height;
   unsigned x0 = ((unsigned)left * w + sw - 1) / sw, x1 = ((unsigned)(left + 8) * w + sw - 1) / sw;
   unsigned y0 = ((unsigned)top * h + sh - 1) / sh, y1 = ((unsigned)(top + 8) * h + sh - 1) / sh;
   if (x1 > w) x1 = w;
   if (y1 > h) y1 = h;
   for (unsigned y = y0; y < y1; ++y) for (unsigned x = x0; x < x1; ++x)
      z->thingsos_reader_pixels[(size_t)y * w + x] = block[((uint64_t)y * sh / h - top) * 8 + ((uint64_t)x * sw / w - left)];
}
static short* thingsos_jpeg_block(stbi__jpeg* z, int component, int block, short scratch[64])
{
   int k;
   if (z->img_comp[component].thingsos_coeff_pages) return (short*)((void**)z->img_comp[component].raw_coeff)[block / 256] + (block % 256) * 64;
   if (!z->thingsos_luma || component == 0) return z->img_comp[component].coeff + 64 * block;
   {
      uint64_t flags = ((uint64_t*)z->img_comp[component].coeff)[block];
      for (k = 0; k < 64; ++k) scratch[k] = (short)((flags >> k) & 1);
   }
   return scratch;
}
static void thingsos_jpeg_store_block(stbi__jpeg* z, int component, int block, const short scratch[64])
{
   if (z->thingsos_luma && component > 0) {
      int k; uint64_t flags = 0;
      for (k = 0; k < 64; ++k) if (scratch[k]) flags |= (uint64_t)1 << k;
      ((uint64_t*)z->img_comp[component].coeff)[block] = flags;
   }
}
'''
    replace('static int stbi__parse_entropy_coded_data(stbi__jpeg *z)', helpers+'\nstatic int stbi__parse_entropy_coded_data(stbi__jpeg *z)',1)
    start=s.index('static int stbi__parse_entropy_coded_data(')
    end=s.index('static void stbi__jpeg_dequantize(',start)
    body=s[start:end]
    before='short *data = z->img_comp[n].coeff + 64 * (i + j * z->img_comp[n].coeff_w);'
    assert before in body
    body=body.replace(before,'short scratch[64];\n               int block = i + j * z->img_comp[n].coeff_w;\n               short *data = thingsos_jpeg_block(z, n, block, scratch);')
    body=body.replace('               // every data block is an MCU, so countdown the restart interval\n               if (--z->todo <= 0) {\n                  if (z->code_bits < 24) stbi__grow_buffer_unsafe(z);\n                  if (!STBI__RESTART(z->marker)) return 1;',
                      '               thingsos_jpeg_store_block(z, n, block, data);\n               // every data block is an MCU, so countdown the restart interval\n               if (--z->todo <= 0) {\n                  if (z->code_bits < 24) stbi__grow_buffer_unsafe(z);\n                  if (!STBI__RESTART(z->marker)) return 1;')
    before='short *data = z->img_comp[n].coeff + 64 * (x2 + y2 * z->img_comp[n].coeff_w);'
    assert before in body
    body=body.replace(before,'short scratch[64];\n                        int block = x2 + y2 * z->img_comp[n].coeff_w;\n                        short *data = thingsos_jpeg_block(z, n, block, scratch);')
    before='''                        if (!stbi__jpeg_decode_block_prog_dc(z, data, &z->huff_dc[z->img_comp[n].hd], n))
                           return 0;'''
    assert before in body
    body=body.replace(before,before+'\n                        thingsos_jpeg_store_block(z, n, block, data);')
    s=s[:start]+body+s[end:]
    replace('STBI_FREE(z->img_comp[i].raw_coeff);', 'thingsos_jpeg_free_coeff(z, i);')
    start=s.index('static void stbi__jpeg_finish(')
    end=s.index('static int stbi__process_marker(',start)
    body=s[start:end].replace('         for (j=0; j < h; ++j) {', '         if (z->thingsos_luma && n > 0) continue;\n         for (j=0; j < h; ++j) {')
    body=body.replace('short *data = z->img_comp[n].coeff + 64 * (i + j * z->img_comp[n].coeff_w);',
                      'short scratch[64];\n               short *data = thingsos_jpeg_block(z, n, i + j * z->img_comp[n].coeff_w, scratch);')
    before='z->idct_block_kernel(z->img_comp[n].data+z->img_comp[n].w2*j*8+i*8, z->img_comp[n].w2, data);'
    assert before in body
    body=body.replace(before, 'if (z->thingsos_reader_pixels && z->thingsos_luma) {\n'
                             '                  stbi_uc block[64]; z->idct_block_kernel(block, 8, data);\n'
                             '                  thingsos_jpeg_emit(z, block, i * 8, j * 8);\n'
                             '               } else '+before)
    s=s[:start]+body+s[end:]
    replace('''   if (j->progressive)
      stbi__jpeg_finish(j);
   return 1;''','''   if (j->progressive) {
      stbi__jpeg_finish(j);
      for (m = 0; m < j->s->img_n; ++m) {
         thingsos_jpeg_free_coeff(j, m);
         j->img_comp[m].raw_coeff = NULL;
         j->img_comp[m].coeff = NULL;
      }
   }
   return 1;''',1)
    p=Path(output);p.parent.mkdir(parents=True,exist_ok=True)
    if not p.exists() or p.read_text(encoding='utf-8')!=s:p.write_text(s,encoding='utf-8')

if __name__=='__main__':generate(*sys.argv[1:])
