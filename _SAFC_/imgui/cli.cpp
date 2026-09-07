#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include "cli.h"
#include "project_session.h"
#include "../JSON/JSON.h"

#include <charconv>
#include <chrono>
#include <cmath>
#include <fstream>
#include <iostream>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <thread>

namespace safc::imgui_ui
{
namespace
{
std::string utf8(const std::wstring& value)
{
    const auto bytes = std::filesystem::path(value).u8string();
    return {bytes.begin(), bytes.end()};
}

std::wstring decode_utf8(std::string bytes)
{
    if (bytes.starts_with("\xEF\xBB\xBF")) bytes.erase(0, 3);
    if (bytes.empty() || bytes.find('\0') != std::string::npos || bytes.size() > INT_MAX)
        throw std::runtime_error("The JSON config is empty or contains an invalid NUL byte.");
    const auto length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    if (!length) throw std::runtime_error("The JSON config must contain valid UTF-8 text.");
    std::wstring wide(length, L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, bytes.data(), static_cast<int>(bytes.size()), wide.data(), length);
    return wide;
}

[[noreturn]] void invalid(const std::wstring& field, const std::string& reason)
{
    throw std::runtime_error(utf8(field) + ": " + reason);
}

// SimpleJSON stores numbers as double. Retain their source lexemes so tick
// offsets keep all signed 64-bit integer digits. The scanner also bounds
// nesting and exponents before the legacy parser's recursive/exponent loops.
struct numeric_lexemes
{
    const std::wstring& source;
    std::size_t position = 0;
    std::map<std::wstring, std::wstring> values;
    void whitespace() { while (position < source.size() && iswspace(source[position])) ++position; }
    wchar_t peek() { whitespace(); return position < source.size() ? source[position] : 0; }
    std::wstring string_token()
    {
        const auto start = position;
        if (position >= source.size() || source[position++] != L'"') throw std::runtime_error("Invalid JSON object key.");
        bool escaped = false;
        while (position < source.size())
        {
            const auto c = source[position++];
            if (!escaped && c == L'"') return source.substr(start, position - start);
            if (!escaped && c == L'\\') escaped = true;
            else escaped = false;
        }
        throw std::runtime_error("Unterminated JSON string.");
    }
    static std::wstring escaped_path(const std::wstring& key)
    {
        std::wstring result;
        for (const auto c : key) { if (c == L'~') result += L"~0"; else if (c == L'/') result += L"~1"; else result += c; }
        return result;
    }
    void scan(const std::wstring& path = {}, int depth = 0)
    {
        if (depth > 64) throw std::runtime_error("JSON nesting exceeds 64 levels.");
        const auto c = peek();
        if (c == L'{')
        {
            ++position;
            if (peek() == L'}') { ++position; return; }
            for (;;)
            {
                whitespace();
                const auto token = string_token();
                const std::unique_ptr<JSONValue> key(JSON::Parse(token.c_str()));
                if (!key || !key->IsString() || peek() != L':') throw std::runtime_error("Invalid JSON object.");
                ++position; scan(path + L"/" + escaped_path(key->AsString()), depth + 1);
                if (peek() == L'}') { ++position; return; }
                if (peek() != L',') throw std::runtime_error("Invalid JSON object separator.");
                ++position;
            }
        }
        if (c == L'[')
        {
            ++position;
            if (peek() == L']') { ++position; return; }
            for (std::size_t index = 0;; ++index)
            {
                scan(path + L"/" + std::to_wstring(index), depth + 1);
                if (peek() == L']') { ++position; return; }
                if (peek() != L',') throw std::runtime_error("Invalid JSON array separator.");
                ++position;
            }
        }
        if (c == L'"') { (void)string_token(); return; }
        const auto start = position;
        while (position < source.size() && !iswspace(source[position]) && source[position] != L','
            && source[position] != L']' && source[position] != L'}') ++position;
        if (start == position) throw std::runtime_error("Missing JSON value.");
        if (c == L'-' || (c >= L'0' && c <= L'9'))
        {
            const auto token = source.substr(start, position - start);
            wchar_t* end = nullptr;
            const auto value = std::wcstod(token.c_str(), &end);
            if (!std::isfinite(value) || !end || *end) invalid(path, "must be a finite number");
            if (const auto exponent = token.find_first_of(L"eE"); exponent != std::wstring::npos)
            {
                const auto magnitude = std::wcstol(token.c_str() + exponent + 1, nullptr, 10);
                if (magnitude < -308 || magnitude > 308) invalid(path, "exponent must be within -308..308");
            }
            values[path] = token;
        }
    }
};

struct config_reader
{
    const std::map<std::wstring, std::wstring>& lexemes;
    static const JSONValue* field(const JSONObject& object, const wchar_t* key)
    {
        const auto it = object.find(key); return it == object.end() ? nullptr : it->second;
    }
    std::optional<std::int64_t> integer(const JSONObject& object, const wchar_t* key, const std::wstring& prefix,
        std::int64_t minimum, std::int64_t maximum) const
    {
        const auto* value = field(object, key);
        if (!value) return {};
        const auto path = prefix + L"/" + key;
        if (!value->IsNumber()) invalid(path, "must be an integer number");
        const auto token = lexemes.at(path);
        std::int64_t number = 0;
        if (token.find_first_of(L".eE") == std::wstring::npos)
        {
            std::string narrow;
            for (const auto c : token) narrow.push_back(static_cast<char>(c));
            const auto parsed = std::from_chars(narrow.data(), narrow.data() + narrow.size(), number);
            if (parsed.ec != std::errc{} || parsed.ptr != narrow.data() + narrow.size()) invalid(path, "integer is outside signed 64-bit range");
        }
        else
        {
            const auto raw = value->AsNumber();
            if (!std::isfinite(raw) || std::trunc(raw) != raw || std::abs(raw) > 9007199254740991.)
                invalid(path, "must be an exact integer; use plain integer digits for large tick values");
            number = static_cast<std::int64_t>(raw);
        }
        if (number < minimum || number > maximum)
            invalid(path, "must be within " + std::to_string(minimum) + ".." + std::to_string(maximum));
        return number;
    }
    static std::optional<double> number(const JSONObject& object, const wchar_t* key, const std::wstring& prefix, double maximum)
    {
        const auto* value = field(object, key);
        if (!value) return {};
        if (!value->IsNumber() || !std::isfinite(value->AsNumber()) || value->AsNumber() < 0 || value->AsNumber() > maximum)
            invalid(prefix + L"/" + key, "must be a finite nonnegative number no greater than " + std::to_string(maximum));
        return value->AsNumber();
    }
    static std::optional<bool> boolean(const JSONObject& object, const wchar_t* key, const std::wstring& prefix)
    {
        const auto* value = field(object, key);
        if (!value) return {};
        if (!value->IsBool()) invalid(prefix + L"/" + key, "must be true or false");
        return value->AsBool();
    }
    static std::optional<std::wstring> path(const JSONObject& object, const wchar_t* key, const std::wstring& prefix)
    {
        const auto* value = field(object, key);
        if (!value) return {};
        if (!value->IsString() || value->AsString().empty() || value->AsString().find(L'\0') != std::wstring::npos)
            invalid(prefix + L"/" + key, "must be a nonempty path string without NUL characters");
        return value->AsString();
    }
};

struct entry_settings
{
    std::wstring filename;
    std::optional<std::int64_t> ppq, offset, selection_start, selection_length;
    std::optional<double> tempo;
    std::vector<std::pair<std::uint32_t, bool>> flags;
    std::vector<std::pair<bool file_settings::*, bool>> switches;
};

void validate_midi(const std::wstring& filename, const std::wstring& field)
{
    std::ifstream input(std::filesystem::path(filename), std::ios::binary);
    std::array<unsigned char, 14> header{};
    input.read(reinterpret_cast<char*>(header.data()), header.size());
    if (!input || header[0] != 'M' || header[1] != 'T' || header[2] != 'h' || header[3] != 'd'
        || header[4] || header[5] || header[6] || header[7] != 6 || (!header[10] && !header[11]))
        invalid(field, "not an accessible MIDI with a complete standard header: " + utf8(filename));
    if ((!header[12] && !header[13]) || (header[12] & 0x80))
        invalid(field, "MIDI must use a positive PPQN time division");
}

std::wstring resolved_for_compare(const std::filesystem::path& path)
{
    std::error_code error;
    if (std::filesystem::exists(path, error)) return std::filesystem::weakly_canonical(path).wstring();
    const auto absolute = std::filesystem::absolute(path).lexically_normal();
    return (std::filesystem::weakly_canonical(absolute.parent_path()) / absolute.filename()).wstring();
}
}

const char* cli_help()
{
    return R"(SAFC ImGui command-line MIDI processing

Usage: SAFCImGui.exe "C:\configs\merge.json"
Help: --help, /help, -?, /?

The UTF-8 JSON config uses the same processing core as the ImGui project.
Relative paths resolve from the current working directory. Normal CLI runs
read saved application defaults and never write registry settings.

{
  "global_ppq_override": 960,
  "global_tempo_override": 120,
  "global_offset_override": 0,
  "save_to": "C:\\MIDIs\\merged.mid",
  "files": [
    {
      "filename": "C:\\MIDIs\\input.mid",
      "ppq_override": 480,
      "tempo_override": 0,
      "offset": 0,
      "selection_start": 0,
      "selection_length": -1,
      "ignore_notes": false,
      "ignore_pitches": false,
      "ignore_tempos": false,
      "ignore_other": false,
      "piano_only": true,
      "remove_remnants": true,
      "remove_empty_tracks": true,
      "channel_split": false,
      "collapse_midi": false,
      "ignore_meta_rsb": false,
      "rsb_compression": false,
      "inplace_mergable": false,
      "allow_sysex": false,
      "enable_zero_velocity": false,
      "apply_offset_after": true
    }
  ]
}

Only files and each filename are required. global_offset is the legacy alias
of global_offset_override; supplying both requires equal values. PPQN is
1..65535, tempo 0..60000000 (values <=3 keep the original), and global offset is a
signed 32-bit integer. Per-file ticks accept signed 64-bit integer digits;
selection_start is nonnegative and selection_length is -1 (to end) or >=0.
An absent save_to uses the first input name plus .AfterSAFC.mid. A missing
.mid extension is appended once. Output may never equal any input MIDI.
All files and settings are validated before a merge is started.
)";
}

void load_cli_config(const std::wstring& path, project_session& project, bool load_preferences)
{
    if (project.loading() || project.merging() || !project.data.files.empty()) throw std::runtime_error("CLI config requires an empty project.");
    std::ifstream input(std::filesystem::path(path), std::ios::binary);
    if (!input) throw std::runtime_error("Cannot open JSON config: " + utf8(path));
    const std::string bytes((std::istreambuf_iterator<char>(input)), {});
    const auto wide = decode_utf8(bytes);
    numeric_lexemes lexemes{wide}; lexemes.scan();
    if (lexemes.peek()) throw std::runtime_error("Unexpected data after the JSON config.");
    std::unique_ptr<JSONValue> root(JSON::Parse(wide.c_str()));
    if (!root || !root->IsObject()) throw std::runtime_error("Config must be a JSON object.");
    const auto& object = root->AsObject();
    const config_reader reader{lexemes.values};
    const auto global_ppq = reader.integer(object, L"global_ppq_override", {}, 1, UINT16_MAX);
    const auto global_tempo = reader.number(object, L"global_tempo_override", {}, 60000000);
    const auto offset_alias = reader.integer(object, L"global_offset_override", {}, INT32_MIN, INT32_MAX);
    const auto offset_old = reader.integer(object, L"global_offset", {}, INT32_MIN, INT32_MAX);
    if (offset_alias && offset_old && *offset_alias != *offset_old) throw std::runtime_error("global_offset and global_offset_override conflict.");
    const auto global_offset = offset_alias ? offset_alias : offset_old;
    const auto save_to = reader.path(object, L"save_to", {});
    const auto* files = reader.field(object, L"files");
    if (!files || !files->IsArray() || files->AsArray().empty()) throw std::runtime_error("files must be a nonempty array.");

    const std::pair<const wchar_t*, std::uint32_t> flags[] = {
        {L"ignore_notes", _BoolSettings::ignore_notes}, {L"ignore_pitches", _BoolSettings::ignore_pitches},
        {L"ignore_tempos", _BoolSettings::ignore_tempos}, {L"ignore_other", _BoolSettings::ignore_all_but_tempos_notes_and_pitch},
        {L"piano_only", _BoolSettings::all_instruments_to_piano}, {L"remove_remnants", _BoolSettings::remove_remnants},
        {L"remove_empty_tracks", _BoolSettings::remove_empty_tracks}
    };
    const std::pair<const wchar_t*, bool file_settings::*> switches[] = {
        {L"channel_split", &file_settings::channels_split}, {L"collapse_midi", &file_settings::collapse_midi},
        {L"apply_offset_after", &file_settings::apply_offset_after}, {L"rsb_compression", &file_settings::rsb_compression},
        {L"ignore_meta_rsb", &file_settings::allow_legacy_rsb_meta_interaction}, {L"inplace_mergable", &file_settings::inplace_merge_enabled},
        {L"allow_sysex", &file_settings::allow_sysex}, {L"enable_zero_velocity", &file_settings::enable_zero_velocity}
    };
    std::vector<entry_settings> entries;
    for (std::size_t i = 0; i < files->AsArray().size(); ++i)
    {
        const auto prefix = L"/files/" + std::to_wstring(i);
        const auto* value = files->AsArray()[i];
        if (!value->IsObject()) invalid(prefix, "must be an object");
        const auto& object = value->AsObject();
        entry_settings entry;
        const auto filename = reader.path(object, L"filename", prefix);
        if (!filename) invalid(prefix + L"/filename", "is required");
        entry.filename = *filename;
        entry.ppq = reader.integer(object, L"ppq_override", prefix, 1, UINT16_MAX);
        entry.tempo = reader.number(object, L"tempo_override", prefix, 60000000);
        entry.offset = reader.integer(object, L"offset", prefix, -INT64_MAX, INT64_MAX);
        entry.selection_start = reader.integer(object, L"selection_start", prefix, 0, INT64_MAX);
        entry.selection_length = reader.integer(object, L"selection_length", prefix, -1, INT64_MAX);
        const auto start = entry.selection_start.value_or(0);
        const auto length = entry.selection_length.value_or(-1);
        if (length >= 0 && start > INT64_MAX - length) invalid(prefix, "selection end exceeds signed 64-bit range");
        for (const auto& [name, flag] : flags) if (const auto setting = reader.boolean(object, name, prefix)) entry.flags.emplace_back(flag, *setting);
        for (const auto& [name, member] : switches) if (const auto setting = reader.boolean(object, name, prefix)) entry.switches.emplace_back(member, *setting);
        validate_midi(entry.filename, prefix + L"/filename");
        entries.push_back(std::move(entry));
    }

    auto output = save_to.value_or(entries.front().filename + L".AfterSAFC.mid");
    if (_wcsicmp(std::filesystem::path(output).extension().c_str(), L".mid") != 0) output += L".mid";
    const auto canonical_output = resolved_for_compare(output);
    for (const auto& entry : entries)
    {
        const auto canonical_input = resolved_for_compare(entry.filename);
        if (_wcsicmp(canonical_input.c_str(), canonical_output.c_str()) == 0) throw std::runtime_error("The merge output must not overwrite an input MIDI.");
    }
    const auto defaults = load_preferences ? preferences_store{}.load() : application_preferences{};
    project.set_defaults(defaults);
    std::vector<std::wstring> paths;
    for (const auto& entry : entries) paths.push_back(entry.filename);
    project.add_files(std::move(paths));
    while (project.loading()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
    project.poll();
    if (project.data.files.size() != entries.size()) throw std::runtime_error("An input MIDI could not be loaded; no per-file settings were applied. " + project.message());
    project.data.is_cli_mode = true;
    if (global_ppq) project.data.set_global_ppqn(static_cast<std::uint16_t>(*global_ppq), true);
    if (global_tempo) project.data.set_global_tempo(static_cast<float>(*global_tempo));
    if (global_offset) project.data.set_global_offset(static_cast<std::int32_t>(*global_offset));
    for (std::size_t i = 0; i < entries.size(); ++i)
    {
        auto& file = project.data.files[i];
        const auto& entry = entries[i];
        if (entry.ppq) { file.new_ppqn = static_cast<std::uint16_t>(*entry.ppq); file.ppqn_manually_set = true; }
        if (entry.tempo) file.new_tempo = *entry.tempo;
        if (entry.offset) file.offset_ticks = *entry.offset;
        if (entry.selection_start) file.selection_start = *entry.selection_start;
        if (entry.selection_length) file.selection_length = *entry.selection_length;
        for (const auto& [flag, state] : entry.flags) file.set_bool_setting(flag, state);
        for (const auto& [member, state] : entry.switches) file.*member = state;
    }
    project.data.save_path = std::move(output);
}

int run_cli(const std::wstring& argument, bool load_preferences)
{
    if (argument == L"--help" || argument == L"/help" || argument == L"-?" || argument == L"/?")
    { std::cout << cli_help() << std::flush; return 0; }
    if (argument.empty()) { std::cerr << cli_help() << std::flush; return 2; }
    try
    {
        project_session project;
        load_cli_config(argument, project, load_preferences);
        std::cout << "Loaded " << project.data.files.size() << " MIDI(s). Output: " << utf8(project.data.save_path) << '\n';
        if (!project.start_merge())
        {
            const auto status = project.progress();
            throw std::runtime_error(status.error.empty() ? "Could not start merge." : status.error);
        }
        std::string previous_stage;
        while (project.merging())
        {
            const auto progress = project.progress();
            if (previous_stage != progress.stage)
            { previous_stage = progress.stage; std::cout << progress.stage << '\n' << std::flush; }
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
        }
        const auto finished = project.progress();
        if (!finished.error.empty()) throw std::runtime_error(finished.error);
        if (finished.stage != "Merge complete") throw std::runtime_error(finished.stage);
        std::cout << "Merge complete in " << finished.seconds << " seconds.\n" << std::flush;
        return 0;
    }
    catch (const std::exception& error) { std::cerr << "SAFC CLI: " << error.what() << '\n' << std::flush; return 1; }
}
}
