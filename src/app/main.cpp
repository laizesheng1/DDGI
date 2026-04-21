#include "app/DDGISample.h"

#if defined(_WIN32)
namespace {
app::DDGISample* gApp = nullptr;

LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    if (gApp != nullptr) {
        gApp->displayWindows.handleMessages(hWnd, uMsg, wParam, lParam);
    }
    return DefWindowProc(hWnd, uMsg, wParam, lParam);
}
} // namespace

int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int)
{
    app::DDGISample app;
    gApp = &app;

    app.displayWindows.setupWindow(hInstance, WndProc);
    if (!app.initVulkan()) {
        return EXIT_FAILURE;
    }

    app.prepare();
    app.renderLoop();
    gApp = nullptr;
    return EXIT_SUCCESS;
}
#else
int main()
{
    app::DDGISample app;
    if (!app.initVulkan()) {
        return EXIT_FAILURE;
    }
    app.prepare();
    app.renderLoop();
    return EXIT_SUCCESS;
}
#endif
