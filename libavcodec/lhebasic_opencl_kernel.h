
#ifndef AVCODEC_LHE_OPENCL_KERNEL_H

#define AVCODEC_LHE_OPENCL_KERNEL_H

#include "libavutil/opencl.h"

const char *ff_kernel_lhe_opencl = AV_OPENCL_KERNEL(


    kernel void lhebasic_encode (global uint8 *a, global uint8 *b, global uint8 *c)
    {   
        unsigned int i = get_global_id(0);
        
        c[i] = a[i]+b[i];        
    }
);

#endif /* AVCODEC_LHE_OPENCL_KERNEL_H */