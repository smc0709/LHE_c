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
    avctx->pix_fmt = AV_PIX_FMT_YUV444P;

    s->frame = av_frame_alloc();
    if (!s->frame)
        return AVERROR(ENOMEM);
    
    lhe_init_cache(&s->prec);
    
    return 0;
}

static void lhe_read_huffman_table (LheState *s, int *huffman) 
{   
    int i, code;
    uint8_t symbol;    

    
    for (i=0; i< LHE_MAX_HUFF_SIZE; i++) 
    {
        symbol = get_bits(&s->gb, LHE_HUFFMAN_NODE_BITS); 

        if (i==0)
        {
            code = 0;
        } else if (i == LHE_MAX_HUFF_SIZE-1)
        {
            code = code + 1;
        } else 
        {
            code = code * 10 + 10;
        }
        
        huffman[symbol] = code;
        
    }  
    
}

static uint8_t lhe_translate_huffman_into_symbol (int huffman_symbol, int *huffman) 
{
    uint8_t symbol;
    
    if (huffman_symbol == huffman[SYM_HOP_O])
    {
        symbol = SYM_HOP_O;
    } 
    else if (huffman_symbol == huffman[SYM_HOP_UP])
    {
        symbol = SYM_HOP_UP;
    } 
    else if (huffman_symbol == huffman[SYM_HOP_POS_1])
    {
        symbol = SYM_HOP_POS_1;
    } 
    else if (huffman_symbol == huffman[SYM_HOP_NEG_1])
    {
        symbol = SYM_HOP_NEG_1;
    } 
    else if (huffman_symbol == huffman[SYM_HOP_POS_2])
    {
        symbol = SYM_HOP_POS_2;
    }
    else if (huffman_symbol == huffman[SYM_HOP_NEG_2])
    {
        symbol = SYM_HOP_NEG_2;
    }
    else if (huffman_symbol == huffman[SYM_HOP_POS_3])
    {
        symbol = SYM_HOP_POS_3;
    }
    else if (huffman_symbol == huffman[SYM_HOP_NEG_3])
    {
        symbol = SYM_HOP_NEG_3;
    } 
    else if (huffman_symbol == huffman[SYM_HOP_POS_4])
    {
        symbol = SYM_HOP_POS_4;
    } 
    else if (huffman_symbol == huffman[SYM_HOP_NEG_4])
    {
        symbol = SYM_HOP_NEG_4;       
    }
    
    
    return symbol;
    
}

static void lhe_read_file_symbols (LheState *s, uint32_t image_size, int *huffman, uint8_t *symbols) 
{
    uint8_t bit;
    int i, huffman_symbol;
    uint32_t decoded_symbols;
    
    decoded_symbols = 0;
    huffman_symbol = 0;
    
    while (decoded_symbols<image_size) {
        
        bit = get_bits(&s->gb, 1); 
                 
        
        if (bit == 1 && count_bits(huffman_symbol) == LHE_MAX_BITS-1) 
        {
            symbols[decoded_symbols] = lhe_translate_huffman_into_symbol(huffman_symbol*10 + 1, huffman);
            huffman_symbol = 0;
            decoded_symbols++;
        }
        else if (bit == 1) 
        {
            huffman_symbol = huffman_symbol * 10 + 1;
        } else 
        {
            symbols[decoded_symbols] = lhe_translate_huffman_into_symbol(huffman_symbol*10, huffman);
            huffman_symbol = 0;
            decoded_symbols++;
        }
        
    }  
}

static uint8_t lhe_translate_symbol_into_hop (uint8_t * symbols, uint8_t *hops, int pix, int pix_size, int width) {
    uint8_t symbol, hop;
    
    symbol = symbols[pix];

    switch (symbol) {
        case SYM_HOP_O:
            hop = HOP_0;
            break;
        case SYM_HOP_UP:
            if (pix > width) {
                hop = hops [pix_size * (pix-width)];
            }
            break;
        case SYM_HOP_POS_1:
            hop = HOP_POS_1;
            break;
        case SYM_HOP_NEG_1:
            hop = HOP_NEG_1;
            break;
        case SYM_HOP_POS_2:
            hop = HOP_POS_2;
            break;
        case SYM_HOP_NEG_2:
            hop = HOP_NEG_2;
            break;
        case SYM_HOP_POS_3:
            hop = HOP_POS_3;
            break;
        case SYM_HOP_NEG_3:
            hop = HOP_NEG_3;
            break;
        case SYM_HOP_POS_4:
            hop = HOP_POS_4;
            break;
        case SYM_HOP_NEG_4:
            hop = HOP_NEG_4;        
            break;
    }
    
    hops[pix] = hop;
    
    return hop;
}

static void lhe_decode_one_hop_per_pixel (LheBasicPrec *prec, uint8_t *hops, uint8_t *image,
                                          uint8_t *symbols, uint8_t first_color, 
                                          uint32_t width, uint32_t height, int pix_size) {
       
    //Hops computation.
    bool small_hop, last_small_hop;
    uint8_t hop, predicted_luminance, hop_1, r_max; 
    int pix;
    
    //Errors
    int min_error;      // error of predicted signal
    int error;          //computed error for each hop 
    
    small_hop           = false;
    last_small_hop      = false;        // indicates if last hop is small
    predicted_luminance = 0;            // predicted signal
    hop_1               = START_HOP_1;
    pix                 = 0;            // pixel possition, from 0 to image size        
    r_max               = PARAM_R;        
    
 
    for (int y=0; y < height; y++)  {
        for (int x=0; x < width; x++)     {
            
            hop = lhe_translate_symbol_into_hop(symbols, hops, pix, pix_size, width);
       
            if ((y>0) &&(x>0) && x!=width-1)
            {
                predicted_luminance=(4*image[pix_size * (pix-1)]+3*image[pix_size * (pix+1-width)])/7;     
            } 
            else if ((x==0) && (y>0))
            {
                predicted_luminance=image[pix_size * (pix-width)];
                last_small_hop=false;
                hop_1=START_HOP_1;
            } 
            else if ((x==width-1) && (y>0)) 
            {
                predicted_luminance=(4*image[pix_size * (pix-1)]+2*image[pix_size * (pix-width)])/6;                               
            } 
            else if (y==0 && x>0) 
            {
                predicted_luminance=image[pix_size * (x-1)];
            }
            else if (x==0 && y==0) {  
                predicted_luminance=first_color;//first pixel always is perfectly predicted! :-)  
            }   
            
            if (predicted_luminance>255) 
            {
                predicted_luminance=255;
            }
            
            if (predicted_luminance<0) 
            {
                predicted_luminance=0;  
            }
            
            //assignment of component_prediction
            //This is the uncompressed image
            image[pix_size * pix]= prec -> prec_luminance[hop_1][predicted_luminance][r_max][hop];
            
            if (MIDDLE_VALUE) 
            {
                image[pix_size * pix]=prec -> prec_luminance_center[hop_1][predicted_luminance][r_max][hop];

            } else 
            {
                image[pix_size * pix]=prec -> prec_luminance[hop_1][predicted_luminance][r_max][hop];
            }
            
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
    }// for y
    
}


static int lhe_decode_frame(AVCodecContext *avctx, void *data, int *got_frame, AVPacket *avpkt)
{
    int i;
    uint32_t width, height, image_size, n_bytes;
    uint8_t *component_Y, *component_U, *component_V, *hops;
    uint8_t *symbols_Y, *symbols_U, *symbols_V;
    int *huffman_Y, *huffman_U, *huffman_V;
    uint8_t first_pixel_Y, first_pixel_U, first_pixel_V;
    int ret;
    
    LheState *s = avctx->priv_data;
    
    const uint8_t *lhe_data = avpkt->data;

    width  = bytestream_get_le32(&lhe_data);
    height = bytestream_get_le32(&lhe_data);
    image_size = width * height;
    
    av_log(NULL, AV_LOG_INFO, "DECODING...Width %d Height %d \n", width, height);

    first_pixel_Y = bytestream_get_byte(&lhe_data); 
    first_pixel_U = bytestream_get_byte(&lhe_data); 
    first_pixel_V = bytestream_get_byte(&lhe_data); 

    n_bytes = bytestream_get_le32(&lhe_data); 
    
    avctx->width  = width;
    avctx->height  = height;    
    
    //Allocates frame
    if ((ret = ff_get_buffer(avctx, s->frame, 0)) < 0)
        return ret;

    const int pix_size = s->frame->linesize[0]/ width;
    
    //Pointers to different color components
    component_Y = s->frame->data[0];
    component_U = s->frame->data[1];
    component_V = s->frame->data[2];
    
    //Symbols array
    symbols_Y = malloc(sizeof(uint8_t) * image_size);
    symbols_U = malloc(sizeof(uint8_t) * image_size);
    symbols_V = malloc(sizeof(uint8_t) * image_size);

    //Huffman array 
    huffman_Y = malloc(sizeof(int) * LHE_MAX_HUFF_SIZE);
    huffman_U = malloc(sizeof(int) * LHE_MAX_HUFF_SIZE);
    huffman_V = malloc(sizeof(int) * LHE_MAX_HUFF_SIZE);
    
    hops = malloc(sizeof(uint8_t) * image_size);
        
    n_bytes = n_bytes - 
            (sizeof(width) + sizeof(height) //width and height
            + sizeof(first_pixel_Y) + sizeof(first_pixel_U) + sizeof(first_pixel_V) //first pixel value
            + sizeof (n_bytes));
            
    init_get_bits(&s->gb, lhe_data, n_bytes * 8);

    lhe_read_huffman_table(s, huffman_Y);
    lhe_read_huffman_table(s, huffman_U);
    lhe_read_huffman_table(s, huffman_V);
    get_bits(&s->gb, LHE_HUFFMAN_TABLE_OFFSET); 

    for (i=0; i<LHE_MAX_HUFF_SIZE; i++) {
        av_log(NULL, AV_LOG_INFO, "huffman_Y[%d] = %d \n",i, huffman_Y[i]);
    }
    
    for (i=0; i<LHE_MAX_HUFF_SIZE; i++) {
        av_log(NULL, AV_LOG_INFO, "huffman_U[%d] = %d \n",i, huffman_U[i]);
    }

    for (i=0; i<LHE_MAX_HUFF_SIZE; i++) {
        av_log(NULL, AV_LOG_INFO, "huffman_V[%d] = %d \n", i, huffman_V[i]);
    }
    
    lhe_read_file_symbols(s, image_size, huffman_Y, symbols_Y);
    lhe_read_file_symbols(s, image_size, huffman_U, symbols_U);
    lhe_read_file_symbols(s, image_size, huffman_V, symbols_V);

    //Luminance
    lhe_decode_one_hop_per_pixel(&s->prec, hops, component_Y, symbols_Y, first_pixel_Y, width, height, pix_size);
    
    //Chrominance U
    lhe_decode_one_hop_per_pixel(&s->prec, hops, component_U, symbols_U, first_pixel_U, width, height, pix_size);
    
    //Chrominance V
    lhe_decode_one_hop_per_pixel(&s->prec, hops, component_V, symbols_V, first_pixel_V, width, height, pix_size);
    
    if ((ret = av_frame_ref(data, s->frame)) < 0)
        return ret;
    
    *got_frame = 1;

    return 0;
}


static av_cold int lhe_decode_close(AVCodecContext *avctx)
{
    LheState *s = avctx->priv_data;

    av_freep(&s->prec.prec_luminance);
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
