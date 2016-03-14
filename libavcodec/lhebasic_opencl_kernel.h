
#ifndef AVCODEC_LHE_OPENCL_KERNEL_H

#define AVCODEC_LHE_OPENCL_KERNEL_H

#include "libavutil/opencl.h"

const char *ff_kernel_lhe_opencl = AV_OPENCL_KERNEL(
    
    kernel void lhebasic_encode (global uchar *prec_luminance,
                                 global uchar *best_hop,
                                 global uchar *component_original_data, 
                                 global uchar *component_prediction, 
                                 global uchar *hops, 
                                 global uchar *first_color_block,
                                 int width, int height, 
                                 int block_width, int block_height,
                                 int linesize)
    {
        unsigned int block_x, block_y, num_block, total_blocks_width;
        unsigned int xini, xfin, yini, yfin;
        int pix, pix_original_data, dif_pix, dif_line, index;

        //Hops computation.
        bool small_hop, last_small_hop;
        uchar predicted_luminance, hop_1, hop_number, original_color, r_max;
        
        block_x = get_group_id(0);
        block_y = get_group_id(1);
        
        total_blocks_width = get_global_size(0);
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
        hop_1= 7;
        hop_number=4;                  // pre-selected hop // 4 is NULL HOP
        pix=0;                         // pixel possition, from 0 to image size  
        pix_original_data = 0;
        original_color=0;              // original color
        r_max=25;
        
        pix = yini*width + xini;
        pix_original_data = yini*linesize + xini;
    
        dif_pix = width - xfin + xini;
        dif_line = linesize - xfin + xini;
        
        for (int y=yini; y < yfin; y++)  {
            for (int x=xini; x < xfin; x++)  {
      
                original_color = component_original_data[pix_original_data];

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
                    hop_1=7;
                } else if (x == xfin -1) 
                {
                    predicted_luminance=(component_prediction[pix-1]+component_prediction[pix-width])>>1;                               
                } 
                else 
                {
                    predicted_luminance=(component_prediction[pix-1]+component_prediction[pix+1-width])>>1;     
                }                
            
                index = 20*256*256*r_max + 256*256*hop_1 + 256*original_color + predicted_luminance;
                hop_number = best_hop[index]; 
                
                hops[pix]= hop_number;
                
                index = 40*20*9*predicted_luminance + 20*9*r_max + 9*hop_1 + hop_number;                
                component_prediction[pix]= prec_luminance[index];


                //tunning hop1 for the next hop ( "h1 adaptation")
                //------------------------------------------------
                               
                if (hop_number<=5 && hop_number>=3)
                {                                                   
                    small_hop=true;                                 
                } else                                              
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
                last_small_hop=small_hop;
                

                //lets go for the next pixel
                //--------------------------
                pix++;
                pix_original_data++;
            }//for x
            pix+=dif_pix;
            pix_original_data+=dif_line;
        }//for y           
    }
);

#endif /* AVCODEC_LHE_OPENCL_KERNEL_H */