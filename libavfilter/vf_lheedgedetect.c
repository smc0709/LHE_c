/*
 * Copyright (c) 2012-2014 Clément Bœsch <u pkh me>
 *
 * This file is part of FFmpeg.
 *
 * FFmpeg is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * FFmpeg is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with FFmpeg; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/**
 * @file
 * Edge detection filter with LHE interpretation.
 */

#include "libavutil/avassert.h"
#include "libavutil/opt.h"
#include "avfilter.h"
#include "formats.h"
#include "internal.h"
#include "video.h"
#include "libavcodec/lhe.h"

typedef struct LheEdgeDetectContext {
    const AVClass *class;
    int  hop_threshold; // The minimum hop (absolute value) required to trigger the edge detection.
} LheEdgeDetectContext;

#define OFFSET(x) offsetof(LheEdgeDetectContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption lheedgedetect_options[] = {
    // SYNTAX: {name, description, offset, type, default_value, min, max, flags},
    {"hop_threshold", "sets the hop threshold", OFFSET(hop_threshold), AV_OPT_TYPE_INT, {.i64=3}, 0, 4, FLAGS},
    { NULL }
};

AVFILTER_DEFINE_CLASS(lheedgedetect);

static av_cold int init(AVFilterContext *ctx)
{
    LheEdgeDetectContext *lheedgedetect = ctx->priv;

    return 0;
}

static int query_formats(AVFilterContext *ctx)
{
    const LheEdgeDetectContext *lheedgedetect = ctx->priv;

    AVFilterFormats *fmts_list;
    const enum AVPixelFormat *pix_fmts = (const enum AVPixelFormat[]){
        AV_PIX_FMT_YUV420P, 
        AV_PIX_FMT_YUV422P, 
        AV_PIX_FMT_YUV444P, 
        AV_PIX_FMT_NONE
    };

    fmts_list = ff_make_format_list(pix_fmts);
    if (!fmts_list)
        return AVERROR(ENOMEM);
    return ff_set_common_formats(ctx, fmts_list);
}

static int config_props(AVFilterLink *inlink)
{
    AVFilterContext *ctx = inlink->dst;
    LheEdgeDetectContext *lheedgedetect = ctx->priv;

    return 0;
}

// The filtering itself
static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    AVFilterContext *ctx = inlink->dst;
    LheEdgeDetectContext *lheedgedetect = ctx->priv;
    AVFilterLink *outlink = ctx->outputs[0];



















    return ff_filter_frame(outlink, in);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    LheEdgeDetectContext *lheedgedetect = ctx->priv;
}

static const AVFilterPad lheedgedetect_inputs[] = {
    {
        .name         = "default",
        .type         = AVMEDIA_TYPE_VIDEO,
        .config_props = config_props,
        .filter_frame = filter_frame,
    },
    { NULL }
};

static const AVFilterPad lheedgedetect_outputs[] = {
    {
        .name = "default",
        .type = AVMEDIA_TYPE_VIDEO,
    },
    { NULL }
};

AVFilter ff_vf_lheedgedetect = {
    .name          = "lheedgedetect",
    .description   = NULL_IF_CONFIG_SMALL("Detect and draw edge from LHE file."),
    .priv_size     = sizeof(LheEdgeDetectContext),
    .init          = init,
    .uninit        = uninit,
    .query_formats = query_formats,
    .inputs        = lheedgedetect_inputs,
    .outputs       = lheedgedetect_outputs,
    .priv_class    = &lheedgedetect_class,
    //.flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
