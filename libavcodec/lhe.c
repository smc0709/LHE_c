#include "lhe.h"

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
    
    
//==================================================================
// AUXILIAR FUNCTIONS
//==================================================================

/**
 * Count bits function
 */
int count_bits (int num) {
    int contador=1;
 
    while(num/10>0)
    {
        num=num/10;
        contador++;
    }

    return contador;
}

/**
 * Tme diff function
 */
double time_diff(struct timeval x , struct timeval y)
{ 
    double timediff = 1000000*(y.tv_sec - x.tv_sec) + (y.tv_usec - x.tv_usec); /* in microseconds */
     
    return timediff;
}

//==================================================================
// HUFFMAN FUNCTIONS
//==================================================================

/* 
 * Compare huffentry lengths
 */
static int huff_cmp_len(const void *a, const void *b)
{
    const LheHuffEntry *aa = a, *bb = b;
    return (aa->len - bb->len)*LHE_MAX_HUFF_SIZE + aa->sym - bb->sym;
}

/* 
 * Compare huffentry symbols 
 */
static int huff_cmp_sym(const void *a, const void *b)
{
    const LheHuffEntry *aa = a, *bb = b;
    return aa->sym - bb->sym;
}

/**
 * Generates Huffman codes using Huffman tree
 * 
 * @param *he Huffman parameters (Huffman tree)
 */
int lhe_generate_huffman_codes(LheHuffEntry *he)
{
    int len, i, last;
    uint16_t code;
    uint64_t bits;
    
    bits=0;
    code = 1;
    last = LHE_MAX_HUFF_SIZE-1;

    //Sorts Huffman table from less occurrence symbols to greater occurrence symbols
    qsort(he, LHE_MAX_HUFF_SIZE, sizeof(*he), huff_cmp_len);  

    //Deletes symbols with no occurrence in model
    while (he[last].len == 255 && last)
        last--;
    
    //Initializes code to 11...11, the length of code depends on number of symbols with occurrence in model
    //For example, if there are 6 different symbols in model (some symbols may not appear), code = 111111
    for (i=0; i<he[last].len; i++)
    {
        code|= 1 << i;
    }      
    
    //From maximum length to 0
    for (len= he[last].len; len > 0; len--) {
        //From 0 to LHE_MAX_HUFF_SIZE (all Huffman codes)
        for (i = 0; i <= last; i++) {
            //Checks if lengths are the same
            if (he[i].len == len)
            {
                //Assigns code
                he[i].code = code;
                //Substracts 1 to code. If another symbol has the same length, codes will be different
                //For example, 1111 (15), 1110 (14), 1101 (13)... all 4 bits length
                code--;
            }          
        }
        
        //Moves 1 bit to the right (Division by two)
        //For example 1111(15) - 111(7) ... 110(6)-11(3)
        code >>= 1;
    }

    //Sorts symbols from 0 to LHE_MAX_HUFF_SIZE
    qsort(he, LHE_MAX_HUFF_SIZE, sizeof(*he), huff_cmp_sym);
    
    //Calculates number of bits
    for (i=0; i<LHE_MAX_HUFF_SIZE; i++) 
    {
        bits += (he[i].len * he[i].count); //bits number is symbol occurrence * symbol bits
    }
    
    return bits;

}


//==================================================================
// ADVANCED LHE
// Common functions encoder and decoder 
//==================================================================

/**
 * Calculates init x, init y, final x, final y for each block of luminance and chrominance signals.
 * 
 * @param block_array_Y Block parameters for luminance
 * @param block_array_UV Block parameters for chrominances
 * @param block_width_Y Block width for luminance
 * @param block_height_Y Block height for luminance
 * @param block_width_UV Block width for chrominances
 * @param block_height_UV Block height for chrominances
 * @param width_image_Y Image width for luminance
 * @param height_image_Y Image height for luminance
 * @param width_image_UV Image width for chrominances
 * @param height_image_UV Image height for chrominances 
 * @param block_x block x index
 * @param block_y block y index
 */
void calculate_block_coordinates (AdvancedLheBlock **block_array_Y, AdvancedLheBlock **block_array_UV,
                                  uint32_t block_width_Y, uint32_t block_height_Y,                             
                                  uint32_t block_width_UV, uint32_t block_height_UV, 
                                  uint32_t width_image_Y, uint32_t height_image_Y,
                                  uint32_t width_image_UV, uint32_t height_image_UV,
                                  int block_x, int block_y)
{
    uint32_t xini_Y, xfin_Y, yini_Y, yfin_Y;
    uint32_t xini_UV, xfin_UV, yini_UV, yfin_UV;

    //LUMINANCE
    xini_Y = block_x * block_width_Y;
    xfin_Y = xini_Y + block_width_Y;
    if (xfin_Y > width_image_Y) {
        xfin_Y = width_image_Y;
    }
    
    yini_Y = block_y * block_height_Y;
    yfin_Y = yini_Y + block_height_Y ;
    
    if (yfin_Y > height_image_Y)
    {
        yfin_Y = height_image_Y;
    }
    
    //CHROMINANCE U
    xini_UV = block_x * block_width_UV;
    xfin_UV = xini_UV + block_width_UV;
    if (xfin_UV > width_image_UV) {
        xfin_UV = width_image_UV;
    }
    
    yini_UV = block_y * block_height_UV;
    yfin_UV = yini_UV + block_height_UV ;
    
    if (yfin_UV > height_image_UV)
    {
        yfin_UV = height_image_UV;
    }

    block_array_Y[block_y][block_x].x_ini = xini_Y;
    block_array_Y[block_y][block_x].x_fin = xfin_Y;
    block_array_Y[block_y][block_x].y_ini = yini_Y;
    block_array_Y[block_y][block_x].y_fin = yfin_Y;
    
    block_array_UV[block_y][block_x].x_ini = xini_UV;
    block_array_UV[block_y][block_x].x_fin = xfin_UV;
    block_array_UV[block_y][block_x].y_ini = yini_UV;
    block_array_UV[block_y][block_x].y_fin = yfin_UV;
}

/**
 * Transforms perceptual relevance to pixels per pixels
 * 
 * @param ***ppp_x pixels per pixel in x coordinate
 * @param ***ppp_y pixels per pixel in y coordinate
 * @param **perceptual_relevance_x perceptual relevance in x coordinate
 * @param **perceptual_relevance_y perceptual relevance in y coordinate
 * @param compression_factor compression factor 
 * @param ppp_max_theoric maximum number of pixels per pixel
 * @param block_x block x index
 * @param block_y block y index
 */
float lhe_advanced_perceptual_relevance_to_ppp (float *** ppp_x, float *** ppp_y, 
                                                float ** perceptual_relevance_x, float ** perceptual_relevance_y,
                                                float compression_factor,
                                                uint32_t ppp_max_theoric,
                                                int block_x, int block_y) 
{
    float const1, const2, ppp_min, ppp_max;

    ppp_min = PPP_MIN;
    const1 = ppp_max_theoric - 1;
    const2 = ppp_max_theoric * compression_factor;
    
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
    
    return ppp_max;
}


/**
 * This function transform PPP values at corners in order to generate a rectangle when 
 * the block is downsampled.
 * 
 * However, this function does not assure that the block takes a rectangular shape when image is interpolated.
 * A rectangular downsampled block, after interpolation, generates a poligonal shape (not parallelepiped)
 * 
 *                                                                   
 *         original                down             interpolated 
 *          side_c              
 *     0  +-------+  1            +----+                    +
 *        |       |         ----> |    |   ---->     +             
 * side a |       | side b        +----+                                    
 *        |       |             rectangle                 +             
 *     2  +-------+  3                             +  
 *          side d                                  any shape
 * 
 * @param **array_block_Y Block parameters for luminance
 * @param block_array_UV Block parameters for chrominances
 * @param ***ppp_x pixels per pixel in x coordinate
 * @param ***ppp_y Pixels per pixel in y coordinate
 * @param width_image_Y Image width for luminance
 * @param height_image_Y Image height for luminance
 * @param width_image_UV Image width for chrominances 
 * @param height_image_UV Image height for chrominances
 * @param block_length Block length
 * @param ppp_max Maximum number of pixels per pixel
 * @param block_x Block x index
 * @param block_y Block y index                                                        
 */
void lhe_advanced_ppp_side_to_rectangle_shape (AdvancedLheBlock **array_block_Y, AdvancedLheBlock **array_block_UV,
                                               float ***ppp_x, float ***ppp_y,
                                               uint32_t width_image_Y, uint32_t height_image_Y, 
                                               uint32_t width_image_UV, uint32_t height_image_UV,
                                               uint32_t block_length, float ppp_max, 
                                               int block_x, int block_y) 
{
    float ppp_x_0, ppp_x_1, ppp_x_2, ppp_x_3, ppp_y_0, ppp_y_1, ppp_y_2, ppp_y_3, side_a, side_b, side_c, side_d, side_average, side_min, side_max, add;
    
    uint32_t downsampled_block_Y, downsampled_block_UV;
    uint32_t x_fin_downsampled_Y, x_fin_downsampled_UV, y_fin_downsampled_Y, y_fin_downsampled_UV;
    
    //HORIZONTAL ADJUSTMENT
    ppp_x_0 = ppp_x[block_y][block_x][TOP_LEFT_CORNER];
    ppp_x_1 = ppp_x[block_y][block_x][TOP_RIGHT_CORNER];
    ppp_x_2 = ppp_x[block_y][block_x][BOT_LEFT_CORNER];
    ppp_x_3 = ppp_x[block_y][block_x][BOT_RIGHT_CORNER];
    
    side_c = ppp_x_0 + ppp_x_1;
    side_d = ppp_x_2 + ppp_x_3;
    
    side_average = side_c;
    
    if (side_c != side_d) 
    {
        
        if (side_c < side_d) 
        {
            side_min = side_d; //side_min is the side whose ppp summation is bigger 
            side_max = side_c; //side max is the side whose resolution is bigger and ppp summation is lower
        } 
        else 
        {
            side_min = side_c;
            side_max = side_d;
        }
        
        side_average=side_max;
    }
    
    downsampled_block_Y = 2.0*block_length/ side_average + 0.5;
    downsampled_block_UV = (downsampled_block_Y - 1) / CHROMA_FACTOR_WIDTH + 1;
    
    array_block_Y[block_y][block_x].downsampled_x_side = downsampled_block_Y;
    
    x_fin_downsampled_Y = array_block_Y[block_y][block_x].x_ini + downsampled_block_Y;
    if (x_fin_downsampled_Y > width_image_Y) 
    {
        x_fin_downsampled_Y = width_image_Y;
    }
    array_block_Y[block_y][block_x].x_fin_downsampled = x_fin_downsampled_Y;

    array_block_UV[block_y][block_x].downsampled_x_side = downsampled_block_UV;
    
    x_fin_downsampled_UV = array_block_UV[block_y][block_x].x_ini + downsampled_block_UV;
    if (x_fin_downsampled_UV > width_image_UV) 
    {
        x_fin_downsampled_UV = width_image_UV;
    }
    array_block_UV[block_y][block_x].x_fin_downsampled = x_fin_downsampled_UV;
    
    side_average=2.0*block_length/downsampled_block_Y;
       
    //adjust side c
    //--------------
    if (ppp_x_0<=ppp_x_1)
    {       
        ppp_x_0=side_average*ppp_x_0/side_c;

        if (ppp_x_0<PPP_MIN) 
        {
            ppp_x_0=PPP_MIN;
            
        }//PPPmin is 1 a PPP value <1 is not possible

        add = 0;
        ppp_x_1=side_average-ppp_x_0;
        if (ppp_x_1>ppp_max) 
        {
            add=ppp_x_1-ppp_max; 
            ppp_x_1=ppp_max;       
        }

        ppp_x_0+=add;
    }
    else
    {
        ppp_x_1=side_average*ppp_x_1/side_c;

        if (ppp_x_1<PPP_MIN) 
        { 
            ppp_x_1=PPP_MIN;    
        }//PPPmin is 1 a PPP value <1 is not possible
        
        add=0;
        ppp_x_0=side_average-ppp_x_1;
        if (ppp_x_0>ppp_max) 
        {
            add=ppp_x_0-ppp_max; 
            ppp_x_0=ppp_max;          
        }

        ppp_x_1+=add;

    }

    //adjust side d
    //--------------
    if (ppp_x_2<=ppp_x_3)
    {       
        ppp_x_2=side_average*ppp_x_2/side_d;

        
        if (ppp_x_2<PPP_MIN) 
        {
            ppp_x_2=PPP_MIN;
            
        }// PPP can not be <PPP_MIN
        
        add=0;
        ppp_x_3=side_average-ppp_x_2;
        if (ppp_x_3>ppp_max) 
        {
            add=ppp_x_3-ppp_max; 
            ppp_x_3=ppp_max;
        }

        ppp_x_2+=add;
    }
    else
    {
        ppp_x_3=side_average*ppp_x_3/side_d;

        if (ppp_x_3<PPP_MIN) 
        {
            ppp_x_3=PPP_MIN;
            
        }

        add=0;
        ppp_x_2=side_average-ppp_x_3;
        if (ppp_x_2>ppp_max) 
        {
            add=ppp_x_2-ppp_max; 
            ppp_x_2=ppp_max;           
        }
        ppp_x_3+=add;

    }
    
    ppp_x[block_y][block_x][TOP_LEFT_CORNER] = ppp_x_0;
    ppp_x[block_y][block_x][TOP_RIGHT_CORNER] = ppp_x_1;
    ppp_x[block_y][block_x][BOT_LEFT_CORNER] = ppp_x_2;
    ppp_x[block_y][block_x][BOT_RIGHT_CORNER] = ppp_x_3;
    
    //VERTICAL ADJUSTMENT
    ppp_y_0 = ppp_y[block_y][block_x][TOP_LEFT_CORNER];
    ppp_y_1 = ppp_y[block_y][block_x][TOP_RIGHT_CORNER];
    ppp_y_2 = ppp_y[block_y][block_x][BOT_LEFT_CORNER];
    ppp_y_3 = ppp_y[block_y][block_x][BOT_RIGHT_CORNER];
    
    side_a = ppp_y_0 + ppp_y_2;
    side_b = ppp_y_1 + ppp_y_3;
    
    side_average = side_a;
    
    if (side_a != side_b) 
    {
        
        if (side_a < side_b) 
        {
            side_min = side_b; //side_min is the side whose ppp summation is bigger 
            side_max = side_a; //side max is the side whose resolution is bigger and ppp summation is lower
        } 
        else 
        {
            side_min = side_a;
            side_max = side_b;
        }
        
        side_average=side_max;
    }
    
    downsampled_block_Y = 2.0*block_length/ side_average + 0.5;    
    downsampled_block_UV = (downsampled_block_Y - 1) / CHROMA_FACTOR_HEIGHT + 1;
    
    array_block_Y[block_y][block_x].downsampled_y_side = downsampled_block_Y;
    y_fin_downsampled_Y = array_block_Y[block_y][block_x].y_ini + downsampled_block_Y;
    if (y_fin_downsampled_Y > height_image_Y)
    {
        y_fin_downsampled_Y = height_image_Y;
    }
    array_block_Y[block_y][block_x].y_fin_downsampled = y_fin_downsampled_Y;

    array_block_UV[block_y][block_x].downsampled_y_side = downsampled_block_UV;
    y_fin_downsampled_UV = array_block_UV[block_y][block_x].y_ini + downsampled_block_UV;
    if (y_fin_downsampled_UV > height_image_UV)
    {
        y_fin_downsampled_UV = height_image_UV;
    }
    array_block_UV[block_y][block_x].y_fin_downsampled = y_fin_downsampled_UV;

    
    side_average=2.0*block_length/downsampled_block_Y;    
    
    //adjust side a
    //--------------
    if (ppp_y_0<=ppp_y_2)
    {       
        ppp_y_0=side_average*ppp_y_0/side_a;

        if (ppp_y_0<PPP_MIN) 
        {
            ppp_y_0=PPP_MIN;
            
        }//PPPmin is 1 a PPP value <1 is not possible

        add = 0;
        ppp_y_2=side_average-ppp_y_0;
        if (ppp_y_2>ppp_max) 
        {
            add=ppp_y_2-ppp_max; 
            ppp_y_2=ppp_max;       
        }

        ppp_y_0+=add;
    }
    else
    {
        ppp_y_2=side_average*ppp_y_2/side_a;

        if (ppp_y_2<PPP_MIN) 
        { 
            ppp_y_2=PPP_MIN;    
        }//PPPmin is 1 a PPP value <1 is not possible
        
        add=0;
        ppp_y_0=side_average-ppp_y_2;
        if (ppp_y_0>ppp_max) 
        {
            add=ppp_y_0-ppp_max; 
            ppp_y_0=ppp_max;          
        }

        ppp_y_2+=add;

    }

     //adjust side b
    //--------------
    if (ppp_y_1<=ppp_y_3)
    {       
        ppp_y_1=side_average*ppp_y_1/side_b;

        
        if (ppp_y_1<PPP_MIN) 
        {
            ppp_y_1=PPP_MIN;
            
        }// PPP can not be <PPP_MIN
        
        add=0;
        ppp_y_3=side_average-ppp_y_1;
        if (ppp_y_3>ppp_max) 
        {
            add=ppp_y_3-ppp_max; 
            ppp_y_3=ppp_max;
        }

        ppp_y_1+=add;
    }
    else
    {
        ppp_y_3=side_average*ppp_y_3/side_b;

        if (ppp_y_3<PPP_MIN) 
        {
            ppp_y_3=PPP_MIN;
            
        }

        add=0;
        ppp_y_1=side_average-ppp_y_3;
        if (ppp_y_1>ppp_max) 
        {
            add=ppp_y_1-ppp_max; 
            ppp_y_1=ppp_max;           
        }
        ppp_y_3+=add;

    }
    
    ppp_y[block_y][block_x][TOP_LEFT_CORNER] = ppp_y_0;
    ppp_y[block_y][block_x][TOP_RIGHT_CORNER] = ppp_y_1;
    ppp_y[block_y][block_x][BOT_LEFT_CORNER] = ppp_y_2;
    ppp_y[block_y][block_x][BOT_RIGHT_CORNER] = ppp_y_3;
}


//==================================================================
// LHE PRECOMPUTATION
// Precomputation methods for both LHE encoder and decoder .
//==================================================================

/**
 * Calculates color component value in the middle of the interval for each hop.
 * Bassically this method inits the luminance value of each hop with the intermediate 
 * value between hops frontiers.
 *
 * h0--)(-----h1----center-------------)(---------h2--------center----------------)
 */
static void lhe_init_hop_center_color_component_value (LheBasicPrec *prec, int hop0_Y, int hop1, int rmax)
{
    
    //MIDDLE VALUE LUMINANCE
    //It is calculated as: luminance_center_hop= (Y_hop + (Y_hop_ant+Y_hop_next)/2)/2;

    uint8_t hop_neg_1_Y, hop_neg_2_Y, hop_neg_3_Y, hop_neg_4_Y;
    uint8_t hop_pos_1_Y, hop_pos_2_Y, hop_pos_3_Y, hop_pos_4_Y;
      
    hop_neg_1_Y = prec -> prec_luminance[hop0_Y][rmax][hop1][HOP_NEG_1];
    hop_neg_2_Y = prec -> prec_luminance[hop0_Y][rmax][hop1][HOP_NEG_2];
    hop_neg_3_Y = prec -> prec_luminance[hop0_Y][rmax][hop1][HOP_NEG_3];
    hop_neg_4_Y = prec -> prec_luminance[hop0_Y][rmax][hop1][HOP_NEG_4];
    hop_pos_1_Y = prec -> prec_luminance[hop0_Y][rmax][hop1][HOP_POS_1];
    hop_pos_2_Y = prec -> prec_luminance[hop0_Y][rmax][hop1][HOP_POS_2];
    hop_pos_3_Y = prec -> prec_luminance[hop0_Y][rmax][hop1][HOP_POS_3];
    hop_pos_4_Y = prec -> prec_luminance[hop0_Y][rmax][hop1][HOP_POS_4];
    
    //HOP-3                   
    prec-> prec_luminance[hop0_Y][rmax][hop1][HOP_NEG_3]= (hop_neg_3_Y + (hop_neg_4_Y+hop_neg_2_Y)/2)/2;

    if (prec-> prec_luminance[hop0_Y][rmax][hop1][HOP_NEG_3] <= MIN_COMPONENT_VALUE) 
    {
        prec->prec_luminance[hop0_Y][rmax][hop1][HOP_NEG_3]=1;
        
    }

    //HOP-2                  
    prec-> prec_luminance[hop0_Y][rmax][hop1][HOP_NEG_2]= (hop_neg_2_Y+ (hop_neg_3_Y+hop_neg_1_Y)/2)/2;

    if (prec-> prec_luminance [hop0_Y][rmax][hop1][HOP_NEG_2] <= MIN_COMPONENT_VALUE) 
    { 
            prec-> prec_luminance [hop0_Y][rmax][hop1][HOP_NEG_2]=1;
        
    }


    //HOP2                   
    prec-> prec_luminance[hop0_Y][rmax][hop1][HOP_POS_2]= (hop_pos_2_Y + (hop_pos_1_Y+hop_pos_3_Y)/2)/2;

    if (prec-> prec_luminance[hop0_Y][rmax][hop1][HOP_POS_2]>MAX_COMPONENT_VALUE) 
    {
        prec-> prec_luminance[hop0_Y][rmax][hop1][HOP_POS_2]=MAX_COMPONENT_VALUE;
        
    }

    //HOP3
                    
    prec-> prec_luminance[hop0_Y][rmax][hop1][HOP_POS_3]= (hop_pos_3_Y + (hop_pos_2_Y+hop_pos_4_Y)/2)/2;

    if (prec-> prec_luminance[hop0_Y][rmax][hop1][HOP_POS_3]>MAX_COMPONENT_VALUE) 
    {
        prec-> prec_luminance[hop0_Y][rmax][hop1][HOP_POS_3]=MAX_COMPONENT_VALUE;
        
    }  
}
    
/**
 * Inits precalculated luminance cache
 * Calculates color component value for each hop.
 * Final color component ( luminance or chrominance) depends on hop1
 * Color component for negative hops is calculated as: hopi_Y = hop0_Y - hi
 * Color component for positive hops is calculated as: hopi_Y = hop0_Y + hi
 * where hop0_Y is hop0 component color value 
 * and hi is the luminance distance from hop0_Y to hopi_Y
 */
static void lhe_init_hop_color_component_value (LheBasicPrec *prec, int hop0_Y, int hop1, int rmax,
                                                uint8_t hop_neg_4, uint8_t hop_neg_3, uint8_t hop_neg_2,
                                                uint8_t hop_pos_2, uint8_t hop_pos_3, uint8_t hop_pos_4)
{    
    //HOP -4
    prec-> prec_luminance[hop0_Y][rmax][hop1][HOP_NEG_4]= hop0_Y  - hop_neg_4; 
    
    if (prec-> prec_luminance[hop0_Y][rmax][hop1][HOP_NEG_4]<=MIN_COMPONENT_VALUE) 
    { 
        prec-> prec_luminance[hop0_Y][rmax][hop1][HOP_NEG_4]=1;
    }

    //HOP-3
    prec-> prec_luminance[hop0_Y][rmax][hop1][HOP_NEG_3]= hop0_Y  - hop_neg_3; 

    if (prec-> prec_luminance[hop0_Y][rmax][hop1][HOP_NEG_3] <= MIN_COMPONENT_VALUE) 
    {
        prec->prec_luminance [hop0_Y][rmax][hop1][HOP_NEG_3]=1;
        
    }

    //HOP-2
    prec-> prec_luminance[hop0_Y][rmax][hop1][HOP_NEG_2]= hop0_Y  - hop_neg_2; 

    if (prec-> prec_luminance [hop0_Y][rmax][hop1][HOP_NEG_2] <= MIN_COMPONENT_VALUE) 
    { 
            prec-> prec_luminance[hop0_Y][rmax][hop1][HOP_NEG_2]=1;
        
    }

    //HOP-1
    prec-> prec_luminance[hop0_Y][rmax][hop1][HOP_NEG_1]= hop0_Y-hop1;

    if (prec-> prec_luminance[hop0_Y][rmax][hop1][HOP_NEG_1] <= MIN_COMPONENT_VALUE) 
    {
        prec-> prec_luminance[hop0_Y][rmax][hop1][HOP_NEG_1]=1;
    }

    //HOP0(int)
    prec-> prec_luminance[hop0_Y][rmax][hop1][HOP_0]= hop0_Y; //null hop

    if (prec-> prec_luminance[hop0_Y][rmax][hop1][HOP_0]<=MIN_COMPONENT_VALUE) 
    {
        prec-> prec_luminance[hop0_Y][rmax][hop1][HOP_0]=1; //null hop
    }

    if (prec-> prec_luminance[hop0_Y][rmax][hop1][HOP_0]>MAX_COMPONENT_VALUE) 
    {
        prec-> prec_luminance[hop0_Y][hop1][rmax][HOP_0]=MAX_COMPONENT_VALUE;//null hop
    }

    //HOP1
    prec-> prec_luminance[hop0_Y][rmax][hop1][HOP_POS_1]= hop0_Y + hop1;

    if (prec-> prec_luminance[hop0_Y][rmax][hop1][HOP_POS_1]>MAX_COMPONENT_VALUE)
    {
        prec-> prec_luminance[hop0_Y][rmax][hop1][HOP_POS_1]=MAX_COMPONENT_VALUE;
    }

    //HOP2
    prec-> prec_luminance[hop0_Y][rmax][hop1][HOP_POS_2]= hop0_Y  + hop_pos_2; 

    if (prec-> prec_luminance[hop0_Y][rmax][hop1][HOP_POS_2]>MAX_COMPONENT_VALUE) 
    {
        prec-> prec_luminance[hop0_Y][rmax][hop1][HOP_POS_2]=MAX_COMPONENT_VALUE;
        
    }

    //HOP3
    prec-> prec_luminance[hop0_Y][rmax][hop1][HOP_POS_3]= hop0_Y  + hop_pos_3; 

    if (prec-> prec_luminance[hop0_Y][rmax][hop1][HOP_POS_3]>MAX_COMPONENT_VALUE) 
    {
        prec-> prec_luminance[hop0_Y][rmax][hop1][HOP_POS_3]=MAX_COMPONENT_VALUE;
        
    }

    //HOP4
    prec-> prec_luminance[hop0_Y][rmax][hop1][HOP_POS_4]= hop0_Y  + hop_pos_4; 

    if (prec-> prec_luminance[hop0_Y][rmax][hop1][HOP_POS_4]>MAX_COMPONENT_VALUE) 
    {
        prec-> prec_luminance[hop0_Y][rmax][hop1][HOP_POS_4]=MAX_COMPONENT_VALUE;
    }             
}

/**
 * Inits best hop cache
 */
static void lhe_init_best_hop(LheBasicPrec* prec, int hop0_Y, int hop_1, int r_max)
{

    int j,original_color, error, min_error;

    
    for (original_color = 0; original_color<Y_MAX_COMPONENT; original_color++) 
    {            
        error = 0;
        min_error = 256;
        
        //Positive hops computation
        //-------------------------
        if (original_color - hop0_Y>=0) 
        {                        
            for (j=HOP_0;j<=HOP_POS_4;j++) 
            {               
                error= original_color - prec -> prec_luminance[hop0_Y][r_max][hop_1][j];

                if (error<0) {
                    error=-error;
                }

                if (error<min_error) 
                {
                    prec->best_hop[r_max][hop_1][original_color][hop0_Y]=j;
                    min_error=error;
                    
                }
                else
                {
                    break;
                }
            }
        }

        //Negative hops computation
        //-------------------------
        else 
        {
            for (j=HOP_0;j>=HOP_NEG_4;j--) 
            {              
                error= original_color - prec -> prec_luminance[hop0_Y][r_max][hop_1][j];
 
                if (error<0) 
                {
                    error = -error;
                }

                if (error<min_error) {
                    prec->best_hop[r_max][hop_1][original_color][hop0_Y]=j;
                    min_error=error;
                }
                else 
                {
                    break;
                }
            }
        }
    }
}

/**
 * @deprecated
 * 
 * Inits h1 cache 
 */
static void lhe_init_h1_adaptation (LheBasicPrec* prec) 
{
    uint8_t hop_prev, hop, x, hop_1; 
    
    for (hop_1=1; hop_1<H1_RANGE; hop_1++) 
    {
         for (hop_prev=0; hop_prev<NUMBER_OF_HOPS; hop_prev++)
            {
                for (hop = 0; hop<NUMBER_OF_HOPS; hop++) 
                {
           
                if(hop<=HOP_POS_1 && hop>=HOP_NEG_1 && hop_prev<=HOP_POS_1 && hop_prev>=HOP_NEG_1)  {

                    prec->h1_adaptation[hop_1][hop_prev][hop] = hop_1 - 1;
                    
                    if (hop_1<MIN_HOP_1) {
                        prec->h1_adaptation[hop_1][hop_prev][hop] = MIN_HOP_1;
                    } 
                    
                } else {
                    prec->h1_adaptation[hop_1][hop_prev][hop] = MAX_HOP_1;
                }
            }
        }
    }
}

/**
 * Inits lhe cache
 */
void lhe_init_cache (LheBasicPrec *prec) 
{ 
    
    float positive_ratio, negative_ratio; //pow functions

    //NEGATIVE HOPS
    uint8_t hop_neg_4, hop_neg_3, hop_neg_2;
    
    //POSITIVE HOPS
    uint8_t hop_pos_2, hop_pos_3, hop_pos_4;
    
    const float percent_range=0.8f;//0.8 is the  80%
    const float pow_index = 1.0f/3;

    //hop0_Y is hop0 component color value
    for (int hop0_Y = 0; hop0_Y<Y_MAX_COMPONENT; hop0_Y++)
    {
        //this bucle allows computations for different values of rmax from 20 to 40. 
        //however, finally only one value (25) is used in LHE
        for (int rmax=R_MIN; rmax<R_MAX ;rmax++) 
        {
            //hop1 is the distance from hop0_Y to next hop (positive or negative)
            for (int hop1 = 1; hop1<H1_RANGE; hop1++) 
            {
                //variable declaration
                float max= rmax/10.0;// control of limits if rmax is 25 then max is 2.5f;
                              
                // r values for possitive hops  
                positive_ratio=(float)pow(percent_range*(255-hop0_Y)/(hop1), pow_index);

                if (positive_ratio>max) 
                {
                    positive_ratio=max;
                }
                
                // r' values for negative hops
                negative_ratio=(float)pow(percent_range*(hop0_Y)/(hop1), pow_index);

                if (negative_ratio>max) 
                {
                    negative_ratio=max;
                }
                
                // COMPUTATION OF HOPS
                
                //  Possitive hops luminance
                hop_pos_2 = hop1*positive_ratio;
                hop_pos_3 = hop_pos_2*positive_ratio;
                hop_pos_4 = hop_pos_3*positive_ratio;

                //Negative hops luminance                        
                hop_neg_2 = hop1*negative_ratio;
                hop_neg_3 = hop_neg_2*negative_ratio;
                hop_neg_4 = hop_neg_3*negative_ratio;

                lhe_init_hop_color_component_value (prec, hop0_Y, hop1, rmax, hop_neg_4, hop_neg_3, 
                                                    hop_neg_2, hop_pos_2, hop_pos_3, hop_pos_4);
                
                if (MIDDLE_VALUE) {
                    lhe_init_hop_center_color_component_value(prec, hop0_Y, hop1, rmax);
                }
                
                lhe_init_best_hop(prec, hop0_Y, hop1, rmax );
            }
        }
    }
}