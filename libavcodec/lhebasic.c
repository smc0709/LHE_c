#include "lhebasic.h"

/**
 * LHE Precomputation class
 * 
 * Precomputation methods for both LHE encoder and decoder .
 */


void lhe_init_hop_color_component_value (LheBasicPrec *prec, int hop0_Y, int hop1, int rmax,
                                                float hop_neg_4 [H1_RANGE][Y_COMPONENT], 
                                                float hop_neg_3 [H1_RANGE][Y_COMPONENT], 
                                                float hop_neg_2 [H1_RANGE][Y_COMPONENT],
                                                float hop_pos_2 [H1_RANGE][Y_COMPONENT],
                                                float hop_pos_3 [H1_RANGE][Y_COMPONENT],
                                                float hop_pos_4 [H1_RANGE][Y_COMPONENT])
{
    //From most negative hop (pccr[hop1][hop0_Y][HOP_NEG_4]) to most possitive hop (pccr[hop1][hop0_Y][HOP_POS_4])
    
    //HOP -4
    prec-> prec_luminance[hop1][hop0_Y][rmax][HOP_NEG_4]= hop0_Y  - (int) hop_neg_4[hop1][hop0_Y] ; 

    if (prec-> prec_luminance[hop1][hop0_Y][rmax][HOP_NEG_4]<=MIN_COMPONENT_VALUE) 
    { 
        prec-> prec_luminance[hop1][hop0_Y][rmax][HOP_NEG_4]=1;
        
    }

    //HOP-3
    prec-> prec_luminance[hop1][hop0_Y][rmax][HOP_NEG_3]= hop0_Y  - (int) hop_neg_3[hop1][hop0_Y]; 

    if (prec-> prec_luminance[hop1][hop0_Y][rmax][HOP_NEG_3] <= MIN_COMPONENT_VALUE) 
    {
        prec->prec_luminance [hop1][hop0_Y][rmax][HOP_NEG_3]=1;
        
    }

    //HOP-2
    prec-> prec_luminance[hop1][hop0_Y][rmax][HOP_NEG_2]= hop0_Y  - (int) hop_neg_2[hop1][hop0_Y]; 

    if (prec-> prec_luminance [hop1][hop0_Y][rmax][HOP_NEG_2] <= MIN_COMPONENT_VALUE) 
    { 
            prec-> prec_luminance[hop1][hop0_Y][rmax][HOP_NEG_2]=1;
        
    }

    //HOP-1
    prec-> prec_luminance[hop1][hop0_Y][rmax][HOP_NEG_1]= hop0_Y-hop1;

    if (prec-> prec_luminance[hop1][hop0_Y][rmax][HOP_NEG_1] <= MIN_COMPONENT_VALUE) 
    {
        prec-> prec_luminance[hop1][hop0_Y][rmax][HOP_NEG_1]=1;
    }

    //HOP0(int)
    prec-> prec_luminance[hop1][hop0_Y][rmax][HOP_0]= hop0_Y; //null hop

    if (prec-> prec_luminance[hop1][hop0_Y][rmax][HOP_0]<=MIN_COMPONENT_VALUE) 
    {
        prec-> prec_luminance[hop1][hop0_Y][rmax][HOP_0]=1; //null hop
    }

    if (prec-> prec_luminance[hop1][hop0_Y][rmax][HOP_0]>MAX_COMPONENT_VALUE) 
    {
        prec-> prec_luminance[hop1][hop0_Y][rmax][HOP_0]=MAX_COMPONENT_VALUE;//null hop
    }

    //HOP1
    prec-> prec_luminance[hop1][hop0_Y][rmax][HOP_POS_1]= hop0_Y + hop1;

    if (prec-> prec_luminance[hop1][hop0_Y][rmax][HOP_POS_1]>MAX_COMPONENT_VALUE)
    {
        prec-> prec_luminance[hop1][hop0_Y][rmax][HOP_POS_1]=MAX_COMPONENT_VALUE;
    }

    //HOP2
    prec-> prec_luminance[hop1][hop0_Y][rmax][HOP_POS_2]= hop0_Y  + (int) hop_pos_2[hop1][hop0_Y]; 

    if (prec-> prec_luminance[hop1][hop0_Y][rmax][HOP_POS_2]>MAX_COMPONENT_VALUE) 
    {
        prec-> prec_luminance[hop1][hop0_Y][rmax][HOP_POS_2]=MAX_COMPONENT_VALUE;
        
    }

    //HOP3
    prec-> prec_luminance[hop1][hop0_Y][rmax][HOP_POS_3]= hop0_Y  + (int) hop_pos_3[hop1][hop0_Y]; 

    if (prec-> prec_luminance[hop1][hop0_Y][rmax][HOP_POS_3]>MAX_COMPONENT_VALUE) 
    {
        prec-> prec_luminance[hop1][hop0_Y][rmax][HOP_POS_3]=MAX_COMPONENT_VALUE;
        
    }

    //HOP4
    prec-> prec_luminance[hop1][hop0_Y][rmax][HOP_POS_4]= hop0_Y  + (int) hop_pos_4[hop1][hop0_Y]; 

    if (prec-> prec_luminance[hop1][hop0_Y][rmax][HOP_POS_4]>MAX_COMPONENT_VALUE) 
    {
        prec-> prec_luminance[hop1][hop0_Y][rmax][HOP_POS_4]=MAX_COMPONENT_VALUE;
    }             
}


/**
 * Calculates lhe init cache
 */
void lhe_init_cache (LheBasicPrec *prec) 
{ 
    float cache_ratio[SIGN][H1_RANGE][Y_COMPONENT][RATIO]; //pow functions

    //NEGATIVE HOPS
    float hop_neg_4 [H1_RANGE][Y_COMPONENT]; // h-4 value 
    float hop_neg_3 [H1_RANGE][Y_COMPONENT]; // h-3 value 
    float hop_neg_2 [H1_RANGE][Y_COMPONENT]; // h-2 value 
    
    //POSITIVE HOPS
    float hop_pos_2 [H1_RANGE][Y_COMPONENT]; // h2 value 
    float hop_pos_3 [H1_RANGE][Y_COMPONENT]; // h3 value 
    float hop_pos_4 [H1_RANGE][Y_COMPONENT]; // h4 value
    
    
    //hop0_Y is hop0 component color value
    for (int hop0_Y = 0; hop0_Y<=255; hop0_Y++)
    {
        //hop1 is the distance from hop0_Y to next hop (positive or negative)
        for (int hop1 = 1; hop1 < H1_RANGE; hop1++) 
        {
            
            float percent_range=0.8f;//0.8 is the  80%
            float pow_index = 1.0f/3;
            
            //this bucle allows computations for different values of rmax from 20 to 40. 
            //however, finally only one value (25) is used in LHE
            for (int rmax=20;rmax<=40;rmax++) 
            {
                //variable declaration
                float max= rmax/10.0;// control of limits if rmax is 25 then max is 2.5f;
                float ratio_pos;
                float ratio_neg;
                
                // r values for possitive hops  
                cache_ratio[POSITIVE][hop1][hop0_Y][rmax]=(float)pow(percent_range*(255-hop0_Y)/(hop1), pow_index);

                if (cache_ratio[POSITIVE][hop1][hop0_Y][rmax]>max) 
                {
                    cache_ratio[POSITIVE][hop1][hop0_Y][rmax]=max;
                }
                
                // r' values for negative hops
                cache_ratio[NEGATIVE][hop1][hop0_Y][rmax]=(float)pow(percent_range*(hop0_Y)/(hop1), pow_index);

                if (cache_ratio[NEGATIVE][hop1][hop0_Y][rmax]>max) 
                {
                    cache_ratio[NEGATIVE][hop1][hop0_Y][rmax]=max;
                }
                
                //get r value for possitive hops from cache_ratio       
                ratio_pos=cache_ratio[POSITIVE][hop1][hop0_Y][rmax];

                //get r' value for negative hops from cache_ratio
                ratio_neg=cache_ratio[NEGATIVE][hop1][hop0_Y][rmax];

                // COMPUTATION OF HOPS
                
                //  Possitive hops luminance
                hop_pos_2[hop1][hop0_Y] = hop1*ratio_pos;
                hop_pos_3[hop1][hop0_Y] = hop_pos_2[hop1][hop0_Y]*ratio_pos;
                hop_pos_4[hop1][hop0_Y] = hop_pos_3[hop1][hop0_Y]*ratio_pos;

                //Negative hops luminance                        
                hop_neg_2[hop1][hop0_Y] = hop1*ratio_neg;
                hop_neg_3[hop1][hop0_Y] = hop_neg_2[hop1][hop0_Y]*ratio_neg;
                hop_neg_4[hop1][hop0_Y] = hop_neg_3[hop1][hop0_Y]*ratio_neg;

                lhe_init_hop_color_component_value (prec, hop0_Y, hop1, rmax, hop_neg_4, hop_neg_3, 
                                                    hop_neg_2, hop_pos_2, hop_pos_3, hop_pos_4);
            }
        }
    }
}