#ifndef AVCODEC_LHE_OPENCL_H
#define AVCODEC_LHE_OPENCL_H

#include "libavutil/opencl.h"
#include "libavutil/opencl_internal.h"
#include "libavutil/error.h"
#include "libavutil/log.h"

typedef struct {
    cl_command_queue command_queue;
    cl_program program;
    cl_kernel kernel_encode;
    int in_plane_size[8];
    int out_plane_size[8];
    cl_mem a;
    cl_mem b;
    cl_mem c;
    size_t cl_inbuf_size;
    size_t cl_outbuf_size;
} LheOpenclContext;


int ff_opencl_lhebasic_init(LheOpenclContext *lhe_opencl);
int ff_opencl_encode(LheOpenclContext *locc,int width, int height);

#endif /* AVCODEC_LHE_OPENCL_H */

