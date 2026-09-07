#include "cli.h"
#include "project_session.h"
#include "../JSON/JSON.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

// Standalone core diagnostics for this test executable.
void throw_alert_error(std::string&& text) { std::cerr << text << '\n'; }
void throw_alert_warning(std::string&& text) { std::cerr << text << '\n'; }

namespace
{
void require(bool value, const char* message) { if (!value) throw std::runtime_error(message); }

std::string quote(const std::filesystem::path& path)
{
    const auto utf8 = path.u8string();
    std::string result = "\"";
    for (const auto c : utf8) { if (c == '\\' || c == '"') result += '\\'; result += static_cast<char>(c); }
    return result + "\"";
}

void write_midi(const std::filesystem::path& path)
{
    const unsigned char midi[] = {'M','T','h','d',0,0,0,6,0,0,0,1,1,0xe0,'M','T','r','k',0,0,0,13,0,0x90,60,100,0x83,0x60,0x80,60,0,0,0xff,0x2f,0};
    std::ofstream out(path, std::ios::binary); out.write(reinterpret_cast<const char*>(midi), sizeof(midi));
}

void write_config(const std::filesystem::path& path, const std::string& text)
{
    std::ofstream out(path, std::ios::binary); out << text;
}

void reject(const std::filesystem::path& config, const std::string& text, const char* expected)
{
    using namespace safc::imgui_ui;
    write_config(config, text);
    project_session project;
    bool rejected = false;
    try { load_cli_config(config.wstring(), project, false); }
    catch (const std::exception& error)
    {
        rejected = true;
        if (std::string(error.what()).find(expected) == std::string::npos)
            throw std::runtime_error(std::string("Unexpected validation error: ") + error.what());
    }
    require(rejected, "Invalid config was accepted.");
    require(project.data.files.empty(), "Invalid config partially mutated the project.");
}
}

int main(int argc, char** argv)
{
    using namespace safc::imgui_ui;
    try
    {
        const auto directory = std::filesystem::absolute(argc > 1 ? argv[1] : "cli-regression-output");
        std::filesystem::create_directories(directory);
        const auto midi = directory / L"\x041C\x0443\x0437\x044B\x043A\x0430.mid";
        write_midi(midi);
        const auto config = directory / L"\x043A\x043E\x043D\x0444\x0438\x0433.json";
        const auto output = directory / "merged";
        const auto filename = "\"filename\":" + quote(midi);
        const auto entry = "{" + filename + "}";
        const auto save = "\"save_to\":" + quote(output);

        write_config(config, "{\"global_ppq_override\":960,\"global_tempo_override\":120,\"global_offset_override\":27," + save
            + ",\"files\":[{" + filename + ",\"ppq_override\":240,\"tempo_override\":90,\"offset\":9007199254740993,"
            + "\"piano_only\":false,\"remove_empty_tracks\":true,\"allow_sysex\":true},{" + filename + "}]}");
        {
            project_session project; load_cli_config(config.wstring(), project, false);
            require(project.data.files.size() == 2, "UTF-8 paths or duplicate MIDI entries failed to load.");
            require(project.data.global_ppqn == 960 && project.data.files[0].new_ppqn == 240 && project.data.files[1].new_ppqn == 960, "Global/per-file PPQN precedence changed.");
            require(project.data.files[0].new_tempo == 90 && project.data.files[1].new_tempo == 120, "Global/per-file tempo precedence changed.");
            require(project.data.files[0].offset_ticks == 9007199254740993LL && project.data.files[1].offset_ticks == 27, "Tick integer precision or offset precedence was lost.");
            require(!(project.data.files[0].bool_settings & _BoolSettings::all_instruments_to_piano), "remove_empty_tracks incorrectly enabled piano-only.");
            require(project.data.files[0].bool_settings & _BoolSettings::remove_empty_tracks, "remove_empty_tracks flag was not applied.");
            require(project.data.files[0].allow_sysex, "Boolean switch was not applied.");
            require(project.data.save_path == output.wstring() + L".mid", "Missing extension was not appended exactly once.");
        }
        write_config(config, "{\"global_offset\":-3,\"save_to\":" + quote(directory / "already.MID") + ",\"files\":[" + entry + "]}");
        {
            project_session project; load_cli_config(config.wstring(), project, false);
            require(project.data.files[0].offset_ticks == -3, "Legacy global_offset alias failed.");
            require(std::filesystem::path(project.data.save_path).filename() == L"already.MID", "Existing uppercase MID extension was duplicated.");
        }
        reject(config, "[]", "object");
        reject(config, "{}", "files");
        reject(config, "{\"files\":[]}", "nonempty");
        reject(config, "{\"files\":[42]}", "/files/0");
        reject(config, "{\"files\":[{}]}", "filename");
        reject(config, "{\"global_ppq_override\":0,\"files\":[" + entry + "]}", "1..65535");
        reject(config, "{\"global_ppq_override\":480.5,\"files\":[" + entry + "]}", "exact integer");
        reject(config, "{\"global_offset_override\":2147483648,\"files\":[" + entry + "]}", "2147483647");
        reject(config, "{\"global_offset_override\":2,\"global_offset\":3,\"files\":[" + entry + "]}", "conflict");
        reject(config, "{\"files\":[{" + filename + ",\"ignore_notes\":1}]}", "true or false");
        reject(config, "{\"files\":[{" + filename + ",\"offset\":9223372036854775808}]}", "64-bit range");
        reject(config, "{\"files\":[{" + filename + ",\"selection_start\":-1}]}", "within");
        reject(config, "{\"files\":[{" + filename + ",\"selection_length\":-2}]}", "within");
        reject(config, "{\"files\":[{" + filename + ",\"selection_start\":9223372036854775807,\"selection_length\":1}]}", "selection end");
        reject(config, "{\"files\":[{\"filename\":" + quote(directory / "missing.mid") + ",\"ignore_notes\":true}," + entry + "]}", "/files/0/filename");
        reject(config, "{\"save_to\":" + quote(midi) + ",\"files\":[" + entry + "]}", "overwrite an input");
        reject(config, "{\"global_tempo_override\":1e999999999,\"files\":[" + entry + "]}", "finite number");
        reject(config, "{\"files\":[" + entry + "]} trailing", "Unexpected");
        reject(config, std::string("{\"files\":[\"") + char(0xFF) + "\"]}", "UTF-8");

        const auto merge_config = directory / "merge.json";
        write_config(merge_config, "{" + save + ",\"files\":[{" + filename + ",\"piano_only\":false,\"remove_remnants\":true}]}");
        require(run_cli(merge_config.wstring(), false) == 0, "CLI production merge failed.");
        const auto merged = std::filesystem::path(output.wstring() + L".mid");
        std::ifstream merged_input(merged, std::ios::binary);
        char magic[4]{}; merged_input.read(magic, 4);
        require(std::string(magic, 4) == "MThd" && std::filesystem::file_size(merged) > 22, "CLI did not write a valid MIDI output.");
        require(run_cli(L"--help", false) == 0, "Help alias failed.");
        require(run_cli((directory / "absent.json").wstring(), false) != 0, "Missing config returned success.");
        std::cout << "CLI regression passed: Unicode, defaults bypass, all-or-nothing validation, integer precision, per-file precedence, aliases, extension, flags, and actual MIDI merge.\n";
        return 0;
    }
    catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
