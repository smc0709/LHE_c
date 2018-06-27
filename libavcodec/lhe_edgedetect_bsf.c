/*
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
 * Lhe edge detection bitstream filter -- gives a greyscale interpretation of the lhe hops.
 */

#include "avcodec.h"
#include "bsf.h"
#include "lhe.h"


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

//////////////////////// COPIADO DE lhedec.c //////////////////////////
/*
typedef struct LheState {
    AVClass *class;  
    LheBasicPrec prec;
    AVFrame * frame;
    GetBitContext gb;
    LheProcessing procY;
    LheProcessing procUV;
    LheImage lheY;
    LheImage lheU;
    LheImage lheV;
    uint8_t chroma_factor_width;
    uint8_t chroma_factor_height;
    uint8_t lhe_mode;
    uint8_t pixel_format;
    uint8_t quality_level;
    uint32_t total_blocks_width;
    uint32_t total_blocks_height;
    uint64_t global_frames_count;
} LheState;
*/

//==================================================================
// DECODE FRAME
//==================================================================

/**
 * Read and decodes LHE image
 *
 * @param *avctx Codec context
 * @param *data data from file
 * @param *got_frame indicates frame is ready
 * @param *avpkt AV packet
 */ 
static int lhe_decode_frame(AVCodecContext *avctx, void *data, int *got_frame, AVPacket *avpkt)
{    

    uint32_t total_blocks, pixels_block, image_size_Y, image_size_UV;
    int ret;
    
    float compression_factor;
    uint32_t ppp_max_theoric;
    
    LheHuffEntry he_mesh[LHE_MAX_HUFF_SIZE_MESH];
    LheHuffEntry he_Y[LHE_MAX_HUFF_SIZE_SYMBOLS];
    LheHuffEntry he_UV[LHE_MAX_HUFF_SIZE_SYMBOLS];
   
    LheState *s = avctx->priv_data;
    
    const uint8_t *lhe_data = avpkt->data;
    
    init_get_bits(&s->gb, lhe_data, avpkt->size * 8);
    
    //LHE mode
    s->lhe_mode = get_bits(&s->gb, LHE_MODE_SIZE_BITS);
    
    image_size_Y = (&s->procY)->width * (&s->procY)->height;
    image_size_UV = (&s->procUV)->width * (&s->procUV)->height;
    //Allocates frame
    av_frame_unref(s->frame);
    if ((ret = ff_get_buffer(avctx, s->frame, 0)) < 0)
        return ret;
 
    if (s->lhe_mode == BASIC_LHE) 
    {
        s->total_blocks_width = 1;
        s->total_blocks_height = 1;
    }
    
    total_blocks = s->total_blocks_height * s->total_blocks_width;
    
    //First pixel array 
    //for (int i=0; i<total_blocks; i++) 
    //{
    (&s->lheY)->first_color_block[0] = get_bits(&s->gb, FIRST_COLOR_SIZE_BITS);
    //}

    
    //for (int i=0; i<total_blocks; i++) 
    //{
    (&s->lheU)->first_color_block[0] = get_bits(&s->gb, FIRST_COLOR_SIZE_BITS);
    //}
    
        
    //for (int i=0; i<total_blocks; i++) 
    //{
    (&s->lheV)->first_color_block[0] = get_bits(&s->gb, FIRST_COLOR_SIZE_BITS);
    //}

    //Pointers to different color components
    (&s->lheY)->component_prediction = s->frame->data[0];
    (&s->lheU)->component_prediction  = s->frame->data[1];
    (&s->lheV)->component_prediction  = s->frame->data[2];
    
    if (s->lhe_mode == ADVANCED_LHE) /*ADVANCED LHE*/
    {

        (&s->procY)-> theoretical_block_width = (&s->procY)->width / s->total_blocks_width;    
        (&s->procY)-> theoretical_block_height = (&s->procY)->height / s->total_blocks_height;   
        
        (&s->procUV)-> theoretical_block_width = (&s->procUV)->width / s->total_blocks_width;
        (&s->procUV)-> theoretical_block_height = (&s->procUV)->height / s->total_blocks_height; 
                
        //MESH Huffman
        lhe_read_huffman_table(s, he_mesh, LHE_MAX_HUFF_SIZE_MESH, LHE_HUFFMAN_NODE_BITS_MESH, LHE_HUFFMAN_NO_OCCURRENCES_MESH);
        
        //Read quality level and calculate compression factor
        s->quality_level = get_bits(&s->gb, QL_SIZE_BITS); 
        ppp_max_theoric = (&s->procY)-> theoretical_block_width/SIDE_MIN;
        if (ppp_max_theoric > PPP_MAX) ppp_max_theoric = PPP_MAX;
        compression_factor = (&s->prec)->compression_factor[ppp_max_theoric][s->quality_level];        
       /*
        for (int block_y=0; block_y<s->total_blocks_height; block_y++) 
        {
            for (int block_x=0; block_x<s->total_blocks_width; block_x++) 
            { 
                (&s->procY)->advanced_block[block_y][block_x].empty_flagY = get_bits(&s->gb, 1);
                
                (&s->procY)->advanced_block[block_y][block_x].empty_flagU = get_bits(&s->gb, 1);
                
                (&s->procY)->advanced_block[block_y][block_x].empty_flagV = get_bits(&s->gb, 1);
            }
        }*/

        lhe_advanced_read_mesh(s, he_mesh, ppp_max_theoric, compression_factor);

        lhe_advanced_read_all_file_symbols (s, he_Y, he_UV);
              
        lhe_advanced_decode_symbols (s, he_Y, he_UV, image_size_Y, image_size_UV);     
        
    }

    else /*BASIC LHE*/       
    {
        (&s->procY)->num_hopsY = image_size_Y;
        (&s->procUV)->num_hopsU = image_size_UV;
        (&s->procUV)->num_hopsV = image_size_UV;
        lhe_advanced_read_file_symbols2 (s, &s->procY, (&s->lheY)->hops, 0, s->total_blocks_height, 1, BASIC_LHE, 0);
        lhe_advanced_read_file_symbols2 (s, &s->procUV, (&s->lheU)->hops, 0, s->total_blocks_height, 1, BASIC_LHE, 1);            
        lhe_advanced_read_file_symbols2 (s, &s->procUV, (&s->lheV)->hops, 0, s->total_blocks_height, 1, BASIC_LHE, 2);
        
        lhe_basic_decode_frame_sequential (s);    
    
    }

    if ((ret = av_frame_ref(data, s->frame)) < 0)
        return ret;
    *got_frame = 1;


    return avpkt->size;//Hay que devolver el numero de bytes leidos (puede ser la solución a la doble ejecución del decoder).
}




//==================================================================
// DECODE VIDEO FRAME
//==================================================================
/**
 * Read and decodes MLHE video
 *
 * @param *avctx Codec context
 * @param *data data from file
 * @param *got_frame indicates frame is ready
 * @param *avpkt AV packet
 */ 
static int mlhe_decode_video(AVCodecContext *avctx, void *data, int *got_frame, AVPacket *avpkt)
{    
    uint32_t total_blocks, pixels_block, image_size_Y, image_size_UV;
    int ret;
    
    float compression_factor;
    uint32_t ppp_max_theoric;
    
    LheHuffEntry he_mesh[LHE_MAX_HUFF_SIZE_MESH];
    LheHuffEntry he_Y[LHE_MAX_HUFF_SIZE_SYMBOLS];
    LheHuffEntry he_UV[LHE_MAX_HUFF_SIZE_SYMBOLS];
   
    LheState *s = avctx->priv_data;
    
    const uint8_t *lhe_data = avpkt->data;
    
    init_get_bits(&s->gb, lhe_data, avpkt->size * 8);
    
    s->lhe_mode = get_bits(&s->gb, LHE_MODE_SIZE_BITS); 
        
    if (s->lhe_mode==DELTA_MLHE && s->global_frames_count<=0) 
      return -1;
         
    if (s->lhe_mode == DELTA_MLHE) { /*DELTA VIDEO FRAME*/      
        
        image_size_Y = (&s->procY)->width * (&s->procY)->height;
        image_size_UV = (&s->procUV)->width * (&s->procUV)->height; 
        
        //Allocates frame
        av_frame_unref(s->frame);
        if ((ret = ff_get_buffer(avctx, s->frame, 0)) < 0)
            return ret;
        
        (&s->lheY)->component_prediction = s->frame->data[0];
        (&s->lheU)->component_prediction  = s->frame->data[1];
        (&s->lheV)->component_prediction  = s->frame->data[2];
    
        total_blocks = s->total_blocks_height * s->total_blocks_width;
        
        //First pixel array
        //for (int i=0; i<total_blocks; i++) 
        //{
        (&s->lheY)->first_color_block[0] = get_bits(&s->gb, FIRST_COLOR_SIZE_BITS);
        //}

        //for (int i=0; i<total_blocks; i++) 
        //{
        (&s->lheU)->first_color_block[0] = get_bits(&s->gb, FIRST_COLOR_SIZE_BITS);
        //}
        
        //for (int i=0; i<total_blocks; i++) 
        //{
        (&s->lheV)->first_color_block[0] = get_bits(&s->gb, FIRST_COLOR_SIZE_BITS); 
        //}

         //MESH Huffman
        lhe_read_huffman_table(s, he_mesh, LHE_MAX_HUFF_SIZE_MESH, LHE_HUFFMAN_NODE_BITS_MESH, LHE_HUFFMAN_NO_OCCURRENCES_MESH);     
        
        //Calculate compression factor
        ppp_max_theoric = (&s->procY)->theoretical_block_width/SIDE_MIN;
        if (ppp_max_theoric > PPP_MAX) ppp_max_theoric = PPP_MAX;
        compression_factor = (&s->prec)->compression_factor[ppp_max_theoric][s->quality_level];        
       /*
        for (int block_y=0; block_y<s->total_blocks_height; block_y++) 
        {
            for (int block_x=0; block_x<s->total_blocks_width; block_x++) 
            { 
                (&s->procY)->advanced_block[block_y][block_x].empty_flagY = get_bits(&s->gb, 1);
                
                (&s->procY)->advanced_block[block_y][block_x].empty_flagU = get_bits(&s->gb, 1);
                
                (&s->procY)->advanced_block[block_y][block_x].empty_flagV = get_bits(&s->gb, 1);
            }
        }*/
       
        lhe_advanced_read_mesh(s, he_mesh, ppp_max_theoric, compression_factor);
        
        lhe_advanced_read_all_file_symbols (s, he_Y, he_UV);
        
        mlhe_decode_delta_frame (s, he_Y, he_UV, image_size_Y, image_size_UV);
    } 
    else if (s->lhe_mode == ADVANCED_LHE)
    {   
        s->global_frames_count++;
        
        image_size_Y = (&s->procY)->width * (&s->procY)->height;
        image_size_UV = (&s->procUV)->width * (&s->procUV)->height;
        //Allocates frame
        av_frame_unref(s->frame);
        if ((ret = ff_get_buffer(avctx, s->frame, 0)) < 0)
            return ret;
        
        //Pointers to different color components
        (&s->lheY)->component_prediction = s->frame->data[0];
        (&s->lheU)->component_prediction  = s->frame->data[1];
        (&s->lheV)->component_prediction  = s->frame->data[2];

        total_blocks = s->total_blocks_height * s->total_blocks_width;
        
        //First pixel array
        //for (int i=0; i<total_blocks; i++) 
        //{
        (&s->lheY)->first_color_block[0] = get_bits(&s->gb, FIRST_COLOR_SIZE_BITS);
        //}

        //for (int i=0; i<total_blocks; i++) 
        //{
        (&s->lheU)->first_color_block[0] = get_bits(&s->gb, FIRST_COLOR_SIZE_BITS);
        //}
        
        //for (int i=0; i<total_blocks; i++) 
        //{
        (&s->lheV)->first_color_block[0] = get_bits(&s->gb, FIRST_COLOR_SIZE_BITS); 
        //}
        
        (&s->procY)-> theoretical_block_width = (&s->procY)->width / s->total_blocks_width;    
        (&s->procY)-> theoretical_block_height = (&s->procY)->height / s->total_blocks_height;   
        
        (&s->procUV)-> theoretical_block_width = (&s->procUV)->width / s->total_blocks_width;
        (&s->procUV)-> theoretical_block_height = (&s->procUV)->height / s->total_blocks_height; 
        
        //MESH Huffman
        lhe_read_huffman_table(s, he_mesh, LHE_MAX_HUFF_SIZE_MESH, LHE_HUFFMAN_NODE_BITS_MESH, LHE_HUFFMAN_NO_OCCURRENCES_MESH);
        
        //Read quality level and calculate compression factor
        s->quality_level = get_bits(&s->gb, QL_SIZE_BITS); 
        ppp_max_theoric = (&s->procY)-> theoretical_block_width/SIDE_MIN;
        if (ppp_max_theoric > PPP_MAX) ppp_max_theoric = PPP_MAX;
        compression_factor = (&s->prec)->compression_factor[ppp_max_theoric][s->quality_level];        
        /*
        for (int block_y=0; block_y<s->total_blocks_height; block_y++) 
        {
            for (int block_x=0; block_x<s->total_blocks_width; block_x++) 
            { 
                (&s->procY)->advanced_block[block_y][block_x].empty_flagY = get_bits(&s->gb, 1);
                
                (&s->procY)->advanced_block[block_y][block_x].empty_flagU = get_bits(&s->gb, 1);
                
                (&s->procY)->advanced_block[block_y][block_x].empty_flagV = get_bits(&s->gb, 1);
            }
        }*/

        lhe_advanced_read_mesh(s, he_mesh, ppp_max_theoric, compression_factor);
        
        lhe_advanced_read_all_file_symbols (s, he_Y, he_UV);
                
        lhe_advanced_decode_symbols (s, he_Y, he_UV, image_size_Y, image_size_UV);
    }   
    





    for (int i=0; i < s->total_blocks_height; i++)
    {
        memcpy((&s->procY)->last_advanced_block[i], (&s->procY)->advanced_block[i], sizeof(AdvancedLheBlock) * (s->total_blocks_width));
        memcpy((&s->procUV)->last_advanced_block[i], (&s->procUV)->advanced_block[i], sizeof(AdvancedLheBlock) * (s->total_blocks_width));
    }   
    
    memcpy ((&s->lheY)->last_downsampled_image, (&s->lheY)->downsampled_image, image_size_Y);    
    memcpy ((&s->lheU)->last_downsampled_image, (&s->lheU)->downsampled_image, image_size_UV);
    memcpy ((&s->lheV)->last_downsampled_image, (&s->lheV)->downsampled_image, image_size_UV);
    
    memset((&s->lheY)->downsampled_image, 0, image_size_Y);
    memset((&s->lheU)->downsampled_image, 0, image_size_UV);
    memset((&s->lheV)->downsampled_image, 0, image_size_UV);     
    
    if ((ret = av_frame_ref(data, s->frame)) < 0) 
        return ret;
 
    *got_frame = 1;

    return 0;
}


////////////////////////////////////////////////////




typedef struct LHEEdgeDetectContext {
    const AVClass *class;
    LheContext *lhe_ctx; //The lhe context (definition copied from the lheenc.c)
    int  hop_threshold; // The minimum hop (absolute value) required to trigger the edge detection.
    bool absolute_hop; // Whether the hops are or not fully interpreted or just their absolute value
} LHEEdgeDetectContext;

static void set_edges(LHEEdgeDetectContext *led_ctx, AVPacket *out){
    int x, y, pix, k, hop_threshold;
    uint8_t *hops;
    uint8_t hop, bg_color;
    bool negative_hop;
    LheContext *lhe_ctx = led_ctx->lhe_ctx;
    hops = (lhe_ctx->lheY).hops;
    hop_threshold = led_ctx->hop_threshold;
    uint32_t width = (lhe_ctx->procY)->width;
    uint32_t height = (lhe_ctx->procY)->height;

    for (y = 0; y < height; y++) {
        for (x = 0; x < width; x++) {
            pix = x + y * width; //out->linesize[0];
            if (led_ctx->absolute_hop){
                k=63;
                bg_color=0x00;
                negative_hop=0;
                hop = (uint8_t) abs(((int8_t)hops[pix])-4);
                if (hop<hop_threshold) hop=0;
            }else{ // hop has sign
                k=31;
                bg_color=0x80;
                hop = (uint8_t) abs(((int8_t)hops[pix])-4);
                if (hop<hop_threshold) hop=0;
                if (((int8_t)hops[pix])-4 < 0) negative_hop=1;
                else negative_hop=0;
            }

            if (negative_hop){
                out->data[0][pix] = bg_color - hop*k; // Y
            } else{
                out->data[0][pix] = bg_color + hop*k; // Y
            }
            out->data[1][pix] = 0x80; // Cr
            out->data[2][pix] = 0x80; // Cb
        }
    }

    // PAINTS ALL BLACK
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

static int lhe_edgedetect_filter(AVBSFContext *ctx, AVPacket *out)
{
    LHEEdgeDetectContext *led_ctx = ctx->priv_data;
    AVPacket *in;
    int ret;

    ret = ff_bsf_get_packet(ctx, &in);
    if (ret < 0)
        return ret;
    
    ret = av_new_packet(out, in->size);
    if (ret < 0)
        goto fail;

    ret = av_packet_copy_props(out, in);
    if (ret < 0)
        goto fail;

    memcpy(out->data, in->data, in->size);

    set_edges(led_ctx, out);

    //av_packet_move_ref(out, in); // copies refence from out to in av_packet_move_ref(*dest, *src)
    av_packet_free(&in);
    return 0;

fail:
    if (ret < 0)
        av_packet_unref(out);
    av_packet_free(&in);
    return ret;
}

#define OFFSET(x) offsetof(LHEEdgeDetectContext, x)
static const AVOption lhe_edgedetect_options[] = {
    // SYNTAX: {name, description, offset, type, default_value, min, max},
    {"hopth", "sets the hop threshold", OFFSET(hop_threshold), AV_OPT_TYPE_INT, {.i64=1}, 1, 4}, // default threshold is 1. Note that 1 gives all the hops greyscale and 4 a plain grey/black
    //{"basic", "enables the basic mode", OFFSET(basic_lhe), AV_OPT_TYPE_BOOL, {.i64=1}, 0, 1}, // basic lhe is ON by default
    {"abshop", "only absolute value of hops is interpreted", OFFSET(absolute_hop), AV_OPT_TYPE_BOOL, {.i64=0}, 0, 1}, // only absolute value of hops is interpreted. OFF by default
    { NULL }
};

static void lhe_edgedetect_uninit(AVBSFContext *ctx)
{
    LHEEdgeDetectContext *s = ctx->priv_data;

    lhedec_free_tables(s->lhe_state);
    av_log(NULL, AV_LOG_INFO, "Llama a close despues de liberar los arrays\n");
    av_packet_free(&s->buffer_pkt);

    return 0;
}

static const AVClass lhe_edgedetect_class = {
    .class_name = "lhe_edgedetect",
    .item_name  = av_default_item_name,
    .option     = lhe_edgedetect_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const AVBitStreamFilter ff_lhe_edgedetect_bsf = {
    .name           = "lhe_edgedetect",
    .priv_data_size = sizeof(LHEEdgeDetectContext),
    .priv_class     = &lhe_edgedetect_class,
    .filter         = lhe_edgedetect_filter,
    .close          = lhe_edgedetect_uninit,
    .codec_ids      = (const enum AVCodecID []){ AV_CODEC_ID_LHE, AV_CODEC_ID_MLHE, AV_CODEC_ID_NONE },
};
