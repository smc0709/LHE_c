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
 * Edge detection filter with LHE hop interpretation.
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
//#include <stdio.h>

static uint8_t *intermediate_downsample_Y, *intermediate_downsample_U, *intermediate_downsample_V;
static double microsec;//, num_bloques_nulos;

/////// COPIED FROM LHEENC.C (move to lheenc.h?)
/**
 * Adapts hop_1 value depending on last hops. It is used
 * in BASIC LHE and ADVANCED LHE
 */
/*#define H1_ADAPTATION                                   \
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
*/

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
    char *codec_name; // The codec used
    int gop_size; // The GOP size
    int height;
    int width;
    bool basic_lhe;
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

    if (strcmp(led_ctx->codec_name, "lhe") == 0) {

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

    } else if (strcmp(led_ctx->codec_name, "mlhe") == 0) {

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

    if (strcmp(led_ctx->codec_name, "lhe") == 0) {

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

    } else if (strcmp(led_ctx->codec_name, "mlhe") == 0) {
        
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

/*
/////// COPIED FROM LHEENC.C (move to lheenc.h?)
static void mlhe_reconfig (LheEdgeDetectContext *led_ctx, AVFrame *in) // cambiados parametros (AVCodecContext *avctx, LheContext *s)
{
    if (s->ql_reconf != -1 && s->ql != s->ql_reconf)
        s->ql = s->ql_reconf;
    if (s->down_mode_reconf != -1 && s->down_mode != s->down_mode_reconf)
        s->down_mode = s->down_mode_reconf;
    if (s->down_mode_p_reconf != -1 && s->down_mode_p != s->down_mode_p_reconf)
        s->down_mode_p = s->down_mode_p_reconf;

    if (s->color_reconf != -1 && s->color != s->color_reconf)
        s->color = s->color_reconf;

    for (int i = 0; i < MAX_RECTANGLES; i++) {
            s->protected_rectangles[i] = s->protected_rectangles_reconf[i];
    }

    if (s->pr_metrics_active != s->pr_metrics_active_reconf)
        s->pr_metrics_active = s->pr_metrics_active_reconf;
    if (s->skip_frames_reconf != -1 && s->skip_frames != s->skip_frames_reconf)
        s->skip_frames = s->skip_frames_reconf;
    if (s->gop_reconf != -1 && led_ctx->gop_size != s->gop_reconf)
        led_ctx->gop_size = s->gop_reconf;

}*/



/////// COPIED FROM LHEENC.C (move to lheenc.h?)     NOTHING CHANGED
static void lhe_advanced_encode_block2_sequential (LheBasicPrec *prec, LheProcessing *proc, LheImage *lhe, uint8_t *original_data,
                                       int total_blocks_width, int block_x, int block_y, int lhe_type, int linesize)
{

    int h1, emin, error, dif_line, dif_pix, pix, pix_original_data, soft_counter, soft_threshold;
    int oc, hop0, quantum, hop_value, hop_number, prev_color;
    bool last_small_hop, small_hop, soft_mode;
    int xini, xfin, yini, yfin, num_block, soft_h1, grad;
    uint8_t *component_original_data, *component_prediction, *hops;
    const int max_h1 = 10;
    const int min_h1 = 4;
    const int start_h1 = min_h1;//(max_h1+min_h1)/2;

    //soft_counter = 0;
    //soft_threshold = 8;
    //soft_mode = false;
    //soft_h1 = 2;
    grad = 0;
    
    //gettimeofday(&before , NULL);
    //for (int i = 0; i < 5000; i++){

    xini = proc->basic_block[block_y][block_x].x_ini;
    yini = proc->basic_block[block_y][block_x].y_ini;

    if (lhe_type == DELTA_MLHE) {
        xfin = proc->advanced_block[block_y][block_x].x_fin_downsampled;    
        yfin = proc->advanced_block[block_y][block_x].y_fin_downsampled;
        component_original_data = lhe->delta;
        dif_line = proc->width - xfin + xini;
        dif_pix = dif_line;
        pix_original_data = yini*proc->width + xini;
        pix = yini*proc->width + xini;
    } else if (lhe_type == ADVANCED_LHE) {        
        xfin = proc->advanced_block[block_y][block_x].x_fin_downsampled;    
        yfin = proc->advanced_block[block_y][block_x].y_fin_downsampled;
        component_original_data = lhe->downsampled_image;
        dif_line = proc->width - xfin + xini;
        dif_pix = dif_line;
        pix_original_data = yini*proc->width + xini;
        pix = yini*proc->width + xini;
    } else { //BASIC_LHE
        xfin = proc->basic_block[block_y][block_x].x_fin;
        yfin = proc->basic_block[block_y][block_x].y_fin;
        component_original_data = original_data;
        dif_line = linesize - xfin + xini;
        dif_pix = proc->width - xfin + xini;
        pix_original_data = yini*linesize + xini;
        pix = yini*proc->width + xini;
    }

    int ratioY = 1;
    if (block_x > 0){
        ratioY = 1000*(proc->advanced_block[block_y][block_x-1].y_fin_downsampled - proc->basic_block[block_y][block_x-1].y_ini)/(yfin - yini);
    }

    int ratioX = 1;
    if (block_y > 0){
        ratioX = 1000*(proc->advanced_block[block_y-1][block_x].x_fin_downsampled - proc->basic_block[block_y-1][block_x].x_ini)/(xfin - xini);
    }

    component_prediction = lhe->component_prediction;
    hops = lhe->hops;
    h1 = start_h1;
    last_small_hop = true;//true; //last hop was small
    small_hop = false;//true;//current hop is small
    emin = 255;//error min
    error = 0;//computed error
    hop0 = 0; //prediction
    hop_value = 0;//data from cache
    hop_number = 4;// final assigned hop
    num_block = block_y * total_blocks_width + block_x;
    //if (block_x > 0 && block_y > 0) oc = (component_prediction[yini*proc->width+proc->advanced_block[block_y][block_x-1].x_fin_downsampled-1]+component_prediction[(proc->advanced_block[block_y-1][block_x].y_fin_downsampled-1)*proc->width+xini])/2;///Habria que tener tambien en cuenta el superior derecho(mejora)
    oc = component_original_data[pix_original_data];//original color
    //av_log(NULL,AV_LOG_INFO, "num_block: %d, first_color_block: %d\n", num_block, oc);
    if (num_block == 0) lhe->first_color_block[num_block]=oc;
    if (block_x == 0 && block_y == 0) prev_color = oc;
    else if (block_x == 0) prev_color = component_prediction[(proc->advanced_block[block_y-1][block_x].y_fin_downsampled-1)*proc->width+xini];
    else if (block_y == 0) prev_color = component_prediction[yini*proc->width + proc->advanced_block[block_y][block_x-1].x_fin_downsampled-1];
    else prev_color = (component_prediction[yini*proc->width + proc->advanced_block[block_y][block_x-1].x_fin_downsampled-1]+component_prediction[(proc->advanced_block[block_y-1][block_x].y_fin_downsampled-1)*proc->width+xini])/2;
    quantum = oc; //final quantum asigned value
   
    //bool nulos = true;

    for (int y = yini; y < yfin; y++) {
        int y_prev = ((y-yini)*ratioY/1000)+yini;
        for (int x=xini;x<xfin;x++) {
            int x_prev = ((x-xini)*ratioX/1000)+xini;
            // --------------------- PHASE 1: PREDICTION-------------------------------
            oc=component_original_data[pix_original_data];//original color
            if (y>yini && x>xini && x<(xfin-1)) { //Interior del bloque
                hop0=(prev_color+component_prediction[pix-proc->width+1])>>1;
            } else if (x==xini && y>yini) { //Lateral izquierdo
                if (x > 0) hop0=(component_prediction[y_prev*proc->width + proc->advanced_block[block_y][block_x-1].x_fin_downsampled-1]+component_prediction[pix-proc->width+1])/2;
                else hop0=component_prediction[pix-proc->width];
                last_small_hop = true;
                h1 = start_h1;
                //soft_counter = 0;
                //soft_mode = true;
            } else if (y == yini) { //Lateral superior y pixel inicial
                //hop0=prev_color;
                if (y >0 && x != xini) hop0=(prev_color + component_prediction[(proc->advanced_block[block_y-1][block_x].y_fin_downsampled-1)*proc->width+x_prev])/2;
                else hop0=prev_color;
                //if (y > 0) av_log (NULL, AV_LOG_INFO, "pix %d, pred_lum %d, delta_pred pix-1: %d, delta_pred anterior: %d\n", pix, hop0, prev_color, component_prediction[(proc->advanced_block[block_y-1][block_x].y_fin_downsampled-1)*proc->width+x_prev]);
            } else { //Lateral derecho
                hop0=(prev_color+component_prediction[pix-proc->width])>>1;
            }

            if (lhe_type != DELTA_MLHE)
            {
                hop0 = hop0 + grad;
                if (hop0 > 255) hop0 = 255;
                else if (hop0 < 0) hop0 = 0;
            }
            
            //-------------------------PHASE 2: HOPS COMPUTATION-------------------------------
            hop_number = 4;// prediction corresponds with hop_number=4
            quantum = hop0;//this is the initial predicted quantum, the value of prediction
            small_hop = true;//i supossed initially that hop will be small (3,4,5)
            emin = oc-hop0 ; 
            if (emin<0) emin=-emin;//minimum error achieved
            if (emin>h1/2) { //only enter in computation if emin>threshold
                //positive hops
                if (oc>hop0) {
                    //case hop0 (most frequent)
                    if ((quantum +h1)>255) goto phase3;
                    //case hop1 (frequent)
                    error=emin-h1;
                    if (error<0) error=-error;
                    if (error<emin){
                        hop_number=5;
                        emin=error;
                        quantum+=h1;
                        //f (emin<4) goto phase3;
                    } else goto phase3;
                    // case hops 6 to 8 (less frequent)
                    //if (soft_mode) h1 = min_h1;
                    for (int i=3;i<6;i++){
                        //cache de 5KB simetrica
                        //if (!lineal_mode) 
                        hop_value=255-prec->cache_hops[255-hop0][h1-4][5-i];//indexes are 2 to 0
                        //else hop_value = hop0+2*(i-1);
                        //if (hop_value>255) hop_value=255;

                        error=oc-hop_value;
                        if (error<0) error=-error;
                        if (error<emin){
                            hop_number=i+3;
                            emin=error;
                            quantum=hop_value;
                            //if (emin<4) break;// go to phase 3
                        } else break;
                    }
                }
                //negative hops
                else {
                    //case hop0 (most frequent)
                    if ((quantum - h1)<0)    goto phase3;
                    //case hop1 (frequent)
                    error=emin-h1;
                    if (error<0) error=-error;
                    if (error<emin) {
                        hop_number=3;
                        emin=error;
                        quantum-=h1;
                        //if (emin<4) goto phase3;
                    } else goto phase3;
                    // case hops 2 to 0 (less frequent)
                    //if (soft_mode) h1 = min_h1;
                    for (int i=2;i>=0;i--) {
                       // if (!lineal_mode)
                        hop_value=prec->cache_hops[hop0][h1-4][i];//indexes are 2 to 0
                        //else hop_value = hop0 - 2*(4-i);
                        //if (hop_value<0) hop_value=0;
                        error=hop_value-oc;
                        if (error<0) error=-error;
                        if (error<emin) {
                            hop_number=i;
                            emin=error;
                            quantum=hop_value;
                            //if (emin<4) break;// go to phase 3
                        } else break;
                    }
                }
            }//endif emin
            //if (soft_mode) h1 = soft_h1;
            //------------- PHASE 3: assignment of final quantized value --------------------------
            phase3:    
            component_prediction[pix]=quantum;
            prev_color=quantum;
            hops[pix]=hop_number;

            //tunning grad for next pixel
            //if (hop_number != 4){
                
            //}

            //------------- PHASE 4: h1 logic  --------------------------
            if (hop_number>5 || hop_number<3) small_hop=false; //true by default
            //if(!soft_mode){
                //if (hop_number>5 || hop_number<3) small_hop=false; //true by default
            if (small_hop==true && last_small_hop==true) {
                if (h1>min_h1) h1--;
            } else {
                h1=max_h1;
            }
            //}
            //h1=2;
            last_small_hop=small_hop;

            if (hop_number == 5) grad = 1;
            else if (hop_number == 3) grad = -1;
            else if (!small_hop) grad = 0;

            //Soft mode logic
            


            /*
            if (soft_mode && !small_hop) {
                soft_counter = 0;
                soft_mode = false;
                h1 = max_h1;
            } else if (!soft_mode) {
                if (small_hop) {
                    soft_counter++;
                    if (soft_counter == soft_threshold) {
                        //oft_mode = true;
                        h1 = soft_h1;
                    }
                } else {
                    soft_counter = 0;
                }
            }*/




            pix++;
            pix_original_data++;
        }
        pix+=dif_pix;
        pix_original_data+=dif_line;
    }

    //if ((&proc->advanced_block[block_y][block_x])->hop_counter[4] == (xfin-xini)*(yfin-yini)) (&proc->advanced_block[block_y][block_x])->empty_flag = 1;
    //else (&proc->advanced_block[block_y][block_x])->empty_flag = 0;
    //return nulos;
    /*
    }

    gettimeofday(&after , NULL);
    microsec += (time_diff(before , after))/5000;
    */
}

/////// END OF COPIED FUNCTIONS FROM LHEENC.C ////////




#define OFFSET(x) offsetof(LheEdgeDetectContext, x)
#define FLAGS AV_OPT_FLAG_FILTERING_PARAM|AV_OPT_FLAG_VIDEO_PARAM
static const AVOption lheedgedetect_options[] = {
	// SYNTAX: {name, description, offset, type, default_value, min, max, flags},
	{"hopth", "sets the hop threshold", OFFSET(hop_threshold), AV_OPT_TYPE_INT, {.i64=3}, 0, 4, FLAGS}, // defaul th is 3
    {"basic", "enables the basic mode", OFFSET(basic_lhe), AV_OPT_TYPE_BOOL, {.i64=1}, 0, 1, FLAGS}, // basic lhe is ON by default
	{ NULL }
};

AVFILTER_DEFINE_CLASS(lheedgedetect);

static av_cold int init(AVFilterContext *ctx)
{
	LheEdgeDetectContext *led_ctx = ctx->priv;
	led_ctx->lhe_ctx = av_calloc(1, sizeof(LheContext));
    led_ctx->codec_name = av_calloc(32, sizeof(char));

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

static int config_props(AVFilterLink *inlink)
{
	AVFilterContext *ctx = inlink->dst;
	LheEdgeDetectContext *led_ctx = ctx->priv;
    LheContext *lhe_ctx = led_ctx->lhe_ctx;
    led_ctx->height = inlink->h;
    led_ctx->width = inlink->w;
    strcpy(led_ctx->codec_name, "lhe");
    lhe_ctx->basic_lhe = led_ctx->basic_lhe;

	return 0;
}


static void set_edges(LheEdgeDetectContext *led_ctx, AVFrame *out){
    int x, y, pix;
    uint8_t *hops;
    LheContext *lhe_ctx = led_ctx->lhe_ctx;
    hops = (lhe_ctx->lheY).hops;

    for (y = 0; y < led_ctx->height; y++) {
        for (x = 0; x < led_ctx->width; x++) {
            pix = x + y * out->linesize[0];
            if (hops[pix] < led_ctx->hop_threshold || hops[pix] > 8-led_ctx->hop_threshold) {
                out->data[0][pix] = 0xFF;
                out->data[1][pix] = 0x80;
                out->data[2][pix] = 0x80;
            } else {
                out->data[0][pix] = 0x00;
                out->data[1][pix] = 0x80;
                out->data[2][pix] = 0x80;
            }
        }
    }

    // PINTA TODO NEGRO
    /*
    for (y = 0; y < led_ctx->height; y++) {
        for (x = 0; x < led_ctx->width; x++) {
            offset = x + y * out->linesize[0];

            out->data[0][offset] = 0x00;
            out->data[1][offset] = 0x80;
            out->data[2][offset] = 0x80;
        }
    }*/

}

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



////////////////////////*********************************////////////////////////
//  AVCodecContext *avctx,     AVPacket *pkt,     const AVFrame *frame,      int *got_packet)

    uint8_t *component_original_data_Y, *component_original_data_U, *component_original_data_V;
    uint32_t total_blocks_width, total_blocks_height, pixels_block;
    uint32_t image_size_Y, image_size_UV;
    
    uint8_t mode; 
    int ret;   

    //gettimeofday(&before , NULL);

    LheContext *s = led_ctx->lhe_ctx;

    image_size_Y = (&s->procY)->width * (&s->procY)->height;
    image_size_UV = (&s->procUV)->width * (&s->procUV)->height;
    total_blocks_width = HORIZONTAL_BLOCKS;
    pixels_block = in->width / HORIZONTAL_BLOCKS;
    total_blocks_height = in->height / pixels_block;

    
    //Pointers to different color components
    component_original_data_Y = in->data[0];
    component_original_data_U = in->data[1];
    component_original_data_V = in->data[2];

    //mlhe_reconfig(avctx, s);

    //s->basic_lhe=1; // basic lhe
    if (s->basic_lhe) {  
        //BASIC LHE
        mode = BASIC_LHE;
        total_blocks_height = 1;
        total_blocks_width = 1;

        //Calculate the coordinates for 1 block
        lhe_calculate_block_coordinates (&s->procY, &s->procUV, 1, 1, 0, 0);
        
        // Borders are detected on luminance only
        lhe_advanced_encode_block2_sequential (&s->prec, &s->procY, &s->lheY, component_original_data_Y, 1, 0,  0, BASIC_LHE, in->linesize[0]); 
        //static void lhe_advanced_encode_block2_sequential (LheBasicPrec *prec, LheProcessing *proc, LheImage *lhe, uint8_t *original_data, int total_blocks_width, int block_x, int block_y, int lhe_type, int linesize)

    } /*else {   
        mode = ADVANCED_LHE;
        //for (int i = 0; i < 1000; i++){
        lhe_advanced_encode (s, in, component_original_data_Y, component_original_data_U, component_original_data_V,
                                total_blocks_width, total_blocks_height);
        //}         
    }*/




//////////////////******************************************/////////////////////
    //printf("%d\n", inlink->h);
    //printf("%d\n", inlink->w);
    //printf("%d\n", out->data[0][0]);
    set_edges(led_ctx, out);
    /*int x, y, offset;
    for (y = 0; y < inlink->h; y++) {
        for (x = 0; x < inlink->w; x++) {
            offset = 3 * x + y * inlink->w;

            out->data[0][offset + 0] = 0xFF; // Y
            out->data[0][offset + 1] = 0xFF; // U
            out->data[0][offset + 2] = 0xFF; // V
            
        }
    } */

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
	.description   = NULL_IF_CONFIG_SMALL("Detect and draw edges with LHE hop interpretation."),
	.priv_size     = sizeof(LheEdgeDetectContext),
	.init          = init,
	.uninit        = uninit,
	.query_formats = query_formats,
	.inputs        = lheedgedetect_inputs,
	.outputs       = lheedgedetect_outputs,
	.priv_class    = &lheedgedetect_class,
	//.flags         = AVFILTER_FLAG_SUPPORT_TIMELINE_GENERIC,
};
