#pragma comment(lib, "AppCore.lib")
#pragma comment(lib, "Ultralight.lib")
#pragma comment(lib, "UltralightCore.lib")
#pragma comment(lib, "WebCore.lib")

#include <Ultralight/Ultralight.h>
#include <AppCore/App.h>
#include <AppCore/Window.h>
#include <AppCore/Overlay.h>

#include "MRWF.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>

using namespace ultralight;

constexpr const wchar_t* index_file = L"index.htm";
std::string index_file_content;

JSValueRef MyCallback(JSContextRef ctx, JSObjectRef function,
    JSObjectRef thisObject, size_t argumentCount,
    const JSValueRef arguments[], JSValueRef* exception) {

    // Handle JavaScript arguments in our C callback here...

    // Optionally return a value back to JavaScript

    return JSValueMakeNull(ctx);
}

int main() {
    if (_waccess(index_file, 0)) {
        std::cout << "Unable to locate index file";
        exit(0);
    }

    std::ifstream inp(index_file);
    std::stringstream strstr;
    strstr << inp.rdbuf();
    index_file_content = strstr.str();

    //std::cout << index_file_content << std::endl;

    Ref<App> app = App::Create();

    Ref<Window> window = Window::Create(app->main_monitor(), 900, 600, false,
        kWindowFlags_Titled | kWindowFlags_Resizable | kWindowFlags_Maximizable);

    window->SetTitle("SAFC2");

    app->set_window(window);

    Ref<Overlay> overlay = Overlay::Create(window, window->width(),
        window->height(), 0, 0);

    overlay->view()->LoadHTML(index_file_content.c_str());

    app->Run();

    return 0;
}
