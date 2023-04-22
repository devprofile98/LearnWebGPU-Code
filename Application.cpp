/**
 * This file is part of the "Learn WebGPU for C++" book.
 *   https://github.com/eliemichel/LearnWebGPU
 *
 * MIT License
 * Copyright (c) 2022-2023 Elie Michel
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in all
 * copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "Application.h"
#include "ResourceManager.h"

#include "save_texture.h"

#include "stb_image.h"

#include <glfw3webgpu/glfw3webgpu.h>

#define GLM_FORCE_LEFT_HANDED
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

#include <imgui.h>
#include <backends/imgui_impl_wgpu.h>
#include <backends/imgui_impl_glfw.h>

#include <webgpu/webgpu.hpp>
#include "webgpu-release.h"

#include <iostream>
#include <cassert>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <array>

constexpr float PI = 3.14159265358979323846f;

using namespace wgpu;
using glm::mat4x4;
using glm::vec4;
using glm::vec3;
using glm::vec2;

// == Utils == //

// Equivalent of std::bit_width that is available from C++20 onward
uint32_t bit_width(uint32_t m) {
	if (m == 0) return 0;
	else { uint32_t w = 0; while (m >>= 1) ++w; return w; }
}

uint32_t getMaxMipLevelCount(const Extent3D& textureSize) {
	return bit_width(std::max(textureSize.width, textureSize.height));
}

// == GLFW Callbacks == //

void onWindowResize(GLFWwindow* window, int width, int height) {
	(void)width; (void)height;
	auto pApp = reinterpret_cast<Application*>(glfwGetWindowUserPointer(window));
	if (pApp != nullptr) pApp->onResize();
}

// == Application == //

bool Application::onInit() {
	if (!initWindow()) return false;
	if (!initDevice()) return false;
	initSwapChain();
	initGui();
	initBindGroupLayout();
	initComputePipeline();
	initTexture();
	initTextureViews();
	return true;
}

void Application::onFinish() {
	terminateTextureViews();
	terminateTexture();
	terminateComputePipeline();
	terminateBindGroupLayout();
	terminateGui();
	terminateSwapChain();
	terminateDevice();
	terminateWindow();
}

bool Application::isRunning() {
	return !glfwWindowShouldClose(m_window);
}

bool Application::shouldCompute() {
	return m_shouldCompute;
}

bool Application::initDevice() {
	// Create instance
	m_instance = createInstance(InstanceDescriptor{});
	if (!m_instance) {
		std::cerr << "Could not initialize WebGPU!" << std::endl;
		return false;
	}

	// Create surface and adapter
	std::cout << "Requesting adapter..." << std::endl;
	m_surface = glfwGetWGPUSurface(m_instance, m_window);
	RequestAdapterOptions adapterOpts{};
	adapterOpts.compatibleSurface = nullptr;
	adapterOpts.compatibleSurface = m_surface;
	Adapter adapter = m_instance.requestAdapter(adapterOpts);
	std::cout << "Got adapter: " << adapter << std::endl;

	std::cout << "Requesting device..." << std::endl;
	SupportedLimits supportedLimits;
	adapter.getLimits(&supportedLimits);
	RequiredLimits requiredLimits = Default;
	requiredLimits.limits.maxVertexAttributes = 6;
	requiredLimits.limits.maxVertexBuffers = 1;
	requiredLimits.limits.maxBindGroups = 2;
	requiredLimits.limits.maxUniformBuffersPerShaderStage = 2;
	requiredLimits.limits.maxUniformBufferBindingSize = 16 * 4 * sizeof(float);
	requiredLimits.limits.minStorageBufferOffsetAlignment = supportedLimits.limits.minStorageBufferOffsetAlignment;
	requiredLimits.limits.maxBufferSize = 0;
	requiredLimits.limits.maxTextureDimension1D = 4096;
	requiredLimits.limits.maxTextureDimension2D = 4096;
	requiredLimits.limits.maxTextureDimension3D = 4096;
	requiredLimits.limits.maxTextureArrayLayers = 1;
	requiredLimits.limits.maxSampledTexturesPerShaderStage = 3;
	requiredLimits.limits.maxSamplersPerShaderStage = 1;
	requiredLimits.limits.maxVertexBufferArrayStride = 68;
	requiredLimits.limits.maxInterStageShaderComponents = 17;
	requiredLimits.limits.maxStorageBuffersPerShaderStage = 2;
	requiredLimits.limits.maxComputeWorkgroupSizeX = 32;
	requiredLimits.limits.maxComputeWorkgroupSizeY = 1;
	requiredLimits.limits.maxComputeWorkgroupSizeZ = 1;
	requiredLimits.limits.maxComputeInvocationsPerWorkgroup = 32;
	requiredLimits.limits.maxComputeWorkgroupsPerDimension = 2;
	requiredLimits.limits.maxStorageBufferBindingSize = 0;

	// Create device
	DeviceDescriptor deviceDesc{};
	deviceDesc.label = "My Device";
	deviceDesc.requiredFeaturesCount = 0;
	deviceDesc.requiredLimits = &requiredLimits;
	deviceDesc.defaultQueue.label = "The default queue";
	m_device = adapter.requestDevice(deviceDesc);
	std::cout << "Got device: " << m_device << std::endl;

	// Add an error callback for more debug info
	m_uncapturedErrorCallback = m_device.setUncapturedErrorCallback([](ErrorType type, char const* message) {
		std::cout << "Device error: type " << type;
		if (message) std::cout << " (message: " << message << ")";
		std::cout << std::endl;
	});

	m_deviceLostCallback = m_device.setDeviceLostCallback([](DeviceLostReason reason, char const* message) {
		std::cout << "Device lost: reason " << reason;
		if (message) std::cout << " (message: " << message << ")";
		std::cout << std::endl;
	});

	m_queue = m_device.getQueue();

#ifdef WEBGPU_BACKEND_WGPU
	m_queue.submit(0, nullptr);
#else
	m_instance.processEvents();
#endif

	return true;
}

void Application::terminateDevice() {
#ifndef WEBGPU_BACKEND_WGPU
	wgpuQueueRelease(m_queue);
#endif WEBGPU_BACKEND_WGPU
	wgpuDeviceRelease(m_device);
	wgpuInstanceRelease(m_instance);
}

bool Application::initWindow() {
	if (!glfwInit()) {
		std::cerr << "Could not initialize GLFW!" << std::endl;
		return false;
	}

	// Create window
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
	m_window = glfwCreateWindow(640, 480, "Learn WebGPU", NULL, NULL);
	if (!m_window) {
		std::cerr << "Could not open window!" << std::endl;
		return false;
	}

	// Add window callbacks
	glfwSetWindowUserPointer(m_window, this);
	glfwSetFramebufferSizeCallback(m_window, onWindowResize);
	return true;
}

void Application::terminateWindow() {
	glfwDestroyWindow(m_window);
	glfwTerminate();
}


void Application::initSwapChain() {
#ifdef WEBGPU_BACKEND_DAWN
	m_swapChainFormat = TextureFormat::BGRA8Unorm;
#else
	m_swapChainFormat = m_surface.getPreferredFormat(adapter);
#endif

	int width, height;
	glfwGetFramebufferSize(m_window, &width, &height);

	std::cout << "Creating swapchain..." << std::endl;
	m_swapChainDesc = {};
	m_swapChainDesc.width = (uint32_t)width;
	m_swapChainDesc.height = (uint32_t)height;
	m_swapChainDesc.usage = TextureUsage::RenderAttachment;
	m_swapChainDesc.format = m_swapChainFormat;
	m_swapChainDesc.presentMode = PresentMode::Fifo;
	m_swapChain = m_device.createSwapChain(m_surface, m_swapChainDesc);
	std::cout << "Swapchain: " << m_swapChain << std::endl;
}

void Application::terminateSwapChain() {
	wgpuSwapChainRelease(m_swapChain);
}

void Application::initGui() {
	// Setup Dear ImGui context
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;

	// Setup Platform/Renderer backends
	ImGui_ImplGlfw_InitForOther(m_window, true);
	ImGui_ImplWGPU_Init(m_device, 3, m_swapChainFormat, TextureFormat::Undefined);
}

void Application::terminateGui() {
	ImGui_ImplWGPU_Shutdown();
	ImGui_ImplGlfw_Shutdown();
}

void Application::initTexture() {
	// Load image data
	int width, height, channels;
	uint8_t* pixelData = stbi_load(RESOURCE_DIR "/input.jpg", &width, &height, &channels, 4 /* force 4 channels */);
	if (nullptr == pixelData) throw std::runtime_error("Could not load input texture!");
	Extent3D textureSize = { (uint32_t)width, (uint32_t)height, 1 };

	// Create texture
	TextureDescriptor textureDesc;
	textureDesc.dimension = TextureDimension::_2D;
	textureDesc.format = TextureFormat::RGBA8Unorm;
	textureDesc.size = textureSize;
	textureDesc.sampleCount = 1;
	textureDesc.viewFormatCount = 0;
	textureDesc.viewFormats = nullptr;

	textureDesc.usage = (
		TextureUsage::TextureBinding | // to read the texture in a shader
		TextureUsage::StorageBinding | // to write the texture in a shader
		TextureUsage::CopyDst | // to upload the input data
		TextureUsage::CopySrc   // to save the output data
	);

	textureDesc.mipLevelCount = getMaxMipLevelCount(textureSize);
	m_textureMipSizes.resize(textureDesc.mipLevelCount);
	m_textureMipSizes[0] = textureSize;

	m_texture = m_device.createTexture(textureDesc);

	// Upload texture data for MIP level 0 to the GPU
	ImageCopyTexture destination;
	destination.texture = m_texture;
	destination.origin = { 0, 0, 0 };
	destination.aspect = TextureAspect::All;
	destination.mipLevel = 0;
	TextureDataLayout source;
	source.offset = 0;
	source.bytesPerRow = 4 * textureSize.width;
	source.rowsPerImage = textureSize.height;
	m_queue.writeTexture(destination, pixelData, (size_t)(4 * width * height), source, textureSize);

	// Free CPU-side data
	stbi_image_free(pixelData);
}

void Application::terminateTexture() {
	m_texture.destroy();
	wgpuTextureRelease(m_texture);
}

void Application::initTextureViews() {
	TextureViewDescriptor textureViewDesc;
	textureViewDesc.aspect = TextureAspect::All;
	textureViewDesc.baseArrayLayer = 0;
	textureViewDesc.arrayLayerCount = 1;
	textureViewDesc.dimension = TextureViewDimension::_2D;
	textureViewDesc.format = TextureFormat::RGBA8Unorm;

	// Each view must correspond to only 1 MIP level at a time
	textureViewDesc.mipLevelCount = 1;

	m_textureMipViews.reserve(m_textureMipSizes.size());
	for (uint32_t level = 0; level < m_textureMipSizes.size(); ++level) {
		std::string label = "MIP level #" + std::to_string(level);
		textureViewDesc.label = label.c_str();
		textureViewDesc.baseMipLevel = level;
		m_textureMipViews.push_back(m_texture.createView(textureViewDesc));

		if (level > 0) {
			Extent3D previousSize = m_textureMipSizes[level - 1];
			m_textureMipSizes[level] = {
				previousSize.width / 2,
				previousSize.height / 2,
				previousSize.depthOrArrayLayers / 2
			};
		}
	}
}

void Application::terminateTextureViews() {
	for (TextureView v : m_textureMipViews) {
		wgpuTextureViewRelease(v);
	}
	m_textureMipViews.clear();
	m_textureMipSizes.clear();
}

void Application::initBindGroup(uint32_t nextMipLevel) {
	// Create compute bind group
	std::vector<BindGroupEntry> entries(2, Default);

	// Input buffer
	entries[0].binding = 0;
	entries[0].textureView = m_textureMipViews[nextMipLevel - 1];

	// Output buffer
	entries[1].binding = 1;
	entries[1].textureView = m_textureMipViews[nextMipLevel];

	BindGroupDescriptor bindGroupDesc;
	bindGroupDesc.layout = m_bindGroupLayout;
	bindGroupDesc.entryCount = (uint32_t)entries.size();
	bindGroupDesc.entries = (WGPUBindGroupEntry*)entries.data();
	m_bindGroup = m_device.createBindGroup(bindGroupDesc);
}

void Application::terminateBindGroup() {
	wgpuBindGroupRelease(m_bindGroup);
}

void Application::initBindGroupLayout() {
	// Create bind group layout
	std::vector<BindGroupLayoutEntry> bindings(2, Default);

	// Input image: MIP level 0 of the texture
	bindings[0].binding = 0;
	bindings[0].texture.sampleType = TextureSampleType::Float;
	bindings[0].texture.viewDimension = TextureViewDimension::_2D;
	bindings[0].visibility = ShaderStage::Compute;

	// Output image: MIP level 1 of the texture
	bindings[1].binding = 1;
	bindings[1].storageTexture.access = StorageTextureAccess::WriteOnly;
	bindings[1].storageTexture.format = TextureFormat::RGBA8Unorm;
	bindings[1].storageTexture.viewDimension = TextureViewDimension::_2D;
	bindings[1].visibility = ShaderStage::Compute;

	BindGroupLayoutDescriptor bindGroupLayoutDesc;
	bindGroupLayoutDesc.entryCount = (uint32_t)bindings.size();
	bindGroupLayoutDesc.entries = bindings.data();
	m_bindGroupLayout = m_device.createBindGroupLayout(bindGroupLayoutDesc);
}

void Application::terminateBindGroupLayout() {
	wgpuBindGroupLayoutRelease(m_bindGroupLayout);
}

void Application::initComputePipeline() {
	// Load compute shader
	ShaderModule computeShaderModule = ResourceManager::loadShaderModule(RESOURCE_DIR "/compute-shader.wgsl", m_device);

	// Create compute pipeline layout
	PipelineLayoutDescriptor pipelineLayoutDesc;
	pipelineLayoutDesc.bindGroupLayoutCount = 1;
	pipelineLayoutDesc.bindGroupLayouts = (WGPUBindGroupLayout*)&m_bindGroupLayout;
	m_pipelineLayout = m_device.createPipelineLayout(pipelineLayoutDesc);

	// Create compute pipeline
	ComputePipelineDescriptor computePipelineDesc;
	computePipelineDesc.compute.constantCount = 0;
	computePipelineDesc.compute.constants = nullptr;
	computePipelineDesc.compute.entryPoint = "computeMipMap";
	computePipelineDesc.compute.module = computeShaderModule;
	computePipelineDesc.layout = m_pipelineLayout;
	m_pipeline = m_device.createComputePipeline(computePipelineDesc);
}

void Application::terminateComputePipeline() {
	wgpuComputePipelineRelease(m_pipeline);
	wgpuPipelineLayoutRelease(m_pipelineLayout);
}

void Application::onFrame() {
	glfwPollEvents();

	TextureView nextTexture = m_swapChain.getCurrentTextureView();
	if (!nextTexture) {
		std::cerr << "Cannot acquire next swap chain texture" << std::endl;
		return;
	}

	RenderPassDescriptor renderPassDesc = Default;
	WGPURenderPassColorAttachment renderPassColorAttachment{};
	renderPassColorAttachment.view = nextTexture;
	renderPassColorAttachment.resolveTarget = nullptr;
	renderPassColorAttachment.loadOp = WGPULoadOp_Clear;
	renderPassColorAttachment.storeOp = WGPUStoreOp_Store;
	renderPassColorAttachment.clearValue = WGPUColor{ 0.0, 0.0, 0.0, 1.0 };
	renderPassDesc.colorAttachmentCount = 1;
	renderPassDesc.colorAttachments = &renderPassColorAttachment;

	CommandEncoder encoder = m_device.createCommandEncoder(Default);
	RenderPassEncoder renderPass = encoder.beginRenderPass(renderPassDesc);
	onGui(renderPass);
	renderPass.end();

	CommandBuffer command = encoder.finish(CommandBufferDescriptor{});
	m_queue.submit(command);

	m_swapChain.present();
#if !defined(WEBGPU_BACKEND_WGPU)
	wgpuCommandBufferRelease(command);
	wgpuCommandEncoderRelease(encoder);
	wgpuRenderPassEncoderRelease(renderPass);
#endif

	wgpuTextureViewRelease(nextTexture);
#ifdef WEBGPU_BACKEND_WGPU
	wgpuQueueSubmit(m_queue, 0, nullptr);
#else
	wgpuDeviceTick(m_device);
#endif
}

void Application::onGui(RenderPassEncoder renderPass) {
	ImGui_ImplWGPU_NewFrame();
	ImGui_ImplGlfw_NewFrame();
	ImGui::NewFrame();

	ImDrawList* drawList = ImGui::GetBackgroundDrawList();
	drawList->AddRectFilled({ 0, 0 }, { 20, 20 }, ImColor(255, 0, 0));
	drawList->AddImage((ImTextureID)m_textureMipViews[0],{ 20, 0 }, { 220, 200 });

	bool changed = false;
	ImGui::Begin("Parameters");
	changed = ImGui::SliderFloat("Test", &m_parameters.test, 0.0f, 1.0f) || changed;
	if (ImGui::Button("Save MIP levels")) {
		// Save all MIP levels
		for (uint32_t nextLevel = 0; nextLevel < m_textureMipSizes.size(); ++nextLevel) {
			std::filesystem::path path = RESOURCE_DIR "/output.mip" + std::to_string(nextLevel) + ".png";
			saveTexture(path, m_device, m_texture, nextLevel);
		}
	}
	ImGui::End();

	m_shouldCompute = changed;

	ImGui::Render();
	ImGui_ImplWGPU_RenderDrawData(ImGui::GetDrawData(), renderPass);
}

void Application::onCompute() {
	std::cout << "Computing..." << std::endl;

	// Initialize a command encoder
	CommandEncoderDescriptor encoderDesc = Default;
	CommandEncoder encoder = m_device.createCommandEncoder(encoderDesc);

	// Create compute pass
	ComputePassDescriptor computePassDesc;
	computePassDesc.timestampWriteCount = 0;
	computePassDesc.timestampWrites = nullptr;
	ComputePassEncoder computePass = encoder.beginComputePass(computePassDesc);

	computePass.setPipeline(m_pipeline);

	for (uint32_t nextLevel = 1; nextLevel < m_textureMipSizes.size(); ++nextLevel) {
		initBindGroup(nextLevel);
		computePass.setBindGroup(0, m_bindGroup, 0, nullptr);

		uint32_t invocationCountX = m_textureMipSizes[nextLevel].width;
		uint32_t invocationCountY = m_textureMipSizes[nextLevel].height;
		uint32_t workgroupSizePerDim = 8;
		// This ceils invocationCountX / workgroupSizePerDim
		uint32_t workgroupCountX = (invocationCountX + workgroupSizePerDim - 1) / workgroupSizePerDim;
		uint32_t workgroupCountY = (invocationCountY + workgroupSizePerDim - 1) / workgroupSizePerDim;
		computePass.dispatchWorkgroups(workgroupCountX, workgroupCountY, 1);

		terminateBindGroup();
	}

	// Finalize compute pass
	computePass.end();

	// Encode and submit the GPU commands
	CommandBuffer commands = encoder.finish(CommandBufferDescriptor{});
	m_queue.submit(commands);

#if !defined(WEBGPU_BACKEND_WGPU)
	wgpuCommandBufferRelease(commands);
	wgpuCommandEncoderRelease(encoder);
	wgpuComputePassEncoderRelease(computePass);
#endif

	m_shouldCompute = false;
}

void Application::onResize() {
	terminateSwapChain();
	initSwapChain();
}
