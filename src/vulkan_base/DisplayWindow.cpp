#include "DisplayWindow.h"
#include "VK_Base.h"

void DisplayWindows::SetUI(vkm::HUD* ui)
{
	this->ui = ui;
}

std::string DisplayWindows::getWindowTitle() const
{
	std::string windowTitle = title + " - " + base->physicalDeviceProperties.deviceName.data();
	if (!settings.overlay) {
		windowTitle += " - " + std::to_string(frameCounter) + " fps";
	}
	return windowTitle;
}

HWND DisplayWindows::setupWindow(HINSTANCE hinstance, WNDPROC wndproc)
{
	this->windowInstance = hinstance;

	WNDCLASSEX wndClass{
		.cbSize = sizeof(WNDCLASSEX),
		.style = CS_HREDRAW | CS_VREDRAW,
		.lpfnWndProc = wndproc,
		.cbClsExtra = 0,
		.cbWndExtra = 0,
		.hInstance = hinstance,
		.hIcon = LoadIcon(NULL, IDI_APPLICATION),
		.hCursor = LoadCursor(NULL, IDC_ARROW),
		.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH),
		.lpszMenuName = NULL,
		.lpszClassName = name.c_str(),
		.hIconSm = LoadIcon(NULL, IDI_WINLOGO),
	};

	if (!RegisterClassEx(&wndClass))
	{
		std::cout << "Could not register window class!\n";
		fflush(stdout);
		exit(1);
	}

	int screenWidth = GetSystemMetrics(SM_CXSCREEN);
	int screenHeight = GetSystemMetrics(SM_CYSCREEN);

	if (settings.fullscreen)
	{
		if ((base->width != (uint32_t)screenWidth) && (base->height != (uint32_t)screenHeight))
		{
			DEVMODE dmScreenSettings;
			memset(&dmScreenSettings, 0, sizeof(dmScreenSettings));
			dmScreenSettings.dmSize = sizeof(dmScreenSettings);
			dmScreenSettings.dmPelsWidth = base->width;
			dmScreenSettings.dmPelsHeight = base->height;
			dmScreenSettings.dmBitsPerPel = 32;
			dmScreenSettings.dmFields = DM_BITSPERPEL | DM_PELSWIDTH | DM_PELSHEIGHT;
			if (ChangeDisplaySettings(&dmScreenSettings, CDS_FULLSCREEN) != DISP_CHANGE_SUCCESSFUL)
			{
				if (MessageBox(NULL, "Fullscreen Mode not supported!\n Switch to window mode?", "Error", MB_YESNO | MB_ICONEXCLAMATION) == IDYES)
				{
					settings.fullscreen = false;
				}
				else
				{
					return nullptr;
				}
			}
			screenWidth = base->width;
			screenHeight = base->height;
		}

	}

	DWORD dwExStyle;
	DWORD dwStyle;

	if (settings.fullscreen)
	{
		dwExStyle = WS_EX_APPWINDOW;
		dwStyle = WS_POPUP | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
	}
	else
	{
		dwExStyle = WS_EX_APPWINDOW | WS_EX_WINDOWEDGE;
		dwStyle = WS_OVERLAPPEDWINDOW | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
	}

	RECT windowRect{
		.left = 0L,
		.top = 0L,
		.right = settings.fullscreen ? (long)screenWidth : (long)base->width,
		.bottom = settings.fullscreen ? (long)screenHeight : (long)base->height
	};
	AdjustWindowRectEx(&windowRect, dwStyle, FALSE, dwExStyle);

	std::string windowTitle = getWindowTitle();
	window = CreateWindowEx(0,
		name.c_str(),
		windowTitle.c_str(),
		dwStyle | WS_CLIPSIBLINGS | WS_CLIPCHILDREN,
		0,
		0,
		windowRect.right - windowRect.left,
		windowRect.bottom - windowRect.top,
		NULL,
		NULL,
		hinstance,
		NULL);

	if (!window)
	{
		std::cerr << "Could not create window!\n";
		fflush(stdout);
		return nullptr;
	}

	if (!settings.fullscreen)
	{
		// Center on screen
		uint32_t x = (GetSystemMetrics(SM_CXSCREEN) - windowRect.right) / 2;
		uint32_t y = (GetSystemMetrics(SM_CYSCREEN) - windowRect.bottom) / 2;
		SetWindowPos(window, 0, x, y, 0, 0, SWP_NOZORDER | SWP_NOSIZE);
	}

	ShowWindow(window, SW_SHOW);
	SetForegroundWindow(window);
	SetFocus(window);

	return window;
}

void DisplayWindows::handleMouseMove(int32_t x, int32_t y)
{
	int32_t dx = (int32_t)mouseState.position.x - x;
	int32_t dy = (int32_t)mouseState.position.y - y;

	bool handled = false;
	ImGuiIO& io = ImGui::GetIO();
	handled = io.WantCaptureMouse && ui->visible;

	base->mouseMoved((float)x, (float)y, handled);

	if (handled) {
		mouseState.position = glm::vec2((float)x, (float)y);
		return;
	}

	if (mouseState.buttons.left) {
		camera.rotate(glm::vec3(dy * camera.rotationSpeed, -dx * camera.rotationSpeed, 0.0f));
	}
	if (mouseState.buttons.right) {
		camera.translate(glm::vec3(-0.0f, 0.0f, dy * .005f));
	}
	if (mouseState.buttons.middle) {
		camera.translate(glm::vec3(-dx * 0.005f, -dy * 0.005f, 0.0f));
	}
	mouseState.position = glm::vec2((float)x, (float)y);
}

void DisplayWindows::handleMessages(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	switch (uMsg)
	{
	case WM_CLOSE:
		prepared = false;
		DestroyWindow(hWnd);
		PostQuitMessage(0);
		break;
	case WM_PAINT:
		ValidateRect(window, NULL);
		break;
	case WM_KEYDOWN:
		switch (wParam)
		{
		case KEY_P:
			paused = !paused;
			break;
		case KEY_F1:
			ui->visible = !ui->visible;
			break;
		case KEY_F2:
			if (camera.type == Camera::CameraType::lookat) {
				camera.type = Camera::CameraType::firstperson;
			}
			else {
				camera.type = Camera::CameraType::lookat;
			}
			break;
		case KEY_ESCAPE:
			PostQuitMessage(0);
			break;
		}

		if (camera.type == Camera::firstperson)
		{
			switch (wParam)
			{
			case KEY_W:
				camera.keys.up = true;
				break;
			case KEY_S:
				camera.keys.down = true;
				break;
			case KEY_A:
				camera.keys.left = true;
				break;
			case KEY_D:
				camera.keys.right = true;
				break;
			}
		}

		base->keyPressed((uint32_t)wParam);
		break;
	case WM_KEYUP:
		if (camera.type == Camera::firstperson)
		{
			switch (wParam)
			{
			case KEY_W:
				camera.keys.up = false;
				break;
			case KEY_S:
				camera.keys.down = false;
				break;
			case KEY_A:
				camera.keys.left = false;
				break;
			case KEY_D:
				camera.keys.right = false;
				break;
			}
		}
		break;
	case WM_LBUTTONDOWN:
		mouseState.position = glm::vec2((float)LOWORD(lParam), (float)HIWORD(lParam));
		mouseState.buttons.left = true;
		break;
	case WM_RBUTTONDOWN:
		mouseState.position = glm::vec2((float)LOWORD(lParam), (float)HIWORD(lParam));
		mouseState.buttons.right = true;
		break;
	case WM_MBUTTONDOWN:
		mouseState.position = glm::vec2((float)LOWORD(lParam), (float)HIWORD(lParam));
		mouseState.buttons.middle = true;
		break;
	case WM_LBUTTONUP:
		mouseState.buttons.left = false;
		break;
	case WM_RBUTTONUP:
		mouseState.buttons.right = false;
		break;
	case WM_MBUTTONUP:
		mouseState.buttons.middle = false;
		break;
	case WM_MOUSEWHEEL:
	{
		short wheelDelta = GET_WHEEL_DELTA_WPARAM(wParam);
		camera.translate(glm::vec3(0.0f, 0.0f, (float)wheelDelta * 0.005f));
		break;
	}
	case WM_MOUSEMOVE:
	{
		handleMouseMove(LOWORD(lParam), HIWORD(lParam));
		break;
	}
	case WM_SIZE:
		if ((prepared) && (wParam != SIZE_MINIMIZED))
		{
			if ((resizing) || ((wParam == SIZE_MAXIMIZED) || (wParam == SIZE_RESTORED)))
			{
				destWidth = LOWORD(lParam);
				destHeight = HIWORD(lParam);
				base->windowResize();
			}
		}
		break;
	case WM_GETMINMAXINFO:
	{
		LPMINMAXINFO minMaxInfo = (LPMINMAXINFO)lParam;
		minMaxInfo->ptMinTrackSize.x = 64;
		minMaxInfo->ptMinTrackSize.y = 64;
		break;
	}
	case WM_ENTERSIZEMOVE:
		resizing = true;
		break;
	case WM_EXITSIZEMOVE:
		resizing = false;
		break;
	}
	base->OnHandleMessage(hWnd, uMsg, wParam, lParam);
}

#if defined(_WIN32)
// Win32 : Sets up a console window and redirects standard output to it
void DisplayWindows::setupConsole(std::string title)
{
	AllocConsole();
	AttachConsole(GetCurrentProcessId());
	FILE* stream;
	freopen_s(&stream, "CONIN$", "r", stdin);
	freopen_s(&stream, "CONOUT$", "w+", stdout);
	freopen_s(&stream, "CONOUT$", "w+", stderr);
	// Enable flags so we can color the output
	HANDLE consoleHandle = GetStdHandle(STD_OUTPUT_HANDLE);
	DWORD dwMode = 0;
	GetConsoleMode(consoleHandle, &dwMode);
	dwMode |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
	SetConsoleMode(consoleHandle, dwMode);
	SetConsoleTitle(TEXT(title.c_str()));
}

void DisplayWindows::setupDPIAwareness()
{
	typedef HRESULT* (__stdcall* SetProcessDpiAwarenessFunc)(PROCESS_DPI_AWARENESS);

	HMODULE shCore = LoadLibraryA("Shcore.dll");
	if (shCore)
	{
		SetProcessDpiAwarenessFunc setProcessDpiAwareness =
			(SetProcessDpiAwarenessFunc)GetProcAddress(shCore, "SetProcessDpiAwareness");

		if (setProcessDpiAwareness != nullptr)
		{
			setProcessDpiAwareness(PROCESS_PER_MONITOR_DPI_AWARE);
		}

		FreeLibrary(shCore);
	}
}
#endif