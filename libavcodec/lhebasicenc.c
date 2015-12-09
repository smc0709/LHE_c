/*
 * LHE Basic encoder
 */

/**
 * @file
 * LHE Basic encoder
 */

#include "avcodec.h"
#include "lhebasic.h"
#include "internal.h"
#include "put_bits.h"
#include "bytestream.h"
#include "cbrt_tablegen.h"

typedef struct LheContext {
    AVClass *class;    
    LheBasicPrec prec;
    PutBitContext pb;
} LheContext;

static av_cold int lhe_encode_init(AVCodecContext *avctx)
{
    LheContext *s = avctx->priv_data;

    lhe_init_cache(&s->prec);

    return 0;

}

static void lhe_translate_hop_into_symbol (uint8_t * symbols_hops, uint8_t *hops, int pix, int pix_size, int width) {
    uint8_t hop, symbol;
    bool hop_found;
    
    hop_found = false;  
    hop = hops[pix];
    
    if (hop == HOP_0) {
        symbol = SYM_HOP_O;
        hop_found = true;
    }
    
    if (!hop_found && pix > width && hops[pix-width]==hop) {
        symbol = SYM_HOP_UP;
        hop_found = true;
    }

    if (!hop_found) {
        switch (hop) {
            case HOP_POS_1:
                symbol = SYM_HOP_POS_1;
                break;
            case HOP_NEG_1:
                symbol = SYM_HOP_NEG_1;
                break;
            case HOP_POS_2:
                symbol = SYM_HOP_POS_2;
                break;
            case HOP_NEG_2:
                symbol = SYM_HOP_NEG_2;
                break;
            case HOP_POS_3:
                symbol = SYM_HOP_POS_3;
                break;
            case HOP_NEG_3:
                symbol = SYM_HOP_NEG_3;
                break;
            case HOP_POS_4:
                symbol = SYM_HOP_POS_4;
                break;
            case HOP_NEG_4:
                symbol = SYM_HOP_NEG_4;        
                break;
        }
    }
    
    symbols_hops[pix] = symbol;
    
}

static void lhe_encode_one_hop_per_pixel (LheBasicPrec *prec, uint8_t *component_original_data, uint8_t *component_prediction,
                                          uint8_t *hops, uint8_t *symbols_buf, int height, int width, int pix_size)
{      
    //Hops computation.
    bool small_hop, last_small_hop;
    uint8_t predicted_luminance, hop_1, hop_number, original_color, r_max;
    int pix;
    
    //Errors
    int min_error;      // error of predicted signal
    int error;          //computed error for each hop 

    small_hop = false;
    last_small_hop=false;          // indicates if last hop is small
    predicted_luminance=0;         // predicted signal
    hop_1= START_HOP_1;
    hop_number=4;                  // pre-selected hop // 4 is NULL HOP
    pix=0;                         // pixel possition, from 0 to image size        
    original_color=0;              // original color
    
    r_max=PARAM_R;     
    for (int y=0; y < height; y++)  {
        for (int x=0; x < width; x++)  {
            
            //av_log(NULL, AV_LOG_INFO, "Linesize %d Pix %d pix_sixe %d \n", frame->linesize[0], pix, pix_size);

            original_color = component_original_data[pix_size*pix];

            //prediction of signal (predicted_luminance) , based on pixel's coordinates 
            //----------------------------------------------------------
            if ((y>0) &&(x>0) && x!=width-1)
            {
                predicted_luminance=(4*component_prediction[pix-1]+3*component_prediction[pix+1-width])/7;     
            } 
            else if ((x==0) && (y>0))
            {
                predicted_luminance=component_prediction[pix-width];
                last_small_hop=false;
                hop_1=START_HOP_1;
            } 
            else if ((x==width-1) && (y>0)) 
            {
                predicted_luminance=(4*component_prediction[pix-1]+2*component_prediction[pix-width])/6;                               
            } 
            else if (y==0 && x>0) 
            {
                predicted_luminance=component_prediction[x-1];
            }
            else if (x==0 && y==0) {  
                predicted_luminance=original_color;//first pixel always is perfectly predicted! :-)  
            }          
            
            if (predicted_luminance>255) 
            {
                predicted_luminance=255;
            }
            
            if (predicted_luminance<0) 
            {
                predicted_luminance=0;  
            }

            // end of center adjustment section 
            //==================================       
            error = 0;
            min_error = 256;
            
            //Positive hops computation
            //-------------------------
            if (original_color - predicted_luminance>=0) 
            {
                for (int j=HOP_0;j<=HOP_POS_4;j++) 
                {
                        error= original_color - prec -> prec_luminance[hop_1][predicted_luminance][r_max][j];
                        
                        if (error<0) {
                            error=-error;
                        }
                        
                        if (error<min_error) 
                        {
                            hop_number=j;
                            min_error=error;
                            
                        }
                        else break;
                }
            }
            
            //Negative hops computation
            //-------------------------
            else 
            {
                for (int j=HOP_0;j>=HOP_NEG_4;j--) 
                {
                        error = prec -> prec_luminance[hop_1][predicted_luminance][r_max][j]-original_color;
                        
                        if (error<0) 
                        {
                            error = -error;
                        }
                        
                        if (error<min_error) {
                            hop_number=j;
                            min_error=error;
                        }
                        else break;
                }
            }
            

            //assignment of final color value
            //--------------------------------
            if (MIDDLE_VALUE) 
            {
                component_prediction[pix]=prec -> prec_luminance_center[hop_1][predicted_luminance][r_max][hop_number];

            } else 
            {
                component_prediction[pix]=prec -> prec_luminance[hop_1][predicted_luminance][r_max][hop_number];
            }

            hops[pix]=hop_number; 
            lhe_translate_hop_into_symbol(symbols_buf, hops, pix, pix_size, width);

            //tunning hop1 for the next hop ( "h1 adaptation")
            //------------------------------------------------
            small_hop=false;
            if (hop_number<=HOP_POS_1 && hop_number>=HOP_NEG_1) 
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
        }//for x
    }//for y     
}


static void lhe_print_data(AVFrame *picture)
{
    int pix = 0;
    uint8_t *p = (uint8_t *)picture->data[0];
    uint8_t *p_end = p + (picture->linesize[0] / sizeof(uint8_t)) * picture->height;

    av_log(NULL, AV_LOG_INFO, "linesize %d size %d height %d p %d p_end %d \n", picture->linesize[0], sizeof(uint8_t), picture->height, p, p_end);

    for (; p < p_end; p++) {
        if (pix<100) av_log(NULL, AV_LOG_INFO, "P[%d] = %d \n", pix, *p);
        pix++;
    }
    

}

/**
 * Comparator - our nodes should ascend by count
 * but with preserved symbol order
 */
static int lhe_huff_cmp(const void *va, const void *vb)
{
    const Node *a = va, *b = vb;
    return (a->count - b->count)*LHE_MAX_HUFF_SIZE + a->sym - b->sym;
}

static int lhe_build_huff_tree(AVCodecContext *avctx, int *codes, uint8_t *symbols, 
                               uint8_t *huffman, int image_size, int pix_size)
{
    Node nodes[2*LHE_MAX_HUFF_SIZE];
    int code, coded_symbols, bits;
    VLC vlc;
    int i, ret;
    
    
    //Initialize values
    for (i=0; i<2*LHE_MAX_HUFF_SIZE; i++) 
    {
        nodes[i].count = 0;
    }
    
    /* first compute probabilities from model */
    for (i=0; i<image_size; i++) {
        nodes[lhe_huff_coeff_map[symbols[i]]].count++;
    }

    /* then build the huffman tree according to probabilities */
    if (ret = ff_huff_build_tree(avctx, &vlc, LHE_MAX_HUFF_SIZE, LHE_MAX_BITS,
                                nodes, lhe_huff_cmp,
                                FF_HUFFMAN_FLAG_HNODE_FIRST) < 0) 
        return ret;

    ff_free_vlc(&vlc);
    
    coded_symbols = 0;
    bits = 0;
    for (i=LHE_MAX_HUFF_SIZE*2 - 1; i>=0; i--) 
    {   
        if (nodes[i].sym != -1) {
            
            if (coded_symbols == 0) 
            {
                code = 0;
            }
            else if (coded_symbols == LHE_MAX_HUFF_SIZE-1) 
            {
                //Last symbol only changes last bit
                code = code + 1;
            }
            else
            {
                code = code * 10 + 10;
            }
            
            codes[nodes[i].sym] = code;
            huffman[coded_symbols] = nodes[i].sym;
            coded_symbols++;
            bits+=nodes[i].count*count_bits(code); //bits number is symbol occurrence * symbol bits

        }     
    }
    
     
     for (i=0; i<LHE_MAX_HUFF_SIZE; i++) 
    {
        av_log(NULL, AV_LOG_INFO, "huffman[%d]= %d \n", i, codes[i]);

    }
   
    
    return bits;
}

static void lhe_put_bits (LheContext *s, int bits) 
{
    int i, count;
    uint8_t *bit;
    int original_bits;
    
    
    original_bits = bits;
    count = count_bits(bits);
    bit = malloc(sizeof(uint8_t) * count);    
    
    i = count-1;
    while (i>=0)
    {
        bit[i] = bits % 10;
        bits = bits / 10;
        i--;
    }
    
    for (i=0; i<count; i++) 
    {
        put_bits(&s->pb, 1, bit[i]);
    }
    
    av_freep(&bit);
}

static int lhe_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                             const AVFrame *frame, int *got_packet)
{
    uint8_t *component_Y, *component_U, *component_V;
    uint8_t *component_prediction, *hops, *symbols_Y, *symbols_U, *symbols_V, *buf;
    uint8_t *huffman_Y, *huffman_U, *huffman_V;
    uint8_t first_pixel_Y, first_pixel_U, first_pixel_V;
    uint8_t file_offset;
    int *codes_Y, *codes_U , *codes_V;
    int width, height;
    int image_size;
    int bits, n_bytes, n_bytes_components, n_bits_hops_Y , n_bits_hops_U, n_bits_hops_V;
    int ret, i; 

    struct timeval before , after;

    LheContext *s = avctx->priv_data;

    width = (int) frame->width;
    height = (int) frame->height;  
    image_size = frame -> height * frame -> width;
    const int pix_size = frame->linesize[0]/ width;
    
    //Pointers to different color components
    component_Y = frame->data[0];
    component_U = frame->data[1];
    component_V = frame->data[2];
      
    component_prediction = malloc(sizeof(uint8_t) * image_size);  
    hops = malloc(sizeof(uint8_t) * image_size);
    huffman_Y = malloc (sizeof(uint8_t) * LHE_MAX_HUFF_SIZE);
    huffman_U = malloc (sizeof(uint8_t) * LHE_MAX_HUFF_SIZE);
    huffman_V = malloc (sizeof(uint8_t) * LHE_MAX_HUFF_SIZE);
    symbols_Y = malloc(sizeof(uint8_t) * image_size); 
    symbols_U = malloc(sizeof(uint8_t) * image_size); 
    symbols_V = malloc(sizeof(uint8_t) * image_size); 
    codes_Y = malloc (sizeof(int) * LHE_MAX_HUFF_SIZE);
    codes_U = malloc (sizeof(int) * LHE_MAX_HUFF_SIZE);
    codes_V = malloc (sizeof(int) * LHE_MAX_HUFF_SIZE);

    gettimeofday(&before , NULL);

    //Luminance
    lhe_encode_one_hop_per_pixel(&s->prec, component_Y, component_prediction, hops, symbols_Y, height, width, pix_size); 

    //Crominance U
    lhe_encode_one_hop_per_pixel(&s->prec, component_U, component_prediction, hops, symbols_U, height, width, pix_size); 

    //Crominance V
    lhe_encode_one_hop_per_pixel(&s->prec, component_V, component_prediction, hops, symbols_V, height, width, pix_size);   
    
    gettimeofday(&after , NULL);
    
    n_bits_hops_Y = lhe_build_huff_tree(avctx, codes_Y, symbols_Y, huffman_Y, image_size, pix_size);
    av_log(NULL, AV_LOG_INFO, "Componente_Y  %d bits %d \n", n_bits_hops_Y);

    n_bits_hops_U = lhe_build_huff_tree(avctx, codes_U, symbols_U, huffman_U, image_size, pix_size);
    av_log(NULL, AV_LOG_INFO, "Componente_U  %d bits \n", n_bits_hops_U);

    n_bits_hops_V = lhe_build_huff_tree(avctx, codes_V, symbols_V, huffman_V, image_size, pix_size);
    av_log(NULL, AV_LOG_INFO, "Componente_V  %d bits \n", n_bits_hops_V);

    ret = (n_bits_hops_Y + n_bits_hops_U + n_bits_hops_V) % 8;
    n_bytes_components = (n_bits_hops_Y + n_bits_hops_U + n_bits_hops_V + ret)/8;
    
    //File size
    n_bytes = sizeof(width) + sizeof(height) //width and height
              + sizeof(first_pixel_Y) + sizeof(first_pixel_U) + sizeof(first_pixel_V) //first pixel value
              + sizeof (n_bytes) + 
              + 3 * LHE_HUFFMAN_TABLE_SIZE_BYTES + //huffman trees
              + n_bytes_components; //components
              
    file_offset = ((n_bytes * 8) + LHE_HUFFMAN_TABLE_OFFSET) % 32;
    n_bytes = ((n_bytes * 8) + file_offset)/8;
    
    //ff_alloc_packet2 reserves n_bytes of memory
    if ((ret = ff_alloc_packet2(avctx, pkt, n_bytes, 0)) < 0)
        return ret;

    buf = pkt->data;    
                
    //save width and height
    bytestream_put_le32(&buf, width);
    bytestream_put_le32(&buf, height);  
    
    //save first pixel color 
    first_pixel_Y = component_Y[0];
    first_pixel_U = component_U[0];
    first_pixel_V = component_V[0];

    bytestream_put_byte(&buf, first_pixel_Y);
    bytestream_put_byte(&buf, first_pixel_U);
    bytestream_put_byte(&buf, first_pixel_V);
    
    bytestream_put_le32(&buf, n_bytes);
    
    init_put_bits(&s->pb, buf, 3*LHE_HUFFMAN_TABLE_SIZE_BYTES + n_bytes_components );

    for (i=0; i<LHE_MAX_HUFF_SIZE; i++)
    {
        put_bits(&s->pb, LHE_HUFFMAN_NODE_BITS, huffman_Y[i]);
    }
    
    for (i=0; i<LHE_MAX_HUFF_SIZE; i++)
    {
        put_bits(&s->pb, LHE_HUFFMAN_NODE_BITS, huffman_U[i]);
    }
    
        for (i=0; i<LHE_MAX_HUFF_SIZE; i++)
    {
        put_bits(&s->pb, LHE_HUFFMAN_NODE_BITS, huffman_V[i]);
    }
    
    put_bits(&s->pb, LHE_HUFFMAN_TABLE_OFFSET, 0);

    for (i=0; i<image_size; i++) 
    {
        bits = codes_Y[symbols_Y[i]];
        lhe_put_bits(s , bits);
    }
    
    for (i=0; i<image_size; i++) 
    {
        bits = codes_U[symbols_U[i]];
        lhe_put_bits(s , bits);
    }
    
    for (i=0; i<image_size; i++) 
    {
        bits = codes_V[symbols_V[i]];
        lhe_put_bits(s , bits);
    }
    
    put_bits(&s->pb, file_offset, 0);
      
    av_log(NULL, AV_LOG_INFO, "LHE Coding...buffer size %d CodingTime %.0lf \n", n_bytes, time_diff(before , after));

    pkt->flags |= AV_PKT_FLAG_KEY;
    *got_packet = 1;

    return 0;

}


static int lhe_encode_close(AVCodecContext *avctx)
{
    LheContext *s = avctx->priv_data;

    av_freep(&s->prec.prec_luminance);

    return 0;

}


static const AVClass lhe_class = {
    .class_name = "LHE Basic encoder",
    .item_name  = av_default_item_name,
    .version    = LIBAVUTIL_VERSION_INT,
};

AVCodec ff_lhe_encoder = {
    .name           = "lhe",
    .long_name      = NULL_IF_CONFIG_SMALL("LHE"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_LHE,
    .priv_data_size = sizeof(LheContext),
    .init           = lhe_encode_init,
    .encode2        = lhe_encode_frame,
    .close          = lhe_encode_close,
    .pix_fmts       = (const enum AVPixelFormat[]){
        AV_PIX_FMT_YUV444P, AV_PIX_FMT_GRAY8, AV_PIX_FMT_NONE
    },
    .priv_class     = &lhe_class,
};
