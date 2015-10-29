/*
 * LHE Basic decoder
 */

#include "libavutil/imgutils.h"
#include "libavutil/opt.h"
#include "bytestream.h"
#include "internal.h"
#include "lhebasic.h"


typedef struct LheBasicState {
    GetByteContext gbc;

} LheBasicState;



static av_cold int lhe_basic_decode_init(AVCodecContext *avctx)
{
	LheBasicState *s = avctx->priv_data;

    return 0;
}

static int lhe_basic_decode_frame(AVCodecContext *avctx, void *data, int *got_frame, AVPacket *avpkt)
{

	LheBasicState *s = avctx->priv_data;

    return 0;
}

static av_cold int lhe_basic_decode_close(AVCodecContext *avctx)
{
	LheBasicState *s = avctx->priv_data;

    return 0;
}

static const AVOption options[] = {

};

static const AVClass decoder_class = {
    .class_name = "lhe decoder",
    .item_name  = av_default_item_name,
    .option     = options,
    .version    = LIBAVUTIL_VERSION_INT,
    .category   = AV_CLASS_CATEGORY_DECODER,
};

AVCodec ff_lhe_basic_decoder = {
    .name           = "lhe",
    .long_name      = NULL_IF_CONFIG_SMALL("LHE Basic"),
    .type           = AVMEDIA_TYPE_VIDEO,
    .id             = AV_CODEC_ID_LHE_BASIC,
    .priv_data_size = sizeof(LheBasicState),
    .init           = lhe_basic_decode_init,
    .close          = lhe_basic_decode_close,
    .decode         = lhe_basic_decode_frame,
    .priv_class     = &decoder_class,
};
