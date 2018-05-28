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
#include "libavcodec/avcodec.h"
#include "libavcodec/lhe.h"
#include "libavcodec/put_bits.h"

static uint8_t *intermediate_downsample_Y, *intermediate_downsample_U, *intermediate_downsample_V;
static double microsec;//, num_bloques_nulos;

/////// COPIED FROM LHEENC.C (move to lheenc.h?)
/**
 * Adapts hop_1 value depending on last hops. It is used
 * in BASIC LHE and ADVANCED LHE
 */
#define H1_ADAPTATION                                   \
    if (hop_number<=HOP_POS_1 && hop_number>=HOP_NEG_1) \
    {                                                   \
        small_hop=true;                                 \
    } else                                              \
    {                                                   \
        small_hop=false;                                \
    }                                                   \
                                                        \
    if( (small_hop) && (last_small_hop))  {             \
        hop_1=hop_1-1;                                  \
        if (hop_1<MIN_HOP_1) {                          \
            hop_1=MIN_HOP_1;                            \
        }                                               \
                                                        \
    } else {                                            \
        hop_1=MAX_HOP_1;                                \
    }                                                   \
    last_small_hop=small_hop;


/////// COPIED FROM LHEENC.C (move to lheenc.h?)
/**
 * Lhe Context
 * 
 * @p AVClass *class Pointer to AVClass
 * @p LheBasicPrec prec Caches for LHE
 * @p PutBitContext pb Params for putting bits in a file
 * @p int pr_metrics Print or not print perceptual relevance metrics
 * @p int basic_lhe Basic LHE or Advanced LHE
 */                
typedef struct LheContext {
    AVClass *class;    
    LheBasicPrec prec;
    PutBitContext pb;
    LheProcessing procY;
    LheProcessing procUV;
    LheImage lheY;
    LheImage lheU;
    LheImage lheV;
    uint8_t chroma_factor_width;
    uint8_t chroma_factor_height;
    bool pr_metrics;
    bool basic_lhe;
    int ql;
    int down_mode;
    uint16_t dif_frames_count;
    int skip_frames;
    Prot_Rectangle protected_rectangles[MAX_RECTANGLES];
    uint8_t down_mode_p;
    int down_mode_reconf;
    bool color;
    bool pr_metrics_active;
    int ql_reconf;
    int skip_frames_reconf;
    Prot_Rectangle protected_rectangles_reconf[MAX_RECTANGLES];
    uint8_t down_mode_p_reconf;
    bool color_reconf;
    bool pr_metrics_active_reconf;
    uint8_t gop_reconf;
} LheContext;


typedef struct LheEdgeDetectContext {
    const AVClass *class;
    LheContext *lhe_ctx; //The lhe context (definition copied from the lheenc.c)
    int  hop_threshold; // The minimum hop (absolute value) required to trigger the edge detection.
    AVCodec *codec; // The codec used??? is this really needed?
} LheEdgeDetectContext;


/////// COPIED FROM LHEENC.C (move to lheenc.h?)
static int lhe_free_tables(LheEdgeDetectContext *led_ctx, AVFrame *in) // cambiados parametros (AVCodecContext *ctx)
{

    uint32_t total_blocks_height, pixels_block;

    LheContext *s = led_ctx->lhe_ctx;

    pixels_block = in->width / HORIZONTAL_BLOCKS;
    total_blocks_height = in->height / pixels_block;

    av_free((&s->lheY)->buffer3);//component_prediction);
    av_free((&s->lheU)->buffer3);//component_prediction);
    av_free((&s->lheV)->buffer3);//component_prediction);
    av_free((&s->lheY)->hops);
    av_free((&s->lheU)->hops);
    av_free((&s->lheV)->hops);
    av_free((&s->lheY)->first_color_block);
    av_free((&s->lheU)->first_color_block);
    av_free((&s->lheV)->first_color_block);

    for (int i=0; i < total_blocks_height; i++)
    {
        av_free((&s->procY)->basic_block[i]);
    }

    av_free((&s->procY)->basic_block);
        
    for (int i=0; i < total_blocks_height; i++)
    {
        av_free((&s->procUV)->basic_block[i]);
    }

    av_free((&s->procUV)->basic_block);

    if (strcmp((led_ctx->codec)->name, "lhe") == 0) {

        if (s->basic_lhe == 0) {
            
            av_free(intermediate_downsample_Y);
            av_free(intermediate_downsample_U);
            av_free(intermediate_downsample_V);

            for (int i=0; i < total_blocks_height; i++)
            {
                av_free((&s->procY)->perceptual_relevance_x[i]);
            }

            av_free((&s->procY)->perceptual_relevance_x);
            
            for (int i=0; i < total_blocks_height; i++)
            {
                av_free((&s->procY)->perceptual_relevance_y[i]);
            }

            av_free((&s->procY)->perceptual_relevance_y);
        
            //Advanced blocks            
            for (int i=0; i < total_blocks_height; i++)
            {
                av_free((&s->procY)->advanced_block[i]);
            }

            av_free((&s->procY)->advanced_block);
            
            for (int i=0; i < total_blocks_height; i++)
            {
                av_free((&s->procUV)->advanced_block[i]);
            }

            av_free((&s->procUV)->advanced_block);
            
            av_free((&s->lheY)->downsampled_image);
            av_free((&s->lheU)->downsampled_image);
            av_free((&s->lheV)->downsampled_image);

        }

    } else if (strcmp((led_ctx->codec)->name, "mlhe") == 0) {

        av_free((&s->lheY)->delta);
        av_free((&s->lheU)->delta);
        av_free((&s->lheV)->delta);

        av_free(intermediate_downsample_Y);
        av_free(intermediate_downsample_U);
        av_free(intermediate_downsample_V);
            
        for (int i=0; i < total_blocks_height; i++)
        {
            av_free((&s->procY)->perceptual_relevance_x[i]);
        }

        av_free((&s->procY)->perceptual_relevance_x);
        
        for (int i=0; i < total_blocks_height; i++)
        {
            av_free((&s->procY)->perceptual_relevance_y[i]);
        }

        av_free((&s->procY)->perceptual_relevance_y);
    
        //Advanced blocks
        for (int i=0; i < total_blocks_height; i++)
        {
            av_free((&s->procY)->buffer_advanced_block[i]);
        }

        av_free((&s->procY)->buffer_advanced_block);
        
        for (int i=0; i < total_blocks_height; i++)
        {
            av_free((&s->procUV)->buffer_advanced_block[i]);
        }

        av_free((&s->procUV)->buffer_advanced_block);

        av_free((&s->lheY)->downsampled_image);
        av_free((&s->lheU)->downsampled_image);
        av_free((&s->lheV)->downsampled_image);

        //av_free((&s->lheY)->last_downsampled_image);
        //av_free((&s->lheU)->last_downsampled_image);
        //av_free((&s->lheV)->last_downsampled_image);

        //av_free((&s->lheY)->downsampled_player_image);
        //av_free((&s->lheU)->downsampled_player_image);
        //av_free((&s->lheV)->downsampled_player_image); 

        av_free((&s->lheY)->buffer1);
        av_free((&s->lheU)->buffer1);
        av_free((&s->lheV)->buffer1);

        av_free((&s->lheY)->buffer2);
        av_free((&s->lheU)->buffer2);
        av_free((&s->lheV)->buffer2);

        for (int i=0; i < total_blocks_height; i++)
        {
            av_free((&s->procY)->buffer1_advanced_block[i]);
        }

        av_free((&s->procY)->buffer1_advanced_block);
        
        for (int i=0; i < total_blocks_height; i++)
        {
            av_free((&s->procUV)->buffer1_advanced_block[i]);
        }

        av_free((&s->procUV)->buffer1_advanced_block);
    }
    return 0;
}


/////// COPIED FROM LHEENC.C (move to lheenc.h?)
static int lhe_alloc_tables(LheEdgeDetectContext *led_ctx, AVFrame *in) // cambiados parametros (AVCodecContext *ctx, LheContext *s)
{
    LheContext *s = led_ctx->lhe_ctx; //line added, the rest stays unchanged except the parameters


    uint32_t image_size_Y, image_size_UV;
    uint32_t total_blocks_width, total_blocks_height, pixels_block;

    image_size_Y = in->width * in->height;
    image_size_UV = in->width/s->chroma_factor_width * in->height/s->chroma_factor_height;

    total_blocks_width = HORIZONTAL_BLOCKS;
    pixels_block = in->width / HORIZONTAL_BLOCKS;
    total_blocks_height = in->height / pixels_block;

    FF_ALLOC_ARRAY_OR_GOTO(s, (&s->lheY)->hops, image_size_Y, sizeof(uint8_t), fail);
    FF_ALLOC_ARRAY_OR_GOTO(s, (&s->lheU)->hops, image_size_UV, sizeof(uint8_t), fail);
    FF_ALLOC_ARRAY_OR_GOTO(s, (&s->lheV)->hops, image_size_UV, sizeof(uint8_t), fail);
    FF_ALLOC_ARRAY_OR_GOTO(s, (&s->lheY)->first_color_block, image_size_Y, sizeof(uint8_t), fail);
    FF_ALLOC_ARRAY_OR_GOTO(s, (&s->lheU)->first_color_block, image_size_UV, sizeof(uint8_t), fail);
    FF_ALLOC_ARRAY_OR_GOTO(s, (&s->lheV)->first_color_block, image_size_UV, sizeof(uint8_t), fail);

    FF_ALLOC_ARRAY_OR_GOTO(s, (&s->procY)->basic_block, total_blocks_height, sizeof(BasicLheBlock *), fail);
        
    for (int i=0; i < total_blocks_height; i++)
    {
        FF_ALLOC_ARRAY_OR_GOTO(s, (&s->procY)->basic_block[i], total_blocks_width, sizeof(BasicLheBlock), fail);
    }
        
    FF_ALLOC_ARRAY_OR_GOTO(s, (&s->procUV)->basic_block, total_blocks_height, sizeof(BasicLheBlock *), fail);
        
    for (int i=0; i < total_blocks_height; i++)
    {
        FF_ALLOC_ARRAY_OR_GOTO(s, (&s->procUV)->basic_block[i], total_blocks_width, sizeof(BasicLheBlock), fail);
    }

    if (strcmp((led_ctx->codec)->name, "lhe") == 0) {

        FF_ALLOC_ARRAY_OR_GOTO(s, (&s->lheY)->component_prediction, image_size_Y, sizeof(uint8_t), fail);
        FF_ALLOC_ARRAY_OR_GOTO(s, (&s->lheU)->component_prediction, image_size_UV, sizeof(uint8_t), fail);
        FF_ALLOC_ARRAY_OR_GOTO(s, (&s->lheV)->component_prediction, image_size_UV, sizeof(uint8_t), fail); 

        if (s->basic_lhe == 0) {

            FF_ALLOC_ARRAY_OR_GOTO(s, intermediate_downsample_Y, image_size_Y, sizeof(uint8_t), fail);
            FF_ALLOC_ARRAY_OR_GOTO(s, intermediate_downsample_U, image_size_UV, sizeof(uint8_t), fail);
            FF_ALLOC_ARRAY_OR_GOTO(s, intermediate_downsample_V, image_size_UV, sizeof(uint8_t), fail);
            
            FF_ALLOC_ARRAY_OR_GOTO(s, (&s->procY)->perceptual_relevance_x, (total_blocks_height+1), sizeof(float*), fail); 
        
            for (int i=0; i<total_blocks_height+1; i++) 
            {
                FF_ALLOC_ARRAY_OR_GOTO(s, (&s->procY)->perceptual_relevance_x[i], (total_blocks_width+1), sizeof(float), fail);
            }
            
            FF_ALLOC_ARRAY_OR_GOTO(s, (&s->procY)->perceptual_relevance_y, (total_blocks_height+1), sizeof(float*), fail); 
        
            for (int i=0; i<total_blocks_height+1; i++) 
            {
                FF_ALLOC_ARRAY_OR_GOTO(s, (&s->procY)->perceptual_relevance_y[i], (total_blocks_width+1), sizeof(float), fail);
            }
        
            //Advanced blocks
            FF_ALLOC_ARRAY_OR_GOTO(s, (&s->procY)->advanced_block, total_blocks_height, sizeof(AdvancedLheBlock *), fail); 
            
            for (int i=0; i < total_blocks_height; i++)
            {
                FF_ALLOC_ARRAY_OR_GOTO(s, (&s->procY)->advanced_block[i], total_blocks_width, sizeof(AdvancedLheBlock), fail); 
            }
            
            FF_ALLOC_ARRAY_OR_GOTO(s, (&s->procUV)->advanced_block, total_blocks_height, sizeof(AdvancedLheBlock *), fail); 
            
            for (int i=0; i < total_blocks_height; i++)
            {
                FF_ALLOC_ARRAY_OR_GOTO(s, (&s->procUV)->advanced_block[i], total_blocks_width, sizeof(AdvancedLheBlock), fail); 
            }
            
            FF_ALLOC_ARRAY_OR_GOTO(s, (&s->lheY)->downsampled_image, image_size_Y, sizeof(uint8_t), fail); 
            FF_ALLOC_ARRAY_OR_GOTO(s, (&s->lheU)->downsampled_image, image_size_UV, sizeof(uint8_t), fail); 
            FF_ALLOC_ARRAY_OR_GOTO(s, (&s->lheV)->downsampled_image, image_size_UV, sizeof(uint8_t), fail);

        }

    } else if (strcmp((led_ctx->codec)->name, "mlhe") == 0) {
        
        FF_ALLOC_ARRAY_OR_GOTO(s, (&s->lheY)->delta, image_size_Y, sizeof(uint8_t), fail);
        FF_ALLOC_ARRAY_OR_GOTO(s, (&s->lheU)->delta, image_size_UV, sizeof(uint8_t), fail);
        FF_ALLOC_ARRAY_OR_GOTO(s, (&s->lheV)->delta, image_size_UV, sizeof(uint8_t), fail);

        FF_ALLOC_ARRAY_OR_GOTO(s, intermediate_downsample_Y, image_size_Y, sizeof(uint8_t), fail);
        FF_ALLOC_ARRAY_OR_GOTO(s, intermediate_downsample_U, image_size_UV, sizeof(uint8_t), fail);
        FF_ALLOC_ARRAY_OR_GOTO(s, intermediate_downsample_V, image_size_UV, sizeof(uint8_t), fail);
            
        FF_ALLOC_ARRAY_OR_GOTO(s, (&s->procY)->perceptual_relevance_x, (total_blocks_height+1), sizeof(float*), fail); 
        
        for (int i=0; i<total_blocks_height+1; i++) 
        {
            FF_ALLOC_ARRAY_OR_GOTO(s, (&s->procY)->perceptual_relevance_x[i], (total_blocks_width+1), sizeof(float), fail);
        }
            
        FF_ALLOC_ARRAY_OR_GOTO(s, (&s->procY)->perceptual_relevance_y, (total_blocks_height+1), sizeof(float*), fail); 
        
        for (int i=0; i<total_blocks_height+1; i++) 
        {
            FF_ALLOC_ARRAY_OR_GOTO(s, (&s->procY)->perceptual_relevance_y[i], (total_blocks_width+1), sizeof(float), fail);
        }
        
        //Advanced blocks
        FF_ALLOC_ARRAY_OR_GOTO(s, (&s->procY)->buffer_advanced_block, total_blocks_height, sizeof(AdvancedLheBlock *), fail); 
            
        for (int i=0; i < total_blocks_height; i++)
        {
            FF_ALLOC_ARRAY_OR_GOTO(s, (&s->procY)->buffer_advanced_block[i], total_blocks_width, sizeof(AdvancedLheBlock), fail); 
        }
        
        FF_ALLOC_ARRAY_OR_GOTO(s, (&s->procUV)->buffer_advanced_block, total_blocks_height, sizeof(AdvancedLheBlock *), fail); 
        
        for (int i=0; i < total_blocks_height; i++)
        {
            FF_ALLOC_ARRAY_OR_GOTO(s, (&s->procUV)->buffer_advanced_block[i], total_blocks_width, sizeof(AdvancedLheBlock), fail); 
        } 
            
        FF_ALLOC_ARRAY_OR_GOTO(s, (&s->lheY)->downsampled_image, image_size_Y, sizeof(uint8_t), fail); 
        FF_ALLOC_ARRAY_OR_GOTO(s, (&s->lheU)->downsampled_image, image_size_UV, sizeof(uint8_t), fail); 
        FF_ALLOC_ARRAY_OR_GOTO(s, (&s->lheV)->downsampled_image, image_size_UV, sizeof(uint8_t), fail); 

        FF_ALLOC_ARRAY_OR_GOTO(s, (&s->lheY)->buffer1, image_size_Y, sizeof(uint8_t), fail); 
        FF_ALLOC_ARRAY_OR_GOTO(s, (&s->lheU)->buffer1, image_size_UV, sizeof(uint8_t), fail); 
        FF_ALLOC_ARRAY_OR_GOTO(s, (&s->lheV)->buffer1, image_size_UV, sizeof(uint8_t), fail);

        FF_ALLOC_ARRAY_OR_GOTO(s, (&s->lheY)->buffer2, image_size_Y, sizeof(uint8_t), fail); 
        FF_ALLOC_ARRAY_OR_GOTO(s, (&s->lheU)->buffer2, image_size_UV, sizeof(uint8_t), fail); 
        FF_ALLOC_ARRAY_OR_GOTO(s, (&s->lheV)->buffer2, image_size_UV, sizeof(uint8_t), fail);
        
        FF_ALLOC_ARRAY_OR_GOTO(s, (&s->lheY)->buffer3, image_size_Y, sizeof(uint8_t), fail);
        FF_ALLOC_ARRAY_OR_GOTO(s, (&s->lheU)->buffer3, image_size_UV, sizeof(uint8_t), fail);
        FF_ALLOC_ARRAY_OR_GOTO(s, (&s->lheV)->buffer3, image_size_UV, sizeof(uint8_t), fail);
         
        //FF_ALLOC_ARRAY_OR_GOTO(s, (&s->lheY)->last_downsampled_image, image_size_Y, sizeof(uint8_t), fail); 
        //FF_ALLOC_ARRAY_OR_GOTO(s, (&s->lheU)->last_downsampled_image, image_size_UV, sizeof(uint8_t), fail); 
        //FF_ALLOC_ARRAY_OR_GOTO(s, (&s->lheV)->last_downsampled_image, image_size_UV, sizeof(uint8_t), fail);

        //FF_ALLOC_ARRAY_OR_GOTO(s, (&s->lheY)->downsampled_player_image, image_size_Y, sizeof(uint8_t), fail);
        //FF_ALLOC_ARRAY_OR_GOTO(s, (&s->lheU)->downsampled_player_image, image_size_Y, sizeof(uint8_t), fail); 
        //FF_ALLOC_ARRAY_OR_GOTO(s, (&s->lheV)->downsampled_player_image, image_size_Y, sizeof(uint8_t), fail); 

        FF_ALLOC_ARRAY_OR_GOTO(s, (&s->procY)->buffer1_advanced_block, total_blocks_height, sizeof(AdvancedLheBlock *), fail); 
            
        for (int i=0; i < total_blocks_height; i++)
        {
            FF_ALLOC_ARRAY_OR_GOTO(s, (&s->procY)->buffer1_advanced_block[i], total_blocks_width, sizeof(AdvancedLheBlock), fail); 
        }
        
        FF_ALLOC_ARRAY_OR_GOTO(s, (&s->procUV)->buffer1_advanced_block, total_blocks_height, sizeof(AdvancedLheBlock *), fail); 
        
        for (int i=0; i < total_blocks_height; i++)
        {
            FF_ALLOC_ARRAY_OR_GOTO(s, (&s->procUV)->buffer1_advanced_block[i], total_blocks_width, sizeof(AdvancedLheBlock), fail); 
        } 

    } else {
        goto fail;
    }

    return 0;

    fail:
        lhe_free_tables(led_ctx, in);
        return AVERROR(ENOMEM);
}



/////// END OF COPIED FUNCTIONS FROM LHEENC.C ////////




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
	LheEdgeDetectContext *led_ctx = ctx->priv;
	led_ctx->lhe_ctx = av_calloc(1, sizeof(LheContext));

	return 0;
}

static int query_formats(AVFilterContext *ctx)
{
	//const LheEdgeDetectContext *led_ctx = ctx->priv;

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

/*static int config_props(AVFilterLink *inlink)
{
	AVFilterContext *ctx = inlink->dst;
	LheEdgeDetectContext *led_ctx = ctx->priv;

	return 0;
}
*/

static void config_ctx(LheEdgeDetectContext *led_ctx, AVFrame *in){ //// cambiados parametros (AVCodecContext avctx)
	LheContext *s = led_ctx->lhe_ctx;
	
	//s->ql_reconf = -1;
    s->down_mode_reconf = -1;
    s->color_reconf = true;
    s->down_mode_p_reconf = -1;
    for (int i = 0; i < MAX_RECTANGLES; i++){
        s->protected_rectangles_reconf[i].active = false;    
    }
    s->pr_metrics_active_reconf = false;
    s->skip_frames_reconf = -1;
    //s->gop_reconf = -1;


    /*s->protected_rectangles_reconf[0].active = true;
    s->protected_rectangles_reconf[0].xini = -50;
    s->protected_rectangles_reconf[0].xfin = 800;
    s->protected_rectangles_reconf[0].yini = -50;
    s->protected_rectangles_reconf[0].yfin = 500;
    s->protected_rectangles_reconf[0].protection = false;

    s->protected_rectangles_reconf[1].active = true;
    s->protected_rectangles_reconf[1].xini = 300;
    s->protected_rectangles_reconf[1].xfin = 500;
    s->protected_rectangles_reconf[1].yini = 200;
    s->protected_rectangles_reconf[1].yfin = 300;
    s->protected_rectangles_reconf[1].protection = true;
*/
    
    uint32_t total_blocks_width, pixels_block, total_blocks_height;
    //uint8_t pixel_format;

    if (in->format == AV_PIX_FMT_YUV420P)
    {
        s->chroma_factor_width = 2;
        s->chroma_factor_height = 2;
    } else if (in->format == AV_PIX_FMT_YUV422P) 
    {
        s->chroma_factor_width = 2;
        s->chroma_factor_height = 1;
    } else if (in->format == AV_PIX_FMT_YUV444P) 
    {
        s->chroma_factor_width = 1;
        s->chroma_factor_height = 1;
    }

    total_blocks_width = HORIZONTAL_BLOCKS;
    pixels_block = in->width / HORIZONTAL_BLOCKS;
    total_blocks_height = in->height / pixels_block;

    (&s->procY)->width = in->width;
    (&s->procY)->height =  in->height; 

    (&s->procUV)->width = ((&s->procY)->width - 1)/s->chroma_factor_width + 1;
    (&s->procUV)->height = ((&s->procY)->height - 1)/s->chroma_factor_height + 1;

    (&s->procY)->pr_factor = (&s->procY)->width/128;
    if ((&s->procY)->pr_factor == 0) (&s->procY)->pr_factor = 1;

    (&s->procY)->theoretical_block_width = (&s->procY)->width / total_blocks_width;
    (&s->procY)->theoretical_block_height = (&s->procY)->height / total_blocks_height;       

    (&s->procUV)->theoretical_block_width = (&s->procUV)->width / total_blocks_width;
    (&s->procUV)->theoretical_block_height = (&s->procUV)->height / total_blocks_height;

    lhe_init_cache2(&s->prec);
    lhe_alloc_tables(led_ctx, in);

    for (int block_y=0; block_y<total_blocks_height; block_y++)      
    {  
        for (int block_x=0; block_x<total_blocks_width; block_x++) 
        {
            lhe_calculate_block_coordinates (&s->procY, &s->procUV,
                                             total_blocks_width, total_blocks_height,
                                             block_x, block_y);
        }
    }

    s->dif_frames_count = s->gop_reconf;

    microsec = 0;
    //num_bloques_nulos = 0;

}


// Filters a frame given it and the link between this filter and the previous one
static int filter_frame(AVFilterLink *inlink, AVFrame *in)
{
    int direct;
    AVFrame *out;

	AVFilterContext *ctx = inlink->dst;
	LheEdgeDetectContext *led_ctx = ctx->priv;
	AVFilterLink *outlink = ctx->outputs[0];

	config_ctx(led_ctx, in);


	// Check if AVFrame parameter is writable.
		// If it is, configs the next part to edit it
		// If not, reserves space for an equal size one
	direct = 0;
	if (av_frame_is_writable(in)) {
		direct = 1;
		out = in;
	} else {
		out = ff_get_video_buffer(outlink, outlink->w, outlink->h);
		if (!out) {
			av_frame_free(&in);
			return AVERROR(ENOMEM);
		}
		av_frame_copy_props(out, in);
	}

	///// THE PROCESSING ITSELF /////
	// out->data[...] = foobar(in->data[...])
	//LheContext *s = led_ctx->lhe_ctx;







	// Outputs the processed frame to the next filter
	if (!direct) //only frees input if it is not the output
		av_frame_free(&in);
	return ff_filter_frame(outlink, out);
}

static av_cold void uninit(AVFilterContext *ctx)
{
    LheEdgeDetectContext *led_ctx = ctx->priv;
    av_free(led_ctx->lhe_ctx);
    av_free(led_ctx);
}


static const AVFilterPad lheedgedetect_inputs[] = {
	{
		.name         = "default",
		.type         = AVMEDIA_TYPE_VIDEO,
		//.config_props = config_props,
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
	.description   = NULL_IF_CONFIG_SMALL("Detect and draw edges with LHE kernel."),
	.priv_size     = sizeof(LheEdgeDetectContext),
	.init          = init,
	.uninit        = uninit,
	.query_formats = query_formats,
	.inputs        = lheedgedetect_inputs,
	.outputs       = lheedgedetect_outputs,
	.priv_class    = &lheedgedetect_class,
	//.flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
