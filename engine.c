#include "engine.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <vulkan/vulkan_core.h>

#define WIDTH 600
#define HEIGHT 600

#define PI 3.14159265358979323846

#define VK_CHECK(expr) do { \
    VkResult result = expr; \
    if (result != VK_SUCCESS) { \
        fprintf(stderr, "%s failed with error: %d\n", #expr, result); \
        exit(1); \
    } \
} while(0)

static 
void vulkan_init(Engine *e, Display *display, Window window) {
	// driver vendor may use this
	VkApplicationInfo app_info = {
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "ApplicationName",
        .applicationVersion = VK_MAKE_VERSION(1, 0, 0),
        .pEngineName = "EngineName",
        .engineVersion = VK_MAKE_VERSION(1, 0, 0),
        .apiVersion = VK_API_VERSION_1_0,
    };

    const char *global_extensions[] = {
        VK_KHR_SURFACE_EXTENSION_NAME,
        VK_KHR_XLIB_SURFACE_EXTENSION_NAME,
    };

	// TODO: VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR can be set for flags
	// TODO: layers
    VkInstanceCreateInfo instance_ci = {
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pApplicationInfo = &app_info,
        .enabledExtensionCount = sizeof(global_extensions) / sizeof(const char *),
        .ppEnabledExtensionNames = global_extensions,
    };

    VK_CHECK(vkCreateInstance(&instance_ci, NULL, &e->instance));

	// Create Vulkan surface for X11 window
	VkXlibSurfaceCreateInfoKHR xlib_surface_ci = {
		.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR,
		.dpy = display,
		.window = window,
	};

	VK_CHECK(vkCreateXlibSurfaceKHR(e->instance,  &xlib_surface_ci, NULL, &e->surface));

	// TODO: pick device better, may be you want specific one
	{
		uint32_t device_count = 0;
		VK_CHECK(vkEnumeratePhysicalDevices(e->instance, &device_count, NULL));
		VkPhysicalDevice *phys_devices = malloc(device_count * sizeof(VkPhysicalDevice));
		VK_CHECK(vkEnumeratePhysicalDevices(e->instance, &device_count, phys_devices));

		e->phys_device = VK_NULL_HANDLE;

		for (uint32_t i = 0; i < device_count; i++) {
			VkBool32 supported = VK_FALSE;
			VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(phys_devices[i], 0, e->surface, &supported));
			if (supported == VK_TRUE) {
				e->phys_device = phys_devices[i];
				break;
			}
		}

		// TODO: is it even possible?
		if (e->phys_device == VK_NULL_HANDLE) {
			fprintf(stderr, "e->phys_device == VK_NULL_HANDLE\n");
			exit(1);
		}

		free(phys_devices);
	}

	e->graphics_queue_family = UINT32_MAX;

	// TODO: pick queue better, may be you want specific one
	{
		uint32_t queue_family_count = 0;
		vkGetPhysicalDeviceQueueFamilyProperties(e->phys_device, &queue_family_count, NULL);
		VkQueueFamilyProperties *queue_families = malloc(queue_family_count * sizeof(VkQueueFamilyProperties));
		vkGetPhysicalDeviceQueueFamilyProperties(e->phys_device, &queue_family_count, queue_families);

		for (uint32_t i = 0; i < queue_family_count; i++) {
			if (queue_families[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
				VkBool32 supported = VK_FALSE;
				VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(e->phys_device, i, e->surface, &supported));
				if (supported == VK_TRUE) {
					e->graphics_queue_family = i;
					break;
				}
			}
		}

		if (e->graphics_queue_family == UINT32_MAX) {
			fprintf(stderr, "e->graphics_queue_family == UINT32_MAX\n");
			exit(1);
		}

		free(queue_families);
	}

	const float queue_priorities[] = {
		1.0f,
	};

	VkDeviceQueueCreateInfo queue_ci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .flags = 0,
        .queueFamilyIndex = e->graphics_queue_family,
        .queueCount = sizeof(queue_priorities) / sizeof(float),
        .pQueuePriorities = queue_priorities,
    };

	const char *device_extensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
    };

	// TODO: pNext can be something useful here, for features

	// .enabledLayerCount and .ppEnabledLayerNames deprecated
	// TODO: for some reason, there is still some recomendation to put here
	// https://registry.khronos.org/vulkan/specs/1.3-extensions/html/vkspec.html#extendingvulkan-layers-devicelayerdeprecation
	
    VkDeviceCreateInfo device_ci = {
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queue_ci,
        
        .enabledExtensionCount = sizeof(device_extensions) / sizeof(const char *),
    	.ppEnabledExtensionNames = device_extensions,
        .pEnabledFeatures = NULL
    };

    VK_CHECK(vkCreateDevice(e->phys_device, &device_ci, NULL, &e->device));

	vkGetDeviceQueue(e->device, e->graphics_queue_family, 0, &e->graphics_queue);
}

static
void vulkan_deinit(Engine *e) {
	// TODO vkDeviceWaitIdle can be used
	vkDestroyDevice(e->device, NULL);
}

static void memory_init(Engine *e) {
	{
		VkPhysicalDeviceMemoryProperties mem_properties;
		vkGetPhysicalDeviceMemoryProperties(e->phys_device, &mem_properties);

		// Now iterate over all memory heaps
		for (uint32_t i = 0; i < mem_properties.memoryHeapCount; i++) {
			VkMemoryHeap heap = mem_properties.memoryHeaps[i];
			double mib_size = heap.size / 1024.0 / 1024.0;
			printf("Heap %d: size = %.2f MiB, flags = %d\n", i, mib_size, heap.flags);
		}

		// And now iterate over all memory types
		for (uint32_t i = 0; i < mem_properties.memoryTypeCount; i++) {
			VkMemoryType type = mem_properties.memoryTypes[i];
			printf("Type %d: heap index = %d, flags = %d\n", i, type.heapIndex, type.propertyFlags);
		}
	}

	// TODO: flags
	VkBufferCreateInfo buffer_ci = {
		.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
		.size = 2 * 1024,
		.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
	};

	VK_CHECK(vkCreateBuffer(e->device, &buffer_ci, NULL, &e->buffer));

	VkMemoryRequirements mem_req;
	vkGetBufferMemoryRequirements(e->device, e->buffer, &mem_req);

	VkPhysicalDeviceMemoryProperties mem_prop;
	vkGetPhysicalDeviceMemoryProperties(e->phys_device, &mem_prop);

	uint32_t mem_type_index = UINT32_MAX;
	uint64_t best_memory_size = 0;
	for (uint32_t i = 0; i < mem_prop.memoryTypeCount; i++) {
		VkMemoryType mem_type = mem_prop.memoryTypes[i];
		uint64_t heap_size = mem_prop.memoryHeaps[mem_type.heapIndex].size;
		if ((mem_req.memoryTypeBits & (1 << i)) &&
			(mem_type.propertyFlags & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
			(mem_type.propertyFlags & VK_MEMORY_PROPERTY_HOST_COHERENT_BIT) &&
			(heap_size > best_memory_size)) {
			mem_type_index = i;
			best_memory_size = heap_size;
			break;
		}
	}

	if (mem_type_index == UINT32_MAX) {
		fprintf(stderr, "Unable to find memory type\n");
        exit(1);
	}

	{
		uint32_t heap_index = mem_prop.memoryTypes[mem_type_index].heapIndex;
		double mib_size = mem_prop.memoryHeaps[heap_index].size / 1024.0 / 1024.0;

		printf("Memory found. Heap: %d, Type: %d, Size: %.2f\n", heap_index, mem_type_index, mib_size);
	}

	VkMemoryAllocateInfo mem_alloc_info = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.allocationSize = mem_req.size,
		.memoryTypeIndex = mem_type_index,
	};

	VK_CHECK(vkAllocateMemory(e->device, &mem_alloc_info, NULL, &e->memory));

	VK_CHECK(vkBindBufferMemory(e->device, e->buffer, e->memory, 0));

	VK_CHECK(vkMapMemory(e->device, e->memory, 0, buffer_ci.size, 0, &e->mapped_data));
}

static
void memory_deinit(Engine *e) {
	vkUnmapMemory(e->device, e->memory);

	vkFreeMemory(e->device, e->memory, NULL);
	vkDestroyBuffer(e->device, e->buffer, NULL);
}

static
void swapchain_init(Engine *e) {
    VkSurfaceFormatKHR surface_format;
	{
		uint32_t format_count;
		VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(e->phys_device, e->surface, &format_count, NULL));
		VkSurfaceFormatKHR *surface_formats = malloc(format_count * sizeof(VkSurfaceFormatKHR));
		VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(e->phys_device, e->surface, &format_count, surface_formats));

		uint32_t i = 0;
		for (; i < format_count; i++) {
			if (surface_formats[i].format == VK_FORMAT_B8G8R8A8_UNORM && surface_formats[i].colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
				surface_format = surface_formats[i];
				break;
			}
		}

		if (i == format_count) {
			fprintf(stderr, "i == format_count\n");
			exit(1);
		}

		free(surface_formats);
	}
	
	// TODO: fifo is always avaible we may be want to query another one
    VkPresentModeKHR present_mode = VK_PRESENT_MODE_FIFO_KHR;
	// TODO: you may want to find or others, VK_PRESENT_MODE_MAILBOX_KHR
	{
		// VkPresentModeKHR desired = VK_PRESENT_MODE_MAILBOX_KHR;
		// VkPresentModeKHR desired = VK_PRESENT_MODE_IMMEDIATE_KHR;
		VkPresentModeKHR desired = VK_PRESENT_MODE_FIFO_KHR;

		uint32_t present_mode_count = 0;

		VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(e->phys_device, e->surface, &present_mode_count, NULL));
		VkPresentModeKHR *present_modes = malloc(present_mode_count * sizeof(VkPresentModeKHR) );
		VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(e->phys_device,  e->surface, &present_mode_count, present_modes));

		for (uint32_t i = 0; i < present_mode_count; i++) {
			if (present_modes[i] == desired) {
				present_mode = desired;
				break;
			}
		}

		free(present_modes);
	}


	// TODO: VkSurfaceCapabilitiesKHR have a lot of cool info
	VkSurfaceCapabilitiesKHR surface_capabilities;
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(e->phys_device, e->surface, &surface_capabilities));
    VkExtent2D swapchain_extent = surface_capabilities.currentExtent;
    if (swapchain_extent.width == 0xFFFFFFFF && swapchain_extent.height == 0xFFFFFFFF) {
        swapchain_extent.width = WIDTH;
        swapchain_extent.height = HEIGHT;
    }

	// NOTE: I know that it actually checks that maxImageCount == 1, but this code more from when 2 set higher value
	// surface_capabilities.maxImageCount != 0, checked because zero stand for unlimited
	if (surface_capabilities.maxImageCount < 2 && surface_capabilities.maxImageCount != 0) {
        fprintf(stderr, "surface_capabilities.maxImageCount < 2 && surface_capabilities.maxImageCount != 0\n");
		exit(1);
    }
    uint32_t image_count = 2;

	// TODO: flags
	// TODO: imageExtent have actually can have strange behaviour, need to research
    VkSwapchainCreateInfoKHR swapchain_ci = {
        .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
        .surface = e->surface,
        .minImageCount = image_count,
        .imageFormat = surface_format.format,
        .imageColorSpace = surface_format.colorSpace,
        .imageExtent = swapchain_extent,
        .imageArrayLayers = 1, // TODO: For non-stereoscopic-3D applications, this value is 1, WTF?
        .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
        .preTransform = surface_capabilities.currentTransform,
        .compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
        .presentMode = present_mode,
        .clipped = VK_TRUE,
        .oldSwapchain = VK_NULL_HANDLE
    };

    VK_CHECK(vkCreateSwapchainKHR(e->device, &swapchain_ci, NULL, &e->swapchain));

	e->swapchain_image_format = surface_format.format;

	{
		// TODO: well we have image_count, but vulkan driver may allocate more, does this even happen?
		VK_CHECK(vkGetSwapchainImagesKHR(e->device, e->swapchain, &e->swapchain_image_count, NULL));
		e->swapchain_images = malloc(e->swapchain_image_count * sizeof(VkImage));
		VK_CHECK(vkGetSwapchainImagesKHR(e->device, e->swapchain, &e->swapchain_image_count, e->swapchain_images));

		e->swapchain_image_views = malloc(e->swapchain_image_count * sizeof(VkImageView));
		

		// TODO: pNext may be useful with flags
		VkImageViewCreateInfo image_view_ci = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			// .image = e->swapchain_images[i],
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = surface_format.format,
			// .components // becomes identity being zero
			.subresourceRange = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1,
			}
		};
		
		for (uint32_t i = 0; i < e->swapchain_image_count; i++) {
			image_view_ci.image = e->swapchain_images[i];
			VK_CHECK(vkCreateImageView(e->device, &image_view_ci, NULL, &e->swapchain_image_views[i]));
		}
	}
}

static
void swapchain_deinit(Engine *e) {
	for (int i = e->swapchain_image_count; i >= 0; i--) {
		vkDestroyImageView(e->device, e->swapchain_image_views[i], NULL);
	}

	free(e->swapchain_image_views);
	free(e->swapchain_images);

	vkDestroySwapchainKHR(e->device, e->swapchain, NULL);
}

static
void renderpass_init(Engine *e) {
	VkAttachmentDescription color_attach_desc = {
		.format = e->swapchain_image_format,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
		.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
	};

	// TODO: ok, why can not we use VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL here
	// I now color layout make more sense, but spec does no prohibit
	VkAttachmentReference color_attachment_ref = {
		.attachment = 0,
		.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	};

	// TODO: flags, inputs etc, what to do here
	VkSubpassDescription subpass_desc = {
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.colorAttachmentCount = 1,
		.pColorAttachments = &color_attachment_ref,
	};
	
	// TODO: mask? seems to be acess masks for syncronization in gpu
	VkSubpassDependency subpass_dep = {
		.srcSubpass = VK_SUBPASS_EXTERNAL,
		.dstSubpass = 0,
		.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.srcAccessMask = 0,
		.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
		.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,
	};

	// TODO: pNext can do more
	VkRenderPassCreateInfo render_pass_ci = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &color_attach_desc,
		.subpassCount = 1,
		.pSubpasses = &subpass_desc,
		.dependencyCount = 1,
		.pDependencies = &subpass_dep,
	};
	
	VK_CHECK(vkCreateRenderPass(e->device, &render_pass_ci, NULL, &e->render_pass));
}

static
void renderpass_deinit(Engine *e) {
	vkDestroyRenderPass(e->device, e->render_pass, NULL);
}

static
void framebuffers_init(Engine *e) {
	// TODO: pNext with flags may have more stuff with VkFramebufferAttachmentsCreateInfo
	VkFramebufferCreateInfo framebuffer_ci = {
		.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
		.renderPass = e->render_pass,
		.attachmentCount = 1,
		// .pAttachments = &e->swapchain_image_views[i],
		.width = e->window.width,
		.height = e->window.height,
		.layers = 1,
	};

	e->framebuffers = malloc(e->swapchain_image_count * sizeof(VkFramebuffer));
	for (int i = 0; i < e->swapchain_image_count; i++) {
		framebuffer_ci.pAttachments = &e->swapchain_image_views[i];

		VK_CHECK(vkCreateFramebuffer(e->device, &framebuffer_ci, NULL, &e->framebuffers[i]));
	}
}

static
void framebuffers_deinit(Engine *e) {
	for (int i = e->swapchain_image_count; i >= 0; i--) {
		vkDestroyFramebuffer(e->device, e->framebuffers[i], NULL);
	}

	free(e->framebuffers);
}

static
void command_pool_init(Engine *e) {
	VkCommandPoolCreateInfo command_pool_ci = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
		.queueFamilyIndex = e->graphics_queue_family,
	};

	VK_CHECK(vkCreateCommandPool(e->device, &command_pool_ci, NULL, &e->command_pool));

	VkCommandBufferAllocateInfo command_buf_alloc_ci = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
		.commandPool = e->command_pool,
		.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
		.commandBufferCount = 1,
	};

	VK_CHECK(vkAllocateCommandBuffers(e->device, &command_buf_alloc_ci, &e->command_buffer));
}

static
void command_pool_deinit(Engine *e) {
	vkFreeCommandBuffers(e->device, e->command_pool, 1, &e->command_buffer);

	vkDestroyCommandPool(e->device, e->command_pool, NULL);
}

static
void sync_structures_init(Engine *e) {
	VkFenceCreateInfo fence_ci = {
		.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
		.flags = VK_FENCE_CREATE_SIGNALED_BIT,
	};

	VK_CHECK(vkCreateFence(e->device, &fence_ci, NULL, &e->render_fence));

	VkSemaphoreCreateInfo semaphore_ci = {
		.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO,
	};

	VK_CHECK(vkCreateSemaphore(e->device, &semaphore_ci, NULL, &e->present_sema));
	VK_CHECK(vkCreateSemaphore(e->device, &semaphore_ci, NULL, &e->render_sema));
}

static
void sync_structures_deinit(Engine *e) {
	vkDestroySemaphore(e->device, e->render_sema, NULL);
	vkDestroySemaphore(e->device, e->present_sema, NULL);

	vkDestroyFence(e->device, e->render_fence, NULL);
}

static
void load_shader_module(Engine *e, const char* filepath, VkShaderModule *out_shader_module) {
	FILE *file = fopen(filepath, "rb");
    if (!file) {
        fprintf(stderr, "Failed to open file: %s\n", filepath);
        exit(1);
    }

	// TODO: handle errors for file access

    fseek(file, 0, SEEK_END);
    long filesize = ftell(file);
    fseek(file, 0, SEEK_SET);

    // Vulkan requires the shader size to be a multiple of 4, the SPIR-V binary is naturally aligned to 4 bytes
    if (filesize % 4 != 0) {
        fprintf(stderr, "Invalid SPIR-V code\n");
        exit(1);
    }

    // Allocate memory and read the file
    uint32_t *buffer = malloc(filesize);
    if (buffer == NULL) {
        fprintf(stderr, "Failed to allocate memory\n");
        exit(1);
    }

	// TODO: fread is strange, especially second paramm
    fread(buffer, 1, filesize, file);
    fclose(file);

	VkShaderModuleCreateInfo shader_module_ci = {
		.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
		.codeSize = filesize,
		.pCode = buffer,
	};

	VK_CHECK(vkCreateShaderModule(e->device, &shader_module_ci, NULL, out_shader_module));

	free(buffer);
}

static
void pipelines_init(Engine *e) {
	VkShaderModule triangle_frag_shader;
	load_shader_module(e, "triangle.frag.spv", &triangle_frag_shader);

	VkShaderModule triangle_vert_shader;
	load_shader_module(e, "triangle.vert.spv", &triangle_vert_shader);

	// TODO: flags
	// no layouts or push constants
	VkPipelineLayoutCreateInfo pipeline_layout_ci = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
		.setLayoutCount = 0,
		.pSetLayouts = NULL,
		.pushConstantRangeCount = 0,
		.pPushConstantRanges = NULL,
	};

	VK_CHECK(vkCreatePipelineLayout(e->device, &pipeline_layout_ci, NULL, &e->triangle_pipeline_layout));

	// TODO flags are interested here also
	VkPipelineShaderStageCreateInfo vertex_shader_stage_ci = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.stage = VK_SHADER_STAGE_VERTEX_BIT,
		.module = triangle_vert_shader,
		.pName = "main",
	};

	VkPipelineShaderStageCreateInfo fragment_shader_stage_ci = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
		.stage = VK_SHADER_STAGE_FRAGMENT_BIT,
		.module = triangle_frag_shader,
		.pName = "main",
	};

	VkPipelineShaderStageCreateInfo shader_stages[2] = {vertex_shader_stage_ci, fragment_shader_stage_ci};

	VkViewport viewport = {
		.x = 0.0f,
		.y = 0.0f,
		.width = e->window.width,
		.height = e->window.height,
		.minDepth = 0.0f,
		.maxDepth = 1.0f,
	};

	VkRect2D scissor_rect2d = {
		.offset = {.x = 0, .y = 0},
		.extent = e->window,
	};

	// TODO: pNext may do more intersting, especially on NV
	VkPipelineViewportStateCreateInfo viewport_state_ci = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
		.viewportCount = 1,
		.pViewports = &viewport,
		.scissorCount = 1,
		.pScissors = &scissor_rect2d,
	};

	// Well, we do not want to do any complex blending
	VkPipelineColorBlendAttachmentState color_blend_attach_state = {
		.blendEnable = VK_FALSE,
		.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
	};

	VkPipelineColorBlendStateCreateInfo color_blend_ci = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
		.logicOpEnable = VK_FALSE,
		.attachmentCount = 1,
		.pAttachments = &color_blend_attach_state,
	};

	VkVertexInputBindingDescription binding_desc = {
		.binding = 0,
		.stride = sizeof(float) * 2,
		.inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
	};

	VkVertexInputAttributeDescription attr_desc = {
		.location = 0,
		.binding = 0,
		.format = VK_FORMAT_R32G32_SFLOAT,
		.offset = 0,
	};

	// TODO: input vs attribute?
	VkPipelineVertexInputStateCreateInfo vertex_input_ci = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
		.vertexBindingDescriptionCount = 1,
		.pVertexBindingDescriptions = &binding_desc,
		.vertexAttributeDescriptionCount = 1,
		.pVertexAttributeDescriptions = &attr_desc,
	};

	VkPipelineInputAssemblyStateCreateInfo input_assembly_ci = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
		.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
		.primitiveRestartEnable = VK_FALSE,
	};

	// TODO: intresting there is other polygon modes, such as point and lines, how the actually work?
	VkPipelineRasterizationStateCreateInfo raster_ci = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
		.depthClampEnable = VK_FALSE,
		.rasterizerDiscardEnable = VK_FALSE,
		.polygonMode = VK_POLYGON_MODE_FILL,
		.cullMode = VK_CULL_MODE_NONE,
		.frontFace = VK_FRONT_FACE_CLOCKWISE,
		.depthBiasEnable = VK_FALSE,
		.depthBiasConstantFactor = 0.0f,
		.depthBiasClamp = 0.0f,
		.depthBiasSlopeFactor = 0.0f,
		.lineWidth = 1.0f,
	};

	VkPipelineMultisampleStateCreateInfo multisample_ci = {
		.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
		.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
		.sampleShadingEnable = VK_FALSE,
		.minSampleShading = 1.0f,
		.pSampleMask = NULL,
		.alphaToCoverageEnable = VK_FALSE,
		.alphaToOneEnable = VK_FALSE,
	};

	VkGraphicsPipelineCreateInfo pipelineInfo = {
		.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
		.stageCount = 2,
		.pStages = shader_stages,
		.pVertexInputState = &vertex_input_ci,
		.pInputAssemblyState = &input_assembly_ci,
		.pViewportState = &viewport_state_ci,
		.pRasterizationState = &raster_ci,
		.pMultisampleState = &multisample_ci,
		.pColorBlendState = &color_blend_ci,
		.layout = e->triangle_pipeline_layout,
		.renderPass = e->render_pass,
		.subpass = 0,
		.basePipelineHandle = VK_NULL_HANDLE,
	};
	
	VK_CHECK(vkCreateGraphicsPipelines(e->device, VK_NULL_HANDLE, 1, &pipelineInfo, NULL, &e->triangle_pipeline));

	vkDestroyShaderModule(e->device, triangle_frag_shader, NULL);
	vkDestroyShaderModule(e->device, triangle_vert_shader, NULL);
}

static
void pipelines_deinit(Engine *e) {
	vkDestroyPipeline(e->device, e->triangle_pipeline, NULL);

	vkDestroyPipelineLayout(e->device, e->triangle_pipeline_layout, NULL);
}


void engine_init(Engine *e, Display *display, Window window) {
	e->window.width = WIDTH;
	e->window.height = HEIGHT;

    vulkan_init(e, display, window);

	printf("VULKAN INITTED\n");
	fflush(stdout);

	memory_init(e);

	printf("MEMORY INITTED\n"); fflush(stdout);

	swapchain_init(e);

	printf("SWAPCHAIN INITTED\n"); fflush(stdout);

	renderpass_init(e);

	printf("RENDERPASS INITTED\n"); fflush(stdout);

	framebuffers_init(e);

	printf("FRAMEBUFFER INITTED\n"); fflush(stdout);

	command_pool_init(e);

	printf("COMMANDS INITTED\n"); fflush(stdout);

	sync_structures_init(e);

	printf("SYNC INITTED\n"); fflush(stdout);

	pipelines_init(e);

	printf("PIPELINES INITTED\n"); fflush(stdout);
}

void engine_deinit(Engine *e) {
	pipelines_deinit(e); printf("pipelines_deinit(e)\n"); fflush(stdout);

	sync_structures_deinit(e); printf("sync_structures_deinit(e)\n"); fflush(stdout);

	command_pool_deinit(e); printf("command_pool_deinit(e)\n"); fflush(stdout);

	framebuffers_deinit(e); printf("framebuffers_deinit(e)\n"); fflush(stdout);

	renderpass_deinit(e); printf("renderpass_deinit(e)\n"); fflush(stdout);

	swapchain_deinit(e); printf("swapchain_deinit(e)\n"); fflush(stdout);

	memory_deinit(e); ; printf("memory_deinit(e)\n"); fflush(stdout);

	vulkan_deinit(e); printf("vulkan_deinit(e)\n"); fflush(stdout);
}

void engine_draw(Engine *e, float cycle) {
	VK_CHECK(vkWaitForFences(e->device, 1, &e->render_fence, VK_TRUE, UINT64_MAX));
	VK_CHECK(vkResetFences(e->device, 1, &e->render_fence));

	vkResetCommandBuffer(e->command_buffer, 0);

	uint32_t swapchain_image_index;
	VK_CHECK(vkAcquireNextImageKHR(e->device, e->swapchain, UINT64_MAX, e->present_sema, NULL, &swapchain_image_index));

	VkCommandBufferBeginInfo command_buf_begin_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
		.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		.pInheritanceInfo = NULL,
	};

	VK_CHECK(vkBeginCommandBuffer(e->command_buffer, &command_buf_begin_info));

	VkClearValue clear_value = {
		.color.float32 = { 0.0f, 0.0f, 0.0f, 1.0f },
	};

	VkRenderPassBeginInfo render_pass_begin_info = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
		.renderPass = e->render_pass,
		.framebuffer = e->framebuffers[swapchain_image_index],
		.renderArea = {
			.offset = {
				.x = 0,
				.y = 0,
			},
			.extent = e->window,
		},
		.clearValueCount = 1,
		.pClearValues = &clear_value,
	};

	float mod_cycle = -cycle - 0.5f;
	float alpha = (mod_cycle) * 2 * PI;
	float beta = (mod_cycle + 1.0 / 3.0) * 2 * PI;
	float gamma = (mod_cycle + 2.0 / 3.0) * 2 * PI;
	float vertices[] = {
		sinf(alpha) / 2.0, cosf(alpha) / 2.0,
		sinf(beta) / 2.0, cosf(beta) / 2.0,
		sinf(gamma) / 2.0, cosf(gamma) / 2.0,
	};

	memcpy(e->mapped_data, vertices, sizeof(vertices));

	vkCmdBeginRenderPass(e->command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

	VkDeviceSize offsets[] = {0};
	vkCmdBindVertexBuffers(e->command_buffer, 0, 1, &e->buffer, offsets);

	vkCmdBindPipeline(e->command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, e->triangle_pipeline);
	vkCmdDraw(e->command_buffer, 3, 1, 0, 0);

	vkCmdEndRenderPass(e->command_buffer);
	

	VK_CHECK(vkEndCommandBuffer(e->command_buffer));

	VkPipelineStageFlags wait_stage_flags[] = {
		VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
	};

	VkSubmitInfo submit_info = {
		.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
		
		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &e->present_sema,
		.pWaitDstStageMask = wait_stage_flags,

		.commandBufferCount = 1,
		.pCommandBuffers = &e->command_buffer,

		.signalSemaphoreCount = 1,
		.pSignalSemaphores = &e->render_sema,
	};

	VK_CHECK(vkQueueSubmit(e->graphics_queue, 1, &submit_info, e->render_fence));

	VkPresentInfoKHR present_info = {
		.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,

		.waitSemaphoreCount = 1,
		.pWaitSemaphores = &e->render_sema,

		.swapchainCount = 1,
		.pSwapchains = &e->swapchain,
		
		.pImageIndices = &swapchain_image_index,
	};

	VK_CHECK(vkQueuePresentKHR(e->graphics_queue, &present_info));
}