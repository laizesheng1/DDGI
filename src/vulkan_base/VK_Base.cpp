#include "VK_Base.h"

VKM_Base* VKM_Base::singleton = nullptr;

VKM_Base::VKM_Base()
{
	width = 1280;
	height = 720;
	displayWindows = DisplayWindows(this);
	displayWindows.SetUI(&ui);

#if defined(_WIN32)
	// Enable console if validation is active, debug message callback will output to it
	if (displayWindows.settings.validation)
	{
		displayWindows.setupConsole("Vulkan example");
	}
	displayWindows.setupDPIAwareness();
#endif
}

VKM_Base::~VKM_Base()
{
	swapChain.cleanup();
	if (descriptorPool)
	{
		device.destroyDescriptorPool(descriptorPool);
	}
	device.freeCommandBuffers(cmdPool, static_cast<uint32_t>(drawCmdBuffers.size()), drawCmdBuffers.data());
	if (renderPass != VK_NULL_HANDLE) {
		device.destroyRenderPass(renderPass);
	}
	for (auto& frameBuffer : frameBuffers) {
		device.destroyFramebuffer(frameBuffer);
	}
	for (auto& shaderModule : shaderModules) {
		device.destroyShaderModule(shaderModule);
	}
	device.destroyImageView(depthStencil.view);
	device.destroyImage(depthStencil.image);
	device.freeMemory(depthStencil.memory);
	device.destroyPipelineCache(pipelineCache);
	device.destroyCommandPool(cmdPool);
	for (auto& fence : waitFences) {
		device.destroyFence(fence);
	}
	for (auto& semaphore : imageAvaliableSemaphores) {
		device.destroySemaphore(semaphore);
	}
	for (auto& semaphore : renderCompleteSemaphores) {
		device.destroySemaphore(semaphore);
	}
	if (displayWindows.settings.overlay)
	{
		ui.freeResources();
	}
	delete VKMDevice;
	if (displayWindows.settings.validation)
	{
		vkm::debug::freeDebugCallback(instance);
	}
	instance.destroy();
}

void VKM_Base::prepare()
{
	createSurface();
	createCmdPool();
	createSwapChain();
	createCmdBuffers();
	InitializedSync();
	createDefaultDepthStencil();
	createRenderPass();
	createPipelineCache();
	createFrameBuffer();

	ui.maxFrames = maxConcurrentFrames;
	ui.device = VKMDevice;
	ui.queue = queue;
	ui.shaders = {
			loadShader(vkm::tools::getShaderPath() + "/glsl/base/uioverlay.vert.spv", vk::ShaderStageFlagBits::eVertex),
			loadShader(vkm::tools::getShaderPath() + "/glsl/base/uioverlay.frag.spv", vk::ShaderStageFlagBits::eFragment),
	};
	ui.prepareResources();
	ui.createPipeline(pipelineCache, renderPass, swapChain.colorFormat, depthFormat);
}

vkm_result VKM_Base::createInstance()
{
	std::vector<const char*> instanceExtensions = { VK_KHR_SURFACE_EXTENSION_NAME };
	instanceExtensions.push_back("VK_KHR_win32_surface");

	// Get extensions supported by the instance and store for later use
	std::vector<vk::ExtensionProperties> extensions = vk::enumerateInstanceExtensionProperties();
	if (extensions.size() > 0)
	{
		for (VkExtensionProperties& extension : extensions)
		{
			supportedInstanceExtensions.push_back(extension.extensionName);
		}
	}
	// Enabled requested instance extensions
	if (!enabledInstanceExtensions.empty())
	{
		for (const char* enabledExtension : enabledInstanceExtensions)
		{
			// Output message if requested extension is not available
			if (std::find(supportedInstanceExtensions.begin(), supportedInstanceExtensions.end(), enabledExtension) == supportedInstanceExtensions.end())
			{
				std::cerr << "Enabled instance extension \"" << enabledExtension << "\" is not present at instance level\n";
			}
			instanceExtensions.push_back(enabledExtension);
		}
	}
	if (displayWindows.settings.validation)
	{
		instanceExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
	}

	vk::ApplicationInfo applicationInfo;
	applicationInfo.setApiVersion(apiVersion)
		.setPEngineName(displayWindows.name.c_str())
		.setPApplicationName(displayWindows.name.c_str());

	vk::InstanceCreateInfo instanceCreateInfo;
	const char* validationLayerName = "VK_LAYER_KHRONOS_validation";
	if (displayWindows.settings.validation) {
		// Check if this layer is available at instance level
		std::vector<vk::LayerProperties> instanceLayerProperties = vk::enumerateInstanceLayerProperties();
		bool validationLayerPresent = false;
		for (VkLayerProperties& layer : instanceLayerProperties) {
			if (strcmp(layer.layerName, validationLayerName) == 0) {
				validationLayerPresent = true;
				break;
			}
		}
		if (validationLayerPresent) {
			instanceCreateInfo.setPEnabledLayerNames(validationLayerName);
		}
		else {
			std::cerr << "Validation layer VK_LAYER_KHRONOS_validation not present, validation is disabled";
		}
	}
	//get validation message during create instance
	vk::DebugUtilsMessengerCreateInfoEXT debugUtilsMessengerCI;
	if (displayWindows.settings.validation) {
		vkm::debug::setupDebugingMessengerCreateInfo(debugUtilsMessengerCI);
		debugUtilsMessengerCI.pNext = instanceCreateInfo.pNext;
		instanceCreateInfo.pNext = &debugUtilsMessengerCI;
	}

	instanceCreateInfo.setPApplicationInfo(&applicationInfo)
		.setEnabledExtensionCount((uint32_t)instanceExtensions.size())
		.setPpEnabledExtensionNames(instanceExtensions.data());

	if (vk::Result result = vk::createInstance(&instanceCreateInfo, nullptr, &instance); result != vk::Result::eSuccess) {
		std::cout << std::format("[ VK_Base ] ERROR\nFailed to create a vulkan instance!\nError code: {}\n", int32_t(result));
		return result;
	}
	return vk::Result::eSuccess;
}

bool VKM_Base::initVulkan()
{
	vk::Result result = createInstance();

	if (displayWindows.settings.validation)
	{
		vkm::debug::Init(instance);
		vkm::debug::setupDebugging(instance);
	}

	// Physical device
	auto devices = instance.enumeratePhysicalDevices();
	if (devices.size()==0)
	{
		OutputMessage("No device with Vulkan support found!");
		return false;
	}
	physicalDevice = devices[0];
	if (physicalDevice == nullptr)
	{
		OutputMessage("failed to find a suitable GPU!");
		return false;
	}
	physicalDeviceProperties = physicalDevice.getProperties();
	std::cout << "Renderer: " << physicalDeviceProperties.deviceName << std::endl;

	PhysicalDeviceFeatures = physicalDevice.getFeatures();
	physicalDeviceMemoryProperties= physicalDevice.getMemoryProperties();

	getEnabledFeatures();

	VKMDevice = new vkm::VKMDevice(physicalDevice);
	getEnabledExtensions();

	result = VKMDevice->createLogicalDevice(enbaleFeatures, enabledDeviceExtensions, deviceCreatepNextChain);
	if (result != vk::Result::eSuccess)
	{
		OutputMessage("[ VK_Base ] ERROR\nFailed to create logical device!\nError code: {}\n", int32_t(result));
		return false;
	}
	device = VKMDevice->logicalDevice;
	queue = device.getQueue(VKMDevice->queueFamilyIndices.graphics, 0);
	//Find a suitable depth and/or stencil format
	vk::Bool32 validFormat = false;
	if (requireStencil) {
		validFormat = vkm::tools::getSupportedDepthStencilFormat(physicalDevice, &depthFormat);
	}
	else {
		validFormat = vkm::tools::getSupportedDepthFormat(physicalDevice, &depthFormat);
	}
	//create surface
	if(createSurface_callback)
	{
		vk::SurfaceKHR surface = createSurface_callback(instance);
		swapChain.Surface(surface);
	}
	swapChain.setContext(instance, physicalDevice, device);
	return true;
}

VKM_Base& VKM_Base::Get()
{
	if (singleton == nullptr)
		singleton = new VKM_Base();
	return *singleton;
}

vk::PipelineShaderStageCreateInfo VKM_Base::loadShader(std::string fileName, vk::ShaderStageFlagBits stage)
{
	vk::PipelineShaderStageCreateInfo shderCreateInfo;
	shderCreateInfo.setStage(stage)
		.setPName("main")
		.setModule(vkm::tools::loadShader(fileName.c_str(), device));
	assert(shderCreateInfo.module != VK_NULL_HANDLE);
	shaderModules.push_back(shderCreateInfo.module);
	return shderCreateInfo;
}

void VKM_Base::windowResize()
{
	if (!displayWindows.prepared) {
		return;
	}
	displayWindows.prepared = false;
	displayWindows.resized = true;

	// Ensure all operations on the device have been finished before destroying resources
	device.waitIdle();

	// Recreate swap chain
	width = displayWindows.destWidth;
	height = displayWindows.destHeight;
	createSwapChain();

	// Recreate the frame buffers
	device.destroyImageView(depthStencil.view);
	device.destroyImage(depthStencil.image);
	device.freeMemory(depthStencil.memory);
	createDefaultDepthStencil();
	for (auto& frameBuffer : frameBuffers) {
		device.destroyFramebuffer(frameBuffer);
	}
	createFrameBuffer();

	if ((width > 0.0f) && (height > 0.0f)) {
		ui.resize(width, height);
	}

	for (auto& semaphore : imageAvaliableSemaphores) {
		device.destroySemaphore(semaphore);
	}
	for (auto& semaphore : renderCompleteSemaphores) {
		device.destroySemaphore(semaphore);
	}
	for (auto& fence : waitFences) {
		device.destroyFence(fence);
	}
	InitializedSync();

	device.waitIdle();
	if ((width > 0.0f) && (height > 0.0f)) {
		displayWindows.camera.updateAspectRatio((float)width / (float)height);
	}

	// Notify derived class
	windowHasResized();

	displayWindows.prepared = true;
}

void VKM_Base::drawUI(const vk::CommandBuffer commandBuffer)
{
	if (displayWindows.settings.overlay && ui.visible) {
		const vk::Viewport viewport(0.0, 0.0, (float)width, (float)height, 0.0, 1.0);
		const vk::Rect2D scissor({ 0,0 }, { width,height });
		commandBuffer.setViewport(0, viewport);
		commandBuffer.setScissor(0, scissor);
		ui.draw(commandBuffer, currentBuffer);
	}
}

void VKM_Base::prepareFrame(bool waitForFence)
{
	if (waitForFence) {
		VK_CHECK_RESULT(device.waitForFences(1, &waitFences[currentBuffer], VK_TRUE, UINT64_MAX));
		VK_CHECK_RESULT(device.resetFences(1, &waitFences[currentBuffer]));
	}

	updateOverlay();
	// Acquire the next image from the swap chain
	vk::Result result = swapChain.acquireNextImage(imageAvaliableSemaphores[currentBuffer], currentImageIndex);
	// Recreate the swapchain if it's no longer compatible with the surface (OUT_OF_DATE)
	// If no longer optimal (VK_SUBOPTIMAL_KHR), wait until submitFrame() in case number of swapchain images will change on resize
	if ((result == vk::Result::eErrorOutOfDateKHR ) || (result == vk::Result::eSuboptimalKHR)) {
		if (result == vk::Result::eErrorOutOfDateKHR) {
			windowResize();
		}
		return;
	}
	else {
		VK_CHECK_RESULT(result);
	}
}

void VKM_Base::submitFrame(bool skipQueueSubmit)
{
	if (!skipQueueSubmit)
	{
		const vk::PipelineStageFlags waitPipelineStage(vk::PipelineStageFlagBits::eColorAttachmentOutput);
		vk::SubmitInfo submitInfo;
		submitInfo.setWaitSemaphoreCount(1)
			.setWaitSemaphores(imageAvaliableSemaphores[currentBuffer])
			.setWaitDstStageMask(waitPipelineStage)
			.setCommandBuffers(drawCmdBuffers[currentBuffer])
			.setSignalSemaphores(renderCompleteSemaphores[currentImageIndex]);
		VK_CHECK_RESULT(queue.submit(1, &submitInfo, waitFences[currentBuffer]));
	}
	vk::PresentInfoKHR presentInfo;
	presentInfo.setWaitSemaphores(renderCompleteSemaphores[currentImageIndex])
		.setSwapchains(swapChain.swapChain)
		.setImageIndices(currentImageIndex);
	vkm_result result = queue.presentKHR(presentInfo);
	if (result == vk::Result::eErrorOutOfDateKHR || result == vk::Result::eSuboptimalKHR)
	{
		windowResize();
		if (result == vk::Result::eErrorOutOfDateKHR)
			return;
	}
	else {
		VK_CHECK_RESULT(result);
	}
	currentBuffer = (currentBuffer + 1) % maxConcurrentFrames;
}

void VKM_Base::createSurface()
{
	swapChain.initSurface(displayWindows.windowInstance, displayWindows.window);
}

void VKM_Base::createCmdPool()
{
	vk::CommandPoolCreateInfo cmdPoolInfo;
	cmdPoolInfo.setFlags(vk::CommandPoolCreateFlagBits::eResetCommandBuffer)
		.setQueueFamilyIndex(swapChain.queueNodeIndex);
	VK_CHECK_RESULT(device.createCommandPool(&cmdPoolInfo, nullptr,&cmdPool));
}

void VKM_Base::createSwapChain()
{
	swapChain.CreateSwapchain(width, height, false);
}

void VKM_Base::createCmdBuffers()
{
	vk::CommandBufferAllocateInfo cmdBufAllocateInfo;
	cmdBufAllocateInfo.setCommandPool(cmdPool)
		.setLevel(vk::CommandBufferLevel::ePrimary)
		.setCommandBufferCount(static_cast<uint32_t>(drawCmdBuffers.size()));
	VK_CHECK_RESULT(device.allocateCommandBuffers(&cmdBufAllocateInfo, drawCmdBuffers.data()));
}

void VKM_Base::InitializedSync()
{
	// Wait fences to sync command buffer access
	vk::FenceCreateInfo fenceCreateInfo;
	fenceCreateInfo.setFlags(vk::FenceCreateFlagBits::eSignaled);

	for (auto& fence : waitFences) {
		VK_CHECK_RESULT(device.createFence(&fenceCreateInfo, nullptr, &fence));
	}
	// Used to ensure that image presentation is complete before starting to submit again
	for (auto& semaphore : imageAvaliableSemaphores) {
		vk::SemaphoreCreateInfo semaphoreCreateInfo;
		VK_CHECK_RESULT(device.createSemaphore(&semaphoreCreateInfo, nullptr, &semaphore));
	}
	// Semaphore used to ensure that all commands submitted have been finished before submitting the image to the queue
	renderCompleteSemaphores.resize(swapChain.images.size());
	for (auto& semaphore : renderCompleteSemaphores) {
		vk::SemaphoreCreateInfo semaphoreCreateInfo;
		VK_CHECK_RESULT(device.createSemaphore(&semaphoreCreateInfo, nullptr, &semaphore));
	}
}

void VKM_Base::createDefaultDepthStencil()
{
	vk::ImageCreateInfo createInfo;
	createInfo.setArrayLayers(1)
		.setExtent({ width, height, 1 })
		.setFormat(depthFormat)
		.setInitialLayout(vk::ImageLayout::eUndefined)
		.setMipLevels(1)
		.setTiling(vk::ImageTiling::eOptimal)
		.setUsage(vk::ImageUsageFlagBits::eDepthStencilAttachment)
		.setSharingMode(vk::SharingMode::eExclusive)
		.setSamples(vk::SampleCountFlagBits::e1)
		.setImageType(vk::ImageType::e2D);
	//create image | alloc memory | bind image to memory
	VK_CHECK_RESULT(device.createImage(&createInfo, nullptr, &depthStencil.image));
	vk::MemoryRequirements memReqs=device.getImageMemoryRequirements(depthStencil.image);
	vk::MemoryAllocateInfo memAllocInfo;
	memAllocInfo.setAllocationSize(memReqs.size)
		.setMemoryTypeIndex(VKMDevice->queryMemTypeIndex(memReqs.memoryTypeBits, vk::MemoryPropertyFlagBits::eDeviceLocal));
	VK_CHECK_RESULT(device.allocateMemory(&memAllocInfo, nullptr, &depthStencil.memory));			
	device.bindImageMemory(depthStencil.image, depthStencil.memory, 0);
	//create imageview
	vk::ImageViewCreateInfo imageViewCreateInfo;
	vk::ImageSubresourceRange range;
	range.setAspectMask(vk::ImageAspectFlagBits::eDepth)
		.setBaseArrayLayer(0)
		.setBaseMipLevel(0)
		.setLayerCount(1)
		.setLevelCount(1);
	imageViewCreateInfo.setImage(depthStencil.image)
		.setViewType(vk::ImageViewType::e2D)
		.setFormat(depthFormat)
		.setSubresourceRange(range);
	if (depthFormat >= vk::Format::eD16UnormS8Uint)
	{
		imageViewCreateInfo.subresourceRange.aspectMask |= vk::ImageAspectFlagBits::eStencil;
	}
	VK_CHECK_RESULT(device.createImageView(&imageViewCreateInfo, nullptr, &depthStencil.view));
}

void VKM_Base::createRenderPass()
{
	vk::AttachmentDescription ColorattachDes;
	ColorattachDes.setFormat(swapChain.colorFormat)			
		.setInitialLayout(vk::ImageLayout::eUndefined)					
		.setFinalLayout(vk::ImageLayout::ePresentSrcKHR)			
		.setLoadOp(vk::AttachmentLoadOp::eClear)			
		.setStoreOp(vk::AttachmentStoreOp::eStore)			
		.setSamples(vk::SampleCountFlagBits::e1)
		.setStencilLoadOp(vk::AttachmentLoadOp::eDontCare)			
		.setStencilStoreOp(vk::AttachmentStoreOp::eDontCare);

	vk::AttachmentDescription depthAttachment;
	depthAttachment.setFormat(depthFormat)
		.setSamples(vk::SampleCountFlagBits::e1)
		.setLoadOp(vk::AttachmentLoadOp::eClear)
		.setStoreOp(vk::AttachmentStoreOp::eStore)
		.setStencilLoadOp(vk::AttachmentLoadOp::eClear)
		.setStencilStoreOp(vk::AttachmentStoreOp::eDontCare)
		.setInitialLayout(vk::ImageLayout::eUndefined)
		.setFinalLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);

	std::array<vk::AttachmentDescription, 2> attachments = { ColorattachDes ,depthAttachment };

	vk::AttachmentReference colorReference;
	colorReference.setAttachment(0)
		.setLayout(vk::ImageLayout::eColorAttachmentOptimal);
	vk::AttachmentReference depthReference;
	depthReference.setAttachment(1)
		.setLayout(vk::ImageLayout::eDepthStencilAttachmentOptimal);
	vk::SubpassDescription subpassDes;
	subpassDes.setColorAttachments(colorReference)			//设置的颜色附着在数组中的索引会被片段着色器使用
		.setColorAttachmentCount(1)
		.setPDepthStencilAttachment(&depthReference);

	//dependencies for layout transitions
	std::array<vk::SubpassDependency, 2> dependencies{};
	dependencies[0].setSrcSubpass(VK_SUBPASS_EXTERNAL)
		.setDstSubpass(0)
		.setSrcStageMask(vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests)
		.setDstStageMask(vk::PipelineStageFlagBits::eEarlyFragmentTests | vk::PipelineStageFlagBits::eLateFragmentTests)
		.setSrcAccessMask(vk::AccessFlagBits::eDepthStencilAttachmentWrite)
		.setDstAccessMask(vk::AccessFlagBits::eDepthStencilAttachmentWrite | vk::AccessFlagBits::eDepthStencilAttachmentRead);

	dependencies[1].setSrcSubpass(VK_SUBPASS_EXTERNAL)
		.setDstSubpass(0)
		.setSrcStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput)
		.setDstStageMask(vk::PipelineStageFlagBits::eColorAttachmentOutput)
		.setSrcAccessMask(vk::AccessFlagBits::eNone)
		.setDstAccessMask(vk::AccessFlagBits::eColorAttachmentRead | vk::AccessFlagBits::eColorAttachmentWrite);
	vk::RenderPassCreateInfo renderPassCreateInfo;
	renderPassCreateInfo.setAttachments(attachments)
		.setSubpasses(subpassDes)
		.setDependencies(dependencies);
	VK_CHECK_RESULT(device.createRenderPass(&renderPassCreateInfo, nullptr, &renderPass));
}

void VKM_Base::createPipelineCache()
{
	vk::PipelineCacheCreateInfo pipelineCacheCreateInfo;
	VK_CHECK_RESULT(device.createPipelineCache(&pipelineCacheCreateInfo, nullptr, &pipelineCache));
}

void VKM_Base::createFrameBuffer()
{
	frameBuffers.resize(swapChain.images.size());
	for (uint32_t i = 0; i < frameBuffers.size(); i++) {
		const vk::ImageView attachments[2] = { swapChain.imageViews[i], depthStencil.view };
		vk::FramebufferCreateInfo frameBufferCreateInfo;
		frameBufferCreateInfo.setRenderPass(renderPass)
			.setAttachments(attachments)
			.setWidth(width)
			.setHeight(height)
			.setLayers(1);
		VK_CHECK_RESULT(device.createFramebuffer(&frameBufferCreateInfo, nullptr, &frameBuffers[i]));
	}
}

void VKM_Base::renderLoop()
{
	displayWindows.destWidth = width;
	displayWindows.destHeight = height;
	displayWindows.lastTimestamp = std::chrono::high_resolution_clock::now();
	displayWindows.tPrevEnd = displayWindows.lastTimestamp;
#if defined(_WIN32)
	MSG msg;
	bool quitMessageReceived = false;
	while (!quitMessageReceived) {
		while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {			//handle message input (keyboard\mouse)
			TranslateMessage(&msg);
			DispatchMessage(&msg);
			if (msg.message == WM_QUIT) {
				quitMessageReceived = true;
				break;
			}
		}
		if (displayWindows.prepared && !IsIconic(displayWindows.window)) {
			nextFrame();
		}
	}
#endif
	if (device != VK_NULL_HANDLE)
	{
		device.waitIdle();
	}
}

void VKM_Base::nextFrame()
{
	auto tStart = std::chrono::high_resolution_clock::now();
	render();
	displayWindows.frameCounter++;
	auto tEnd = std::chrono::high_resolution_clock::now();

	auto tDiff = std::chrono::duration<double, std::milli>(tEnd - tStart).count();
	frameTimer = (float)tDiff / 1000.0f;
	displayWindows.camera.update(frameTimer);
	// Convert to clamped timer value
	if (!displayWindows.paused)
	{
		timer += timerSpeed * frameTimer;
		if (timer > 1.0)
		{
			timer -= 1.0f;
		}
	}
	float fpsTimer = (float)(std::chrono::duration<double, std::milli>(tEnd - displayWindows.lastTimestamp).count());
	if (fpsTimer > 1000.0f)
	{
		displayWindows.lastFPS = static_cast<uint32_t>((float)displayWindows.frameCounter * (1000.0f / fpsTimer));
#if defined(_WIN32)
		if (!displayWindows.settings.overlay) {
			std::string windowTitle = displayWindows.getWindowTitle();
			SetWindowText(displayWindows.window, windowTitle.c_str());
		}
#endif
		displayWindows.frameCounter = 0;
		displayWindows.lastTimestamp = tEnd;
	}
	displayWindows.tPrevEnd = tEnd;
}

void VKM_Base::updateOverlay()
{
	const DisplayWindows::Settings& settings = displayWindows.settings;
	const DisplayWindows::MouseState& mouseState = displayWindows.mouseState;

	if (!settings.overlay)
		return;

	ImGuiIO& io = ImGui::GetIO();
	io.DisplaySize = ImVec2((float)width, (float)height);
	io.DeltaTime = frameTimer;
	io.MousePos = ImVec2(mouseState.position.x, mouseState.position.y);
	io.MouseDown[0] = mouseState.buttons.left && ui.visible;
	io.MouseDown[1] = mouseState.buttons.right && ui.visible;
	io.MouseDown[2] = mouseState.buttons.middle && ui.visible;

	ImGui::NewFrame();
	ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);
	ImGui::SetNextWindowPos(ImVec2(10 * ui.scale, 10 * ui.scale));
	ImGui::SetNextWindowSize(ImVec2(0, 0), ImGuiSetCond_FirstUseEver);
	ImGui::Begin("Vulkan Example", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove);
	ImGui::TextUnformatted(displayWindows.title.c_str());
	ImGui::TextUnformatted(physicalDeviceProperties.deviceName);
	ImGui::Text("%.2f ms/frame (%.1d fps)", (1000.0f / displayWindows.lastFPS), displayWindows.lastFPS);
	ImGui::PushItemWidth(110.0f * ui.scale);

	OnUpdateHUD(&ui);			//call in derived

	ImGui::PopItemWidth();
	ImGui::End();
	ImGui::PopStyleVar();
	ImGui::Render();

	ui.update(currentBuffer);
}

void VKM_Base::render(){}

void VKM_Base::keyPressed(uint32_t){}

void VKM_Base::mouseMoved(double x, double y, bool& handled){}

void VKM_Base::windowHasResized() {}

void VKM_Base::OnUpdateHUD(vkm::HUD* ui) {}

void VKM_Base::OnHandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {}

void VKM_Base::getEnabledFeatures()
{

}

void VKM_Base::getEnabledExtensions()
{
}

void VKM_Base::AddLayerOrExtension(std::vector<const char*>& container, const char* name)
{
	for (auto& i : container)
		if (!strcmp(name, i))
			return;
	container.push_back(name);
}

void VKM_Base::AddInstanceExtensions(const char* extension)
{
	AddLayerOrExtension(enabledDeviceExtensions, extension);
}

void VKM_Base::SetCreateSurface(CreateSurfaceCallback createSurface)
{
	this->createSurface_callback = createSurface;
}

std::string VKM_Base::getShadersPath() const
{
	return vkm::tools::getShaderPath() + "glsl/";
}