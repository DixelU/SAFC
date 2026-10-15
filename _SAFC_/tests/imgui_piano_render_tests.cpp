#define NOMINMAX
#include <Windows.h>
#include <GLFW/glfw3.h>
#include "../SAFC_InnerModules/simple_player.h"

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
constexpr int width = 1248, height = 680;
constexpr unsigned char background[] = {8, 16, 24};

void check_note_borders()
{
    auto player = std::make_unique<simple_player>();
    auto data = std::make_unique<simple_player::draw_data>();
    data->enable_simulated_lag = false;
    data->scroll_window_us = 1'000'000;
    data->reinit(width, 580, 100, 56, 0);
    data->move(0, 390);

    auto& notes = player->get_visuals();
    for (int key = 0; key < 128; key += 7)
        for (int note = 0; note < 8; ++note)
        {
            notes.push_note_on(key, note * 100'000, key, 0, 90);
            notes.push_note_off(key, note * 100'000 + 180'000, key, 0);
        }

    glViewport(0, 0, width, height);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, width, 0, height, -1, 1);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glClearColor(background[0] / 255.f, background[1] / 255.f, background[2] / 255.f, 1.f);

    std::vector<unsigned char> pixels(width * height * 4);
    // The GPU path keeps the notes in buffer objects and scrolls them with the
    // modelview matrix; the CPU path rebuilds client arrays per frame and is
    // the one the overlap modes apply to. Both must stay inside the rectangles.
    for (bool gpu : {true, false})
    for (auto overlap_mode : {0, 1, 255})
    {
        data->gpu_notes_enabled = gpu;
        data->remove_overlaps = static_cast<std::uint8_t>(overlap_mode);
        // Exercise reused client arrays, both key colors, and overlap splitting.
        for (int frame = 0; frame < 40; ++frame)
        {
            glClear(GL_COLOR_BUFFER_BIT);
            player->draw_at(*data, frame * 17'000);
        }
        glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
        if (glGetError() != GL_NO_ERROR)
            throw std::runtime_error("OpenGL error during piano rendering");

        const std::string variant = std::string(gpu ? "GPU" : "CPU") + " path, overlap mode " +
            std::to_string(overlap_mode);

        // At 663 ms the last note ends at y=226. Diagonals from the broken
        // x86 separate-array path extended far above every note rectangle.
        for (int y = 240; y < height; ++y)
            for (int x = 0; x < width; ++x)
                for (int channel = 0; channel < 3; ++channel)
                    if (pixels[(y * width + x) * 4 + channel] != background[channel])
                        throw std::runtime_error("Note border escaped its rectangle, " + variant);

        // Also require a note fill, so an empty or skipped render cannot pass.
        if (pixels[(180 * width + 8) * 4] < 100)
            throw std::runtime_error("Expected visible note fill, " + variant);
    }
    if (!data->gl_buffers.available)
        std::cout << "Buffer objects unavailable in this context; GPU path fell back to CPU\n";
}
}

int main()
{
    try
    {
        if (!glfwInit()) throw std::runtime_error("GLFW initialization failed");
        struct glfw_guard { ~glfw_guard() { glfwTerminate(); } } glfw_lifetime;
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
        glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
        glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);
        auto* window = glfwCreateWindow(width, height, "Piano render test", nullptr, nullptr);
        if (!window) throw std::runtime_error("OpenGL compatibility context creation failed");
        struct window_guard
        {
            GLFWwindow* window;
            ~window_guard() { glfwDestroyWindow(window); }
        } window_lifetime{window};
        glfwMakeContextCurrent(window);
        std::cout << glGetString(GL_RENDERER) << " / " << glGetString(GL_VERSION) << '\n';
        check_note_borders();
        std::cout << "Piano border rendering passed\n";
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
