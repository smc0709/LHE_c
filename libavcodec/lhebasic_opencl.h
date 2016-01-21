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
    cl_mem a;
    cl_mem b;
    cl_mem c;
} LheOpenclContext;

void ff_opencl_info (void);
int ff_opencl_lhebasic_init(LheOpenclContext *locc);
int ff_opencl_lhebasic_encode(LheOpenclContext *locc,int width, int height);

#endif /* AVCODEC_LHE_OPENCL_H */

