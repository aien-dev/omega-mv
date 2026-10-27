// SPDX-License-Identifier: Apache-2.0
// INTERIM verification host: runs one vkspirv GLCompute entry point on one
// build-pinned Vulkan driver. It never generates IR or SPIR-V and is not
// part of the translator. Successor: seed/spirv/vkhost.om, Omega's own Vulkan
// loader binding through the native ABI (vkCreateInstance .. vkQueueSubmit
// called from seeded Omega); until that exists this file is a foreign oracle
// recorded as such, never an Omega implementation.
//
// usage: vk-host MODULE.spv ENTRY NPARAMS < ROWS
//   ROWS: one line per invocation, NPARAMS canonical u64 decimals separated by
//   single spaces (an empty line when NPARAMS is 0). At most 1048576 rows.
// stdout: "VALUE STATUS\n" per row, read back from outputs[2g] and outputs[2g+1].
// Exit 0 only when every Vulkan call succeeded and every row was written; any
// usage, environment, API or runtime failure exits nonzero with a named stderr
// line and prints nothing on stdout. Device and driver identity go to stderr.
//
// Driver isolation: the build pins one target and one absolute driver path.
//   VKSPIRV_TARGET=0 lavapipe: VKSPIRV_ICD is the lavapipe ICD manifest; the only
//     accepted device is VK_PHYSICAL_DEVICE_TYPE_CPU "llvmpipe*", MESA_LLVMPIPE.
//   VKSPIRV_TARGET=1 MoltenVK: the binary links libMoltenVK directly (no loader);
//     VKSPIRV_ICD names that dylib; accepted driverID is MOLTENVK. This is a
//     Vulkan-on-Metal translation layer, not a native Vulkan driver.
//   VKSPIRV_TARGET=2 NVIDIA: VKSPIRV_ICD is the nvidia ICD manifest; accepted
//     device is a GPU with driverID NVIDIA_PROPRIETARY. Build/run it only inside
//     an authorized GPU window.
// Loader discovery, driver selection and layer variables are overwritten or
// removed before the first Vulkan call and implicit layers are disabled, so an
// inherited environment can never select a different driver.

#define _POSIX_C_SOURCE 200809L
#include <vulkan/vulkan.h>

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
	EXIT_USAGE = 2,
	EXIT_ENVIRONMENT = 3,
	EXIT_MODULE = 4,
	EXIT_DEVICE = 5,
	EXIT_PIPELINE = 6,
	EXIT_INPUT = 7,
	EXIT_RUN = 8,
};

#define MAX_PARAMS 6
#define MAX_ROWS 1048576u
#define LOCAL_SIZE 64u
#define POISON UINT64_C(0xa5a5a5a5a5a5a5a5)
#ifndef VKSPIRV_ICD
#define VKSPIRV_ICD ""
#endif
#ifndef VKSPIRV_TARGET
#define VKSPIRV_TARGET 0
#endif
#if VKSPIRV_TARGET == 0
#define TARGET_NAME "lavapipe"
#elif VKSPIRV_TARGET == 1
#define TARGET_NAME "moltenvk"
#elif VKSPIRV_TARGET == 2
#define TARGET_NAME "nvidia"
#else
#error "VKSPIRV_TARGET must be 0 (lavapipe), 1 (MoltenVK) or 2 (NVIDIA)"
#endif

static int refuse(int code, const char *what)
{
	fprintf(stderr, "vk-host: %s\n", what);
	return code;
}

static int vkfail(int code, const char *what, VkResult r)
{
	fprintf(stderr, "vk-host: %s failed (VkResult %d)\n", what, (int)r);
	return code;
}

static int parse_u64(const char *text, size_t length, uint64_t *out)
{
	if (length == 0 || length > 20 || (text[0] == '0' && length > 1))
		return -1;
	uint64_t v = 0;
	for (size_t i = 0; i < length; i++) {
		if (text[i] < '0' || text[i] > '9')
			return -1;
		unsigned d = (unsigned)(text[i] - '0');
		if (v > (UINT64_MAX - d) / 10)
			return -1;
		v = v * 10 + d;
	}
	*out = v;
	return 0;
}

static int pin_loader(void)
{
	const char *icd = VKSPIRV_ICD;
	if (icd[0] != '/')
		return refuse(EXIT_ENVIRONMENT, "build requires an absolute VKSPIRV_ICD driver path");
	static const char *drop[] = {
		"VK_ADD_DRIVER_FILES", "VK_LAYER_PATH", "VK_ADD_LAYER_PATH",
		"VK_IMPLICIT_LAYER_PATH", "VK_ADD_IMPLICIT_LAYER_PATH",
		"VK_INSTANCE_LAYERS", "VK_LOADER_LAYERS_ENABLE",
		"VK_LOADER_DRIVERS_SELECT", "VK_LOADER_DRIVERS_DISABLE",
		"VK_LOADER_DEVICE_SELECT", "MESA_VK_DEVICE_SELECT",
	};
	for (size_t i = 0; i < sizeof drop / sizeof drop[0]; i++)
		if (unsetenv(drop[i]) != 0)
			return refuse(EXIT_ENVIRONMENT, "cannot clear loader variable");
	if (setenv("VK_DRIVER_FILES", icd, 1) != 0 || setenv("VK_ICD_FILENAMES", icd, 1) != 0 ||
	    setenv("VK_LOADER_LAYERS_DISABLE", "~all~", 1) != 0)
		return refuse(EXIT_ENVIRONMENT, "cannot pin loader variables");
	fprintf(stderr, "vk-host: target=%s VK_ICD_FILENAMES=%s VK_DRIVER_FILES=%s layers=disabled%s\n",
		TARGET_NAME, icd, icd, VKSPIRV_TARGET == 1 ? " linked=direct-no-loader" : "");
	return 0;
}

static void *read_file(const char *path, size_t *size)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return NULL;
	void *buf = NULL;
	if (fseek(f, 0, SEEK_END) == 0) {
		long n = ftell(f);
		if (n > 0 && n % 4 == 0 && n <= (1L << 26) && fseek(f, 0, SEEK_SET) == 0) {
			buf = malloc((size_t)n);
			if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) {
				free(buf);
				buf = NULL;
			}
			*size = (size_t)n;
		}
	}
	fclose(f);
	return buf;
}

// Reads rows into a malloc'd array of rows*nparams words; returns row count or 0.
static uint32_t read_rows(unsigned nparams, uint64_t **out)
{
	size_t cap = 1024, rows = 0;
	uint64_t *v = malloc(cap * (nparams ? nparams : 1) * sizeof *v);
	char line[512];
	if (!v)
		return 0;
	while (fgets(line, sizeof line, stdin)) {
		size_t n = strlen(line);
		if (n == 0 || line[n - 1] != '\n' || rows == MAX_ROWS)
			goto bad;
		line[--n] = 0;
		if (rows == cap) {
			cap *= 2;
			uint64_t *g = realloc(v, cap * (nparams ? nparams : 1) * sizeof *v);
			if (!g)
				goto bad;
			v = g;
		}
		size_t p = 0;
		for (unsigned j = 0; j < nparams; j++) {
			size_t q = p;
			while (line[q] && line[q] != ' ')
				q++;
			if (parse_u64(line + p, q - p, &v[rows * nparams + j]) != 0)
				goto bad;
			if (j + 1 < nparams) {
				if (line[q] != ' ')
					goto bad;
				q++;
			}
			p = q;
		}
		if (line[p] != 0)
			goto bad;
		rows++;
	}
	if (ferror(stdin) || rows == 0)
		goto bad;
	*out = v;
	return (uint32_t)rows;
bad:
	free(v);
	return 0;
}

static int find_memory(VkPhysicalDevice pd, uint32_t bits, uint32_t *index)
{
	VkPhysicalDeviceMemoryProperties mp;
	vkGetPhysicalDeviceMemoryProperties(pd, &mp);
	VkMemoryPropertyFlags want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
	for (uint32_t i = 0; i < mp.memoryTypeCount; i++)
		if ((bits & (1u << i)) && (mp.memoryTypes[i].propertyFlags & want) == want) {
			*index = i;
			return 0;
		}
	return -1;
}

static int make_buffer(VkPhysicalDevice pd, VkDevice dev, VkDeviceSize size, VkBuffer *buf,
		       VkDeviceMemory *mem, void **map)
{
	VkBufferCreateInfo bi = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = size,
		.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
		.sharingMode = VK_SHARING_MODE_EXCLUSIVE,
	};
	VkResult r = vkCreateBuffer(dev, &bi, NULL, buf);
	if (r != VK_SUCCESS)
		return vkfail(EXIT_DEVICE, "vkCreateBuffer", r);
	VkMemoryRequirements req;
	vkGetBufferMemoryRequirements(dev, *buf, &req);
	uint32_t type;
	if (find_memory(pd, req.memoryTypeBits, &type) != 0)
		return refuse(EXIT_DEVICE, "no host-visible coherent memory type");
	VkMemoryAllocateInfo ai = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = req.size,
		.memoryTypeIndex = type,
	};
	if ((r = vkAllocateMemory(dev, &ai, NULL, mem)) != VK_SUCCESS)
		return vkfail(EXIT_DEVICE, "vkAllocateMemory", r);
	if ((r = vkBindBufferMemory(dev, *buf, *mem, 0)) != VK_SUCCESS)
		return vkfail(EXIT_DEVICE, "vkBindBufferMemory", r);
	if ((r = vkMapMemory(dev, *mem, 0, VK_WHOLE_SIZE, 0, map)) != VK_SUCCESS)
		return vkfail(EXIT_DEVICE, "vkMapMemory", r);
	return 0;
}

static int pick_device(VkInstance inst, VkPhysicalDevice *out)
{
	uint32_t n = 0;
	VkResult r = vkEnumeratePhysicalDevices(inst, &n, NULL);
	if (r != VK_SUCCESS)
		return vkfail(EXIT_DEVICE, "vkEnumeratePhysicalDevices", r);
	if (n != 1)
		return refuse(EXIT_DEVICE, "pinned ICD must expose exactly one physical device");
	if ((r = vkEnumeratePhysicalDevices(inst, &n, out)) != VK_SUCCESS)
		return vkfail(EXIT_DEVICE, "vkEnumeratePhysicalDevices", r);
	VkPhysicalDeviceDriverProperties drv = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES};
	VkPhysicalDeviceProperties2 p2 = {.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = &drv};
	vkGetPhysicalDeviceProperties2(*out, &p2);
	const VkPhysicalDeviceProperties *p = &p2.properties;
	fprintf(stderr, "vk-host: device=\"%s\" type=%d driver=\"%s\" info=\"%s\" api=%u.%u.%u vendor=0x%x\n",
		p->deviceName, (int)p->deviceType, drv.driverName, drv.driverInfo,
		VK_API_VERSION_MAJOR(p->apiVersion), VK_API_VERSION_MINOR(p->apiVersion),
		VK_API_VERSION_PATCH(p->apiVersion), p->vendorID);
#if VKSPIRV_TARGET == 0
	if (p->deviceType != VK_PHYSICAL_DEVICE_TYPE_CPU || strncmp(p->deviceName, "llvmpipe", 8) != 0 ||
	    drv.driverID != VK_DRIVER_ID_MESA_LLVMPIPE)
		return refuse(EXIT_DEVICE, "physical device is not lavapipe (llvmpipe CPU)");
#elif VKSPIRV_TARGET == 1
	if (drv.driverID != VK_DRIVER_ID_MOLTENVK)
		return refuse(EXIT_DEVICE, "physical device is not MoltenVK");
#else
	if ((p->deviceType != VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU &&
	     p->deviceType != VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) ||
	    drv.driverID != VK_DRIVER_ID_NVIDIA_PROPRIETARY)
		return refuse(EXIT_DEVICE, "physical device is not an NVIDIA proprietary-driver GPU");
#endif
	VkPhysicalDeviceFeatures f;
	vkGetPhysicalDeviceFeatures(*out, &f);
	if (!f.shaderInt64)
		return refuse(EXIT_DEVICE, "device lacks shaderInt64");
	return 0;
}

int main(int argc, char **argv)
{
	if (argc != 4)
		return refuse(EXIT_USAGE, "usage: vk-host MODULE.spv ENTRY NPARAMS < ROWS");
	uint64_t np64;
	if (parse_u64(argv[3], strlen(argv[3]), &np64) != 0 || np64 > MAX_PARAMS)
		return refuse(EXIT_USAGE, "NPARAMS must be 0..6");
	unsigned nparams = (unsigned)np64;
	int rc = pin_loader();
	if (rc)
		return rc;
	size_t codesize = 0;
	uint32_t *code = read_file(argv[1], &codesize);
	if (!code)
		return refuse(EXIT_MODULE, "cannot read module (nonempty, multiple of 4 bytes)");
	uint64_t *in = NULL;
	uint32_t rows = read_rows(nparams, &in);
	if (rows == 0)
		return refuse(EXIT_INPUT, "malformed or empty ROWS on stdin");

	VkApplicationInfo app = {
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.pApplicationName = "vkspirv-host",
		.apiVersion = VK_API_VERSION_1_3,
	};
	VkInstanceCreateInfo ii = {.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO, .pApplicationInfo = &app};
	VkInstance inst;
	VkResult r = vkCreateInstance(&ii, NULL, &inst);
	if (r != VK_SUCCESS)
		return vkfail(EXIT_DEVICE, "vkCreateInstance", r);
	VkPhysicalDevice pd;
	if ((rc = pick_device(inst, &pd)))
		return rc;

	uint32_t nq = 0, qf = UINT32_MAX;
	vkGetPhysicalDeviceQueueFamilyProperties(pd, &nq, NULL);
	VkQueueFamilyProperties qp[16];
	if (nq > 16)
		nq = 16;
	vkGetPhysicalDeviceQueueFamilyProperties(pd, &nq, qp);
	for (uint32_t i = 0; i < nq && qf == UINT32_MAX; i++)
		if (qp[i].queueFlags & VK_QUEUE_COMPUTE_BIT)
			qf = i;
	if (qf == UINT32_MAX)
		return refuse(EXIT_DEVICE, "no compute queue");
	float prio = 1.0f;
	VkDeviceQueueCreateInfo qi = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
		.queueFamilyIndex = qf,
		.queueCount = 1,
		.pQueuePriorities = &prio,
	};
	VkPhysicalDeviceFeatures want = {.shaderInt64 = VK_TRUE};
	// A portability (MoltenVK) device must have VK_KHR_portability_subset enabled.
	const char *ext[1];
	uint32_t next = 0, nprop = 0;
	VkExtensionProperties props[512];
	if ((r = vkEnumerateDeviceExtensionProperties(pd, NULL, &nprop, NULL)) != VK_SUCCESS)
		return vkfail(EXIT_DEVICE, "vkEnumerateDeviceExtensionProperties", r);
	if (nprop > 512)
		nprop = 512;
	if ((r = vkEnumerateDeviceExtensionProperties(pd, NULL, &nprop, props)) != VK_SUCCESS &&
	    r != VK_INCOMPLETE)
		return vkfail(EXIT_DEVICE, "vkEnumerateDeviceExtensionProperties", r);
	for (uint32_t i = 0; i < nprop; i++)
		if (strcmp(props[i].extensionName, "VK_KHR_portability_subset") == 0)
			ext[next++] = "VK_KHR_portability_subset";
	VkDeviceCreateInfo di = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.queueCreateInfoCount = 1,
		.pQueueCreateInfos = &qi,
		.enabledExtensionCount = next,
		.ppEnabledExtensionNames = next ? ext : NULL,
		.pEnabledFeatures = &want,
	};
	VkDevice dev;
	if ((r = vkCreateDevice(pd, &di, NULL, &dev)) != VK_SUCCESS)
		return vkfail(EXIT_DEVICE, "vkCreateDevice", r);
	VkQueue queue;
	vkGetDeviceQueue(dev, qf, 0, &queue);

	VkDeviceSize insize = (VkDeviceSize)rows * nparams * 8;
	if (insize == 0)
		insize = 8;
	VkDeviceSize outsize = (VkDeviceSize)rows * 16;
	VkBuffer bin, bout;
	VkDeviceMemory min, mout;
	void *pin, *pout;
	if ((rc = make_buffer(pd, dev, insize, &bin, &min, &pin)) ||
	    (rc = make_buffer(pd, dev, outsize, &bout, &mout, &pout)))
		return rc;
	memset(pin, 0, (size_t)insize);
	if (nparams)
		memcpy(pin, in, (size_t)rows * nparams * 8);
	uint64_t *o = pout;
	for (uint64_t i = 0; i < (uint64_t)rows * 2; i++)
		o[i] = POISON;

	VkShaderModuleCreateInfo si = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = codesize,
		.pCode = code,
	};
	VkShaderModule sm;
	if ((r = vkCreateShaderModule(dev, &si, NULL, &sm)) != VK_SUCCESS)
		return vkfail(EXIT_PIPELINE, "vkCreateShaderModule", r);
	VkDescriptorSetLayoutBinding lb[2] = {
		{.binding = 0, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1,
		 .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT},
		{.binding = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 1,
		 .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT},
	};
	VkDescriptorSetLayoutCreateInfo li = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
		.bindingCount = 2,
		.pBindings = lb,
	};
	VkDescriptorSetLayout dsl;
	if ((r = vkCreateDescriptorSetLayout(dev, &li, NULL, &dsl)) != VK_SUCCESS)
		return vkfail(EXIT_PIPELINE, "vkCreateDescriptorSetLayout", r);
	VkPipelineLayoutCreateInfo pli = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 1,
		.pSetLayouts = &dsl,
	};
	VkPipelineLayout pl;
	if ((r = vkCreatePipelineLayout(dev, &pli, NULL, &pl)) != VK_SUCCESS)
		return vkfail(EXIT_PIPELINE, "vkCreatePipelineLayout", r);
	VkComputePipelineCreateInfo ci = {
		.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
		.stage = {
			.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
			.stage = VK_SHADER_STAGE_COMPUTE_BIT,
			.module = sm,
			.pName = argv[2],
		},
		.layout = pl,
	};
	VkPipeline pipe;
	if ((r = vkCreateComputePipelines(dev, VK_NULL_HANDLE, 1, &ci, NULL, &pipe)) != VK_SUCCESS)
		return vkfail(EXIT_PIPELINE, "vkCreateComputePipelines", r);

	VkDescriptorPoolSize ps = {.type = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .descriptorCount = 2};
	VkDescriptorPoolCreateInfo dpi = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
		.maxSets = 1,
		.poolSizeCount = 1,
		.pPoolSizes = &ps,
	};
	VkDescriptorPool pool;
	if ((r = vkCreateDescriptorPool(dev, &dpi, NULL, &pool)) != VK_SUCCESS)
		return vkfail(EXIT_PIPELINE, "vkCreateDescriptorPool", r);
	VkDescriptorSetAllocateInfo dai = {
		.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
		.descriptorPool = pool,
		.descriptorSetCount = 1,
		.pSetLayouts = &dsl,
	};
	VkDescriptorSet set;
	if ((r = vkAllocateDescriptorSets(dev, &dai, &set)) != VK_SUCCESS)
		return vkfail(EXIT_PIPELINE, "vkAllocateDescriptorSets", r);
	VkDescriptorBufferInfo bi[2] = {
		{.buffer = bin, .offset = 0, .range = insize},
		{.buffer = bout, .offset = 0, .range = outsize},
	};
	VkWriteDescriptorSet w[2] = {
		{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set, .dstBinding = 0,
		 .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &bi[0]},
		{.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, .dstSet = set, .dstBinding = 1,
		 .descriptorCount = 1, .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, .pBufferInfo = &bi[1]},
	};
	vkUpdateDescriptorSets(dev, 2, w, 0, NULL);

	VkCommandPoolCreateInfo cpi = {.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO, .queueFamilyIndex = qf};
	VkCommandPool cp;
	if ((r = vkCreateCommandPool(dev, &cpi, NULL, &cp)) != VK_SUCCESS)
		return vkfail(EXIT_RUN, "vkCreateCommandPool", r);
	VkCommandBufferAllocateInfo cai = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = cp,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1,
	};
	VkCommandBuffer cb;
	if ((r = vkAllocateCommandBuffers(dev, &cai, &cb)) != VK_SUCCESS)
		return vkfail(EXIT_RUN, "vkAllocateCommandBuffers", r);
	VkCommandBufferBeginInfo cbi = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
	};
	if ((r = vkBeginCommandBuffer(cb, &cbi)) != VK_SUCCESS)
		return vkfail(EXIT_RUN, "vkBeginCommandBuffer", r);
	vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pipe);
	vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, pl, 0, 1, &set, 0, NULL);
	vkCmdDispatch(cb, (rows + LOCAL_SIZE - 1) / LOCAL_SIZE, 1, 1);
	VkMemoryBarrier mb = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
		.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT,
		.dstAccessMask = VK_ACCESS_HOST_READ_BIT,
	};
	vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &mb,
			     0, NULL, 0, NULL);
	if ((r = vkEndCommandBuffer(cb)) != VK_SUCCESS)
		return vkfail(EXIT_RUN, "vkEndCommandBuffer", r);
	VkFenceCreateInfo fi = {.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
	VkFence fence;
	if ((r = vkCreateFence(dev, &fi, NULL, &fence)) != VK_SUCCESS)
		return vkfail(EXIT_RUN, "vkCreateFence", r);
	VkSubmitInfo sub = {.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO, .commandBufferCount = 1, .pCommandBuffers = &cb};
	if ((r = vkQueueSubmit(queue, 1, &sub, fence)) != VK_SUCCESS)
		return vkfail(EXIT_RUN, "vkQueueSubmit", r);
	if ((r = vkWaitForFences(dev, 1, &fence, VK_TRUE, UINT64_C(600000000000))) != VK_SUCCESS)
		return vkfail(EXIT_RUN, "vkWaitForFences", r);
	for (uint32_t g = 0; g < rows; g++)
		if (o[2 * g + 1] == POISON)
			return refuse(EXIT_RUN, "an invocation did not write its status word");
	for (uint32_t g = 0; g < rows; g++)
		if (printf("%llu %llu\n", (unsigned long long)o[2 * g], (unsigned long long)o[2 * g + 1]) < 0)
			return refuse(EXIT_RUN, "stdout write failed");
	if (fflush(stdout) != 0)
		return refuse(EXIT_RUN, "stdout flush failed");
	fprintf(stderr, "vk-host: rows=%u nparams=%u workgroups=%u local_size=%u entry=%s\n", rows, nparams,
		(rows + LOCAL_SIZE - 1) / LOCAL_SIZE, LOCAL_SIZE, argv[2]);
	vkDestroyFence(dev, fence, NULL);
	vkDestroyCommandPool(dev, cp, NULL);
	vkDestroyDescriptorPool(dev, pool, NULL);
	vkDestroyPipeline(dev, pipe, NULL);
	vkDestroyPipelineLayout(dev, pl, NULL);
	vkDestroyDescriptorSetLayout(dev, dsl, NULL);
	vkDestroyShaderModule(dev, sm, NULL);
	vkUnmapMemory(dev, min);
	vkUnmapMemory(dev, mout);
	vkDestroyBuffer(dev, bin, NULL);
	vkDestroyBuffer(dev, bout, NULL);
	vkFreeMemory(dev, min, NULL);
	vkFreeMemory(dev, mout, NULL);
	vkDestroyDevice(dev, NULL);
	vkDestroyInstance(inst, NULL);
	free(in);
	free(code);
	return 0;
}
