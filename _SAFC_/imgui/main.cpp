#define NOMINMAX
#define GLFW_INCLUDE_NONE
#define GLFW_EXPOSE_NATIVE_WIN32
#include <Windows.h>
#include <commdlg.h>
#include <shellapi.h>
#include <GL/gl.h>
#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include "folded_theme.h"
#include "playback_session.h"
#include "project_panel.h"
#include "editor_panel.h"
#include "analysis_panel.h"
#include "video_export_panel.h"
#include "preferences.h"
#include "cli.h"
#include "widgets.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cwctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace ui = safc::imgui_ui;
namespace
{
std::string utf8(const std::wstring& text)
{
    if (text.empty()) return {};
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string result(size, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), size, nullptr, nullptr);
    return result;
}

std::wstring wide(const char* text)
{
    const int size = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, nullptr, 0);
    if (!size) throw std::runtime_error("Invalid UTF-8 filename");
    std::wstring result(size, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, -1, result.data(), size);
    result.pop_back();
    return result;
}

std::vector<std::wstring> file_dialog(GLFWwindow* window, const wchar_t* filter,
    bool save = false, const std::wstring& initial = {}, const wchar_t* extension = nullptr, bool multiple = false)
{
    std::vector<wchar_t> path(262144);
    if (!initial.empty()) wcsncpy_s(path.data(), path.size(), initial.c_str(), _TRUNCATE);
    OPENFILENAMEW dialog{sizeof(dialog)};
    dialog.hwndOwner = glfwGetWin32Window(window);
    dialog.lpstrFile = path.data();
    dialog.nMaxFile = static_cast<DWORD>(path.size());
    dialog.lpstrFilter = filter;
    dialog.lpstrDefExt = extension;
    dialog.Flags = OFN_EXPLORER | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR |
        (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST) | (multiple ? OFN_ALLOWMULTISELECT : 0);
    if (save ? GetSaveFileNameW(&dialog) : GetOpenFileNameW(&dialog))
    {
        std::vector<std::wstring> result;
        const std::filesystem::path first(path.data());
        const auto* next = path.data() + wcslen(path.data()) + 1;
        if (!multiple || !*next) result.push_back(first.wstring());
        else while (*next) { result.push_back((first / next).wstring()); next += wcslen(next) + 1; }
        return result;
    }
    if (const auto error = CommDlgExtendedError())
        throw std::runtime_error("File dialog failed: " + std::to_string(error));
    return {};
}

ui::native_dialogs make_dialogs(GLFWwindow* window)
{
    constexpr auto midi = L"MIDI files (*.mid;*.midi)\0*.mid;*.midi\0\0";
    constexpr auto any = L"MIDI and archives\0*.mid;*.midi;*.zip;*.7z;*.rar;*.gz;*.bz2;*.xz;*.zst;*.tar\0All files\0*.*\0\0";
    constexpr auto bank = L"Sound banks (*.sf2;*.sfz)\0*.sf2;*.sfz\0\0";
    auto one = [](std::vector<std::wstring> paths) { return paths.empty() ? std::wstring{} : paths.front(); };
    ui::native_dialogs d;
    d.open_midi = [=] { return one(file_dialog(window, midi)); };
    d.open_any = [=] { return one(file_dialog(window, any)); };
    d.open_bank = [=] { return one(file_dialog(window, bank)); };
    d.add_midis = [=] { return file_dialog(window, midi, false, {}, nullptr, true); };
    d.save_midi = [=](const std::wstring& name) { return one(file_dialog(window, midi, true, name, L"mid")); };
    d.save_video = [=](const std::wstring& name) { return one(file_dialog(window, L"MPEG-4 video (*.mp4)\0*.mp4\0\0", true, name, L"mp4")); };
    d.save_data = [=](const std::wstring& name) { return one(file_dialog(window, L"Analysis data\0*.csv;*.atraw\0All files\0*.*\0\0", true, name)); };
    return d;
}

// Only the retained piano renderer needs a compatibility context. Its output is
// a texture item, so ImGui handles clipping, overlap, and window ordering.
class piano_texture
{
    using gen_t = void(APIENTRY*)(GLsizei, GLuint*);
    using bind_t = void(APIENTRY*)(GLenum, GLuint);
    using attach_t = void(APIENTRY*)(GLenum, GLenum, GLenum, GLuint, GLint);
    using check_t = GLenum(APIENTRY*)(GLenum);
    using delete_t = void(APIENTRY*)(GLsizei, const GLuint*);
    static constexpr GLenum framebuffer = 0x8D40, color_attachment = 0x8CE0, complete = 0x8CD5;
    gen_t gen = reinterpret_cast<gen_t>(glfwGetProcAddress("glGenFramebuffers"));
    bind_t bind = reinterpret_cast<bind_t>(glfwGetProcAddress("glBindFramebuffer"));
    attach_t attach = reinterpret_cast<attach_t>(glfwGetProcAddress("glFramebufferTexture2D"));
    check_t check = reinterpret_cast<check_t>(glfwGetProcAddress("glCheckFramebufferStatus"));
    delete_t destroy = reinterpret_cast<delete_t>(glfwGetProcAddress("glDeleteFramebuffers"));
    GLuint fbo{}, texture{};
    int width{}, height{};
public:
    piano_texture()
    {
        if (!gen || !bind || !attach || !check || !destroy)
            throw std::runtime_error("OpenGL framebuffer support is required");
        gen(1, &fbo);
        glGenTextures(1, &texture);
    }
    ~piano_texture()
    {
        glDeleteTextures(1, &texture);
        destroy(1, &fbo);
    }
    ImTextureID render(ui::playback_session& session, ImVec2 size, float seconds)
    {
        const auto scale = ImGui::GetIO().DisplayFramebufferScale;
        const int w = std::clamp(static_cast<int>(size.x * scale.x), 1, 4096);
        const int h = std::clamp(static_cast<int>(size.y * scale.y), 1, 4096);
        GLint old_fbo{}, old_matrix{};
        glGetIntegerv(0x8CA6 /* GL_FRAMEBUFFER_BINDING */, &old_fbo);
        glGetIntegerv(GL_MATRIX_MODE, &old_matrix);
        glPushAttrib(GL_ALL_ATTRIB_BITS);
        glPushClientAttrib(GL_CLIENT_ALL_ATTRIB_BITS);
        bind(framebuffer, fbo);
        if (width != w || height != h)
        {
            width = w; height = h;
            glBindTexture(GL_TEXTURE_2D, texture);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, 0x812F /* GL_CLAMP_TO_EDGE */);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, 0x812F);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
            attach(framebuffer, color_attachment, GL_TEXTURE_2D, texture, 0);
        }
        const bool ready = check(framebuffer) == complete;
        if (ready)
        {
            glViewport(0, 0, w, h);
            glDisable(GL_SCISSOR_TEST);
            glDisable(GL_DEPTH_TEST);
            glDisable(GL_TEXTURE_2D);
            glEnable(GL_BLEND);
            glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
            glClearColor(0.022f, 0.043f, 0.065f, 1.0f);
            glClear(GL_COLOR_BUFFER_BIT);
            glMatrixMode(GL_PROJECTION);
            glPushMatrix();
            glLoadIdentity();
            glOrtho(0, size.x, 0, size.y, -1, 1);
            glMatrixMode(GL_MODELVIEW);
            glPushMatrix();
            glLoadIdentity();
            session.draw_visuals(size.x, size.y, seconds);
            glPopMatrix();
            glMatrixMode(GL_PROJECTION);
            glPopMatrix();
        }
        bind(framebuffer, static_cast<GLuint>(old_fbo));
        glPopClientAttrib();
        glPopAttrib();
        glMatrixMode(old_matrix);
        if (!ready) throw std::runtime_error("Piano framebuffer is incomplete");
        return static_cast<ImTextureID>(texture);
    }
};

struct workspace
{
    ui::native_dialogs dialogs;
    ui::preferences_store preferences_store;
    ui::application_preferences preferences;
    ui::playback_session playback;
    ui::project_session project;
    ui::analysis_panel analysis;
    ui::editor_panel editor;
    ui::video_export_panel video;
    ui::project_panel project_view;
    std::wstring file, bank;
    std::string notice;
    syncore_preferences draft;
    float visible_seconds = 2.5f;
    float seek_position{};
    bool seek_editing{}, player_open = true, synth_open = true, reset_layout = true;
    bool focus_player{}, focus_synth{};
    bool project_open = true, editor_open = false, analysis_open = false, video_open = false, settings_open = false;
    bool simulate_lag = false, exit_prompt = false, exiting = false;
    int overlap_mode = 0;
    bool test_mode = false;
    ImVec2 player_position{}, player_size{}, limiter_center{};

    workspace(ui::native_dialogs d, bool test)
        : dialogs(std::move(d)), analysis(dialogs), editor(playback, dialogs), video(playback, dialogs),
          project_view(project, analysis, dialogs), test_mode(test)
    {
        if (!test) preferences = preferences_store.load();
        project.set_defaults(preferences);
        bank = preferences.sound_bank; draft = preferences.synth;
        playback.configure_synth(bank, draft);
        video.set_settings(preferences.video);
        const auto names = playback.device_names();
        for (size_t i = 0; i < names.size(); ++i) if (names[i] == utf8(preferences.midi_device)) playback.select_device(i);
        project_view.play = [this](std::wstring path) { open_file(std::move(path)); };
        project_view.edit = [this](std::wstring path) { editor.open_file(std::move(path)); editor_open = true; ImGui::SetWindowFocus("MIDI editor"); };
        project_view.show_analysis = [this] { analysis_open = true; ImGui::SetWindowFocus("MIDI analysis"); };
        if (test) project_open = false;
        else { player_open = synth_open = false; reset_layout = false; }
    }

    void save_preferences()
    {
        const auto& p = draft;
        if (p.sample_rate < 8000 || p.sample_rate > 192000 || p.buffer_frames < 256 || p.buffer_frames > 1048576 ||
            p.maximum_cohorts < 1 || p.maximum_cohorts > 1048576 || p.render_threads > 64 ||
            !std::isfinite(p.output_gain_db) || p.output_gain_db < -60 || p.output_gain_db > 12)
            throw std::runtime_error("Correct the SYNCore settings before saving preferences.");
        preferences.sound_bank = bank; preferences.synth = draft;
        preferences.video = video.settings();
        if (!playback.snapshot().busy) playback.configure_synth(bank, draft);
        const auto names = playback.device_names();
        const auto selected = playback.selected_device();
        if (selected < names.size()) preferences.midi_device = wide(names[selected].c_str());
        project.set_defaults(preferences);
        if (!test_mode) preferences_store.save(preferences);
        notice = "Preferences saved.";
    }

    void shutdown()
    {
        video.shutdown(); editor.shutdown(); analysis.shutdown(); project.shutdown(); playback.shutdown();
    }

    void open_file(std::wstring path, bool silent = false, bool start_paused = true)
    {
        if (path.empty()) return;
        if (playback.open(path, silent, start_paused)) { file = std::move(path); notice.clear(); player_open = true; focus_player = true; }
        else notice = "Stop the current session before opening another MIDI.";
    }
};

void render_workspace(workspace& app, piano_texture& piano, GLFWwindow* window)
{
    app.editor.poll();
    auto status = app.playback.snapshot();
    const auto display = ImGui::GetIO().DisplaySize;
    const float scale = ImGui::GetFontSize() / 17.f;
    auto* background = ImGui::GetBackgroundDrawList();
    if (app.preferences.background == 1)
        background->AddRectFilledMultiColor({0, 0}, display, IM_COL32(151, 111, 63, 255), IM_COL32(0, 120, 246, 255), IM_COL32(0, 120, 246, 255), IM_COL32(255, 135, 0, 255));
    else background->AddRectFilledMultiColor({0, 0}, display, IM_COL32(30, 42, 56, 255), IM_COL32(17, 47, 68, 255), IM_COL32(8, 25, 42, 255), IM_COL32(26, 33, 44, 255));
    background->AddLine({0, 3}, {display.x * 0.29f, 3}, IM_COL32(242, 152, 49, 255), 3.f);
    background->AddLine({display.x * 0.29f, 3}, {display.x * 0.29f + 9, 12}, IM_COL32(242, 152, 49, 255), 3.f);
    background->AddText({24 * scale, 27 * scale}, IM_COL32(224, 239, 247, 255), "SAFC   /   MIDI WORKSTATION");

    ImGui::SetNextWindowPos({24 * scale, 48 * scale});
    ImGui::SetNextWindowSize({display.x - 48 * scale, 32 * scale});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowMinSize, {0, 0});
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, {0, 0});
    ImGui::Begin("Workspace navigation", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    ImGui::PopStyleVar(2);
    if (ImGui::Button("Project")) { app.project_open = true; ImGui::SetWindowFocus("SAFC project"); }
    ImGui::SameLine();
    if (ImGui::Button("Editor")) { app.editor_open = true; ImGui::SetWindowFocus("MIDI editor"); }
    ImGui::SameLine();
    if (ImGui::Button("Analysis")) { app.analysis_open = true; ImGui::SetWindowFocus("MIDI analysis"); }
    ImGui::SameLine();
    if (ImGui::Button("Player")) { app.player_open = true; app.focus_player = true; }
    ImGui::SameLine();
    if (ImGui::Button("SYNCore")) { app.synth_open = true; app.focus_synth = true; }
    ImGui::SameLine();
    if (ImGui::Button("Video export")) { app.video_open = true; ImGui::SetWindowFocus("Player video render"); }
    ImGui::SameLine();
    if (ImGui::Button("Settings")) { app.settings_open = true; ImGui::SetWindowFocus("Application settings"); }
    ImGui::SameLine();
    if (ImGui::Button("Reset layout"))
    {
        app.reset_layout = true; app.player_open = app.synth_open = true;
        ImGui::SetWindowPos("SAFC project", {24, 86}); ImGui::SetWindowSize("SAFC project", {1010, 700});
        ImGui::SetWindowPos("MIDI editor", {80, 86}); ImGui::SetWindowSize("MIDI editor", {1180, 700});
    }
    ImGui::End();

    const float gap = 22 * scale, top = 82 * scale;
    const float available = display.x - 3 * gap;
    const bool split = available >= 780.f * scale;
    if (app.reset_layout && !split) app.focus_player = true;
    const float left_width = split ? std::clamp(available * .67f, 450.f * scale, available - 330.f * scale) : display.x - 2 * gap;
    if (app.player_open)
    {
        if (app.reset_layout || !app.test_mode)
        {
            const auto condition = app.reset_layout ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
            ImGui::SetNextWindowPos({gap, top}, condition);
            ImGui::SetNextWindowSize({left_width, std::max(420.f * scale, display.y - top - 54 * scale)}, condition);
        }
        ImGui::SetNextWindowSizeConstraints({430 * scale, 340 * scale}, {FLT_MAX, FLT_MAX});
        if (app.focus_player) { ImGui::SetNextWindowFocus(); app.focus_player = false; }
        if (ui::begin_folded_window("Simple MIDI player", &app.player_open))
        {
            app.player_position = ui::folded_window_position(); app.player_size = ui::folded_window_size();
            ImGui::BeginDisabled(status.busy);
            if (ImGui::Button("Open MIDI..."))
            {
                try { app.open_file(app.dialogs.open_any()); }
                catch (const std::exception& error) { app.notice = error.what(); }
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            const auto playback_path = app.playback.current_path();
            const std::string source_label = playback_path.empty()
                ? (app.playback.current_source() ? "Editor snapshot" : "Open a MIDI or archive to begin")
                : utf8(std::filesystem::path(playback_path).filename().wstring());
            ImGui::TextDisabled("%s", source_label.c_str());
            ImGui::Spacing();
            ImGui::BeginDisabled((app.file.empty() && !app.playback.current_source()) || status.stopping || (status.busy && !status.playing));
            if (ImGui::Button(status.playing && !status.paused ? "Pause" : "Play", {82 * scale, 0}))
            {
                if (status.busy) app.playback.toggle_pause();
                else if (!app.playback.restart()) app.open_file(app.file, false, false);
            }
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::BeginDisabled(!status.busy || status.stopping);
            if (ImGui::Button("Stop", {72 * scale, 0})) app.playback.stop();
            ImGui::EndDisabled();
            ImGui::SameLine();
            ImGui::Text("%02llu:%02llu.%02llu  /  %02llu:%02llu", status.position_us / 60000000, status.position_us / 1000000 % 60, status.position_us / 10000 % 100, status.duration_us / 60000000, status.duration_us / 1000000 % 60);
            ImGui::SameLine();
            if (status.playing && !status.paused && status.lead_in_us && !status.stopping)
                ImGui::TextDisabled("  Starts in %.1fs", status.lead_in_us / 1000000.0);
            else ImGui::TextDisabled("  %s", status.stopping ? "Stopping..." : status.seeking ? "Seeking..." : status.paused ? "Paused" : status.playing ? "Playing" : status.busy ? "Opening..." : "Ready");
            if (!app.seek_editing) app.seek_position = status.duration_us ? static_cast<float>(static_cast<double>(status.position_us) / status.duration_us) : 0.f;
            ImGui::BeginDisabled(!status.playing || status.stopping || status.seeking);
            ImGui::SetNextItemWidth(-1);
            ImGui::SliderFloat("##timeline", &app.seek_position, 0.f, 1.f, "");
            app.seek_editing = ImGui::IsItemActive();
            if (ImGui::IsItemDeactivatedAfterEdit()) app.playback.seek(app.seek_position);
            ImGui::EndDisabled();

            auto size = ImGui::GetContentRegionAvail();
            size.y = std::max(100.f * scale, size.y - 83 * scale);
            size.x = std::max(1.f, size.x);
            ImGui::Image(piano.render(app.playback, size, app.visible_seconds), size, {0, 1}, {1, 0});
            ImGui::SetNextItemWidth(190 * scale);
            float viewport_power = std::log2(app.visible_seconds * 1000000.f);
            char viewport_label[64]; snprintf(viewport_label, sizeof(viewport_label), "%.6g s", app.visible_seconds);
            if (ImGui::SliderFloat("Visible seconds", &viewport_power, 0.f, 30.f, viewport_label)) app.visible_seconds = std::exp2(viewport_power) / 1000000.f;
            ImGui::SameLine();
            bool visuals_changed = ImGui::Checkbox("Simulate lag", &app.simulate_lag);
            ImGui::SameLine(); ImGui::SetNextItemWidth(120 * scale);
            visuals_changed |= ImGui::Combo("Overlap", &app.overlap_mode, "Naive removal\0Realtime removal\0Draw all\0");
            if (visuals_changed) app.playback.set_visual_options(app.simulate_lag, static_cast<std::uint8_t>(app.overlap_mode));
            if (!status.error.empty()) ImGui::TextWrapped("%s", status.error.c_str());
            else if (!app.notice.empty()) ImGui::TextWrapped("%s", app.notice.c_str());
            else ImGui::TextDisabled("%s", status.message.c_str());
        }
        ui::end_folded_window();
    }

    if (app.synth_open)
    {
        if (app.reset_layout || !app.test_mode)
        {
            const auto condition = app.reset_layout ? ImGuiCond_Always : ImGuiCond_FirstUseEver;
            ImGui::SetNextWindowPos({split ? 2 * gap + left_width : gap, top}, condition);
            ImGui::SetNextWindowSize({split ? available - left_width : display.x - 2 * gap, std::max(420.f * scale, display.y - top - 54 * scale)}, condition);
        }
        ImGui::SetNextWindowSizeConstraints({330 * scale, 410 * scale}, {FLT_MAX, FLT_MAX});
        if (app.focus_synth) { ImGui::SetNextWindowFocus(); app.focus_synth = false; }
        if (ui::begin_folded_window("Output and SYNCore setup", &app.synth_open))
        {
            ImGui::SeparatorText("MIDI output");
            const auto names = app.playback.device_names();
            const auto selected = app.playback.selected_device();
            ImGui::BeginDisabled(status.busy);
            ImGui::SetNextItemWidth(-1);
            if (ui::begin_literal_combo("##output", selected < names.size() ? names[selected].c_str() : "No output available"))
            {
                for (size_t i = 0; i < names.size(); ++i)
                {
                    ImGui::PushID(static_cast<int>(i));
                    if (ui::literal_selectable("##device", names[i], i == selected)) app.playback.select_device(i);
                    ImGui::PopID();
                }
                ImGui::EndCombo();
            }
            ImGui::Spacing();
            ImGui::SeparatorText("Sound bank");
            ImGui::BeginDisabled(!app.playback.syncore_available());
            ImGui::TextWrapped("%s", app.bank.empty() ? "Built-in sine" : utf8(std::filesystem::path(app.bank).filename().wstring()).c_str());
            if (ImGui::Button("Choose SF2 / SFZ..."))
            {
                try { if (auto path = app.dialogs.open_bank(); !path.empty()) app.bank = std::move(path); }
                catch (const std::exception& error) { app.notice = error.what(); }
            }
            ImGui::SameLine();
            if (ImGui::Button("Use sine")) app.bank.clear();
            ImGui::Spacing();
            ImGui::SeparatorText("Synthesis");
            if (ImGui::BeginTable("synth_fields", 2, ImGuiTableFlags_SizingStretchProp))
            {
                ImGui::TableSetupColumn("Label", ImGuiTableColumnFlags_WidthStretch, 0.52f);
                ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.48f);
                auto row = [](const char* label, std::uint32_t& value)
                {
                    ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted(label);
                    ImGui::TableNextColumn(); ImGui::SetNextItemWidth(-1); ImGui::PushID(label);
                    ImGui::InputScalar("##value", ImGuiDataType_U32, &value); ImGui::PopID();
                };
                row("Sample rate", app.draft.sample_rate);
                row("Buffer frames", app.draft.buffer_frames);
                row("Cohort ceiling", app.draft.maximum_cohorts);
                row("Render threads", app.draft.render_threads);
                ImGui::TableNextRow(); ImGui::TableNextColumn(); ImGui::AlignTextToFramePadding(); ImGui::TextUnformatted("Gain (dB)");
                ImGui::TableNextColumn(); ImGui::SetNextItemWidth(-1);
                ImGui::InputDouble("##gain", &app.draft.output_gain_db, 0, 0, "%.2f");
                ImGui::EndTable();
            }
            ImGui::Checkbox("Limiter", &app.draft.limiter_enabled);
            const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
            app.limiter_center = {(a.x + b.x) * .5f, (a.y + b.y) * .5f};
            constexpr const char* phases[] = {"Coherent", "Random polarity", "Analytic", "Smooth field", "Independent bins"};
            int phase = static_cast<int>(app.draft.phase_mode);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::Combo("##phase", &phase, phases, IM_ARRAYSIZE(phases))) app.draft.phase_mode = static_cast<syncore_phase_mode>(phase);
            ImGui::TextDisabled("0 render threads = automatic");
            ImGui::Spacing();
            if (ImGui::Button("Apply to next playback", {-1, 0}))
            {
                const auto& p = app.draft;
                if (p.sample_rate < 8000 || p.sample_rate > 192000 || p.buffer_frames < 256 || p.buffer_frames > 1048576 || p.maximum_cohorts < 1 || p.maximum_cohorts > 1048576 || p.render_threads > 64 || !std::isfinite(p.output_gain_db) || p.output_gain_db < -60 || p.output_gain_db > 12)
                    app.notice = "SYNCore ranges: rate 8000-192000; buffer 256-1048576; cohorts 1-1048576; threads 0-64; gain -60 to +12 dB.";
                else { app.playback.configure_synth(app.bank, p); app.notice = "SYNCore settings applied to the next playback."; }
            }
            ImGui::EndDisabled();
            ImGui::EndDisabled();
            ImGui::Spacing();
            ImGui::TextWrapped("Stop playback before changing output settings.");
            if (ImGui::Button("Save preferences"))
            { try { app.save_preferences(); } catch (const std::exception& e) { app.notice = e.what(); } }
            if (!app.notice.empty()) ImGui::TextWrapped("%s", app.notice.c_str());
            if (!app.playback.syncore_available()) ImGui::TextWrapped("This build does not include SYNCore.");
        }
        ui::end_folded_window();
    }
    app.reset_layout = false;
    if (app.project_open) app.project_view.draw(&app.project_open); else app.project.poll();
    if (app.editor_open) app.editor.draw(&app.editor_open);
    if (app.analysis_open) app.analysis.draw(&app.analysis_open);
    if (app.video_open) app.video.draw(&app.video_open);
    if (status.waiting_for_member)
    {
        ImGui::SetNextWindowSize({650, 390}, ImGuiCond_FirstUseEver);
        bool choose_open = true;
        if (ui::begin_folded_window("Choose archive member", &choose_open))
        {
            ImGui::Text("Archive layer %u", status.archive_layer);
            ImGui::TextWrapped("Choose the MIDI or nested archive to open:");
            for (size_t i = 0; i < status.archive_members.size(); ++i)
            { ImGui::PushID(static_cast<int>(i)); if (ui::literal_selectable("##member", status.archive_members[i])) app.playback.choose_archive_member(i); ImGui::PopID(); }
            if (ImGui::Button("Cancel")) app.playback.stop();
        }
        ui::end_folded_window();
        if (!choose_open) app.playback.stop();
    }
    if (app.settings_open)
    {
        ImGui::SetNextWindowSize({530, 700}, ImGuiCond_FirstUseEver);
        if (ui::begin_folded_window("Application settings", &app.settings_open))
        {
            ImGui::SeparatorText("Defaults for new MIDIs");
            ui::draw_processing_flags(app.preferences.processing_flags);
            ImGui::Checkbox("Split by channel", &app.preferences.split_channels);
            ImGui::Checkbox("Collapse tracks", &app.preferences.collapse_tracks);
            ImGui::Checkbox("Apply offset after PPQN", &app.preferences.apply_offset_after);
            ImGui::Checkbox("In-place merge", &app.preferences.inplace_merge);
            ImGui::Checkbox("Running-status compression", &app.preferences.rsb_compression);
            ImGui::Checkbox("Allow SysEx", &app.preferences.allow_sysex);
            int threads = app.preferences.processing_threads;
            if (ImGui::SliderInt("Processing threads", &threads, 1, 64)) app.preferences.processing_threads = static_cast<std::uint16_t>(threads);
            ImGui::SeparatorText("Appearance");
            ImGui::Combo("Background", &app.preferences.background, "Dark blue\0Classic amber / blue\0");
            if (ImGui::SliderFloat("Interface scale", &app.preferences.ui_scale, .8f, 1.75f, "%.2f"))
                ImGui::GetIO().FontGlobalScale = app.preferences.ui_scale;
            ImGui::TextWrapped("Window positions and sizes are saved automatically. Hold Ctrl to select several project files.");
            try
            {
                if (ImGui::Button("Apply and save")) app.save_preferences();
                ImGui::SameLine(); ImGui::BeginDisabled(app.project.merging());
                if (ImGui::Button("Apply defaults to all MIDIs")) app.project.set_defaults(app.preferences, true);
                ImGui::EndDisabled();
            }
            catch (const std::exception& e) { app.notice = e.what(); }
            ImGui::TextWrapped("%s", app.notice.c_str());
            ImGui::SeparatorText("SAFC");
            ImGui::TextWrapped("MIDI processing, editing, analysis, playback and video export. Dear ImGui frontend with the original folded panel design.");
            if (ImGui::Button("Project / releases")) ShellExecuteW(glfwGetWin32Window(window), L"open", L"https://github.com/DixelU/SAFC/releases", nullptr, nullptr, SW_SHOWNORMAL);
        }
        ui::end_folded_window();
    }
    background->AddText({24 * scale, display.y - 29 * scale}, IM_COL32(136, 166, 187, 255), "SAFC  /  ImGui   |   Drag headers to move panels; drag corners to resize.");
}

void write_capture(const std::filesystem::path& path, int w, int h)
{
    std::vector<unsigned char> pixels(static_cast<size_t>(w) * h * 4);
    glPixelStorei(GL_PACK_ALIGNMENT, 1);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    for (size_t i = 0; i < pixels.size(); i += 4) std::swap(pixels[i], pixels[i + 2]);
    BITMAPFILEHEADER file{};
    BITMAPINFOHEADER info{};
    file.bfType = 0x4D42;
    file.bfOffBits = sizeof(file) + sizeof(info);
    file.bfSize = file.bfOffBits + static_cast<DWORD>(pixels.size());
    info.biSize = sizeof(info); info.biWidth = w; info.biHeight = h;
    info.biPlanes = 1; info.biBitCount = 32; info.biCompression = BI_RGB;
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char*>(&file), sizeof(file));
    out.write(reinterpret_cast<const char*>(&info), sizeof(info));
    out.write(reinterpret_cast<const char*>(pixels.data()), pixels.size());
    if (!out) throw std::runtime_error("Cannot write preview capture");
}

std::filesystem::path write_smoke_midi(const std::filesystem::path& output)
{
    const auto path = output.parent_path() / "imgui-smoke.mid";
    std::vector<unsigned char> track;
    for (int i = 0; i < 80; ++i)
    {
        const auto key = static_cast<unsigned char>(48 + (i * 7) % 36);
        track.insert(track.end(), {0, 0x90, key, 90, 120, 0x80, key, 0});
    }
    track.insert(track.end(), {0, 0xff, 0x2f, 0});
    std::ofstream file(path, std::ios::binary);
    const unsigned char header[] = {'M','T','h','d',0,0,0,6,0,0,0,1,1,0xe0,'M','T','r','k'};
    file.write(reinterpret_cast<const char*>(header), sizeof(header));
    const auto size = static_cast<std::uint32_t>(track.size());
    for (int shift : {24, 16, 8, 0}) file.put(static_cast<char>(size >> shift));
    file.write(reinterpret_cast<const char*>(track.data()), track.size());
    if (!file) throw std::runtime_error("Cannot create smoke MIDI");
    return path;
}

// A bounded silent integration run exercises the real parser and transport,
// standard ImGui input, custom header movement, GL composition, and teardown.
class smoke_run
{
    using clock = std::chrono::steady_clock;
    clock::time_point started = clock::now();
    int stage{}, frame{};
    std::filesystem::path midi;
    ImVec2 initial_position{}, initial_size{};
    bool limiter{};
public:
    explicit smoke_run(const std::filesystem::path& output) : midi(write_smoke_midi(output)) {}
    bool capture{}, done{};
    void tick(workspace& app)
    {
        if (clock::now() - started > std::chrono::seconds(25)) throw std::runtime_error("ImGui smoke timeout at stage " + std::to_string(stage));
        auto& io = ImGui::GetIO();
        const auto status = app.playback.snapshot();
        if (!status.error.empty()) throw std::runtime_error(status.error);
        ++frame;
        switch (stage)
        {
        case 0:
            if (frame < 3) break;
            initial_position = app.player_position;
            io.AddMousePosEvent(initial_position.x + 150, initial_position.y + 16);
            io.AddMouseButtonEvent(0, true); stage = 1; break;
        case 1:
            io.AddMousePosEvent(initial_position.x + 174, initial_position.y + 28); stage = 2; break;
        case 2:
            io.AddMouseButtonEvent(0, false); stage = 3; break;
        case 3:
            if (app.player_position.x < initial_position.x + 10) throw std::runtime_error("Folded header did not move the native window");
            initial_position = app.player_position; initial_size = app.player_size;
            io.AddMousePosEvent(initial_position.x + initial_size.x - 3, initial_position.y + initial_size.y - 3);
            io.AddMouseButtonEvent(0, true); stage = 20; break;
        case 20:
            io.AddMousePosEvent(initial_position.x + initial_size.x - 67, initial_position.y + initial_size.y - 27);
            stage = 21; break;
        case 21: io.AddMouseButtonEvent(0, false); stage = 22; break;
        case 22:
            if (app.player_size.x > initial_size.x - 30) throw std::runtime_error("Native window resize did not change the piano panel");
            limiter = app.draft.limiter_enabled;
            io.AddMousePosEvent(app.limiter_center.x, app.limiter_center.y);
            io.AddMouseButtonEvent(0, true); stage = 4; break;
        case 4: io.AddMouseButtonEvent(0, false); stage = 5; break;
        case 5:
            if (app.playback.syncore_available() && limiter == app.draft.limiter_enabled) throw std::runtime_error("Native ImGui checkbox did not accept input");
            app.open_file(midi.wstring(), true); stage = 6; break;
        case 6:
            if (status.playing && status.paused) { app.playback.toggle_pause(); stage = 7; }
            break;
        case 7:
            if (status.position_us > 250000) { app.playback.toggle_pause(); stage = 8; }
            break;
        case 8:
            if (status.paused) { app.playback.seek(.45); stage = 9; }
            break;
        case 9:
            if (status.paused && !status.seeking && status.position_us >= 4490000 && status.position_us <= 4510000)
                stage = 23;
            break;
        case 23: capture = true; app.playback.stop(); stage = 10; break;
        case 10:
            if (!status.busy) { app.open_file(midi.wstring(), true); app.playback.stop(); stage = 11; }
            break;
        case 11:
            if (!status.busy) { app.open_file(midi.wstring(), true); stage = 12; }
            break;
        case 12:
            if (status.playing && status.paused)
            {
                app.playback.shutdown(); done = true;
                std::cout << "PASS: ImGui input/header drag/resize, MIDI open/play/pause/seek/stop/reopen, immediate cancellation, paused shutdown, GL rendering\n";
            }
            break;
        }
    }
};

int run(bool smoke, bool workflows, const std::filesystem::path& capture_path, const std::wstring& initial_file)
{
    glfwSetErrorCallback([](int code, const char* text) { std::cerr << "GLFW " << code << ": " << text << '\n'; });
    if (!glfwInit()) throw std::runtime_error("GLFW initialization failed");
    struct glfw_guard { ~glfw_guard() { glfwTerminate(); } } glfw_lifetime;
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 3);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 3);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_COMPAT_PROFILE);
    glfwWindowHint(GLFW_SCALE_TO_MONITOR, smoke ? GLFW_FALSE : GLFW_TRUE);
    glfwWindowHint(GLFW_VISIBLE, smoke ? GLFW_FALSE : GLFW_TRUE);
    auto* window = glfwCreateWindow(1400, 850, "SAFC", nullptr, nullptr);
    if (!window) throw std::runtime_error("OpenGL 3.3 compatibility context creation failed");
    struct window_guard { GLFWwindow* w; ~window_guard() { glfwDestroyWindow(w); } } window_lifetime{window};
    glfwSetWindowSizeLimits(window, 1000, 650, GLFW_DONT_CARE, GLFW_DONT_CARE);
    glfwMakeContextCurrent(window);
    glfwSwapInterval(smoke ? 0 : 1);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    struct imgui_guard { ~imgui_guard() { ImGui::DestroyContext(); } } imgui_lifetime;
    auto& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    std::string layout_path;
    io.IniFilename = nullptr;
    if (!smoke)
    {
        wchar_t local_app_data[32768]{};
        const auto count = GetEnvironmentVariableW(L"LOCALAPPDATA", local_app_data, 32768);
        if (count && count < 32768)
        {
            const auto directory = std::filesystem::path(local_app_data) / "SAFC";
            std::filesystem::create_directories(directory);
            layout_path = utf8((directory / "imgui.ini").wstring());
            io.IniFilename = layout_path.c_str();
        }
    }
    float scale_x{}, scale_y{};
    glfwGetWindowContentScale(window, &scale_x, &scale_y);
    const float scale = smoke ? 1.f : std::clamp(scale_x, 1.f, 2.f);
    wchar_t windows_directory[MAX_PATH]{};
    GetWindowsDirectoryW(windows_directory, MAX_PATH);
    const auto font_path = std::filesystem::path(windows_directory) / "Fonts" / "segoeui.ttf";
    if (!io.Fonts->AddFontFromFileTTF(utf8(font_path.wstring()).c_str(), 17.f * scale, nullptr, io.Fonts->GetGlyphRangesCyrillic())) io.Fonts->AddFontDefault();
    ui::apply_theme(scale);
    io.FontGlobalScale = 1.f; // Font atlas already contains the requested DPI size.
    if (!ImGui_ImplGlfw_InitForOpenGL(window, true)) throw std::runtime_error("ImGui GLFW backend failed");
    struct platform_guard { ~platform_guard() { ImGui_ImplGlfw_Shutdown(); } } platform_lifetime;
    if (!ImGui_ImplOpenGL3_Init("#version 330")) throw std::runtime_error("ImGui OpenGL backend failed");
    struct renderer_guard { ~renderer_guard() { ImGui_ImplOpenGL3_Shutdown(); } } renderer_lifetime;
    workspace app(make_dialogs(window), smoke);
    io.FontGlobalScale = app.preferences.ui_scale;
    piano_texture piano;
    glfwSetWindowUserPointer(window, &app);
    glfwSetDropCallback(window, [](GLFWwindow* w, int count, const char** paths)
    {
        auto& app = *static_cast<workspace*>(glfwGetWindowUserPointer(w));
        try
        {
            std::vector<std::wstring> midis;
            for (int i = 0; i < count; ++i)
            {
                auto path = wide(paths[i]);
                auto extension = std::filesystem::path(path).extension().wstring();
                std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
                if (extension == L".mid" || extension == L".midi") midis.push_back(std::move(path));
                else if (i == 0) app.open_file(std::move(path));
            }
            if (!midis.empty()) { app.project.add_files(std::move(midis)); app.project_open = true; }
        }
        catch (const std::exception& error) { app.notice = error.what(); }
    });
    if (!initial_file.empty())
    {
        auto extension = std::filesystem::path(initial_file).extension().wstring();
        std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
        if (extension == L".mid" || extension == L".midi") app.project.add_files({initial_file});
        app.open_file(initial_file);
    }
    std::unique_ptr<smoke_run> test;
    if (smoke && !workflows) test = std::make_unique<smoke_run>(capture_path);
    int workflow_frame = 0;
    if (workflows)
    {
        const auto directory = std::filesystem::absolute(capture_path).parent_path();
        std::filesystem::create_directories(directory);
        std::string report;
        if (!app.project.run_smoke(directory.wstring(), report)) throw std::runtime_error(report);
        std::cout << "PASS: " << report << '\n';
        if (!app.editor.run_smoke(directory.wstring(), report)) throw std::runtime_error(report);
        std::cout << "PASS: " << report << '\n';
        app.analysis.open_file((directory / "editor-output.mid").wstring());
        app.project_open = true; app.player_open = app.synth_open = false;
    }
    while (!app.exiting)
    {
        glfwPollEvents();
        int width{}, height{};
        glfwGetFramebufferSize(window, &width, &height);
        if (!width || !height) { glfwWaitEventsTimeout(.1); continue; }
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        if (smoke) io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        ImGui::NewFrame();
        if (glfwWindowShouldClose(window))
        {
            glfwSetWindowShouldClose(window, GLFW_FALSE);
            if (app.editor.has_unsaved_changes() || app.editor.busy() || app.project.merging() || app.video.snapshot().busy)
                ImGui::OpenPopup("Close SAFC?");
            else app.exiting = true;
        }
        render_workspace(app, piano, window);
        if (ImGui::BeginPopupModal("Close SAFC?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::TextUnformatted("Close SAFC, discard unsaved editor changes and cancel running jobs?");
            if (ImGui::Button("Close SAFC")) { app.exiting = true; ImGui::CloseCurrentPopup(); }
            ImGui::SameLine(); if (ImGui::Button("Keep working")) ImGui::CloseCurrentPopup();
            ImGui::EndPopup();
        }
        ImGui::Render();
        glViewport(0, 0, width, height);
        glClearColor(.02f, .03f, .05f, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if (smoke)
        {
            if (const auto error = glGetError()) throw std::runtime_error("OpenGL error " + std::to_string(error));
            if (test)
            {
                test->tick(app);
                if (test->capture) { write_capture(capture_path, width, height); test->capture = false; }
                if (test->done) break;
            }
            else if (workflows)
            {
                ++workflow_frame;
                if (workflow_frame == 3)
                {
                    write_capture(capture_path, width, height);
                    app.project_open = false; app.editor_open = true;
                }
                if (workflow_frame == 6)
                {
                    write_capture(capture_path.parent_path() / "editor-workflow.bmp", width, height);
                    app.editor_open = false; app.analysis_open = true;
                }
                if (workflow_frame >= 9 && !app.analysis.busy())
                {
                    if (!app.analysis.result()) throw std::runtime_error(app.analysis.status());
                    write_capture(capture_path.parent_path() / "analysis-workflow.bmp", width, height);
                    std::cout << "PASS: complete project/editor/analysis workspace GL composition\n";
                    break;
                }
                if (workflow_frame > 3000) throw std::runtime_error("Workflow analysis timeout");
            }
        }
        glfwSwapBuffers(window);
        if (smoke) std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }
    app.shutdown();
    return 0;
}
} // namespace

int main()
{
    int argc{};
    auto** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (!argv) return 1;
    struct arguments_guard { wchar_t** p; ~arguments_guard() { LocalFree(p); } } arguments{argv};
    const bool smoke = argc >= 2 && std::wstring(argv[1]) == L"--smoke";
    const bool workflows = argc >= 2 && std::wstring(argv[1]) == L"--workflow-smoke";
    bool cli = false;
    try
    {
        if ((smoke || workflows) && argc != 3) throw std::runtime_error("Usage: SAFCImGui --smoke|--workflow-smoke <capture.bmp>");
        if (!smoke && !workflows && argc > 1)
        {
            const std::wstring argument = argv[1];
            auto extension = std::filesystem::path(argument).extension().wstring();
            std::transform(extension.begin(), extension.end(), extension.begin(), towlower);
            cli = extension == L".json" || argument == L"--help" || argument == L"/?" || argument == L"-?" || argument == L"/help";
            if (cli) return ui::run_cli(argument);
        }
        if (!smoke && !workflows)
        {
            DWORD processes[2]{};
            if (GetConsoleProcessList(processes, 2) == 1) ShowWindow(GetConsoleWindow(), SW_HIDE);
        }
        return run(smoke || workflows, workflows, smoke || workflows ? std::filesystem::path(argv[2]) : std::filesystem::path{}, !smoke && !workflows && argc > 1 ? argv[1] : L"");
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        if (!smoke && !workflows && !cli) MessageBoxA(nullptr, error.what(), "SAFC", MB_OK | MB_ICONERROR);
        return 1;
    }
}
