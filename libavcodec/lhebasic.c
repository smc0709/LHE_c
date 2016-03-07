#include "lhebasic.h"

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

/**
 * 
 * Huffman
 */

static int huff_cmp_len(const void *a, const void *b)
{
    const LheHuffEntry *aa = a, *bb = b;
    return (aa->len - bb->len)*LHE_MAX_HUFF_SIZE + aa->sym - bb->sym;
}

/* Compare huffentry symbols */
static int huff_cmp_sym(const void *a, const void *b)
{
    const LheHuffEntry *aa = a, *bb = b;
    return aa->sym - bb->sym;
}

int lhe_generate_huffman_codes(LheHuffEntry *he)
{
    int len, i, j, last;
    uint16_t code;
    uint64_t bits;
    
    code = 1;
    last = LHE_MAX_HUFF_SIZE-1;

    qsort(he, LHE_MAX_HUFF_SIZE, sizeof(*he), huff_cmp_len); 

    
    while (he[last].len == 255 && last)
        last--;
    
    for (i=0; i<he[last].len; i++)
    {
        code|= 1 << i;
    }      
        
    for (len= he[last].len; len > 0; len--) {
        for (i = 0; i <= last; i++) {
            if (he[i].len == len)
            {
                he[i].code = code;
                code--;
            }          
        }
        
        code >>= 1;
    }

    
    qsort(he, LHE_MAX_HUFF_SIZE, sizeof(*he), huff_cmp_sym);
    
    for (i=0; i<LHE_MAX_HUFF_SIZE; i++) 
    {
        bits += (he[i].len * he[i].count); //bits number is symbol occurrence * symbol bits
    }
    
    return bits;

}

/**
 * LHE Precomputation 
 * 
 * Precomputation methods for both LHE encoder and decoder .
 */

/**
 * Calculates color component value in the middle of the interval for each hop.
 * This method improves quality. Bassically this method init the value
 * of luminance of hops by the intermediate value between hops frontiers.
 *
 * h0--)(-----h1----center-------------)(---------h2--------center----------------)
 */
static void lhe_init_hop_center_color_component_value (LheBasicPrec *prec, int hop0_Y, int hop1, int rmax,
                                                    uint8_t hop_neg_4 [H1_RANGE][Y_MAX_COMPONENT], 
                                                    uint8_t hop_neg_3 [H1_RANGE][Y_MAX_COMPONENT], 
                                                    uint8_t hop_neg_2 [H1_RANGE][Y_MAX_COMPONENT],
                                                    uint8_t hop_pos_2 [H1_RANGE][Y_MAX_COMPONENT],
                                                    uint8_t hop_pos_3 [H1_RANGE][Y_MAX_COMPONENT],
                                                    uint8_t hop_pos_4 [H1_RANGE][Y_MAX_COMPONENT])
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
 * Calculates color component value for each hop.
 * Final color component ( luminance or chrominance) depends on hop1
 * Color component for negative hops is calculated as: hopi_Y = hop0_Y - hi
 * Color component for positive hops is calculated as: hopi_Y = hop0_Y + hi
 * where hop0_Y is hop0 component color value 
 * and hi is the luminance distance from hop0_Y to hopi_Y
 */
static void lhe_init_hop_color_component_value (LheBasicPrec *prec, int hop0_Y, int hop1, int rmax,
                                                uint8_t hop_neg_4 [Y_MAX_COMPONENT][H1_RANGE], 
                                                uint8_t hop_neg_3 [Y_MAX_COMPONENT][H1_RANGE], 
                                                uint8_t hop_neg_2 [Y_MAX_COMPONENT][H1_RANGE],
                                                uint8_t hop_pos_2 [Y_MAX_COMPONENT][H1_RANGE],
                                                uint8_t hop_pos_3 [Y_MAX_COMPONENT][H1_RANGE],
                                                uint8_t hop_pos_4 [Y_MAX_COMPONENT][H1_RANGE])
{
    //From most negative hop (pccr[hop1][hop0_Y][HOP_NEG_4]) to most possitive hop (pccr[hop1][hop0_Y][HOP_POS_4])
    
    //HOP -4
    prec-> prec_luminance[hop0_Y][rmax][hop1][HOP_NEG_4]= hop0_Y  - (uint8_t) hop_neg_4[hop0_Y][hop1] ; 
    
    if (prec-> prec_luminance[hop0_Y][rmax][hop1][HOP_NEG_4]<=MIN_COMPONENT_VALUE) 
    { 
        prec-> prec_luminance[hop0_Y][rmax][hop1][HOP_NEG_4]=1;
    }

    //HOP-3
    prec-> prec_luminance[hop0_Y][rmax][hop1][HOP_NEG_3]= hop0_Y  - (uint8_t) hop_neg_3[hop0_Y][hop1]; 

    if (prec-> prec_luminance[hop0_Y][rmax][hop1][HOP_NEG_3] <= MIN_COMPONENT_VALUE) 
    {
        prec->prec_luminance [hop0_Y][rmax][hop1][HOP_NEG_3]=1;
        
    }

    //HOP-2
    prec-> prec_luminance[hop0_Y][rmax][hop1][HOP_NEG_2]= hop0_Y  - (uint8_t) hop_neg_2[hop0_Y][hop1]; 

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
    prec-> prec_luminance[hop0_Y][rmax][hop1][HOP_POS_2]= hop0_Y  + (uint8_t) hop_pos_2[hop0_Y][hop1]; 

    if (prec-> prec_luminance[hop0_Y][rmax][hop1][HOP_POS_2]>MAX_COMPONENT_VALUE) 
    {
        prec-> prec_luminance[hop0_Y][rmax][hop1][HOP_POS_2]=MAX_COMPONENT_VALUE;
        
    }

    //HOP3
    prec-> prec_luminance[hop0_Y][rmax][hop1][HOP_POS_3]= hop0_Y  + (uint8_t) hop_pos_3[hop0_Y][hop1]; 

    if (prec-> prec_luminance[hop0_Y][rmax][hop1][HOP_POS_3]>MAX_COMPONENT_VALUE) 
    {
        prec-> prec_luminance[hop0_Y][rmax][hop1][HOP_POS_3]=MAX_COMPONENT_VALUE;
        
    }

    //HOP4
    prec-> prec_luminance[hop0_Y][rmax][hop1][HOP_POS_4]= hop0_Y  + (uint8_t) hop_pos_4[hop0_Y][hop1]; 

    if (prec-> prec_luminance[hop0_Y][rmax][hop1][HOP_POS_4]>MAX_COMPONENT_VALUE) 
    {
        prec-> prec_luminance[hop0_Y][rmax][hop1][HOP_POS_4]=MAX_COMPONENT_VALUE;
    }             
}

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
 * Calculates lhe init cache
 */
void lhe_init_cache (LheBasicPrec *prec) 
{ 
    
    float cache_ratio[Y_MAX_COMPONENT][RATIO][H1_RANGE][SIGN]; //pow functions

    //NEGATIVE HOPS
    uint8_t hop_neg_4 [Y_MAX_COMPONENT][H1_RANGE]; // h-4 value 
    uint8_t hop_neg_3 [Y_MAX_COMPONENT][H1_RANGE]; // h-3 value 
    uint8_t hop_neg_2 [Y_MAX_COMPONENT][H1_RANGE]; // h-2 value 
    
    //POSITIVE HOPS
    uint8_t hop_pos_2 [Y_MAX_COMPONENT][H1_RANGE]; // h2 value 
    uint8_t hop_pos_3 [Y_MAX_COMPONENT][H1_RANGE]; // h3 value 
    uint8_t hop_pos_4 [Y_MAX_COMPONENT][H1_RANGE]; // h4 value
    
    const float percent_range=0.8f;//0.8 is the  80%
    const float pow_index = 1.0f/3;
    float ratio_pos;
    float ratio_neg;

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
                cache_ratio[hop0_Y][rmax][hop1][POSITIVE]=(float)pow(percent_range*(255-hop0_Y)/(hop1), pow_index);

                if (cache_ratio[hop0_Y][rmax][hop1][POSITIVE]>max) 
                {
                    cache_ratio[hop0_Y][rmax][hop1][POSITIVE]=max;
                }
                
                // r' values for negative hops
                cache_ratio[hop0_Y][rmax][hop1][NEGATIVE]=(float)pow(percent_range*(hop0_Y)/(hop1), pow_index);

                if (cache_ratio[hop0_Y][rmax][hop1][NEGATIVE]>max) 
                {
                    cache_ratio[hop0_Y][rmax][hop1][NEGATIVE]=max;
                }
                
                //get r value for possitive hops from cache_ratio       
                ratio_pos=cache_ratio[hop0_Y][rmax][hop1][POSITIVE];

                //get r' value for negative hops from cache_ratio
                ratio_neg=cache_ratio[hop0_Y][rmax][hop1][NEGATIVE];

                // COMPUTATION OF HOPS
                
                //  Possitive hops luminance
                hop_pos_2[hop0_Y][hop1] = hop1*ratio_pos;
                hop_pos_3[hop0_Y][hop1] = hop_pos_2[hop0_Y][hop1]*ratio_pos;
                hop_pos_4[hop0_Y][hop1] = hop_pos_3[hop0_Y][hop1]*ratio_pos;

                //Negative hops luminance                        
                hop_neg_2[hop0_Y][hop1] = hop1*ratio_neg;
                hop_neg_3[hop0_Y][hop1] = hop_neg_2[hop0_Y][hop1]*ratio_neg;
                hop_neg_4[hop0_Y][hop1] = hop_neg_3[hop0_Y][hop1]*ratio_neg;

                lhe_init_hop_color_component_value (prec, hop0_Y, hop1, rmax, hop_neg_4, hop_neg_3, 
                                                    hop_neg_2, hop_pos_2, hop_pos_3, hop_pos_4);
                
                if (MIDDLE_VALUE) {
                    lhe_init_hop_center_color_component_value(prec, hop0_Y, hop1, rmax, hop_neg_4, hop_neg_3, 
                                                              hop_neg_2, hop_pos_2, hop_pos_3, hop_pos_4);
                }
                
                lhe_init_best_hop(prec, hop0_Y, hop1, rmax );
            }
        }
    }
    
    //h1 adaptation cache
    lhe_init_h1_adaptation (prec);
}