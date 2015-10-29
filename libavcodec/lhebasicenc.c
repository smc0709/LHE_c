/*
 * LHE Basic encoder
 */

/**
 * @file
 * LHE Basic encoder
 */
#include "avcodec.h"
#include "lhebasic.h"
#include "libavutil/opt.h"


typedef struct LheBasicContext {

} LheBasicContext;

static av_cold int lhe_basic_encode_init(AVCodecContext *avctx)
{
	LheBasicContext *s = avctx->priv_data;

	return 0;

}

static int lhe_basic_encode_frame(AVCodecContext *avctx, AVPacket *pkt,
                             const AVFrame *pict, int *got_packet)
{

    LheBasicContext *s = avctx->priv_data;

    return 0;

}

static int lhe_basic_encode_close(AVCodecContext *avctx)
{
    LheBasicContext *s = avctx->priv_data;

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

AVCodec ff_lhe_basic_encoder = {
    .name           = "lhe",
    .long_name      = NULL_IF_CONFIG_SMALL("LHE basic"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_LHE_BASIC,
    .priv_data_size = sizeof(LheBasicContext),
    .init           = lhe_basic_encode_init,
    .encode2        = lhe_basic_encode_frame,
    .close          = lhe_basic_encode_close,
    .priv_class     = &lhe_basic_class,
};
