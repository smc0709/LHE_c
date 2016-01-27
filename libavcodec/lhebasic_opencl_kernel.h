
#ifndef AVCODEC_LHE_OPENCL_KERNEL_H

#define AVCODEC_LHE_OPENCL_KERNEL_H

#include "libavutil/opencl.h"

const char *ff_kernel_lhe_opencl = AV_OPENCL_KERNEL(
    
    kernel void lhebasic_encode (global uchar *prec_luminance,
                                 global uchar *best_hop,
                                 global uchar *component_original_data, 
                                 global uchar *component_prediction, 
                                 global uchar *hops, 
                                 int image_width, int image_height, 
                                 int block_width, int block_height,
                                 int pix_size,
                                 global uchar *a,
                                 global uchar *b,
                                 global uchar *c
                                )
    {
        unsigned int i,j;
        unsigned int xini, xfin, yini, yfin;
        int pix, index;

        //Hops computation.
        bool small_hop, last_small_hop;
        uchar predicted_luminance, hop_1, hop_number, original_color, r_max;

        i = get_global_id(0);
        j = get_global_id(1);
        
        xini = i*block_width;
        xfin = xini + block_width;
        
        yini = j*block_height;
        yfin = yini + block_height;        

        small_hop = false;
        last_small_hop=false;          // indicates if last hop is small
        predicted_luminance=0;         // predicted signal
        hop_1= 4;
        hop_number=7;                  // pre-selected hop // 4 is NULL HOP
        pix=0;                         // pixel possition, from 0 to image size        
        original_color=0;              // original color
        r_max=25;  
        
        for (int y=yini; y < yfin; y++)  {
            for (int x=xini; x < xfin; x++)  {
                
                pix = y*image_width + x;
                
                original_color = component_original_data[pix_size*pix];

                //prediction of signal (predicted_luminance) , based on pixel's coordinates 
                //----------------------------------------------------------
                if ((y>yini) &&(x>xini) && x!=block_width-1)
                {
                    predicted_luminance=(4*component_prediction[pix-1]+3*component_prediction[pix+1-image_width])/7;     
                } 
                else if ((x==xini) && (y>yini))
                {
                    predicted_luminance=component_prediction[pix-image_width];
                    last_small_hop=false;
                    hop_1=7;
                } 
                else if ((x==block_width-1) && (y>yini)) 
                {
                    predicted_luminance=(4*component_prediction[pix-1]+2*component_prediction[pix-image_width])/6;                               
                } 
                else if (y==yini && x>xini) 
                {
                    predicted_luminance=component_prediction[x-1];
                }
                else if (x==xini && y==yini) {  
                    predicted_luminance=original_color;//first pixel always is perfectly predicted! :-)  
                }          
                
        //index =H1_RANGE*Y_MAX_COMPONENT*Y_MAX_COMPONENT*r_max + Y_MAX_COMPONENT*Y_MAX_COMPONENT*hop_1 + Y_MAX_COMPONENT*original_color + predicted_luminance
        //index = RATIO*H1_RANGE*NUMBER_OF_HOPS*predicted_luminance + H1_RANGE*NUMBER_OF_HOPS*r_max + NUMBER_OF_HOPS*hop_1 + hop_number
            
                index = 20*256*256*r_max + 256*256*hop_1 + 256*original_color + predicted_luminance;
                hop_number = best_hop[index]; 
                hops[pix]= hop_number;
                index = 40*20*9*predicted_luminance + 20*9*r_max + 9*hop_1 + hop_number;                
                component_prediction[pix]= prec_luminance[index];


                //tunning hop1 for the next hop ( "h1 adaptation")
                //------------------------------------------------
                               
                if (hop_number<5 && hop_number>3) 
                {
                    small_hop=true;// 4 is in the center, 4 is null hop
                }
                else 
                {
                    small_hop=false;    
                }

                if( (small_hop) && (last_small_hop))  {
                    hop_1=hop_1-1;
                    if (hop_1<4) {
                        hop_1=4;
                    } 
                    
                } else {
                    hop_1=10;
                }
                

                //lets go for the next pixel
                //--------------------------
                last_small_hop=small_hop;
            }//for x
        }//for y           
    }
);

#endif /* AVCODEC_LHE_OPENCL_KERNEL_H */