/*
 * LHE Basic decoder
 */

#include "bytestream.h"
#include "internal.h"
#include "lhebasic.h"

typedef struct LheState {
    AVClass *class;  
    LheBasicPrec prec;
    AVFrame * frame;
    GetBitContext gb;
} LheState;



static av_cold int lhe_decode_init(AVCodecContext *avctx)
{
    LheState *s = avctx->priv_data;
    avctx->pix_fmt = AV_PIX_FMT_YUV422P;

    s->frame = av_frame_alloc();
    if (!s->frame)
        return AVERROR(ENOMEM);
    
    lhe_init_cache(&s->prec);
    
    return 0;
}

static void lhe_read_huffman_table (LheState *s, LheHuffEntry *he) 
{   
    int i;
    uint8_t len;

    
    for (i=0; i< LHE_MAX_HUFF_SIZE; i++) 
    {
        len = get_bits(&s->gb, LHE_HUFFMAN_NODE_BITS); 
        if (len==15) len=255; //If symbol does not have any occurence, encoder assigns 255 length. As each table slot has 4 bits, this is 15 in the file.
        he[i].len = len;
        he[i].sym = i; 
    }  
    
    lhe_generate_huffman_codes(he);
       
}

static uint8_t lhe_translate_huffman_into_symbol (int huffman_symbol, LheHuffEntry *he, int pix) 
{
    uint8_t symbol;
    
    symbol = NO_SYMBOL;
    
    if (huffman_symbol == he[SYM_0].code)
    {
        symbol = SYM_0;
    } 
    else if (huffman_symbol == he[SYM_1].code)
    {
        symbol = SYM_1;
    } 
    else if (huffman_symbol == he[SYM_2].code)
    {
        symbol = SYM_2;
    } 
    else if (huffman_symbol == he[SYM_3].code)
    {
        symbol = SYM_3;
    } 
    else if (huffman_symbol == he[SYM_4].code)
    {
        symbol = SYM_4;
    }
    else if (huffman_symbol == he[SYM_5].code)
    {
        symbol = SYM_5;
    }
    else if (huffman_symbol == he[SYM_6].code)
    {
        symbol = SYM_6;
    }
    else if (huffman_symbol == he[SYM_7].code)
    {
        symbol = SYM_7;
    } 
    else if (huffman_symbol == he[SYM_8].code)
    {
        symbol = SYM_8;
    } 
    else if (huffman_symbol == he[SYM_9].code)
    {
        symbol = SYM_9;       
    }
    
    return symbol;
    
}

static void lhe_read_file_symbols (LheState *s, LheHuffEntry *he, uint32_t image_size, uint8_t *symbols) 
{
    uint8_t bit, symbol;
    int i, huffman_symbol;
    uint32_t decoded_symbols;
    
    decoded_symbols = 0;
    huffman_symbol = 0;
    bit = 1;
    
    while (decoded_symbols<image_size) {
        
        huffman_symbol = (huffman_symbol<<1) | get_bits(&s->gb, 1); 
        symbol = lhe_translate_huffman_into_symbol(huffman_symbol, he, decoded_symbols);        
        
        if (symbol != NO_SYMBOL) 
        {
            symbols[decoded_symbols] = symbol;
            decoded_symbols++;
            huffman_symbol = 0;
        } 
    }
}

static void lhe_translate_symbol_into_hop (uint8_t * symbols, uint8_t *hops, int pix, int width) {
    uint8_t symbol, hop;
    bool hop_found;
    
    hop_found = false;
    
    symbol = symbols[pix];
    

    if (symbol == SYM_0) 
    {
        hop = HOP_0;
        hop_found = true;
    } else 
    {
        symbol-= HOP_0_CHECK;
    }
    
    if (!hop_found && pix>width && symbol == 0)
    {
        hop = hops[pix-width];
        hop_found = true;
    } else if (!hop_found && pix>width)
    {
        symbol-=HOP_UP_CHECK;
    }
    
    if (!hop_found) {
        
        if (symbol - HOP_POS_1_CHECK == 0)
        {
            hop = HOP_POS_1;
        } else if (symbol - HOP_NEG_1_CHECK == 0) 
        {
            hop = HOP_NEG_1;

        } else if (symbol - HOP_POS_2_CHECK == 0) 
        {
            hop = HOP_POS_2;

        } else if (symbol - HOP_NEG_2_CHECK == 0) 
        {
            hop = HOP_NEG_2;

        } else if (symbol - HOP_POS_3_CHECK == 0) 
        {
            hop = HOP_POS_3;

        } else if (symbol - HOP_NEG_3_CHECK == 0) 
        {
            hop = HOP_NEG_3;

        } else if (symbol - HOP_POS_4_CHECK == 0) 
        {
            hop = HOP_POS_4;

        } else if (symbol - HOP_NEG_4_CHECK == 0) 
        {
            hop = HOP_NEG_4;

        }
    }
    
    hops[pix] = hop;   

}

static void lhe_translate_symbols_into_hops (uint8_t * symbols, uint8_t *hops, int width, int image_size) {
    int pix;
    for (pix=0; pix<image_size; pix++) 
    {
        lhe_translate_symbol_into_hop(symbols, hops, pix, width);
    }
}


static void lhe_decode_one_hop_per_pixel_block (LheBasicPrec *prec, uint8_t *hops, uint8_t *image,
                                                uint32_t width, uint32_t height, int linesize,
                                                uint8_t *first_color_block, int total_blocks_width,
                                                int block_x, int block_y,
                                                int block_width, int block_height) 
{
       
    //Hops computation.
    int xini, xfin, yini, yfin;
    bool small_hop, last_small_hop;
    uint8_t hop, predicted_luminance, hop_1, r_max; 
    int pix, dif_pix, dif_hops, num_block;
    
    //Errors
    int min_error;      // error of predicted signal
    int error;          //computed error for each hop 
    
    num_block = block_y * total_blocks_width + block_x;
    
    xini = block_x * block_width;
    xfin = xini + block_width;
    if (xfin>width) 
    {
        xfin = width;
    }
    yini = block_y * block_height;
    yfin = yini + block_height;
    if (yfin>height)
    {
        yfin = height;
    }
    
    small_hop           = false;
    last_small_hop      = false;        // indicates if last hop is small
    predicted_luminance = 0;            // predicted signal
    hop_1               = START_HOP_1;
    pix                 = 0;            // pixel possition, from 0 to image size        
    r_max               = PARAM_R;        
    
 
    pix = yini*linesize + xini; 
    hops += (yini*width + xini);
    dif_pix = linesize - xfin + xini;
    dif_hops = width - xfin + xini;
    
    for (int y=yini; y < yfin; y++)  {
        for (int x=xini; x < xfin; x++)     {
            
            hop = *hops++; 
  
             if ((y>yini) &&(x>xini) && x!=xfin-1)
            {
                predicted_luminance=(image[pix-1]+image[pix+1-linesize])>>1;     
            } 
            else if ((x==xini) && (y>yini))
            {
                predicted_luminance=image[pix-linesize];
                last_small_hop=false;
                hop_1=START_HOP_1;
            } 
            else if ((x==xfin-1) && (y>yini)) 
            {
                predicted_luminance=(image[pix-1]+image[pix-linesize])>>1;                               
            } 
            else if (y==yini && x>xini) 
            {
                predicted_luminance=image[pix-1];
            }
            else if (x==xini && y==yini) {  
                predicted_luminance=first_color_block[num_block];//first pixel always is perfectly predicted! :-)  
            }   
            
            //assignment of component_prediction
            //This is the uncompressed image
            image[pix]= prec -> prec_luminance[predicted_luminance][r_max][hop_1][hop];
            
            //tunning hop1 for the next hop ( "h1 adaptation")
            //------------------------------------------------
            small_hop = (hop<=HOP_POS_1 && hop>=HOP_NEG_1) ;


            if((small_hop) && (last_small_hop))  {

                if (hop_1>MIN_HOP_1) {
                    hop_1--;
                } 
            
            } else {
                hop_1=MAX_HOP_1;
            }

            //lets go for the next pixel
            //--------------------------
            pix++;
            last_small_hop=small_hop;     
        }// for x
        pix+=dif_pix;
        hops+=dif_hops;
    }// for y
    
}

static void lhe_decode_one_hop_per_pixel (LheBasicPrec *prec, uint8_t *hops, uint8_t *image,
                                          uint8_t first_color, uint32_t width, uint32_t height, 
                                          int linesize) {
       
    //Hops computation.
    bool small_hop, last_small_hop;
    uint8_t hop, predicted_luminance, hop_1, r_max; 
    int pix, pix_hop_data, dif_pix;
    
    //Errors
    int min_error;      // error of predicted signal
    int error;          //computed error for each hop 
    
    small_hop           = false;
    last_small_hop      = false;        // indicates if last hop is small
    predicted_luminance = 0;            // predicted signal
    hop_1               = START_HOP_1;
    pix                 = 0;            // pixel possition, from 0 to image size        
    r_max               = PARAM_R;        
    
    dif_pix = linesize - width;
 
    for (int y=0; y < height; y++)  {
        for (int x=0; x < width; x++)     {
            
            hop = *hops++; 
       
            if (y>0 && x>0 && x!=width-1)
            {
                predicted_luminance=(image[pix-1]+image[pix+1-linesize])>>1;     
            } 
            else if ((x==0) && (y>0))
            {
                predicted_luminance=image[pix-linesize];
                last_small_hop=false;
                hop_1=START_HOP_1;
            } 
            else if ((x==width-1) && (y>0)) 
            {
                predicted_luminance=(image[pix-1]+image[pix-linesize])>>1;                               
            } 
            else if (y==0 && x>0) 
            {
                predicted_luminance=image[pix-1];
            }
            else  
            {  
                predicted_luminance=first_color;//first pixel always is perfectly predicted! :-)  
            }   
            
            //assignment of component_prediction
            //This is the uncompressed image
            image[pix]= prec -> prec_luminance[predicted_luminance][r_max][hop_1][hop];
            
            //tunning hop1 for the next hop ( "h1 adaptation")
            //------------------------------------------------
            small_hop=false;
            if (hop<=HOP_POS_1 && hop>=HOP_NEG_1) 
            {
                small_hop=true;// 4 is in the center, 4 is null hop
            }
            else 
            {
                small_hop=false;    
            }

           if( (small_hop) && (last_small_hop))  {
                hop_1=hop_1-1;
                if (hop_1<MIN_HOP_1) {
                    hop_1=MIN_HOP_1;
                } 
                
            } else {
                hop_1=MAX_HOP_1;
            }

            //lets go for the next pixel
            //--------------------------
            last_small_hop=small_hop;     
            pix++;
        }// for x
        pix+=dif_pix;
    }// for y
    
}

static void lhe_decode_frame_sequential (LheBasicPrec *prec, 
                                         uint8_t *component_Y, uint8_t *component_U, uint8_t *component_V,
                                         uint8_t *hops_Y, uint8_t *hops_U, uint8_t *hops_V,
                                         int width_Y, int height_Y, int width_UV, int height_UV, 
                                         int linesize_Y, int linesize_U, int linesize_V, 
                                         uint8_t *first_color_block_Y, uint8_t *first_color_block_U, uint8_t *first_color_block_V) 
{
    //Luminance
    lhe_decode_one_hop_per_pixel(prec, hops_Y, component_Y, first_color_block_Y[0], width_Y, height_Y, linesize_Y);

    //Chrominance U
    lhe_decode_one_hop_per_pixel(prec, hops_U, component_U, first_color_block_U[0], width_UV, height_UV, linesize_U);

    //Chrominance V
    lhe_decode_one_hop_per_pixel(prec, hops_V, component_V, first_color_block_V[0], width_UV, height_UV, linesize_V);
}


static void lhe_decode_frame_pararell (LheBasicPrec *prec, 
                                       uint8_t *component_Y, uint8_t *component_U, uint8_t *component_V,
                                       uint8_t *hops_Y, uint8_t *hops_U, uint8_t *hops_V,
                                       int width_Y, int height_Y, int width_UV, int height_UV, 
                                       int linesize_Y, int linesize_U, int linesize_V, 
                                       uint8_t *first_color_block_Y, uint8_t *first_color_block_U, uint8_t *first_color_block_V,
                                       int total_blocks_width, int total_blocks_height) 
{
    
    #pragma omp parallel for
    for (int j=0; j<total_blocks_height; j++)      
    {  
        for (int i=0; i<total_blocks_width; i++) 
        {
            
            //Luminance
            lhe_decode_one_hop_per_pixel_block(prec, hops_Y, component_Y, 
                                                width_Y, height_Y, linesize_Y, 
                                                first_color_block_Y, total_blocks_width, 
                                                i, j, BLOCK_WIDTH_Y, BLOCK_HEIGHT_Y);

            //Chrominance U
            lhe_decode_one_hop_per_pixel_block(prec, hops_U, component_U, 
                                                width_UV, height_UV, linesize_U,
                                                first_color_block_U, total_blocks_width, 
                                                i, j, BLOCK_WIDTH_UV, BLOCK_HEIGHT_UV);
        
            //Chrominance V
            lhe_decode_one_hop_per_pixel_block(prec, hops_V, component_V, 
                                            width_UV, height_UV, linesize_V,
                                            first_color_block_V, total_blocks_width, 
                                            i, j, BLOCK_WIDTH_UV, BLOCK_HEIGHT_UV);
        }
    }
}

static int lhe_decode_frame(AVCodecContext *avctx, void *data, int *got_frame, AVPacket *avpkt)
{
    uint32_t width_Y, width_UV, height_Y, height_UV, image_size_Y, image_size_UV;
    uint8_t *component_Y, *component_U, *component_V, *hops_Y, *hops_U, *hops_V;
    uint8_t *symbols_Y, *symbols_U, *symbols_V;
    uint8_t *first_color_block_Y, *first_color_block_U, *first_color_block_V;
    int total_blocks, total_blocks_width, total_blocks_height;
    int ret, i,j;

    LheHuffEntry he_Y[LHE_MAX_HUFF_SIZE];
    LheHuffEntry he_U[LHE_MAX_HUFF_SIZE];
    LheHuffEntry he_V[LHE_MAX_HUFF_SIZE];
   
    LheState *s = avctx->priv_data;
    
    const uint8_t *lhe_data = avpkt->data;

    width_Y  = bytestream_get_le32(&lhe_data);
    height_Y = bytestream_get_le32(&lhe_data);
    image_size_Y = width_Y * height_Y;

    width_UV = (width_Y - 1)/CHROMA_FACTOR_WIDTH + 1;
    height_UV = (height_Y - 1)/CHROMA_FACTOR_HEIGHT + 1;
    image_size_UV = width_UV * height_UV;
    
    avctx->width  = width_Y;
    avctx->height  = height_Y;    
    
    //Allocates frame
    if ((ret = ff_get_buffer(avctx, s->frame, 0)) < 0)
        return ret;
    
    //Blocks
    total_blocks_width = bytestream_get_byte(&lhe_data); 
    total_blocks_height = bytestream_get_byte(&lhe_data); 

    total_blocks = total_blocks_height * total_blocks_width;
    
    //First pixel array
    first_color_block_Y = malloc(sizeof(uint8_t) * image_size_Y);
    first_color_block_U = malloc(sizeof(uint8_t) * image_size_UV);
    first_color_block_V = malloc(sizeof(uint8_t) * image_size_UV);
    
    for (i=0; i<total_blocks; i++) 
    {
        first_color_block_Y[i] = bytestream_get_byte(&lhe_data); 
    }

    
    for (i=0; i<total_blocks; i++) 
    {
        first_color_block_U[i] = bytestream_get_byte(&lhe_data); 
    }
    
        
    for (i=0; i<total_blocks; i++) 
    {
        first_color_block_V[i] = bytestream_get_byte(&lhe_data); 
    }

    //Pointers to different color components
    component_Y = s->frame->data[0];
    component_U = s->frame->data[1];
    component_V = s->frame->data[2];
    
    //Symbols array
    symbols_Y = malloc(sizeof(uint8_t) * image_size_Y);
    symbols_U = malloc(sizeof(uint8_t) * image_size_UV);
    symbols_V = malloc(sizeof(uint8_t) * image_size_UV);
    
    hops_Y = malloc(sizeof(uint8_t) * image_size_Y);      
    hops_U = malloc(sizeof(uint8_t) * image_size_UV);    
    hops_V = malloc(sizeof(uint8_t) * image_size_UV);      
           
    init_get_bits(&s->gb, lhe_data, avpkt->size * 8);

    lhe_read_huffman_table(s, he_Y);
    lhe_read_huffman_table(s, he_U);
    lhe_read_huffman_table(s, he_V);

    lhe_read_file_symbols(s, he_Y, image_size_Y, symbols_Y);
    lhe_read_file_symbols(s, he_U, image_size_UV, symbols_U);
    lhe_read_file_symbols(s, he_V, image_size_UV, symbols_V);

    //Translate into hops
    lhe_translate_symbols_into_hops(symbols_Y, hops_Y, width_Y, image_size_Y);
    lhe_translate_symbols_into_hops(symbols_U, hops_U, width_UV, image_size_UV);
    lhe_translate_symbols_into_hops(symbols_V, hops_V, width_UV, image_size_UV);
    
    if (total_blocks > 1 && OPENMP_FLAGS == CONFIG_OPENMP) 
    {
        lhe_decode_frame_pararell (&s->prec, 
                                   component_Y, component_U, component_V, 
                                   hops_Y, hops_U, hops_V,
                                   width_Y, height_Y, width_UV, height_UV, 
                                   s->frame->linesize[0], s->frame->linesize[1], s->frame->linesize[2],
                                   first_color_block_Y, first_color_block_U, first_color_block_V,
                                   total_blocks_width, total_blocks_height);
       
    } else 
    {      
        lhe_decode_frame_sequential (&s->prec, 
                                     component_Y, component_U, component_V, 
                                     hops_Y, hops_U, hops_V,
                                     width_Y, height_Y, width_UV, height_UV, 
                                     s->frame->linesize[0], s->frame->linesize[1], s->frame->linesize[2],
                                     first_color_block_Y, first_color_block_U, first_color_block_V);    
    }
    
    av_log(NULL, AV_LOG_INFO, "DECODING...Width %d Height %d \n", width_Y, height_Y);

    if ((ret = av_frame_ref(data, s->frame)) < 0)
        return ret;
    
    *got_frame = 1;

    return 0;
}


static av_cold int lhe_decode_close(AVCodecContext *avctx)
{
    LheState *s = avctx->priv_data;

    av_freep(&s->prec.prec_luminance);
    av_freep(&s->prec.best_hop);
    
    av_frame_free(&s->frame);

    return 0;
}

static const AVClass decoder_class = {
    .class_name = "lhe decoder",
    .item_name  = av_default_item_name,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_DECODER,
};

AVCodec ff_lhe_decoder = {
    .name           = "lhe",
    .long_name      = NULL_IF_CONFIG_SMALL("LHE"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_LHE,
    .priv_data_size = sizeof(LheState),
    .init           = lhe_decode_init,
    .close          = lhe_decode_close,
    .decode         = lhe_decode_frame,
    .priv_class     = &decoder_class,
};
