/*
 * LHE Basic encoder
 */

/**
 * @file
 * LHE Basic encoder
 */

#include "avcodec.h"
#include "lhebasic.h"
#include "internal.h"
#include "libavutil/opt.h"
#include "libavutil/imgutils.h"
#include "put_bits.h"
#include "bytestream.h"


typedef struct LheContext {
    AVClass *class;    
    LheBasicPrec prec;
} LheContext;

static av_cold int lhe_encode_init(AVCodecContext *avctx)
{
    LheContext *s = avctx->priv_data;

    lhe_init_cache(&s->prec);

    return 0;

}

static uint8_t* lhe_encode_one_hop_per_pixel (LheBasicPrec *prec, const AVFrame *frame) 
{      
    //Hops computation.
    bool small_hop, last_small_hop;
    int predicted_luminance, hop_1, hop_number, pix, original_color, r_max;
    
    //Errors
    int min_error;      // error of predicted signal
    int error;          //computed error for each hop 
    
    //Colin computation variables
    float hY, hYant, hYnext;
    int hop0i;
    uint8_t colin[9];
    
    //Result arrays
    const int size = frame -> height * frame -> width;
    uint8_t *component_prediction, *hops;
    
    small_hop = false;
    last_small_hop=false;          // indicates if last hop is small
    predicted_luminance=0;         // predicted signal
    hop_1= START_HOP_1;
    hop_number=4;                  // pre-selected hop // 4 is NULL HOP
    pix=0;                         // pixel possition, from 0 to image size        
    original_color=0;              // original color
    
    r_max=PARAM_R;                      

    component_prediction = malloc(sizeof(uint8_t) * size);
    hops = malloc(sizeof(uint8_t) * size);

    for (int y=0; y < frame -> height; y++)  {
        for (int x=0; x < frame -> width; x++)  {
             
            original_color = frame->data[0][pix];

            //prediction of signal (predicted_luminance) , based on pixel's coordinates 
            //----------------------------------------------------------
            if ((y>0) &&(x>0) && x!=frame -> width-1)
            {
                predicted_luminance=(4*component_prediction[pix-1]+3*component_prediction[pix+1-frame -> width])/7;     
            } 
            else if ((x==0) && (y>0))
            {
                predicted_luminance=component_prediction[pix-frame -> width];
                last_small_hop=false;
                hop_1=START_HOP_1;
            } 
            else if ((x==frame -> width-1) && (y>0)) 
            {
                predicted_luminance=(4*component_prediction[pix-1]+2*component_prediction[pix-frame -> width])/6;                               
            } 
            else if (y==0 && x>0) 
            {
                predicted_luminance=component_prediction[x-1];
            }
            else if (x==0 && y==0) {  
                predicted_luminance=original_color;//first pixel always is perfectly predicted! :-)  
            }          
            
            if (predicted_luminance>255) 
            {
                predicted_luminance=255;
            }
            
            if (predicted_luminance<0) 
            {
                predicted_luminance=0;  
            }
            
            // CENTER ADJUSTMENT SECTION
            //===========================
            // this section improves quality. In fact this section should be added to
            // the init() function instead here. Bassically this section changes the value
            // of luminance of hops by the intermediate value between hops frontiers.
            //
            // without this section, the program works fine but less quality
            //
            // h0--)(-----h1----center-------------)(---------h2--------center----------------)

            hop0i = prec -> prec_luminance[hop_1][predicted_luminance][r_max][HOP_0];//null hop
            
            colin[HOP_0]=hop0i;//new null does not change 
            colin[HOP_POS_4]=prec -> prec_luminance[hop_1][hop0i][r_max][HOP_POS_4];//hop4 (maximum possitive) does not change
            colin[HOP_NEG_4]=prec -> prec_luminance[hop_1][hop0i][r_max][HOP_NEG_4];//hop-4 (maximum negative) does not change
            colin[HOP_NEG_1]=prec -> prec_luminance[hop_1][hop0i][r_max][HOP_NEG_1];//hop-1 does not change
            colin[HOP_POS_1]=prec -> prec_luminance[hop_1][hop0i][r_max][HOP_POS_1];//hop1 does not change

            

            for (int j=1; j<8;j++)
            {
 
                if (j == HOP_0 || j == HOP_POS_1 || j == HOP_NEG_1) 
                {
                    colin[j] = 0; //Do not adjust h0, h1, h-1
                }
                else {
                    hY = (float) prec -> prec_luminance[hop_1][hop0i][r_max][j];
                    hYant = (float) prec -> prec_luminance[hop_1][hop0i][r_max][j-1];
                    hYnext = (float) prec -> prec_luminance[hop_1][hop0i][r_max][j+ 1];
                    
                    colin[j]= (int) ((hY + (hYant+hYnext)/2)/2);
                }
            }


            // end of center adjustment section 
            //==================================
            
            
            error = 0;
            min_error = 256;
            
            //Positive hops computation
            //-------------------------
            if (original_color-predicted_luminance>=0) 
            {
                for (int j=HOP_0;j<=HOP_POS_4;j++) 
                {
                        error= original_color - prec -> prec_luminance[hop_1][predicted_luminance][r_max][j];
                        
                        if (error<0) {
                            error=-error;
                        }
                        
                        if (error<min_error) 
                        {
                            hop_number=j;
                            min_error=error;
                            
                        }
                        else break;
                }
            }
            
            //Negative hops computation
            //-------------------------
            else 
            {
                for (int j=HOP_0;j>=HOP_NEG_4;j--) 
                {
                        error = prec -> prec_luminance[hop_1][predicted_luminance][r_max][j]-original_color;

                        if (error<0) 
                        {
                            error = -error;
                        }
                        if (error<min_error) {
                            hop_number=j;
                            min_error=error;
                        }
                        else break;
                }
            }

            //assignment of final color value
            //--------------------------------
            component_prediction[pix]=prec -> prec_luminance[hop_1][predicted_luminance][r_max][hop_number];
            hops[pix]=hop_number; 

            //tunning hop1 for the next hop ( "h1 adaptation")
            //------------------------------------------------
            small_hop=false;
            if (hop_number<=HOP_POS_1 && hop_number>=HOP_NEG_1) 
            {
                small_hop=true;// 4 is in the center, 4 is null hop
            }
            else 
            {
                small_hop=false;    
            }

            if( (small_hop) && (last_small_hop))  {
                hop_1=hop_1-1;
                if (hop_1<MIN_HOP_1) {
                    hop_1=MIN_HOP_1;
                } 
                else {
                    hop_1=MAX_HOP_1;
                }
            }

            //lets go for the next pixel
            //--------------------------
            last_small_hop=small_hop;
            pix++;            
        }//for x
    }//for y 
    
    return hops;
}



static int lhe_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                             const AVFrame *frame, int *got_packet)
{
    uint8_t * hops, *buf, original_color;
    uint32_t width, height;
    int image_size;
    int ret, n_bytes, n_bytes_hops;
    LheContext *s = avctx->priv_data;
    
    width = (uint32_t) frame->width;
    height = (uint32_t) frame->height;  
    image_size = frame -> height * frame -> width;

    n_bytes_hops = sizeof(uint8_t) * image_size;
    n_bytes = n_bytes_hops + sizeof(original_color) 
            + sizeof(width) + sizeof(height);
    
    original_color = frame->data[0][0];
    hops = lhe_encode_one_hop_per_pixel(&s->prec, frame); 
    
    //ff_alloc_packet2 reserves n_bytes of memory
    if ((ret = ff_alloc_packet2(avctx, pkt, n_bytes, 0)) < 0)
        return ret;

    buf = pkt->data;
    
    //save original color 
    bytestream_put_byte(&buf, original_color);
    
    //save width and height
    bytestream_put_le32(&buf, width);
    bytestream_put_le32(&buf, height);    
    
    //copy n_bytes_hops from buf pointer
    memcpy(buf, hops, n_bytes_hops);

    av_log(NULL, AV_LOG_INFO, "LHE Coding...buffer size %d \n", n_bytes);

    pkt->flags |= AV_PKT_FLAG_KEY;
    *got_packet = 1;

    return 0;

}


static int lhe_encode_close(AVCodecContext *avctx)
{
    LheContext *s = avctx->priv_data;

    av_freep(&s->prec.prec_luminance);

    return 0;

}


static const AVClass lhe_class = {
    .class_name = "LHE Basic encoder",
    .item_name  = av_default_item_name,
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
    .priv_class     = &lhe_class,
};
