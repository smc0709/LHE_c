#include "lhebasic_opencl.h"

int ff_opencl_lhebasic_init(LheBasicPrec *prec, LheOpenclContext *locc)
{
    int ret = 0;
 
    ret = av_opencl_init(NULL);

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
    
    //Creating cache buffers 
    ret = av_opencl_buffer_create(&locc->prec_luminance,
                                  sizeof(uint8_t) * PREC_LUMINANCE_CACHE_SIZE ,
                                  CL_MEM_READ_ONLY, NULL);
    if (ret < 0)
        return ret;
    
    ret = clEnqueueWriteBuffer(locc->command_queue, locc->prec_luminance, 
                               CL_TRUE, 0, sizeof(uint8_t) * PREC_LUMINANCE_CACHE_SIZE, 
                               prec->prec_luminance, 0, NULL, NULL);
    
    if (ret < 0)
        return ret;
    
    ret = av_opencl_buffer_create(&locc->best_hop,
                                  sizeof(uint8_t) * BEST_HOP_CACHE_SIZE ,
                                  CL_MEM_READ_ONLY, NULL);
    if (ret < 0)
        return ret;
   
    ret = clEnqueueWriteBuffer(locc->command_queue, locc->best_hop, 
                               CL_TRUE, 0, sizeof(uint8_t) * BEST_HOP_CACHE_SIZE, 
                               prec->best_hop, 0, NULL, NULL);
    
    if (ret < 0)
        return ret;
    return ret;
}

static int ff_opencl_lhe_create_image_buffers (LheOpenclContext *locc,
                                               uint8_t *component_original_data,
                                               uint8_t *component_prediction,
                                               int image_size, 
                                               int num_blocks)
{    
    int ret = 0;
    
    ret = av_opencl_buffer_create(&locc->component_original_data,
                                  sizeof(uint8_t) * image_size ,
                                  CL_MEM_READ_ONLY, NULL );
    if (ret < 0)
        return ret;
    
    ret = clEnqueueWriteBuffer(locc->command_queue, 
                               locc->component_original_data, CL_TRUE, 0, 
                               sizeof(uint8_t) * image_size, component_original_data, 
                               0, NULL, NULL);
     
    ret = av_opencl_buffer_create(&locc->component_prediction,
                                  sizeof(uint8_t) * image_size ,
                                  CL_MEM_WRITE_ONLY, NULL);
    if (ret < 0)
        return ret;
    
    
    ret = av_opencl_buffer_create(&locc->hops,
                                  sizeof(uint8_t) * image_size,
                                  CL_MEM_WRITE_ONLY, NULL);
    if (ret < 0)
        return ret;
    
    
    
    
    return ret;
}


int ff_opencl_lhebasic_encode(LheOpenclContext *locc,
                                uint8_t *component_original_data,
                                uint8_t *component_prediction,
                                uint8_t *hops,
                                uint8_t *first_pixel_block,
                                int image_width, int image_height,
                                int block_width, int block_height,
                                int pix_size)
{
    int ret, image_size;
    int num_blocks, num_blocks_width, num_blocks_height;
    cl_int status;
        
    ret=0;
    num_blocks_width = image_width/block_width;
    num_blocks_height = image_height/block_height;
    num_blocks = num_blocks_width*num_blocks_height;
    image_size = image_width * image_height;
        
    size_t local_work_size_2d[2] = {num_blocks_width, num_blocks_height}; 
    size_t global_work_size_2d[2] = {(size_t)num_blocks_width, (size_t) num_blocks_height};
    FFOpenclParam param_encode = {0};
    
    ff_opencl_lhe_create_image_buffers(locc, component_original_data,
                                       component_prediction, image_size, num_blocks);

    param_encode.kernel = locc -> kernel_encode;
    
    ret = avpriv_opencl_set_parameter(&param_encode,
                                      FF_OPENCL_PARAM_INFO(locc->prec_luminance),
                                      FF_OPENCL_PARAM_INFO(locc->best_hop),
                                      FF_OPENCL_PARAM_INFO(locc->component_original_data),
                                      FF_OPENCL_PARAM_INFO(locc->component_prediction),
                                      FF_OPENCL_PARAM_INFO(locc->hops),
                                      FF_OPENCL_PARAM_INFO(image_width),
                                      FF_OPENCL_PARAM_INFO(image_height),
                                      FF_OPENCL_PARAM_INFO(block_width),
                                      FF_OPENCL_PARAM_INFO(block_height),
                                      FF_OPENCL_PARAM_INFO(pix_size),
                                      NULL);
    if (ret < 0)
        return ret;
    
    status = clEnqueueNDRangeKernel(locc -> command_queue,
                                    locc -> kernel_encode, 2, NULL,
                                    global_work_size_2d, local_work_size_2d, 0, NULL, NULL);
   
    
    if (status != CL_SUCCESS) {
        av_log(NULL, AV_LOG_ERROR, "OpenCL run kernel error occurred: %s\n", av_opencl_errstr(status));
        return AVERROR_EXTERNAL;
    }
    
    clFinish(locc -> command_queue);
        
    status = clEnqueueReadBuffer(locc -> command_queue, locc -> hops, CL_TRUE, 0,
                                 sizeof(uint8_t)*image_size, hops, 0, 
                                 NULL, NULL);

      
    if (status != CL_SUCCESS) {
        av_log(NULL, AV_LOG_ERROR, "OpenCL read buffer error ocurred: %s\n", av_opencl_errstr(status));
        return AVERROR_EXTERNAL;
    }
    
    
    clFinish(locc -> command_queue);
    
    
    return ret;
}

void ff_opencl_info (void) {
    char dname[500];
    cl_device_id devices[10];
    cl_uint num_devices,entries;
    cl_ulong long_entries;
    int d;
    cl_int err;
    cl_platform_id platform_id = NULL;
    size_t p_size;
    
    av_log(NULL, AV_LOG_INFO, "*************OPENCL INFO************* \n");


    //obtain list of platforms available
    err = clGetPlatformIDs(1, &platform_id,NULL);
    if (err != CL_SUCCESS)
    {
        av_log(NULL, AV_LOG_ERROR, "Error: Failure in clGetPlatformIDs,error code=%d \n",err);
        return 0;
    }

    //obtain information about platform 
    clGetPlatformInfo(platform_id,CL_PLATFORM_NAME,500,dname,NULL);
    av_log(NULL, AV_LOG_INFO, "CL_PLATFORM_NAME = %s\n", dname);
    clGetPlatformInfo(platform_id,CL_PLATFORM_VERSION,500,dname,NULL);
    av_log(NULL, AV_LOG_INFO,"CL_PLATFORM_VERSION = %s\n", dname);

    /* obtain list of devices available on platform */
    clGetDeviceIDs(platform_id, CL_DEVICE_TYPE_ALL, 10, devices, &num_devices);
    av_log(NULL, AV_LOG_INFO,"%d devices found\n", num_devices);

    //query devices for information 
    for (d = 0; d < num_devices; ++d) {
        clGetDeviceInfo(devices[d], CL_DEVICE_NAME, 500, dname,NULL);
        av_log(NULL, AV_LOG_INFO, "Device #%d name = %s\n", d, dname);
        clGetDeviceInfo(devices[d],CL_DRIVER_VERSION, 500, dname,NULL);
        av_log(NULL, AV_LOG_INFO, "Driver version = %s\n", dname);
        clGetDeviceInfo(devices[d],CL_DEVICE_GLOBAL_MEM_SIZE,sizeof(cl_ulong),&long_entries,NULL);
        av_log(NULL, AV_LOG_INFO, "Global Memory (MB):\t%llu\n",long_entries/1024/1024);
        clGetDeviceInfo(devices[d],CL_DEVICE_GLOBAL_MEM_CACHE_SIZE,sizeof(cl_ulong),&long_entries,NULL);
        av_log(NULL, AV_LOG_INFO, "Global Memory Cache (MB):\t%llu\n",long_entries/1024/1024);
        clGetDeviceInfo(devices[d],CL_DEVICE_LOCAL_MEM_SIZE,sizeof(cl_ulong),&long_entries,NULL);
        av_log(NULL, AV_LOG_INFO, "Local Memory (KB):\t%llu\n",long_entries/1024);
        clGetDeviceInfo(devices[d],CL_DEVICE_MAX_CLOCK_FREQUENCY,sizeof(cl_ulong),&long_entries,NULL);
        av_log(NULL, AV_LOG_INFO, "Max clock (MHz) :\t%llu\n",long_entries);
        clGetDeviceInfo(devices[d],CL_DEVICE_MAX_WORK_GROUP_SIZE,sizeof(size_t),&p_size,NULL);
        av_log(NULL, AV_LOG_INFO, "Max Work Group Size:\t%d\n",p_size);
        clGetDeviceInfo(devices[d],CL_DEVICE_MAX_COMPUTE_UNITS,sizeof(cl_uint),&entries,NULL);
        av_log(NULL, AV_LOG_INFO, "Number of parallel compute cores:\t%d\n",entries);
    }
    
    av_log(NULL, AV_LOG_INFO, "*************END OPENCL INFO************* \n");

}