#include "lhebasic_opencl.h"

int ff_opencl_lhebasic_init(LheOpenclContext *locc)
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
   
    return ret;
}

static int ff_opencl_lhe_create_image_buffers (LheOpenclContext *locc, 
                                               int image_size)
{
    
    uint8_t *data_a, *data_b; 
    int ret = 0;
        
    data_a = malloc(sizeof(uint8_t) * image_size);
    data_b = malloc(sizeof(uint8_t) * image_size);
     
    for(int i=0; i < image_size; i++)
    {
        data_a[i] = i;
        data_b[i] = i;
    }
    
    ret = av_opencl_buffer_create(&locc->a,
                                  sizeof(uint8_t) * image_size ,
                                  CL_MEM_READ_ONLY|CL_MEM_COPY_HOST_PTR, data_a);
    if (ret < 0)
        return ret;
    
    ret = av_opencl_buffer_create(&locc->b,
                                  sizeof(uint8_t) * image_size,
                                  CL_MEM_READ_ONLY|CL_MEM_COPY_HOST_PTR, data_b);
    if (ret < 0)
        return ret;
    
    ret = av_opencl_buffer_create(&locc->c,
                                  sizeof(uint8_t) * image_size,
                                  CL_MEM_WRITE_ONLY, NULL);
    if (ret < 0)
        return ret;
    
    return ret;
}


int ff_opencl_lhebasic_encode(LheOpenclContext *locc,
                              int width, int height)
{
    int ret, image_size;
    uint8_t *output;
    cl_int status;
    
    image_size = 64;
    ret=0;

    size_t local_work_size_1d[1] = {32}; 
    size_t global_work_size_1d[1] = {(size_t)image_size};
    FFOpenclParam param_encode = {0};

    
    output = malloc(sizeof(uint8_t) * image_size);
    
    ff_opencl_lhe_create_image_buffers(locc, image_size);

    param_encode.kernel = locc -> kernel_encode;
    
    ret = avpriv_opencl_set_parameter(&param_encode,
                                  FF_OPENCL_PARAM_INFO(locc->a),
                                  FF_OPENCL_PARAM_INFO(locc->b),
                                  FF_OPENCL_PARAM_INFO(locc->c),
                                  NULL);
    if (ret < 0)
        return ret;
    
    status = clEnqueueNDRangeKernel(locc -> command_queue,
                                    locc -> kernel_encode, 1, NULL,
                                    global_work_size_1d, local_work_size_1d, 0, NULL, NULL);
   
    
    if (status != CL_SUCCESS) {
        av_log(NULL, AV_LOG_ERROR, "OpenCL run kernel error occurred: %s\n", av_opencl_errstr(status));
        return AVERROR_EXTERNAL;
    }
    
    clFinish(locc -> command_queue);
        
    status = clEnqueueReadBuffer(locc -> command_queue, locc -> c, CL_TRUE, 0,
                                 sizeof(uint8_t)*image_size, output, 0, 
                                 NULL, NULL);
    
     if (status != CL_SUCCESS) {
        av_log(NULL, AV_LOG_ERROR, "OpenCL read buffer error ocurred: %s\n", av_opencl_errstr(status));
        return AVERROR_EXTERNAL;
    }
    clFinish(locc -> command_queue);

    for(int i=0; i < image_size; i++)
    {
       av_log(NULL, AV_LOG_INFO,"output[%d] = %d\n", i, output[i]);
    }
    
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