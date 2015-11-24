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

static void lhe_encode_one_hop_per_pixel (LheBasicPrec *prec, uint8_t *component_original_data, uint8_t *component_prediction,
                                              uint8_t *hops_buf, int height, int width, int pix_size)
{      
    //Hops computation.
    bool small_hop, last_small_hop;
    uint8_t predicted_luminance, hop_1, hop_number, original_color, r_max;
    int pix;
    
    //Errors
    int min_error;      // error of predicted signal
    int error;          //computed error for each hop 

    small_hop = false;
    last_small_hop=false;          // indicates if last hop is small
    predicted_luminance=0;         // predicted signal
    hop_1= START_HOP_1;
    hop_number=4;                  // pre-selected hop // 4 is NULL HOP
    pix=0;                         // pixel possition, from 0 to image size        
    original_color=0;              // original color
    
    r_max=PARAM_R;     
    for (int y=0; y < height; y++)  {
        for (int x=0; x < width; x++)  {
            
            //av_log(NULL, AV_LOG_INFO, "Linesize %d Pix %d pix_sixe %d \n", frame->linesize[0], pix, pix_size);

            original_color = component_original_data[pix_size*pix];

            //prediction of signal (predicted_luminance) , based on pixel's coordinates 
            //----------------------------------------------------------
            if ((y>0) &&(x>0) && x!=width-1)
            {
                predicted_luminance=(4*component_prediction[pix-1]+3*component_prediction[pix+1-width])/7;     
            } 
            else if ((x==0) && (y>0))
            {
                predicted_luminance=component_prediction[pix-width];
                last_small_hop=false;
                hop_1=START_HOP_1;
            } 
            else if ((x==width-1) && (y>0)) 
            {
                predicted_luminance=(4*component_prediction[pix-1]+2*component_prediction[pix-width])/6;                               
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

            // end of center adjustment section 
            //==================================       
            error = 0;
            min_error = 256;
            
            //Positive hops computation
            //-------------------------
            if (original_color - predicted_luminance>=0) 
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
            if (MIDDLE_VALUE) 
            {
                component_prediction[pix]=prec -> prec_luminance_center[hop_1][predicted_luminance][r_max][hop_number];

            } else 
            {
                component_prediction[pix]=prec -> prec_luminance[hop_1][predicted_luminance][r_max][hop_number];
            }

            hops_buf[pix]=hop_number; 

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
                
            } else {
                hop_1=MAX_HOP_1;
            }

            //lets go for the next pixel
            //--------------------------
            last_small_hop=small_hop;
            pix++;
        }//for x
    }//for y     
}


static void lhe_print_data(AVFrame *picture)
{
    int pix = 0;
    uint8_t *p = (uint8_t *)picture->data[0];
    uint8_t *p_end = p + (picture->linesize[0] / sizeof(uint8_t)) * picture->height;

    av_log(NULL, AV_LOG_INFO, "linesize %d size %d height %d p %d p_end %d \n", picture->linesize[0], sizeof(uint8_t), picture->height, p, p_end);

    for (; p < p_end; p++) {
        if (pix<100) av_log(NULL, AV_LOG_INFO, "P[%d] = %d \n", pix, *p);
        pix++;
    }
    

}

static int lhe_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                             const AVFrame *frame, int *got_packet)
{
    uint8_t *component_Y, *component_U, *component_V;
    uint8_t *component_prediction, *buf, first_pixel_Y, first_pixel_U, first_pixel_V;
    int width, height;
    int image_size;
    int ret, n_bytes, n_bytes_hops_Y, n_bytes_hops_U, n_bytes_hops_V;
    LheContext *s = avctx->priv_data;

    width = (int) frame->width;
    height = (int) frame->height;  
    image_size = frame -> height * frame -> width;
    const int pix_size = frame->linesize[0]/ width;
    
    //Pointers to different color components
    component_Y = frame->data[0];
    component_U = frame->data[1];
    component_V = frame->data[2];
    
    //File size
    n_bytes_hops_Y = sizeof(uint8_t) * image_size;
    n_bytes_hops_U = sizeof(uint8_t) * image_size;
    n_bytes_hops_V = sizeof(uint8_t) * image_size;
    n_bytes = n_bytes_hops_Y + n_bytes_hops_U + n_bytes_hops_V 
              + sizeof(first_pixel_Y) + sizeof(first_pixel_U) + sizeof(first_pixel_V) 
              + sizeof(width) + sizeof(height);
        
    //ff_alloc_packet2 reserves n_bytes of memory
    if ((ret = ff_alloc_packet2(avctx, pkt, n_bytes, 0)) < 0)
        return ret;

    buf = pkt->data;
    
    //save width and height
    bytestream_put_le32(&buf, width);
    bytestream_put_le32(&buf, height);  
    
    //save first pixel color 
    first_pixel_Y = component_Y[0];
    first_pixel_U = component_U[0];
    first_pixel_V = component_V[0];

    bytestream_put_byte(&buf, first_pixel_Y);
    bytestream_put_byte(&buf, first_pixel_U);
    bytestream_put_byte(&buf, first_pixel_V);
  
    component_prediction = malloc(sizeof(uint8_t) * image_size);  
    
    //Luminance
    lhe_encode_one_hop_per_pixel(&s->prec, component_Y, component_prediction, buf, height, width, pix_size); 
    
    //Crominance U
    buf = buf + image_size;
    lhe_encode_one_hop_per_pixel(&s->prec, component_U, component_prediction, buf, height, width, pix_size); 

    //Crominance V
    buf = buf + image_size;
    lhe_encode_one_hop_per_pixel(&s->prec, component_V, component_prediction, buf, height, width, pix_size);     
    
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
    .pix_fmts       = (const enum AVPixelFormat[]){
        AV_PIX_FMT_YUV444P, AV_PIX_FMT_GRAY8, AV_PIX_FMT_NONE
    },
    .priv_class     = &lhe_class,
};
