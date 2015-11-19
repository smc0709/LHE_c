/*
 * LHE Basic decoder
 */

#include "bytestream.h"
#include "internal.h"
#include "lhebasic.h"
#include "cbrt_tablegen.h"

typedef struct LheState {
    AVClass *class;  
    LheBasicPrec prec;
    AVFrame * frame;
} LheState;



static av_cold int lhe_decode_init(AVCodecContext *avctx)
{
    LheState *s = avctx->priv_data;
    avctx->pix_fmt = AV_PIX_FMT_GRAY8;

    s->frame = av_frame_alloc();
    if (!s->frame)
        return AVERROR(ENOMEM);
    
    lhe_init_cache(&s->prec);
    
    return 0;
}

static void lhe_decode_one_hop_per_pixel (AVFrame *frame, LheBasicPrec *prec, 
                                          const uint8_t *lhe_data, uint8_t first_color, 
                                          uint32_t width, uint32_t height) {
   
    uint8_t hop;
    
    //Hops computation.
    bool small_hop, last_small_hop;
    int predicted_luminance, hop_1, pix, r_max;
    
    //Errors
    int min_error;      // error of predicted signal
    int error;          //computed error for each hop 
    
    //Colin computation variables
    float hY, hYant, hYnext;
    int hop0i;
    uint8_t colin[9];
    
    small_hop           = false;
    last_small_hop      = false;        // indicates if last hop is small
    predicted_luminance = 0;            // predicted signal
    hop_1               = START_HOP_1;
    pix                 = 0;            // pixel possition, from 0 to image size        
    r_max               = PARAM_R;        
    
    uint8_t * image = (uint8_t *)frame->data[0];
 
    av_log(NULL, AV_LOG_INFO, "Width %d Height %d \n", width, height);

    for (int y=0; y < height; y++)  {
        for (int x=0; x < width; x++)     {
            
            hop = bytestream_get_byte(&lhe_data);
       
            if ((y>0) &&(x>0) && x!=width-1)
            {
                predicted_luminance=(4*image[pix-1]+3*image[pix+1-width])/7;     
            } 
            else if ((x==0) && (y>0))
            {
                predicted_luminance=image[pix-width];
                last_small_hop=false;
                hop_1=START_HOP_1;
            } 
            else if ((x==width-1) && (y>0)) 
            {
                predicted_luminance=(4*image[pix-1]+2*image[pix-width])/6;                               
            } 
            else if (y==0 && x>0) 
            {
                predicted_luminance=image[x-1];
            }
            else if (x==0 && y==0) {  
                predicted_luminance=first_color;//first pixel always is perfectly predicted! :-)  
            }   
            
            if (predicted_luminance>255) 
            {
                predicted_luminance=255;
            }
            
            if (predicted_luminance<0) 
            {
                predicted_luminance=0;  
            }
            
            //assignment of component_prediction
            //This is the uncompressed image
            image[pix]= prec -> prec_luminance[hop_1][predicted_luminance][r_max][hop];
            //av_log(NULL, AV_LOG_INFO, "Image pix[%d]= %d \n" , pix, image[pix]);

            //tunning hop1 for the next hop ( "h1 adaptation")
            //------------------------------------------------
            small_hop=false;
            if (hop<=HOP_POS_1 && hop>=HOP_NEG_1) 
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
                 
        }// for x
    }// for y
    
}

static void lhe_fill(AVFrame *picture, uint8_t color)
{
    uint8_t *p = (uint8_t *)picture->data[0];
    uint8_t *p_end = p + (picture->linesize[0] / sizeof(uint8_t)) * picture->height;

    //av_log(NULL, AV_LOG_INFO, "linesize %d size %d height %d p %p p_end %p \n", picture->linesize[0], sizeof(uint8_t), picture->height, p, p_end);

    for (; p < p_end; p++)
        *p = color;
    

}

static int lhe_decode_frame(AVCodecContext *avctx, void *data, int *got_frame, AVPacket *avpkt)
{
    uint32_t first_color, width, height;
    int ret;
    LheState *s = avctx->priv_data;
    const uint8_t *lhe_data = avpkt->data;
    int i, linesize, n;
    uint8_t color = 100;

    first_color = bytestream_get_byte(&lhe_data); 
    width  = bytestream_get_le32(&lhe_data);
    height = bytestream_get_le32(&lhe_data);
    
   // av_log(NULL, AV_LOG_INFO, "Original Color %d Width %d Height %d \n", first_color, width, height);

    avctx->width  = width;
    avctx->height  = height;    
    
    
    if ((ret = ff_get_buffer(avctx, s->frame, 0)) < 0)
        return ret;

    lhe_decode_one_hop_per_pixel(s->frame, &s->prec, lhe_data, first_color, width, height);
    
    //lhe_fill(s->frame, color);
    
    if ((ret = av_frame_ref(data, s->frame)) < 0)
        return ret;
    
    *got_frame = 1;

    return 0;
}


static av_cold int lhe_decode_close(AVCodecContext *avctx)
{
    LheState *s = avctx->priv_data;

    av_freep(&s->prec.prec_luminance);
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
