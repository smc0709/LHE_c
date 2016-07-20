/*
 * LHE Basic encoder
 */

/**
 * @file
 * LHE Basic encoder
 */

#include "avcodec.h"
#include "lhe.h"
#include "internal.h"
#include "put_bits.h"
#include "bytestream.h"
#include "siprdata.h"

/**
 * Adapts hop_1 value depending on last hops. It is used
 * in BASIC LHE and ADVANCED LHE
 */
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

/**
 * Lhe Context
 * 
 * @p AVClass *class Pointer to AVClass
 * @p LheBasicPrec prec Caches for LHE
 * @p PutBitContext pb Params for putting bits in a file
 * @p int pr_metrics Print or not print perceptual relevance metrics
 * @p int basic_lhe Basic LHE or Advanced LHE
 */                
typedef struct LheContext {
    AVClass *class;    
    LheBasicPrec prec;
    PutBitContext pb;
    uint8_t chroma_factor_width;
    uint8_t chroma_factor_height;
    int pr_metrics;
    int basic_lhe;
    int ql;
} LheContext;

/**
 * Initializes coder
 * 
 * @param *avctx Pointer to AVCodec context
 */
static av_cold int lhe_encode_init(AVCodecContext *avctx)
{
    LheContext *s = avctx->priv_data;

    lhe_init_cache(&s->prec);
    
    if (avctx->pix_fmt == AV_PIX_FMT_YUV420P)
    {
        s->chroma_factor_width = 2;
        s->chroma_factor_height = 2;
    } else if (avctx->pix_fmt == AV_PIX_FMT_YUV422P) 
    {
        s->chroma_factor_width = 2;
        s->chroma_factor_height = 1;
    } else if (avctx->pix_fmt == AV_PIX_FMT_YUV444P) 
    {
        s->chroma_factor_width = 1;
        s->chroma_factor_height = 1;
    }
        

    return 0;

}


//==================================================================
// AUXILIARY FUNCTIONS
//==================================================================

static void lhe_compute_error_for_psnr (AVCodecContext *avctx, const AVFrame *frame,
                                        int height_Y, int width_Y, int height_UV, int width_UV,
                                        uint8_t *component_original_data_Y, uint8_t *component_original_data_U, uint8_t *component_original_data_V,
                                        uint8_t *component_prediction_Y, uint8_t *component_prediction_U, uint8_t *component_prediction_V) 
{
    
    int error= 0;

    if(frame->data[0]) {
        for(int y=0; y<height_Y; y++){
            for(int x=0; x<width_Y; x++){
                error = component_original_data_Y[y*frame->linesize[0] + x] - component_prediction_Y[y*width_Y + x];
                error = abs(error);
                avctx->error[0] += error*error;
            }
        }    
    }
    
    if(frame->data[1]) {
        for(int y=0; y<height_UV; y++){
            for(int x=0; x<width_UV; x++){
                error = component_original_data_U[y*frame->linesize[1] + x] - component_prediction_U[y*width_UV + x];
                error = abs(error);
                avctx->error[1] += error*error;
            }
        }    
    }
    
    if(frame->data[2]) {
        for(int y=0; y<height_UV; y++){
            for(int x=0; x<width_UV; x++){
                error = component_original_data_V[y*frame->linesize[2] + x] - component_prediction_V[y*width_UV + x];
                error = abs(error);
                avctx->error[2] += error*error;
            }
        }    
    }
}

static void print_json_pr_metrics (float** perceptual_relevance_x, float** perceptual_relevance_y,
                                   int total_blocks_width, int total_blocks_height) 
{
    int i,j;
    
    av_log (NULL, AV_LOG_PANIC, "[");
        
    for (j=0; j<total_blocks_height+1; j++) 
    {
        for (i=0; i<total_blocks_width+1; i++) 
        {  
            if (i==total_blocks_width && j==total_blocks_height) 
            {
                av_log (NULL, AV_LOG_PANIC, "{\"prx\":%.4f, \"pry\":%.4f}", perceptual_relevance_x[j][i], perceptual_relevance_y[j][i]);
            }
            else 
            {
                av_log (NULL, AV_LOG_PANIC, "{\"prx\":%.4f, \"pry\":%.4f},", perceptual_relevance_x[j][i], perceptual_relevance_y[j][i]);
            }
        }
        
    }
    
    av_log (NULL, AV_LOG_PANIC, "]");   
}

static void print_csv_pr_metrics (float** perceptual_relevance_x, float** perceptual_relevance_y,
                                   int total_blocks_width, int total_blocks_height) 
{
    int i,j;
            
    for (j=0; j<total_blocks_height+1; j++) 
    {
        for (i=0; i<total_blocks_width+1; i++) 
        {  

            av_log (NULL, AV_LOG_INFO, "%.4f;%.4f;", perceptual_relevance_x[j][i], perceptual_relevance_y[j][i]);

        }
        
        av_log (NULL, AV_LOG_INFO, "\n");

        
    }
    
}



//==================================================================
// BASIC LHE FILE
//==================================================================


/**
 * Generates Huffman for BASIC LHE
 * 
 * @param *he_Y Parameters for Huffman of luminance signal
 * @param *he_UV Parameters for Huffman of chrominance signals
 * @param *symbols_Y Luminance symbols (or hops)
 * @param *symbols_U Chrominance U symbols (or hops)
 * @param *symbols_V Chrominance V symbols (or hops)
 * @param image_size_Y Width x Height of luminance
 * @param image_size_UV Width x Height of chrominances
 * @return n_bits Number of total bits
 */
static uint64_t lhe_basic_gen_huffman (LheHuffEntry *he_Y, LheHuffEntry *he_UV,
                                       uint8_t *symbols_Y, uint8_t *symbols_U, uint8_t *symbols_V,
                                       int image_size_Y, int image_size_UV)
{
    int i, ret, n_bits;
    float bpp;
    uint8_t  huffman_lengths_Y[LHE_MAX_HUFF_SIZE];
    uint8_t  huffman_lengths_UV[LHE_MAX_HUFF_SIZE];
    uint64_t symbol_count_Y[LHE_MAX_HUFF_SIZE]     = { 0 };
    uint64_t symbol_count_UV[LHE_MAX_HUFF_SIZE]    = { 0 };
    
    //LUMINANCE
    
    //First compute luminance probabilities from model
    for (i=0; i<image_size_Y; i++) {
        symbol_count_Y[symbols_Y[i]]++; //Counts occurrences of different luminance symbols
    }
    
    //Generates Huffman length for luminance signal
    if ((ret = ff_huff_gen_len_table(huffman_lengths_Y, symbol_count_Y, LHE_MAX_HUFF_SIZE, 1)) < 0)
        return ret;
    
    //Fills he_Y struct with data
    for (i = 0; i < LHE_MAX_HUFF_SIZE; i++) {
        he_Y[i].len = huffman_lengths_Y[i];
        he_Y[i].count = symbol_count_Y[i];
        he_Y[i].sym = i;
        he_Y[i].code = 1024; //imposible code to initialize
    }
    
    //Generates luminance Huffman codes
    n_bits = lhe_generate_huffman_codes(he_Y);
    bpp = 1.0*n_bits/image_size_Y;
    
    av_log (NULL, AV_LOG_INFO, "Y bpp: %f ", bpp );
    
    //CHROMINANCES (same Huffman table for both chrominances)
    
    //First, compute chrominance probabilities.
    for (i=0; i<image_size_UV; i++) {
        symbol_count_UV[symbols_U[i]]++; //Counts occurrences of different chrominance U symbols
    }
    
    for (i=0; i<image_size_UV; i++) {
        symbol_count_UV[symbols_V[i]]++; //Counts occurrences of different chrominance V symbols
    }

    
     //Generates Huffman length for chrominance signals
    if ((ret = ff_huff_gen_len_table(huffman_lengths_UV, symbol_count_UV, LHE_MAX_HUFF_SIZE, 1)) < 0)
        return ret;
    
    //Fills he_UV data
    for (i = 0; i < LHE_MAX_HUFF_SIZE; i++) {
        he_UV[i].len = huffman_lengths_UV[i];
        he_UV[i].count = symbol_count_UV[i];
        he_UV[i].sym = i;
        he_UV[i].code = 1024;
    }

    //Generates chrominance Huffman codes
    n_bits += lhe_generate_huffman_codes(he_UV);
    bpp = 1.0*n_bits/image_size_Y;
    
    av_log (NULL, AV_LOG_INFO, "YUV bpp: %f ", bpp );
    
    return n_bits;
    
}

/**
 * Writes BASIC LHE file 
 * 
 * @param *avctx Pointer to AVCodec context
 * @param *pkt Pointer to AVPacket 
 * @param image_size_Y Width x Height of luminance
 * @param width_Y Width of luminance
 * @param heigth_Y Height of luminance
 * @param image_size_UV Width x Height of chrominances
 * @param width_UV Width of chrominance
 * @param height_UV Height of chrominance
 * @param total_blocks_width Number of blocks widthwise
 * @param total_blocks_height Number of blocks heightwise
 * @param *first_pixel_blocks_Y First luminance component of each block
 * @param *first_pixel_blocks_U First chrominance U component of each block
 * @param *first_pixel_blocks_V First chrominance V component of each block
 * @param *hops_Y Luminance hops
 * @param *hops_U Chrominance U hops
 * @param *hops_V Chrominance V hops
 */
static int lhe_basic_write_lhe_file(AVCodecContext *avctx, AVPacket *pkt, 
                                    int image_size_Y, int width_Y, int height_Y,
                                    int image_size_UV, int width_UV, int height_UV,
                                    uint8_t total_blocks_width, uint8_t total_blocks_height,
                                    uint8_t *first_pixel_blocks_Y, uint8_t *first_pixel_blocks_U, uint8_t *first_pixel_blocks_V,
                                    uint8_t *hops_Y, uint8_t *hops_U, uint8_t *hops_V) {
  
    uint8_t *buf;
    uint8_t lhe_mode, pixel_format;
    uint64_t n_bits_hops, n_bytes, n_bytes_components, total_blocks;
    
    int i, ret;
        
    struct timeval before , after;
    
    LheHuffEntry he_Y[LHE_MAX_HUFF_SIZE]; //Struct for luminance Huffman data
    LheHuffEntry he_UV[LHE_MAX_HUFF_SIZE]; //Struct for chrominance Huffman data

    LheContext *s = avctx->priv_data;
    
    total_blocks = total_blocks_height * total_blocks_width; //Number of blocks in the image
    
    gettimeofday(&before , NULL);

    //Generates Huffman
    n_bits_hops = lhe_basic_gen_huffman (he_Y, he_UV, 
                                         hops_Y, hops_U, hops_V, 
                                         image_size_Y, image_size_UV);
    

    n_bytes_components = n_bits_hops/8;        
    
    //File size
    n_bytes = sizeof(lhe_mode) + sizeof(pixel_format) + //Lhe mode and pixel format
              + sizeof(width_Y) + sizeof(height_Y) //width and height
              + sizeof(total_blocks_height) + sizeof(total_blocks_width) //Number of blocks heightwise and widthwise
              + total_blocks * (sizeof(*first_pixel_blocks_Y) + sizeof(*first_pixel_blocks_U) + sizeof(*first_pixel_blocks_V)) //first component value for each block array
              + LHE_HUFFMAN_TABLE_BYTES + //huffman table
              + n_bytes_components
              + FILE_OFFSET_BYTES; //components
              
    av_log (NULL, AV_LOG_INFO, "YUV+Header bpp: %f \n ", (n_bytes*8.0)/image_size_Y);
              
    //ff_alloc_packet2 reserves n_bytes of memory
    if ((ret = ff_alloc_packet2(avctx, pkt, n_bytes, 0)) < 0)
        return ret;

    buf = pkt->data; //Pointer to write buffer
        
    //Lhe mode byte
    lhe_mode = BASIC_LHE; 
    bytestream_put_byte(&buf, lhe_mode);
    
    //Pixel format byte
    if (avctx->pix_fmt == AV_PIX_FMT_YUV420P)
    {
        pixel_format = LHE_YUV420;
    } else if (avctx->pix_fmt == AV_PIX_FMT_YUV422P) 
    {
        pixel_format = LHE_YUV422;
    } else if (avctx->pix_fmt == AV_PIX_FMT_YUV444P) 
    {
        pixel_format = LHE_YUV444;
    }
    
    bytestream_put_byte(&buf, pixel_format);
    
    //save width and height
    bytestream_put_le32(&buf, width_Y);
    bytestream_put_le32(&buf, height_Y);  

    //Save number of blocks (this allows to know if LHE has been parallelized or not)
    bytestream_put_byte(&buf, total_blocks_width);
    bytestream_put_byte(&buf, total_blocks_height);

    //Save first component of each signal 
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
    
      
    init_put_bits(&s->pb, buf, LHE_HUFFMAN_TABLE_BYTES + n_bytes_components + FILE_OFFSET_BYTES);

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
    
    //Write signals of the image
    for (i=0; i<image_size_Y; i++) 
    {        
        put_bits(&s->pb, he_Y[hops_Y[i]].len , he_Y[hops_Y[i]].code);
    }
    
    for (i=0; i<image_size_UV; i++) 
    {        
       put_bits(&s->pb, he_UV[hops_U[i]].len , he_UV[hops_U[i]].code);
    }
    
    for (i=0; i<image_size_UV; i++) 
    {
        put_bits(&s->pb, he_UV[hops_V[i]].len , he_UV[hops_V[i]].code);
    }
    
    put_bits(&s->pb, FILE_OFFSET_BITS , 0);
    
    gettimeofday(&after , NULL);

    av_log(NULL, AV_LOG_INFO, "LHE WriteTime %.0lf ", time_diff(before , after));
    
    return n_bytes;
}



//==================================================================
// ADVANCED LHE FILE
//==================================================================


/**
 * Generates Huffman for ADVANCED LHE 
 * 
 * @param *he_Y Parameters for Huffman of luminance signal
 * @param *he_UV Parameters for Huffman of chrominance signals
 * @param **basic_block_Y Basic block parameters for luminance signal
 * @param **basic_block_UV Basic block parameters for chrominance signals
 * @param **advanced_block_Y Advanced block parameters for luminance signal
 * @param **advanced_block_UV Advanced block parameters for chrominance signals
 * @param *symbols_Y Luminance symbols (or hops)
 * @param *symbols_U Chrominance U symbols (or hops)
 * @param *symbols_V Chrominance V symbols (or hops)
 * @param width_Y Width of luminance
 * @param width_UV Width of chrominance
 * @param heigth_Y Height of luminance
 * @param height_UV Height of chrominance
 * @param block_width_Y Block width for luminance signal
 * @param block_height_Y Block height for luminance signal
 * @param block_width_UV Block width for chrominance signal
 * @param block_height_UV Block height for chrominance signal
 * @param total_blocks_width Number of blocks widthwise
 * @param total_blocks_height Number of blocks heightwise
 * @return n_bits Number of total bits
 */
static uint64_t lhe_advanced_gen_huffman (LheHuffEntry *he_Y, LheHuffEntry *he_UV, 
                                          BasicLheBlock **basic_block_Y, BasicLheBlock **basic_block_UV,
                                          AdvancedLheBlock **advanced_block_Y, AdvancedLheBlock **advanced_block_UV,
                                          uint8_t *symbols_Y, uint8_t *symbols_U, uint8_t *symbols_V,
                                          uint32_t width_Y, uint32_t width_UV, uint32_t height_Y, uint32_t height_UV,
                                          uint32_t block_width_Y,  uint32_t block_height_Y, uint32_t block_width_UV, uint32_t block_height_UV,
                                          uint32_t total_blocks_width, uint32_t total_blocks_height)
{
    int i, ret, n_bits;
    float bpp;
    uint8_t  huffman_lengths_Y[LHE_MAX_HUFF_SIZE];
    uint8_t  huffman_lengths_UV[LHE_MAX_HUFF_SIZE];
    uint64_t symbol_count_Y[LHE_MAX_HUFF_SIZE]     = { 0 };
    uint64_t symbol_count_UV[LHE_MAX_HUFF_SIZE]    = { 0 };
    
    uint32_t xini_Y, xini_UV, xfin_downsampled_Y, xfin_downsampled_UV, yini_Y, yini_UV, yfin_downsampled_Y, yfin_downsampled_UV;

    //LUMINANCE
    //First compute luminance probabilities from model taking into account different image blocks
    for (int block_y=0; block_y<total_blocks_height; block_y++) 
    {
        for (int block_x=0; block_x<total_blocks_width; block_x++)
        {

            xini_Y = basic_block_Y[block_y][block_x].x_ini;
            yini_Y = basic_block_Y[block_y][block_x].y_ini;
            
            xfin_downsampled_Y = advanced_block_Y[block_y][block_x].x_fin_downsampled;          
            yfin_downsampled_Y = advanced_block_Y[block_y][block_x].y_fin_downsampled;
               
            xini_UV = basic_block_UV[block_y][block_x].x_ini;
            yini_UV = basic_block_UV[block_y][block_x].y_ini;
            
            xfin_downsampled_UV = advanced_block_UV[block_y][block_x].x_fin_downsampled;
            yfin_downsampled_UV = advanced_block_UV[block_y][block_x].y_fin_downsampled;
             
            //LUMINANCE
            for (int y=yini_Y; y<yfin_downsampled_Y; y++) 
            {
                for (int x=xini_Y; x<xfin_downsampled_Y; x++) {
                    symbol_count_Y[symbols_Y[y*width_Y + x]]++;  //Generates Huffman length for luminance signal               
                }
            }  
      
            //CHROMINANCE
            for (int y=yini_UV; y<yfin_downsampled_UV; y++) 
            {
                for (int x=xini_UV; x<xfin_downsampled_UV; x++) {
                    symbol_count_UV[symbols_U[y*width_UV + x]]++;  //Generates Huffman length for chrominance U signal
                    symbol_count_UV[symbols_V[y*width_UV + x]]++;  //Generates Huffman length for chrominance V signal
                }
            } 
        }
    }
    

    //LUMINANCE
    //Generates Huffman length for luminance
    if ((ret = ff_huff_gen_len_table(huffman_lengths_Y, symbol_count_Y, LHE_MAX_HUFF_SIZE, 1)) < 0)
        return ret;
    
    //Fills he_Y struct with data
    for (i = 0; i < LHE_MAX_HUFF_SIZE; i++) {
        he_Y[i].len = huffman_lengths_Y[i];
        he_Y[i].count = symbol_count_Y[i];
        he_Y[i].sym = i;
        he_Y[i].code = 1024; //imposible code to initialize   
    }
    
    //Generates luminance Huffman codes
    n_bits = lhe_generate_huffman_codes(he_Y);
    bpp = 1.0*n_bits/(width_Y*height_Y);
    
    av_log (NULL, AV_LOG_INFO, "Y bpp: %f ",bpp );
    
    //CHROMINANCES
    //Generate Huffman length chrominance (same Huffman table for both chrominances)
    if ((ret = ff_huff_gen_len_table(huffman_lengths_UV, symbol_count_UV, LHE_MAX_HUFF_SIZE, 1)) < 0)
        return ret;
    
    //Fills he_UV struct with data
    for (i = 0; i < LHE_MAX_HUFF_SIZE; i++) {
        he_UV[i].len = huffman_lengths_UV[i];
        he_UV[i].count = symbol_count_UV[i];
        he_UV[i].sym = i;
        he_UV[i].code = 1024;      
    }

    //Generates chrominance Huffman codes
    n_bits += lhe_generate_huffman_codes(he_UV);
    bpp = 1.0*n_bits/(width_Y*height_Y);

    av_log (NULL, AV_LOG_INFO, "YUV bpp: %f ", bpp);
    
    //Returns total bits
    return n_bits; 
}

/**
 * Translates Perceptual relevance values into perceptual relevance interval number 
 * to save it on advanced lhe file.
 * 
 *    Interval   -  Quant - Interval number
 * [0.0, 0.125)  -  0.0   -         0
 * [0.125, 0.25) -  0.125 -         1
 * [0.25, 0.5)   -  0.25  -         2
 * [0.5, 0.75)   -  0.5   -         3
 * [0.75, 1]     -  1.0   -         4
 * 
 * @param perceptual_relevance perceptual relevance value to translate
 */
static uint8_t lhe_advanced_translate_pr_into_mesh (float perceptual_relevance) 
{
    uint8_t pr_interval;
    if (perceptual_relevance == PR_QUANT_0) 
    {
        pr_interval = PR_INTERVAL_0;
    } 
    else if (perceptual_relevance == PR_QUANT_1) 
    {
        pr_interval = PR_INTERVAL_1;
    }
    else if (perceptual_relevance == PR_QUANT_2) 
    {
        pr_interval = PR_INTERVAL_2;
    }
    else if (perceptual_relevance == PR_QUANT_3) 
    {
        pr_interval = PR_INTERVAL_3;
    }
    else if (perceptual_relevance == PR_QUANT_5) 
    {
        pr_interval = PR_INTERVAL_4;
    }
   
    return pr_interval;
}

/**
 * Writes ADVANCED LHE file 
 * 
 * @param *avctx Pointer to AVCodec context
 * @param *pkt Pointer to AVPacket 
 * @param **basic_block_Y Basic block parameters for luminance signal
 * @param **basic_block_UV Basic block parameters for chrominance signals
 * @param **advanced_block_Y Advanced block parameters for luminance signal
 * @param **advanced_block_UV Advanced block parameters for chrominance signals
 * @param image_size_Y Width x Height of luminance
 * @param width_Y Width of luminance
 * @param heigth_Y Height of luminance
 * @param image_size_UV Width x Height of chrominances
 * @param width_UV Width of chrominance
 * @param height_UV Height of chrominance
 * @param total_blocks_width Number of blocks widthwise
 * @param total_blocks_height Number of blocks heightwise
 * @param block_width_Y luminance block width
 * @param block_with_UV chrominance block width
 * @param block_height_Y luminance block height
 * @param block_height_UV chrominance block height
 * @param *first_pixel_blocks_Y First luminance component of each block
 * @param *first_pixel_blocks_U First chrominance U component of each block
 * @param *first_pixel_blocks_V First chrominance V component of each block
 * @param **perceptual_relevance_x Perceptual Relevance X coordinate
 * @param **perceptual_relevance_y Perceptual Relevance Y coordinate
 * @param *hops_Y Luminance hops
 * @param *hops_U Chrominance U hops
 * @param *hops_V Chrominance V hops
 * @param quality_level quality level of compressed image
 */
static int lhe_advanced_write_lhe_file(AVCodecContext *avctx, AVPacket *pkt, 
                                       BasicLheBlock **basic_block_Y, BasicLheBlock **basic_block_UV,
                                       AdvancedLheBlock **advanced_block_Y, AdvancedLheBlock **advanced_block_UV,
                                       uint32_t image_size_Y, uint32_t width_Y, uint32_t height_Y,
                                       uint32_t image_size_UV, uint32_t width_UV, uint32_t height_UV,
                                       uint8_t total_blocks_width, uint8_t total_blocks_height,                                       
                                       uint32_t block_width_Y, uint32_t block_width_UV, uint32_t block_height_Y, uint32_t block_height_UV,
                                       uint8_t *first_pixel_blocks_Y, uint8_t *first_pixel_blocks_U, uint8_t *first_pixel_blocks_V,
                                       float **perceptual_relevance_x, float **perceptual_relevance_y,
                                       uint8_t *hops_Y, uint8_t *hops_U, uint8_t *hops_V,
                                       uint8_t quality_level) 
{
  
    uint8_t *buf;
    uint8_t lhe_mode, pixel_format;
    uint64_t n_bits_hops, n_bytes, n_bytes_components, n_bytes_mesh, total_blocks;
    uint32_t xini_Y, xfin_downsampled_Y, yini_Y, yfin_downsampled_Y, xini_UV, xfin_downsampled_UV, yini_UV, yfin_downsampled_UV; 
    uint64_t pix;
        
    int i, ret;
        
    struct timeval before , after;
    
    LheHuffEntry he_Y[LHE_MAX_HUFF_SIZE]; //Struct for luminance Huffman data
    LheHuffEntry he_UV[LHE_MAX_HUFF_SIZE]; //Struct for chrominance Huffman data

    LheContext *s = avctx->priv_data;
        
    total_blocks = total_blocks_height * total_blocks_width;
    
    gettimeofday(&before , NULL);

    //Generates HUffman
    n_bits_hops = lhe_advanced_gen_huffman (he_Y, he_UV, 
                                            basic_block_Y, basic_block_UV,
                                            advanced_block_Y, advanced_block_UV,
                                            hops_Y, hops_U, hops_V, 
                                            width_Y, width_UV, height_Y, height_UV,
                                            block_width_Y, block_height_Y, block_width_UV, block_height_UV,
                                            total_blocks_width, total_blocks_height);
     
    n_bytes_components = (n_bits_hops/8) + 1;        
    
    //Mesh bytes
    n_bytes_mesh = ((PR_MESH_BITS * (total_blocks_width + 1) * (total_blocks_height + 1)) / 8) + 1;
    
    //File size
    n_bytes = sizeof(lhe_mode) + sizeof (pixel_format) +
              + sizeof(width_Y) + sizeof(height_Y) //width and height
              + sizeof(total_blocks_height) + sizeof(total_blocks_width)
              + total_blocks * (sizeof(*first_pixel_blocks_Y) + sizeof(*first_pixel_blocks_U) + sizeof(*first_pixel_blocks_V)) //first pixel blocks array value
              + sizeof (quality_level) + //quality level
              + LHE_HUFFMAN_TABLE_BYTES + //huffman table
              + n_bytes_mesh 
              + n_bytes_components
              + FILE_OFFSET_BYTES; //components
              
    av_log (NULL, AV_LOG_INFO, "YUV+Header bpp: %f \n", (n_bytes*8.0)/image_size_Y);
              
    //ff_alloc_packet2 reserves n_bytes of memory
    if ((ret = ff_alloc_packet2(avctx, pkt, n_bytes, 0)) < 0)
        return ret;

    buf = pkt->data; //Pointer to write buffer
    
    //LHE Mode
    lhe_mode = ADVANCED_LHE; 
    
    //Lhe mode byte
    bytestream_put_byte(&buf, lhe_mode);
    
    //Pixel format byte
    if (avctx->pix_fmt == AV_PIX_FMT_YUV420P)
    {
        pixel_format = LHE_YUV420;
    } else if (avctx->pix_fmt == AV_PIX_FMT_YUV422P) 
    {
        pixel_format = LHE_YUV422;
    } else if (avctx->pix_fmt == AV_PIX_FMT_YUV444P) 
    {
        pixel_format = LHE_YUV444;
    }
    
    bytestream_put_byte(&buf, pixel_format);
        
    //save width and height
    bytestream_put_le32(&buf, width_Y);
    bytestream_put_le32(&buf, height_Y);  

    //Save total blocks widthwise and total blocks heightwise
    bytestream_put_byte(&buf, total_blocks_width);
    bytestream_put_byte(&buf, total_blocks_height);

    //Save first pixel for each block
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
         
    init_put_bits(&s->pb, buf, LHE_HUFFMAN_TABLE_BYTES + sizeof(quality_level) + n_bytes_mesh + n_bytes_components + FILE_OFFSET_BYTES);

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
    
    //Advanced LHE quality level
    put_bits(&s->pb, QL_SIZE_BITS, quality_level);    
    
    //Write mesh (perceptual relevance intervals)
    for (int block_y=0; block_y<total_blocks_height+1; block_y++) 
    {
        for (int block_x=0; block_x<total_blocks_width+1; block_x++) 
        { 
            put_bits(&s->pb, PR_INTERVAL_BITS, lhe_advanced_translate_pr_into_mesh(perceptual_relevance_x[block_y][block_x]));
            put_bits(&s->pb, PR_INTERVAL_BITS, lhe_advanced_translate_pr_into_mesh(perceptual_relevance_y[block_y][block_x]));

        }
    }
    
    //Write hops
    for (int block_y=0; block_y<total_blocks_height; block_y++) 
    {
        for (int block_x=0; block_x<total_blocks_width; block_x++)
        {
            xini_Y = basic_block_Y[block_y][block_x].x_ini;
            yini_Y = basic_block_Y[block_y][block_x].y_ini;
            
            xfin_downsampled_Y = advanced_block_Y[block_y][block_x].x_fin_downsampled;          
            yfin_downsampled_Y = advanced_block_Y[block_y][block_x].y_fin_downsampled;
               
            xini_UV = basic_block_UV[block_y][block_x].x_ini;
            yini_UV = basic_block_UV[block_y][block_x].y_ini;
            
            xfin_downsampled_UV = advanced_block_UV[block_y][block_x].x_fin_downsampled;
            yfin_downsampled_UV = advanced_block_UV[block_y][block_x].y_fin_downsampled;
         
            //LUMINANCE
            for (int y=yini_Y; y<yfin_downsampled_Y; y++) 
            {
                for (int x=xini_Y; x<xfin_downsampled_Y; x++) {
                    pix = y*width_Y + x;
                    put_bits(&s->pb, he_Y[hops_Y[pix]].len , he_Y[hops_Y[pix]].code);
                }
            }
            
            //CHROMINANCE U
            for (int y=yini_UV; y<yfin_downsampled_UV; y++) 
            {
                for (int x=xini_UV; x<xfin_downsampled_UV; x++) {
                    pix = y*width_UV + x;
                    put_bits(&s->pb, he_UV[hops_U[pix]].len , he_UV[hops_U[pix]].code);
                }
            }
            
            //CHROMINANCE_V
            for (int y=yini_UV; y<yfin_downsampled_UV; y++) 
            {
                for (int x=xini_UV; x<xfin_downsampled_UV; x++) {
                    pix = y*width_UV + x;
                    put_bits(&s->pb, he_UV[hops_V[pix]].len , he_UV[hops_V[pix]].code);
                }
            }
        }
    }

    put_bits(&s->pb, FILE_OFFSET_BITS , 0);
    
    gettimeofday(&after , NULL);

    av_log(NULL, AV_LOG_INFO, "LHE WriteTime %.0lf ", time_diff(before , after));
    
    return n_bytes;
}
                             

//==================================================================
// BASIC LHE FUNCTIONS
//==================================================================
/**
 * Encodes one hop per pixel sequentially
 * 
 * @param *prec Pointer to LHE precalculated parameters
 * @param *component_original_data original image
 * @param *component_prediction Prediction made. This will be decoded image
 * @param *hops hops array. Hop represents error in prediction
 * @param width_image image width
 * @param width_sps sps is single pix selection. Only samples needed are coded (uses a smaller image to encode)
 * @param height_sps sps is single pix selection. Only samples needed are coded (uses a smaller image to encode)
 * @param linesize rectangle images create a square image in ffmpeg memory. Linesize is width used by ffmpeg in memory
 * @param *fist_color_block first component value for each block
 * @param sps_ratio_width indicates how often an image sample is taken widthwise to encode
 * @param sps_ratio_height indicates how often an image sample is taken heightwise to encode
 */
static void lhe_basic_encode_one_hop_per_pixel (LheBasicPrec *prec, uint8_t *component_original_data, 
                                                uint8_t *component_prediction, uint8_t *hops, 
                                                int width_image, int width_sps, int height_sps, int linesize, 
                                                uint8_t *first_color_block,
                                                uint8_t sps_ratio_width, uint8_t sps_ratio_height )
{      

    //Hops computation.
    bool small_hop, last_small_hop;
    uint8_t predicted_component, hop_1, hop_number, original_color, r_max;
    int pix, pix_original_data, dif_line, sps_line_pix, x, y;

    small_hop = false;
    last_small_hop=false;          // indicates if last hop is small
    predicted_component=0;         // predicted signal
    hop_1= START_HOP_1;
    hop_number=4;                  // pre-selected hop // 4 is NULL HOP
    pix=0;                         // pixel possition, from 0 to image size   
    pix_original_data = 0;
    x = 0;
    y = 0;
    original_color=0;              // original color
    r_max=PARAM_R;
    
    dif_line = linesize - width_image;       
    sps_line_pix = (sps_ratio_height-1) * linesize + dif_line;
    
    for (y=0; y < height_sps; y++)  
    {
        for (x=0; x < width_sps; x++)  
        {    
            original_color = component_original_data[pix_original_data];    
        
            if (x==0 && y==0) //First pixel
            {
                predicted_component=original_color;
                first_color_block[0]=original_color; //Save first component (needed in lhe file)
            }
            else if (y == 0) //First row
            {
                predicted_component=component_prediction[pix-1];                
            }
            else if (x == 0) //First column
            {
                predicted_component=component_prediction[pix-width_sps];
                last_small_hop=false;
                hop_1=START_HOP_1;  
            } 
            else if (x == width_sps -1) //Last column
            {
                predicted_component=(component_prediction[pix-1]+component_prediction[pix-width_sps])>>1;                               
            }
            else //Rest of the image
            {
                predicted_component = (component_prediction[pix-1]+component_prediction[pix+1-width_sps])>>1; 
            }
            
            
            hop_number = prec->best_hop[r_max][hop_1][original_color][predicted_component];            
            component_prediction[pix]=prec -> prec_luminance[predicted_component][r_max][hop_1][hop_number];  
            hops[pix]= hop_number;
                        
            H1_ADAPTATION;
            pix++;   
            pix_original_data+=sps_ratio_width;

        }
        pix_original_data+=sps_line_pix;            
    }    
    
}

/**
 * Encodes one hop per pixel in a block
 * 
 * @param *prec Pointer to LHE precalculated parameters
 * @param **basic_block Basic block parameters
 * @param *component_original_data original image
 * @param *component_prediction Prediction made. This will be decoded image
 * @param *hops hops array. Hop represents error in prediction
 * @param width_image image width
 * @param width_sps sps is single pix selection. Less samples are used to encode
 * @param height_image image height
 * @param height_sps sps is single pix selection. Less samples are used to encode
 * @param linesize rectangle images create a square image in ffmpeg memory. Linesize is width used by ffmpeg in memory
 * @param *fist_color_block first component value for each block
 * @param total_blocks_width number of blocks widthwise
 * @param total_blocks_height number of blocks heightwise
 * @param block_x block in x coordinate to encode
 * @param block_y block in y coordinate to encode
 * @param block_width block width
 * @param block_width_sps sps is single pix selection. Less samples are used to encode
 * @param block_height block height
 * @param block_height_sps sps is single pix selection. Less samples are used to encode
 * @param sps_ratio_width indicates how often an image sample is taken widthwise to encode
 * @param sps_ratio_height indicates how often an image sample is taken heightwise to encode
 */
static void lhe_basic_encode_one_hop_per_pixel_block (LheBasicPrec *prec, 
                                                      BasicLheBlock **basic_block,
                                                      uint8_t *component_original_data, 
                                                      uint8_t *component_prediction, uint8_t *hops, 
                                                      int width_image, int width_sps, int height_image, int height_sps, int linesize, 
                                                      uint8_t *first_color_block, 
                                                      uint32_t total_blocks_width, uint32_t total_blocks_height,
                                                      int block_x, int block_y,
                                                      uint32_t block_width, uint32_t block_width_sps, uint32_t block_height, uint32_t block_height_sps,
                                                      uint8_t sps_ratio_width, uint8_t sps_ratio_height)
{      
    
    //Hops computation.
    int xini, xini_sps, xfin, xfin_sps, yini, yini_sps, yfin_sps;
    bool small_hop, last_small_hop;
    uint8_t predicted_component, hop_1, hop_number, original_color, r_max;
    int pix, pix_original_data, dif_line, sps_line_pix, dif_pix ,num_block;
    
    num_block = block_y * total_blocks_width + block_x;
    
    //ORIGINAL IMAGE
    xini = basic_block[block_y][block_x].x_ini;
    xfin = basic_block[block_y][block_x].x_fin;
    yini = basic_block[block_y][block_x].y_ini;
    
    //SPS IMAGE (Downsampled image using sps)
    xini_sps = block_x * block_width_sps;
    xfin_sps = xini_sps + block_width_sps;
    
    yini_sps = block_y * block_height_sps;
    yfin_sps = yini_sps + block_height_sps;
    
    //Limits
    if (block_x==total_blocks_width-1) 
    {
        xfin = width_image;
        xfin_sps = width_sps;
    }
    
    if (block_y == total_blocks_height-1)
    {
        yfin_sps = height_sps;
    }
    
    small_hop = false;
    last_small_hop=false;          // indicates if last hop is small
    predicted_component=0;         // predicted signal
    hop_1= START_HOP_1;
    hop_number=4;                  // pre-selected hop // 4 is NULL HOP
    pix=0;                         // pixel possition, from 0 to image size        
    original_color=0;              // original color
    
    r_max=PARAM_R;
    
    pix = yini_sps*width_sps + xini_sps;
    pix_original_data = yini*linesize + xini;
    
    dif_pix = width_sps - xfin_sps + xini_sps;
    dif_line = linesize - xfin + xini;
    sps_line_pix = (sps_ratio_height-1) * linesize + dif_line;
    
    
    for (int y=yini_sps; y < yfin_sps; y++)  {
        for (int x=xini_sps; x < xfin_sps; x++)  {
            
            original_color = component_original_data[pix_original_data]; //This can't be pix because ffmpeg adds empty memory slots. 

            //prediction of signal (predicted_component) , based on pixel's coordinates 
            //----------------------------------------------------------
                        
            if (x == xini_sps && y==yini_sps) //First pixel block
            {
                predicted_component=original_color;//first pixel always is perfectly predicted! :-)  
                first_color_block[num_block] = original_color;
            } 
            else if (y == yini_sps) //First row 
            {
                predicted_component=component_prediction[pix-1];
            } 
            else if (x == xini_sps) //First column
            {
                predicted_component=component_prediction[pix-width_sps];
                last_small_hop=false;
                hop_1=START_HOP_1;
            } else if (x == xfin_sps -1) //Last column
            {
                predicted_component=(component_prediction[pix-1]+component_prediction[pix-width_sps])>>1;                               
            } 
            else //Rest of the block
            {
                predicted_component=(component_prediction[pix-1]+component_prediction[pix+1-width_sps])>>1;     
            }


            hop_number = prec->best_hop[r_max][hop_1][original_color][predicted_component]; 
            hops[pix]= hop_number;
            component_prediction[pix]=prec -> prec_luminance[predicted_component][r_max][hop_1][hop_number];


            //tunning hop1 for the next hop ( "h1 adaptation")
            //------------------------------------------------
            H1_ADAPTATION;

            //lets go for the next pixel
            //--------------------------
            pix++;
            pix_original_data+=sps_ratio_width; //we jumped number of samples needed
        }//for x
        pix+=dif_pix; 
        pix_original_data+=sps_line_pix;
    }//for y     
}

/**
 * Calls methods to encode sequentially
 */
static void lhe_basic_encode_frame_sequential (LheBasicPrec *prec, 
                                               uint8_t *component_original_data_Y, uint8_t *component_original_data_U, uint8_t *component_original_data_V,
                                               uint8_t *component_prediction_Y, uint8_t *component_prediction_U, uint8_t *component_prediction_V,
                                               uint8_t *hops_Y, uint8_t *hops_U, uint8_t *hops_V,
                                               int width_Y, int width_sps_Y, int height_sps_Y, int width_UV, int width_sps_UV, int height_sps_UV,
                                               int linesize_Y, int linesize_U, int linesize_V, 
                                               uint8_t *first_color_block_Y, uint8_t *first_color_block_U, uint8_t *first_color_block_V,
                                               uint8_t sps_ratio_width, uint8_t sps_ratio_height )
{
    //Luminance
    lhe_basic_encode_one_hop_per_pixel(prec, 
                                       component_original_data_Y, component_prediction_Y, hops_Y, 
                                       width_Y, width_sps_Y, height_sps_Y, linesize_Y, first_color_block_Y,
                                       sps_ratio_width, sps_ratio_height ); 

    //Crominance U
    lhe_basic_encode_one_hop_per_pixel(prec, component_original_data_U, component_prediction_U, hops_U, 
                                       width_UV, width_sps_UV, height_sps_UV, linesize_U, first_color_block_U,
                                       sps_ratio_width, sps_ratio_height  ); 

    //Crominance V
    lhe_basic_encode_one_hop_per_pixel(prec, component_original_data_V, component_prediction_V, hops_V, 
                                       width_UV, width_sps_UV, height_sps_UV, linesize_V, first_color_block_V,
                                       sps_ratio_width, sps_ratio_height  );   
}

/**
 * Call methods to encode parallel
 */
static void lhe_basic_encode_frame_pararell (LheBasicPrec *prec, 
                                             BasicLheBlock **basic_block_Y, BasicLheBlock **basic_block_UV,
                                             uint8_t *component_original_data_Y, uint8_t *component_original_data_U, uint8_t *component_original_data_V,
                                             uint8_t *component_prediction_Y, uint8_t *component_prediction_U, uint8_t *component_prediction_V,  
                                             uint8_t *hops_Y, uint8_t *hops_U, uint8_t *hops_V,
                                             int width_Y, int width_sps_Y, int height_Y, int height_sps_Y,  
                                             int width_UV, int width_sps_UV, int height_UV, int height_sps_UV,
                                             int linesize_Y, int linesize_U, int linesize_V, 
                                             uint8_t *first_color_block_Y, uint8_t *first_color_block_U, uint8_t *first_color_block_V,
                                             int total_blocks_width, int total_blocks_height,
                                             int block_width_Y, int block_width_sps_Y, int block_height_Y, int block_height_sps_Y, 
                                             int block_width_UV, int block_width_sps_UV, int block_height_UV, int block_height_sps_UV,
                                             uint8_t sps_ratio_width, uint8_t sps_ratio_height )
{        
    #pragma omp parallel for
    for (int block_y=0; block_y<total_blocks_height; block_y++)      
    {  
        for (int block_x=0; block_x<total_blocks_width; block_x++) 
        {
            lhe_calculate_block_coordinates (basic_block_Y, basic_block_UV,
                                             block_width_Y, block_height_Y,                             
                                             block_width_UV, block_height_UV, 
                                             width_Y, height_Y,
                                             width_UV, height_UV,
                                             total_blocks_width, total_blocks_height,
                                             block_x, block_y);

            //Luminance
            lhe_basic_encode_one_hop_per_pixel_block(prec,
                                                     basic_block_Y, 
                                                     component_original_data_Y, component_prediction_Y, hops_Y,      
                                                     width_Y, width_sps_Y, height_Y, height_sps_Y, linesize_Y,
                                                     first_color_block_Y, 
                                                     total_blocks_width, total_blocks_height,
                                                     block_x, block_y, block_width_Y, block_width_sps_Y, block_height_Y, block_height_sps_Y,
                                                     sps_ratio_width, sps_ratio_height );

            
            //Crominance U
            lhe_basic_encode_one_hop_per_pixel_block(prec,
                                                     basic_block_UV,
                                                     component_original_data_U, component_prediction_U, hops_U,
                                                     width_UV, width_sps_UV, height_UV, height_sps_UV, linesize_U, 
                                                     first_color_block_U, 
                                                     total_blocks_width, total_blocks_height,
                                                     block_x, block_y, block_width_UV, block_width_sps_UV, block_height_UV, block_height_sps_UV, 
                                                     sps_ratio_width, sps_ratio_height  ); 

            //Crominance V
            lhe_basic_encode_one_hop_per_pixel_block(prec,
                                                     basic_block_UV,
                                                     component_original_data_V, component_prediction_V, hops_V, 
                                                     width_UV, width_sps_UV, height_UV, height_sps_UV, linesize_V, 
                                                     first_color_block_V, 
                                                     total_blocks_width, total_blocks_height,
                                                     block_x, block_y, block_width_UV, block_width_sps_UV, block_height_UV, block_height_sps_UV,
                                                     sps_ratio_width, sps_ratio_height  );
                                                     
                                               
        }
    }  
}


//==================================================================
// ADVANCED LHE FUNCTIONS
//==================================================================
/**
 * 
 * Computes Perceptual Relevance for each block
 * 
 * @param **perceptual_relevance_x Perceptual relevance in x
 * @param **perceptual_relevance_y Perceptual relevance in y
 * @param *hops_Y Hops array
 * @param xini_pr_block init x for perceptual relevance block
 * @param xfin_pr_block final x for perceptual relevance block
 * @param yini_pr_block init y for perceptual relevance block
 * @param yfin_pr_block final y for perceptual relevance block
 * @param block_x Block x index
 * @param block_y Block y index
 * @param width image width
 */
static void lhe_advanced_compute_perceptual_relevance_block (float **perceptual_relevance_x, float  **perceptual_relevance_y,
                                                             uint8_t *hops_Y,
                                                             int xini_pr_block, int xfin_pr_block, int yini_pr_block, int yfin_pr_block,
                                                             int block_x, int block_y,
                                                             int width) 
{
    int pix, dif_pix;
    uint8_t last_hop, top_hop, hop;
    float prx, pry;
    uint64_t hx, hy;
    uint32_t count_hx, count_hy;  
    
    pix = yini_pr_block*width + xini_pr_block;
    dif_pix = width - xfin_pr_block + xini_pr_block;
        
    hx = 0;
    hy = 0;
    count_hx = 0;
    count_hy = 0;
    
    
    //Computes Perceptual Relevance
    for (int y=yini_pr_block; y < yfin_pr_block; y++)  
    {
        for (int x=xini_pr_block; x < xfin_pr_block; x++)  
        {
            hop = hops_Y [pix];
            last_hop = HOP_0;
            top_hop = HOP_0;
            
            if (pix>0) 
                last_hop = hops_Y[pix-1];
            
            if (pix>width)
                top_hop = hops_Y[pix-width];
                    
            
            if (hop == HOP_POS_4 || hop == HOP_NEG_4) {
                hx += 4;
                hy += 4;
                count_hx++;
                count_hy++;
            } else {
                //Adapts index because hops go from 0 to 8 but 
                //hop weight goes from 0 to 4
                if (hop > HOP_0 && last_hop < HOP_0) 
                {
                    hx += hop - HOP_0; // only abs (-4...0...4)
                    count_hx++;
                } else if (hop < HOP_0 && last_hop > HOP_0) 
                {
                    hx += HOP_0 - hop;
                    count_hx++;
                } 
                
                if (hop > HOP_0 && top_hop < HOP_0) 
                {
                    hy += hop - HOP_0;
                    count_hy++;
                } else if (hop < HOP_0 && top_hop > HOP_0) 
                {
                    hy += HOP_0 - hop;
                    count_hy++;
                }
            }            
                                
            pix++;
        }
      
        pix+=dif_pix;

    }   
    

    if (count_hx == 0) 
    {
        perceptual_relevance_x[block_y][block_x] = 0;      
    } else 
    {
        prx = (1.0 * hx) / (count_hx * PR_HMAX);
            
        //PR HISTOGRAM EXPANSION
        prx = (prx-PR_MIN) / PR_DIF;
              
        //PR QUANTIZATION
        if (prx < PR_QUANT_1) {
            prx = PR_QUANT_0;
        } else if (prx < PR_QUANT_2) {
            prx = PR_QUANT_1;
        } else if (prx < PR_QUANT_3) {
            prx = PR_QUANT_2;
        } else if (prx < PR_QUANT_4) {
            prx = PR_QUANT_3;
        } else {
            prx = PR_QUANT_5;
        }       
 
        perceptual_relevance_x[block_y][block_x] = prx;
    }

    if (count_hy == 0) 
    {
        perceptual_relevance_y[block_y][block_x] = 0;
    } else 
    {
        pry = (1.0 * hy) / (count_hy * PR_HMAX);

        //PR HISTOGRAM EXPANSION
        pry = (pry-PR_MIN) / PR_DIF;
        
        //PR QUANTIZATION
        if (pry < PR_QUANT_1) {
            pry = PR_QUANT_0;
        } else if (pry < PR_QUANT_2) {
            pry = PR_QUANT_1;
        } else if (pry < PR_QUANT_3) {
            pry = PR_QUANT_2;
        } else if (pry < PR_QUANT_4) {
            pry = PR_QUANT_3;
        } else {
            pry = PR_QUANT_5;
        }
      
        perceptual_relevance_y[block_y][block_x] = pry;
    }            
}


/**
 * Computes perceptual relevance. 
 * @param **basic_block Basic block parameters
 * @param **perceptual_relevance_x Perceptual relevance in x
 * @param **perceptual_relevance_y Perceptual relevance in y
 * @param hops_Y luminance hops array
 * @param width image width
 * @param height image height
 * @param total_blocks_width total blocks widthwise
 * @param total_blocks_height total blocks heightwise
 * @param block_width block width
 * @param block_height height block
 */
static void lhe_advanced_compute_perceptual_relevance (BasicLheBlock **basic_block,
                                                       float **perceptual_relevance_x, float  **perceptual_relevance_y,
                                                       uint8_t *hops_Y,
                                                       int width, int height,
                                                       uint32_t total_blocks_width, uint32_t total_blocks_height,
                                                       uint32_t block_width, uint32_t block_height) 
{
    
    int xini, xfin, yini, yfin, xini_pr_block, xfin_pr_block, yini_pr_block, yfin_pr_block;
    
    #pragma omp parallel for
    for (int block_y=0; block_y<total_blocks_height+1; block_y++)      
    {  
        for (int block_x=0; block_x<total_blocks_width+1; block_x++) 
        {
            //First LHE Block coordinates
            xini = block_x * block_width;
            xfin = xini +  block_width;

            yini = block_y * block_height;
            yfin = yini + block_height;
            
            //PR Blocks coordinates 
            xini_pr_block = xini - (block_width >>1); 
            
            if (xini_pr_block < 0) 
            {
                xini_pr_block = 0;
            }
            
            xfin_pr_block = xfin - (block_width>>1);
            
            if (xfin_pr_block>width) 
            {
                xfin_pr_block = width;
            }    
            
            yini_pr_block = yini - (block_height>>1);
            
            if (yini_pr_block < 0) 
            {
                yini_pr_block = 0;
            }
            
            yfin_pr_block = yfin - (block_height>>1);
            
            if (yfin_pr_block>height)
            {
                yfin_pr_block = height;
            }
            
            //Calls method to compute perceptual relevance using calculated coordinates 
            lhe_advanced_compute_perceptual_relevance_block (perceptual_relevance_x, perceptual_relevance_y,
                                                             hops_Y,
                                                             xini_pr_block, xfin_pr_block, yini_pr_block, yfin_pr_block,
                                                             block_x, block_y,
                                                             width) ;                                                           
        }
    }
}

/**
 * Downsamples image in x coordinate with different resolution along the block. 
 * Samples are taken using sps with different cadence depending on ppp (pixels per pixel)
 * 
 * @param **basic_block Basic block parameters 
 * @param **advanced_block Advanced block parameters
 * @param ***ppp_array ppp (pixel per pixel) for each pixel and corner
 * @param *component_original_data original image
 * @param *downsampled_data final downsampled image in x coordinate
 * @param width_image image width
 * @param height_image height image
 * @param linesize rectangle images create a square image in ffmpeg memory. Linesize is width used by ffmpeg in memory
 * @param block_width block width
 * @param block_height height width
 * @param block_x block x index
 * @param block_y block y index
 */
static void lhe_advanced_horizontal_downsample_sps (BasicLheBlock **basic_block, AdvancedLheBlock **advanced_block,
                                                    uint8_t *component_original_data, 
                                                    uint8_t *downsampled_data,
                                                    int width_image, int height_image, int linesize,
                                                    int block_width, int block_height,
                                                    int block_x, int block_y) 
{
    uint32_t downsampled_x_side, xini, xdown, xfin_downsampled, yini, yfin;
    float xdown_float;
    float gradient, gradient_0, gradient_1, ppp_x, ppp_0, ppp_1, ppp_2, ppp_3;
    
    downsampled_x_side = advanced_block[block_y][block_x].downsampled_x_side;

    xini = basic_block[block_y][block_x].x_ini;
    xfin_downsampled = advanced_block[block_y][block_x].x_fin_downsampled;
 
    yini = basic_block[block_y][block_x].y_ini;
    yfin = basic_block[block_y][block_x].y_fin;  
        
    ppp_0=advanced_block[block_y][block_x].ppp_x[TOP_LEFT_CORNER];
    ppp_1=advanced_block[block_y][block_x].ppp_x[TOP_RIGHT_CORNER];
    ppp_2=advanced_block[block_y][block_x].ppp_x[BOT_LEFT_CORNER];
    ppp_3=advanced_block[block_y][block_x].ppp_x[BOT_RIGHT_CORNER];
    
    //gradient PPPx side a
    gradient_0=(ppp_2-ppp_0)/(block_height-1.0);   
    //gradient PPPx side b
    gradient_1=(ppp_3-ppp_1)/(block_height-1.0);

    for (int y=yini; y<yfin; y++)
    {        
        gradient=(ppp_1-ppp_0)/(downsampled_x_side-1.0); 

        ppp_x=ppp_0;
        xdown_float=xini;

        for (int x=xini; x<xfin_downsampled; x++)
        {
            xdown = xdown_float + 0.5;
                      
            downsampled_data[y*width_image+x]=component_original_data[y*linesize+xdown];

            ppp_x+=gradient;
            xdown_float+=ppp_x;

        }//x

        ppp_0+=gradient_0;
        ppp_1+=gradient_1;

    }//y
}

/**
 * Downsamples image in y coordinate with different resolution along the block. 
 * Samples are taken using sps with different cadence depending on ppp (pixels per pixel)
 * 
 * @param **basic_block Basic block parameters
 * @param **advanced_block Advanced block parameters 
 * @param ***ppp_array ppp (pixel per pixel) for each pixel and corner
 * @param *intermediate_downsample downsampled image in x coordinate
 * @param *downsampled_data final downsampled image
 * @param width_image image width
 * @param height_image height image
 * @param block_width block width
 * @param block_height height width
 * @param block_x block x index
 * @param block_y block y index
 */
static void lhe_advanced_vertical_downsample_sps (BasicLheBlock **basic_block, AdvancedLheBlock **advanced_block,
                                                  uint8_t *intermediate_downsample, 
                                                  uint8_t *downsampled_data,
                                                  int width_image, int height_image, int block_width, int block_height,
                                                  int block_x, int block_y) 
{
    
    float ppp_y, ppp_0, ppp_1, ppp_2, ppp_3, gradient, gradient_0, gradient_1;
    uint32_t downsampled_y_side, xini, xfin_downsampled, yini, ydown, yfin_downsampled;
    float ydown_float;
    
    downsampled_y_side = advanced_block[block_y][block_x].downsampled_y_side;
    
    xini = basic_block[block_y][block_x].x_ini;
    xfin_downsampled = advanced_block[block_y][block_x].x_fin_downsampled; //Vertical downsampling is performed after horizontal down. x coord has been already down.  
 
    yini = basic_block[block_y][block_x].y_ini;
    yfin_downsampled = advanced_block[block_y][block_x].y_fin_downsampled;

    ppp_0=advanced_block[block_y][block_x].ppp_y[TOP_LEFT_CORNER];
    ppp_1=advanced_block[block_y][block_x].ppp_y[TOP_RIGHT_CORNER];
    ppp_2=advanced_block[block_y][block_x].ppp_y[BOT_LEFT_CORNER];
    ppp_3=advanced_block[block_y][block_x].ppp_y[BOT_RIGHT_CORNER];

    //gradient PPPy side c
    gradient_0=(ppp_1-ppp_0)/(block_width-1.0);    
    //gradient PPPy side d
    gradient_1=(ppp_3-ppp_2)/(block_width-1.0);
  
    for (int x=xini; x < xfin_downsampled;x++)
    {
        gradient=(ppp_2-ppp_0)/(downsampled_y_side-1.0);
        ppp_y=ppp_0; 

        ydown_float=yini; 

        for (int y=yini; y < yfin_downsampled; y++)
        {
            ydown = ydown_float + 0.5;

            downsampled_data[y*width_image+x]=intermediate_downsample[ydown*width_image+x];;
                        
            ppp_y+=gradient;
            ydown_float+=ppp_y;
        }//ysc
        ppp_0+=gradient_0;
        ppp_2+=gradient_1;

    }//x
    
}

/**
 * Encodes block in Advanced LHE (downsampled image)
 * 
 * @param *prec Pointer to LHE precalculated data
 * @param **basic_block Basic block parameters
 * @param **advanced_block Advanced block parameters
 * @param *downsampled_data downsampled image
 * @param *component_prediction Component prediction
 * @param *hops hops array
 * @param width_image image width
 * @param height_image image height
 * @param *first_color_block first component value for each block
 * @param total_blocks_width number of blocks widthwise
 * @param block_x block x index
 * @param block_y block y index
 * @param block_width block width
 * @param block_height block height
 */
static void lhe_advanced_encode_block (LheBasicPrec *prec, 
                                       BasicLheBlock **basic_block, AdvancedLheBlock **advanced_block,
                                       uint8_t *downsampled_data, 
                                       uint8_t *component_prediction, uint8_t *hops, 
                                       int width_image, int height_image, 
                                       uint8_t *first_color_block, int total_blocks_width,
                                       int block_x, int block_y,
                                       int block_width, int block_height)
{      
    
    //Hops computation.
    int xini, xfin_downsampled, yini, yfin_downsampled;
    bool small_hop, last_small_hop;
    uint8_t predicted_luminance, hop_1, hop_number, original_color, r_max;
    int pix, dif_pix, num_block;
            
    num_block = block_y * total_blocks_width + block_x;
    
    //DOWNSAMPLED IMAGE
    xini = basic_block[block_y][block_x].x_ini;
    xfin_downsampled = advanced_block[block_y][block_x].x_fin_downsampled; 
 
    yini = basic_block[block_y][block_x].y_ini;
    yfin_downsampled = advanced_block[block_y][block_x].y_fin_downsampled;
    
    small_hop = false;
    last_small_hop=false;          // indicates if last hop is small
    predicted_luminance=0;         // predicted signal
    hop_1= START_HOP_1;
    hop_number=4;                  // pre-selected hop // 4 is NULL HOP
    pix=0;                         // pixel possition, from 0 to image size        
    original_color=0;              // original color
    
    r_max = PARAM_R;
    
    pix = yini*width_image + xini;       
    dif_pix = width_image - xfin_downsampled + xini;    

    for (int y=yini; y < yfin_downsampled; y++)  {
        for (int x=xini; x < xfin_downsampled; x++)  {
              
            original_color = downsampled_data[pix]; //This can't be pix because ffmpeg adds empty memory slots. 

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
                predicted_luminance=component_prediction[pix-width_image];
                last_small_hop=false;
                hop_1=START_HOP_1;
            } else if (x == xfin_downsampled -1) 
            {
                predicted_luminance=(component_prediction[pix-1]+component_prediction[pix-width_image])>>1;    
            } 
            else 
            {
                predicted_luminance=(component_prediction[pix-1]+component_prediction[pix+1-width_image])>>1;     
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
        }//for x
        pix+=dif_pix;
    }//for y    
}



/**
 * LHE advanced encoding
 * 
 * PR to PPP conversion
 * PPP to rectangle shape
 * Elastic Downsampling
 */
static float lhe_advanced_encode (LheContext *s, const AVFrame *frame, 
                                  BasicLheBlock **basic_block_Y, BasicLheBlock **basic_block_UV,
                                  AdvancedLheBlock **advanced_block_Y, AdvancedLheBlock **advanced_block_UV, 
                                  uint8_t *component_original_data_Y, uint8_t *component_original_data_U, uint8_t *component_original_data_V,
                                  float **perceptual_relevance_x, float **perceptual_relevance_y,
                                  uint8_t *component_prediction_Y, uint8_t *component_prediction_U, uint8_t *component_prediction_V,
                                  uint8_t *hops_Y, uint8_t *hops_U, uint8_t *hops_V,
                                  uint8_t *first_color_block_Y, uint8_t *first_color_block_U, uint8_t *first_color_block_V,
                                  uint32_t width_Y, uint32_t height_Y, uint32_t width_UV, uint32_t height_UV, 
                                  int linesize_Y, int linesize_U, int linesize_V,
                                  uint32_t total_blocks_width, uint32_t total_blocks_height, 
                                  uint32_t block_width_Y, uint32_t block_height_Y, uint32_t block_width_UV, uint32_t block_height_UV) 
{
    float compression_factor;
    uint8_t *downsampled_data_Y, *downsampled_data_U, *downsampled_data_V, *intermediate_downsample_Y, *intermediate_downsample_U, *intermediate_downsample_V;
    uint32_t image_size_Y, image_size_UV, ppp_max_theoric;
    
    uint8_t *component_prediction_flhe, *hops_flhe;
    int width_flhe, height_flhe, image_size_flhe, block_width_flhe, block_height_flhe;
        
    image_size_Y = width_Y * height_Y;
    image_size_UV = width_UV * height_UV;
    
    ppp_max_theoric = block_width_Y/SIDE_MIN;
    compression_factor = (&s->prec)->compression_factor[ppp_max_theoric][s->ql];

    downsampled_data_Y = malloc (sizeof(uint8_t) * image_size_Y);
    intermediate_downsample_Y = malloc (sizeof(uint8_t) * image_size_Y);
    
    downsampled_data_U = malloc (sizeof(uint8_t) * image_size_UV);
    intermediate_downsample_U = malloc (sizeof(uint8_t) * image_size_UV);
    
    downsampled_data_V = malloc (sizeof(uint8_t) * image_size_UV);
    intermediate_downsample_V = malloc (sizeof(uint8_t) * image_size_UV);
 
    width_flhe = (width_Y-1) / SPS_RATIO_WIDTH + 1;
    height_flhe = (height_Y-1) / SPS_RATIO_HEIGHT + 1;
    image_size_flhe = width_flhe * height_flhe;
    
    block_width_flhe = width_flhe / total_blocks_width;
    block_height_flhe = height_flhe / total_blocks_height;
    
    hops_flhe = malloc(sizeof(uint8_t) * image_size_flhe);
    component_prediction_flhe = malloc(sizeof(uint8_t) * image_size_flhe);
    
    #pragma omp parallel for
    for (int block_y=0; block_y<total_blocks_height; block_y++)      
    {  
        for (int block_x=0; block_x<total_blocks_width; block_x++) 
        {
                lhe_calculate_block_coordinates (basic_block_Y, basic_block_UV,
                                                block_width_Y, block_height_Y,                             
                                                block_width_UV, block_height_UV, 
                                                width_Y, height_Y,
                                                width_UV, height_UV,
                                                total_blocks_width, total_blocks_height,
                                                block_x, block_y);
            
            lhe_basic_encode_one_hop_per_pixel_block(&s->prec, 
                                                        basic_block_Y,
                                                        component_original_data_Y, component_prediction_flhe, hops_flhe,      
                                                        width_Y, width_flhe, height_Y, height_flhe, frame->linesize[0], 
                                                        first_color_block_Y, 
                                                        total_blocks_width, total_blocks_height,
                                                        block_x, block_y, 
                                                        block_width_Y, block_width_flhe, block_height_Y, block_height_flhe,
                                                        SPS_RATIO_WIDTH, SPS_RATIO_HEIGHT);
        }
    }

    lhe_advanced_compute_perceptual_relevance (basic_block_Y,
                                               perceptual_relevance_x, perceptual_relevance_y,
                                               hops_flhe,
                                               width_flhe,  height_flhe,
                                               total_blocks_width,  total_blocks_height,
                                               block_width_flhe,  block_height_flhe);
           
    #pragma omp parallel for
    for (int block_y=0; block_y<total_blocks_height; block_y++) 
    {
        for (int block_x=0; block_x<total_blocks_width; block_x++) 
        {          
                                              
            lhe_advanced_perceptual_relevance_to_ppp(advanced_block_Y, advanced_block_UV,
                                                     perceptual_relevance_x, perceptual_relevance_y, 
                                                     compression_factor, ppp_max_theoric, 
                                                     block_x, block_y);
            
            lhe_advanced_ppp_side_to_rectangle_shape (basic_block_Y, advanced_block_Y,
                                                      width_Y, height_Y, 
                                                      ppp_max_theoric,
                                                      block_x, block_y);
            
            lhe_advanced_ppp_side_to_rectangle_shape (basic_block_UV, advanced_block_UV,
                                                      width_UV, height_UV, 
                                                      ppp_max_theoric,
                                                      block_x, block_y);

            
            //LUMINANCE
            //Downsamples using component original data         
            lhe_advanced_horizontal_downsample_sps (basic_block_Y, advanced_block_Y,
                                                    component_original_data_Y, 
                                                    intermediate_downsample_Y,
                                                    width_Y, height_Y, linesize_Y,
                                                    block_width_Y, block_height_Y,
                                                    block_x, block_y);
                                                            
           
            lhe_advanced_vertical_downsample_sps (basic_block_Y, advanced_block_Y,
                                                  intermediate_downsample_Y, 
                                                  downsampled_data_Y,
                                                  width_Y, height_Y, block_width_Y, block_height_Y,
                                                  block_x, block_y);
                                                  
            //Encode downsampled blocks                          
            lhe_advanced_encode_block (&s->prec, 
                                       basic_block_Y, advanced_block_Y, 
                                       downsampled_data_Y, 
                                       component_prediction_Y, hops_Y, 
                                       width_Y,  height_Y, 
                                       first_color_block_Y, total_blocks_width,
                                       block_x,  block_y,
                                       block_width_Y,  block_height_Y);
                                       
            
            //CHROMINANCES
            
            //CHROMINANCE U
            lhe_advanced_horizontal_downsample_sps (basic_block_UV, advanced_block_UV,
                                                    component_original_data_U, 
                                                    intermediate_downsample_U,
                                                    width_UV, height_UV, linesize_U,
                                                    block_width_UV, block_height_UV,
                                                    block_x, block_y);
            
            lhe_advanced_vertical_downsample_sps (basic_block_UV, advanced_block_UV,  
                                                  intermediate_downsample_U, 
                                                  downsampled_data_U,
                                                  width_UV, height_UV, block_width_UV, block_height_UV,
                                                  block_x, block_y);
                                                                                                              
            lhe_advanced_encode_block (&s->prec, 
                                       basic_block_UV, advanced_block_UV, 
                                       downsampled_data_U, 
                                       component_prediction_U, hops_U, 
                                       width_UV,  height_UV, 
                                       first_color_block_U, total_blocks_width,
                                       block_x,  block_y,
                                       block_width_UV,  block_height_UV);
            


             
            //CHROMINANCE_V
            lhe_advanced_horizontal_downsample_sps (basic_block_UV, advanced_block_UV, 
                                                    component_original_data_V, 
                                                    intermediate_downsample_V,
                                                    width_UV, height_UV, linesize_V, 
                                                    block_width_UV, block_height_UV,
                                                    block_x, block_y);
            
            lhe_advanced_vertical_downsample_sps (basic_block_UV, advanced_block_UV,
                                                  intermediate_downsample_V, 
                                                  downsampled_data_V,
                                                  width_UV, height_UV, block_width_UV, block_height_UV,
                                                  block_x, block_y);
                          
                                                                 
            lhe_advanced_encode_block (&s->prec, 
                                       basic_block_UV, advanced_block_UV, 
                                       downsampled_data_V, 
                                       component_prediction_V, hops_V, 
                                       width_UV,  height_UV, 
                                       first_color_block_V, total_blocks_width,
                                       block_x,  block_y,
                                       block_width_UV,  block_height_UV); 
        }
    }
    
    
    return compression_factor;
}



//==================================================================
// ENCODE FRAME
//==================================================================

static int lhe_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                             const AVFrame *frame, int *got_packet)
{
    uint8_t *component_original_data_Y, *component_original_data_U, *component_original_data_V;
    uint8_t *component_prediction_Y, *component_prediction_U, *component_prediction_V;
    uint8_t *hops_Y, *hops_U, *hops_V;
    uint8_t *first_color_block_Y, *first_color_block_U, *first_color_block_V;
    uint32_t width_Y, width_UV, height_Y, height_UV, image_size_Y, image_size_UV; 
    uint32_t total_blocks_width, total_blocks_height, total_blocks, pixels_block;
    uint32_t block_width_Y, block_width_UV, block_height_Y, block_height_UV;
    
    float **perceptual_relevance_x,  **perceptual_relevance_y;
    float compression_factor;
    
    struct timeval before , after;

    BasicLheBlock **basic_block_Y, **basic_block_UV;
    AdvancedLheBlock **advanced_block_Y, **advanced_block_UV;

    LheContext *s = avctx->priv_data;

    
    width_Y = frame->width;
    height_Y =  frame->height; 
    image_size_Y = width_Y * height_Y;

    width_UV = (width_Y - 1)/s->chroma_factor_width + 1;
    height_UV = (height_Y - 1)/s->chroma_factor_height + 1;
    image_size_UV = width_UV * height_UV;
    
    total_blocks_width = HORIZONTAL_BLOCKS;
    pixels_block = width_Y / HORIZONTAL_BLOCKS;
    total_blocks_height = height_Y / pixels_block;
    
     //total_blocks_height = (height_Y - 1)/ BLOCK_HEIGHT_Y + 1;
     //total_blocks_width = (width_Y - 1) / BLOCK_WIDTH_Y + 1;
    
    total_blocks = total_blocks_height * total_blocks_width;

    block_width_Y = width_Y / total_blocks_width;
    block_height_Y = height_Y / total_blocks_height;       

    block_width_UV = width_UV / total_blocks_width;
    block_height_UV = height_UV / total_blocks_height;
    
    
    //Pointers to different color components
    component_original_data_Y = frame->data[0];
    component_original_data_U = frame->data[1];
    component_original_data_V = frame->data[2];
      
    component_prediction_Y = malloc(sizeof(uint8_t) * image_size_Y);  
    component_prediction_U = malloc(sizeof(uint8_t) * image_size_UV); 
    component_prediction_V = malloc(sizeof(uint8_t) * image_size_UV);  
    hops_Y = malloc(sizeof(uint8_t) * image_size_Y);
    hops_U = malloc(sizeof(uint8_t) * image_size_UV);
    hops_V = malloc(sizeof(uint8_t) * image_size_UV);
    first_color_block_Y = malloc(sizeof(uint8_t) * total_blocks);
    first_color_block_U = malloc(sizeof(uint8_t) * total_blocks);
    first_color_block_V = malloc(sizeof(uint8_t) * total_blocks);
    
    basic_block_Y = malloc(sizeof(BasicLheBlock *) * total_blocks_height);
    
    for (int i=0; i < total_blocks_height; i++)
    {
        basic_block_Y[i] = malloc (sizeof(BasicLheBlock) * (total_blocks_width));
    }
    
    basic_block_UV = malloc(sizeof(BasicLheBlock *) * total_blocks_height);
    
    for (int i=0; i < total_blocks_height; i++)
    {
        basic_block_UV[i] = malloc (sizeof(BasicLheBlock) * (total_blocks_width));
    }
    
    gettimeofday(&before , NULL);

    if (s->basic_lhe) 
    {
        //BASIC LHE        
        if(OPENMP_FLAGS == CONFIG_OPENMP) {
     
            lhe_basic_encode_frame_pararell (&s->prec, 
                                            basic_block_Y, basic_block_UV, 
                                            component_original_data_Y, component_original_data_U, component_original_data_V, 
                                            component_prediction_Y, component_prediction_U, component_prediction_V, 
                                            hops_Y, hops_U, hops_V,
                                            width_Y, width_Y, height_Y, height_Y, 
                                            width_UV, width_UV, height_UV, height_UV, 
                                            frame->linesize[0], frame->linesize[1], frame->linesize[2],
                                            first_color_block_Y, first_color_block_U, first_color_block_V,
                                            total_blocks_width, total_blocks_height,
                                            block_width_Y, block_width_Y, block_height_Y, block_height_Y,
                                            block_width_UV, block_width_UV, block_height_UV, block_height_UV,
                                            NO_SPS_RATIO, NO_SPS_RATIO );      
        } else 
        {
            total_blocks_height = 1;
            total_blocks_width = 1;
            total_blocks = 1;
            
            lhe_basic_encode_frame_sequential (&s->prec, 
                                               component_original_data_Y, component_original_data_U, component_original_data_V, 
                                               component_prediction_Y, component_prediction_U, component_prediction_V,
                                               hops_Y, hops_U, hops_V,
                                               width_Y, width_Y, height_Y, width_UV,  width_UV, height_UV, 
                                               frame->linesize[0], frame->linesize[1], frame->linesize[2],
                                               first_color_block_Y, first_color_block_U, first_color_block_V,
                                               NO_SPS_RATIO, NO_SPS_RATIO  );     
            
             
        }
        gettimeofday(&after , NULL);

        
        lhe_basic_write_lhe_file(avctx, pkt,image_size_Y,  width_Y,  height_Y,
                                 image_size_UV,  width_UV,  height_UV,
                                 total_blocks_width, total_blocks_height,
                                 first_color_block_Y, first_color_block_U, first_color_block_V, 
                                 hops_Y, hops_U, hops_V);  
                          
    } 
    else 
    {
        //ADVANCED LHE
        advanced_block_Y = malloc(sizeof(AdvancedLheBlock *) * total_blocks_height);
        
        for (int i=0; i < total_blocks_height; i++)
        {
            advanced_block_Y[i] = malloc (sizeof(AdvancedLheBlock) * (total_blocks_width));
        }
        
        advanced_block_UV = malloc(sizeof(AdvancedLheBlock *) * total_blocks_height);
        
        for (int i=0; i < total_blocks_height; i++)
        {
            advanced_block_UV[i] = malloc (sizeof(AdvancedLheBlock) * (total_blocks_width));
        }
             
        perceptual_relevance_x = malloc(sizeof(float*) * (total_blocks_height+1));  
    
        for (int i=0; i<total_blocks_height+1; i++) 
        {
            perceptual_relevance_x[i] = malloc(sizeof(float) * (total_blocks_width+1));
        }
        
        perceptual_relevance_y = malloc(sizeof(float*) * (total_blocks_height+1)); 
        
        for (int i=0; i<total_blocks_height+1; i++) 
        {
            perceptual_relevance_y[i] = malloc(sizeof(float) * (total_blocks_width+1));
        }   
    
        compression_factor =  lhe_advanced_encode (s, frame, 
                                                   basic_block_Y, basic_block_UV, 
                                                   advanced_block_Y, advanced_block_UV, 
                                                   component_original_data_Y, component_original_data_U, component_original_data_V,
                                                   perceptual_relevance_x, perceptual_relevance_y,
                                                   component_prediction_Y, component_prediction_U, component_prediction_V,
                                                   hops_Y, hops_U, hops_V,
                                                   first_color_block_Y, first_color_block_U, first_color_block_V,
                                                   width_Y, height_Y, width_UV, height_UV, 
                                                   frame->linesize[0], frame->linesize[1], frame->linesize[2],
                                                   total_blocks_width, total_blocks_height, 
                                                   block_width_Y, block_height_Y, block_width_UV, block_height_UV);
        gettimeofday(&after , NULL);

        av_log (NULL, AV_LOG_INFO, "Advanced LHE with ql = %d and cf = %f \n",s->ql, compression_factor); 


        lhe_advanced_write_lhe_file(avctx, pkt, 
                                    basic_block_Y, basic_block_UV, advanced_block_Y, advanced_block_UV,
                                    image_size_Y, width_Y, height_Y,
                                    image_size_UV, width_UV, height_UV,
                                    total_blocks_width, total_blocks_height,                                       
                                    block_width_Y, block_width_UV, block_height_Y, block_height_UV,
                                    first_color_block_Y, first_color_block_U, first_color_block_V,
                                    perceptual_relevance_x, perceptual_relevance_y,                                           
                                    hops_Y, hops_U, hops_V,
                                    s->ql);         
    }

    if(avctx->flags&AV_CODEC_FLAG_PSNR){
        lhe_compute_error_for_psnr (avctx, frame,
                                    height_Y, width_Y, height_UV, width_UV,
                                    component_original_data_Y, component_original_data_U, component_original_data_V,
                                    component_prediction_Y, component_prediction_U, component_prediction_V); 
    }
    
       
    if (s->pr_metrics)
    {
        print_csv_pr_metrics(perceptual_relevance_x, perceptual_relevance_y,
                             total_blocks_width, total_blocks_height);  
    }
    
    av_log(NULL, AV_LOG_INFO, "CodingTime %.0lf \n", time_diff(before , after));

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

#define OFFSET(x) offsetof(LheContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "pr_metrics", "Print PR metrics", OFFSET(pr_metrics), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, VE },
    { "basic_lhe", "Basic LHE", OFFSET(basic_lhe), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, VE },
    { "ql", "Quality level from 0 to 99", OFFSET(ql), AV_OPT_TYPE_INT, { .i64 = 50 }, 0, 99, VE },
    { NULL },
};


static const AVClass lhe_class = {
    .class_name = "LHE Basic encoder",
    .item_name  = av_default_item_name,
    .option     = options,
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
        AV_PIX_FMT_YUV420P, 
        AV_PIX_FMT_YUV422P, 
        AV_PIX_FMT_YUV444P, 
        AV_PIX_FMT_NONE
    },
    .priv_class     = &lhe_class,
};
