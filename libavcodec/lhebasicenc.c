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
#include "siprdata.h"

#define H1_ADAPTATION                                   \
    if (hop_number<=HOP_POS_1 && hop_number>=HOP_NEG_1) \
    {                                                   \
        small_hop=true;                                 \
    } else                                              \
    {                                                   \
        small_hop=false;                                \
    }                                                   \
                                                        \
    if( (small_hop) && (last_small_hop))  {             \
        hop_1=hop_1-1;                                  \
        if (hop_1<MIN_HOP_1) {                          \
            hop_1=MIN_HOP_1;                            \
        }                                               \
                                                        \
    } else {                                            \
        hop_1=MAX_HOP_1;                                \
    }                                                   \
    last_small_hop=small_hop;
        
        
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


static void lhe_translate_hops_into_symbols (uint8_t *symbols, uint8_t *hops,
                                             int width, int image_size) {
    int pix;
    uint8_t hop, symbol;
    bool hop_found;
    
    for (pix=0; pix<image_size; pix++) 
    {    
        hop_found = false;
        hop = hops[pix];
        symbol = SYM_0;
        
        //First, check if hop is HOP_O
        if (hop == HOP_0) 
        {
            hop_found = true;
        } else 
        {
            symbol += HOP_0_CHECK;
        }
        
        //Second, check if hop is HOP_UP
        if (!hop_found && pix > width && hops[pix-width]==hop) 
        {
            hop_found = true;
        } else if (!hop_found && pix>width)
        {
            symbol += HOP_UP_CHECK;
        }

        //Third, look for the right hop
        
        if (!hop_found) 
        {
            switch (hop) {
                case HOP_POS_1:
                    symbol += HOP_POS_1_CHECK;
                    break;
                case HOP_NEG_1:
                    symbol += HOP_NEG_1_CHECK;
                    break;
                case HOP_POS_2:
                    symbol += HOP_POS_2_CHECK;
                    break;
                case HOP_NEG_2:
                    symbol += HOP_NEG_2_CHECK;
                    break;
                case HOP_POS_3:
                    symbol += HOP_POS_3_CHECK;
                    break;
                case HOP_NEG_3:
                    symbol += HOP_NEG_3_CHECK;
                    break;
                case HOP_POS_4:
                    symbol += HOP_POS_4_CHECK;
                    break;
                case HOP_NEG_4:
                    symbol += HOP_NEG_4_CHECK;   
                    break;
            }  
        }
        
        symbols[pix] = symbol;
    }   
}

static void lhe_encode_one_hop_per_pixel_block (LheBasicPrec *prec, uint8_t *component_original_data, 
                                                uint8_t *component_prediction, uint8_t *hops, 
                                                int width, int height, int linesize, 
                                                uint8_t *first_color_block, int total_blocks_width,
                                                int block_x, int block_y,
                                                int block_width, int block_height)
{      
    
    //Hops computation.
    int xini, xfin, yini, yfin;
    bool small_hop, last_small_hop;
    uint8_t predicted_luminance, hop_1, hop_number, original_color, r_max;
    int pix, pix_original_data, dif_line, dif_pix ,num_block;
    
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

    small_hop = false;
    last_small_hop=false;          // indicates if last hop is small
    predicted_luminance=0;         // predicted signal
    hop_1= START_HOP_1;
    hop_number=4;                  // pre-selected hop // 4 is NULL HOP
    pix=0;                         // pixel possition, from 0 to image size        
    original_color=0;              // original color
    
    r_max=PARAM_R;
    
    pix = yini*width + xini;
    pix_original_data = yini*linesize + xini;
    
    dif_pix = width - xfin + xini;
    dif_line = linesize - xfin + xini;
    
    for (int y=yini; y < yfin; y++)  {
        for (int x=xini; x < xfin; x++)  {
            
            original_color = component_original_data[pix_original_data]; //This can't be pix because ffmpeg adds empty memory slots. 

            //prediction of signal (predicted_luminance) , based on pixel's coordinates 
            //----------------------------------------------------------
                        
            if (x == xini && y==yini) 
            {
                predicted_luminance=original_color;//first pixel always is perfectly predicted! :-)  
                first_color_block[num_block] = original_color;
            } 
            else if (y == yini) 
            {
                predicted_luminance=component_prediction[pix-1];
            } 
            else if (x == xini) 
            {
                predicted_luminance=component_prediction[pix-width];
                last_small_hop=false;
                hop_1=START_HOP_1;
            } else if (x == xfin -1) 
            {
                predicted_luminance=(component_prediction[pix-1]+component_prediction[pix-width])>>1;                               
            } 
            else 
            {
                predicted_luminance=(component_prediction[pix-1]+component_prediction[pix+1-width])>>1;     
            }


            hop_number = prec->best_hop[r_max][hop_1][original_color][predicted_luminance]; 
            hops[pix]= hop_number;
            component_prediction[pix]=prec -> prec_luminance[predicted_luminance][r_max][hop_1][hop_number];


            //tunning hop1 for the next hop ( "h1 adaptation")
            //------------------------------------------------
            H1_ADAPTATION;

            //lets go for the next pixel
            //--------------------------
            pix++;
            pix_original_data++;
        }//for x
        pix+=dif_pix;
        pix_original_data+=dif_line;
    }//for y     
}

static void lhe_encode_one_hop_per_pixel (LheBasicPrec *prec, uint8_t *component_original_data, 
                                          uint8_t *component_prediction, uint8_t *hops, 
                                          int width, int height, int linesize, 
                                          uint8_t *first_color_block)
{      

    //Hops computation.
    bool small_hop, last_small_hop;
    uint8_t predicted_luminance, hop_1, hop_number, original_color, r_max;
    int pix, dif_line, x, y;

    small_hop = false;
    last_small_hop=false;          // indicates if last hop is small
    predicted_luminance=0;         // predicted signal
    hop_1= START_HOP_1;
    hop_number=4;                  // pre-selected hop // 4 is NULL HOP
    pix=0;                         // pixel possition, from 0 to image size   
    x = 0;
    y = 0;
    original_color=0;              // original color
    r_max=PARAM_R;
    
    dif_line = linesize - width;
      
    
    for (y=0; y < height; y++)  {
        for (x=0; x < width; x++)  {
            
            
            original_color = *component_original_data++;    
        
            if (x==0 && y==0)
            {
                predicted_luminance=original_color;//first pixel always is perfectly predicted! :-)  
                first_color_block[0]=original_color;
            }
            else if (y == 0)
            {
                predicted_luminance=component_prediction[pix-1];               
            }
            else if (x == 0)
            {
                predicted_luminance=component_prediction[pix-width];
                last_small_hop=false;
                hop_1=START_HOP_1;  
            } 
            else if (x == width -1)
            {
                predicted_luminance=(component_prediction[pix-1]+component_prediction[pix-width])>>1;                               
            }
            else 
            {
                predicted_luminance = (component_prediction[pix-1]+component_prediction[pix+1-width])>>1; 
            }
            
            
            hop_number = prec->best_hop[r_max][hop_1][original_color][predicted_luminance];            
            component_prediction[pix]=prec -> prec_luminance[predicted_luminance][r_max][hop_1][hop_number];  
            hops[pix]= hop_number;
            
            H1_ADAPTATION;
            pix++;            

        }
        component_original_data+=dif_line;            
    }    
    
}

static uint64_t lhe_gen_huffman (LheHuffEntry *he_Y, LheHuffEntry *he_UV,
                                 uint8_t *symbols_Y, uint8_t *symbols_U, uint8_t *symbols_V,
                                 int image_size_Y, int image_size_UV)
{
    int i, ret, n_bits;
    uint8_t  huffman_lengths_Y[LHE_MAX_HUFF_SIZE];
    uint8_t  huffman_lengths_UV[LHE_MAX_HUFF_SIZE];
    uint64_t symbol_count_Y[LHE_MAX_HUFF_SIZE]     = { 0 };
    uint64_t symbol_count_UV[LHE_MAX_HUFF_SIZE]    = { 0 };

    //LUMINANCE
    
    //First compute luminance probabilities from model
    for (i=0; i<image_size_Y; i++) {
        symbol_count_Y[symbols_Y[i]]++;
    }
    
    //Generate Huffman length luminance
    if ((ret = ff_huff_gen_len_table(huffman_lengths_Y, symbol_count_Y, LHE_MAX_HUFF_SIZE, 1)) < 0)
        return ret;
    
    
     for (i = 0; i < LHE_MAX_HUFF_SIZE; i++) {
        he_Y[i].len = huffman_lengths_Y[i];
        he_Y[i].count = symbol_count_Y[i];
        he_Y[i].sym = i;
    }
    
    //Generate luminance Huffman codes
    n_bits = lhe_generate_huffman_codes(he_Y);
    
    //CHROMINANCE
    
    //First, compute chrominance probabilities.
    for (i=0; i<image_size_UV; i++) {
        symbol_count_UV[symbols_U[i]]++;
    }
    
    for (i=0; i<image_size_UV; i++) {
        symbol_count_UV[symbols_V[i]]++;
    }
    
     //Generate Huffman length chrominance
    if ((ret = ff_huff_gen_len_table(huffman_lengths_UV, symbol_count_UV, LHE_MAX_HUFF_SIZE, 1)) < 0)
        return ret;
    
     for (i = 0; i < LHE_MAX_HUFF_SIZE; i++) {
        he_UV[i].len = huffman_lengths_UV[i];
        he_UV[i].count = symbol_count_UV[i];
        he_UV[i].sym = i;
    }

    //Generate chrominance Huffman codes
    n_bits += lhe_generate_huffman_codes(he_UV);
    
    return n_bits;
    
}
                             
static int lhe_write_lhe_file(AVCodecContext *avctx, AVPacket *pkt, 
                              int image_size_Y, int width_Y, int height_Y,
                              int image_size_UV, int width_UV, int height_UV,
                              uint8_t total_blocks_width, uint8_t total_blocks_height,
                              uint8_t *first_pixel_blocks_Y, uint8_t *first_pixel_blocks_U, uint8_t *first_pixel_blocks_V,
                              uint8_t *hops_Y, uint8_t *hops_U, uint8_t *hops_V) {
  
    uint8_t *buf;
    uint8_t file_offset, file_offset_bytes;
    uint8_t *symbols_Y, *symbols_U, *symbols_V;

    uint64_t n_bits_hops, n_bytes, n_bytes_components, total_blocks;
    int i, ret;

    struct timeval before , after;
    
    LheHuffEntry he_Y[LHE_MAX_HUFF_SIZE];
    LheHuffEntry he_UV[LHE_MAX_HUFF_SIZE];

    LheContext *s = avctx->priv_data;
    
    total_blocks = total_blocks_height * total_blocks_width;
    
    symbols_Y = malloc(sizeof(uint8_t) * image_size_Y); 
    symbols_U = malloc(sizeof(uint8_t) * image_size_UV); 
    symbols_V = malloc(sizeof(uint8_t) * image_size_UV); 


    gettimeofday(&before , NULL);

    //Translate hops into symbols
    lhe_translate_hops_into_symbols(symbols_Y, hops_Y, 
                                    width_Y, image_size_Y); 
    lhe_translate_hops_into_symbols(symbols_U, hops_U,
                                    width_UV, image_size_UV); 
    lhe_translate_hops_into_symbols(symbols_V, hops_V,
                                    width_UV, image_size_UV); 

    gettimeofday(&after , NULL);

    
    n_bits_hops = lhe_gen_huffman (he_Y, he_UV, 
                                     symbols_Y, symbols_U, symbols_V, 
                                     image_size_Y, image_size_UV);

    n_bytes_components = (n_bits_hops + (n_bits_hops%8))/8;
    
    //File size
    n_bytes = sizeof(width_Y) + sizeof(height_Y) //width and height
              + sizeof(total_blocks_height) + sizeof(total_blocks_width)
              + total_blocks * (sizeof(first_pixel_blocks_Y) + sizeof(first_pixel_blocks_U) + sizeof(first_pixel_blocks_V)) //first pixel blocks array value
              + 2*LHE_HUFFMAN_TABLE_SIZE_BYTES + //huffman table
              + n_bytes_components; //components
              
    file_offset = (n_bytes * 8) % 32;
    file_offset += (file_offset%8);
    file_offset_bytes = file_offset / 8;
    n_bytes += file_offset_bytes;
    
    //ff_alloc_packet2 reserves n_bytes of memory
    if ((ret = ff_alloc_packet2(avctx, pkt, n_bytes, 0)) < 0)
        return ret;

    buf = pkt->data;    
        
    //save width and height
    bytestream_put_le32(&buf, width_Y);
    bytestream_put_le32(&buf, height_Y);  

    bytestream_put_byte(&buf, total_blocks_width);
    bytestream_put_byte(&buf, total_blocks_height);

    for (i=0; i<total_blocks; i++) 
    {
        bytestream_put_byte(&buf, first_pixel_blocks_Y[i]);
    }
    
      for (i=0; i<total_blocks; i++) 
    {
        bytestream_put_byte(&buf, first_pixel_blocks_U[i]);

    }
    
      for (i=0; i<total_blocks; i++) 
    {
        bytestream_put_byte(&buf, first_pixel_blocks_V[i]);
    }
    
         
    init_put_bits(&s->pb, buf, 2*LHE_HUFFMAN_TABLE_SIZE_BYTES + n_bytes_components + file_offset_bytes);

    //Write Huffman tables
    for (i=0; i<LHE_MAX_HUFF_SIZE; i++)
    {
        if (he_Y[i].len==255) he_Y[i].len=15;
        put_bits(&s->pb, LHE_HUFFMAN_NODE_BITS, he_Y[i].len);
    }
    
      for (i=0; i<LHE_MAX_HUFF_SIZE; i++)
    {
        if (he_UV[i].len==255) he_UV[i].len=15;
        put_bits(&s->pb, LHE_HUFFMAN_NODE_BITS, he_UV[i].len);
    }   
    
    //Write image
    for (i=0; i<image_size_Y; i++) 
    {        
        put_bits(&s->pb, he_Y[symbols_Y[i]].len , he_Y[symbols_Y[i]].code);
    }
    
    for (i=0; i<image_size_UV; i++) 
    {        
       put_bits(&s->pb, he_UV[symbols_U[i]].len , he_UV[symbols_U[i]].code);
        
    }
    
    for (i=0; i<image_size_UV; i++) 
    {
        put_bits(&s->pb, he_UV[symbols_V[i]].len , he_UV[symbols_V[i]].code);
    }
    
    put_bits(&s->pb, file_offset, 0);
    
    av_log(NULL, AV_LOG_INFO, "LHE Write file %.0lf \n", time_diff(before , after));

    
    return n_bytes;
}

static void lhe_encode_frame_pararell (LheBasicPrec *prec, 
                                       uint8_t *component_original_data_Y, uint8_t *component_original_data_U, uint8_t *component_original_data_V,
                                       uint8_t *component_prediction_Y, uint8_t *component_prediction_UV, 
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
            lhe_encode_one_hop_per_pixel_block(prec, component_original_data_Y, component_prediction_Y, hops_Y,      
                                                width_Y, height_Y, linesize_Y,
                                                first_color_block_Y, total_blocks_width,
                                                i, j, BLOCK_WIDTH_Y, BLOCK_HEIGHT_Y);

            //Crominance U
            lhe_encode_one_hop_per_pixel_block(prec, component_original_data_U, component_prediction_UV, hops_U,
                                               width_UV, height_UV, linesize_U, 
                                               first_color_block_U, total_blocks_width,
                                               i, j, BLOCK_WIDTH_UV, BLOCK_HEIGHT_UV); 

            //Crominance V
            lhe_encode_one_hop_per_pixel_block(prec, component_original_data_V, component_prediction_UV, hops_V, 
                                               width_UV, height_UV, linesize_V, 
                                               first_color_block_V, total_blocks_width,
                                               i, j, BLOCK_WIDTH_UV, BLOCK_HEIGHT_UV);
        }
    }  
}


static void lhe_encode_frame_sequential (LheBasicPrec *prec, 
                                       uint8_t *component_original_data_Y, uint8_t *component_original_data_U, uint8_t *component_original_data_V,
                                       uint8_t *component_prediction_Y, uint8_t *component_prediction_UV, 
                                       uint8_t *hops_Y, uint8_t *hops_U, uint8_t *hops_V,
                                       int width_Y, int height_Y, int width_UV, int height_UV, 
                                       int linesize_Y, int linesize_U, int linesize_V, 
                                       uint8_t *first_color_block_Y, uint8_t *first_color_block_U, uint8_t *first_color_block_V)
{
    //Luminance
    lhe_encode_one_hop_per_pixel(prec, component_original_data_Y, component_prediction_Y, hops_Y, width_Y, height_Y, linesize_Y, first_color_block_Y); 

    //Crominance U
    lhe_encode_one_hop_per_pixel(prec, component_original_data_U, component_prediction_UV, hops_U, width_UV, height_UV, linesize_U, first_color_block_U); 

    //Crominance V
    lhe_encode_one_hop_per_pixel(prec, component_original_data_V, component_prediction_UV, hops_V, width_UV, height_UV, linesize_V, first_color_block_V);   
}


static int lhe_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                             const AVFrame *frame, int *got_packet)
{
    uint8_t *component_original_data_Y, *component_original_data_U, *component_original_data_V;
    uint8_t *component_prediction_Y, *component_prediction_UV, *hops_Y, *hops_U, *hops_V;
    uint8_t *first_color_block_Y, *first_color_block_U, *first_color_block_V;
    int width_Y, width_UV, height_Y, height_UV, image_size_Y, image_size_UV, n_bytes; 
    int total_blocks_width, total_blocks_height, total_blocks;
    
    struct timeval before , after;
    
    LheContext *s = avctx->priv_data;

    width_Y = frame->width;
    height_Y =  frame->height; 
    image_size_Y = width_Y * height_Y;

    width_UV = (width_Y - 1)/CHROMA_FACTOR_WIDTH + 1;
    height_UV = (height_Y - 1)/CHROMA_FACTOR_HEIGHT + 1;
    image_size_UV = width_UV * height_UV;
    
    total_blocks_height = (height_Y - 1)/ BLOCK_HEIGHT_Y + 1;
    total_blocks_width = (width_Y - 1) / BLOCK_WIDTH_Y + 1;
    
    if (OPENMP_FLAGS == CONFIG_OPENMP) 
    {
        total_blocks = total_blocks_height * total_blocks_width;
    } else {
        total_blocks_height = 1;
        total_blocks_width = 1;
        total_blocks = 1;
    }
    
    //Pointers to different color components
    component_original_data_Y = frame->data[0];
    component_original_data_U = frame->data[1];
    component_original_data_V = frame->data[2];
      
    component_prediction_Y = malloc(sizeof(uint8_t) * image_size_Y);  
    component_prediction_UV = malloc(sizeof(uint8_t) * image_size_UV);  
    hops_Y = malloc(sizeof(uint8_t) * image_size_Y);
    hops_U = malloc(sizeof(uint8_t) * image_size_UV);
    hops_V = malloc(sizeof(uint8_t) * image_size_UV);
    first_color_block_Y = malloc(sizeof(uint8_t) * total_blocks);
    first_color_block_U = malloc(sizeof(uint8_t) * total_blocks);
    first_color_block_V = malloc(sizeof(uint8_t) * total_blocks);
    
    gettimeofday(&before , NULL);
   

    if(OPENMP_FLAGS == CONFIG_OPENMP) {
        
        lhe_encode_frame_pararell (&s->prec, 
                                   component_original_data_Y, component_original_data_U, component_original_data_V, 
                                   component_prediction_Y, component_prediction_UV, 
                                   hops_Y, hops_U, hops_V,
                                   width_Y, height_Y, width_UV, height_UV, 
                                   frame->linesize[0], frame->linesize[1], frame->linesize[2],
                                   first_color_block_Y, first_color_block_U, first_color_block_V,
                                   total_blocks_width, total_blocks_height);

                                       
                               
    } else 
    {
        lhe_encode_frame_sequential (&s->prec, 
                                     component_original_data_Y, component_original_data_U, component_original_data_V, 
                                     component_prediction_Y, component_prediction_UV, 
                                     hops_Y, hops_U, hops_V,
                                     width_Y, height_Y, width_UV, height_UV, 
                                     frame->linesize[0], frame->linesize[1], frame->linesize[2],
                                     first_color_block_Y, first_color_block_U, first_color_block_V);        
    }
    
    
    gettimeofday(&after , NULL);  
      
    n_bytes = lhe_write_lhe_file(avctx, pkt,image_size_Y,  width_Y,  height_Y,
                                 image_size_UV,  width_UV,  height_UV,
                                 total_blocks_width, total_blocks_height,
                                 first_color_block_Y, first_color_block_U, first_color_block_V, 
                                 hops_Y, hops_U, hops_V);
    
    av_log(NULL, AV_LOG_INFO, "LHE Coding...buffer size %d CodingTime %.0lf \n", n_bytes, time_diff(before , after));

    pkt->flags |= AV_PKT_FLAG_KEY;
    *got_packet = 1;

    return 0;

}


static int lhe_encode_close(AVCodecContext *avctx)
{
    LheContext *s = avctx->priv_data;

    av_freep(&s->prec.prec_luminance);
    av_freep(&s->prec.best_hop);

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
        AV_PIX_FMT_YUV422P, AV_PIX_FMT_GRAY8, AV_PIX_FMT_NONE
    },
    .priv_class     = &lhe_class,
};
