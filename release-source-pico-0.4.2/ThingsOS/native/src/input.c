// SPDX-License-Identifier: Apache-2.0
#include "thingsos/input.h"
#include <string.h>
static bool boundary(const char* s, size_t n, size_t at) {
    return at <= n && (at == n || ((unsigned char)s[at] & 192) != 128);
}
static bool utf8(const char* s, size_t n) {
    for (size_t i = 0; i < n;) {
        unsigned char c = (unsigned char)s[i];
        size_t k = c < 128                ? 1
                   : c >= 194 && c <= 223 ? 2
                   : c <= 239 && c >= 224 ? 3
                   : c >= 240 && c <= 244 ? 4
                                          : 0;
        if (!k || i + k > n || c < 32 || c == 127) return false;
        for (size_t j = 1; j < k; j++)
            if (((unsigned char)s[i + j] & 192) != 128) return false;
        if (k == 3 && ((c == 224 && (unsigned char)s[i + 1] < 160) ||
                       (c == 237 && (unsigned char)s[i + 1] >= 160)))
            return false;
        if (k == 4 && ((c == 240 && (unsigned char)s[i + 1] < 144) ||
                       (c == 244 && (unsigned char)s[i + 1] >= 144)))
            return false;
        i += k;
    }
    return true;
}
bool thingsos_input_open(thingsos_input_t* i, const char* text, size_t limit, bool secret) {
    if (!i || !text || limit >= sizeof(i->text) || strlen(text) > limit || !utf8(text, strlen(text)))
        return false;
    memset(i, 0, sizeof(*i));
    strcpy(i->text, text);
    i->length = i->cursor = strlen(text);
    i->limit = limit;
    i->secret = secret;
    return true;
}
bool thingsos_input_insert(thingsos_input_t* i, const char* s) {
    if (!i || !s) return false;
    size_t n = strlen(s);
    if (i->length + n > i->limit || !utf8(s, n) || !boundary(i->text, i->length, i->cursor))
        return false;
    memmove(i->text + i->cursor + n, i->text + i->cursor, i->length - i->cursor + 1);
    memcpy(i->text + i->cursor, s, n);
    i->cursor += n;
    i->length += n;
    return true;
}
void thingsos_input_move(thingsos_input_t* i, int direction) {
    if (!i) return;
    i->candidate_page=0;
    if (direction < 0 && i->cursor) {
        do {
            i->cursor--;
        } while (i->cursor && ((unsigned char)i->text[i->cursor] & 192) == 128);
    } else if (direction > 0 && i->cursor < i->length) {
        do {
            i->cursor++;
        } while (i->cursor < i->length && ((unsigned char)i->text[i->cursor] & 192) == 128);
    }
}
void thingsos_input_backspace(thingsos_input_t* i) {
    if (!i || !i->cursor) return;
    size_t end = i->cursor;
    thingsos_input_move(i, -1);
    memmove(i->text + i->cursor, i->text + end, i->length - end + 1);
    i->length -= end - i->cursor;
}
void thingsos_input_display(const thingsos_input_t* i, char* out, size_t cap) {
    if (!i || !out || !cap) return;
    size_t used = 0;
    for (size_t p = 0; p <= i->length;) {
        if (p == i->cursor && used + 1 < cap) out[used++] = '|';
        if (p == i->length) break;
        size_t end = p + 1;
        while (end < i->length && ((unsigned char)i->text[end] & 192) == 128) end++;
        if (i->secret && !i->reveal) {
            if (used + 1 >= cap) break;
            out[used++] = '*';
        } else {
            if (used + end - p >= cap) break;
            memcpy(out + used, i->text + p, end - p);
            used += end - p;
        }
        p = end;
    }
    out[used] = 0;
}
const char* thingsos_input_key(const thingsos_input_t* i, unsigned index) {
    static const char* lower[] = { "q", "w", "e", "r", "t", "y", "u", "i", "o", "p",
                                   "a", "s", "d", "f", "g", "h", "j", "k", "l", "@",
                                   "z", "x", "c", "v", "b", "n", "m", ".", "_", "-",
                                   "0", "1", "2", "3", "4", "5", "6", "7", "8", "9" };
    static const char* upper[] = { "Q", "W", "E", "R", "T", "Y", "U", "I", "O", "P",
                                   "A", "S", "D", "F", "G", "H", "J", "K", "L", "@",
                                   "Z", "X", "C", "V", "B", "N", "M", ".", "_", "-",
                                   "0", "1", "2", "3", "4", "5", "6", "7", "8", "9" };
    static const char* symbols[] = { "!", "@", "#", "$", "%", "^", "&",  "*",  "(", ")",
                                     "[", "]", "{", "}", "<", ">", "/",  "\\", "|", "~",
                                     "+", "-", "=", "_", ":", ";", "\"", "'",  ",", ".",
                                     "0", "1", "2", "3", "4", "5", "6",  "7",  "8", "9" };
    static const char* actions[] = { "大小写", "符号", "空格", "删除", "←",
                                     "→",      "显隐", "取消", "确认" };
    if (!i || index >= 49) return "";
    if (index >= 40) return actions[index - 40];
    return i->mode == THINGSOS_KEYS_SYMBOLS ? symbols[index]
           : i->mode == THINGSOS_KEYS_UPPER ? upper[index]
                                            : lower[index];
}
int thingsos_input_key_hit(int x, int y) {
    if (x < 36 || x >= 646 || y < 600 || y >= 1044) return -1;
    unsigned row = (unsigned)(y - 600) / 74, col = (unsigned)(x - 36) / 61;
    if ((y - 600) % 74 >= 68 || (x - 36) % 61 >= 55) return -1;
    unsigned index = row * 10 + col;
    return index < 49 ? (int)index : -1;
}
