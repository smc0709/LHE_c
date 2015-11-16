/*
 * LHE Basic decoder
 */

#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "bytestream.h"
#include "internal.h"
#include "lhebasic.h"

typedef struct LheState {
    AVClass *class;  
} LheState;



static av_cold int lhe_decode_init(AVCodecContext *avctx)
{
    LheState *s = avctx->priv_data;

    av_log(NULL, AV_LOG_INFO, "LHE CodingDecoding private data address %p \n", s);

    return 0;
}


static int lhe_decode_frame(AVCodecContext *avctx, void *data, int *got_frame, AVPacket *avpkt)
{
    LheState *s = avctx->priv_data;

    return 0;
}

static av_cold int lhe_decode_close(AVCodecContext *avctx)
{
    LheState *s = avctx->priv_data;

    //av_freep(&s->prec.prec_luminance);
    
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
