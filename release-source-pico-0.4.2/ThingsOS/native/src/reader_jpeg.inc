/* SPDX-License-Identifier: Apache-2.0 */
/* Progressive luma coefficients stay in small PSRAM pages. At the final IDCT
   pass each block is sampled directly into bounded reader output; no full
   source-sized pixel plane or unused chroma plane is needed. */
bool thingsos_decode_progressive_jpeg(const unsigned char* data,size_t bytes,unsigned bw,unsigned bh,
                                      unsigned char** gray,unsigned* width,unsigned* height) {
    if(!data || bytes<4 || bytes>THINGSOS_MEMBER_BYTES || data[0]!=255 || data[1]!=216 ||
       !bw || !bh || !gray || !width || !height)return false;
    stbi__jpeg* decoder=image_alloc(sizeof(*decoder));if(!decoder)return false;
    memset(decoder,0,sizeof(*decoder));
    stbi__context input;stbi__start_mem(&input,data,(int)bytes);
    decoder->s=&input;stbi__setup_jpeg(decoder);
    bool supported=stbi__decode_jpeg_header(decoder,STBI__SCAN_header) && decoder->progressive &&
        input.img_x && input.img_y && (uint64_t)input.img_x*input.img_y<=2u*1024u*1024u &&
        (input.img_n==1 || (input.img_n==3 && decoder->rgb!=3 &&
            !(decoder->app14_color_transform==0 && !decoder->jfif)));
    for(int i=1;supported && i<input.img_n;++i)
        if(decoder->img_comp[i].h>decoder->img_comp[0].h || decoder->img_comp[i].v>decoder->img_comp[0].v)supported=false;
    thingsos_size_t fit;
    if(!supported || !thingsos_image_fit(input.img_x,input.img_y,bw,bh,&fit)) {image_free(decoder);return false;}
    while((uint64_t)fit.width*fit.height>1024u*1024u) {fit.width=(fit.width+1)/2;fit.height=(fit.height+1)/2;}
    bool ok=false;unsigned char* pixels=NULL;
    for(unsigned attempt=0;attempt<4;++attempt) {
        pixels=thingsos_image_buffer((size_t)fit.width*fit.height);
        if(pixels) {
            stbi__start_mem(&input,data,(int)bytes);
            decoder->thingsos_want_gray=1;decoder->thingsos_reader_pixels=pixels;
            decoder->thingsos_reader_width=(int)fit.width;decoder->thingsos_reader_height=(int)fit.height;
            ok=stbi__decode_jpeg_image(decoder)!=0;
            stbi__cleanup_jpeg(decoder);
            if(ok)break;
            free(pixels);pixels=NULL;
        }
        if(fit.width<32 || fit.height<32)break;
        fit.width=(fit.width+1)/2;fit.height=(fit.height+1)/2;
    }
    image_free(decoder);
    if(!ok)return false;
    *gray=pixels;*width=fit.width;*height=fit.height;return true;
}
