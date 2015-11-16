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

typedef struct LheContext {
    AVClass *class;  
} LheContext;

static av_cold int lhe_encode_init(AVCodecContext *avctx)
{
    LheContext *s = avctx->priv_data;

    return 0;

}

static int lhe_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                             const AVFrame *frame, int *got_packet)
{
    LheContext *s = avctx->priv_data;

    int ret = av_image_get_buffer_size(frame->format,
                                           frame->width, frame->height, 1);

    if (ret < 0)
	    return ret;

    if ((ret = ff_alloc_packet2(avctx, pkt, ret, ret)) < 0)
            return ret;

    if ((ret = av_image_copy_to_buffer(pkt->data, pkt->size,
                                       frame->data, frame->linesize,
                                       frame->format,
                                       frame->width, frame->height, 1)) < 0)
        return ret;

    av_log(NULL, AV_LOG_INFO, "LHE Coding...buffer size %d \n", ret);

    pkt->flags |= AV_PKT_FLAG_KEY;
    *got_packet = 1;

    return 0;

}

static int lhe_encode_close(AVCodecContext *avctx)
{
    LheContext *s = avctx->priv_data;

    return 0;

}

static const AVOption lhe_options[] = {

};

static const AVClass lhe_basic_class = {
    .class_name = "LHE Basic encoder",
    .item_name  = av_default_item_name,
    .option     = lhe_options,
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
    .priv_class     = &lhe_basic_class,
};
