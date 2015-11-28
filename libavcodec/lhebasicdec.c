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
    avctx->pix_fmt = AV_PIX_FMT_YUV444P;

    s->frame = av_frame_alloc();
    if (!s->frame)
        return AVERROR(ENOMEM);
    
    lhe_init_cache(&s->prec);
    
    return 0;
}

static uint8_t lhe_translate_symbol_into_hop (const uint8_t * lhe_data, uint8_t *hops, int pix, int pix_size, int width) {
    uint8_t symbol, hop;
    
    lhe_data+=pix;
    symbol = bytestream_get_byte(&lhe_data);

    switch (symbol) {
        case SYM_HOP_O:
            hop = HOP_0;
            break;
        case SYM_HOP_UP:
            if (pix > width) {
                hop = hops [pix_size * (pix-width)];
            }
            break;
        case SYM_HOP_POS_1:
            hop = HOP_POS_1;
            break;
        case SYM_HOP_NEG_1:
            hop = HOP_NEG_1;
            break;
        case SYM_HOP_POS_2:
            hop = HOP_POS_2;
            break;
        case SYM_HOP_NEG_2:
            hop = HOP_NEG_2;
            break;
        case SYM_HOP_POS_3:
            hop = HOP_POS_3;
            break;
        case SYM_HOP_NEG_3:
            hop = HOP_NEG_3;
            break;
        case SYM_HOP_POS_4:
            hop = HOP_POS_4;
            break;
        case SYM_HOP_NEG_4:
            hop = HOP_NEG_4;        
            break;
    }
    
    hops[pix] = hop;
    
    return hop;
}

static void lhe_decode_one_hop_per_pixel (LheBasicPrec *prec, uint8_t *hops, uint8_t *image,
                                          const uint8_t *lhe_data, uint8_t first_color, 
                                          uint32_t width, uint32_t height, int pix_size) {
       
    //Hops computation.
    bool small_hop, last_small_hop;
    uint8_t hop, predicted_luminance, hop_1, r_max; 
    int pix;
    
    //Errors
    int min_error;      // error of predicted signal
    int error;          //computed error for each hop 
    
    small_hop           = false;
    last_small_hop      = false;        // indicates if last hop is small
    predicted_luminance = 0;            // predicted signal
    hop_1               = START_HOP_1;
    pix                 = 0;            // pixel possition, from 0 to image size        
    r_max               = PARAM_R;        
    
 
    for (int y=0; y < height; y++)  {
        for (int x=0; x < width; x++)     {
            
            hop = lhe_translate_symbol_into_hop(lhe_data, hops, pix, pix_size, width);
       
            if ((y>0) &&(x>0) && x!=width-1)
            {
                predicted_luminance=(4*image[pix_size * (pix-1)]+3*image[pix_size * (pix+1-width)])/7;     
            } 
            else if ((x==0) && (y>0))
            {
                predicted_luminance=image[pix_size * (pix-width)];
                last_small_hop=false;
                hop_1=START_HOP_1;
            } 
            else if ((x==width-1) && (y>0)) 
            {
                predicted_luminance=(4*image[pix_size * (pix-1)]+2*image[pix_size * (pix-width)])/6;                               
            } 
            else if (y==0 && x>0) 
            {
                predicted_luminance=image[pix_size * (x-1)];
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
            image[pix_size * pix]= prec -> prec_luminance[hop_1][predicted_luminance][r_max][hop];
            
            if (MIDDLE_VALUE) 
            {
                image[pix_size * pix]=prec -> prec_luminance_center[hop_1][predicted_luminance][r_max][hop];

            } else 
            {
                image[pix_size * pix]=prec -> prec_luminance[hop_1][predicted_luminance][r_max][hop];
            }
            
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


static int lhe_decode_frame(AVCodecContext *avctx, void *data, int *got_frame, AVPacket *avpkt)
{
    uint32_t width, height, image_size;
    uint8_t *component_Y, *component_U, *component_V, *hops;
    uint8_t first_pixel_Y, first_pixel_U, first_pixel_V;
    int ret;
    
    LheState *s = avctx->priv_data;
    const uint8_t *lhe_data = avpkt->data;

    width  = bytestream_get_le32(&lhe_data);
    height = bytestream_get_le32(&lhe_data);
    image_size = width * height;
    
    av_log(NULL, AV_LOG_INFO, "DECODING...Width %d Height %d \n", width, height);

    first_pixel_Y = bytestream_get_byte(&lhe_data); 
    first_pixel_U = bytestream_get_byte(&lhe_data); 
    first_pixel_V = bytestream_get_byte(&lhe_data); 

    avctx->width  = width;
    avctx->height  = height;    
    
    //Allocates frame
    if ((ret = ff_get_buffer(avctx, s->frame, 0)) < 0)
        return ret;

    const int pix_size = s->frame->linesize[0]/ width;
    
    //Pointers to different color components
    component_Y = s->frame->data[0];
    component_U = s->frame->data[1];
    component_V = s->frame->data[2];
    
    //Hops array
    hops = malloc(sizeof(uint8_t) * image_size);

    //Luminance
    lhe_decode_one_hop_per_pixel(&s->prec, hops, component_Y, lhe_data, first_pixel_Y, width, height, pix_size);
    
    //Chrominance U
    lhe_data = lhe_data + image_size; 
    lhe_decode_one_hop_per_pixel(&s->prec, hops, component_U, lhe_data, first_pixel_U, width, height, pix_size);
    
    //Chrominance V
    lhe_data = lhe_data + image_size;
    lhe_decode_one_hop_per_pixel(&s->prec, hops, component_V, lhe_data, first_pixel_V, width, height, pix_size);
    
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
