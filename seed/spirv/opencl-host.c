// SPDX-License-Identifier: Apache-2.0
// Verification/demo host only: runs one irspirv kernel on a PoCL CPU device.
// It never generates IR or SPIR-V and is not part of the translator build.
//
// usage: opencl-host MODULE.spv KERNEL [ARG ...]   (up to 6 canonical u64 decimals)
// stdout: "VALUE STATUS\n" read back from outputs[0] and outputs[1].
// Exit 0 only when every OpenCL call succeeded; any usage, environment, API or
// runtime failure exits nonzero with a named stderr line and prints nothing on
// stdout. Device and toolchain identity are recorded on stderr.
//
// GPU isolation: the build pins one absolute PoCL shared-library path. Inherited
// vendor/layer discovery settings are overwritten before the first OpenCL call.
// Only CL_DEVICE_TYPE_CPU devices are enumerated; POCL_DEVICES is forced to cpu.

#define _POSIX_C_SOURCE 200809L
#define CL_TARGET_OPENCL_VERSION 300
#include <CL/cl.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

enum {
	EXIT_USAGE = 2,
	EXIT_ENVIRONMENT = 3,
	EXIT_MODULE = 4,
	EXIT_DEVICE = 5,
	EXIT_PROGRAM = 6,
	EXIT_KERNEL = 7,
	EXIT_RUN = 8,
};

#define MAX_ARGS 6
#define INFO_MAX 1024
#define POISON UINT64_C(0xa5a5a5a5a5a5a5a5)
#ifndef IRSPIRV_POCL_LIBRARY
#define IRSPIRV_POCL_LIBRARY ""
#endif

static int fail(int code, const char *what, cl_int err)
{
	fprintf(stderr, "opencl-host: %s failed (cl error %d)\n", what, (int)err);
	return code;
}

static int refuse(int code, const char *what)
{
	fprintf(stderr, "opencl-host: %s\n", what);
	return code;
}

// Canonical unsigned decimal: digits only, no sign, no leading zero, fits 64 bits.
static int parse_u64(const char *text, cl_ulong *out)
{
	size_t length = strlen(text);
	if (length == 0 || length > 20 || (text[0] == '0' && length > 1))
		return -1;
	for (size_t i = 0; i < length; i++)
		if (text[i] < '0' || text[i] > '9')
			return -1;
	errno = 0;
	char *end = NULL;
	unsigned long long value = strtoull(text, &end, 10);
	if (errno != 0 || end != text + length)
		return -1;
	*out = (cl_ulong)value;
	return 0;
}

// ocl-icd 2.3.2 accepts an absolute shared-library path in OCL_ICD_VENDORS.
// A fixed library avoids both untrusted manifest paths and manifest re-open races.
// The configured library and its dependency directories must be trusted installs.
static int pin_loader(void)
{
	const char *library = IRSPIRV_POCL_LIBRARY;
	if (library[0] != '/')
		return refuse(EXIT_ENVIRONMENT, "build requires an absolute IRSPIRV_POCL_LIBRARY");
	const char *base = strrchr(library, '/');
	if (base == NULL || strncmp(base + 1, "libpocl.so", 10) != 0)
		return refuse(EXIT_ENVIRONMENT, "configured library is not libpocl.so");
	if (setenv("OCL_ICD_VENDORS", library, 1) != 0 ||
	    unsetenv("OCL_ICD_FILENAMES") != 0 || unsetenv("OPENCL_LAYERS") != 0 ||
	    unsetenv("OPENCL_VENDOR_PATH") != 0 ||
	    setenv("OPENCL_LAYER_PATH", "/dev/null", 1) != 0 ||
	    setenv("POCL_DEVICES", "cpu", 1) != 0)
		return refuse(EXIT_ENVIRONMENT, "cannot pin loader environment");
	fprintf(stderr, "opencl-host: library=%s POCL_DEVICES=cpu layers=disabled\n", library);
	return 0;
}

static int platform_text(cl_platform_id platform, cl_platform_info key, char *out)
{
	size_t size = 0;
	cl_int err = clGetPlatformInfo(platform, key, INFO_MAX, out, &size);
	if (err != CL_SUCCESS || size == 0 || size > INFO_MAX)
		return -1;
	out[size - 1] = '\0';
	return 0;
}

static int device_text(cl_device_id device, cl_device_info key, char *out)
{
	size_t size = 0;
	cl_int err = clGetDeviceInfo(device, key, INFO_MAX, out, &size);
	if (err != CL_SUCCESS || size == 0 || size > INFO_MAX)
		return -1;
	out[size - 1] = '\0';
	return 0;
}

// Reads the whole module; SPIR-V is a nonempty sequence of 32-bit words.
static unsigned char *read_module(const char *path, size_t *size)
{
	FILE *file = fopen(path, "rb");
	if (file == NULL)
		return NULL;
	struct stat info;
	if (fstat(fileno(file), &info) != 0 || !S_ISREG(info.st_mode) ||
	    info.st_size < 20 || info.st_size > 8388608 || info.st_size % 4 != 0) {
		fclose(file);
		return NULL;
	}
	size_t used = (size_t)info.st_size;
	unsigned char *bytes = malloc(used);
	if (bytes == NULL) {
		fclose(file);
		return NULL;
	}
	size_t got = fread(bytes, 1, used, file);
	int bad = got != used || ferror(file) || fgetc(file) != EOF;
	fclose(file);
	if (bad) {
		free(bytes);
		return NULL;
	}
	*size = used;
	return bytes;
}

static void print_build_log(cl_program program, cl_device_id device, int always)
{
	size_t size = 0;
	if (clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, 0, NULL, &size) != CL_SUCCESS || size == 0)
		return;
	char *log = malloc(size);
	if (log == NULL)
		return;
	if (clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, size, log, NULL) == CL_SUCCESS) {
		log[size - 1] = '\0';
		if (always || log[strspn(log, " \t\r\n")] != '\0')
			fprintf(stderr, "opencl-host: build log:\n%s\n", log);
	}
	free(log);
}

int main(int argc, char **argv)
{
	if (argc < 3 || argc > 3 + MAX_ARGS)
		return refuse(EXIT_USAGE, "usage: opencl-host MODULE.spv KERNEL [u64 ...] (at most 6 arguments)");
	cl_ulong inputs[MAX_ARGS] = { 0 };
	for (int i = 3; i < argc; i++)
		if (parse_u64(argv[i], &inputs[i - 3]) != 0) {
			fprintf(stderr, "opencl-host: argument %d is not a canonical u64 decimal: %s\n", i - 2, argv[i]);
			return EXIT_USAGE;
		}
	if (argv[2][0] == '\0')
		return refuse(EXIT_USAGE, "empty kernel name");

	int rc = pin_loader();
	if (rc != 0)
		return rc;

	size_t il_size = 0;
	unsigned char *il = read_module(argv[1], &il_size);
	if (il == NULL)
		return refuse(EXIT_MODULE, "cannot read module or size is not a nonzero multiple of 4 bytes");

	cl_context context = NULL;
	cl_command_queue queue = NULL;
	cl_program program = NULL;
	cl_kernel kernel = NULL;
	cl_mem in_buffer = NULL, out_buffer = NULL;
	cl_event done = NULL;
	char text[INFO_MAX];
	cl_int err;

	cl_uint platforms = 0;
	cl_platform_id platform;
	err = clGetPlatformIDs(1, &platform, &platforms);
	if (err != CL_SUCCESS) {
		rc = fail(EXIT_DEVICE, "clGetPlatformIDs", err);
		goto out;
	}
	if (platforms != 1) {
		rc = refuse(EXIT_DEVICE, "expected exactly one (PoCL) platform");
		goto out;
	}
	if (platform_text(platform, CL_PLATFORM_NAME, text) != 0 || strcmp(text, "Portable Computing Language") != 0) {
		rc = refuse(EXIT_DEVICE, "platform is not PoCL");
		goto out;
	}
	fprintf(stderr, "opencl-host: platform=%s", text);
	if (platform_text(platform, CL_PLATFORM_VERSION, text) == 0)
		fprintf(stderr, " version=\"%s\"", text);
	fputc('\n', stderr);

	cl_uint devices = 0;
	cl_device_id device;
	err = clGetDeviceIDs(platform, CL_DEVICE_TYPE_CPU, 1, &device, &devices);
	if (err != CL_SUCCESS) {
		rc = fail(EXIT_DEVICE, "clGetDeviceIDs(CL_DEVICE_TYPE_CPU)", err);
		goto out;
	}
	cl_device_type type = 0;
	err = clGetDeviceInfo(device, CL_DEVICE_TYPE, sizeof type, &type, NULL);
	if (err != CL_SUCCESS || type != CL_DEVICE_TYPE_CPU) {
		rc = refuse(EXIT_DEVICE, "selected device is not a CPU");
		goto out;
	}
	if (device_text(device, CL_DEVICE_IL_VERSION, text) != 0 || strstr(text, "SPIR-V_1.0") == NULL) {
		rc = refuse(EXIT_DEVICE, "CPU device does not accept SPIR-V 1.0 IL");
		goto out;
	}
	fprintf(stderr, "opencl-host: il=\"%s\"", text);
	if (device_text(device, CL_DEVICE_NAME, text) == 0)
		fprintf(stderr, " device=%s", text);
	if (device_text(device, CL_DEVICE_VERSION, text) == 0)
		fprintf(stderr, " device-version=\"%s\"", text);
	if (device_text(device, CL_DRIVER_VERSION, text) == 0)
		fprintf(stderr, " driver=%s", text);
	fprintf(stderr, " cpu-devices=%u host-cc=\"%s\"\n", (unsigned)devices, __VERSION__);

	context = clCreateContext(NULL, 1, &device, NULL, NULL, &err);
	if (err != CL_SUCCESS) {
		rc = fail(EXIT_DEVICE, "clCreateContext", err);
		goto out;
	}
	queue = clCreateCommandQueueWithProperties(context, device, NULL, &err);
	if (err != CL_SUCCESS) {
		rc = fail(EXIT_DEVICE, "clCreateCommandQueueWithProperties", err);
		goto out;
	}

	program = clCreateProgramWithIL(context, il, il_size, &err);
	if (err != CL_SUCCESS) {
		rc = fail(EXIT_PROGRAM, "clCreateProgramWithIL", err);
		goto out;
	}
	err = clBuildProgram(program, 1, &device, "", NULL, NULL);
	print_build_log(program, device, err != CL_SUCCESS);
	if (err != CL_SUCCESS) {
		rc = fail(EXIT_PROGRAM, "clBuildProgram", err);
		goto out;
	}

	kernel = clCreateKernel(program, argv[2], &err);
	if (err != CL_SUCCESS) {
		rc = fail(EXIT_KERNEL, "clCreateKernel", err);
		goto out;
	}
	cl_uint kernel_args = 0;
	err = clGetKernelInfo(kernel, CL_KERNEL_NUM_ARGS, sizeof kernel_args, &kernel_args, NULL);
	if (err != CL_SUCCESS) {
		rc = fail(EXIT_KERNEL, "clGetKernelInfo(CL_KERNEL_NUM_ARGS)", err);
		goto out;
	}
	if (kernel_args != 2) {
		rc = refuse(EXIT_KERNEL, "kernel signature is not (inputs, outputs)");
		goto out;
	}

	// All six input slots are always backed; unused slots are zero.
	cl_ulong outputs[2] = { POISON, POISON };
	in_buffer = clCreateBuffer(context, CL_MEM_READ_ONLY | CL_MEM_COPY_HOST_PTR, sizeof inputs, inputs, &err);
	if (err != CL_SUCCESS) {
		rc = fail(EXIT_RUN, "clCreateBuffer(inputs)", err);
		goto out;
	}
	out_buffer = clCreateBuffer(context, CL_MEM_READ_WRITE | CL_MEM_COPY_HOST_PTR, sizeof outputs, outputs, &err);
	if (err != CL_SUCCESS) {
		rc = fail(EXIT_RUN, "clCreateBuffer(outputs)", err);
		goto out;
	}
	err = clSetKernelArg(kernel, 0, sizeof in_buffer, &in_buffer);
	if (err != CL_SUCCESS) {
		rc = fail(EXIT_KERNEL, "clSetKernelArg(0)", err);
		goto out;
	}
	err = clSetKernelArg(kernel, 1, sizeof out_buffer, &out_buffer);
	if (err != CL_SUCCESS) {
		rc = fail(EXIT_KERNEL, "clSetKernelArg(1)", err);
		goto out;
	}

	size_t one = 1;
	err = clEnqueueNDRangeKernel(queue, kernel, 1, NULL, &one, &one, 0, NULL, &done);
	if (err != CL_SUCCESS) {
		rc = fail(EXIT_RUN, "clEnqueueNDRangeKernel", err);
		goto out;
	}
	err = clEnqueueReadBuffer(queue, out_buffer, CL_TRUE, 0, sizeof outputs, outputs, 1, &done, NULL);
	if (err != CL_SUCCESS) {
		rc = fail(EXIT_RUN, "clEnqueueReadBuffer", err);
		goto out;
	}
	cl_int status = 0;
	err = clGetEventInfo(done, CL_EVENT_COMMAND_EXECUTION_STATUS, sizeof status, &status, NULL);
	if (err != CL_SUCCESS) {
		rc = fail(EXIT_RUN, "clGetEventInfo", err);
		goto out;
	}
	if (status != CL_COMPLETE) {
		rc = fail(EXIT_RUN, "kernel execution", status);
		goto out;
	}

	if (printf("%llu %llu\n", (unsigned long long)outputs[0], (unsigned long long)outputs[1]) < 0 ||
	    fflush(stdout) != 0) {
		rc = refuse(EXIT_RUN, "cannot write result");
		goto out;
	}
	rc = 0;

out:
	if (done)
		clReleaseEvent(done);
	if (out_buffer)
		clReleaseMemObject(out_buffer);
	if (in_buffer)
		clReleaseMemObject(in_buffer);
	if (kernel)
		clReleaseKernel(kernel);
	if (program)
		clReleaseProgram(program);
	if (queue)
		clReleaseCommandQueue(queue);
	if (context)
		clReleaseContext(context);
	free(il);
	return rc;
}
