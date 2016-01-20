#include "lhebasic_opencl.h"

int ff_opencl_lhebasic_init(LheOpenclContext *locc)
{
    int ret = 0;
    
    ret = av_opencl_init(NULL);
    
    if (ret < 0)
        return ret;
    locc-> command_queue = av_opencl_get_command_queue();
    if (!locc-> command_queue) {
        av_log(NULL, AV_LOG_ERROR, "Unable to get OpenCL command queue in LHE\n");
        return AVERROR(EINVAL);
    }
    locc->program = av_opencl_compile("lhebasic", NULL);

    if (!locc->program) {
        av_log(NULL, AV_LOG_ERROR, "OpenCL failed to compile program 'lhebasic'\n");
        return AVERROR(EINVAL);
    }
    if (!locc->kernel_encode) {
        locc->kernel_encode = clCreateKernel(locc->program,
                                             "lhebasic_encode", &ret);
        if (ret != CL_SUCCESS) {
            av_log(NULL, AV_LOG_ERROR, "OpenCL failed to create kernel 'lhe_encode'\n");
            return AVERROR(EINVAL);
        }
    }
   
    return ret;
}

int ff_opencl_encode(LheOpenclContext *locc,
                        int width, int height)
{
    int ret = 0;
    
    return ret;
}