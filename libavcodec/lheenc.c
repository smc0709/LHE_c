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
    int pr_metrics;
} LheContext;

static av_cold int lhe_encode_init(AVCodecContext *avctx)
{
    LheContext *s = avctx->priv_data;

    lhe_init_cache(&s->prec);

    return 0;

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
        he_Y[i].code = 1024; //imposible code to initialize
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
        he_UV[i].code = 1024;
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
    uint64_t n_bits_hops, n_bytes, n_bytes_components, total_blocks;
    
    int i, ret;
        
    struct timeval before , after;
    
    LheHuffEntry he_Y[LHE_MAX_HUFF_SIZE];
    LheHuffEntry he_UV[LHE_MAX_HUFF_SIZE];

    LheContext *s = avctx->priv_data;
    
    total_blocks = total_blocks_height * total_blocks_width;
    
    gettimeofday(&before , NULL);


    n_bits_hops = lhe_gen_huffman (he_Y, he_UV, 
                                   hops_Y, hops_U, hops_V, 
                                   image_size_Y, image_size_UV);
    

    n_bytes_components = n_bits_hops/8;        
    
    //File size
    n_bytes = sizeof(width_Y) + sizeof(height_Y) //width and height
              + sizeof(total_blocks_height) + sizeof(total_blocks_width)
              + total_blocks * (sizeof(first_pixel_blocks_Y) + sizeof(first_pixel_blocks_U) + sizeof(first_pixel_blocks_V)) //first pixel blocks array value
              + LHE_HUFFMAN_TABLE_BYTES + //huffman table
              + n_bytes_components; //components

              
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
    
    //Write image
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

    av_log(NULL, AV_LOG_INFO, "LHE Write file %.0lf \n", time_diff(before , after));
    
    return n_bytes;
}


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

static void lhe_advanced_compute_perceptual_relevance_block (float **perceptual_relevance_x, float  **perceptual_relevance_y,
                                                             uint8_t *hops_Y,
                                                             int xini_pr_block, int xfin_pr_block, int yini_pr_block, int yfin_pr_block,
                                                             int coord_x, int coord_y,
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
        perceptual_relevance_x[coord_y][coord_x] = 0;
    } else 
    {
        prx = (PR_HMAX * hx) / count_hx;
        
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
 
        perceptual_relevance_x[coord_y][coord_x] = prx;
    }

    if (count_hy == 0) 
    {
        perceptual_relevance_y[coord_y][coord_x] = 0;
    } else 
    {
        pry = (PR_HMAX * hy) / count_hy;
        
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
      
        perceptual_relevance_y[coord_y][coord_x] = pry;
    }
            
}

static void lhe_advanced_compute_perceptual_relevance (float **perceptual_relevance_x, float  **perceptual_relevance_y,
                                                       uint8_t *hops_Y,
                                                       int width, int height,
                                                       uint32_t total_blocks_width, uint32_t total_blocks_height,
                                                       uint32_t block_width, uint32_t block_height) 
{
    
    int xini, xfin, yini, yfin, xini_pr_block, xfin_pr_block, yini_pr_block, yfin_pr_block;
    
    #pragma omp parallel for
    for (int coord_y=0; coord_y<total_blocks_height+1; coord_y++)      
    {  
        for (int coord_x=0; coord_x<total_blocks_width+1; coord_x++) 
        {
            
            xini = coord_x * block_width;
            xini_pr_block = xini - (((block_width - 1)>>1) + 1); 
            
            if (xini_pr_block < 0) 
            {
                xini_pr_block = 0;
            }
            
            xfin = xini +  block_width;
            xfin_pr_block = xfin - (((block_width-1)>>1) + 1);
            
            if (xfin_pr_block>width) 
            {
                xfin_pr_block = width;
            }    
            
            yini = coord_y * block_height;
            yini_pr_block = yini - (((block_width-1)>>1) + 1);
            
            if (yini_pr_block < 0) 
            {
                yini_pr_block = 0;
            }
            
            yfin = yini + block_height;
            yfin_pr_block = yfin - (((block_height-1)>>1) + 1);
            
            if (yfin_pr_block>height)
            {
                yfin_pr_block = height;
            }
            
            
            lhe_advanced_compute_perceptual_relevance_block (perceptual_relevance_x, perceptual_relevance_y,
                                                             hops_Y,
                                                             xini_pr_block, xfin_pr_block, yini_pr_block, yfin_pr_block,
                                                             coord_x, coord_y,
                                                             width) ;
                                                             
           
         
        }
    }
}

static void ppp_to_rectangle_shape (float *** ppp_x, float *** ppp_y, 
                                    int total_blocks_width, int total_blocks_height,
                                    int block_width, int block_height) {
    
    float max_ppp,side_c, side_d, weight, side_average, side_min, side_max, add;
    
    int subsampled_block_width;
    weight = 2;
    max_ppp = block_width / SIDE_MIN;
    
    #pragma omp parallel for
    for (int block_y=0; block_y<total_blocks_height; block_y++) 
    {
        for (int block_x=0; block_x<total_blocks_width; block_x++) 
        {
            side_c = ppp_x[block_y][block_x][0] + ppp_x[block_y][block_x][1];
            side_d = ppp_x[block_y][block_x][2] + ppp_x[block_y][block_x][2];
            
            side_average = side_c;
            if (side_c != side_d) {
                //horizontal sides adjustment   
                //---------------------------
                //side_min is the side whose ppp summation is bigger ( seems a contradiction but it is correct)
                //side max is the side whose resolution is bigger and ppp summation is lower
                side_min = side_c;
                side_max = side_d;
                if (side_min < side_max) {
                    side_min = side_d;
                    side_max = side_c;
                }
                
                side_average=side_max;
            }
            
            subsampled_block_width = (2 * block_width -1 ) / side_average + 1;
            
            side_average=2*block_width/subsampled_block_width;
            
            //adjust side c
            //--------------
            if (ppp_x[block_y][block_x][0]<=ppp_x[block_y][block_x][1])
            {       
                ppp_x[block_y][block_x][0]=side_average*ppp_x[block_y][block_x][0]/side_c;

                if (ppp_x[block_y][block_x][0]<1) {ppp_x[block_y][block_x][0]=1;}//PPPmin is 1 a PPP value <1 is not possible

                add = 0;
                ppp_x[block_y][block_x][1]=side_average-ppp_x[block_y][block_x][0];//+add1;
                if (ppp_x[block_y][block_x][1]>max_ppp) {add=ppp_x[block_y][block_x][1]-max_ppp; ppp_x[block_y][block_x][1]=max_ppp;}

                ppp_x[block_y][block_x][0]+=add;
            }
            else
            {
                ppp_x[block_y][block_x][1]=side_average*ppp_x[block_y][block_x][1]/side_c;

                if (ppp_x[block_y][block_x][1]<1) { ppp_x[block_y][block_x][1]=1;}//PPPmin is 1 a PPP value <1 is not possible
                
                add=0;
                ppp_x[block_y][block_x][0]=side_average-ppp_x[block_y][block_x][1];//+add0;
                if (ppp_x[block_y][block_x][0]>max_ppp) {add=ppp_x[block_y][block_x][0]-max_ppp; ppp_x[block_y][block_x][0]=max_ppp;}

                ppp_x[block_y][block_x][1]+=add;



            }

            //adjust side d
            if (ppp_x[block_y][block_x][2]<=ppp_x[block_y][block_x][3])
            {       
                ppp_x[block_y][block_x][2]=side_average*ppp_x[block_y][block_x][2]/side_d;

                
                if (ppp_x[block_y][block_x][2]<1) {ppp_x[block_y][block_x][2]=1;}// PPP can not be <1
                
                add=0;
                ppp_x[block_y][block_x][3]=side_average-ppp_x[block_y][block_x][2];//+add3;
                if (ppp_x[block_y][block_x][3]>max_ppp) {add=ppp_x[block_y][block_x][3]-max_ppp; ppp_x[block_y][block_x][3]=max_ppp;}

                ppp_x[block_y][block_x][2]+=add;

                //el problema es que podemos habernos pasado
            }
            else
            {
                ppp_x[block_y][block_x][3]=side_average*ppp_x[block_y][block_x][3]/side_d;

                if (ppp_x[block_y][block_x][3]<1) {ppp_x[block_y][block_x][3]=1;}

                add=0;
                ppp_x[block_y][block_x][2]=side_average-ppp_x[block_y][block_x][3];
                if (ppp_x[block_y][block_x][2]>max_ppp) {add=ppp_x[block_y][block_x][2]-max_ppp; ppp_x[block_y][block_x][2]=max_ppp;}
                ppp_x[block_y][block_x][3]+=add;

            }
            
        }
    }
}

static void lhe_advanced_perceptual_relevance_to_ppp (float compression_factor,
                                                      float *** ppp_x, float *** ppp_y, 
                                                      float ** perceptual_relevance_x, float ** perceptual_relevance_y,
                                                      int total_blocks_width, int total_blocks_height, int block_width) 
{
    float const1, const2, ppp_min, ppp_max;
    
    int ppp_max_theoric;
    
    ppp_max_theoric = block_width/SIDE_MIN;
    ppp_min = PPP_MIN;

    const1 = ppp_max_theoric - 1;
    const2 = ppp_max_theoric * compression_factor;
    
    
    #pragma omp parallel for
    for (int block_y=0; block_y<total_blocks_height; block_y++) 
    {
        for (int block_x=0; block_x<total_blocks_width; block_x++) 
        {
            ppp_x[block_y][block_x][0] = const2 / (1.0 + const1 * perceptual_relevance_x[block_y][block_x]);
            ppp_x[block_y][block_x][1] = const2 / (1.0 + const1 * perceptual_relevance_x[block_y][block_x+1]);     
            ppp_x[block_y][block_x][2] = const2 / (1.0 + const1 * perceptual_relevance_x[block_y+1][block_x]);  
            ppp_x[block_y][block_x][3] = const2 / (1.0 + const1 * perceptual_relevance_x[block_y+1][block_x+1]);
            

            ppp_y[block_y][block_x][0] = const2 / (1.0 + const1 * perceptual_relevance_y[block_y][block_x]);    
            ppp_y[block_y][block_x][1] = const2 / (1.0 + const1 * perceptual_relevance_y[block_y][block_x+1]);   
            ppp_y[block_y][block_x][2] = const2 / (1.0 + const1 * perceptual_relevance_y[block_y+1][block_x]);        
            ppp_y[block_y][block_x][3] = const2 / (1.0 + const1 * perceptual_relevance_y[block_y+1][block_x+1]);
            
            
             //Looks for ppp_min
            if (ppp_x[block_y][block_x][0] < ppp_min) ppp_min = ppp_x[block_y][block_x][0];
            if (ppp_x[block_y][block_x][1] < ppp_min) ppp_min = ppp_x[block_y][block_x][1];
            if (ppp_x[block_y][block_x][2] < ppp_min) ppp_min = ppp_x[block_y][block_x][2];
            if (ppp_x[block_y][block_x][3] < ppp_min) ppp_min = ppp_x[block_y][block_x][3];
            if (ppp_y[block_y][block_x][0] < ppp_min) ppp_min = ppp_y[block_y][block_x][0];
            if (ppp_y[block_y][block_x][1] < ppp_min) ppp_min = ppp_y[block_y][block_x][1];
            if (ppp_y[block_y][block_x][2] < ppp_min) ppp_min = ppp_y[block_y][block_x][2];
            if (ppp_y[block_y][block_x][3] < ppp_min) ppp_min = ppp_y[block_y][block_x][3];
            
            //Max elastic restriction
            ppp_max = ppp_min * ELASTIC_MAX;
            
            if (ppp_max > ppp_max_theoric) ppp_max = ppp_max_theoric;
            
            //Adjust values
            if (ppp_x[block_y][block_x][0]> ppp_max) ppp_x[block_y][block_x][0] = ppp_max;
            if (ppp_x[block_y][block_x][0]< PPP_MIN) ppp_x[block_y][block_x][0] = PPP_MIN;
            if (ppp_x[block_y][block_x][1]> ppp_max) ppp_x[block_y][block_x][1] = ppp_max;
            if (ppp_x[block_y][block_x][1]< PPP_MIN) ppp_x[block_y][block_x][1] = PPP_MIN;
            if (ppp_x[block_y][block_x][2]> ppp_max) ppp_x[block_y][block_x][2] = ppp_max;
            if (ppp_x[block_y][block_x][2]< PPP_MIN) ppp_x[block_y][block_x][2] = PPP_MIN;
            if (ppp_x[block_y][block_x][3]> ppp_max) ppp_x[block_y][block_x][3] = ppp_max;
            if (ppp_x[block_y][block_x][3]< PPP_MIN) ppp_x[block_y][block_x][3] = PPP_MIN;   
            if (ppp_y[block_y][block_x][0]> ppp_max) ppp_y[block_y][block_x][0] = ppp_max;
            if (ppp_y[block_y][block_x][0]< PPP_MIN) ppp_y[block_y][block_x][0] = PPP_MIN;
            if (ppp_y[block_y][block_x][1]> ppp_max) ppp_y[block_y][block_x][1] = ppp_max;
            if (ppp_y[block_y][block_x][1]< PPP_MIN) ppp_y[block_y][block_x][1] = PPP_MIN;
            if (ppp_y[block_y][block_x][2]> ppp_max) ppp_y[block_y][block_x][2] = ppp_max;
            if (ppp_y[block_y][block_x][2]< PPP_MIN) ppp_y[block_y][block_x][2] = PPP_MIN;
            if (ppp_y[block_y][block_x][3]> ppp_max) ppp_y[block_y][block_x][3] = ppp_max;
            if (ppp_y[block_y][block_x][3]< PPP_MIN) ppp_y[block_y][block_x][3] = PPP_MIN;
               
        }
    }
}


static void lhe_basic_encode_one_hop_per_pixel_block (LheBasicPrec *prec, uint8_t *component_original_data, 
                                                      uint8_t *component_prediction, uint8_t *hops, 
                                                      int width_image, int width_sps, int height_image, int height_sps, int linesize, 
                                                      uint8_t *first_color_block, int total_blocks_width,
                                                      int block_x, int block_y,
                                                      int block_width, int block_width_sps, int block_height, int block_height_sps,
                                                      uint8_t sps_ratio_width, uint8_t sps_ratio_height)
{      
    
    //Hops computation.
    int xini, xini_sps, xfin, xfin_sps, yini, yini_sps, yfin_sps;
    bool small_hop, last_small_hop;
    uint8_t predicted_luminance, hop_1, hop_number, original_color, r_max;
    int pix, pix_original_data, dif_line, sps_line_pix, dif_pix ,num_block;
    
    num_block = block_y * total_blocks_width + block_x;
    
    //ORIGINAL IMAGE
    xini = block_x * block_width;
    xfin = xini + block_width;
    if (xfin>width_image) 
    {
        xfin = width_image;
    }
    yini = block_y * block_height;
    
    
    //SPS IMAGE
    xini_sps = block_x * block_width_sps;
    xfin_sps = xini_sps + block_width_sps;
    if (xfin_sps>width_sps) 
    {
        xfin_sps = width_sps;
    }
    yini_sps = block_y * block_height_sps;
    yfin_sps = yini_sps + block_height_sps;
    if (yfin_sps>height_sps)
    {
        yfin_sps = height_sps;
    }
    
    
    small_hop = false;
    last_small_hop=false;          // indicates if last hop is small
    predicted_luminance=0;         // predicted signal
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

            //prediction of signal (predicted_luminance) , based on pixel's coordinates 
            //----------------------------------------------------------
                        
            if (x == xini_sps && y==yini_sps) 
            {
                predicted_luminance=original_color;//first pixel always is perfectly predicted! :-)  
                first_color_block[num_block] = original_color;
            } 
            else if (y == yini_sps) 
            {
                predicted_luminance=component_prediction[pix-1];
            } 
            else if (x == xini_sps) 
            {
                predicted_luminance=component_prediction[pix-width_sps];
                last_small_hop=false;
                hop_1=START_HOP_1;
            } else if (x == xfin_sps -1) 
            {
                predicted_luminance=(component_prediction[pix-1]+component_prediction[pix-width_sps])>>1;                               
            } 
            else 
            {
                predicted_luminance=(component_prediction[pix-1]+component_prediction[pix+1-width_sps])>>1;     
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
            pix_original_data+=sps_ratio_width;
        }//for x
        pix+=dif_pix;
        pix_original_data+=sps_line_pix;
    }//for y     
}

static void lhe_basic_encode_one_hop_per_pixel (LheBasicPrec *prec, uint8_t *component_original_data, 
                                                uint8_t *component_prediction, uint8_t *hops, 
                                                int width_image, int width_sps, int height_sps, int linesize, 
                                                uint8_t *first_color_block,
                                                uint8_t sps_ratio_width, uint8_t sps_ratio_height )
{      

    //Hops computation.
    bool small_hop, last_small_hop;
    uint8_t predicted_luminance, hop_1, hop_number, original_color, r_max;
    int pix, pix_original_data, dif_line, sps_line_pix, x, y;

    small_hop = false;
    last_small_hop=false;          // indicates if last hop is small
    predicted_luminance=0;         // predicted signal
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
    
    for (y=0; y < height_sps; y++)  {
        for (x=0; x < width_sps; x++)  {
            
            
            original_color = component_original_data[pix_original_data];    
        
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
                predicted_luminance=component_prediction[pix-width_sps];
                last_small_hop=false;
                hop_1=START_HOP_1;  
            } 
            else if (x == width_sps -1)
            {
                predicted_luminance=(component_prediction[pix-1]+component_prediction[pix-width_sps])>>1;                               
            }
            else 
            {
                predicted_luminance = (component_prediction[pix-1]+component_prediction[pix+1-width_sps])>>1; 
            }
            
            
            hop_number = prec->best_hop[r_max][hop_1][original_color][predicted_luminance];            
            component_prediction[pix]=prec -> prec_luminance[predicted_luminance][r_max][hop_1][hop_number];  
            hops[pix]= hop_number;
                        
            H1_ADAPTATION;
            pix++;   
            pix_original_data+=sps_ratio_width;

        }
        pix_original_data+=sps_line_pix;            
    }    
    
}


static void lhe_basic_encode_frame_pararell (LheBasicPrec *prec, 
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
    for (int j=0; j<total_blocks_height; j++)      
    {  
        for (int i=0; i<total_blocks_width; i++) 
        {

            //Luminance
            lhe_basic_encode_one_hop_per_pixel_block(prec, component_original_data_Y, component_prediction_Y, hops_Y,      
                                                     width_Y, width_sps_Y, height_Y, height_sps_Y, linesize_Y,
                                                     first_color_block_Y, total_blocks_width,
                                                     i, j, block_width_Y, block_width_sps_Y, block_height_Y, block_height_sps_Y,
                                                     sps_ratio_width, sps_ratio_height );

            
            //Crominance U
            lhe_basic_encode_one_hop_per_pixel_block(prec, component_original_data_U, component_prediction_U, hops_U,
                                                     width_UV, width_sps_UV, height_UV, height_sps_UV, linesize_U, 
                                                     first_color_block_U, total_blocks_width,
                                                     i, j, block_width_UV, block_width_sps_UV, block_height_UV, block_height_sps_UV, 
                                                     sps_ratio_width, sps_ratio_height  ); 

            //Crominance V
            lhe_basic_encode_one_hop_per_pixel_block(prec, component_original_data_V, component_prediction_V, hops_V, 
                                                     width_UV, width_sps_UV, height_UV, height_sps_UV, linesize_V, 
                                                     first_color_block_V, total_blocks_width,
                                                     i, j, block_width_UV, block_width_sps_UV, block_height_UV, block_height_sps_UV,
                                                     sps_ratio_width, sps_ratio_height  );
                                                     
                                               
        }
    }  
}


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


static int lhe_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                             const AVFrame *frame, int *got_packet)
{
    uint8_t *component_original_data_Y, *component_original_data_U, *component_original_data_V;
    uint8_t *component_prediction_Y, *component_prediction_U, *component_prediction_V;
    uint8_t *hops_Y, *hops_U, *hops_V;
    uint8_t *first_color_block_Y, *first_color_block_U, *first_color_block_V;
    float **perceptual_relevance_x,  **perceptual_relevance_y;
    float ***ppp_x, ***ppp_y;
    int width_Y, width_UV, height_Y, height_UV, image_size_Y, image_size_UV, n_bytes; 
    int total_blocks_width, total_blocks_height, total_blocks, pixels_block, total_blocks_width_pr, total_blocks_height_pr;
    int block_width_Y, block_width_UV, block_height_Y, block_height_UV, block_width_pr, block_height_pr;
    int i,j;
    
    uint8_t *component_original_data_flhe, *component_prediction_flhe, *hops_flhe;
    int width_flhe, height_flhe, image_size_flhe, block_width_flhe, block_height_flhe;
    
    struct timeval before , after;

    LheContext *s = avctx->priv_data;

    width_Y = frame->width;
    height_Y =  frame->height; 
    image_size_Y = width_Y * height_Y;

    width_UV = (width_Y - 1)/CHROMA_FACTOR_WIDTH + 1;
    height_UV = (height_Y - 1)/CHROMA_FACTOR_HEIGHT + 1;
    image_size_UV = width_UV * height_UV;
    
    total_blocks_width_pr = HORIZONTAL_BLOCKS;
    pixels_block = (width_Y-1) / HORIZONTAL_BLOCKS + 1;
    total_blocks_height_pr = (height_Y-1) / pixels_block + 1;
    
     //total_blocks_height_pr = (height_Y - 1)/ BLOCK_HEIGHT_Y + 1;
     //total_blocks_width_pr = (width_Y - 1) / BLOCK_WIDTH_Y + 1;
    
    block_width_pr = (width_Y-1)/total_blocks_width_pr + 1;
    block_height_pr = (height_Y-1)/total_blocks_height_pr + 1;
        
    
    if (OPENMP_FLAGS == CONFIG_OPENMP) 
    {
        total_blocks_width = HORIZONTAL_BLOCKS;
        total_blocks_height = total_blocks_height_pr;
     
        total_blocks = total_blocks_height * total_blocks_width;
        
        block_width_Y = block_width_pr;
        block_height_Y = block_height_pr;
        block_width_UV = (width_UV-1)/total_blocks_width + 1;
        block_height_UV = (height_UV-1)/total_blocks_height +1;
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
    component_prediction_U = malloc(sizeof(uint8_t) * image_size_UV); 
    component_prediction_V = malloc(sizeof(uint8_t) * image_size_UV);  
    hops_Y = malloc(sizeof(uint8_t) * image_size_Y);
    hops_U = malloc(sizeof(uint8_t) * image_size_UV);
    hops_V = malloc(sizeof(uint8_t) * image_size_UV);
    first_color_block_Y = malloc(sizeof(uint8_t) * total_blocks);
    first_color_block_U = malloc(sizeof(uint8_t) * total_blocks);
    first_color_block_V = malloc(sizeof(uint8_t) * total_blocks);


    //BASIC LHE
    if(OPENMP_FLAGS == CONFIG_OPENMP) {
     
        lhe_basic_encode_frame_pararell (&s->prec, 
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
        lhe_basic_encode_frame_sequential (&s->prec, 
                                           component_original_data_Y, component_original_data_U, component_original_data_V, 
                                           component_prediction_Y, component_prediction_U, component_prediction_V,
                                           hops_Y, hops_U, hops_V,
                                           width_Y, width_Y, height_Y, width_UV,  width_UV, height_UV, 
                                           frame->linesize[0], frame->linesize[1], frame->linesize[2],
                                           first_color_block_Y, first_color_block_U, first_color_block_V,
                                           NO_SPS_RATIO, NO_SPS_RATIO  );        
    }


    //ADVANCED LHE
    gettimeofday(&before , NULL);

    width_flhe = (width_Y-1) / SPS_RATIO_WIDTH + 1;
    height_flhe = (height_Y-1) / SPS_RATIO_HEIGHT + 1;
    image_size_flhe = width_flhe * height_flhe;
    
    block_width_flhe = (width_flhe-1) / total_blocks_width + 1;
    block_height_flhe = (height_flhe-1) / total_blocks_height + 1;
    
    component_original_data_flhe = malloc(sizeof(uint8_t) * image_size_flhe);
    hops_flhe = malloc(sizeof(uint8_t) * image_size_flhe);
    component_prediction_flhe = malloc(sizeof(uint8_t) * image_size_flhe);
    
    perceptual_relevance_x = malloc(sizeof(float*) * (total_blocks_height_pr+1));  
    
    for (i=0; i<total_blocks_height_pr+1; i++) 
    {
        perceptual_relevance_x[i] = malloc(sizeof(float) * (total_blocks_width_pr+1));
    }
    
    perceptual_relevance_y = malloc(sizeof(float*) * (total_blocks_height_pr+1)); 
    
    for (i=0; i<total_blocks_height_pr+1; i++) 
    {
        perceptual_relevance_y[i] = malloc(sizeof(float) * (total_blocks_width_pr+1));
    }   
    
    ppp_x = malloc(sizeof(float**) * (total_blocks_height_pr+1));  
    
    for (i=0; i<total_blocks_height_pr+1; i++) 
    {
        ppp_x[i] = malloc(sizeof(float*) * (total_blocks_width_pr+1));
        
        for (j=0; j<total_blocks_width+1; j++) 
        {
            ppp_x[i][j] = malloc(sizeof(float) * CORNERS);
        }
    }
    
     ppp_y = malloc(sizeof(float**) * (total_blocks_height_pr+1));  
    
    for (i=0; i<total_blocks_height_pr+1; i++) 
    {
        ppp_y[i] = malloc(sizeof(float*) * (total_blocks_width_pr+1));
        
        for (j=0; j<total_blocks_width+1; j++) 
        {
            ppp_y[i][j] = malloc(sizeof(float) * CORNERS);
        }
    }
    
    

    if(OPENMP_FLAGS == CONFIG_OPENMP) {
        #pragma omp parallel for
        for (int j=0; j<total_blocks_height; j++)      
        {  
            for (int i=0; i<total_blocks_width; i++) 
            {
                
                lhe_basic_encode_one_hop_per_pixel_block(&s->prec, component_original_data_Y, component_prediction_flhe, hops_flhe,      
                                                         width_Y, width_flhe, height_Y, height_flhe, frame->linesize[0], 
                                                         first_color_block_Y, total_blocks_width,
                                                         i, j, block_width_Y, block_width_flhe, block_height_Y, block_height_flhe,
                                                         SPS_RATIO_WIDTH, SPS_RATIO_HEIGHT   );
            }
        }

                                       
                               
    } else 
    {
        lhe_basic_encode_one_hop_per_pixel(&s->prec, 
                                           component_original_data_Y, component_prediction_flhe, hops_flhe, 
                                           width_Y, width_flhe, height_flhe, frame->linesize[0], first_color_block_Y,
                                           SPS_RATIO_WIDTH, SPS_RATIO_HEIGHT  );        
    }

    lhe_advanced_compute_perceptual_relevance (perceptual_relevance_x, perceptual_relevance_y,
                                               hops_flhe,
                                               width_flhe,  height_flhe,
                                               total_blocks_width_pr,  total_blocks_height_pr,
                                               block_width_flhe,  block_height_flhe);
    
    lhe_advanced_perceptual_relevance_to_ppp (1, 
                                              ppp_x, ppp_y,
                                              perceptual_relevance_x, perceptual_relevance_y,
                                              total_blocks_width, total_blocks_height, block_width_pr);
    
    ppp_to_rectangle_shape (ppp_x, ppp_y, 
                            total_blocks_width,total_blocks_height,
                            block_width_pr, block_height_pr);
                                               
    
    gettimeofday(&after , NULL);


    n_bytes = lhe_write_lhe_file(avctx, pkt,image_size_Y,  width_Y,  height_Y,
                                 image_size_UV,  width_UV,  height_UV,
                                 total_blocks_width, total_blocks_height,
                                 first_color_block_Y, first_color_block_U, first_color_block_V, 
                                 hops_Y, hops_U, hops_V);
    
    if (s->pr_metrics)
    {
        print_csv_pr_metrics(perceptual_relevance_x, perceptual_relevance_y,
                         total_blocks_width_pr, total_blocks_height_pr);  
    }
    
    /*
    for (int i=0; i<total_blocks_height_pr; i++) {
        for (int j=0; j<total_blocks_width; j++) {
            for (int c=0; c<CORNERS; c++) {
                    av_log(NULL, AV_LOG_INFO, "%f %f \n", ppp_x[i][j][c], ppp_y[i][j][c]);
            }
        }
    }
    */
   
    if(avctx->flags&AV_CODEC_FLAG_PSNR){
        lhe_compute_error_for_psnr (avctx, frame,
                                    height_Y, width_Y, height_UV, width_UV,
                                    component_original_data_Y, component_original_data_U, component_original_data_V,
                                    component_prediction_Y, component_prediction_U, component_prediction_V); 
        
    }
    
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

#define OFFSET(x) offsetof(LheContext, x)
#define VE AV_OPT_FLAG_VIDEO_PARAM | AV_OPT_FLAG_ENCODING_PARAM
static const AVOption options[] = {
    { "pr_metrics", "Print PR metrics", OFFSET(pr_metrics), AV_OPT_TYPE_BOOL, { .i64 = 0 }, 0, 1, VE },
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
        AV_PIX_FMT_YUV422P, AV_PIX_FMT_GRAY8, AV_PIX_FMT_NONE
    },
    .priv_class     = &lhe_class,
};
