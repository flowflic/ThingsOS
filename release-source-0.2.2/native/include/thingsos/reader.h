// SPDX-License-Identifier: Apache-2.0
#ifndef THINGSOS_READER_H
#define THINGSOS_READER_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
/* Logical UTF-8 layout before font-metric integration. Offsets are source bytes. */
typedef enum {
    THINGSOS_TEXT_OK,
    THINGSOS_TEXT_INVALID,
    THINGSOS_TEXT_CAPACITY
} thingsos_text_result_t;
thingsos_text_result_t thingsos_txt_pages(
    const unsigned char* text, size_t length, unsigned columns, unsigned rows, size_t* starts,
    size_t capacity, size_t* count);
/* Decoder-independent 8-bit grayscale plane. Caller owns buffers. */
typedef struct {
    const uint8_t* pixels;
    uint32_t width, height, stride;
    size_t bytes;
} thingsos_gray_t;
typedef struct {
    uint32_t width, height;
} thingsos_size_t;
bool thingsos_image_fit(
    uint32_t width, uint32_t height, uint32_t box_width, uint32_t box_height, thingsos_size_t* out);
/* Fit the whole image into the box, scaling up when both dimensions allow it. */
bool thingsos_image_contain(
    uint32_t width, uint32_t height, uint32_t box_width, uint32_t box_height, thingsos_size_t* out);
bool thingsos_image_fit_width(uint32_t,uint32_t,uint32_t,thingsos_size_t*);
bool thingsos_image_resize(
    const thingsos_gray_t* source, uint8_t* destination, size_t destination_bytes, uint32_t width,
    uint32_t height);
#endif
