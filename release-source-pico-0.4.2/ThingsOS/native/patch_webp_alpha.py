"""Generate a bounded ALPH history derivative; pinned libwebp stays pristine."""
from pathlib import Path
import sys

def generate(source, output):
    text = Path(source).read_text(encoding='utf-8')
    start = text.index('static int DecodeImageData(')
    end = text.index('// -----------------------------------------------------------------------------\n// VP8LTransform', start)
    decoder = text[start:end]
    replacements = {
        'static int DecodeImageData(VP8LDecoder* const dec, uint32_t* const data,': 'static int ThingsAlphaDecode(VP8LDecoder* const dec, ThingsAlphaHistory* const data,',
        'uint32_t* src = data + dec->last_pixel;': 'int src = dec->last_pixel;\n  uint32_t pixel = 0;',
        'uint32_t* last_cached = src;': 'int last_cached = src;',
        'uint32_t* const src_end = data + width * height;': 'const int src_end = width * height;',
        'uint32_t* const src_last = data + width * last_row;': 'const int src_last = width * last_row;',
        'src - data': 'src',
        '*src =': 'pixel =',
        'ReadPackedSymbols(htree_group, br, src)': 'ReadPackedSymbols(htree_group, br, &pixel)',
        '    AdvanceByOne:\n': '    AdvanceByOne:\n      if (!ThingsAlphaSet(data, src, pixel)) return VP8LSetError(dec, VP8_STATUS_OUT_OF_MEMORY);\n',
        'VP8LColorCacheInsert(color_cache, *last_cached++);': 'VP8LColorCacheInsert(color_cache, ThingsAlphaGet(data, last_cached++));',
        'CopyBlock32b(src, dist, length);': 'for (int i = 0; i < length; ++i) ThingsAlphaCopy(data, src + i, src - dist + i);',
    }
    for before, after in replacements.items():
        assert before in decoder, before
        decoder = decoder.replace(before, after)
    rows_start = text.index('static void ExtractAlphaRows(')
    rows_end = text.index('\nint VP8LDecodeAlphaHeader(', rows_start)
    original_rows = text[rows_start:rows_end]
    rows = original_rows.replace('ExtractAlphaRows(', 'ThingsAlphaRows(')
    rows = rows.replace('const uint32_t* in = dec->pixels + dec->width * cur_row;',
                        'ThingsAlphaHistory* const history = (ThingsAlphaHistory*)dec->pixels;')
    rows = rows.replace('    ApplyInverseTransforms(dec, cur_row, num_rows_to_process, in);',
                        '    for (int i = 0; i < dec->width * num_rows_to_process; ++i)\n'
                        '      history->expanded[i] = ThingsAlphaGet(history, cur_row * dec->width + i);\n'
                        '    ApplyInverseTransforms(dec, cur_row, num_rows_to_process, history->expanded);')
    rows = rows.replace('    in += num_rows_to_process * dec->width;\n', '')
    rows = rows.replace('NUM_ARGB_CACHE_ROWS','1')
    rows = rows.replace('output + width * cur_row', 'output + width * (cur_row - alph_dec->thingsos_window_start)')
    text = text[:rows_start] + original_rows.replace('output + width * cur_row', 'output + width * (cur_row - alph_dec->thingsos_window_start)') + rows + text[rows_end:]
    text = text.replace('    ok = AllocateInternalBuffers32b(dec, alph_dec->width);',
                        '    if ((uint64_t)dec->width * dec->height > 65536) {\n'
                        '      alph_dec->use_8b_decode = 2;\n'
                        '      ok = ThingsAlphaAllocate(dec, alph_dec->width);\n'
                        '    } else {\n'
                        '      ok = AllocateInternalBuffers32b(dec, alph_dec->width);\n'
                        '    }')
    before = 'DecodeImageData(dec, dec->pixels, dec->width, dec->height,\n                      last_row, ExtractAlphaRows);'
    assert before in text
    text = text.replace('return alph_dec->use_8b_decode ?', 'return alph_dec->use_8b_decode == 1 ?')
    text = text.replace('if (!alph_dec->use_8b_decode) WebPInitAlphaProcessing();',
                        'if (alph_dec->use_8b_decode != 1) WebPInitAlphaProcessing();')
    text = text.replace(before,
        '(alph_dec->use_8b_decode == 2 ? ThingsAlphaDecode(dec, (ThingsAlphaHistory*)dec->pixels, dec->width, dec->height,\n'
        '                      last_row, ThingsAlphaRows) : ' + before[:-1] + ');')
    text = text.replace('alph_dec->output + width * first_row',
                        'alph_dec->output + width * (first_row - alph_dec->thingsos_window_start)')
    text = text.replace('  WebPSafeFree(dec->pixels);', '  ThingsAlphaClear(dec);\n  WebPSafeFree(dec->pixels);')
    support = Path(__file__).with_name('src').joinpath('webp_alpha_history.inc').read_text(encoding='utf-8')
    start = text.index('static int DecodeImageData(')
    text = text[:start] + support + '\n' + decoder + '\n' + text[start:]
    output = Path(output)
    output.parent.mkdir(parents=True, exist_ok=True)
    if not output.exists() or output.read_text(encoding='utf-8') != text:
        output.write_text(text, encoding='utf-8')
    # Alpha reconstruction only needs the immediately previous row plus the
    # rows handed to the lossy output hook. Keep full planes for crop/dither
    # cases, which can request nonsequential or whole-image alpha data.
    source_dir = Path(source).parent
    header = (source_dir/'alphai_dec.h').read_text(encoding='utf-8')
    header = header.replace('  int width;\n', '  int thingsos_window, thingsos_window_start;\n  int width;\n', 1)
    header_path = output.parent/'src/dec/alphai_dec.h'
    header_path.parent.mkdir(parents=True, exist_ok=True)
    if not header_path.exists() or header_path.read_text(encoding='utf-8') != header:
        header_path.write_text(header, encoding='utf-8')
    lossless_header = (source_dir/'vp8li_dec.h').read_text(encoding='utf-8')
    lossless_header = lossless_header.replace('struct VP8LDecoder {\n', 'struct VP8LDecoder {\n  int thingsos_packed_alpha;\n')
    lossless_path = header_path.with_name('vp8li_dec.h')
    if not lossless_path.exists() or lossless_path.read_text(encoding='utf-8') != lossless_header:
        lossless_path.write_text(lossless_header, encoding='utf-8')
    alpha = (source_dir/'alpha_dec.c').read_text(encoding='utf-8')
    alpha = alpha.replace('const uint64_t alpha_size = (uint64_t)stride * height;',
        'const int window = !(dec->alpha_data[0] & 0x30) && !io->use_cropping && height > 64;\n'
        '  const uint64_t alpha_size = (uint64_t)stride * (window ? 64 : height);\n'
        '  dec->alph_dec->thingsos_window = window;')
    alpha = alpha.replace('dec->alpha_plane + row * width',
                          'dec->alpha_plane + (row - dec->alph_dec->thingsos_window_start) * width')
    before = '    if (!ALPHDecode(dec, row, num_rows)) goto Error;'
    assert before in alpha
    alpha = alpha.replace(before,
        '    if (dec->alph_dec->thingsos_window) {\n'
        '      ALPHDecoder* const a = dec->alph_dec;\n'
        '      if (num_rows > 63 || row < a->thingsos_window_start || row > a->thingsos_window_start + 64) goto Error;\n'
        '      if (row > 0) {\n'
        '        const uint8_t* previous = dec->alpha_plane + (row - 1 - a->thingsos_window_start) * width;\n'
        '        memmove(dec->alpha_plane, previous, width);\n'
        '        if (a->prev_line) a->prev_line = dec->alpha_plane;\n'
        '        if (dec->alpha_prev_line) dec->alpha_prev_line = dec->alpha_plane;\n'
        '      }\n'
        '      a->thingsos_window_start = row > 0 ? row - 1 : 0;\n'
        '    }\n' + before)
    alpha = alpha.replace('if (dec->is_alpha_decoded) {   // finished?',
                          'if (dec->is_alpha_decoded && !dec->alph_dec->thingsos_window) {   // finished?')
    # The non-window path deletes alph_dec at the end; its origin is zero.
    alpha = alpha.replace('return dec->alpha_plane + (row - dec->alph_dec->thingsos_window_start) * width;',
                          'return dec->alpha_plane + (row - (dec->alph_dec ? dec->alph_dec->thingsos_window_start : 0)) * width;')
    alpha_path = output.with_name('alpha_dec.c')
    if not alpha_path.exists() or alpha_path.read_text(encoding='utf-8') != alpha:
        alpha_path.write_text(alpha, encoding='utf-8')

if __name__ == '__main__':
    generate(*sys.argv[1:])
