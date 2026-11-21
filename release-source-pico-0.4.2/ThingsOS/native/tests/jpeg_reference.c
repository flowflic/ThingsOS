/* SPDX-License-Identifier: Apache-2.0 */
/* Compile the pinned, unmodified stb decoder as an independent pixel oracle. */
#define STB_IMAGE_STATIC
#define STBI_ONLY_JPEG
#define STBI_NO_STDIO
#define STBI_NO_HDR
#define STBI_NO_LINEAR
#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
unsigned char* jpeg_reference(const unsigned char* bytes,int n,int* w,int* h) {
    int components;return stbi_load_from_memory(bytes,n,w,h,&components,1);
}
