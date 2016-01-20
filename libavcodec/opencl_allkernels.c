
#include "opencl_allkernels.h"
#include "libavutil/error.h"
#include "libavutil/log.h"

#if CONFIG_OPENCL
#include "libavutil/opencl.h"
#include "lhebasic_opencl_kernel.h"
#endif

#define OPENCL_REGISTER_KERNEL_CODE(X, x)                                              \
    {                                                                                  \
        if (CONFIG_##X##_ENCODER || CONFIG_##X##_DECODER) {                            \
            av_opencl_register_kernel_code(ff_kernel_##x##_opencl);                    \
        }                                                                              \
    }

void ff_opencl_register_codec_kernel_code_all(void)
{

 #if CONFIG_OPENCL
   OPENCL_REGISTER_KERNEL_CODE(LHE,         lhe);
 #endif
}