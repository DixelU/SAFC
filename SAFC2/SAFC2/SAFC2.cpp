#pragma comment(lib, "AppCore.lib")
#pragma comment(lib, "Ultralight.lib")
#pragma comment(lib, "UltralightCore.lib")
#pragma comment(lib, "WebCore.lib")

#include <Ultralight/Ultralight.h>
#include <JavaScriptCore/JavaScript.h>
#include <AppCore/AppCore.h>

#include "MRWF.h"

#include  <io.h>
#include  <stdio.h>
#include  <stdlib.h>

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>

#include <Windows.h>

using namespace ultralight;

const std::string index_file = "index.htm";
std::string index_file_content;


class MyApp : 
    public AppListener,
    public WindowListener,
    public LoadListener,
    public ViewListener {
public:
    MyApp() {
        app_ = App::Create();
        window_ = Window::Create(app_->main_monitor(), 900, 600, false, kWindowFlags_Titled | kWindowFlags_Resizable | kWindowFlags_Maximizable);
        app_->set_window(*window_.get());
        overlay_ = Overlay::Create(*window_.get(), 1, 1, 0, 0);
        OnResize(window_->width(), window_->height());
        overlay_->view()->LoadURL(("file:///" + index_file).c_str());
        app_->set_listener(this);
        window_->set_listener(this);
        overlay_->view()->set_load_listener(this);
        overlay_->view()->set_view_listener(this);
    }
    virtual ~MyApp() {

    }

    virtual void Run() {
        app_->Run();
    }

    virtual void OnUpdate() {

    }
    
    virtual void OnClose() {

    }

    virtual void OnResize(uint32_t width, uint32_t height) {

        overlay_->Resize(width, height);
    }

    virtual void OnFinishLoading(ultralight::View* caller,
        uint64_t frame_id,
        bool is_main_frame,
        const String& url) {

    }


    virtual void OnDOMReady(ultralight::View* caller,
        uint64_t frame_id,
        bool is_main_frame,
        const String& url) {

    }


    virtual void OnChangeCursor(ultralight::View* caller,
        Cursor cursor) {

        window_->SetCursor(cursor);
    }

    virtual void OnChangeTitle(ultralight::View* caller,
        const String& title) {

        window_->SetTitle(title.utf8().data());
    }

protected:
    RefPtr<App> app_;
    RefPtr<Window> window_;
    RefPtr<Overlay> overlay_;
};


int main() {
    if (_access(index_file.c_str(), 0)) {
        std::cout << "Unable to locate index file";
        exit(0);
    }

    MyApp app;
    app.Run();

    return 0;
}
