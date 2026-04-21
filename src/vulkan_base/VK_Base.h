#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "vulkan/vulkan.hpp"

#include <VKM_Tools.h>
#include "Buffer.h"
#include "VKMDevice.h"
#include "Swapchain.h"
#include "HUD.h"
#include "DisplayWindow.h"

#define DestroyHandleBy(Func, handle)  { if (handle && VKM_Base::Get().Device()) VKM_Base::Get().Device().Func(handle, nullptr); handle = VK_NULL_HANDLE; }
#define MoveHandle handle = other.handle; other.handle = VK_NULL_HANDLE;
#define DefineHandleTypeOperator operator decltype(handle)() const { return handle; }
#define DefineAddressFunction const decltype(handle)* Address() const { return &handle; }

constexpr uint32_t maxConcurrentFrames = 2;

class VKM_Base {
	friend class DisplayWindows;
	using CreateSurfaceCallback = std::function<vk::SurfaceKHR(vk::Instance)>;
private:

	static VKM_Base* singleton;
	
	void createSurface();
	void createCmdPool();
	void createSwapChain();
	void createCmdBuffers();
	void InitializedSync();
	void createPipelineCache();

	void nextFrame();
	void updateOverlay();
protected:
	vk::Instance instance= VK_NULL_HANDLE;
	std::vector<std::string> supportedInstanceExtensions;

	vk::PhysicalDevice physicalDevice = VK_NULL_HANDLE;
	vk::PhysicalDeviceProperties physicalDeviceProperties{};
	vk::PhysicalDeviceMemoryProperties physicalDeviceMemoryProperties{};
	vk::PhysicalDeviceFeatures PhysicalDeviceFeatures{};
	vk::PhysicalDeviceFeatures enbaleFeatures{};
	std::vector<vk::PhysicalDevice> availablePhysicalDevices;

	std::vector<const char*> enabledDeviceExtensions;			//for create vk::device
	std::vector<const char*> enabledInstanceExtensions;

	void* deviceCreatepNextChain = nullptr;

	vk::Device device = VK_NULL_HANDLE;
	vk::Queue queue = VK_NULL_HANDLE;
	vk::Format depthFormat = vk::Format::eUndefined;
	vk::CommandPool cmdPool = VK_NULL_HANDLE;
	std::array<vk::CommandBuffer, maxConcurrentFrames> drawCmdBuffers;
	vk::RenderPass renderPass = VK_NULL_HANDLE;
	vk::PipelineCache pipelineCache;
	std::vector<vk::Framebuffer> frameBuffers;
	std::vector<vk::ShaderModule> shaderModules;
	vk::DescriptorPool descriptorPool = VK_NULL_HANDLE;			//child use

	SwainChain swapChain;
	CreateSurfaceCallback createSurface_callback;

	uint32_t currentImageIndex = 0;
	uint32_t currentBuffer = 0;
	std::array<vk::Semaphore, maxConcurrentFrames> imageAvaliableSemaphores{};		//before present
	std::vector<vk::Semaphore> renderCompleteSemaphores{};
	std::array<vk::Fence, maxConcurrentFrames> waitFences;

	bool requireStencil = false;
	std::string getShadersPath() const;
public:
	uint32_t width, height;
	vkm::VKMDevice* VKMDevice{};
	uint32_t apiVersion= VK_API_VERSION_1_3;
	vkm::HUD ui;
	DisplayWindows displayWindows;
	//Default depth stencil attachment used by the default render pass
	struct {
		vk::Image image;
		vk::DeviceMemory memory;
		vk::ImageView view;
	} depthStencil{};

	float frameTimer = 1.0f;
	float timer = 0.0f;
	float timerSpeed = 0.25f;

	VKM_Base();
	VKM_Base(VKM_Base&&) = delete;
	virtual ~VKM_Base();
	bool initVulkan();
	static VKM_Base& Get();
	vk::PipelineShaderStageCreateInfo loadShader(std::string fileName, vk::ShaderStageFlagBits stage);
	void windowResize();
	void renderLoop();
	void drawUI(const vk::CommandBuffer commandBuffer);

	//Prepare the next frame for workload submission by acquiring the next swap chain image and waiting for the previous command buffer to finish
	void prepareFrame(bool waitForFence = true);

	void submitFrame(bool skipQueueSubmit = false);

	virtual vkm_result createInstance();
	virtual void prepare();
	virtual void createDefaultDepthStencil();
	virtual void createRenderPass();
	virtual void createFrameBuffer();

	virtual void render();			//call in derived object
	virtual void keyPressed(uint32_t);
	virtual void mouseMoved(double x, double y, bool& handled);
	virtual void windowHasResized();
	virtual void OnUpdateHUD(vkm::HUD* ui);
	virtual void OnHandleMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

	virtual void getEnabledFeatures();
	virtual void getEnabledExtensions();
	
public:
	//Gretter
	void AddLayerOrExtension(std::vector<const char*>& container, const char* name);
	void AddInstanceExtensions(const char* extension);
	void SetCreateSurface(CreateSurfaceCallback createSurface);
	vk::Device Device() const { return device; }

};