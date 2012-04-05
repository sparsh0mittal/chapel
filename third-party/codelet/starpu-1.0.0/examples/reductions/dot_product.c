/* StarPU --- Runtime system for heterogeneous multicore architectures.
 *
 * Copyright (C) 2010-2011  Université de Bordeaux 1
 * Copyright (C) 2012 inria
 *
 * StarPU is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or (at
 * your option) any later version.
 *
 * StarPU is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU Lesser General Public License in COPYING.LGPL for more details.
 */

#include <starpu.h>
#include <assert.h>
#include <math.h>

#include <reductions/dot_product.h>

#ifdef STARPU_USE_CUDA
#include <cuda.h>
#include <cublas.h>
#include <starpu_cuda.h>
#endif

#ifdef STARPU_USE_OPENCL
#include <starpu_opencl.h>
#endif

#define FPRINTF(ofile, fmt, args ...) do { if (!getenv("STARPU_SSILENT")) {fprintf(ofile, fmt, ##args); }} while(0)

static float *x;
static float *y;
static starpu_data_handle_t *x_handles;
static starpu_data_handle_t *y_handles;
#ifdef STARPU_USE_OPENCL
static struct starpu_opencl_program opencl_program;
#endif

static unsigned nblocks = 4096;
static unsigned entries_per_block = 1024;

static DOT_TYPE dot = 0.0f;
static starpu_data_handle_t dot_handle;

static int can_execute(unsigned workerid, struct starpu_task *task, unsigned nimpl)
{
	enum starpu_archtype type = starpu_worker_get_type(workerid);
	if (type == STARPU_CPU_WORKER || type == STARPU_OPENCL_WORKER)
		return 1;

#ifdef STARPU_USE_CUDA
	/* Cuda device */
	const struct cudaDeviceProp *props;
	props = starpu_cuda_get_device_properties(workerid);
	if (props->major >= 2 || props->minor >= 3)
		/* At least compute capability 1.3, supports doubles */
		return 1;
#endif
	/* Old card, does not support doubles */
	return 0;
}

/*
 *	Codelet to create a neutral element
 */

void init_cpu_func(void *descr[], void *cl_arg)
{
	DOT_TYPE *dot = (DOT_TYPE *)STARPU_VARIABLE_GET_PTR(descr[0]);
	*dot = 0.0f;
}

#ifdef STARPU_USE_CUDA
void init_cuda_func(void *descr[], void *cl_arg)
{
	DOT_TYPE *dot = (DOT_TYPE *)STARPU_VARIABLE_GET_PTR(descr[0]);
	cudaMemset(dot, 0, sizeof(DOT_TYPE));
	cudaThreadSynchronize();
}
#endif

#ifdef STARPU_USE_OPENCL
void init_opencl_func(void *buffers[], void *args)
{
        cl_int err;
	cl_command_queue queue;

	cl_mem dot = (cl_mem) STARPU_VARIABLE_GET_PTR(buffers[0]);
	starpu_opencl_get_current_queue(&queue);
	DOT_TYPE zero = (DOT_TYPE) 0.0;

	err = clEnqueueWriteBuffer(queue,
			dot,
			CL_TRUE,
			0,
			sizeof(DOT_TYPE),
			&zero,
			0,
			NULL,
			NULL);
	if (err != CL_SUCCESS)
		STARPU_OPENCL_REPORT_ERROR(err);

}
#endif

static struct starpu_codelet init_codelet =
{
	.can_execute = can_execute,
	.cpu_funcs = {init_cpu_func, NULL},
#ifdef STARPU_USE_CUDA
	.cuda_funcs = {init_cuda_func, NULL},
#endif
#ifdef STARPU_USE_OPENCL
	.opencl_funcs = {init_opencl_func, NULL},
#endif
	.nbuffers = 1
};

/*
 *	Codelet to perform the reduction of two elements
 */

void redux_cpu_func(void *descr[], void *cl_arg)
{
	DOT_TYPE *dota = (DOT_TYPE *)STARPU_VARIABLE_GET_PTR(descr[0]);
	DOT_TYPE *dotb = (DOT_TYPE *)STARPU_VARIABLE_GET_PTR(descr[1]);

	*dota = *dota + *dotb;
}

#ifdef STARPU_USE_CUDA
extern void redux_cuda_func(void *descr[], void *_args);
#endif

#ifdef STARPU_USE_OPENCL
void redux_opencl_func(void *buffers[], void *args)
{
	int id, devid;
        cl_int err;
	cl_kernel kernel;
	cl_command_queue queue;
	cl_event event;

	cl_mem dota = (cl_mem) STARPU_VARIABLE_GET_PTR(buffers[0]);
	cl_mem dotb = (cl_mem) STARPU_VARIABLE_GET_PTR(buffers[1]);

	id = starpu_worker_get_id();
	devid = starpu_worker_get_devid(id);

	err = starpu_opencl_load_kernel(&kernel, &queue, &opencl_program, "_redux_opencl", devid);
	if (err != CL_SUCCESS)
		STARPU_OPENCL_REPORT_ERROR(err);

	err = clSetKernelArg(kernel, 0, sizeof(dota), &dota);
	err|= clSetKernelArg(kernel, 1, sizeof(dotb), &dotb);
	if (err != CL_SUCCESS)
		STARPU_OPENCL_REPORT_ERROR(err);

	{
		size_t global=1;
		size_t local;
                size_t s;
                cl_device_id device;

                starpu_opencl_get_device(devid, &device);

                err = clGetKernelWorkGroupInfo (kernel, device, CL_KERNEL_WORK_GROUP_SIZE, sizeof(local), &local, &s);
                if (err != CL_SUCCESS)
			STARPU_OPENCL_REPORT_ERROR(err);
                if (local > global)
			local=global;

		err = clEnqueueNDRangeKernel(queue, kernel, 1, NULL, &global, &local, 0, NULL, &event);
		if (err != CL_SUCCESS)
			STARPU_OPENCL_REPORT_ERROR(err);
	}

	clFinish(queue);
	starpu_opencl_collect_stats(event);
	clReleaseEvent(event);

	starpu_opencl_release_kernel(kernel);
}
#endif

static struct starpu_codelet redux_codelet =
{
	.can_execute = can_execute,
	.cpu_funcs = {redux_cpu_func, NULL},
#ifdef STARPU_USE_CUDA
	.cuda_funcs = {redux_cuda_func, NULL},
#endif
#ifdef STARPU_USE_OPENCL
	.opencl_funcs = {redux_opencl_func, NULL},
#endif
	.nbuffers = 2
};

/*
 *	Dot product codelet
 */

void dot_cpu_func(void *descr[], void *cl_arg)
{
	float *local_x = (float *)STARPU_VECTOR_GET_PTR(descr[0]);
	float *local_y = (float *)STARPU_VECTOR_GET_PTR(descr[1]);
	DOT_TYPE *dot = (DOT_TYPE *)STARPU_VARIABLE_GET_PTR(descr[2]);

	unsigned n = STARPU_VECTOR_GET_NX(descr[0]);

	DOT_TYPE local_dot = 0.0;

	unsigned i;
	for (i = 0; i < n; i++)
	{
		local_dot += (DOT_TYPE)local_x[i]*(DOT_TYPE)local_y[i];
	}

	*dot = *dot + local_dot;
}

#ifdef STARPU_USE_CUDA
void dot_cuda_func(void *descr[], void *cl_arg)
{
	DOT_TYPE current_dot;
	DOT_TYPE local_dot;

	float *local_x = (float *)STARPU_VECTOR_GET_PTR(descr[0]);
	float *local_y = (float *)STARPU_VECTOR_GET_PTR(descr[1]);
	DOT_TYPE *dot = (DOT_TYPE *)STARPU_VARIABLE_GET_PTR(descr[2]);

	unsigned n = STARPU_VECTOR_GET_NX(descr[0]);

	cudaMemcpy(&current_dot, dot, sizeof(DOT_TYPE), cudaMemcpyDeviceToHost);

	cudaThreadSynchronize();

	local_dot = (DOT_TYPE)cublasSdot(n, local_x, 1, local_y, 1);

	/* FPRINTF(stderr, "current_dot %f local dot %f -> %f\n", current_dot, local_dot, current_dot + local_dot); */
	current_dot += local_dot;

	cudaThreadSynchronize();

	cudaMemcpy(dot, &current_dot, sizeof(DOT_TYPE), cudaMemcpyHostToDevice);

	cudaThreadSynchronize();
}
#endif

#ifdef STARPU_USE_OPENCL
void dot_opencl_func(void *buffers[], void *args)
{
	int id, devid;
        cl_int err;
	cl_kernel kernel;
	cl_command_queue queue;
	cl_event event;

	cl_mem x = (cl_mem) STARPU_VECTOR_GET_DEV_HANDLE(buffers[0]);
	cl_mem y = (cl_mem) STARPU_VECTOR_GET_DEV_HANDLE(buffers[1]);
	cl_mem dot = (cl_mem) STARPU_VARIABLE_GET_PTR(buffers[2]);
	unsigned n = STARPU_VECTOR_GET_NX(buffers[0]);

	id = starpu_worker_get_id();
	devid = starpu_worker_get_devid(id);

	err = starpu_opencl_load_kernel(&kernel, &queue, &opencl_program, "_dot_opencl", devid);
	if (err != CL_SUCCESS)
		STARPU_OPENCL_REPORT_ERROR(err);

	err = clSetKernelArg(kernel, 0, sizeof(x), &x);
	err|= clSetKernelArg(kernel, 1, sizeof(y), &y);
	err|= clSetKernelArg(kernel, 2, sizeof(dot), &dot);
	err|= clSetKernelArg(kernel, 3, sizeof(n), &n);
	if (err != CL_SUCCESS)
		STARPU_OPENCL_REPORT_ERROR(err);

	{
		size_t global=1;
		size_t local;
                size_t s;
                cl_device_id device;

                starpu_opencl_get_device(devid, &device);

                err = clGetKernelWorkGroupInfo (kernel, device, CL_KERNEL_WORK_GROUP_SIZE, sizeof(local), &local, &s);
                if (err != CL_SUCCESS)
			STARPU_OPENCL_REPORT_ERROR(err);
                if (local > global)
			local=global;

		err = clEnqueueNDRangeKernel(queue, kernel, 1, NULL, &global, &local, 0, NULL, &event);
		if (err != CL_SUCCESS)
			STARPU_OPENCL_REPORT_ERROR(err);
	}

	clFinish(queue);
	starpu_opencl_collect_stats(event);
	clReleaseEvent(event);

	starpu_opencl_release_kernel(kernel);
}
#endif

static struct starpu_codelet dot_codelet =
{
	.can_execute = can_execute,
	.cpu_funcs = {dot_cpu_func, NULL},
#ifdef STARPU_USE_CUDA
	.cuda_funcs = {dot_cuda_func, NULL},
#endif
#ifdef STARPU_USE_OPENCL
	.opencl_funcs = {dot_opencl_func, NULL},
#endif
	.nbuffers = 3,
	.modes = {STARPU_R, STARPU_R, STARPU_REDUX}
};

/*
 *	Tasks initialization
 */

int main(int argc, char **argv)
{
	int ret;

	ret = starpu_init(NULL);
	if (ret == -ENODEV)
		return 77;
	STARPU_CHECK_RETURN_VALUE(ret, "starpu_init");

#ifdef STARPU_USE_OPENCL
	ret = starpu_opencl_load_opencl_from_file("examples/reductions/dot_product_opencl_kernels.cl",
						  &opencl_program, NULL);
	STARPU_CHECK_RETURN_VALUE(ret, "starpu_opencl_load_opencl_from_file");
#endif

	starpu_helper_cublas_init();

	unsigned long nelems = nblocks*entries_per_block;
	size_t size = nelems*sizeof(float);

	x = (float *) malloc(size);
	y = (float *) malloc(size);

	x_handles = (starpu_data_handle_t *) calloc(nblocks, sizeof(starpu_data_handle_t));
	y_handles = (starpu_data_handle_t *) calloc(nblocks, sizeof(starpu_data_handle_t));

	assert(x && y);

        starpu_srand48(0);
	
	DOT_TYPE reference_dot = 0.0;

	unsigned long i;
	for (i = 0; i < nelems; i++)
	{
		x[i] = (float)starpu_drand48();
		y[i] = (float)starpu_drand48();

		reference_dot += (DOT_TYPE)x[i]*(DOT_TYPE)y[i];
	} 
	
	unsigned block;
	for (block = 0; block < nblocks; block++)
	{
		starpu_vector_data_register(&x_handles[block], 0,
			(uintptr_t)&x[entries_per_block*block], entries_per_block, sizeof(float));
		starpu_vector_data_register(&y_handles[block], 0,
			(uintptr_t)&y[entries_per_block*block], entries_per_block, sizeof(float));
	}

	starpu_variable_data_register(&dot_handle, 0, (uintptr_t)&dot, sizeof(DOT_TYPE));

	/*
	 *	Compute dot product with StarPU
	 */
	starpu_data_set_reduction_methods(dot_handle, &redux_codelet, &init_codelet);

	for (block = 0; block < nblocks; block++)
	{
		struct starpu_task *task = starpu_task_create();

		task->cl = &dot_codelet;
		task->destroy = 1;

		task->handles[0] = x_handles[block];
		task->handles[1] = y_handles[block];
		task->handles[2] = dot_handle;

		int ret = starpu_task_submit(task);
		if (ret == -ENODEV) goto enodev;
		STARPU_ASSERT(!ret);
	}

	for (block = 0; block < nblocks; block++)
	{
		starpu_data_unregister(x_handles[block]);
		starpu_data_unregister(y_handles[block]);
	}
	starpu_data_unregister(dot_handle);

	FPRINTF(stderr, "Reference : %e vs. %e (Delta %e)\n", reference_dot, dot, reference_dot - dot);

	starpu_helper_cublas_shutdown();

#ifdef STARPU_USE_OPENCL
        ret = starpu_opencl_unload_opencl(&opencl_program);
        STARPU_CHECK_RETURN_VALUE(ret, "starpu_opencl_unload_opencl");
#endif
	starpu_shutdown();

	free(x);
	free(y);
	free(x_handles);
	free(y_handles);

	if (fabs(reference_dot - dot) < reference_dot * 1e-6)
		return EXIT_SUCCESS;
	else
		return EXIT_FAILURE;

enodev:
	fprintf(stderr, "WARNING: No one can execute this task\n");
	/* yes, we do not perform the computation but we did detect that no one
 	 * could perform the kernel, so this is not an error from StarPU */
	return 77;
}