#pragma once

#ifdef _WIN32
#define KEY_ESCAPE VK_ESCAPE 
#define KEY_F1 VK_F1
#define KEY_F2 VK_F2
#define KEY_F3 VK_F3
#define KEY_F4 VK_F4
#define KEY_F5 VK_F5
#define KEY_W 0x57
#define KEY_A 0x41
#define KEY_S 0x53
#define KEY_D 0x44
#define KEY_P 0x50
#define KEY_SPACE 0x20
#define KEY_KPADD 0x6B
#define KEY_KPSUB 0x6D
#define KEY_B 0x42
#define KEY_F 0x46
#define KEY_L 0x4C
#define KEY_N 0x4E
#define KEY_O 0x4F
#define KEY_T 0x54
#endif
#include "VKM_Tools.h"
#include "camera.hpp"
#include "HUD.h"
#include <ShellScalingAPI.h>
#include "imgui.h"

class VKM_Base;

class DisplayWindows {
private:
	vkm::HUD* ui;
	VKM_Base* base;			//point to derived object

public:
	bool resizing = false;
	uint32_t frameCounter = 0;
	uint32_t lastFPS = 0;
	std::chrono::time_point<std::chrono::high_resolution_clock> lastTimestamp, tPrevEnd;

	uint32_t destWidth{};
	uint32_t destHeight{};

	Camera camera;
	HWND window;
	HINSTANCE windowInstance;
	std::string title = "vulkan";
	std::string name = "vulkanMiniRender";

	bool prepared = false;
	bool resized = false;
	bool paused = false;

	struct Settings {
		/** @brief Activates validation layers (and message output) when set to true */
		bool validation = true;
		/** @brief Set to true if fullscreen mode has been requested via command line */
		bool fullscreen = false;
		/** @brief Set to true if v-sync will be forced for the swapchain */
		bool vsync = false;
		/** @brief Enable UI overlay */
		bool overlay = true;
	} settings;

	struct MouseState{
		struct Button{
			bool left = false;
			bool right = false;
			bool middle = false;
		} buttons;
		glm::vec2 position;
	} mouseState;

	DisplayWindows() {};
	DisplayWindows(VKM_Base* base_) :base(base_) {};
	void SetUI(vkm::HUD* ui);

	std::string getWindowTitle() const;
	HWND setupWindow(HINSTANCE hinstance, WNDPROC wndproc);
	void handleMouseMove(int32_t x, int32_t y);
	void handleMessages(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
	void setupConsole(std::string title);
	void setupDPIAwareness();
	void updateFPS();		//call during nextFrame
};
