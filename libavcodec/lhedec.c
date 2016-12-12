/*
 * LHE Basic decoder
 */

#include "bytestream.h"
#include "get_bits.h"
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
    LheProcessing procY;
    LheProcessing procUV;
    LheImage lheY;
    LheImage lheU;
    LheImage lheV;
    uint8_t chroma_factor_width;
    uint8_t chroma_factor_height;
    uint8_t lhe_mode;
    uint8_t pixel_format;
    uint8_t quality_level;
    uint32_t total_blocks_width;
    uint32_t total_blocks_height;
    int dif_frames_count;
} LheState;


static av_cold int lhe_decode_init(AVCodecContext *avctx)
{
    LheState *s = avctx->priv_data;

    s->frame = av_frame_alloc();
    if (!s->frame)
        return AVERROR(ENOMEM);
    
    lhe_init_cache(&s->prec);
    
    return 0;
}

//==================================================================
// HUFFMAN FUNCTIONS
//==================================================================
/**
 * Reads Huffman table
 * 
 * @param max_huff_size Maximum number of symbols in Huffman tree
 * @param max_huff_node_bits Number of bits for each entry in the file
 * @param huff_no_occurrences Code for no occurrences
 */
static void lhe_read_huffman_table (LheState *s, LheHuffEntry *he, 
                                    int max_huff_size, int max_huff_node_bits, 
                                    int huff_no_occurrences) 
{   
    int i;
    uint8_t len;

    
    for (i=0; i< max_huff_size; i++) 
    {
        len = get_bits(&s->gb, max_huff_node_bits); 

        if (len==huff_no_occurrences) len=255; //If symbol does not have any occurence, encoder assigns 255 length. This is 15 or 7 in file
        he[i].len = len;
        he[i].sym = i; 
        he[i].code = 1024;
    }    
    
    lhe_generate_huffman_codes(he, max_huff_size);
       
}

/**
 * Translates Huffman into symbols (hops)
 * 
 * @huffman_symbol huffman symbol
 * @he Huffman entry, Huffman parameters
 * @count_bits Number of bits of huffman symbol 
 */
static uint8_t lhe_translate_huffman_into_symbol (uint32_t huffman_symbol, LheHuffEntry *he, uint8_t count_bits) 
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

/**
 * Translates Huffman symbol into PR interval
 * 
 * @param huffman_symbol huffman symbol extracted from file
 * @param *he Huffman entry, Huffman params 
 * @param count_bits Number of bits of Huffman symbol
 */
static uint8_t lhe_translate_huffman_into_interval (uint32_t huffman_symbol, LheHuffEntry *he, uint8_t count_bits) 
{
    uint8_t interval;
        
    interval = NO_SYMBOL;
    
    if (huffman_symbol == he[PR_INTERVAL_0].code && he[PR_INTERVAL_0].len == count_bits)
    {
        interval = PR_INTERVAL_0;
    } 
    else if (huffman_symbol == he[PR_INTERVAL_1].code && he[PR_INTERVAL_1].len == count_bits)
    {
        interval = PR_INTERVAL_1;
    } 
    else if (huffman_symbol == he[PR_INTERVAL_2].code && he[PR_INTERVAL_2].len == count_bits)
    {
        interval = PR_INTERVAL_2;
    } 
    else if (huffman_symbol == he[PR_INTERVAL_3].code && he[PR_INTERVAL_3].len == count_bits)
    {
        interval = PR_INTERVAL_3;
    } 
    else if (huffman_symbol == he[PR_INTERVAL_4].code && he[PR_INTERVAL_4].len == count_bits)
    {
        interval = PR_INTERVAL_4;
    }
    
    
    return interval;
    
}

//==================================================================
// BASIC LHE FILE
//==================================================================
/**
 * Reads file symbols of basic lhe file
 * 
 * @s Lhe parameters
 * @he Huffman entry, Huffman parameters
 * @image_size image size
 */
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
        
        symbol = lhe_translate_huffman_into_symbol(huffman_symbol, he, count_bits);        
        
        if (symbol != NO_SYMBOL) 
        {
            symbols[decoded_symbols] = symbol;
            decoded_symbols++;
            huffman_symbol = 0;
            count_bits = 0;
        }       
    }
}

//==================================================================
// ADVANCED LHE FILE
//==================================================================
/**
 * Reads file symbols from advanced lhe file
 * 
 * @param s Lhe parameters
 * @param he Huffman entry, Huffman parameters
 * @param **basic_block Basic block parameters 
 * @param **advanced_block Advanced block parameters
 * @param symbols symbols array (hops)
 * @param width image width
 * @param height image height
 * @param block_x block x index
 * @param block_y block y index
 */
static void lhe_advanced_read_file_symbols (LheState *s, LheHuffEntry *he, 
                                            BasicLheBlock **basic_block, AdvancedLheBlock **advanced_block,
                                            uint8_t *symbols, 
                                            uint32_t width, uint32_t height, 
                                            int block_x, int block_y) 
{
    uint8_t symbol, count_bits;
    uint32_t huffman_symbol, pix;
    uint32_t xini, xfin_downsampled, yini, yfin_downsampled;
    
    xini = basic_block[block_y][block_x].x_ini;
    xfin_downsampled = advanced_block[block_y][block_x].x_fin_downsampled; 
 
    yini = basic_block[block_y][block_x].y_ini;
    yfin_downsampled = advanced_block[block_y][block_x].y_fin_downsampled;
    
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
            
            symbol = lhe_translate_huffman_into_symbol(huffman_symbol, he, count_bits);        
            
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

/**
 * Reads file symbols from advanced lhe file
 * 
 * @param *s Lhe parameters
 * @param *he_Y Luminance Huffman entry, Huffman parameters
 * @param *he_UV Chrominance Huffman entry, Huffman parameters
 * @param **basic_block_Y Basic block parameters for luminance signal
 * @param **basic_block_UV Basic block parameters for chrominance signals
 * @param **advanced_block_Y Advanced block parameters for luminance signal
 * @param **advanced_block_UV Advanced block parameters for chrominance signals
 * @param symbols_Y luminance symbols array (hops)
 * @param symbols_U chrominance u symbols array (hops)
 * @param symbols_V chrominance v symbols array (hops)
 * @param width_Y luminance image width
 * @param height_Y luminance image height
 * @param width_UV chrominance image width
 * @param height_UV chrominance image height
 * @param total_blocks_width number of blocks widthwise
 * @param total_blocks_height number of blocks heightwise
 */
static void lhe_advanced_read_all_file_symbols (LheState *s, 
                                                LheHuffEntry *he_Y, LheHuffEntry *he_UV,
                                                BasicLheBlock **basic_block_Y, BasicLheBlock **basic_block_UV,
                                                AdvancedLheBlock **advanced_block_Y, AdvancedLheBlock **advanced_block_UV,
                                                uint8_t *symbols_Y, uint8_t *symbols_U, uint8_t *symbols_V,
                                                uint32_t width_Y, uint32_t height_Y, uint32_t width_UV, uint32_t height_UV, 
                                                uint32_t total_blocks_width, uint32_t total_blocks_height) 
{
    
    for (int block_y=0; block_y<total_blocks_height; block_y++)
    {
        for (int block_x=0; block_x<total_blocks_width; block_x++)
        {          

            lhe_advanced_read_file_symbols (s, he_Y, 
                                            basic_block_Y, advanced_block_Y,
                                            symbols_Y,
                                            width_Y, height_Y,
                                            block_x, block_y);  
                                            
            lhe_advanced_read_file_symbols (s, he_UV, 
                                            basic_block_UV, advanced_block_UV,
                                            symbols_U,
                                            width_UV, height_UV,
                                            block_x, block_y);
            
            lhe_advanced_read_file_symbols (s, he_UV, 
                                            basic_block_UV, advanced_block_UV,
                                            symbols_V, 
                                            width_UV, height_UV,
                                            block_x, block_y);

        }
    }
}

/**
 * Translates perceptual relevance intervals to perceptual relevance quants
 * 
 *    Interval   -  Quant - Interval number
 * [0.0, 0.125)  -  0.0   -         0
 * [0.125, 0.25) -  0.125 -         1
 * [0.25, 0.5)   -  0.25  -         2
 * [0.5, 0.75)   -  0.5   -         3
 * [0.75, 1]     -  1.0   -         4
 * 
 * @param perceptual_relevance_interval perceptual relevance interval number
 */
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
            perceptual_relevance_quant = PR_QUANT_5;
            break;     
    }
    
    return perceptual_relevance_quant;
}

/**
 * Reads Perceptual Relevance interval values from file
 * 
 * @param *s Lhe params
 * @param *he_mesh Huffman params for LHE mesh
 * @param **perceptual_relevance Perceptual Relevance values
 * @param total_blocks_width number of blocks widthwise
 * @param total_blocks_height number of blocks heightwise
 */
static void lhe_advanced_read_perceptual_relevance_interval (LheState *s, LheHuffEntry *he_mesh, 
                                                             float ** perceptual_relevance, 
                                                             uint32_t total_blocks_width, uint32_t total_blocks_height) 
{
    uint8_t perceptual_relevance_interval, count_bits;
    uint32_t huffman_symbol;
    
    perceptual_relevance_interval = NO_INTERVAL;
    count_bits = 0;
    huffman_symbol = 0;
    
    for (int block_y=0; block_y<total_blocks_height+1; block_y++) 
    {
        for (int block_x=0; block_x<total_blocks_width+1;) 
        { 
            //Reads from file
            huffman_symbol = (huffman_symbol<<1) | get_bits(&s->gb, 1);
            count_bits++;
            
            perceptual_relevance_interval = lhe_translate_huffman_into_interval(huffman_symbol, he_mesh, count_bits);        
            
            if (perceptual_relevance_interval != NO_INTERVAL) 
            {
                perceptual_relevance[block_y][block_x] = lhe_advance_translate_pr_interval_to_pr_quant(perceptual_relevance_interval);;
                block_x++;
                huffman_symbol = 0;
                count_bits = 0;
            }                  
        }
    }
}

/**
 * Reads perceptual intervals and translates them to perceptual relevance quants.
 * Calculates block coordinates according to perceptual relevance values.
 * Calculates pixels per pixel according to perceptual relevance values.
 * 
 * @param *s Lhe params
 * @param *he_mesh Huffman params for LHE mesh
 * @param **basic_block_Y Basic block parameters for luminance signal
 * @param **basic_block_UV Basic block parameters for chrominance signals
 * @param **advanced_block_Y Advanced block parameters for luminance signal
 * @param **advanced_block_UV Advanced block parameters for chrominance signals
 * @param **perceptual_relevance_x Perceptual relevance array for x coordinate
 * @param **perceptual_relevance_y Perceptual relevance array for y coordinate
 * @param ***ppp_x Pixels per pixel array for x coordinate
 * @param ***ppp_y Pixels per pixel array for y coordinate
 * @param ppp_max_theoric Maximum number of pixels per pixel
 * @param compression_factor Compression factor number
 * @param width_Y luminance width
 * @param height_Y luminance height
 * @param width_UV chrominance width
 * @param height_UV chrominance height
 * @param block_width_Y luminance block width
 * @param block_height_Y luminance block height
 * @param block_width_UV chrominance block width
 * @param block_height_UV chrominance block height
 * @param total_blocks_width Number of blocks widthwise
 * @param total_blocks_height Number of blocks heightwise
 */
static void lhe_advanced_read_mesh (LheState *s, LheHuffEntry *he_mesh,
                                    BasicLheBlock **basic_block_Y, BasicLheBlock **basic_block_UV,
                                    AdvancedLheBlock **advanced_block_Y, AdvancedLheBlock **advanced_block_UV,
                                    float ** perceptual_relevance_x, float ** perceptual_relevance_y,
                                    float ppp_max_theoric, float compression_factor,
                                    uint32_t width_Y, uint32_t height_Y, uint32_t width_UV, uint32_t height_UV,
                                    uint32_t block_width_Y, uint32_t block_height_Y, uint32_t block_width_UV, uint32_t block_height_UV,
                                    uint32_t total_blocks_width, uint32_t total_blocks_height) 
{
    
    
    lhe_advanced_read_perceptual_relevance_interval (s, he_mesh, 
                                                     perceptual_relevance_x, 
                                                     total_blocks_width, total_blocks_height);
    
    lhe_advanced_read_perceptual_relevance_interval (s, he_mesh, 
                                                     perceptual_relevance_y, 
                                                     total_blocks_width, total_blocks_height);
    
    
    for (int block_y=0; block_y<total_blocks_height; block_y++)
    {
        for (int block_x=0; block_x<total_blocks_width; block_x++)
        {
            lhe_calculate_block_coordinates (&s->procY, &s->procUV, 
                                             total_blocks_width, total_blocks_height,
                                             block_x, block_y);

            lhe_advanced_perceptual_relevance_to_ppp(&s->procY, &s->procUV, compression_factor, ppp_max_theoric, block_x, block_y);
            
            //Adjusts luminance ppp to rectangle shape 
            lhe_advanced_ppp_side_to_rectangle_shape (&s->procY, ppp_max_theoric, block_x, block_y);  
            //Adjusts chrominance ppp to rectangle shape
            lhe_advanced_ppp_side_to_rectangle_shape (&s->procUV, ppp_max_theoric, block_x, block_y);   
        }
    }
}


//==================================================================
// BASIC LHE DECODING
//==================================================================
/**
 * Decodes one hop per pixel in a block
 * 
 * @param *prec Pointer to Lhe precalculated data
 * @param **basic_block Basic block parameters
 * @param *hops array of hops 
 * @param *image image result
 * @param width image width
 * @param height image height
 * @param linesize rectangle images create a square image in ffmpeg memory. Linesize is width used by ffmpeg in memory
 * @param *first_color_block first component value for each block
 * @param total_blocks_width number of blocks widthwise
 * @param block_x block x index
 * @param block_y block y index
 * @param block_width block width
 * @param block_height block height
 */
static void lhe_basic_decode_one_hop_per_pixel_block (LheBasicPrec *prec,
                                                      BasicLheBlock **basic_block,
                                                      uint8_t *hops, uint8_t *image,
                                                      uint32_t width, uint32_t height, int linesize,
                                                      uint8_t *first_color_block, 
                                                      uint32_t total_blocks_width, uint32_t total_blocks_height,
                                                      int block_x, int block_y,
                                                      uint32_t block_width, uint32_t block_height) 
{
       
    //Hops computation.
    int xini, xfin, yini, yfin;
    bool small_hop, last_small_hop;
    uint8_t hop, predicted_luminance, hop_1, r_max; 
    int pix, dif_pix, dif_hops, num_block;
    
    num_block = block_y * total_blocks_width + block_x;
    
    //ORIGINAL IMAGE
    xini = basic_block[block_y][block_x].x_ini;
    xfin = basic_block[block_y][block_x].x_fin;  
    yini = basic_block[block_y][block_x].y_ini;
    yfin = basic_block[block_y][block_x].y_fin;
        
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

/**
 * Decodes one hop per pixel
 * 
 * @param *prec precalculated lhe data
 * @param *hops hops array
 * @param *image final result
 * @param first_color First pixel component
 * @param width image width
 * @param height image height
 * @param linesize rectangle images create a square image in ffmpeg memory. Linesize is width used by ffmpeg in memory
 */
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

/**
 * Calls methods to decode sequentially
 */
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

/**
 * Calls methods to decode pararell
 */
static void lhe_basic_decode_frame_pararell (LheState *s, LheBasicPrec *prec, 
                                             BasicLheBlock **basic_block_Y, BasicLheBlock **basic_block_UV,
                                             uint8_t *component_Y, uint8_t *component_U, uint8_t *component_V,
                                             uint8_t *hops_Y, uint8_t *hops_U, uint8_t *hops_V,
                                             uint32_t width_Y, uint32_t height_Y, uint32_t width_UV, uint32_t height_UV, 
                                             int linesize_Y, int linesize_U, int linesize_V, 
                                             uint8_t *first_color_block_Y, uint8_t *first_color_block_U, uint8_t *first_color_block_V,
                                             uint32_t total_blocks_width, uint32_t total_blocks_height,
                                             uint32_t block_width_Y, uint32_t block_height_Y, uint32_t block_width_UV, uint32_t block_height_UV) 
{
    
    #pragma omp parallel for
    for (int block_y=0; block_y<total_blocks_height; block_y++)      
    {  
        for (int block_x=0; block_x<total_blocks_width; block_x++) 
        {
            lhe_calculate_block_coordinates (&s->procY, &s->procUV,
                                             total_blocks_width, total_blocks_height,
                                             block_x, block_y);
            
            //Luminance
            lhe_basic_decode_one_hop_per_pixel_block(prec, basic_block_Y, 
                                                     hops_Y, component_Y, 
                                                     width_Y, height_Y, linesize_Y, 
                                                     first_color_block_Y, 
                                                     total_blocks_width, total_blocks_height,
                                                     block_x, block_y, block_width_Y, block_height_Y);

            //Chrominance U
            lhe_basic_decode_one_hop_per_pixel_block(prec, basic_block_UV,
                                                     hops_U, component_U, 
                                                     width_UV, height_UV, linesize_U,
                                                     first_color_block_U, 
                                                     total_blocks_width, total_blocks_height,
                                                     block_x, block_y, block_width_UV, block_height_UV);
        
            //Chrominance V
            lhe_basic_decode_one_hop_per_pixel_block(prec, basic_block_UV,
                                                     hops_V, component_V, 
                                                     width_UV, height_UV, linesize_V,
                                                     first_color_block_V, 
                                                     total_blocks_width, total_blocks_height,
                                                     block_x, block_y, block_width_UV, block_height_UV);
        }
    }
}

//==================================================================
// ADVANCED LHE DECODING
//==================================================================
/**
 * Decodes one hop per pixel in a block
 * 
 * @param *prec Pointer to precalculated lhe data
 * @param **basic_block Basic block parameters
 * @param **advanced_block Advanced block parameters
 * @param *hops array of hops
 * @param *image final image
 * @param width image width
 * @param height image height
 * @param *first_color_block first component value for each block
 * @param total_blocks_width number of blocks widthwise
 * @param block_x block x index
 * @param block_y block y index
 * @param block_width block width
 * @param block_height block height
 */
static void lhe_advanced_decode_one_hop_per_pixel_block (LheBasicPrec *prec, 
                                                         BasicLheBlock **basic_block, AdvancedLheBlock **advanced_block,
                                                         uint8_t *hops, uint8_t *image,
                                                         uint32_t width, uint32_t height,
                                                         uint8_t *first_color_block, uint32_t total_blocks_width,
                                                         uint32_t block_x, uint32_t block_y,
                                                         uint32_t block_width, uint32_t block_height) 
{
       
    //Hops computation.
    uint32_t xini, xfin_downsampled, yini, yfin_downsampled;
    bool small_hop, last_small_hop;
    uint8_t hop, predicted_luminance, hop_1, r_max; 
    uint32_t pix, dif_pix, num_block;
    
    num_block = block_y * total_blocks_width + block_x;
    
    xini = basic_block[block_y][block_x].x_ini;
    xfin_downsampled = advanced_block[block_y][block_x].x_fin_downsampled; 
 
    yini = basic_block[block_y][block_x].y_ini;
    yfin_downsampled = advanced_block[block_y][block_x].y_fin_downsampled;
    
    small_hop           = false;
    last_small_hop      = false;        // indicates if last hop is small
    predicted_luminance = 0;            // predicted signal
    hop_1               = START_HOP_1;
    pix                 = 0;            // pixel possition, from 0 to image size        
    r_max               = PARAM_R;        
    
 
    pix = yini*width + xini; 
    dif_pix = width - xfin_downsampled + xini;
    
    for (int y=yini; y < yfin_downsampled; y++)  {
        for (int x=xini; x < xfin_downsampled; x++)     {
            
            hop = hops[pix]; 
  
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
                predicted_luminance=image[pix-width];
                last_small_hop=false;
                hop_1=START_HOP_1;
            } else if (x == xfin_downsampled -1) 
            {
                predicted_luminance=(image[pix-1]+image[pix-width])>>1;                                                             
            } 
            else 
            {
                predicted_luminance=(image[pix-1]+image[pix+1-width])>>1;     
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

/**
 * Vertical Nearest neighbour interpolation 
 */
static void lhe_advanced_vertical_nearest_neighbour_interpolation (BasicLheBlock **basic_block, AdvancedLheBlock **advanced_block,
                                                                   uint8_t *downsampled_image, uint8_t *intermediate_interpolated_image,
                                                                   uint32_t width, uint32_t block_width, uint32_t block_height,
                                                                   int block_x, int block_y) 
{
    uint32_t downsampled_y_side;
    float gradient, gradient_0, gradient_1, ppp_y, ppp_0, ppp_1, ppp_2, ppp_3;
    uint32_t xini, xfin_downsampled, yini, yprev_interpolated, yfin_interpolated, yfin_downsampled;
    float yfin_interpolated_float;
    
    downsampled_y_side = advanced_block[block_y][block_x].downsampled_y_side;
    xini = basic_block[block_y][block_x].x_ini;
    xfin_downsampled = advanced_block[block_y][block_x].x_fin_downsampled;
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
    
    // pppx initialized to ppp_0
    ppp_y=ppp_0;    
      
    for (int x=xini;x<xfin_downsampled;x++)
    {
            gradient=(ppp_2-ppp_0)/(downsampled_y_side-1.0);  
            
            ppp_y=ppp_0;

            //Interpolated y coordinates
            yprev_interpolated = yini; 
            yfin_interpolated_float= yini+ppp_y;

            // bucle for horizontal scanline 
            // scans the downsampled image, pixel by pixel
            for (int y_sc=yini;y_sc<yfin_downsampled;y_sc++)
            {            
                yfin_interpolated = yfin_interpolated_float + 0.5;  
                
                for (int i=yprev_interpolated;i < yfin_interpolated;i++)
                {
                    intermediate_interpolated_image[i*width+x]=downsampled_image[y_sc*width+x];                  
                }
          
                yprev_interpolated=yfin_interpolated;
                ppp_y+=gradient;
                yfin_interpolated_float+=ppp_y;               
                
            }//y
            ppp_0+=gradient_0;
            ppp_2+=gradient_1;
    }//x
    
}

/**
 * Horizontal Nearest neighbour interpolation 
 */
static void lhe_advanced_horizontal_nearest_neighbour_interpolation (BasicLheBlock **basic_block, AdvancedLheBlock **advanced_block,
                                                                     uint8_t *intermediate_interpolated_image, uint8_t *component_Y,
                                                                     uint32_t width, int linesize,
                                                                     uint32_t block_width, uint32_t block_height,
                                                                     int block_x, int block_y) 
{
    uint32_t downsampled_x_side;
    float gradient, gradient_0, gradient_1, ppp_x, ppp_0, ppp_1, ppp_2, ppp_3;
    uint32_t xini, xfin_downsampled, xprev_interpolated, xfin_interpolated, yini, yfin;
    float xfin_interpolated_float;
    
    downsampled_x_side = advanced_block[block_y][block_x].downsampled_x_side;
    xini = basic_block[block_y][block_x].x_ini;
    xfin_downsampled = advanced_block[block_y][block_x].x_fin_downsampled;
    yini = basic_block[block_y][block_x].y_ini;
    yfin =  basic_block[block_y][block_x].y_fin;

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
        
        //Interpolated x coordinates
        xprev_interpolated = xini; 
        xfin_interpolated_float= xini+ppp_x;

        for (int x_sc=xini; x_sc<xfin_downsampled; x_sc++)
        {
            xfin_interpolated = xfin_interpolated_float + 0.5;            
               
            for (int i=xprev_interpolated;i < xfin_interpolated;i++)
            {
                component_Y[y*linesize+i]=intermediate_interpolated_image[y*width+x_sc];       
            }
                        
            xprev_interpolated=xfin_interpolated;
            ppp_x+=gradient;
            xfin_interpolated_float+=ppp_x;   
        }//x

        ppp_0+=gradient_0;
        ppp_1+=gradient_1;

    }//y 
}

static void lhe_advanced_decode_symbols (LheState *s, 
                                         LheHuffEntry *he_Y, LheHuffEntry *he_UV,
                                         BasicLheBlock **basic_block_Y, BasicLheBlock **basic_block_UV,
                                         AdvancedLheBlock **advanced_block_Y, AdvancedLheBlock **advanced_block_UV,
                                         uint8_t *first_color_block_Y, uint8_t *first_color_block_U, uint8_t *first_color_block_V,
                                         uint8_t *symbols_Y, uint8_t *symbols_U, uint8_t *symbols_V,
                                         uint8_t *downsampled_image_Y, uint8_t *downsampled_image_U, uint8_t *downsampled_image_V,
                                         uint8_t *component_Y, uint8_t *component_U, uint8_t *component_V,
                                         uint32_t width_Y, uint32_t height_Y, uint32_t width_UV, uint32_t height_UV,
                                         uint32_t image_size_Y, uint32_t image_size_UV,
                                         uint32_t block_width_Y, uint32_t block_height_Y, uint32_t block_width_UV, uint32_t block_height_UV,
                                         uint32_t total_blocks_width, uint32_t total_blocks_height) 
{
    uint8_t *intermediate_interpolated_Y, *intermediate_interpolated_U, *intermediate_interpolated_V;
    
    intermediate_interpolated_Y = malloc (sizeof(uint8_t) * image_size_Y);
    intermediate_interpolated_U = malloc (sizeof(uint8_t) * image_size_UV);
    intermediate_interpolated_V = malloc (sizeof(uint8_t) * image_size_UV);
    
    #pragma omp parallel for
    for (int block_y=0; block_y<total_blocks_height; block_y++)
    {
        for (int block_x=0; block_x<total_blocks_width; block_x++)
        {                        
            //Luminance
            lhe_advanced_decode_one_hop_per_pixel_block(&s->prec, 
                                                        basic_block_Y, advanced_block_Y,
                                                        symbols_Y, downsampled_image_Y, 
                                                        width_Y, height_Y,  
                                                        first_color_block_Y, total_blocks_width, 
                                                        block_x, block_y, block_width_Y, block_height_Y); 
            
                
            
            lhe_advanced_vertical_nearest_neighbour_interpolation (basic_block_Y, advanced_block_Y,
                                                                   downsampled_image_Y, intermediate_interpolated_Y,
                                                                   width_Y, block_width_Y, block_height_Y,
                                                                   block_x, block_y);     
            
            
            lhe_advanced_horizontal_nearest_neighbour_interpolation (basic_block_Y, advanced_block_Y, 
                                                                     intermediate_interpolated_Y, component_Y,
                                                                     width_Y, s->frame->linesize[0],
                                                                     block_width_Y, block_height_Y,
                                                                     block_x, block_y);

            //Chrominance U
            lhe_advanced_decode_one_hop_per_pixel_block(&s->prec, 
                                                        basic_block_UV, advanced_block_UV,
                                                        symbols_U, downsampled_image_U, 
                                                        width_UV, height_UV, 
                                                        first_color_block_U, total_blocks_width, 
                                                        block_x, block_y, block_width_UV, block_height_UV);

            lhe_advanced_vertical_nearest_neighbour_interpolation (basic_block_UV, advanced_block_UV,
                                                                   downsampled_image_U, intermediate_interpolated_U,
                                                                   width_UV, block_width_UV, block_height_UV,
                                                                   block_x, block_y);
            
            lhe_advanced_horizontal_nearest_neighbour_interpolation (basic_block_UV, advanced_block_UV,
                                                                     intermediate_interpolated_U, component_U,
                                                                     width_UV, s->frame->linesize[1],
                                                                     block_width_UV, block_height_UV,
                                                                     block_x, block_y);
                                                                        
        
            //Chrominance V          
            lhe_advanced_decode_one_hop_per_pixel_block(&s->prec, 
                                                        basic_block_UV, advanced_block_UV,
                                                        symbols_V, downsampled_image_V, 
                                                        width_UV, height_UV, 
                                                        first_color_block_V, total_blocks_width, 
                                                        block_x, block_y, block_width_UV, block_height_UV);

            
            lhe_advanced_vertical_nearest_neighbour_interpolation (basic_block_UV, advanced_block_UV, 
                                                                   downsampled_image_V, intermediate_interpolated_V,
                                                                   width_UV, block_width_UV, block_height_UV,
                                                                   block_x, block_y);
            
            lhe_advanced_horizontal_nearest_neighbour_interpolation (basic_block_UV, advanced_block_UV, 
                                                                     intermediate_interpolated_V, component_V,
                                                                     width_UV, s->frame->linesize[2],
                                                                     block_width_UV, block_height_UV,
                                                                     block_x, block_y);         
                                                                     
        }
    }
}

//==================================================================
// VIDEO LHE DECODING
//==================================================================

/**
 * Adds differential info (delta) to last frame
 * 
 * @param *s LHE State
 * @param **basic_block Basic block parameters
 * @param **advanced_block Advanced block parameters
 * @param *delta_frame array containing differential info(delta)
 * @param *downsampled_image final downsampled image
 * @param *last_downsampled_image array containing last downsampled data
 * @param width image width
 * @param block_x block x index
 * @param block_y block y index
 */
static void mlhe_add_delta_to_last_frame (LheState *s, 
                                          BasicLheBlock **basic_block, AdvancedLheBlock **advanced_block, 
                                          uint8_t *delta_frame, 
                                          uint8_t *downsampled_image, uint8_t *last_downsampled_image,
                                          uint32_t width, uint32_t block_x, uint32_t block_y) 
{
    uint32_t xini, xfin, yini, yfin, pix;
    int delta, image;
    
    xini = basic_block[block_y][block_x].x_ini;
    xfin = advanced_block[block_y][block_x].x_fin_downsampled; 
 
    yini = basic_block[block_y][block_x].y_ini;
    yfin = advanced_block[block_y][block_x].y_fin_downsampled;
    
    #pragma omp parallel for
    for (int y=yini; y<yfin; y++) 
    {
        for (int x=xini; x<xfin; x++) 
        {
            pix = y*width + x;
            delta = (delta_frame[pix] - 128.0) * 2.0;
            image = last_downsampled_image[pix] + delta;
            
            if (image > 255) image = 255;
            if (image < 0) image = 1;
            
            downsampled_image[pix] = image;
        }
    }
        
    
}

/**
 * Decodes differential frame
 * 
 * @param *s LHE Context
 * @param *he_Y luminance Huffman data
 * @param *he_UV chrominance Huffman data
 * @param *basic_block_Y luminance basic block 
 * @param *basic_block_UV chrominance basic block 
 * @param *advanced_block_Y luminance advanced block for current frame
 * @param *advanced_block_UV chrominance advanced block for current frame
 * @param *last_advanced_block_Y luminance advanced block for last frame
 * @param *last_advanced_block_UV chrominance advanced block for last frame
 * @param *first_color_block_Y luminance first color block 
 * @param *first_color_block_U chrominance U first color block
 * @param *first_color_block_V chrominance V first color block
 * @param *symbols_Y luminance hops array
 * @param *symbols_U chrominance U hops array
 * @param *symbols_V chrominance V hops array
 * @param component_Y luminance original data
 * @param *component_U chrominance u original data
 * @param *component_V chrominance v original data
 * @param width_Y luminance width
 * @param height_Y luminance height
 * @param width_UV chrominance width
 * @param heigth_UV chorminance height
 * @param image_size_Y luminance image size
 * @param image_size_UV chrominance image size
 * @param block_width_Y luminance block width
 * @param block_height_Y luminance block height
 * @param block_width_UV chrominance block width
 * @param block_height_UV chrominance block height
 * @param total_blocks_width number of blocks widthwise
 * @param total_blocks_height number of blocks heightwise
 */
static void mlhe_decode_delta_frame (LheState *s, 
                                     LheHuffEntry *he_Y, LheHuffEntry *he_UV,
                                     BasicLheBlock **basic_block_Y, BasicLheBlock **basic_block_UV,
                                     AdvancedLheBlock **advanced_block_Y, AdvancedLheBlock **advanced_block_UV,
                                     AdvancedLheBlock **last_advanced_block_Y, AdvancedLheBlock **last_advanced_block_UV,
                                     uint8_t *first_color_block_Y, uint8_t *first_color_block_U, uint8_t *first_color_block_V,
                                     uint8_t *symbols_Y, uint8_t *symbols_U, uint8_t *symbols_V,
                                     uint8_t *last_downsampled_image_Y, uint8_t *last_downsampled_image_U, uint8_t *last_downsampled_image_V,
                                     uint8_t *downsampled_image_Y, uint8_t *downsampled_image_U, uint8_t *downsampled_image_V,
                                     uint8_t *component_Y, uint8_t *component_U, uint8_t *component_V,
                                     uint32_t width_Y, uint32_t height_Y, uint32_t width_UV, uint32_t height_UV,
                                     uint32_t image_size_Y, uint32_t image_size_UV,
                                     uint32_t block_width_Y, uint32_t block_height_Y, uint32_t block_width_UV, uint32_t block_height_UV,
                                     uint32_t total_blocks_width, uint32_t total_blocks_height) 
{
    uint8_t *delta_frame_Y, *delta_frame_U, *delta_frame_V;
    uint8_t *intermediate_adapted_downsampled_data_Y, *intermediate_adapted_downsampled_data_U, *intermediate_adapted_downsampled_data_V;
    uint8_t *adapted_downsampled_image_Y, *adapted_downsampled_image_U, *adapted_downsampled_image_V;
    uint8_t *intermediate_interpolated_Y, *intermediate_interpolated_U, *intermediate_interpolated_V;
    
    delta_frame_Y = malloc (sizeof(uint8_t) * image_size_Y);
    delta_frame_U = malloc (sizeof(uint8_t) * image_size_UV);
    delta_frame_V = malloc (sizeof(uint8_t) * image_size_UV);
    
    intermediate_interpolated_Y = malloc (sizeof(uint8_t) * image_size_Y);
    intermediate_interpolated_U = malloc (sizeof(uint8_t) * image_size_UV);
    intermediate_interpolated_V = malloc (sizeof(uint8_t) * image_size_UV);
    
    intermediate_adapted_downsampled_data_Y = malloc(sizeof(uint8_t) * image_size_Y);  
    intermediate_adapted_downsampled_data_U = malloc(sizeof(uint8_t) * image_size_UV); 
    intermediate_adapted_downsampled_data_V = malloc(sizeof(uint8_t) * image_size_UV); 
    
    adapted_downsampled_image_Y = malloc(sizeof(uint8_t) * image_size_Y);  
    adapted_downsampled_image_U = malloc(sizeof(uint8_t) * image_size_UV); 
    adapted_downsampled_image_V = malloc(sizeof(uint8_t) * image_size_UV); 
    
    #pragma omp parallel for
    for (int block_y=0; block_y<total_blocks_height; block_y++)
    {
        for (int block_x=0; block_x<total_blocks_width; block_x++)
        {                             
            //Luminance
            lhe_advanced_decode_one_hop_per_pixel_block(&s->prec, 
                                                        basic_block_Y, advanced_block_Y,
                                                        symbols_Y, delta_frame_Y, 
                                                        width_Y, height_Y,  
                                                        first_color_block_Y, total_blocks_width, 
                                                        block_x, block_y, block_width_Y, block_height_Y); 
            
             mlhe_adapt_downsampled_data_resolution (&s->procY,
                                                     last_downsampled_image_Y, intermediate_adapted_downsampled_data_Y, adapted_downsampled_image_Y,
                                                     width_Y,
                                                     block_x, block_y);
            
             mlhe_add_delta_to_last_frame (s, 
                                          basic_block_Y, advanced_block_Y, 
                                          delta_frame_Y, 
                                          downsampled_image_Y, adapted_downsampled_image_Y,
                                          width_Y, block_x, block_y);
            
            lhe_advanced_vertical_nearest_neighbour_interpolation (basic_block_Y, advanced_block_Y,
                                                                   downsampled_image_Y, intermediate_interpolated_Y,
                                                                   width_Y, block_width_Y, block_height_Y,
                                                                   block_x, block_y);     
            
            
            lhe_advanced_horizontal_nearest_neighbour_interpolation (basic_block_Y, advanced_block_Y, 
                                                                     intermediate_interpolated_Y, component_Y,
                                                                     width_Y, s->frame->linesize[0],
                                                                     block_width_Y, block_height_Y,
                                                                     block_x, block_y);

            //Chrominance U          
            lhe_advanced_decode_one_hop_per_pixel_block(&s->prec, 
                                                        basic_block_UV, advanced_block_UV,
                                                        symbols_U, delta_frame_U, 
                                                        width_UV, height_UV, 
                                                        first_color_block_U, total_blocks_width, 
                                                        block_x, block_y, block_width_UV, block_height_UV);
            
            mlhe_adapt_downsampled_data_resolution (&s->procUV,
                                                    last_downsampled_image_U, intermediate_adapted_downsampled_data_U, adapted_downsampled_image_U,
                                                    width_UV,
                                                    block_x, block_y);
            
            mlhe_add_delta_to_last_frame (s, 
                                          basic_block_UV, advanced_block_UV, 
                                          delta_frame_U, 
                                          downsampled_image_U, adapted_downsampled_image_U,
                                          width_UV, block_x, block_y);
            
            lhe_advanced_vertical_nearest_neighbour_interpolation (basic_block_UV, advanced_block_UV,
                                                                   downsampled_image_U, intermediate_interpolated_U,
                                                                   width_UV, block_width_UV, block_height_UV,
                                                                   block_x, block_y);
            
            lhe_advanced_horizontal_nearest_neighbour_interpolation (basic_block_UV, advanced_block_UV,
                                                                     intermediate_interpolated_U, component_U,
                                                                     width_UV, s->frame->linesize[1],
                                                                     block_width_UV, block_height_UV,
                                                                     block_x, block_y);

            //Chrominance V
            lhe_advanced_decode_one_hop_per_pixel_block(&s->prec, 
                                                        basic_block_UV, advanced_block_UV,
                                                        symbols_V, delta_frame_V, 
                                                        width_UV, height_UV, 
                                                        first_color_block_V, total_blocks_width, 
                                                        block_x, block_y, block_width_UV, block_height_UV);
                   
            mlhe_adapt_downsampled_data_resolution (&s->procUV,
                                                    last_downsampled_image_V, intermediate_adapted_downsampled_data_V, adapted_downsampled_image_V,
                                                    width_UV,
                                                    block_x, block_y);

            mlhe_add_delta_to_last_frame (s, 
                                          basic_block_UV, advanced_block_UV, 
                                          delta_frame_V, 
                                          downsampled_image_V, adapted_downsampled_image_V,
                                          width_UV, block_x, block_y);
             
            lhe_advanced_vertical_nearest_neighbour_interpolation (basic_block_UV, advanced_block_UV, 
                                                                   downsampled_image_V, intermediate_interpolated_V,
                                                                   width_UV, block_width_UV, block_height_UV,
                                                                   block_x, block_y);
            
            lhe_advanced_horizontal_nearest_neighbour_interpolation (basic_block_UV, advanced_block_UV, 
                                                                     intermediate_interpolated_V, component_V,
                                                                     width_UV, s->frame->linesize[2],
                                                                     block_width_UV, block_height_UV,
                                                                     block_x, block_y);               
        }
    }     
}

//==================================================================
// DECODE FRAME
//==================================================================
static void lhe_init_pixel_format (AVCodecContext *avctx, LheState *s, uint8_t pixel_format)
{
    if (pixel_format == LHE_YUV420)
    {
        avctx->pix_fmt = AV_PIX_FMT_YUV420P;
        s->chroma_factor_width = 2;
        s->chroma_factor_height = 2;
    } else if (pixel_format == LHE_YUV422) 
    {
        avctx->pix_fmt = AV_PIX_FMT_YUV422P;
        s->chroma_factor_width = 2;
        s->chroma_factor_height = 1;
    } else if (pixel_format == LHE_YUV444) 
    {
        avctx->pix_fmt = AV_PIX_FMT_YUV444P;
        s->chroma_factor_width = 1;
        s->chroma_factor_height = 1;
    }
}

static int lhe_decode_frame(AVCodecContext *avctx, void *data, int *got_frame, AVPacket *avpkt)
{    
    uint8_t lhe_mode, pixel_format, quality_level;
    uint8_t *component_Y, *component_U, *component_V;
    uint32_t total_blocks_width, total_blocks_height, total_blocks;
    uint32_t pixels_block, image_size_Y, image_size_UV;
    int ret;
    
    float compression_factor;
    uint32_t ppp_max_theoric;
    
    LheHuffEntry he_mesh[LHE_MAX_HUFF_SIZE_MESH];
    LheHuffEntry he_Y[LHE_MAX_HUFF_SIZE_SYMBOLS];
    LheHuffEntry he_UV[LHE_MAX_HUFF_SIZE_SYMBOLS];
   
    LheState *s = avctx->priv_data;
    
    const uint8_t *lhe_data = avpkt->data;
    
    //LHE mode
    lhe_mode = bytestream_get_byte(&lhe_data); 
    
    //Pixel format byte, init pixel format
    pixel_format = bytestream_get_byte(&lhe_data); 
    lhe_init_pixel_format (avctx, s, pixel_format);
           
    (&s->procY)->width  = bytestream_get_le32(&lhe_data);
    (&s->procY)->height = bytestream_get_le32(&lhe_data);
    
    image_size_Y = (&s->procY)->width * (&s->procY)->height;
    
    (&s->procUV)->width = ((&s->procY)->width - 1)/s->chroma_factor_width + 1;
    (&s->procUV)->height = ((&s->procY)->height - 1)/s->chroma_factor_height + 1;
    image_size_UV = (&s->procUV)->width * (&s->procUV)->height;
    
    avctx->width  = (&s->procY)->width;
    avctx->height  = (&s->procY)->height;    
    
    //Allocates frame
    av_frame_unref(s->frame);
    if ((ret = ff_get_buffer(avctx, s->frame, 0)) < 0)
        return ret;
 
    if (lhe_mode == SEQUENTIAL_BASIC_LHE) 
    {
        total_blocks_width = 1;
        total_blocks_height = 1;
    } 
    else 
    {
        total_blocks_width = HORIZONTAL_BLOCKS;
        pixels_block = (&s->procY)->width / HORIZONTAL_BLOCKS;
        total_blocks_height = (&s->procY)->height / pixels_block;
    }
    
    total_blocks = total_blocks_height * total_blocks_width;
    
    //First pixel array
    (&s->lheY)->first_color_block = malloc(sizeof(uint8_t) * image_size_Y);
    (&s->lheU)->first_color_block = malloc(sizeof(uint8_t) * image_size_UV);
    (&s->lheV)->first_color_block = malloc(sizeof(uint8_t) * image_size_UV);
    
    for (int i=0; i<total_blocks; i++) 
    {
        (&s->lheY)->first_color_block[i] = bytestream_get_byte(&lhe_data); 
    }

    
    for (int i=0; i<total_blocks; i++) 
    {
        (&s->lheU)->first_color_block[i] = bytestream_get_byte(&lhe_data); 
    }
    
        
    for (int i=0; i<total_blocks; i++) 
    {
        (&s->lheV)->first_color_block[i] = bytestream_get_byte(&lhe_data); 
    }

    //Pointers to different color components
    component_Y = s->frame->data[0];
    component_U = s->frame->data[1];
    component_V = s->frame->data[2];
      
    (&s->lheY)->hops = malloc(sizeof(uint8_t) * image_size_Y);      
    (&s->lheU)->hops = malloc(sizeof(uint8_t) * image_size_UV);    
    (&s->lheV)->hops = malloc(sizeof(uint8_t) * image_size_UV); 
    
    (&s->procY)->basic_block = malloc(sizeof(BasicLheBlock *) * total_blocks_height);
    
    for (int i=0; i < total_blocks_height; i++)
    {
        (&s->procY)->basic_block[i] = malloc (sizeof(BasicLheBlock) * (total_blocks_width));
    }
    
    (&s->procUV)->basic_block = malloc(sizeof(BasicLheBlock *) * total_blocks_height);
    
    for (int i=0; i < total_blocks_height; i++)
    {
        (&s->procUV)->basic_block[i] = malloc (sizeof(BasicLheBlock) * (total_blocks_width));
    }
           
    init_get_bits(&s->gb, lhe_data, avpkt->size * 8);

    lhe_read_huffman_table(s, he_Y, LHE_MAX_HUFF_SIZE_SYMBOLS, LHE_HUFFMAN_NODE_BITS_SYMBOLS, LHE_HUFFMAN_NO_OCCURRENCES_SYMBOLS);
    lhe_read_huffman_table(s, he_UV, LHE_MAX_HUFF_SIZE_SYMBOLS, LHE_HUFFMAN_NODE_BITS_SYMBOLS, LHE_HUFFMAN_NO_OCCURRENCES_SYMBOLS);
    
    if (lhe_mode == ADVANCED_LHE) /*ADVANCED LHE*/
    {
        (&s->procY)->perceptual_relevance_x = malloc(sizeof(float*) * (total_blocks_height+1));  
    
        for (int i=0; i<total_blocks_height+1; i++) 
        {
            (&s->procY)->perceptual_relevance_x[i] = malloc(sizeof(float) * (total_blocks_width+1));
        }
        
        (&s->procY)->perceptual_relevance_y = malloc(sizeof(float*) * (total_blocks_height+1)); 
        
        for (int i=0; i<total_blocks_height+1; i++) 
        {
            (&s->procY)->perceptual_relevance_y[i] = malloc(sizeof(float) * (total_blocks_width+1));
        }   

        (&s->procY)->advanced_block = malloc(sizeof(AdvancedLheBlock *) * total_blocks_height);
        
        for (int i=0; i < total_blocks_height; i++)
        {
            (&s->procY)->advanced_block[i] = malloc (sizeof(AdvancedLheBlock) * (total_blocks_width));
        }
        
        (&s->procUV)->advanced_block = malloc(sizeof(AdvancedLheBlock *) * total_blocks_height);
        
        for (int i=0; i < total_blocks_height; i++)
        {
            (&s->procUV)->advanced_block[i] = malloc (sizeof(AdvancedLheBlock) * (total_blocks_width));
        }
        
        (&s->procY)-> block_width = (&s->procY)->width / total_blocks_width;    
        (&s->procY)-> block_height = (&s->procY)->height / total_blocks_height;   
        
        (&s->procUV)-> block_width = (&s->procUV)->width / total_blocks_width;
        (&s->procUV)-> block_height = (&s->procUV)->height / total_blocks_height; 
        
        (&s->lheY)-> downsampled_image = malloc (sizeof(uint8_t) * image_size_Y);
        (&s->lheU)-> downsampled_image = malloc (sizeof(uint8_t) * image_size_UV);
        (&s->lheV)-> downsampled_image = malloc (sizeof(uint8_t) * image_size_UV);
        
        //MESH Huffman
        lhe_read_huffman_table(s, he_mesh, LHE_MAX_HUFF_SIZE_MESH, LHE_HUFFMAN_NODE_BITS_MESH, LHE_HUFFMAN_NO_OCCURRENCES_MESH);
        
        //Read quality level and calculate compression factor
        quality_level = get_bits(&s->gb, QL_SIZE_BITS); 
        ppp_max_theoric = (&s->procY)-> block_width/SIDE_MIN;
        compression_factor = (&s->prec)->compression_factor[ppp_max_theoric][quality_level];        
       
        lhe_advanced_read_mesh(s, he_mesh,
                               (&s->procY)->basic_block, (&s->procUV)->basic_block,
                               (&s->procY)->advanced_block, (&s->procUV)->advanced_block,
                               (&s->procY)->perceptual_relevance_x, (&s->procY)->perceptual_relevance_y,
                               ppp_max_theoric, compression_factor,
                               (&s->procY)->width, (&s->procY)->height, (&s->procUV)->width, (&s->procUV)->height,
                               (&s->procY)->block_width, (&s->procY)->block_height, (&s->procUV)->block_width, (&s->procUV)->block_height,
                               total_blocks_width, total_blocks_height) ; 
        

        lhe_advanced_read_all_file_symbols (s, 
                                            he_Y, he_UV,
                                            (&s->procY)->basic_block, (&s->procUV)->basic_block,
                                            (&s->procY)->advanced_block, (&s->procUV)->advanced_block,
                                            (&s->lheY)->hops, (&s->lheU)->hops, (&s->lheV)->hops,
                                            (&s->procY)->width, (&s->procY)->height, (&s->procUV)->width, (&s->procUV)->height, 
                                            total_blocks_width, total_blocks_height);
        
        lhe_advanced_decode_symbols (s, 
                                     he_Y, he_UV,
                                     (&s->procY)->basic_block, (&s->procUV)->basic_block,
                                     (&s->procY)->advanced_block, (&s->procUV)->advanced_block,
                                     (&s->lheY)->first_color_block, (&s->lheU)->first_color_block, (&s->lheV)->first_color_block,
                                     (&s->lheY)->hops, (&s->lheU)->hops, (&s->lheV)->hops,
                                     (&s->lheY)->downsampled_image, (&s->lheU)->downsampled_image, (&s->lheV)->downsampled_image,
                                     component_Y, component_U, component_V,
                                     (&s->procY)->width, (&s->procY)->height, (&s->procUV)->width, (&s->procUV)->height,
                                     image_size_Y, image_size_UV,
                                     (&s->procY)->block_width, (&s->procY)->block_height, (&s->procUV)->block_width, (&s->procUV)->block_height,
                                     total_blocks_width, total_blocks_height);
        
        
    }
    else /*BASIC LHE*/       
    {
        lhe_basic_read_file_symbols(s, he_Y, image_size_Y, (&s->lheY)->hops);
        lhe_basic_read_file_symbols(s, he_UV, image_size_UV, (&s->lheU)->hops);
        lhe_basic_read_file_symbols(s, he_UV, image_size_UV, (&s->lheV)->hops);
 
        if (total_blocks > 1 && OPENMP_FLAGS == CONFIG_OPENMP) 
        {
            (&s->procY)->block_width = (&s->procY)->width / total_blocks_width;
            (&s->procY)->block_height = (&s->procY)->height / total_blocks_height;
            (&s->procUV)->block_width = (&s->procUV)->width /total_blocks_width;
            (&s->procUV)->block_height = (&s->procUV)->height /total_blocks_height;

            lhe_basic_decode_frame_pararell (s, &s->prec,
                                             (&s->procY)->basic_block, (&s->procUV)->basic_block,
                                             component_Y, component_U, component_V, 
                                             (&s->lheY)->hops, (&s->lheU)->hops, (&s->lheV)->hops,
                                             (&s->procY)->width, (&s->procY)->height, (&s->procUV)->width, (&s->procUV)->height, 
                                             s->frame->linesize[0], s->frame->linesize[1], s->frame->linesize[2],
                                             (&s->lheY)->first_color_block, (&s->lheU)->first_color_block, (&s->lheV)->first_color_block,
                                             total_blocks_width, total_blocks_height,
                                             (&s->procY)->block_width, (&s->procY)->block_height, (&s->procUV)->block_width, (&s->procUV)->block_height);                            
        } else 
        {      
            lhe_basic_decode_frame_sequential (&s->prec, 
                                               component_Y, component_U, component_V, 
                                               (&s->lheY)->hops, (&s->lheU)->hops, (&s->lheV)->hops,
                                               (&s->procY)->width, (&s->procY)->height, (&s->procUV)->width, (&s->procUV)->height, 
                                               s->frame->linesize[0], s->frame->linesize[1], s->frame->linesize[2],
                                               (&s->lheY)->first_color_block, (&s->lheU)->first_color_block, (&s->lheV)->first_color_block);    
        }
    }
   
    av_log(NULL, AV_LOG_INFO, "DECODING...Width %d Height %d \n", (&s->procY)->width, (&s->procY)->height);

    if ((ret = av_frame_ref(data, s->frame)) < 0)
        return ret;
 
    *got_frame = 1;

    return 0;
}

//==================================================================
// DECODE VIDEO FRAME
//==================================================================
static int mlhe_decode_video(AVCodecContext *avctx, void *data, int *got_frame, AVPacket *avpkt)
{    
    uint8_t *component_Y, *component_U, *component_V;
    uint32_t total_blocks, pixels_block, image_size_Y, image_size_UV;
    int ret;
    
    float compression_factor;
    uint32_t ppp_max_theoric;
    
    LheHuffEntry he_mesh[LHE_MAX_HUFF_SIZE_MESH];
    LheHuffEntry he_Y[LHE_MAX_HUFF_SIZE_SYMBOLS];
    LheHuffEntry he_UV[LHE_MAX_HUFF_SIZE_SYMBOLS];
   
    LheState *s = avctx->priv_data;
    
    const uint8_t *lhe_data = avpkt->data;

    
    if ((&s->lheY)->last_downsampled_image) { /*DELTA VIDEO FRAME*/
        s->dif_frames_count++;
        
        image_size_Y = (&s->procY)->width * (&s->procY)->height;
        image_size_UV = (&s->procUV)->width * (&s->procUV)->height; 
        
        //Allocates frame
        av_frame_unref(s->frame);
        if ((ret = ff_get_buffer(avctx, s->frame, 0)) < 0)
            return ret;
    
        if (s->lhe_mode == SEQUENTIAL_BASIC_LHE) 
        {
            s->total_blocks_width = 1;
            s->total_blocks_height = 1;
        } 
        else 
        {
            s->total_blocks_width = HORIZONTAL_BLOCKS;
            pixels_block = (&s->procY)->width / HORIZONTAL_BLOCKS;
            s->total_blocks_height = (&s->procY)->height / pixels_block;
        }
        
        total_blocks = s->total_blocks_height * s->total_blocks_width;
        
        //First pixel array
        for (int i=0; i<total_blocks; i++) 
        {
            (&s->lheY)->first_color_block[i] = bytestream_get_byte(&lhe_data); 
        }

        
        for (int i=0; i<total_blocks; i++) 
        {
            (&s->lheU)->first_color_block[i] = bytestream_get_byte(&lhe_data); 
        }
        
            
        for (int i=0; i<total_blocks; i++) 
        {
            (&s->lheV)->first_color_block[i] = bytestream_get_byte(&lhe_data); 
        }

        //Pointers to different color components
        component_Y = s->frame->data[0];
        component_U = s->frame->data[1];
        component_V = s->frame->data[2];
        
        init_get_bits(&s->gb, lhe_data, avpkt->size * 8);

        lhe_read_huffman_table(s, he_Y, LHE_MAX_HUFF_SIZE_SYMBOLS, LHE_HUFFMAN_NODE_BITS_SYMBOLS, LHE_HUFFMAN_NO_OCCURRENCES_SYMBOLS);
        lhe_read_huffman_table(s, he_UV, LHE_MAX_HUFF_SIZE_SYMBOLS, LHE_HUFFMAN_NODE_BITS_SYMBOLS, LHE_HUFFMAN_NO_OCCURRENCES_SYMBOLS);
        
         //MESH Huffman
        lhe_read_huffman_table(s, he_mesh, LHE_MAX_HUFF_SIZE_MESH, LHE_HUFFMAN_NODE_BITS_MESH, LHE_HUFFMAN_NO_OCCURRENCES_MESH);     
        
        //Calculate compression factor
        ppp_max_theoric = (&s->procY)->block_width/SIDE_MIN;
        compression_factor = (&s->prec)->compression_factor[ppp_max_theoric][s->quality_level];        
       
        lhe_advanced_read_mesh(s, he_mesh,
                               (&s->procY)->basic_block, (&s->procUV)->basic_block,
                               (&s->procY)->advanced_block, (&s->procUV)->advanced_block,
                               (&s->procY)->perceptual_relevance_x, (&s->procY)->perceptual_relevance_y,
                               ppp_max_theoric, compression_factor,
                               (&s->procY)->width, (&s->procY)->height,(&s->procUV)->width,  (&s->procUV)->height, 
                               (&s->procY)->block_width, (&s->procY)->block_height, (&s->procUV)->block_width, (&s->procUV)->block_height,
                               s->total_blocks_width, s->total_blocks_height) ; 
        

        lhe_advanced_read_all_file_symbols (s, 
                                            he_Y, he_UV,
                                            (&s->procY)->basic_block, (&s->procUV)->basic_block,
                                            (&s->procY)->advanced_block, (&s->procUV)->advanced_block,
                                            (&s->lheY)->hops, (&s->lheU)->hops, (&s->lheV)->hops,
                                            (&s->procY)->width, (&s->procY)->height,(&s->procUV)->width, (&s->procUV)->height, 
                                            s->total_blocks_width, s->total_blocks_height);
        
        mlhe_decode_delta_frame (s, 
                                 he_Y, he_UV,
                                 (&s->procY)->basic_block, (&s->procUV)->basic_block,
                                 (&s->procY)->advanced_block, (&s->procUV)->advanced_block,
                                 (&s->procY)->last_advanced_block, (&s->procUV)->last_advanced_block,
                                 (&s->lheY)->first_color_block, (&s->lheU)->first_color_block, (&s->lheV)->first_color_block,
                                 (&s->lheY)->hops, (&s->lheU)->hops, (&s->lheV)->hops,
                                 (&s->lheY)->last_downsampled_image, (&s->lheU)->last_downsampled_image, (&s->lheV)->last_downsampled_image,
                                 (&s->lheY)->downsampled_image, (&s->lheU)->downsampled_image, (&s->lheV)->downsampled_image,
                                 component_Y, component_U, component_V,
                                 (&s->procY)->width, (&s->procY)->height, (&s->procUV)->width, (&s->procUV)->height,
                                 image_size_Y, image_size_UV,
                                 (&s->procY)->block_width, (&s->procY)->block_height, (&s->procUV)->block_width, (&s->procUV)->block_height,
                                 s->total_blocks_width, s->total_blocks_height);
    } 
    else 
    {
        //LHE mode
        s->lhe_mode = bytestream_get_byte(&lhe_data); 
        
        //Pixel format byte, init pixel format
        s->pixel_format = bytestream_get_byte(&lhe_data); 
        lhe_init_pixel_format (avctx, s, s->pixel_format);
            
        (&s->procY)-> width = bytestream_get_le32(&lhe_data);
        (&s->procY)-> height = bytestream_get_le32(&lhe_data);
        
        image_size_Y = (&s->procY)-> width * (&s->procY)-> height;
        
        (&s->procUV)-> width = ((&s->procY)-> width - 1)/s->chroma_factor_width + 1;
        (&s->procUV)-> height = ((&s->procY)-> height - 1)/s->chroma_factor_height + 1;
        image_size_UV = (&s->procUV)-> width * (&s->procUV)-> height;
        
        avctx->width  = (&s->procY)-> width;
        avctx->height  = (&s->procY)-> height;    
        
        //Allocates frame
        av_frame_unref(s->frame);
        if ((ret = ff_get_buffer(avctx, s->frame, 0)) < 0)
            return ret;
    
        if (s->lhe_mode == SEQUENTIAL_BASIC_LHE) 
        {
            s->total_blocks_width = 1;
            s->total_blocks_height = 1;
        } 
        else 
        {
            s->total_blocks_width = HORIZONTAL_BLOCKS;
            pixels_block = (&s->procY)-> width / HORIZONTAL_BLOCKS;
            s->total_blocks_height = (&s->procY)-> height / pixels_block;
        }
        
        total_blocks = s->total_blocks_height * s->total_blocks_width;
        
        //First pixel array
        (&s->lheY)->first_color_block = malloc(sizeof(uint8_t) * total_blocks);
        (&s->lheU)->first_color_block = malloc(sizeof(uint8_t) * total_blocks);
        (&s->lheV)->first_color_block = malloc(sizeof(uint8_t) * total_blocks);
        
        for (int i=0; i<total_blocks; i++) 
        {
            (&s->lheY)->first_color_block[i] = bytestream_get_byte(&lhe_data); 
        }

        
        for (int i=0; i<total_blocks; i++) 
        {
            (&s->lheU)->first_color_block[i] = bytestream_get_byte(&lhe_data); 
        }
        
            
        for (int i=0; i<total_blocks; i++) 
        {
            (&s->lheV)->first_color_block[i] = bytestream_get_byte(&lhe_data); 
        }

        //Pointers to different color components
        component_Y = s->frame->data[0];
        component_U = s->frame->data[1];
        component_V = s->frame->data[2];
        
        (&s->lheY)-> hops = malloc(sizeof(uint8_t) * image_size_Y);      
        (&s->lheU)-> hops = malloc(sizeof(uint8_t) * image_size_UV);    
        (&s->lheV)-> hops = malloc(sizeof(uint8_t) * image_size_UV); 
            
        
        (&s->procY)->basic_block = malloc(sizeof(BasicLheBlock *) * s->total_blocks_height);
        
        for (int i=0; i < s->total_blocks_height; i++)
        {
            (&s->procY)->basic_block[i] = malloc (sizeof(BasicLheBlock) * (s->total_blocks_width));
        }
        
        (&s->procUV)->basic_block = malloc(sizeof(BasicLheBlock *) * s->total_blocks_height);
        
        for (int i=0; i < s->total_blocks_height; i++)
        {
            (&s->procUV)->basic_block[i] = malloc (sizeof(BasicLheBlock) * (s->total_blocks_width));
        }
            
        init_get_bits(&s->gb, lhe_data, avpkt->size * 8);

        lhe_read_huffman_table(s, he_Y, LHE_MAX_HUFF_SIZE_SYMBOLS, LHE_HUFFMAN_NODE_BITS_SYMBOLS, LHE_HUFFMAN_NO_OCCURRENCES_SYMBOLS);
        lhe_read_huffman_table(s, he_UV, LHE_MAX_HUFF_SIZE_SYMBOLS, LHE_HUFFMAN_NODE_BITS_SYMBOLS, LHE_HUFFMAN_NO_OCCURRENCES_SYMBOLS);
        
        
        /*Init dif frames count*/
        s->dif_frames_count=0;

        (&s->procY)->perceptual_relevance_x = malloc(sizeof(float*) * (s->total_blocks_height+1));  
    
        for (int i=0; i<s->total_blocks_height+1; i++) 
        {
            (&s->procY)->perceptual_relevance_x[i] = malloc(sizeof(float) * (s->total_blocks_width+1));
        }
        
        (&s->procY)->perceptual_relevance_y = malloc(sizeof(float*) * (s->total_blocks_height+1)); 
        
        for (int i=0; i<s->total_blocks_height+1; i++) 
        {
            (&s->procY)->perceptual_relevance_y[i] = malloc(sizeof(float) * (s->total_blocks_width+1));
        }   

        (&s->procY)->advanced_block = malloc(sizeof(AdvancedLheBlock *) * s->total_blocks_height);
        
        for (int i=0; i < s->total_blocks_height; i++)
        {
            (&s->procY)->advanced_block[i] = malloc (sizeof(AdvancedLheBlock) * (s->total_blocks_width));
        }
        
        (&s->procUV)->advanced_block = malloc(sizeof(AdvancedLheBlock *) * s->total_blocks_height);
        
        for (int i=0; i < s->total_blocks_height; i++)
        {
            (&s->procUV)->advanced_block[i] = malloc (sizeof(AdvancedLheBlock) * (s->total_blocks_width));
        }
        
        (&s->procY)-> block_width = (&s->procY)-> width / s->total_blocks_width;    
        (&s->procY)-> block_height = (&s->procY)-> height / s->total_blocks_height;   
        
        (&s->procUV)-> block_width = (&s->procUV)-> width / s->total_blocks_width;
        (&s->procUV)-> block_height = (&s->procUV)-> height / s->total_blocks_height; 
        
        (&s->lheY)->downsampled_image = malloc (sizeof(uint8_t) * image_size_Y);
        (&s->lheU)->downsampled_image = malloc (sizeof(uint8_t) * image_size_UV);
        (&s->lheV)->downsampled_image = malloc (sizeof(uint8_t) * image_size_UV);
        
        //MESH Huffman
        lhe_read_huffman_table(s, he_mesh, LHE_MAX_HUFF_SIZE_MESH, LHE_HUFFMAN_NODE_BITS_MESH, LHE_HUFFMAN_NO_OCCURRENCES_MESH);
        
        //Read quality level and calculate compression factor
        s->quality_level = get_bits(&s->gb, QL_SIZE_BITS); 
        ppp_max_theoric = (&s->procY)-> block_width/SIDE_MIN;
        compression_factor = (&s->prec)->compression_factor[ppp_max_theoric][s->quality_level];        
       
        lhe_advanced_read_mesh(s, he_mesh,
                               (&s->procY)->basic_block, (&s->procUV)->basic_block,
                               (&s->procY)->advanced_block, (&s->procUV)->advanced_block,
                               (&s->procY)->perceptual_relevance_x, (&s->procY)->perceptual_relevance_y,
                               ppp_max_theoric, compression_factor,
                               (&s->procY)->width, (&s->procY)->height, (&s->procUV)->width, (&s->procUV)->height,
                               (&s->procY)->block_width, (&s->procY)->block_height, (&s->procUV)->block_width, (&s->procUV)->block_height,
                               s->total_blocks_width, s->total_blocks_height) ; 
        

        lhe_advanced_read_all_file_symbols (s, 
                                            he_Y, he_UV,
                                            (&s->procY)->basic_block, (&s->procUV)->basic_block,
                                            (&s->procY)->advanced_block, (&s->procUV)->advanced_block,
                                            (&s->lheY)->hops, (&s->lheU)->hops, (&s->lheV)->hops,
                                            (&s->procY)->width, (&s->procY)->height, (&s->procUV)->width, (&s->procUV)->height,
                                            s->total_blocks_width, s->total_blocks_height);
        
        lhe_advanced_decode_symbols (s, 
                                     he_Y, he_UV,
                                     (&s->procY)->basic_block, (&s->procUV)->basic_block,
                                     (&s->procY)->advanced_block, (&s->procUV)->advanced_block,
                                     (&s->lheY)->first_color_block, (&s->lheU)->first_color_block, (&s->lheV)->first_color_block,
                                     (&s->lheY)->hops, (&s->lheU)->hops, (&s->lheV)->hops,
                                     (&s->lheY)->downsampled_image, (&s->lheU)->downsampled_image, (&s->lheV)->downsampled_image,
                                     component_Y, component_U, component_V,
                                     (&s->procY)->width, (&s->procY)->height, (&s->procUV)->width, (&s->procUV)->height,
                                     image_size_Y, image_size_UV,
                                     (&s->procY)->block_width, (&s->procY)->block_height, (&s->procUV)->block_width, (&s->procUV)->block_height,
                                     s->total_blocks_width, s->total_blocks_height);
 
  
    }   

    if (!(&s->procY)->last_advanced_block) 
    {
         (&s->procY)->last_advanced_block = malloc(sizeof(AdvancedLheBlock *) * s->total_blocks_height);
        
        for (int i=0; i < s->total_blocks_height; i++)
        {
            (&s->procY)->last_advanced_block[i] = malloc (sizeof(AdvancedLheBlock) * (s->total_blocks_width));
        }      
    }
    
    if (!(&s->procUV)->last_advanced_block) {
        (&s->procUV)->last_advanced_block = malloc(sizeof(AdvancedLheBlock *) * s->total_blocks_height);
        
        for (int i=0; i < s->total_blocks_height; i++)
        {
            (&s->procUV)->last_advanced_block[i] = malloc (sizeof(AdvancedLheBlock) * (s->total_blocks_width));
        }
    }

    
    if (!(&s->lheY)->last_downsampled_image) {
        (&s->lheY)->last_downsampled_image = malloc(sizeof(uint8_t) * image_size_Y);  
    }
    

    if (!(&s->lheU)->last_downsampled_image) {
        (&s->lheU)->last_downsampled_image = malloc(sizeof(uint8_t) * image_size_UV); 
    }
    
    
    if (!(&s->lheV)->last_downsampled_image) {
        (&s->lheV)->last_downsampled_image = malloc(sizeof(uint8_t) * image_size_UV);  
    }
    
     for (int i=0; i < s->total_blocks_height; i++)
    {
        memcpy((&s->procY)->last_advanced_block[i], (&s->procY)->advanced_block[i], sizeof(AdvancedLheBlock) * (s->total_blocks_width));
        memcpy((&s->procUV)->last_advanced_block[i], (&s->procUV)->advanced_block[i], sizeof(AdvancedLheBlock) * (s->total_blocks_width));
    }   
    
    memcpy ((&s->lheY)->last_downsampled_image, (&s->lheY)->downsampled_image, image_size_Y);    
    memcpy ((&s->lheU)->last_downsampled_image, (&s->lheU)->downsampled_image, image_size_UV);
    memcpy ((&s->lheV)->last_downsampled_image, (&s->lheV)->downsampled_image, image_size_UV);
    
    memset((&s->lheY)->downsampled_image, 0, image_size_Y);
    memset((&s->lheU)->downsampled_image, 0, image_size_UV);
    memset((&s->lheV)->downsampled_image, 0, image_size_UV);  
    
    
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

AVCodec ff_mlhe_decoder = {
    .name           = "mlhe",
    .long_name      = NULL_IF_CONFIG_SMALL("MLHE"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_MLHE,
    .priv_data_size = sizeof(LheState),
    .init           = lhe_decode_init,
    .close          = lhe_decode_close,
    .decode         = mlhe_decode_video,
    .priv_class     = &decoder_class,
};
