#ifndef AVCODEC_LHE_OPENCL_H
#define AVCODEC_LHE_OPENCL_H

#include "libavutil/opencl.h"
#include "libavutil/opencl_internal.h"
#include "libavutil/error.h"
#include "libavutil/log.h"
#include "lhebasic_prec.h"

typedef struct {
    cl_command_queue command_queue;
    cl_program program;
    cl_kernel kernel_encode;
    cl_mem component_original_data;
    cl_mem hops;
    cl_mem first_pixel_block;
    cl_mem best_hop;
    cl_mem prec_luminance;
    cl_mem component_prediction; 
    cl_mem a;
    cl_mem b;
    cl_mem c;
} LheOpenclContext;

void ff_opencl_info (void);
int ff_opencl_lhebasic_init(LheBasicPrec *prec, LheOpenclContext *locc);
int ff_opencl_lhebasic_encode(LheOpenclContext *locc,
                              uint8_t *component_original_data,
                              uint8_t *component_prediction,
                              uint8_t *hops,
                              uint8_t *first_pixel_block,
                              int image_width, int image_height,
                              int block_width, int block_height,
                              int pix_size);

#endif /* AVCODEC_LHE_OPENCL_H */

