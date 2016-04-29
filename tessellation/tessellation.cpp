/*
* Vulkan Example - Tessellation shader PN triangles
*
* Based on http://alex.vlachos.com/graphics/CurvedPNTriangles.pdf
* Shaders based on http://onrendering.blogspot.de/2011/12/tessellation-on-gpu-curved-pn-triangles.html
*
* Copyright (C) 2016 by Sascha Willems - www.saschawillems.de
*
* This code is licensed under the MIT license (MIT) (http://opensource.org/licenses/MIT)
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <vector>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <vulkan/vulkan.h>
#include "vulkanexamplebase.h"

#define VERTEX_BUFFER_BIND_ID 0
#define ENABLE_VALIDATION false

// Vertex layout for this example
std::vector<vkMeshLoader::VertexLayout> vertexLayout =
{
	vkMeshLoader::VERTEX_LAYOUT_POSITION,
	vkMeshLoader::VERTEX_LAYOUT_NORMAL,
	vkMeshLoader::VERTEX_LAYOUT_UV
};

class VulkanExample : public VulkanExampleBase
{
public:
	bool splitScreen = true;

	struct {
		vkTools::VulkanTexture colorMap;
	} textures;

	struct {
		VkPipelineVertexInputStateCreateInfo inputState;
		std::vector<VkVertexInputBindingDescription> bindingDescriptions;
		std::vector<VkVertexInputAttributeDescription> attributeDescriptions;
	} vertices;

	struct {
		vkMeshLoader::MeshBuffer object;
	} meshes;
	
	vkTools::UniformData uniformDataTC, uniformDataTE;

	struct {
		float tessLevel = 4.0f;
	} uboTC;

	struct {
		glm::mat4 projection;
		glm::mat4 model;
		float tessAlpha = 0.9f;
	} uboTE;

	struct {
		VkPipeline solid;
		VkPipeline wire;
		VkPipeline solidPassThrough;
		VkPipeline wirePassThrough;
	} pipelines;
	VkPipeline *pipelineLeft = &pipelines.solidPassThrough;
	VkPipeline *pipelineRight = &pipelines.solid;
	
	VkPipelineLayout pipelineLayout;
	VkDescriptorSet descriptorSet;
	VkDescriptorSetLayout descriptorSetLayout;

	VulkanExample() : VulkanExampleBase(ENABLE_VALIDATION)
	{
		zoom = -1.5f;
		rotation = glm::vec3(-380.0f, 18.5f, 0.0f);
		title = "Vulkan Example - Tessellation shader (PN Triangles)";
		// Support for tessellation shaders is optional, so check first
		if (!deviceFeatures.tessellationShader)
		{
			vkTools::exitFatal("Selected GPU does not support tessellation shaders!", "Feature not supported");
		}
	}

	~VulkanExample()
	{
		// Clean up used Vulkan resources 
		// Note : Inherited destructor cleans up resources stored in base class
		vkDestroyPipeline(device, pipelines.solid, nullptr);
		vkDestroyPipeline(device, pipelines.wire, nullptr);
		vkDestroyPipeline(device, pipelines.solidPassThrough, nullptr);
		vkDestroyPipeline(device, pipelines.wirePassThrough, nullptr);

		vkDestroyPipelineLayout(device, pipelineLayout, nullptr);
		vkDestroyDescriptorSetLayout(device, descriptorSetLayout, nullptr);

		vkMeshLoader::freeMeshBufferResources(device, &meshes.object);

		vkDestroyBuffer(device, uniformDataTC.buffer, nullptr);
		vkFreeMemory(device, uniformDataTC.memory, nullptr);

		vkDestroyBuffer(device, uniformDataTE.buffer, nullptr);
		vkFreeMemory(device, uniformDataTE.memory, nullptr);

		textureLoader->destroyTexture(textures.colorMap);
	}

	void reBuildCommandBuffers()
	{
		if (!checkCommandBuffers())
		{
			destroyCommandBuffers();
			createCommandBuffers();
		}
		buildCommandBuffers();
	}

	void buildCommandBuffers()
	{
		VkCommandBufferBeginInfo cmdBufInfo = vkTools::initializers::commandBufferBeginInfo();

		VkClearValue clearValues[2];
		clearValues[0].color = defaultClearColor;
		clearValues[1].depthStencil = { 1.0f, 0 };

		VkRenderPassBeginInfo renderPassBeginInfo = vkTools::initializers::renderPassBeginInfo();
		renderPassBeginInfo.renderPass = renderPass;
		renderPassBeginInfo.renderArea.offset.x = 0;
		renderPassBeginInfo.renderArea.offset.y = 0;
		renderPassBeginInfo.renderArea.extent.width = width;
		renderPassBeginInfo.renderArea.extent.height = height;
		renderPassBeginInfo.clearValueCount = 2;
		renderPassBeginInfo.pClearValues = clearValues;

		VkResult err;

		for (int32_t i = 0; i < drawCmdBuffers.size(); ++i)
		{
			// Set target frame buffer
			renderPassBeginInfo.framebuffer = frameBuffers[i];

			err = vkBeginCommandBuffer(drawCmdBuffers[i], &cmdBufInfo);
			assert(!err);

			vkCmdBeginRenderPass(drawCmdBuffers[i], &renderPassBeginInfo, VK_SUBPASS_CONTENTS_INLINE);

			VkViewport viewport = vkTools::initializers::viewport(
				splitScreen ? (float)width / 2.0f : (float)width,
				(float)height,
				0.0f,
				1.0f
				);
			vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);

			VkRect2D scissor = vkTools::initializers::rect2D(
				width,
				height,
				0,
				0
				);
			vkCmdSetScissor(drawCmdBuffers[i], 0, 1, &scissor);

			vkCmdSetLineWidth(drawCmdBuffers[i], 1.0f);

			vkCmdBindDescriptorSets(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1, &descriptorSet, 0, NULL);

			VkDeviceSize offsets[1] = { 0 };
			vkCmdBindVertexBuffers(drawCmdBuffers[i], VERTEX_BUFFER_BIND_ID, 1, &meshes.object.vertices.buf, offsets);
			vkCmdBindIndexBuffer(drawCmdBuffers[i], meshes.object.indices.buf, 0, VK_INDEX_TYPE_UINT32);

			if (splitScreen)
			{
				vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);
				vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, *pipelineLeft);
				vkCmdDrawIndexed(drawCmdBuffers[i], meshes.object.indexCount, 1, 0, 0, 0);
				viewport.x = float(width) / 2;
			}

			vkCmdSetViewport(drawCmdBuffers[i], 0, 1, &viewport);
			vkCmdBindPipeline(drawCmdBuffers[i], VK_PIPELINE_BIND_POINT_GRAPHICS, *pipelineRight);
			vkCmdDrawIndexed(drawCmdBuffers[i], meshes.object.indexCount, 1, 0, 0, 0);

			vkCmdEndRenderPass(drawCmdBuffers[i]);

			err = vkEndCommandBuffer(drawCmdBuffers[i]);
			assert(!err);
		}
	}

	void draw()
	{
		VkResult err;

		// Get next image in the swap chain (back/front buffer)
		err = swapChain.acquireNextImage(semaphores.presentComplete, &currentBuffer);
		assert(!err);

		submitPostPresentBarrier(swapChain.buffers[currentBuffer].image);

		// Command buffer to be sumitted to the queue
		submitInfo.commandBufferCount = 1;
		submitInfo.pCommandBuffers = &drawCmdBuffers[currentBuffer];

		// Submit to queue
		err = vkQueueSubmit(queue, 1, &submitInfo, VK_NULL_HANDLE);
		assert(!err);

		submitPrePresentBarrier(swapChain.buffers[currentBuffer].image);

		err = swapChain.queuePresent(queue, currentBuffer, semaphores.renderComplete);
		assert(!err);

		err = vkQueueWaitIdle(queue);
		assert(!err);
	}

	void loadMeshes()
	{
		loadMesh(getAssetPath() + "models/lowpoly/bearmug.dae", &meshes.object, vertexLayout, 4.0f);
	}

	void loadTextures()
	{
		textureLoader->loadTexture(
			getAssetPath() + "textures/bearmug.ktx",
			VK_FORMAT_BC3_UNORM_BLOCK,
			&textures.colorMap);
	}

	void setupVertexDescriptions()
	{
		// Binding description
		vertices.bindingDescriptions.resize(1);
		vertices.bindingDescriptions[0] =
			vkTools::initializers::vertexInputBindingDescription(
				VERTEX_BUFFER_BIND_ID,
				vkMeshLoader::vertexSize(vertexLayout),
				VK_VERTEX_INPUT_RATE_VERTEX);

		// Attribute descriptions
		// Describes memory layout and shader positions
		vertices.attributeDescriptions.resize(3);

		// Location 0 : Position
		vertices.attributeDescriptions[0] =
			vkTools::initializers::vertexInputAttributeDescription(
				VERTEX_BUFFER_BIND_ID,
				0,
				VK_FORMAT_R32G32B32_SFLOAT,
				0);

		// Location 1 : Normals
		vertices.attributeDescriptions[1] =
			vkTools::initializers::vertexInputAttributeDescription(
				VERTEX_BUFFER_BIND_ID,
				1,
				VK_FORMAT_R32G32B32_SFLOAT,
				sizeof(float) * 3);

		// Location 2 : Texture coordinates
		vertices.attributeDescriptions[2] =
			vkTools::initializers::vertexInputAttributeDescription(
				VERTEX_BUFFER_BIND_ID,
				2,
				VK_FORMAT_R32G32_SFLOAT,
				sizeof(float) * 6);

		vertices.inputState = vkTools::initializers::pipelineVertexInputStateCreateInfo();
		vertices.inputState.vertexBindingDescriptionCount = (uint32_t)vertices.bindingDescriptions.size();
		vertices.inputState.pVertexBindingDescriptions = vertices.bindingDescriptions.data();
		vertices.inputState.vertexAttributeDescriptionCount = (uint32_t)vertices.attributeDescriptions.size();
		vertices.inputState.pVertexAttributeDescriptions = vertices.attributeDescriptions.data();
	}

	void setupDescriptorPool()
	{
		// Example uses two ubos and one combined image sampler
		std::vector<VkDescriptorPoolSize> poolSizes =
		{
			vkTools::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 2),
			vkTools::initializers::descriptorPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1),
		};

		VkDescriptorPoolCreateInfo descriptorPoolInfo =
			vkTools::initializers::descriptorPoolCreateInfo(
				(uint32_t)poolSizes.size(),
				poolSizes.data(),
				1);

		VkResult vkRes = vkCreateDescriptorPool(device, &descriptorPoolInfo, nullptr, &descriptorPool);
		assert(!vkRes);
	}

	void setupDescriptorSetLayout()
	{
		std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings =
		{
			// Binding 0 : Tessellation control shader ubo
			vkTools::initializers::descriptorSetLayoutBinding(
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT,
				0),
			// Binding 1 : Tessellation evaluation shader ubo
			vkTools::initializers::descriptorSetLayoutBinding(
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT,
				1),
			// Binding 2 : Fragment shader combined sampler
			vkTools::initializers::descriptorSetLayoutBinding(
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				VK_SHADER_STAGE_FRAGMENT_BIT,
				2),
		};

		VkDescriptorSetLayoutCreateInfo descriptorLayout =
			vkTools::initializers::descriptorSetLayoutCreateInfo(
				setLayoutBindings.data(),
				(uint32_t)setLayoutBindings.size());

		VkResult err = vkCreateDescriptorSetLayout(device, &descriptorLayout, nullptr, &descriptorSetLayout);
		assert(!err);

		VkPipelineLayoutCreateInfo pPipelineLayoutCreateInfo =
			vkTools::initializers::pipelineLayoutCreateInfo(
				&descriptorSetLayout,
				1);

		err = vkCreatePipelineLayout(device, &pPipelineLayoutCreateInfo, nullptr, &pipelineLayout);
		assert(!err);
	}

	void setupDescriptorSet()
	{
		VkDescriptorSetAllocateInfo allocInfo =
			vkTools::initializers::descriptorSetAllocateInfo(
				descriptorPool,
				&descriptorSetLayout,
				1);

		VkResult vkRes = vkAllocateDescriptorSets(device, &allocInfo, &descriptorSet);
		assert(!vkRes);

		VkDescriptorImageInfo texDescriptor =
			vkTools::initializers::descriptorImageInfo(
				textures.colorMap.sampler,
				textures.colorMap.view,
				VK_IMAGE_LAYOUT_GENERAL);

		std::vector<VkWriteDescriptorSet> writeDescriptorSets =
		{
			// Binding 0 : Tessellation control shader ubo
			vkTools::initializers::writeDescriptorSet(
			descriptorSet,
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				0,
				&uniformDataTC.descriptor),
			// Binding 1 : Tessellation evaluation shader ubo
			vkTools::initializers::writeDescriptorSet(
				descriptorSet,
				VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
				1,
				&uniformDataTE.descriptor),
			// Binding 2 : Color map 
			vkTools::initializers::writeDescriptorSet(
				descriptorSet,
				VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
				2,
				&texDescriptor)
		};

		vkUpdateDescriptorSets(device, (uint32_t)writeDescriptorSets.size(), writeDescriptorSets.data(), 0, NULL);
	}

	void preparePipelines()
	{
		VkResult err;

		VkPipelineInputAssemblyStateCreateInfo inputAssemblyState =
			vkTools::initializers::pipelineInputAssemblyStateCreateInfo(
				VK_PRIMITIVE_TOPOLOGY_PATCH_LIST,
				0,
				VK_FALSE);

		VkPipelineRasterizationStateCreateInfo rasterizationState =
			vkTools::initializers::pipelineRasterizationStateCreateInfo(
				VK_POLYGON_MODE_FILL,
				VK_CULL_MODE_BACK_BIT,
				VK_FRONT_FACE_COUNTER_CLOCKWISE,
				0);

		VkPipelineColorBlendAttachmentState blendAttachmentState =
			vkTools::initializers::pipelineColorBlendAttachmentState(
				0xf,
				VK_FALSE);

		VkPipelineColorBlendStateCreateInfo colorBlendState =
			vkTools::initializers::pipelineColorBlendStateCreateInfo(
				1,
				&blendAttachmentState);

		VkPipelineDepthStencilStateCreateInfo depthStencilState =
			vkTools::initializers::pipelineDepthStencilStateCreateInfo(
				VK_TRUE,
				VK_TRUE,
				VK_COMPARE_OP_LESS_OR_EQUAL);

		VkPipelineViewportStateCreateInfo viewportState =
			vkTools::initializers::pipelineViewportStateCreateInfo(1, 1, 0);

		VkPipelineMultisampleStateCreateInfo multisampleState =
			vkTools::initializers::pipelineMultisampleStateCreateInfo(
				VK_SAMPLE_COUNT_1_BIT,
				0);

		std::vector<VkDynamicState> dynamicStateEnables = {
			VK_DYNAMIC_STATE_VIEWPORT,
			VK_DYNAMIC_STATE_SCISSOR,
			VK_DYNAMIC_STATE_LINE_WIDTH
		};
		VkPipelineDynamicStateCreateInfo dynamicState =
			vkTools::initializers::pipelineDynamicStateCreateInfo(
				dynamicStateEnables.data(),
				(uint32_t)dynamicStateEnables.size(),
				0);

		VkPipelineTessellationStateCreateInfo tessellationState =
			vkTools::initializers::pipelineTessellationStateCreateInfo(3);

		// Tessellation pipelines
		// Load shaders
		std::array<VkPipelineShaderStageCreateInfo, 4> shaderStages;

		shaderStages[0] = loadShader(getAssetPath() + "shaders/tessellation/base.vert.spv", VK_SHADER_STAGE_VERTEX_BIT);
		shaderStages[1] = loadShader(getAssetPath() + "shaders/tessellation/base.frag.spv", VK_SHADER_STAGE_FRAGMENT_BIT);
		shaderStages[2] = loadShader(getAssetPath() + "shaders/tessellation/pntriangles.tesc.spv", VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT);
		shaderStages[3] = loadShader(getAssetPath() + "shaders/tessellation/pntriangles.tese.spv", VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT);

		VkGraphicsPipelineCreateInfo pipelineCreateInfo =
			vkTools::initializers::pipelineCreateInfo(
				pipelineLayout,
				renderPass,
				0);

		pipelineCreateInfo.pVertexInputState = &vertices.inputState;
		pipelineCreateInfo.pInputAssemblyState = &inputAssemblyState;
		pipelineCreateInfo.pRasterizationState = &rasterizationState;
		pipelineCreateInfo.pColorBlendState = &colorBlendState;
		pipelineCreateInfo.pMultisampleState = &multisampleState;
		pipelineCreateInfo.pViewportState = &viewportState;
		pipelineCreateInfo.pDepthStencilState = &depthStencilState;
		pipelineCreateInfo.pDynamicState = &dynamicState;
		pipelineCreateInfo.pTessellationState = &tessellationState;
		pipelineCreateInfo.stageCount = (uint32_t)shaderStages.size();
		pipelineCreateInfo.pStages = shaderStages.data();
		pipelineCreateInfo.renderPass = renderPass;

		// Tessellation pipelines
		// Solid
		err = vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.solid);
		assert(!err);
		// Wireframe
		rasterizationState.polygonMode = VK_POLYGON_MODE_LINE;
		err = vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.wire);
		assert(!err);

		// Pass through pipelines
		// Load pass through tessellation shaders (Vert and frag are reused)
		shaderStages[2] = loadShader(getAssetPath() + "shaders/tessellation/passthrough.tesc.spv", VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT);
		shaderStages[3] = loadShader(getAssetPath() + "shaders/tessellation/passthrough.tese.spv", VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT);

		// Solid
		rasterizationState.polygonMode = VK_POLYGON_MODE_FILL;
		err = vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.solidPassThrough);
		assert(!err);
		// Wireframe
		rasterizationState.polygonMode = VK_POLYGON_MODE_LINE;
		err = vkCreateGraphicsPipelines(device, pipelineCache, 1, &pipelineCreateInfo, nullptr, &pipelines.wirePassThrough);
		assert(!err);
	}

	// Prepare and initialize uniform buffer containing shader uniforms
	void prepareUniformBuffers()
	{
		// Tessellation evaluation shader uniform buffer
		createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			sizeof(uboTE),
			&uboTE,
			&uniformDataTE.buffer,
			&uniformDataTE.memory,
			&uniformDataTE.descriptor);

		// Tessellation control shader uniform buffer
		createBuffer(
			VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			sizeof(uboTC),
			&uboTC,
			&uniformDataTC.buffer,
			&uniformDataTC.memory,
			&uniformDataTC.descriptor);

		updateUniformBuffers();
	}

	void updateUniformBuffers()
	{
		// Tessellation eval
		glm::mat4 viewMatrix = glm::mat4();
		uboTE.projection = glm::perspective(glm::radians(45.0f), (float)(width* ((splitScreen) ? 0.5f : 1.0f)) / (float)height, 0.1f, 256.0f);
		viewMatrix = glm::translate(viewMatrix, glm::vec3(0.0f, 0.0f, zoom));

		float offset = 0.5f;
		int uboIndex = 1;
		uboTE.model = glm::mat4();
		uboTE.model = viewMatrix * glm::translate(uboTE.model, glm::vec3(0, 0, 0));
		uboTE.model = glm::rotate(uboTE.model, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
		uboTE.model = glm::rotate(uboTE.model, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
		uboTE.model = glm::rotate(uboTE.model, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));

		uint8_t *pData;
		VkResult err = vkMapMemory(device, uniformDataTE.memory, 0, sizeof(uboTE), 0, (void **)&pData);
		assert(!err);
		memcpy(pData, &uboTE, sizeof(uboTE));
		vkUnmapMemory(device, uniformDataTE.memory);

		// Tessellation control
		err = vkMapMemory(device, uniformDataTC.memory, 0, sizeof(uboTC), 0, (void **)&pData);
		assert(!err);
		memcpy(pData, &uboTC, sizeof(uboTC));
		vkUnmapMemory(device, uniformDataTC.memory);
	}

	void prepare()
	{
		VulkanExampleBase::prepare();
		loadTextures();
		loadMeshes();
		setupVertexDescriptions();
		prepareUniformBuffers();
		setupDescriptorSetLayout();
		preparePipelines();
		setupDescriptorPool();
		setupDescriptorSet();
		buildCommandBuffers(); 
		prepared = true;
	}

	virtual void render()
	{
		if (!prepared)
			return;
		vkDeviceWaitIdle(device);
		draw();
		vkDeviceWaitIdle(device);
	}

	virtual void viewChanged()
	{
		updateUniformBuffers();
	}

	void changeTessellationLevel(float delta)
	{
		uboTC.tessLevel += delta;
		// Clamp
		uboTC.tessLevel = fmax(1.0f, fmin(uboTC.tessLevel, 32.0f));
		updateUniformBuffers();
	}

	void togglePipelines()
	{
		if (pipelineRight == &pipelines.solid)
		{
			pipelineRight = &pipelines.wire;
			pipelineLeft = &pipelines.wirePassThrough;
		}
		else
		{
			pipelineRight = &pipelines.solid;
			pipelineLeft = &pipelines.solidPassThrough;
		}
		reBuildCommandBuffers();
	}

	void toggleSplitScreen()
	{
		splitScreen = !splitScreen;
		updateUniformBuffers();
		reBuildCommandBuffers();
	}

	virtual void keyPressed(uint32_t keyCode)
	{
		switch (keyCode)
		{
		case 0x6B://VK_ADD:
			changeTessellationLevel(0.25);
			break;
		case 0x6D://VK_SUBTRACT:
			changeTessellationLevel(-0.25);
			break;
		case 'W'://0x57:
			togglePipelines();
			break;
		case 'S'://0x53:
			toggleSplitScreen();
			break;
		}
	}
};

DEFINE_VULKAN_APPLICATION_ENTRY_POINT();
