/*
 * LHE Basic decoder
 */

#include "bytestream.h"
#include "get_bits.h"
#include "internal.h"
#include "lhe.h"
#include "unistd.h"

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
    uint64_t global_frames_count;
} LheState;

uint8_t *intermediate_interpolated_Y, *intermediate_interpolated_U, *intermediate_interpolated_V;
uint8_t *delta_prediction_Y_dec, *delta_prediction_U_dec, *delta_prediction_V_dec;
uint8_t *intermediate_adapted_downsampled_data_Y_dec, *intermediate_adapted_downsampled_data_U_dec, *intermediate_adapted_downsampled_data_V_dec;
uint8_t *adapted_downsampled_image_Y, *adapted_downsampled_image_U, *adapted_downsampled_image_V;

bool lhe_advanced_decode_symbols_alloc_mem = false;
bool mlhe_decode_delta_frame_alloc_mem = false;

static av_cold int lhe_decode_init(AVCodecContext *avctx)
{
    LheState *s = avctx->priv_data;

    s->frame = av_frame_alloc();
    if (!s->frame)
        return AVERROR(ENOMEM);
    
    //lhe_init_cache(&s->prec);
    lhe_init_cache2(&s->prec);
    
    s->global_frames_count = 0;
    
    return 0;
}

//==================================================================
// HUFFMAN FUNCTIONS
//==================================================================
/**
 * Reads Huffman table
 * 
 * @param *s LHE State
 * @param *he LHE Huffman Entry
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
 * @param s Lhe parameters
 * @param he LHE Huffman entry
 * @param image_size image size
 * @param *symbols Symbols read from file
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
 * @param *s Lhe parameters
 * @param *he Huffman entry, Huffman parameters
 * @param *proc LHE processing parameters
 * @param symbols symbols array (hops)
 * @param block_x block x index
 * @param block_y block y index
 */
static void lhe_advanced_read_file_symbols (LheState *s, LheHuffEntry *he, LheProcessing *proc, uint8_t *symbols, 
                                            int block_x, int block_y) 
{
    uint8_t symbol, count_bits;
    uint32_t huffman_symbol, pix;
    uint32_t xini, xfin_downsampled, yini, yfin_downsampled;
    
    xini = proc->basic_block[block_y][block_x].x_ini;
    xfin_downsampled = proc->advanced_block[block_y][block_x].x_fin_downsampled; 
 
    yini = proc->basic_block[block_y][block_x].y_ini;
    yfin_downsampled = proc->advanced_block[block_y][block_x].y_fin_downsampled;

    symbol = NO_SYMBOL;
    pix = 0;
    huffman_symbol = 0;
    count_bits = 0;

    for (int y=yini; y<yfin_downsampled;y++) 
    {
        for(int x=xini; x<xfin_downsampled;) 
        {
            pix = y * proc->width + x;

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

static uint8_t get_hop(int zeros_since_a_one) {

    if (zeros_since_a_one == 0) {
        return (uint8_t)HOP_0;
    }
    else if (zeros_since_a_one == 1) {
        return (uint8_t)HOP_P1;
    }
    else if (zeros_since_a_one == 2) {
        return (uint8_t)HOP_N1;
    }
    else if (zeros_since_a_one == 3) {
        return (uint8_t)HOP_P2;
    }
    else if (zeros_since_a_one == 4) {
        return (uint8_t)HOP_N2;
    }
    else if (zeros_since_a_one == 5) {
        return (uint8_t)HOP_P3;
    }
    else if (zeros_since_a_one == 6) {
        return (uint8_t)HOP_N3;
    }
    else if (zeros_since_a_one == 7) {
        return (uint8_t)HOP_P4;
    }
    else if (zeros_since_a_one == 8) {
        return (uint8_t)HOP_N4;
    }
    else {
        return (uint8_t) 255;
    }

}

static uint8_t set_bit(uint8_t data, int possition) {

    uint8_t mask = 1;
    mask = mask << possition;
    mask = mask | data;
    return mask;
}

static int get_rlc_number(LheState *s, int rlc_lenght) {
    int rlc_number = 0;

    for (int j = rlc_lenght - 1; j >= 0; j--) {
        if (get_bits(&s->gb, 1) == 1) {
            rlc_number = set_bit(rlc_number, j);
        }
    }

    return rlc_number;
}


static void add_hop0(LheProcessing *proc, uint8_t *symbols, int *pix, int count) {

    for (int i = 0; i < count; i++) {
        symbols[*pix] = HOP_0;
        *pix= (*pix +1);
    }
}


static void lhe_advanced_read_file_symbols2 (LheState *s, LheProcessing *proc, uint8_t *symbols, int block_y_ini, int block_y_fin) {

    int xini, yini, xfin_downsampled, yfin_downsampled, pix, dif_pix;

    int mode = HUFFMAN, h0_counter = 0, hops_counter = 0, zero_counter = 0, hop = 15, data = 3, rlc_number = 0, ahorro = 0;
    int condition_length = 7;
    int rlc_length = 4;
    int last_mode = HUFFMAN;

    int block_x = block_y_ini*HORIZONTAL_BLOCKS;
    int inc_x = 1;

    uint8_t hopl[9]={ 4,5,3,6,2,7,1,8,0 };

    uint8_t *hops = av_calloc(proc->width*proc->height, sizeof(uint8_t));


    while (true){
        if (hops_counter == proc->num_hops) break;
        data = get_bits(&s->gb, 1);
        switch (mode){
            case HUFFMAN:
                if (data == 0) zero_counter++;
                if (data == 1 || zero_counter+ahorro == 8){
                    //hop = get_hop(zero_counter+ahorro);
                    hop=hopl[zero_counter+ahorro];
                    ahorro=0;
                    if (hop == HOP_0) h0_counter++;
                    else h0_counter = 0;
                    if (h0_counter == condition_length) mode = RLC1;
                    hops[hops_counter] = hop;
                    hops_counter++;
                    zero_counter = 0;
                }
            break;
            case RLC1:
                if (data == 0) {
                    rlc_number = get_rlc_number(s, rlc_length);
                    add_hop0(proc, hops, &hops_counter, rlc_number);
                    h0_counter = 0;
                    mode = HUFFMAN;
                    ahorro=1;

                } else {
                    add_hop0(proc, hops, &hops_counter, 15);
                    rlc_length += 1;
                    mode = RLC2;
                }
            break;
            case RLC2:
                if (data == 0) {
                    rlc_number = get_rlc_number(s, rlc_length);
                    add_hop0(proc, hops, &hops_counter, rlc_number);
                    rlc_length = 4;
                    h0_counter = 0;
                    mode = HUFFMAN;
                    ahorro=1;
                } else {
                    add_hop0(proc, hops, &hops_counter, 31);
                }
            break;
        }
    }

    int a = 0;

    for (int block_y = block_y_ini; block_y < block_y_fin; block_y++) {
        for (int i = 0; i < HORIZONTAL_BLOCKS; i++) {

            xini = proc->basic_block[block_y][block_x].x_ini;
            yini = proc->basic_block[block_y][block_x].y_ini;
            
            xfin_downsampled = proc->advanced_block[block_y][block_x].x_fin_downsampled;          
            yfin_downsampled = proc->advanced_block[block_y][block_x].y_fin_downsampled;

            pix = yini*proc->width+xini;
            dif_pix = proc->width-xfin_downsampled+xini;

            for (int y = yini; y < yfin_downsampled; y++) {
                for (int x = xini; x < xfin_downsampled; x++) {

                    symbols[pix] = hops[a];
                    a++;
                    pix++;
                }
                pix+=dif_pix;
            }

            block_x += inc_x;
        }
        block_x=0;
        //block_x -= inc_x;
        //inc_x = -inc_x;
    }

}









/**
 * Reads file symbols from advanced lhe file
 * 
 * @param *s Lhe parameters
 * @param *he_Y Luminance Huffman entry, Huffman parameters
 * @param *he_UV Chrominance Huffman entry, Huffman parameters
 */
static void lhe_advanced_read_all_file_symbols (LheState *s, LheHuffEntry *he_Y, LheHuffEntry *he_UV) 
{
    LheProcessing *procY, *procUV;
    LheImage *lheY, *lheU, *lheV;
    
    procY = &s->procY;
    procUV = &s->procUV;
    
    lheY = &s->lheY;
    lheU = &s->lheU;
    lheV = &s->lheV;
    
    lhe_advanced_read_file_symbols2 (s, procY, lheY->hops, 0, s->total_blocks_height);   
    lhe_advanced_read_file_symbols2 (s, procUV, lheU->hops, 0, s->total_blocks_height);            
    lhe_advanced_read_file_symbols2 (s, procUV, lheV->hops, 0, s->total_blocks_height);

/*
    for (int block_y=0; block_y<s->total_blocks_height; block_y++)
    {
        for (int block_x=0; block_x<s->total_blocks_width; block_x++)
        {          

            //lhe_advanced_read_file_symbols (s, he_Y, procY, lheY->hops, block_x, block_y);   
            //lhe_advanced_read_file_symbols (s, he_UV, procUV, lheU->hops, block_x, block_y);            
            //lhe_advanced_read_file_symbols (s, he_UV, procUV, lheV->hops, block_x, block_y);
            

        }
    }
    */
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
 */
static void lhe_advanced_read_perceptual_relevance_interval (LheState *s, LheHuffEntry *he_mesh, float ** perceptual_relevance) 
{
    uint8_t perceptual_relevance_interval, count_bits;
    uint32_t huffman_symbol;
    
    perceptual_relevance_interval = NO_INTERVAL;
    count_bits = 0;
    huffman_symbol = 0;
    
    for (int block_y=0; block_y<s->total_blocks_height+1; block_y++) 
    {
        for (int block_x=0; block_x<s->total_blocks_width+1;) 
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
 * @param ppp_max_theoric Maximum number of pixels per pixel
 * @param compression_factor Compression factor number
 */
static void lhe_advanced_read_mesh (LheState *s, LheHuffEntry *he_mesh, float ppp_max_theoric, float compression_factor) 
{
    LheProcessing *procY, *procUV;    
    procY = &s->procY;
    procUV = &s->procUV;

    procY->num_hops = 0;
    procUV->num_hops = 0;

    lhe_advanced_read_perceptual_relevance_interval (s, he_mesh, procY->perceptual_relevance_x);
    
    lhe_advanced_read_perceptual_relevance_interval (s, he_mesh, procY->perceptual_relevance_y);
    
    
    for (int block_y=0; block_y<s->total_blocks_height; block_y++)
    {
        for (int block_x=0; block_x<s->total_blocks_width; block_x++)
        {
            lhe_calculate_block_coordinates (procY, procUV, s->total_blocks_width, s->total_blocks_height,
                                             block_x, block_y);

            lhe_advanced_perceptual_relevance_to_ppp(procY, procUV, compression_factor, ppp_max_theoric, block_x, block_y);
            
            //Adjusts luminance ppp to rectangle shape 
            procY->num_hops += lhe_advanced_ppp_side_to_rectangle_shape (procY, ppp_max_theoric, block_x, block_y);  
            //Adjusts chrominance ppp to rectangle shape
            procUV->num_hops += lhe_advanced_ppp_side_to_rectangle_shape (procUV, ppp_max_theoric, block_x, block_y);   
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
 * @param *proc LHE processing parameters
 * @param *lhe LHE image arrays
 * @param linesize rectangle images create a square image in ffmpeg memory. Linesize is width used by ffmpeg in memory
 * @param total_blocks_width number of blocks widthwise
 * @param block_x block x index
 * @param block_y block y index
 */
static void lhe_basic_decode_one_hop_per_pixel_block (LheBasicPrec *prec, LheProcessing *proc, LheImage *lhe, int linesize,
                                                      uint32_t total_blocks_width, int block_x, int block_y) 
{
       
    //Hops computation.
    int xini, xfin, yini, yfin;
    bool small_hop, last_small_hop;
    uint8_t hop, predicted_luminance, hop_1, r_max; 
    int pix, pix_original_data,dif_pix, dif_hops, num_block;
    
    num_block = block_y * total_blocks_width + block_x;
    
    //ORIGINAL IMAGE
    xini = proc->basic_block[block_y][block_x].x_ini;
    xfin = proc->basic_block[block_y][block_x].x_fin;  
    yini = proc->basic_block[block_y][block_x].y_ini;
    yfin = proc->basic_block[block_y][block_x].y_fin;
        
    small_hop           = false;
    last_small_hop      = false;        // indicates if last hop is small
    predicted_luminance = 0;            // predicted signal
    hop_1               = START_HOP_1;
    pix                 = 0;            // pixel possition, from 0 to image size        
    r_max               = PARAM_R;        
    
 
    pix = yini*linesize + xini; 
    pix_original_data = yini * proc->width + xini;
    dif_pix = linesize - xfin + xini;
    dif_hops = proc->width - xfin + xini;
    
    for (int y=yini; y < yfin; y++)  {
        for (int x=xini; x < xfin; x++)     {
            
            hop = lhe->hops[pix_original_data];
  
            if (x == xini && y==yini) 
            {
                predicted_luminance=lhe->first_color_block[num_block];//first pixel always is perfectly predicted! :-)  
            } 
            else if (y == yini) 
            {
                predicted_luminance=lhe->component_prediction[pix-1];
            } 
            else if (x == xini) 
            {
                predicted_luminance=lhe->component_prediction[pix-linesize];
                last_small_hop=false;
                hop_1=START_HOP_1;
            } else if (x == xfin -1) 
            {
                predicted_luminance=(lhe->component_prediction[pix-1]+lhe->component_prediction[pix-linesize])>>1;                                                             
            } 
            else 
            {
                predicted_luminance=(lhe->component_prediction[pix-1]+lhe->component_prediction[pix+1-linesize])>>1;     
            }
            
          
            //assignment of component_prediction
            //This is the uncompressed image
            lhe->component_prediction[pix]= prec -> prec_luminance[predicted_luminance][r_max][hop_1][hop];
            
            //tunning hop1 for the next hop ( "h1 adaptation")
            //------------------------------------------------
            H1_ADAPTATION;

            //lets go for the next pixel
            //--------------------------
            pix++;
            pix_original_data++;
        }// for x
        pix+=dif_pix;
        pix_original_data+=dif_hops;
    }// for y
    
}

/**
 * Decodes one hop per pixel
 * 
 * @param *prec precalculated lhe data
 * @param *proc LHE processing parameters
 * @param *lhe LHE image arrays final result
 * @param linesize rectangle images create a square image in ffmpeg memory. Linesize is width used by ffmpeg in memory
 */
static void lhe_basic_decode_one_hop_per_pixel (LheBasicPrec *prec, LheProcessing *proc, LheImage *lhe, int linesize) {
       
    //Hops computation.
    bool small_hop, last_small_hop;
    uint8_t hop, predicted_luminance, hop_1, r_max; 
    int pix, pix_original_data, dif_pix;
    
    small_hop           = false;
    last_small_hop      = false;        // indicates if last hop is small
    predicted_luminance = 0;            // predicted signal
    hop_1               = START_HOP_1;
    pix                 = 0;            // pixel possition, from 0 to image size       
    pix_original_data   = 0;
    r_max               = PARAM_R;        
    
    dif_pix = linesize - proc->width;
 
    for (int y=0; y < proc->height; y++)  {
        for (int x=0; x < proc->width; x++)     {
            
            hop = lhe->hops[pix_original_data]; 
            
            if (x==0 && y==0)
            {
                predicted_luminance=lhe->first_color_block[0];//first pixel always is perfectly predicted! :-)  
            }
            else if (y == 0)
            {
                predicted_luminance=lhe->component_prediction[pix-1];            
            }
            else if (x == 0)
            {
                predicted_luminance=lhe->component_prediction[pix-linesize];
                last_small_hop=false;
                hop_1=START_HOP_1;
            } 
            else if (x == proc->width -1)
            {
                predicted_luminance=(lhe->component_prediction[pix-1]+lhe->component_prediction[pix-linesize])>>1;                                                       
            }
            else 
            {
                predicted_luminance=(lhe->component_prediction[pix-1]+lhe->component_prediction[pix+1-linesize])>>1;     
            }
    
            //assignment of component_prediction
            lhe->component_prediction[pix]= prec -> prec_luminance[predicted_luminance][r_max][hop_1][hop];
            
            //tunning hop1 for the next hop ( "h1 adaptation")
            //------------------------------------------------
            H1_ADAPTATION;
            //lets go for the next pixel
            //--------------------------
            pix++;
            pix_original_data++;
        }// for x
        pix+=dif_pix;
    }// for y
}

/**
 * Calls methods to decode sequentially
 */
static void lhe_basic_decode_frame_sequential (LheState *s) 
{
    AVFrame *frame;
    LheBasicPrec *prec;
    LheProcessing *procY, *procUV;
    LheImage *lheY, *lheU, *lheV;
    
    frame = s->frame;
    prec = &s->prec;
    procY = &s->procY;
    procUV = &s->procUV;
    lheY = &s->lheY;
    lheU = &s->lheU;
    lheV = &s->lheV;
    
    //Luminance
    lhe_basic_decode_one_hop_per_pixel(prec, procY, lheY, frame->linesize[0]);

    //Chrominance U
    lhe_basic_decode_one_hop_per_pixel(prec, procUV, lheU, frame->linesize[1]);

    //Chrominance V
    lhe_basic_decode_one_hop_per_pixel(prec, procUV, lheV, frame->linesize[2]);
}

/**
 * Calls methods to decode pararell
 */
static void lhe_basic_decode_frame_pararell (LheState *s) 
{
    AVFrame *frame;
    LheBasicPrec *prec;
    LheProcessing *procY, *procUV;
    LheImage *lheY, *lheU, *lheV;
    
    frame = s->frame;
    prec = &s->prec;
    procY = &s->procY;
    procUV = &s->procUV;
    lheY = &s->lheY;
    lheU = &s->lheU;
    lheV = &s->lheV;
    
    #pragma omp parallel for
    for (int block_y=0; block_y<s->total_blocks_height; block_y++)      
    {  
        for (int block_x=0; block_x<s->total_blocks_width; block_x++) 
        {
            lhe_calculate_block_coordinates (&s->procY, &s->procUV,
                                             s->total_blocks_width, s->total_blocks_height,
                                             block_x, block_y);
            
            //Luminance
            lhe_basic_decode_one_hop_per_pixel_block(prec, procY,lheY, frame->linesize[0], s->total_blocks_width, block_x, block_y);
            //Chrominance U
            lhe_basic_decode_one_hop_per_pixel_block(prec, procUV, lheU, frame->linesize[1], s->total_blocks_width, block_x, block_y);
            //Chrominance V
            lhe_basic_decode_one_hop_per_pixel_block(prec, procUV, lheV, frame->linesize[2], s->total_blocks_width, block_x, block_y);
        }
    }
}

//==================================================================
// ADVANCED LHE DECODING
//==================================================================
/**
 * Decodes one hop per pixel in a block
 * 
 * @param *prec precalculated lhe data
 * @param *proc LHE processing parameters
 * @param *lhe LHE image arrays final result
 * @param total_blocks_width number of blocks widthwise
 * @param block_x block x index
 * @param block_y block y index
 */
static void lhe_advanced_decode_one_hop_per_pixel_block (LheBasicPrec *prec, LheProcessing *proc, LheImage *lhe,
                                                         uint32_t total_blocks_width, uint32_t block_x, uint32_t block_y) 
{
       
    //Hops computation.
    uint32_t xini, xfin_downsampled, yini, yfin_downsampled;
    bool small_hop, last_small_hop;
    uint8_t hop, predicted_luminance, hop_1, r_max; 
    uint32_t pix, dif_pix, num_block, dato;
    
    num_block = block_y * total_blocks_width + block_x;
    
    xini = proc->basic_block[block_y][block_x].x_ini;
    xfin_downsampled = proc->advanced_block[block_y][block_x].x_fin_downsampled; 
 
    yini = proc->basic_block[block_y][block_x].y_ini;
    yfin_downsampled = proc->advanced_block[block_y][block_x].y_fin_downsampled;
    
    small_hop           = false;
    last_small_hop      = false;        // indicates if last hop is small
    predicted_luminance = 0;            // predicted signal
    hop_1               = START_HOP_1;
    pix                 = 0;            // pixel possition, from 0 to image size        
    r_max               = PARAM_R;        
    
 
    pix = yini*proc->width + xini; 
    dif_pix = proc->width - xfin_downsampled + xini;
    
    for (int y=yini; y < yfin_downsampled; y++)  {
        for (int x=xini; x < xfin_downsampled; x++)     {
            
            hop = lhe->hops[pix];          
  
            if (x == xini && y==yini) 
            {
                predicted_luminance=lhe->first_color_block[num_block];//first pixel always is perfectly predicted! :-)  
            } 
            else if (y == yini) 
            {
                predicted_luminance=lhe->downsampled_image[pix-1];
            } 
            else if (x == xini) 
            {
                predicted_luminance=lhe->downsampled_image[pix-proc->width];
                last_small_hop=false;
                hop_1=START_HOP_1;
            } else if (x == xfin_downsampled -1) 
            {
                predicted_luminance=(lhe->downsampled_image[pix-1]+lhe->downsampled_image[pix-proc->width])>>1;                                                             
            } 
            else 
            {
                predicted_luminance=(lhe->downsampled_image[pix-1]+lhe->downsampled_image[pix+1-proc->width])>>1;     
            }
            
            if (hop == 4){
                dato = predicted_luminance;
            } else if (hop == 5) {
                dato = predicted_luminance + hop_1;
            } else if (hop == 3) {
                dato = predicted_luminance - hop_1;
            } else {
                if (hop > 5) {
                    dato = 255 - prec->cache_hops[255-predicted_luminance][hop_1-4][8 - hop];
                } else {
                    dato = prec->cache_hops[predicted_luminance][hop_1-4][hop];
                }
            }
          
            //assignment of component_prediction
            //This is the uncompressed image
            lhe->downsampled_image[pix]= dato;//prec -> prec_luminance[predicted_luminance][r_max][hop_1][hop];
            
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
 * 
 * @param *proc LHE processing parameters
 * @param *lhe LHE image arrays
 * @param *intermediate_interpolated_image intermediate interpolated image 
 * @param block_x block x index
 * @param block_y block y index
 */
static void lhe_advanced_vertical_nearest_neighbour_interpolation (LheProcessing *proc, LheImage *lhe,
                                                                   uint8_t *intermediate_interpolated_image, 
                                                                   int block_x, int block_y) 
{
    uint32_t block_width, downsampled_y_side;
    float gradient, gradient_0, gradient_1, ppp_y, ppp_0, ppp_1, ppp_2, ppp_3;
    uint32_t xini, xfin_downsampled, yini, yprev_interpolated, yfin_interpolated, yfin_downsampled;
    float yfin_interpolated_float;
    
    block_width = proc->basic_block[block_y][block_x].block_width;
    downsampled_y_side = proc->advanced_block[block_y][block_x].downsampled_y_side;
    xini = proc->basic_block[block_y][block_x].x_ini;
    xfin_downsampled = proc->advanced_block[block_y][block_x].x_fin_downsampled;
    yini = proc->basic_block[block_y][block_x].y_ini;
    yfin_downsampled = proc->advanced_block[block_y][block_x].y_fin_downsampled;

    ppp_0=proc->advanced_block[block_y][block_x].ppp_y[TOP_LEFT_CORNER];
    ppp_1=proc->advanced_block[block_y][block_x].ppp_y[TOP_RIGHT_CORNER];
    ppp_2=proc->advanced_block[block_y][block_x].ppp_y[BOT_LEFT_CORNER];
    ppp_3=proc->advanced_block[block_y][block_x].ppp_y[BOT_RIGHT_CORNER];
    
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
                    intermediate_interpolated_image[i*proc->width+x]=lhe->downsampled_image[y_sc*proc->width+x];                  
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
 * 
 * @param *proc LHE processing parameters
 * @param *lhe LHE image arrays
 * @param *intermediate_interpolated_image intermediate interpolated image in y coordinate
 * @param linesize rectangle images create a square image in ffmpeg memory. Linesize is width used by ffmpeg in memory
 * @param block_x block x index
 * @param block_y block y index
 */
static void lhe_advanced_horizontal_nearest_neighbour_interpolation (LheProcessing *proc, LheImage *lhe,
                                                                     uint8_t *intermediate_interpolated_image, 
                                                                     int linesize, int block_x, int block_y) 
{
    uint32_t block_height, downsampled_x_side;
    float gradient, gradient_0, gradient_1, ppp_x, ppp_0, ppp_1, ppp_2, ppp_3;
    uint32_t xini, xfin_downsampled, xprev_interpolated, xfin_interpolated, yini, yfin;
    float xfin_interpolated_float;
    
    block_height = proc->basic_block[block_y][block_x].block_height;
    downsampled_x_side = proc->advanced_block[block_y][block_x].downsampled_x_side;
    xini = proc->basic_block[block_y][block_x].x_ini;
    xfin_downsampled = proc->advanced_block[block_y][block_x].x_fin_downsampled;
    yini = proc->basic_block[block_y][block_x].y_ini;
    yfin =  proc->basic_block[block_y][block_x].y_fin;

    ppp_0=proc->advanced_block[block_y][block_x].ppp_x[TOP_LEFT_CORNER];
    ppp_1=proc->advanced_block[block_y][block_x].ppp_x[TOP_RIGHT_CORNER];
    ppp_2=proc->advanced_block[block_y][block_x].ppp_x[BOT_LEFT_CORNER];
    ppp_3=proc->advanced_block[block_y][block_x].ppp_x[BOT_RIGHT_CORNER];
        
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
                lhe->component_prediction[y*linesize+i]=intermediate_interpolated_image[y*proc->width+x_sc];       
                //PARA SACAR LA IMAGEN DOWNSAMPLEADA
                //lhe->component_prediction[y*linesize+i]=lhe->downsampled_image[y*proc->width+i];
            }
                        
            xprev_interpolated=xfin_interpolated;
            ppp_x+=gradient;
            xfin_interpolated_float+=ppp_x;   
        }//x

        ppp_0+=gradient_0;
        ppp_1+=gradient_1;

    }//y 
}





static void lhe_advanced_filter (LheProcessing *proc, LheImage *lhe, int linesize) 
{

    
    for (int y=1; y<proc->height-1; y++)
    {        

        for (int x=1; x<proc->width-1; x++)
        {         
               
                lhe->component_prediction[y*linesize+x]=(lhe->component_prediction[y*linesize+x] +
                                                         lhe->component_prediction[y*linesize+x+1] +
                                                         lhe->component_prediction[y*linesize+x-1] +
                                                         lhe->component_prediction[(y+1)*linesize+x] +
                                                         lhe->component_prediction[(y-1)*linesize+x])/5;
                //PARA SACAR LA IMAGEN DOWNSAMPLEADA
                //lhe->component_prediction[y*linesize+i]=lhe->downsampled_image[y*proc->width+i];
                         
        }//x

    }//y 
}








/**
 * Decodes symbols in advanced LHE file
 * 
 * @param *s LHE State
 * @param *he_Y LHE Huffman Entry for luminance
 * @param *he_UV LHE Huffman Entry for chrominances
 * @param image_size_Y luminance image size
 * @param image_size_UV chrominance image size
 */
static void lhe_advanced_decode_symbols (LheState *s, LheHuffEntry *he_Y, LheHuffEntry *he_UV,
                                         uint32_t image_size_Y, uint32_t image_size_UV) 
{
    if (lhe_advanced_decode_symbols_alloc_mem == false) {
        intermediate_interpolated_Y = av_calloc(image_size_Y, sizeof(uint8_t));
        intermediate_interpolated_U = av_calloc(image_size_UV, sizeof(uint8_t));
        intermediate_interpolated_V = av_calloc(image_size_UV, sizeof(uint8_t));
        lhe_advanced_decode_symbols_alloc_mem = true;
    }
    
    //#pragma omp parallel for
    for (int block_y=0; block_y<s->total_blocks_height; block_y++)
    {
        for (int block_x=0; block_x<s->total_blocks_width; block_x++)
        {             
            //Luminance
            lhe_advanced_decode_one_hop_per_pixel_block(&s->prec, &s->procY, &s->lheY, s->total_blocks_width, block_x, block_y); 

            
            lhe_advanced_vertical_nearest_neighbour_interpolation (&s->procY, &s->lheY, intermediate_interpolated_Y,
                                                                   block_x, block_y);     
                       
            lhe_advanced_horizontal_nearest_neighbour_interpolation (&s->procY, &s->lheY, intermediate_interpolated_Y, 
                                                                     s->frame->linesize[0], block_x, block_y);
            

            //Chrominance U
            lhe_advanced_decode_one_hop_per_pixel_block(&s->prec, &s->procUV, &s->lheU, s->total_blocks_width, block_x, block_y); 


            lhe_advanced_vertical_nearest_neighbour_interpolation (&s->procUV, &s->lheU, intermediate_interpolated_U,
                                                                   block_x, block_y);     
            
            lhe_advanced_horizontal_nearest_neighbour_interpolation (&s->procUV, &s->lheU, intermediate_interpolated_U, 
                                                                     s->frame->linesize[1], block_x, block_y);
                                                                        
            //Chrominance V 
            lhe_advanced_decode_one_hop_per_pixel_block(&s->prec, &s->procUV, &s->lheV, s->total_blocks_width, block_x, block_y); 


            
            lhe_advanced_vertical_nearest_neighbour_interpolation (&s->procUV, &s->lheV, intermediate_interpolated_V,
                                                                   block_x, block_y);     
            
            lhe_advanced_horizontal_nearest_neighbour_interpolation (&s->procUV, &s->lheV, intermediate_interpolated_V, 
                                                                     s->frame->linesize[2], block_x, block_y);    
            
        }
    }
    //lhe_advanced_filter (&s->procY, &s->lheY, s->frame->linesize[0]);
    //lhe_advanced_filter (&s->procUV, &s->lheU, s->frame->linesize[1]);
    //lhe_advanced_filter (&s->procUV, &s->lheV, s->frame->linesize[2]);
}

//==================================================================
// VIDEO LHE DECODING
//==================================================================
/**
 * Decodes one hop per pixel in a block
 * 
 * @param *prec Pointer to precalculated lhe data
 * @param *proc LHE processing parameters
 * @param *lhe LHE image arrays
 * @param *delta_prediction Quantized differential information
 * @param total_blocks_width number of blocks widthwise
 * @param block_x block x index
 * @param block_y block y index
 */
static void mlhe_decode_delta (LheBasicPrec *prec, LheProcessing *proc, LheImage *lhe,
                               uint8_t *delta_prediction, uint8_t *adapted_downsampled_image,
                               uint32_t total_blocks_width, uint32_t block_x, uint32_t block_y) 
{
       
    //Hops computation.
    uint32_t xini, xfin_downsampled, yini, yfin_downsampled;
    bool small_hop, last_small_hop;
    uint8_t hop, predicted_luminance, hop_1, r_max; 
    uint32_t pix, dif_pix, num_block;
    int delta, image;
/////////////////////////////////////////
    int tramo1, tramo2, signo;

    tramo1 = 52;
    tramo2 = 204;
////////////////////////////////////////////////////
    num_block = block_y * total_blocks_width + block_x;
    
    xini = proc->basic_block[block_y][block_x].x_ini;
    xfin_downsampled = proc->advanced_block[block_y][block_x].x_fin_downsampled; 
 
    yini = proc->basic_block[block_y][block_x].y_ini;
    yfin_downsampled = proc->advanced_block[block_y][block_x].y_fin_downsampled;
    
    small_hop           = false;
    last_small_hop      = false;        // indicates if last hop is small
    predicted_luminance = 0;            // predicted signal
    hop_1               = START_HOP_1;
    pix                 = 0;            // pixel possition, from 0 to image size        
    r_max               = PARAM_R;        
    
 
    pix = yini*proc->width + xini; 
    dif_pix = proc->width - xfin_downsampled + xini;
    
    for (int y=yini; y < yfin_downsampled; y++)  {
        for (int x=xini; x < xfin_downsampled; x++)     {
            
            hop = lhe->hops[pix];          
  
            if (x == xini && y==yini) 
            {
                predicted_luminance=lhe->first_color_block[num_block];//first pixel always is perfectly predicted! :-)  
            } 
            else if (y == yini) 
            {
                predicted_luminance=delta_prediction[pix-1];
            } 
            else if (x == xini) 
            {
                predicted_luminance=delta_prediction[pix-proc->width];
                last_small_hop=false;
                hop_1=START_HOP_1;
            } else if (x == xfin_downsampled -1) 
            {
                predicted_luminance=(delta_prediction[pix-1]+delta_prediction[pix-proc->width])>>1;                                                             
            } 
            else 
            {
                predicted_luminance=(delta_prediction[pix-1]+delta_prediction[pix+1-proc->width])>>1;     
            }
            
          
            //assignment of component_prediction
            //This is the uncompressed image
            //delta = prec -> prec_luminance[predicted_luminance][r_max][hop_1][hop];


            if (hop == 4){
                delta = predicted_luminance;
            } else if (hop == 5) {
                delta = predicted_luminance + hop_1;
            } else if (hop == 3) {
                delta = predicted_luminance - hop_1;
            } else {
                if (hop > 5) {
                    delta = 255 - prec->cache_hops[255-predicted_luminance][hop_1-4][8 - hop];
                } else {
                    delta = prec->cache_hops[predicted_luminance][hop_1-4][hop];
                }
            }

            //av_log(NULL, AV_LOG_INFO, "Valor de delta:  %d \n", delta);
            
            delta_prediction[pix] = delta;
            //if (delta >= 98 && delta <= 158) delta = 128;
         
            

            //delta = 128;

            //delta = (delta - 128) * 4;
            //image = adapted_downsampled_image[pix] + delta;



            delta = delta-128;
            signo = 0;
            if (delta < 0) {
                signo = 1;
                delta = -delta;
            }

            //if (delta >= tramo2) delta = tramo2-1;

            if (delta < tramo1){
                if (signo == 0) image = adapted_downsampled_image[pix] + delta;
                else image = adapted_downsampled_image[pix] - delta;
             } else  if (delta <= tramo1+(tramo2-tramo1)/2){
                delta = (delta - tramo1)*2;
                delta += tramo1;
                if (signo == 0) image = adapted_downsampled_image[pix] + delta;
                else image = adapted_downsampled_image[pix] - delta;
            } else {
                delta = (delta - (tramo2 - tramo1)/2 - tramo1)*4;
                delta += tramo2;
                if (signo == 0) image = adapted_downsampled_image[pix] + delta;
                else image = adapted_downsampled_image[pix] - delta;
            }
            
            if (image > 255) 
            {
                image = 255;
            }
            else if (image < 1) 
            {
                image = 1;
            }
            
            lhe->downsampled_image[pix] = image;
            
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
 * Decodes differential frame
 * 
 * @param *s LHE Context
 * @param *he_Y luminance Huffman data
 * @param *he_UV chrominance Huffman data
 * @param image_size_Y luminance image size
 * @param image_size_UV chrominance image size
 */
static void mlhe_decode_delta_frame (LheState *s, LheHuffEntry *he_Y, LheHuffEntry *he_UV, uint32_t image_size_Y, uint32_t image_size_UV) 
{
    
    if (mlhe_decode_delta_frame_alloc_mem == false) {
        delta_prediction_Y_dec = av_calloc (image_size_Y, sizeof(uint8_t));
        delta_prediction_U_dec = av_calloc (image_size_UV, sizeof(uint8_t));
        delta_prediction_V_dec = av_calloc (image_size_UV, sizeof(uint8_t));
        
        intermediate_interpolated_Y = av_calloc (image_size_Y, sizeof(uint8_t));
        intermediate_interpolated_U = av_calloc (image_size_UV, sizeof(uint8_t));
        intermediate_interpolated_V = av_calloc (image_size_UV, sizeof(uint8_t));
        
        intermediate_adapted_downsampled_data_Y_dec = av_calloc(image_size_Y, sizeof(uint8_t));  
        intermediate_adapted_downsampled_data_U_dec = av_calloc(image_size_UV, sizeof(uint8_t)); 
        intermediate_adapted_downsampled_data_V_dec = av_calloc(image_size_UV, sizeof(uint8_t)); 
        
        adapted_downsampled_image_Y = av_calloc(image_size_Y, sizeof(uint8_t));  
        adapted_downsampled_image_U = av_calloc(image_size_UV, sizeof(uint8_t)); 
        adapted_downsampled_image_V = av_calloc(image_size_UV, sizeof(uint8_t)); 
        mlhe_decode_delta_frame_alloc_mem = true;
    }
    
    #pragma omp parallel for
    for (int block_y=0; block_y<s->total_blocks_height; block_y++)
    {
        for (int block_x=0; block_x<s->total_blocks_width; block_x++)
        {                             
            //Luminance
            mlhe_adapt_downsampled_data_resolution2 (&s->procY, &s->lheY,
                                                    intermediate_adapted_downsampled_data_Y_dec, adapted_downsampled_image_Y,
                                                    block_x, block_y);
            
            mlhe_decode_delta (&s->prec, &s->procY, &s->lheY, delta_prediction_Y_dec, 
                               adapted_downsampled_image_Y, s->total_blocks_width, block_x, block_y);

            
            lhe_advanced_vertical_nearest_neighbour_interpolation (&s->procY, &s->lheY, intermediate_interpolated_Y, 
                                                                   block_x, block_y);
                      
            lhe_advanced_horizontal_nearest_neighbour_interpolation (&s->procY, &s->lheY, intermediate_interpolated_Y, 
                                                                     s->frame->linesize[0], block_x, block_y);

            //Chrominance U                    
            mlhe_adapt_downsampled_data_resolution2 (&s->procUV, &s->lheU,
                                                    intermediate_adapted_downsampled_data_U_dec, adapted_downsampled_image_U,
                                                    block_x, block_y);
            
            mlhe_decode_delta (&s->prec, &s->procUV, &s->lheU, delta_prediction_U_dec, 
                               adapted_downsampled_image_U, s->total_blocks_width, block_x, block_y);
            
            lhe_advanced_vertical_nearest_neighbour_interpolation (&s->procUV, &s->lheU, intermediate_interpolated_U, 
                                                                   block_x, block_y);         
            
            lhe_advanced_horizontal_nearest_neighbour_interpolation (&s->procUV, &s->lheU, intermediate_interpolated_U, 
                                                                     s->frame->linesize[1], block_x, block_y);
            //Chrominance V            
            mlhe_adapt_downsampled_data_resolution2 (&s->procUV, &s->lheV, 
                                                    intermediate_adapted_downsampled_data_V_dec, adapted_downsampled_image_V,
                                                    block_x, block_y);
            
            mlhe_decode_delta (&s->prec, &s->procUV, &s->lheV, delta_prediction_V_dec, 
                               adapted_downsampled_image_V, s->total_blocks_width, block_x, block_y);
             
            lhe_advanced_vertical_nearest_neighbour_interpolation (&s->procUV, &s->lheV, intermediate_interpolated_V, 
                                                                   block_x, block_y);
            
            
            lhe_advanced_horizontal_nearest_neighbour_interpolation (&s->procUV, &s->lheV, intermediate_interpolated_V, 
                                                                     s->frame->linesize[2], block_x, block_y);    
        }
    }     
    //lhe_advanced_filter (&s->procY, &s->lheY, s->frame->linesize[0]);
    //lhe_advanced_filter (&s->procUV, &s->lheU, s->frame->linesize[1]);
    //lhe_advanced_filter (&s->procUV, &s->lheV, s->frame->linesize[2]);
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

/**
 * Free pointers
 * 
 * @param *s Pointers
 * @param total_blocks_width 
 * @param total_blocks_height 
 */
static void lhe_basic_free(LheState *s, uint32_t total_blocks_width, uint32_t total_blocks_height) 
{
    av_free((&s->lheY)->hops);
    av_free((&s->lheU)->hops);
    av_free((&s->lheV)->hops);
    av_free((&s->lheY)->first_color_block);
    av_free((&s->lheU)->first_color_block);
    av_free((&s->lheV)->first_color_block);
        
    for (int i=0; i < total_blocks_height; i++)
    {
        av_free((&s->procY)->basic_block[i]);
    }
    
    av_free((&s->procY)->basic_block);

        
    for (int i=0; i < total_blocks_height; i++)
    {
        av_free((&s->procUV)->basic_block[i]);
    }
    
    av_free((&s->procUV)->basic_block);   
}

/**
 * Free pointers
 * 
 * @param *s Pointers
 * @param total_blocks_width 
 * @param total_blocks_height 
 */
static void lhe_advanced_free(LheState *s, uint32_t total_blocks_width, uint32_t total_blocks_height) 
{     
    lhe_basic_free(s, total_blocks_width, total_blocks_height);

    for (int i=0; i<total_blocks_height+1; i++) 
    {
        av_free((&s->procY)->perceptual_relevance_x[i]);
    }
    
    av_free((&s->procY)->perceptual_relevance_x);
    
    for (int i=0; i<total_blocks_height+1; i++) 
    {
        av_free((&s->procY)->perceptual_relevance_y[i]);
    }   
    
    av_free((&s->procY)->perceptual_relevance_y);

    //Advanced blocks    
    for (int i=0; i < total_blocks_height; i++)
    {
        av_free((&s->procY)->advanced_block[i]); 
    }
    
    av_free((&s->procY)->advanced_block); 

        
    for (int i=0; i < total_blocks_height; i++)
    {
        av_free((&s->procUV)->advanced_block[i]);
    }
    
    av_free((&s->procUV)->advanced_block); 
    
    av_free((&s->lheY)->downsampled_image);
    av_free((&s->lheU)->downsampled_image);
    av_free((&s->lheV)->downsampled_image);
}

/**
 * Read and decodes LHE image
 *
 * @param *avctx Codec context
 * @param *data data from file
 * @param *got_frame indicates frame is ready
 * @param *avpkt AV packet
 */ 
static int lhe_decode_frame(AVCodecContext *avctx, void *data, int *got_frame, AVPacket *avpkt)
{    
    uint32_t total_blocks, pixels_block, image_size_Y, image_size_UV;
    int ret;
    
    float compression_factor;
    uint32_t ppp_max_theoric;
    
    LheHuffEntry he_mesh[LHE_MAX_HUFF_SIZE_MESH];
    LheHuffEntry he_Y[LHE_MAX_HUFF_SIZE_SYMBOLS];
    LheHuffEntry he_UV[LHE_MAX_HUFF_SIZE_SYMBOLS];
   
    LheState *s = avctx->priv_data;
    
    const uint8_t *lhe_data = avpkt->data;
    
    init_get_bits(&s->gb, lhe_data, avpkt->size * 8);
    
    //LHE mode
    s->lhe_mode = get_bits(&s->gb, LHE_MODE_SIZE_BITS);
    
    //Pixel format byte, init pixel format
    s->pixel_format = get_bits(&s->gb, PIXEL_FMT_SIZE_BITS); 
    lhe_init_pixel_format (avctx, s, s->pixel_format);
           
    (&s->procY)->width  = get_bits_long(&s->gb, WIDTH_SIZE_BITS);
    (&s->procY)->height = get_bits_long(&s->gb, HEIGHT_SIZE_BITS);
    
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
    if (!(&s->lheY)->first_color_block) {
        (&s->lheY)->first_color_block = av_calloc(image_size_Y, sizeof(uint8_t));
        (&s->lheU)->first_color_block = av_calloc(image_size_UV, sizeof(uint8_t));
        (&s->lheV)->first_color_block = av_calloc(image_size_UV, sizeof(uint8_t));
    }
    
    for (int i=0; i<total_blocks; i++) 
    {
        (&s->lheY)->first_color_block[i] = get_bits(&s->gb, FIRST_COLOR_SIZE_BITS);
    }

    
    for (int i=0; i<total_blocks; i++) 
    {
        (&s->lheU)->first_color_block[i] = get_bits(&s->gb, FIRST_COLOR_SIZE_BITS);
    }
    
        
    for (int i=0; i<total_blocks; i++) 
    {
        (&s->lheV)->first_color_block[i] = get_bits(&s->gb, FIRST_COLOR_SIZE_BITS);
    }

    //Pointers to different color components
    (&s->lheY)->component_prediction = s->frame->data[0];
    (&s->lheU)->component_prediction  = s->frame->data[1];
    (&s->lheV)->component_prediction  = s->frame->data[2];
    if (!(&s->lheY)->hops) {
        (&s->lheY)->hops = av_calloc(image_size_Y , sizeof(uint8_t));
        (&s->lheU)->hops = av_calloc(image_size_UV , sizeof(uint8_t));
        (&s->lheV)->hops = av_calloc(image_size_UV , sizeof(uint8_t));
        
        (&s->procY)->basic_block = av_calloc(s->total_blocks_height, sizeof(BasicLheBlock *));
        
        for (int i=0; i < s->total_blocks_height; i++)
        {
            (&s->procY)->basic_block[i] = av_calloc (s->total_blocks_width, sizeof(BasicLheBlock));  
        }
        
        (&s->procUV)->basic_block = av_calloc (s->total_blocks_height, sizeof(BasicLheBlock *));
        
        for (int i=0; i < s->total_blocks_height; i++)
        {
            (&s->procUV)->basic_block[i] = av_calloc (s->total_blocks_width, sizeof(BasicLheBlock));
        }
    }
           
    //lhe_read_huffman_table(s, he_Y, LHE_MAX_HUFF_SIZE_SYMBOLS, LHE_HUFFMAN_NODE_BITS_SYMBOLS, LHE_HUFFMAN_NO_OCCURRENCES_SYMBOLS);
    //lhe_read_huffman_table(s, he_UV, LHE_MAX_HUFF_SIZE_SYMBOLS, LHE_HUFFMAN_NODE_BITS_SYMBOLS, LHE_HUFFMAN_NO_OCCURRENCES_SYMBOLS);
    
    if (s->lhe_mode == ADVANCED_LHE) /*ADVANCED LHE*/
    {
        if (!(&s->procY)->advanced_block) {
            (&s->procY)->perceptual_relevance_x = av_calloc((s->total_blocks_height+1), sizeof(float*));  
        
            for (int i=0; i<s->total_blocks_height+1; i++) 
            {
                (&s->procY)->perceptual_relevance_x[i] = av_calloc((s->total_blocks_width+1), sizeof(float));
            }
            
            (&s->procY)->perceptual_relevance_y = av_calloc((s->total_blocks_height+1), sizeof(float*)); 
            
            for (int i=0; i<s->total_blocks_height+1; i++) 
            {
                (&s->procY)->perceptual_relevance_y[i] = av_calloc((s->total_blocks_width+1), sizeof(float));
            }   
        
            //Advanced blocks
            (&s->procY)->advanced_block = av_calloc (s->total_blocks_height, sizeof(AdvancedLheBlock *));
            
            for (int i=0; i < s->total_blocks_height; i++)
            {
                (&s->procY)->advanced_block[i] = av_calloc (s->total_blocks_width, sizeof(AdvancedLheBlock));
            }
            
            (&s->procUV)->advanced_block = av_calloc (s->total_blocks_height, sizeof(AdvancedLheBlock *));
            
            for (int i=0; i < s->total_blocks_height; i++)
            {
                (&s->procUV)->advanced_block[i] = av_calloc (s->total_blocks_width, sizeof(AdvancedLheBlock));
            }
        }

        (&s->procY)-> theoretical_block_width = (&s->procY)->width / s->total_blocks_width;    
        (&s->procY)-> theoretical_block_height = (&s->procY)->height / s->total_blocks_height;   
        
        (&s->procUV)-> theoretical_block_width = (&s->procUV)->width / s->total_blocks_width;
        (&s->procUV)-> theoretical_block_height = (&s->procUV)->height / s->total_blocks_height; 
        
        if (!(&s->lheY)->downsampled_image) {
            (&s->lheY)->downsampled_image = av_calloc (image_size_Y, sizeof(uint8_t));
            (&s->lheU)->downsampled_image = av_calloc (image_size_UV, sizeof(uint8_t));
            (&s->lheV)->downsampled_image = av_calloc (image_size_UV, sizeof(uint8_t));
        }
        
        //MESH Huffman
        lhe_read_huffman_table(s, he_mesh, LHE_MAX_HUFF_SIZE_MESH, LHE_HUFFMAN_NODE_BITS_MESH, LHE_HUFFMAN_NO_OCCURRENCES_MESH);
        
        //Read quality level and calculate compression factor
        s->quality_level = get_bits(&s->gb, QL_SIZE_BITS); 
        ppp_max_theoric = (&s->procY)-> theoretical_block_width/SIDE_MIN;
        if (ppp_max_theoric > PPP_MAX) ppp_max_theoric = PPP_MAX;
        compression_factor = (&s->prec)->compression_factor[ppp_max_theoric][s->quality_level];        
       
        lhe_advanced_read_mesh(s, he_mesh, ppp_max_theoric, compression_factor) ; 

        lhe_advanced_read_all_file_symbols (s, he_Y, he_UV);
              
        lhe_advanced_decode_symbols (s, he_Y, he_UV, image_size_Y, image_size_UV);     
        
    }
    else /*BASIC LHE*/       
    {
        lhe_basic_read_file_symbols(s, he_Y, image_size_Y, (&s->lheY)->hops);     
        lhe_basic_read_file_symbols(s, he_UV, image_size_UV, (&s->lheU)->hops);      
        lhe_basic_read_file_symbols(s, he_UV, image_size_UV, (&s->lheV)->hops);      
 
        if (total_blocks > 1 && OPENMP_FLAGS == CONFIG_OPENMP) 
        {
            (&s->procY)->theoretical_block_width = (&s->procY)->width / s->total_blocks_width;
            (&s->procY)->theoretical_block_height = (&s->procY)->height / s->total_blocks_height;
            (&s->procUV)->theoretical_block_width = (&s->procUV)->width /s->total_blocks_width;
            (&s->procUV)->theoretical_block_height = (&s->procUV)->height /s->total_blocks_height;

            lhe_basic_decode_frame_pararell (s);                            
        } else 
        {     
            lhe_basic_decode_frame_sequential (s);    
        }
        
        //lhe_basic_free(s, s->total_blocks_width, s->total_blocks_height);
    }

    if ((ret = av_frame_ref(data, s->frame)) < 0)
        return ret;
    *got_frame = 1;

    return 0;
}

//==================================================================
// DECODE VIDEO FRAME
//==================================================================
/**
 * Read and decodes MLHE video
 *
 * @param *avctx Codec context
 * @param *data data from file
 * @param *got_frame indicates frame is ready
 * @param *avpkt AV packet
 */ 
static int mlhe_decode_video(AVCodecContext *avctx, void *data, int *got_frame, AVPacket *avpkt)
{    
    uint32_t total_blocks, pixels_block, image_size_Y, image_size_UV;
    int ret;
    
    float compression_factor;
    uint32_t ppp_max_theoric;
    
    LheHuffEntry he_mesh[LHE_MAX_HUFF_SIZE_MESH];
    LheHuffEntry he_Y[LHE_MAX_HUFF_SIZE_SYMBOLS];
    LheHuffEntry he_UV[LHE_MAX_HUFF_SIZE_SYMBOLS];
   
    LheState *s = avctx->priv_data;
    
    const uint8_t *lhe_data = avpkt->data;
    
    init_get_bits(&s->gb, lhe_data, avpkt->size * 8);
    
    s->lhe_mode = get_bits(&s->gb, LHE_MODE_SIZE_BITS); 
        
    if (s->lhe_mode==DELTA_MLHE && s->global_frames_count<=0) 
      return -1;
         
    if (s->lhe_mode == DELTA_MLHE) { /*DELTA VIDEO FRAME*/      
        
        image_size_Y = (&s->procY)->width * (&s->procY)->height;
        image_size_UV = (&s->procUV)->width * (&s->procUV)->height; 
        
        //Allocates frame
        av_frame_unref(s->frame);
        if ((ret = ff_get_buffer(avctx, s->frame, 0)) < 0)
            return ret;
        
        (&s->lheY)->component_prediction = s->frame->data[0];
        (&s->lheU)->component_prediction  = s->frame->data[1];
        (&s->lheV)->component_prediction  = s->frame->data[2];
    
        s->total_blocks_width = HORIZONTAL_BLOCKS;
        pixels_block = (&s->procY)->width / HORIZONTAL_BLOCKS;
        s->total_blocks_height = (&s->procY)->height / pixels_block;
      
        total_blocks = s->total_blocks_height * s->total_blocks_width;
        
        //First pixel array
        for (int i=0; i<total_blocks; i++) 
        {
            (&s->lheY)->first_color_block[i] = get_bits(&s->gb, FIRST_COLOR_SIZE_BITS);
        }

        
        for (int i=0; i<total_blocks; i++) 
        {
            (&s->lheU)->first_color_block[i] = get_bits(&s->gb, FIRST_COLOR_SIZE_BITS);
        }
        
            
        for (int i=0; i<total_blocks; i++) 
        {
            (&s->lheV)->first_color_block[i] = get_bits(&s->gb, FIRST_COLOR_SIZE_BITS); 
        }
        
        //lhe_read_huffman_table(s, he_Y, LHE_MAX_HUFF_SIZE_SYMBOLS, LHE_HUFFMAN_NODE_BITS_SYMBOLS, LHE_HUFFMAN_NO_OCCURRENCES_SYMBOLS);
        //lhe_read_huffman_table(s, he_UV, LHE_MAX_HUFF_SIZE_SYMBOLS, LHE_HUFFMAN_NODE_BITS_SYMBOLS, LHE_HUFFMAN_NO_OCCURRENCES_SYMBOLS);
        
         //MESH Huffman
        lhe_read_huffman_table(s, he_mesh, LHE_MAX_HUFF_SIZE_MESH, LHE_HUFFMAN_NODE_BITS_MESH, LHE_HUFFMAN_NO_OCCURRENCES_MESH);     
        
        //Calculate compression factor
        ppp_max_theoric = (&s->procY)->theoretical_block_width/SIDE_MIN;
        if (ppp_max_theoric > PPP_MAX) ppp_max_theoric = PPP_MAX;
        compression_factor = (&s->prec)->compression_factor[ppp_max_theoric][s->quality_level];        
       
        lhe_advanced_read_mesh(s, he_mesh, ppp_max_theoric, compression_factor) ; 
        
        lhe_advanced_read_all_file_symbols (s, he_Y, he_UV);
        
        mlhe_decode_delta_frame (s, he_Y, he_UV, image_size_Y, image_size_UV);
    } 
    else if (s->lhe_mode == ADVANCED_LHE)
    {   
        s->global_frames_count++;
        
        //Pixel format byte, init pixel format
        s->pixel_format = get_bits(&s->gb, PIXEL_FMT_SIZE_BITS);
        lhe_init_pixel_format (avctx, s, s->pixel_format);
            
        (&s->procY)->width  = get_bits_long(&s->gb, WIDTH_SIZE_BITS);
        (&s->procY)->height = get_bits_long(&s->gb, HEIGHT_SIZE_BITS);
        
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
        
        //Pointers to different color components
        (&s->lheY)->component_prediction = s->frame->data[0];
        (&s->lheU)->component_prediction  = s->frame->data[1];
        (&s->lheV)->component_prediction  = s->frame->data[2];

        s->total_blocks_width = HORIZONTAL_BLOCKS;
        pixels_block = (&s->procY)->width / HORIZONTAL_BLOCKS;
        s->total_blocks_height = (&s->procY)->height / pixels_block;
        
        total_blocks = s->total_blocks_height * s->total_blocks_width;
        
        //First pixel array
        if (!(&s->lheY)->first_color_block) {
            (&s->lheY)->first_color_block = av_calloc(image_size_Y, sizeof(uint8_t));
            (&s->lheU)->first_color_block = av_calloc(image_size_UV, sizeof(uint8_t));
            (&s->lheV)->first_color_block = av_calloc(image_size_UV, sizeof(uint8_t));
        }
        
        for (int i=0; i<total_blocks; i++) 
        {
            (&s->lheY)->first_color_block[i] = get_bits(&s->gb, FIRST_COLOR_SIZE_BITS);
        }

        
        for (int i=0; i<total_blocks; i++) 
        {
            (&s->lheU)->first_color_block[i] = get_bits(&s->gb, FIRST_COLOR_SIZE_BITS);
        }
        
            
        for (int i=0; i<total_blocks; i++) 
        {
            (&s->lheV)->first_color_block[i] = get_bits(&s->gb, FIRST_COLOR_SIZE_BITS); 
        }
        
        if (!(&s->lheY)->hops) {
            (&s->lheY)->hops = av_calloc(image_size_Y, sizeof(uint8_t));
            (&s->lheU)->hops = av_calloc(image_size_UV, sizeof(uint8_t));
            (&s->lheV)->hops = av_calloc(image_size_UV, sizeof(uint8_t)); 
        
            (&s->procY)->basic_block = av_calloc(s->total_blocks_height, sizeof(BasicLheBlock *));
            
            for (int i=0; i < s->total_blocks_height; i++)
            {
                (&s->procY)->basic_block[i] = av_calloc (s->total_blocks_width, sizeof(BasicLheBlock));
            }
            
            (&s->procUV)->basic_block = av_calloc(s->total_blocks_height, sizeof(BasicLheBlock *));
            
            for (int i=0; i < s->total_blocks_height; i++)
            {
                (&s->procUV)->basic_block[i] = av_calloc (s->total_blocks_width, sizeof(BasicLheBlock));
            }
        }
            
        //lhe_read_huffman_table(s, he_Y, LHE_MAX_HUFF_SIZE_SYMBOLS, LHE_HUFFMAN_NODE_BITS_SYMBOLS, LHE_HUFFMAN_NO_OCCURRENCES_SYMBOLS);
        //lhe_read_huffman_table(s, he_UV, LHE_MAX_HUFF_SIZE_SYMBOLS, LHE_HUFFMAN_NODE_BITS_SYMBOLS, LHE_HUFFMAN_NO_OCCURRENCES_SYMBOLS);
        
        if (!(&s->procY)->advanced_block) {
            (&s->procY)->perceptual_relevance_x = av_calloc(s->total_blocks_height+1, sizeof(float*));  
            
            for (int i=0; i<s->total_blocks_height+1; i++) 
            {
                (&s->procY)->perceptual_relevance_x[i] = av_calloc(s->total_blocks_width+1, sizeof(float));
            }
            
            (&s->procY)->perceptual_relevance_y = av_calloc(s->total_blocks_height+1, sizeof(float*)); 
            
            for (int i=0; i<s->total_blocks_height+1; i++) 
            {
                (&s->procY)->perceptual_relevance_y[i] = av_calloc(s->total_blocks_width+1, sizeof(float));
            }   

            (&s->procY)->advanced_block = av_calloc(s->total_blocks_height, sizeof(AdvancedLheBlock *));
            
            for (int i=0; i < s->total_blocks_height; i++)
            {
                (&s->procY)->advanced_block[i] = av_calloc (s->total_blocks_width, sizeof(AdvancedLheBlock));
            }
            
            (&s->procUV)->advanced_block = av_calloc(s->total_blocks_height, sizeof(AdvancedLheBlock *));
            
            for (int i=0; i < s->total_blocks_height; i++)
            {
                (&s->procUV)->advanced_block[i] = av_calloc (s->total_blocks_width, sizeof(AdvancedLheBlock));
            }
        }
        
        (&s->procY)-> theoretical_block_width = (&s->procY)->width / s->total_blocks_width;    
        (&s->procY)-> theoretical_block_height = (&s->procY)->height / s->total_blocks_height;   
        
        (&s->procUV)-> theoretical_block_width = (&s->procUV)->width / s->total_blocks_width;
        (&s->procUV)-> theoretical_block_height = (&s->procUV)->height / s->total_blocks_height; 
        
        if (!(&s->lheY)->downsampled_image) {
            (&s->lheY)-> downsampled_image = av_calloc (image_size_Y, sizeof(uint8_t));
            (&s->lheU)-> downsampled_image = av_calloc (image_size_UV, sizeof(uint8_t));
            (&s->lheV)-> downsampled_image = av_calloc (image_size_UV, sizeof(uint8_t));
        }
        
        //MESH Huffman
        lhe_read_huffman_table(s, he_mesh, LHE_MAX_HUFF_SIZE_MESH, LHE_HUFFMAN_NODE_BITS_MESH, LHE_HUFFMAN_NO_OCCURRENCES_MESH);
        
        //Read quality level and calculate compression factor
        s->quality_level = get_bits(&s->gb, QL_SIZE_BITS); 
        ppp_max_theoric = (&s->procY)-> theoretical_block_width/SIDE_MIN;
        if (ppp_max_theoric > PPP_MAX) ppp_max_theoric = PPP_MAX;
        compression_factor = (&s->prec)->compression_factor[ppp_max_theoric][s->quality_level];        
        
        lhe_advanced_read_mesh(s, he_mesh, ppp_max_theoric, compression_factor) ; 
        
        lhe_advanced_read_all_file_symbols (s, he_Y, he_UV);
                
        lhe_advanced_decode_symbols (s, he_Y, he_UV, image_size_Y, image_size_UV);
    }   

    if (!(&s->procY)->last_advanced_block) 
    {
         (&s->procY)->last_advanced_block = av_calloc(s->total_blocks_height, sizeof(AdvancedLheBlock *));
        
        for (int i=0; i < s->total_blocks_height; i++)
        {
            (&s->procY)->last_advanced_block[i] = av_calloc (s->total_blocks_width, sizeof(AdvancedLheBlock));
        }      
    }
    
    if (!(&s->procUV)->last_advanced_block) {
        (&s->procUV)->last_advanced_block = av_calloc(s->total_blocks_height, sizeof(AdvancedLheBlock *));
        
        for (int i=0; i < s->total_blocks_height; i++)
        {
            (&s->procUV)->last_advanced_block[i] = av_calloc (s->total_blocks_width, sizeof(AdvancedLheBlock));
        }
    }
    
    if (!(&s->lheY)->last_downsampled_image) {
        (&s->lheY)->last_downsampled_image = av_calloc(image_size_Y, sizeof(uint8_t));  
    }
    

    if (!(&s->lheU)->last_downsampled_image) {
        (&s->lheU)->last_downsampled_image = av_calloc(image_size_UV, sizeof(uint8_t)); 
    }
    
    
    if (!(&s->lheV)->last_downsampled_image) {
        (&s->lheV)->last_downsampled_image = av_calloc(image_size_UV, sizeof(uint8_t));  
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
/*
    av_freep(&s->prec.prec_luminance);
    av_freep(&s->prec.best_hop);
    av_free(intermediate_interpolated_Y);
    av_free(intermediate_interpolated_U);
    av_free(intermediate_interpolated_V);
    av_free(delta_prediction_Y_dec); 
    av_free(delta_prediction_U_dec); 
    av_free(delta_prediction_V_dec);
    av_free(intermediate_adapted_downsampled_data_Y_dec); 
    av_free(intermediate_adapted_downsampled_data_U_dec); 
    av_free(intermediate_adapted_downsampled_data_V_dec);  
    
    av_free(adapted_downsampled_image_Y); 
    av_free(adapted_downsampled_image_U); 
    av_free(adapted_downsampled_image_V);  
    lhe_advanced_free(s, s->total_blocks_width, s->total_blocks_height);
    av_frame_free(&s->frame);
    lhe_advanced_decode_symbols_alloc_mem = false;
    mlhe_decode_delta_frame_alloc_mem = false;
*/
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
