/*
 * LHE Basic decoder
 */

#include "bytestream.h"
#include "internal.h"
#include "lhe.h"

#define H1_ADAPTATION                                   \
if (hop<=HOP_POS_1 && hop>=HOP_NEG_1)                   \
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
        he[i].code = 1024;
    }  
    
    lhe_generate_huffman_codes(he);
       
}

static uint8_t lhe_translate_huffman_into_symbol (uint32_t huffman_symbol, LheHuffEntry *he, uint32_t pix, uint8_t count_bits) 
{
    uint8_t symbol;
        
    symbol = NO_SYMBOL;
    
    if (huffman_symbol == he[HOP_0].code && he[HOP_0].len == count_bits)
    {
        symbol = HOP_0;
    } 
    else if (huffman_symbol == he[HOP_POS_1].code && he[HOP_POS_1].len == count_bits)
    {
        symbol = HOP_POS_1;
    } 
    else if (huffman_symbol == he[HOP_NEG_1].code && he[HOP_NEG_1].len == count_bits)
    {
        symbol = HOP_NEG_1;
    } 
    else if (huffman_symbol == he[HOP_POS_2].code && he[HOP_POS_2].len == count_bits)
    {
        symbol = HOP_POS_2;
    } 
    else if (huffman_symbol == he[HOP_NEG_2].code && he[HOP_NEG_2].len == count_bits)
    {
        symbol = HOP_NEG_2;
    }
    else if (huffman_symbol == he[HOP_POS_3].code && he[HOP_POS_3].len == count_bits)
    {
        symbol = HOP_POS_3;
    }
    else if (huffman_symbol == he[HOP_NEG_3].code && he[HOP_NEG_3].len == count_bits)
    {
        symbol = HOP_NEG_3;
    }
    else if (huffman_symbol == he[HOP_POS_4].code && he[HOP_POS_4].len == count_bits)
    {
        symbol = HOP_POS_4;
    } 
    else if (huffman_symbol == he[HOP_NEG_4].code && he[HOP_NEG_4].len == count_bits)
    {
        symbol = HOP_NEG_4;
    } 
    
    return symbol;
    
}

static void lhe_basic_read_file_symbols (LheState *s, LheHuffEntry *he, uint32_t image_size, uint8_t *symbols) 
{
    uint8_t symbol, count_bits;
    uint32_t huffman_symbol, decoded_symbols;
    
    symbol = NO_SYMBOL;
    decoded_symbols = 0;
    huffman_symbol = 0;
    count_bits = 0;
    
    while (decoded_symbols<image_size) {
        
        huffman_symbol = (huffman_symbol<<1) | get_bits(&s->gb, 1);
        count_bits++;
        
        symbol = lhe_translate_huffman_into_symbol(huffman_symbol, he, decoded_symbols, count_bits);        
        
        if (symbol != NO_SYMBOL) 
        {
            symbols[decoded_symbols] = symbol;
            decoded_symbols++;
            huffman_symbol = 0;
            count_bits = 0;
        }       
    }
}

static void lhe_advanced_read_file_symbols (LheState *s, LheHuffEntry *he, AdvancedLheBlock **block_array,
                                            uint8_t *symbols, 
                                            uint32_t width, uint32_t height, 
                                            int block_x, int block_y, bool notoquenada) 
{
    uint8_t symbol, count_bits;
    uint32_t huffman_symbol, pix;
    uint32_t xini, xfin_downsampled, yini, yfin_downsampled;
    
    xini = block_array[block_y][block_x].x_ini;
    xfin_downsampled = block_array[block_y][block_x].x_fin_downsampled; 
 
    yini = block_array[block_y][block_x].y_ini;
    yfin_downsampled = block_array[block_y][block_x].y_fin_downsampled;
    
    symbol = NO_SYMBOL;
    pix = 0;
    huffman_symbol = 0;
    count_bits = 0;

    for (int y=yini; y<yfin_downsampled;y++) 
    {
        for(int x=xini; x<xfin_downsampled;) 
        {
            pix = y * width + x;

            huffman_symbol = (huffman_symbol<<1) | get_bits(&s->gb, 1);
            count_bits++;
            
            symbol = lhe_translate_huffman_into_symbol(huffman_symbol, he, pix, count_bits);        
            
            if (symbol != NO_SYMBOL) 
            {
                symbols[pix] = symbol;
                x++;
                huffman_symbol = 0;
                count_bits = 0;
            }
        }
    }
}

static float lhe_advance_translate_pr_interval_to_pr_quant (uint8_t perceptual_relevance_interval)
{
    float perceptual_relevance_quant;
    
    switch (perceptual_relevance_interval) 
    {
        case PR_INTERVAL_0:
            perceptual_relevance_quant = PR_QUANT_0;
            break;
        case PR_INTERVAL_1:
            perceptual_relevance_quant = PR_QUANT_1;
            break;
        case PR_INTERVAL_2:
            perceptual_relevance_quant = PR_QUANT_2;
            break;
        case PR_INTERVAL_3:
            perceptual_relevance_quant = PR_QUANT_3;
            break;
        case PR_INTERVAL_4:
            perceptual_relevance_quant = PR_QUANT_4;
            break;
        case PR_INTERVAL_5:
            perceptual_relevance_quant = PR_QUANT_5;
            break;
            
    }
    
    return perceptual_relevance_quant;
}

static void lhe_advanced_read_mesh (LheState *s, AdvancedLheBlock **block_array_Y, AdvancedLheBlock **block_array_UV,
                                    float ** perceptual_relevance_x, float ** perceptual_relevance_y,
                                    float ***ppp_x, float ***ppp_y,
                                    uint32_t **downsampled_side_x_array, uint32_t **downsampled_side_y_array,
                                    float ppp_max_theoric, float compression_factor,
                                    uint32_t width_Y, uint32_t height_Y, uint32_t width_UV, uint32_t height_UV,
                                    uint32_t block_width_Y, uint32_t block_height_Y, uint32_t block_width_UV, uint32_t block_height_UV,
                                    uint32_t total_blocks_width, uint32_t total_blocks_height) 
{
    uint8_t perceptual_relevance_x_interval, perceptual_relevance_y_interval;
    float ppp_max;
    
    for (int block_y=0; block_y<total_blocks_height+1; block_y++) 
    {
        for (int block_x=0; block_x<total_blocks_width+1; block_x++) 
        { 
            perceptual_relevance_x_interval = get_bits(&s->gb, PR_INTERVAL_BITS); 
            perceptual_relevance_y_interval = get_bits(&s->gb, PR_INTERVAL_BITS); 
                        
            perceptual_relevance_x[block_y][block_x] = lhe_advance_translate_pr_interval_to_pr_quant(perceptual_relevance_x_interval);
            perceptual_relevance_y[block_y][block_x] = lhe_advance_translate_pr_interval_to_pr_quant(perceptual_relevance_y_interval);
            
        }
    }
    

    for (int block_y=0; block_y<total_blocks_height; block_y++)
    {
        for (int block_x=0; block_x<total_blocks_width; block_x++)
        {
            calculate_block_coordinates (block_array_Y, block_array_UV, 
                                         block_width_Y, block_height_Y,                             
                                         block_width_UV, block_height_UV, 
                                         width_Y, height_Y,
                                         width_UV, height_UV,
                                         block_x, block_y);

            ppp_max = lhe_advanced_perceptual_relevance_to_ppp(ppp_x, ppp_y, 
                                                               perceptual_relevance_x, perceptual_relevance_y, 
                                                               compression_factor, ppp_max_theoric, 
                                                               block_x, block_y);
             
            lhe_advanced_ppp_side_to_rectangle_shape (block_array_Y, block_array_UV, downsampled_side_x_array, downsampled_side_y_array,
                                                      ppp_x, ppp_y,
                                                      block_width_Y, ppp_max_theoric,
                                                      block_x, block_y);
     
        }
    }
}

static void lhe_advanced_decode_one_hop_per_pixel_block (LheBasicPrec *prec, AdvancedLheBlock **block_array,
                                                         uint8_t *hops, uint8_t *image,
                                                         uint32_t width, uint32_t height, int linesize,
                                                         uint8_t *first_color_block, uint32_t total_blocks_width,
                                                         uint32_t block_x, uint32_t block_y,
                                                         uint32_t block_width, uint32_t block_height) 
{
       
    //Hops computation.
    uint32_t xini, xfin_downsampled, yini, yfin_downsampled;
    bool small_hop, last_small_hop;
    uint8_t hop, predicted_luminance, hop_1, r_max; 
    uint32_t pix, pix_hops_data, dif_pix, dif_hops, num_block;
    
    num_block = block_y * total_blocks_width + block_x;
    
    xini = block_array[block_y][block_x].x_ini;
    xfin_downsampled = block_array[block_y][block_x].x_fin_downsampled; 
 
    yini = block_array[block_y][block_x].y_ini;
    yfin_downsampled = block_array[block_y][block_x].y_fin_downsampled;
    
    small_hop           = false;
    last_small_hop      = false;        // indicates if last hop is small
    predicted_luminance = 0;            // predicted signal
    hop_1               = START_HOP_1;
    pix                 = 0;            // pixel possition, from 0 to image size        
    r_max               = PARAM_R;        
    
 
    pix = yini*linesize + xini; 
    pix_hops_data = yini*width + xini;
    dif_pix = linesize - xfin_downsampled + xini;
    dif_hops = width - xfin_downsampled + xini;
    
    for (int y=yini; y < yfin_downsampled; y++)  {
        for (int x=xini; x < xfin_downsampled; x++)     {
            
            hop = hops[pix_hops_data]; 
  
            if (x == xini && y==yini) 
            {
                predicted_luminance=first_color_block[num_block];//first pixel always is perfectly predicted! :-)  
            } 
            else if (y == yini) 
            {
                predicted_luminance=image[pix-1];
            } 
            else if (x == xini) 
            {
                predicted_luminance=image[pix-linesize];
                last_small_hop=false;
                hop_1=START_HOP_1;
            } else if (x == xfin_downsampled -1) 
            {
                predicted_luminance=(image[pix-1]+image[pix-linesize])>>1;                                                             
            } 
            else 
            {
                predicted_luminance=(image[pix-1]+image[pix+1-linesize])>>1;     
            }
            
          
            //assignment of component_prediction
            //This is the uncompressed image
            image[pix]= prec -> prec_luminance[predicted_luminance][r_max][hop_1][hop];
            
            //tunning hop1 for the next hop ( "h1 adaptation")
            //------------------------------------------------
            H1_ADAPTATION;

            //lets go for the next pixel
            //--------------------------
            pix++;
            pix_hops_data++;
        }// for x
        pix+=dif_pix;
        pix_hops_data+=dif_hops;
    }// for y
    
}


static void lhe_basic_decode_one_hop_per_pixel_block (LheBasicPrec *prec, uint8_t *hops, uint8_t *image,
                                                      uint32_t width, uint32_t height, int linesize,
                                                      uint8_t *first_color_block, uint32_t total_blocks_width,
                                                      int block_x, int block_y,
                                                      uint32_t block_width, uint32_t block_height) 
{
       
    //Hops computation.
    int xini, xfin, yini, yfin;
    bool small_hop, last_small_hop;
    uint8_t hop, predicted_luminance, hop_1, r_max; 
    int pix, dif_pix, dif_hops, num_block;
    
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
  
            if (x == xini && y==yini) 
            {
                predicted_luminance=first_color_block[num_block];//first pixel always is perfectly predicted! :-)  
            } 
            else if (y == yini) 
            {
                predicted_luminance=image[pix-1];
            } 
            else if (x == xini) 
            {
                predicted_luminance=image[pix-linesize];
                last_small_hop=false;
                hop_1=START_HOP_1;
            } else if (x == xfin -1) 
            {
                predicted_luminance=(image[pix-1]+image[pix-linesize])>>1;                                                             
            } 
            else 
            {
                predicted_luminance=(image[pix-1]+image[pix+1-linesize])>>1;     
            }
            
          
            //assignment of component_prediction
            //This is the uncompressed image
            image[pix]= prec -> prec_luminance[predicted_luminance][r_max][hop_1][hop];
            
            //tunning hop1 for the next hop ( "h1 adaptation")
            //------------------------------------------------
            H1_ADAPTATION;

            //lets go for the next pixel
            //--------------------------
            pix++;
        }// for x
        pix+=dif_pix;
        hops+=dif_hops;
    }// for y
    
}

static void lhe_basic_decode_one_hop_per_pixel (LheBasicPrec *prec, uint8_t *hops, uint8_t *image,
                                                uint8_t first_color, uint32_t width, uint32_t height, 
                                                int linesize) {
       
    //Hops computation.
    bool small_hop, last_small_hop;
    uint8_t hop, predicted_luminance, hop_1, r_max; 
    int pix, dif_pix;
    
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
            
            if (x==0 && y==0)
            {
                predicted_luminance=first_color;//first pixel always is perfectly predicted! :-)  
            }
            else if (y == 0)
            {
                predicted_luminance=image[pix-1];            
            }
            else if (x == 0)
            {
                predicted_luminance=image[pix-linesize];
                last_small_hop=false;
                hop_1=START_HOP_1;
            } 
            else if (x == width -1)
            {
                predicted_luminance=(image[pix-1]+image[pix-linesize])>>1;                                                       
            }
            else 
            {
                predicted_luminance=(image[pix-1]+image[pix+1-linesize])>>1;     
            }
    
            //assignment of component_prediction
            //This is the uncompressed image
            image[pix]= prec -> prec_luminance[predicted_luminance][r_max][hop_1][hop];
            
            //tunning hop1 for the next hop ( "h1 adaptation")
            //------------------------------------------------
            H1_ADAPTATION;

            //lets go for the next pixel
            //--------------------------
            pix++;
        }// for x
        pix+=dif_pix;
    }// for y
    
}

static void lhe_basic_decode_frame_sequential (LheBasicPrec *prec, 
                                               uint8_t *component_Y, uint8_t *component_U, uint8_t *component_V,
                                               uint8_t *hops_Y, uint8_t *hops_U, uint8_t *hops_V,
                                               int width_Y, int height_Y, int width_UV, int height_UV, 
                                               int linesize_Y, int linesize_U, int linesize_V, 
                                               uint8_t *first_color_block_Y, uint8_t *first_color_block_U, uint8_t *first_color_block_V) 
{
    //Luminance
    lhe_basic_decode_one_hop_per_pixel(prec, hops_Y, component_Y, first_color_block_Y[0], width_Y, height_Y, linesize_Y);

    //Chrominance U
    lhe_basic_decode_one_hop_per_pixel(prec, hops_U, component_U, first_color_block_U[0], width_UV, height_UV, linesize_U);

    //Chrominance V
    lhe_basic_decode_one_hop_per_pixel(prec, hops_V, component_V, first_color_block_V[0], width_UV, height_UV, linesize_V);
}


static void lhe_basic_decode_frame_pararell (LheBasicPrec *prec, 
                                             uint8_t *component_Y, uint8_t *component_U, uint8_t *component_V,
                                             uint8_t *hops_Y, uint8_t *hops_U, uint8_t *hops_V,
                                             uint32_t width_Y, uint32_t height_Y, uint32_t width_UV, uint32_t height_UV, 
                                             int linesize_Y, int linesize_U, int linesize_V, 
                                             uint8_t *first_color_block_Y, uint8_t *first_color_block_U, uint8_t *first_color_block_V,
                                             uint32_t total_blocks_width, uint32_t total_blocks_height,
                                             uint32_t block_width_Y, uint32_t block_height_Y, uint32_t block_width_UV, uint32_t block_height_UV) 
{
    
    #pragma omp parallel for
    for (int j=0; j<total_blocks_height; j++)      
    {  
        for (int i=0; i<total_blocks_width; i++) 
        {
            
            //Luminance
            lhe_basic_decode_one_hop_per_pixel_block(prec, hops_Y, component_Y, 
                                                     width_Y, height_Y, linesize_Y, 
                                                     first_color_block_Y, total_blocks_width, 
                                                     i, j, block_width_Y, block_height_Y);

            //Chrominance U
            lhe_basic_decode_one_hop_per_pixel_block(prec, hops_U, component_U, 
                                                     width_UV, height_UV, linesize_U,
                                                     first_color_block_U, total_blocks_width, 
                                                     i, j, block_width_UV, block_height_UV);
        
            //Chrominance V
            lhe_basic_decode_one_hop_per_pixel_block(prec, hops_V, component_V, 
                                                     width_UV, height_UV, linesize_V,
                                                     first_color_block_V, total_blocks_width, 
                                                     i, j, block_width_UV, block_height_UV);
        }
    }
}

static int lhe_decode_frame(AVCodecContext *avctx, void *data, int *got_frame, AVPacket *avpkt)
{
    uint8_t lhe_mode;
    uint8_t *component_Y, *component_U, *component_V, *hops_Y, *hops_U, *hops_V;
    uint8_t *first_color_block_Y, *first_color_block_U, *first_color_block_V;
    uint32_t  block_width_Y, block_width_UV, block_height_Y, block_height_UV, total_blocks;
    int total_blocks_width, total_blocks_height ;
    uint32_t width_Y, width_UV, height_Y, height_UV, image_size_Y, image_size_UV;
    int ret;
    
    float **perceptual_relevance_x, **perceptual_relevance_y;
    float ***ppp_x, ***ppp_y;
    float ppp_max_theoric, compression_factor;
    uint32_t **downsampled_side_x_array, **downsampled_side_y_array;
    uint32_t downsampled_side_x_Y, downsampled_side_x_UV, downsampled_side_y_Y, downsampled_side_y_UV;

    AdvancedLheBlock **block_array_Y;
    AdvancedLheBlock **block_array_UV;
    
    LheHuffEntry he_Y[LHE_MAX_HUFF_SIZE];
    LheHuffEntry he_UV[LHE_MAX_HUFF_SIZE];
   
    LheState *s = avctx->priv_data;
    
    const uint8_t *lhe_data = avpkt->data;
    
    //LHE mode
    lhe_mode = bytestream_get_byte(&lhe_data); 
    
    width_Y  = bytestream2_get_le32u(&lhe_data);
    height_Y = bytestream2_get_le32u(&lhe_data);
    
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
    
    for (int i=0; i<total_blocks; i++) 
    {
        first_color_block_Y[i] = bytestream_get_byte(&lhe_data); 
    }

    
    for (int i=0; i<total_blocks; i++) 
    {
        first_color_block_U[i] = bytestream_get_byte(&lhe_data); 
    }
    
        
    for (int i=0; i<total_blocks; i++) 
    {
        first_color_block_V[i] = bytestream_get_byte(&lhe_data); 
    }

    //Pointers to different color components
    component_Y = s->frame->data[0];
    component_U = s->frame->data[1];
    component_V = s->frame->data[2];
      
    hops_Y = malloc(sizeof(uint8_t) * image_size_Y);      
    hops_U = malloc(sizeof(uint8_t) * image_size_UV);    
    hops_V = malloc(sizeof(uint8_t) * image_size_UV);  
           
    init_get_bits(&s->gb, lhe_data, avpkt->size * 8);

    lhe_read_huffman_table(s, he_Y);
    lhe_read_huffman_table(s, he_UV);
    
    if (lhe_mode == ADVANCED_LHE)
    {      
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
        
        ppp_x = malloc(sizeof(float**) * (total_blocks_height+1));  
    
        for (int i=0; i<total_blocks_height+1; i++) 
        {
            ppp_x[i] = malloc(sizeof(float*) * (total_blocks_width+1));
            
            for (int j=0; j<total_blocks_width+1; j++) 
            {
                ppp_x[i][j] = malloc(sizeof(float) * CORNERS);
            }
        }
        
        ppp_y = malloc(sizeof(float**) * (total_blocks_height+1));  
        
        for (int i=0; i<total_blocks_height+1; i++) 
        {
            ppp_y[i] = malloc(sizeof(float*) * (total_blocks_width+1));
            
            for (int j=0; j<total_blocks_width+1; j++) 
            {
                ppp_y[i][j] = malloc(sizeof(float) * CORNERS);
            }
        }
        
        downsampled_side_x_array = malloc (sizeof(float*) * (total_blocks_height+1));
        
        for (int i=0; i<total_blocks_height+1; i++) 
        {
            downsampled_side_x_array[i] = malloc(sizeof(float) * (total_blocks_width+1));
        }
        
        downsampled_side_y_array = malloc(sizeof(float*) * (total_blocks_height+1));
        
        for (int i=0; i<total_blocks_height+1; i++) 
        {
            downsampled_side_y_array [i] = malloc(sizeof(float) * (total_blocks_width+1));
        }   
        
        block_array_Y = malloc(sizeof(AdvancedLheBlock *) * total_blocks_height);
        
        for (int i=0; i < total_blocks_height; i++)
        {
            block_array_Y[i] = malloc (sizeof(AdvancedLheBlock) * (total_blocks_width));
        }
        
        block_array_UV = malloc(sizeof(AdvancedLheBlock *) * total_blocks_height);
        
        for (int i=0; i < total_blocks_height; i++)
        {
            block_array_UV[i] = malloc (sizeof(AdvancedLheBlock) * (total_blocks_width));
        }
        
        block_width_Y = (width_Y-1)/total_blocks_width + 1;    
        block_height_Y = (height_Y-1)/total_blocks_height + 1;   
        
        block_width_UV = (block_width_Y - 1)/CHROMA_FACTOR_WIDTH + 1;
        block_height_UV = (block_height_Y - 1)/CHROMA_FACTOR_HEIGHT + 1;    
        
        ppp_max_theoric = block_width_Y/SIDE_MIN;
        compression_factor = COMPRESSION_FACTOR;
       
        lhe_advanced_read_mesh(s, block_array_Y, block_array_UV,
                               perceptual_relevance_x, perceptual_relevance_y,
                               ppp_x, ppp_y,
                               downsampled_side_x_array, downsampled_side_y_array,
                               ppp_max_theoric, compression_factor,
                               width_Y, height_Y, width_UV, height_UV,
                               block_width_Y, block_height_Y, block_width_UV, block_height_UV,
                               total_blocks_width, total_blocks_height) ; 
                              
        for (int block_y=0; block_y<total_blocks_height; block_y++)
        {
            for (int block_x=0; block_x<total_blocks_width; block_x++)
            {          
 
                lhe_advanced_read_file_symbols (s, he_Y, block_array_Y,
                                                hops_Y,
                                                width_Y, height_Y,
                                                block_x, block_y, true);  
                                                
                lhe_advanced_read_file_symbols (s, he_UV, block_array_UV,
                                                hops_U,
                                                width_UV, height_UV,
                                                block_x, block_y, true);
                
                lhe_advanced_read_file_symbols (s, he_UV, block_array_UV, 
                                                hops_V, 
                                                width_UV, height_UV,
                                                block_x, block_y, true);

            }
        }
 
        for (int block_y=0; block_y<total_blocks_height; block_y++)
        {
            for (int block_x=0; block_x<total_blocks_width; block_x++)
            {
             
                //Luminance
                lhe_advanced_decode_one_hop_per_pixel_block(&s->prec, block_array_Y,
                                                            hops_Y, component_Y, 
                                                            width_Y, height_Y, s->frame->linesize[0], 
                                                            first_color_block_Y, total_blocks_width, 
                                                            block_x, block_y, block_width_Y, block_height_Y);               

                //Chrominance U
                lhe_advanced_decode_one_hop_per_pixel_block(&s->prec, block_array_UV,
                                                            hops_U, component_U, 
                                                            width_UV, height_UV, s->frame->linesize[1],
                                                            first_color_block_U, total_blocks_width, 
                                                            block_x, block_y, block_width_UV, block_height_UV);
           
                //Chrominance V
                lhe_advanced_decode_one_hop_per_pixel_block(&s->prec, block_array_UV,
                                                            hops_V, component_V, 
                                                            width_UV, height_UV, s->frame->linesize[2],
                                                            first_color_block_V, total_blocks_width, 
                                                            block_x, block_y, block_width_UV, block_height_UV);
                                                            
                                                    
            }
        } 
    }
    else 
    {
        lhe_basic_read_file_symbols(s, he_Y, image_size_Y, hops_Y);
        lhe_basic_read_file_symbols(s, he_UV, image_size_UV, hops_U);
        lhe_basic_read_file_symbols(s, he_UV, image_size_UV, hops_V);
 
        if (total_blocks > 1 && OPENMP_FLAGS == CONFIG_OPENMP) 
        {
            block_width_Y = (width_Y-1)/total_blocks_width + 1;
            block_height_Y = (height_Y-1)/total_blocks_height + 1;
            block_width_UV = (width_UV-1)/total_blocks_width + 1;
            block_height_UV = (height_UV-1)/total_blocks_height +1;

            lhe_basic_decode_frame_pararell (&s->prec, 
                                            component_Y, component_U, component_V, 
                                            hops_Y, hops_U, hops_V,
                                            width_Y, height_Y, width_UV, height_UV, 
                                            s->frame->linesize[0], s->frame->linesize[1], s->frame->linesize[2],
                                            first_color_block_Y, first_color_block_U, first_color_block_V,
                                            total_blocks_width, total_blocks_height,
                                            block_width_Y, block_height_Y, block_width_UV, block_height_UV);                                
        
        } else 
        {      
            lhe_basic_decode_frame_sequential (&s->prec, 
                                            component_Y, component_U, component_V, 
                                            hops_Y, hops_U, hops_V,
                                            width_Y, height_Y, width_UV, height_UV, 
                                            s->frame->linesize[0], s->frame->linesize[1], s->frame->linesize[2],
                                            first_color_block_Y, first_color_block_U, first_color_block_V);    
        }
    }
    /*
    av_log(NULL, AV_LOG_INFO, "COMPONENT Y Y \n");

        for (int i=0; i<height_Y; i++) {
            for (int j=0; j<width_Y; j++) {
                av_log(NULL, AV_LOG_INFO, "%d;", component_Y[i*width_Y + j]);
            }
        av_log(NULL, AV_LOG_INFO, "\n");
    }
    
    av_log(NULL, AV_LOG_INFO, "DOWNSAMPLED \n");

        for (int i=0; i<total_blocks_height; i++) {
            for (int j=0; j<total_blocks_width; j++) {
                av_log(NULL, AV_LOG_INFO, "%d;", block_array_Y[i][j].downsampled_x_side);
            }
        av_log(NULL, AV_LOG_INFO, "\n");
    }
    
        av_log(NULL, AV_LOG_INFO, "HOPS Y \n");

        for (int i=0; i<height_Y; i++) {
            for (int j=0; j<width_Y; j++) {
                av_log(NULL, AV_LOG_INFO, "%d;", hops_Y[i*width_Y + j]);
            }
        av_log(NULL, AV_LOG_INFO, "\n");
    }
        
    av_log(NULL, AV_LOG_INFO, "HOPS U \n");

        for (int i=0; i<height_UV; i++) {
            for (int j=0; j<width_UV; j++) {
                av_log(NULL, AV_LOG_INFO, "%d;", hops_U[i*width_UV + j]);
            }
        av_log(NULL, AV_LOG_INFO, "\n");
    }
    
    av_log(NULL, AV_LOG_INFO, "HOPS V \n");

        for (int i=0; i<height_UV; i++) {
            for (int j=0; j<width_UV; j++) {
                av_log(NULL, AV_LOG_INFO, "%d;", hops_V[i*width_UV + j]);
            }
        av_log(NULL, AV_LOG_INFO, "\n");
    }
    */
  
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
