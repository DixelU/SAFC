#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../imgui/update_service.h"
#include <Windows.h>
#include <archive.h>
#include <archive_entry.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstring>
#include <fstream>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace ui = safc::imgui_ui;
namespace detail = safc::imgui_ui::update_detail;
namespace fs = std::filesystem;
namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}
fs::path self() {
    std::wstring path(32768, L'\0'); DWORD size = static_cast<DWORD>(path.size());
    require(QueryFullProcessImageNameW(GetCurrentProcess(), 0, path.data(), &size), "query process path");
    path.resize(size); return path;
}
std::vector<std::byte> read(const fs::path& file) {
    std::ifstream input(file, std::ios::binary | std::ios::ate);
    require(bool(input), "open fixture");
    std::vector<std::byte> bytes(static_cast<std::size_t>(input.tellg()));
    input.seekg(0); input.read(reinterpret_cast<char*>(bytes.data()), bytes.size());
    require(bool(input), "read fixture"); return bytes;
}
std::string asset(const char* name, std::size_t size, const char* tag = "v2.0.0.0") {
    return std::string("{\"name\":\"") + name + "\",\"state\":\"uploaded\",\"size\":" + std::to_string(size) +
        ",\"browser_download_url\":\"https://github.com/DixelU/SAFC/releases/download/" + tag + "/" + name + "\"}";
}
std::string release(const std::string& assets, const char* tag = "v2.0.0.0", bool draft = false, bool prerelease = false) {
    return std::string("{\"tag_name\":\"") + tag + "\",\"draft\":" + (draft ? "true" : "false") +
        ",\"prerelease\":" + (prerelease ? "true" : "false") + ",\"published_at\":\"2026-09-07T00:00:00Z\",\"assets\":[" + assets + "]}";
}
std::vector<std::byte> bytes(const std::string& text) {
    std::vector<std::byte> result(text.size()); std::memcpy(result.data(), text.data(), text.size()); return result;
}
struct member { const char* name; std::vector<std::byte> content; const char* symlink = nullptr; };
std::vector<std::byte> archive(const std::vector<member>& members) {
    std::size_t capacity = 1024 * 1024;
    for (const auto& member : members) capacity += member.content.size() * 2;
    std::vector<std::byte> result(capacity);
    auto* writer = archive_write_new();
    require(writer, "create fixture archive writer");
    require(archive_write_set_format_zip(writer) == ARCHIVE_OK, "fixture ZIP format");
    std::size_t used = 0;
    require(archive_write_open_memory(writer, result.data(), result.size(), &used) == ARCHIVE_OK, "open fixture archive");
    for (const auto& member : members) {
        auto* entry = archive_entry_new();
        archive_entry_set_pathname(entry, member.name);
        archive_entry_set_filetype(entry, member.symlink ? AE_IFLNK : AE_IFREG);
        archive_entry_set_perm(entry, 0600);
        archive_entry_set_size(entry, member.symlink ? 0 : member.content.size());
        if (member.symlink) archive_entry_set_symlink(entry, member.symlink);
        require(archive_write_header(writer, entry) == ARCHIVE_OK, "fixture member header");
        if (!member.symlink && !member.content.empty())
            require(archive_write_data(writer, member.content.data(), member.content.size()) == member.content.size(), "fixture member bytes");
        archive_entry_free(entry);
    }
    require(archive_write_close(writer) == ARCHIVE_OK, "close fixture archive");
    archive_write_free(writer); result.resize(used); return result;
}
void upgrade_fixture_version(const fs::path& path) {
    const auto module = GetModuleHandleW(nullptr);
    const auto resource = FindResourceW(module, MAKEINTRESOURCEW(1), RT_VERSION);
    require(resource, "test executable must contain the application version resource");
    const auto loaded = LoadResource(module, resource);
    const auto size = SizeofResource(module, resource);
    const auto* data = static_cast<const std::byte*>(LockResource(loaded));
    std::vector<std::byte> copied(data, data + size);
    bool patched = false;
    for (std::size_t offset = 0; offset + sizeof(VS_FIXEDFILEINFO) <= copied.size(); offset += sizeof(WORD)) {
        VS_FIXEDFILEINFO info{}; std::memcpy(&info, copied.data() + offset, sizeof(info));
        if (info.dwSignature != 0xfeef04bd) continue;
        info.dwProductVersionMS = info.dwFileVersionMS = MAKELONG(0, 2);
        info.dwProductVersionLS = info.dwFileVersionLS = 0;
        std::memcpy(copied.data() + offset, &info, sizeof(info)); patched = true; break;
    }
    require(patched, "locate fixture fixed version");
    const auto update = BeginUpdateResourceW(path.c_str(), FALSE);
    require(update, "begin fixture resource update");
    require(UpdateResourceW(update, RT_VERSION, MAKEINTRESOURCEW(1), MAKELANGID(LANG_RUSSIAN, SUBLANG_DEFAULT),
        copied.data(), static_cast<DWORD>(copied.size())), "patch fixture resource");
    require(EndUpdateResourceW(update, FALSE), "save fixture resource");
    require(detail::executable_version(path) == detail::version{2, 0, 0, 0}, "fixture ProductVersion was changed");
}
void wait_finished(ui::update_service& updater) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (std::chrono::steady_clock::now() < deadline) {
        const auto phase = updater.snapshot().phase;
        if (phase != ui::update_phase::checking && phase != ui::update_phase::downloading) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    throw std::runtime_error("offline update did not finish");
}
void parsing_tests() {
    require(detail::parse_version("v2.0.0.0") == detail::version{2, 0, 0, 0}, "strict four-part version");
    require(*detail::parse_version("v2.0.0.0") > *detail::parse_version("v1.65535.65535.65535"), "numeric version ordering");
    for (auto text : {"2.0.0.0", "v2.0.0", "v2.0.0.0.0", "v2.0.0.0-beta", "v02.0.0.0", "v65536.0.0.0", "v-1.0.0.0", "v1..0.0", " v2.0.0.0"})
        require(!detail::parse_version(text), std::string("reject version ") + text);
    std::string error;
    const auto metadata = release(asset("SAFC32.7z", 12) + "," + asset("SAFC64.7z", 25));
    require(detail::select_release(metadata, detail::machine_x64, error)->size == 25, "x64 exact asset");
    require(detail::select_release(metadata, detail::machine_x86, error)->size == 12, "x86 exact asset");
    require(!detail::select_release(metadata, 0xaa64, error), "reject unsupported architecture");
    require(!detail::select_release(release(asset("SAFC64.7z", 1), "v2.0.0.0", true), detail::machine_x64, error), "reject draft");
    require(!detail::select_release(release(asset("SAFC64.7z", 1), "v2.0.0.0", false, true), detail::machine_x64, error), "reject prerelease");
    require(!detail::select_release(release(asset("SAFC32.7z", 1)), detail::machine_x64, error), "reject wrong architecture archive");
    require(!detail::select_release(release(asset("SAFC64.7z", 1) + "," + asset("SAFC64.7z", 2)), detail::machine_x64, error), "reject duplicate asset");
    require(!detail::select_release(release(asset("SAFC64.7z", detail::max_archive_bytes + 1)), detail::machine_x64, error), "reject oversized asset");
    auto invalid_url = metadata; invalid_url.replace(invalid_url.find("github.com"), 10, "github.evil");
    require(!detail::select_release(invalid_url, detail::machine_x86, error), "reject unofficial asset URL");
    require(!detail::select_release("{bad}", detail::machine_x64, error), "reject malformed JSON");
    auto unicode_tag = release(asset("SAFC64.7z", 1));
    unicode_tag.replace(unicode_tag.find("v2.0.0.0"), 1, "\\u0176");
    require(!detail::select_release(unicode_tag, detail::machine_x64, error), "reject Unicode characters that narrow to ASCII version characters");
}
void archive_tests(const fs::path& root) {
    const auto output = root / L"extracted.exe";
    const auto content = bytes("local archive fixture"); std::string error;
    auto good = archive({{"SAFC.exe", content}, {"README.md", bytes("ignored")}});
    require(detail::extract_executable(good, output, {}, error), "extract root executable: " + error);
    require(read(output) == content && !fs::exists(root / L"README.md"), "extract only the executable");
    require(!detail::extract_executable(good, output, {}, error) && read(output) == content, "never overwrite occupied staging file");
    fs::remove(output);
    const std::vector<std::vector<member>> invalid = {
        {{"../SAFC.exe", content}}, {{"..\\SAFC.exe", content}}, {{"C:/SAFC.exe", content}},
        {{"/SAFC.exe", content}}, {{"folder/SAFC.exe", content}}, {{"README.md", content}},
        {{"SAFC.exe", content}, {"SAFC.exe", content}}, {{"SAFC.exe", content}, {"safc.exe", content}},
        {{"SAFC.exe", {}, "other.exe"}}, {{"SAFC.exe", content}, {"link", {}, "../outside"}}
    };
    for (const auto& entries : invalid) {
        require(!detail::extract_executable(archive(entries), output, {}, error), "reject unsafe or incomplete archive");
        require(!fs::exists(output), "remove own partial staged executable on failure");
    }
    require(!detail::extract_executable(bytes("not an archive"), output, {}, error), "reject malformed archive");
    std::stop_source cancellation; cancellation.request_stop();
    require(!detail::extract_executable(good, output, cancellation.get_token(), error) && !fs::exists(output), "cancel extraction");
}
void replacement_tests(const fs::path& root, const fs::path& upgraded) {
    std::string error;
    const auto target = root / L"renamed application.exe", staged = root / L"replacement.exe", backup = root / L"backup.exe";
    fs::copy_file(self(), target); fs::copy_file(upgraded, staged);
    const auto previous_cwd = fs::current_path(); fs::current_path(root.parent_path());
    const auto result = detail::replace_executable(target, staged, backup, error);
    fs::current_path(previous_cwd);
    require(result, "absolute-path installation with different working directory: " + error);
    require(detail::executable_version(target) == detail::version{2, 0, 0, 0}, "installed under original filename");
    require(detail::executable_version(backup) == detail::executable_version(self()), "retained previous file");
    fs::remove(target); fs::remove(backup);
    fs::copy_file(self(), target); fs::copy_file(upgraded, staged);
    HANDLE lock = CreateFileW(staged.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    require(lock != INVALID_HANDLE_VALUE, "lock staged fixture against rename");
    const auto failed = detail::replace_executable(target, staged, backup, error);
    CloseHandle(lock);
    require(!failed && !error.empty(), "locked replacement must report failure");
    require(detail::executable_version(target) == detail::executable_version(self()), "failed installation preserves old executable");
    require(!fs::exists(backup), "rollback restored original pathname");
    fs::remove(target); fs::remove(staged);
}
void running_image_test(const fs::path& root, const fs::path& upgraded) {
    const auto target = root / L"running fixture.exe", staged = root / L"running replacement.exe", backup = root / L"running previous.exe";
    fs::copy_file(self(), target); fs::copy_file(upgraded, staged);
    const std::wstring event_name = L"Local\\SAFC-update-fixture-" + std::to_wstring(GetCurrentProcessId());
    HANDLE done = CreateEventW(nullptr, TRUE, FALSE, event_name.c_str());
    HANDLE ready = CreateEventW(nullptr, TRUE, FALSE, (event_name + L"-ready").c_str());
    require(done && ready, "create fixture events");
    std::wstring command = L"\"" + target.wstring() + L"\" --mapped-child " + event_name;
    STARTUPINFOW startup{}; startup.cb = sizeof(startup); PROCESS_INFORMATION process{};
    require(CreateProcessW(target.c_str(), command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW,
        nullptr, root.c_str(), &startup, &process), "launch local mapped-image fixture");
    const auto started = WaitForSingleObject(ready, 5000);
    std::string error; const bool replaced = started == WAIT_OBJECT_0 && detail::replace_executable(target, staged, backup, error);
    const auto installed = detail::executable_version(target);
    const bool still_running = WaitForSingleObject(process.hProcess, 0) == WAIT_TIMEOUT;
    SetEvent(done);
    const auto finished = WaitForSingleObject(process.hProcess, 5000);
    CloseHandle(process.hProcess); CloseHandle(process.hThread); CloseHandle(done); CloseHandle(ready);
    require(started == WAIT_OBJECT_0 && finished == WAIT_OBJECT_0, "mapped-image fixture lifecycle");
    require(replaced, "replace an actual running Windows executable: " + error);
    require(installed == detail::version{2, 0, 0, 0} && still_running, "running process retains old mapping while new path is installed");
    require(detail::executable_version(backup) == detail::executable_version(self()), "running-image backup holds previous executable");
}
void service_tests(const fs::path& root, const fs::path& upgraded) {
#ifdef _WIN64
    constexpr auto wanted = "SAFC64.7z";
    constexpr auto machine = detail::machine_x64;
#else
    constexpr auto wanted = "SAFC32.7z";
    constexpr auto machine = detail::machine_x86;
#endif
    std::string error;
    require(detail::verify_executable(upgraded, {2, 0, 0, 0}, machine, error), "accept matching PE: " + error);
    require(!detail::verify_executable(upgraded, {3, 0, 0, 0}, machine, error), "reject mismatched ProductVersion");
    require(!detail::verify_executable(upgraded, {2, 0, 0, 0}, machine == detail::machine_x64 ? detail::machine_x86 : detail::machine_x64, error), "reject mismatched PE machine");
    const auto target = root / L"custom SAFC.exe"; fs::copy_file(self(), target);
    const auto package = archive({{"SAFC.exe", read(upgraded)}});
    const auto metadata = bytes(release(asset(wanted, package.size())));
    std::atomic<unsigned> calls = 0;
    auto transport = [&](std::wstring_view url, std::size_t limit, std::stop_token, const detail::progress_callback& progress) {
        ++calls;
        if (url == L"https://api.github.com/repos/DixelU/SAFC/releases/latest") return metadata;
        require(limit == package.size(), "bounded architecture asset request");
        if (progress) progress(1.0f); return package;
    };
    {
        ui::update_service updater({target, transport}); updater.check(); wait_finished(updater);
        require(updater.snapshot().phase == ui::update_phase::ready, "offline update reaches ready: " + updater.snapshot().message);
        updater.shutdown();
        require(updater.snapshot().phase == ui::update_phase::ready, "shutdown preserves ready update");
        require(updater.install_pending(error), "install validated offline update: " + error);
        require(!updater.install_pending(error) && error.empty(), "completed update cannot install twice");
    }
    require(calls == 2 && detail::executable_version(target) == detail::version{2, 0, 0, 0}, "one release check and one exact asset fetch");
    {
        ui::update_service updater({target, transport}); updater.check(); wait_finished(updater);
        require(updater.snapshot().phase == ui::update_phase::up_to_date && calls == 3, "same-version release never downloads");
    }
    fs::remove(target); fs::copy_file(self(), target);
    {
        ui::update_service updater({target, transport}); updater.check(); wait_finished(updater); updater.cancel();
        require(updater.snapshot().phase == ui::update_phase::idle && !updater.install_pending(error) && error.empty(), "cancel discards completed staging");
        require(detail::executable_version(target) == detail::executable_version(self()), "cancel preserves executable");
    }
    {
        std::atomic<bool> started = false;
        ui::update_service updater({target, [&](std::wstring_view, std::size_t, std::stop_token stop, const detail::progress_callback&) {
            std::mutex mutex; std::condition_variable_any changed; std::unique_lock lock(mutex);
            started = true; changed.wait(lock, stop, [] { return false; }); return std::vector<std::byte>{};
        }});
        updater.check();
        while (!started) std::this_thread::yield();
        const auto start = std::chrono::steady_clock::now(); updater.cancel();
        require(std::chrono::steady_clock::now() - start < std::chrono::seconds(1), "cancellation joins transport promptly");
        require(updater.snapshot().phase == ui::update_phase::idle && !updater.install_pending(error), "cancelled request never installs");
    }
    {
        // Deterministically simulate the rare case where another actor occupies
        // the destination after the old image is moved, preventing rollback.
        fs::path retained_backup;
        {
            ui::update_service updater({target, transport,
                [&](const fs::path& destination, const fs::path&, const fs::path& backup, std::string& failure) {
                    fs::rename(destination, backup);
                    fs::create_directory(destination);
                    retained_backup = backup;
                    failure = "Simulated destination obstruction prevents replacement and rollback.";
                    return false;
                }});
            updater.check(); wait_finished(updater);
            require(updater.snapshot().phase == ui::update_phase::ready, "prepare failed-rollback regression");
            require(!updater.install_pending(error) && !error.empty(), "report failed rollback");
        }
        require(fs::is_regular_file(retained_backup) && detail::executable_version(retained_backup) == detail::executable_version(self()),
            "destruction preserves the sole original executable after failed rollback");
        require(!fs::exists(retained_backup.parent_path() / L"installed.txt"), "failed installation has no success marker");
        fs::remove(target); fs::copy_file(self(), target);
        const auto old_tag = detail::format_version(*detail::executable_version(target));
        const auto current_metadata = bytes(release(asset(wanted, 1, old_tag.c_str()), old_tag.c_str()));
        ui::update_service later({target, [&](std::wstring_view, std::size_t, std::stop_token, const detail::progress_callback&) {
            return current_metadata;
        }});
        later.check(); wait_finished(later);
        require(later.snapshot().phase == ui::update_phase::up_to_date && fs::is_regular_file(retained_backup),
            "later update check does not delete a failed-install backup");
    }
}
}

int wmain(int argc, wchar_t** argv) {
    if (argc == 2 && std::wstring_view(argv[1]) == L"--check-live") {
        try {
#ifdef _WIN64
            constexpr auto machine = detail::machine_x64;
#else
            constexpr auto machine = detail::machine_x86;
#endif
            std::string error;
            const auto selected = detail::select_release(detail::fetch_release_metadata(), machine, error);
            require(bool(selected), "live release metadata: " + error);
            const auto current = detail::executable_version(self());
            require(bool(current), "live-check process has a ProductVersion");
            std::cout << "Installed " << detail::format_version(*current) << "; published stable " << selected->tag
                << "; exact architecture archive size " << selected->size << ". No asset downloaded or executed.\n";
            return 0;
        } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
    }
    if (argc == 3 && std::wstring_view(argv[1]) == L"--mapped-child") {
        HANDLE done = OpenEventW(SYNCHRONIZE, FALSE, argv[2]);
        HANDLE ready = OpenEventW(EVENT_MODIFY_STATE, FALSE, (std::wstring(argv[2]) + L"-ready").c_str());
        if (!done || !ready) return 2;
        SetEvent(ready); const auto result = WaitForSingleObject(done, 15000);
        CloseHandle(done); CloseHandle(ready); return result == WAIT_OBJECT_0 ? 0 : 3;
    }
    try {
        const auto root = fs::absolute(argc > 1 ? fs::path(argv[1]) : self().parent_path() / L"update-regression-output") /
            (L"run-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
        fs::create_directories(root);
        const auto upgraded = root / L"upgraded local fixture.exe";
        fs::copy_file(self(), upgraded); upgrade_fixture_version(upgraded);
        parsing_tests(); archive_tests(root); replacement_tests(root, upgraded);
        running_image_test(root, upgraded); service_tests(root, upgraded);
        // This directory was uniquely created above and contains only local
        // regression fixtures; production update code never recursively deletes.
        fs::remove_all(root);
        std::cout << "Updater parsing, archive, cancellation, installation, rollback, and running-image tests passed.\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
