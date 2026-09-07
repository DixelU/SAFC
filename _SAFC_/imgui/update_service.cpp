#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "update_service.h"
#include "../JSON/JSON.h"

#include <Windows.h>
#include <winhttp.h>
#include <archive.h>
#include <archive_entry.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstring>
#include <fstream>
#include <mutex>
#include <stdexcept>
#include <thread>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "version.lib")

namespace safc::imgui_ui {
namespace {
using clock_type = std::chrono::steady_clock;
using namespace update_detail;
constexpr auto release_endpoint = L"https://api.github.com/repos/DixelU/SAFC/releases/latest";
#ifdef _WIN64
constexpr auto native_machine = machine_x64;
#else
constexpr auto native_machine = machine_x86;
#endif

std::string win_error(const char* operation, DWORD code = GetLastError()) {
    return std::string(operation) + " (Windows error " + std::to_string(code) + ")";
}
void check_stop(std::stop_token stop, clock_type::time_point deadline) {
    if (stop.stop_requested()) throw std::runtime_error("Update cancelled.");
    if (clock_type::now() > deadline) throw std::runtime_error("Update operation timed out.");
}
std::filesystem::path self_path() {
    std::wstring buffer(32768, L'\0');
    DWORD length = static_cast<DWORD>(buffer.size());
    if (QueryFullProcessImageNameW(GetCurrentProcess(), 0, buffer.data(), &length)) {
        buffer.resize(length);
        return buffer;
    }
    length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!length || length == buffer.size()) throw std::runtime_error("Cannot locate the running executable.");
    buffer.resize(length);
    return std::filesystem::absolute(buffer);
}
bool ordinary_file(const std::filesystem::path& path) {
    const auto attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES &&
        !(attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT));
}
bool ordinary_directory(const std::filesystem::path& path) {
    const auto attributes = GetFileAttributesW(path.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY) &&
        !(attributes & FILE_ATTRIBUTE_REPARSE_POINT);
}
struct file_handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    ~file_handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
struct internet_handle {
    HINTERNET value = nullptr;
    ~internet_handle() { if (value) WinHttpCloseHandle(value); }
};
struct cancellable_request {
    std::atomic<HINTERNET> value;
    void close() { if (const auto handle = value.exchange(nullptr)) WinHttpCloseHandle(handle); }
    ~cancellable_request() { close(); }
};

// WinHTTP's automatic redirects are disabled: every hop must remain on the
// official GitHub download hosts, with HTTPS and the standard port.
std::vector<std::byte> download(std::wstring_view input, std::size_t limit,
    std::stop_token stop, const progress_callback& progress) {
    const auto deadline = clock_type::now() + std::chrono::minutes(3);
    internet_handle session{WinHttpOpen(L"SAFC-Updater/2", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)};
    if (!session.value) throw std::runtime_error(win_error("Cannot open HTTPS session"));
    if (!WinHttpSetTimeouts(session.value, 3000, 3000, 3000, 3000))
        throw std::runtime_error(win_error("Cannot configure HTTPS timeouts"));
    std::wstring url(input);
    for (int hop = 0; hop != 6; ++hop) {
        check_stop(stop, deadline);
        URL_COMPONENTS components{};
        components.dwStructSize = sizeof(components);
        components.dwHostNameLength = components.dwUrlPathLength = components.dwExtraInfoLength = DWORD(-1);
        components.dwUserNameLength = components.dwPasswordLength = DWORD(-1);
        if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &components))
            throw std::runtime_error("Invalid update URL.");
        std::wstring host(components.lpszHostName, components.dwHostNameLength);
        std::transform(host.begin(), host.end(), host.begin(), towlower);
        if (components.nScheme != INTERNET_SCHEME_HTTPS || components.nPort != INTERNET_DEFAULT_HTTPS_PORT ||
            components.dwUserNameLength || components.dwPasswordLength ||
            (host != L"api.github.com" && host != L"github.com" && host != L"release-assets.githubusercontent.com" &&
                host != L"objects.githubusercontent.com"))
            throw std::runtime_error("Update redirect is not an official HTTPS GitHub endpoint.");
        std::wstring path(components.lpszUrlPath, components.dwUrlPathLength);
        if (components.dwExtraInfoLength) path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
        internet_handle connection{WinHttpConnect(session.value, host.c_str(), components.nPort, 0)};
        if (!connection.value) throw std::runtime_error(win_error("Cannot connect to GitHub"));
        cancellable_request request{WinHttpOpenRequest(connection.value, L"GET", path.c_str(), nullptr,
            WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE)};
        if (!request.value) throw std::runtime_error(win_error("Cannot create update request"));
        // Closing an outstanding WinHTTP request aborts synchronous network I/O.
        // The callback is destroyed before the request owner leaves this scope.
        std::stop_callback cancel_request(stop, [&request] { request.close(); });
        DWORD redirect_policy = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        if (!WinHttpSetOption(request.value, WINHTTP_OPTION_REDIRECT_POLICY, &redirect_policy, sizeof(redirect_policy)))
            throw std::runtime_error(win_error("Cannot configure update redirect policy"));
        const wchar_t* headers = L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
        if (!WinHttpSendRequest(request.value, headers, DWORD(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
            !WinHttpReceiveResponse(request.value, nullptr))
            throw std::runtime_error(win_error("Cannot fetch the update"));
        check_stop(stop, deadline);
        DWORD status = 0, status_size = sizeof(status);
        if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &status, &status_size, WINHTTP_NO_HEADER_INDEX))
            throw std::runtime_error(win_error("Cannot read update HTTP status"));
        if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
            DWORD bytes = 0;
            WinHttpQueryHeaders(request.value, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                nullptr, &bytes, WINHTTP_NO_HEADER_INDEX);
            if (!bytes || bytes > 32768) throw std::runtime_error("Invalid update redirect.");
            std::wstring location(bytes / sizeof(wchar_t), L'\0');
            if (!WinHttpQueryHeaders(request.value, WINHTTP_QUERY_LOCATION, WINHTTP_HEADER_NAME_BY_INDEX,
                location.data(), &bytes, WINHTTP_NO_HEADER_INDEX))
                throw std::runtime_error("Cannot read update redirect.");
            location.resize(wcslen(location.c_str()));
            url = std::move(location);
            continue;
        }
        if (status != 200) throw std::runtime_error("GitHub returned HTTP " + std::to_string(status) + ".");
        DWORD declared = 0, declared_size = sizeof(declared);
        WinHttpQueryHeaders(request.value, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX, &declared, &declared_size, WINHTTP_NO_HEADER_INDEX);
        if (declared > limit) throw std::runtime_error("Update download exceeds its size limit.");
        std::vector<std::byte> result;
        if (declared) result.reserve(declared);
        std::array<std::byte, 64 * 1024> buffer;
        for (;;) {
            check_stop(stop, deadline);
            DWORD received = 0;
            if (!WinHttpReadData(request.value, buffer.data(), static_cast<DWORD>(buffer.size()), &received))
                throw std::runtime_error(win_error("Cannot read update data"));
            if (!received) break;
            if (received > limit - result.size()) throw std::runtime_error("Update download exceeds its size limit.");
            result.insert(result.end(), buffer.begin(), buffer.begin() + received);
            if (progress) progress(declared ? static_cast<float>(result.size()) / declared : 0.0f);
        }
        check_stop(stop, deadline);
        if (declared && declared != result.size()) throw std::runtime_error("Incomplete update download.");
        return result;
    }
    throw std::runtime_error("Too many update redirects.");
}

// Only fixed filenames inside a directory created by this updater are removed.
// A locked previous.exe is expected while the old program is still mapped.
void cleanup_stage(const std::filesystem::path& directory) {
    if (directory.empty() || !ordinary_directory(directory)) return;
    for (const auto* name : {L"SAFC.exe", L"previous.exe"}) {
        const auto path = directory / name;
        if (ordinary_file(path)) DeleteFileW(path.c_str());
    }
    if (GetFileAttributesW((directory / L"previous.exe").c_str()) == INVALID_FILE_ATTRIBUTES) {
        if (ordinary_file(directory / L"owner.txt")) DeleteFileW((directory / L"owner.txt").c_str());
        if (ordinary_file(directory / L"installed.txt")) DeleteFileW((directory / L"installed.txt").c_str());
        RemoveDirectoryW(directory.c_str());
    }
}
void cleanup_previous_stages(const std::filesystem::path& executable) {
    if (!ordinary_file(executable)) return;
    const auto current = executable_version(executable);
    if (!current) return;
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(executable.parent_path(), ec)) {
        const auto name = entry.path().filename().wstring();
        if (!name.starts_with(L".safc-update-") || !ordinary_directory(entry.path())) continue;
        const auto marker = entry.path() / L"owner.txt";
        if (!ordinary_file(marker) || std::filesystem::file_size(marker, ec) > 65536 || ec) continue;
        const auto installed_marker = entry.path() / L"installed.txt";
        if (!ordinary_file(installed_marker) || std::filesystem::file_size(installed_marker, ec) > 32 || ec) continue;
        std::ifstream installed_input(installed_marker, std::ios::binary);
        const std::string installed((std::istreambuf_iterator<char>(installed_input)), {});
        const auto installed_version = parse_version(installed);
        if (!installed_version || *current < *installed_version) continue;
        std::ifstream input(marker, std::ios::binary);
        const std::string owner((std::istreambuf_iterator<char>(input)), {});
        const auto encoded = executable.u8string();
        const std::string expected(reinterpret_cast<const char*>(encoded.data()), encoded.size());
        if (owner == expected && ordinary_file(entry.path() / L"previous.exe")) cleanup_stage(entry.path());
    }
}
std::filesystem::path make_stage(const std::filesystem::path& executable) {
    for (unsigned attempt = 0; attempt != 32; ++attempt) {
        const auto name = L".safc-update-" + std::to_wstring(GetCurrentProcessId()) + L"-" +
            std::to_wstring(clock_type::now().time_since_epoch().count()) + L"-" + std::to_wstring(attempt);
        const auto path = executable.parent_path() / name;
        if (CreateDirectoryW(path.c_str(), nullptr)) {
            const auto encoded = executable.u8string();
            std::ofstream marker(path / L"owner.txt", std::ios::binary);
            marker.write(reinterpret_cast<const char*>(encoded.data()), encoded.size());
            marker.close();
            if (!marker) { cleanup_stage(path); throw std::runtime_error("Cannot record update staging ownership."); }
            return path;
        }
        if (GetLastError() != ERROR_ALREADY_EXISTS)
            throw std::runtime_error(win_error("Cannot create an update staging folder beside the executable"));
    }
    throw std::runtime_error("Cannot create a unique update staging folder.");
}
}

namespace update_detail {
std::string fetch_release_metadata(std::stop_token stop) {
    const auto result = download(release_endpoint, 1024 * 1024, stop, {});
    return std::string(reinterpret_cast<const char*>(result.data()), result.size());
}
std::optional<version> parse_version(std::string_view text) {
    if (text.empty() || text.front() != 'v') return {};
    text.remove_prefix(1);
    version value{};
    for (std::size_t part = 0; part != value.size(); ++part) {
        const auto separator = text.find('.');
        const auto digits = text.substr(0, separator);
        if (digits.empty() || digits.size() > 5 || (digits.size() > 1 && digits.front() == '0')) return {};
        unsigned number = 0;
        for (char digit : digits) {
            if (digit < '0' || digit > '9') return {};
            number = number * 10 + digit - '0';
        }
        if (number > 65535) return {};
        value[part] = static_cast<std::uint16_t>(number);
        if (part == 3) { if (separator != text.npos) return {}; }
        else { if (separator == text.npos) return {}; text.remove_prefix(separator + 1); }
    }
    return value;
}
std::string format_version(const version& value) {
    return "v" + std::to_string(value[0]) + "." + std::to_string(value[1]) + "." +
        std::to_string(value[2]) + "." + std::to_string(value[3]);
}
std::optional<release> select_release(std::string_view json, std::uint16_t machine, std::string& error) {
    error.clear();
    auto fail = [&](const char* message) -> std::optional<release> { error = message; return {}; };
    if (json.empty() || json.size() > 1024 * 1024 || json.find('\0') != json.npos)
        return fail("Invalid release metadata size.");
    // Keep the small recursive JSON reader away from unbounded nesting.
    unsigned depth = 0; bool quoted = false, escaped = false;
    for (char character : json) {
        if (quoted) { if (escaped) escaped = false; else if (character == '\\') escaped = true; else if (character == '"') quoted = false; }
        else if (character == '"') quoted = true;
        else if (character == '[' || character == '{') { if (++depth > 64) return fail("Release metadata is too deeply nested."); }
        else if (character == ']' || character == '}') { if (!depth) return fail("Malformed release metadata."); --depth; }
    }
    if (quoted || depth) return fail("Malformed release metadata.");
    std::unique_ptr<JSONValue> value(JSON::Parse(std::string(json).c_str()));
    if (!value || !value->IsObject()) return fail("GitHub returned invalid release metadata.");
    for (const auto* flag : {L"draft", L"prerelease"}) {
        const auto* child = value->Child(flag);
        if (!child || !child->IsBool() || child->AsBool()) return fail("Only published stable releases can be installed.");
    }
    const auto* published = value->Child(L"published_at");
    const auto* tag = value->Child(L"tag_name");
    const auto* assets = value->Child(L"assets");
    if (!published || !published->IsString() || published->AsString().empty() || !tag || !tag->IsString() ||
        !assets || !assets->IsArray()) return fail("Release metadata is missing required fields.");
    std::string tag_text;
    tag_text.reserve(tag->AsString().size());
    for (const wchar_t character : tag->AsString()) {
        if (character > 127) return fail("Release tag must contain only ASCII version characters.");
        tag_text.push_back(static_cast<char>(character));
    }
    const auto number = parse_version(tag_text);
    if (!number) return fail("Release tag must be vN.N.N.N.");
    const wchar_t* wanted = machine == machine_x64 ? L"SAFC64.7z" : machine == machine_x86 ? L"SAFC32.7z" : nullptr;
    if (!wanted) return fail("Unsupported update architecture.");
    std::optional<release> selected;
    for (auto* asset : assets->AsArray()) {
        if (!asset || !asset->IsObject()) continue;
        auto* name = asset->Child(L"name");
        if (!name || !name->IsString() || name->AsString() != wanted) continue;
        if (selected) return fail("Release contains duplicate architecture assets.");
        auto* url = asset->Child(L"browser_download_url");
        auto* size = asset->Child(L"size");
        auto* state = asset->Child(L"state");
        const auto expected = L"https://github.com/DixelU/SAFC/releases/download/" + tag->AsString() + L"/" + wanted;
        if (!url || !url->IsString() || url->AsString() != expected || !size || !size->IsNumber() ||
            !std::isfinite(size->AsNumber()) || size->AsNumber() < 1 || size->AsNumber() > max_archive_bytes ||
            std::floor(size->AsNumber()) != size->AsNumber() || !state || !state->IsString() || state->AsString() != L"uploaded")
            return fail("Release asset has an invalid URL, size, or upload state.");
        selected = release{*number, tag_text, expected, static_cast<std::size_t>(size->AsNumber())};
    }
    if (!selected) return fail("The release has no update archive for this architecture.");
    return selected;
}
std::optional<version> executable_version(const std::filesystem::path& executable) {
    DWORD ignored = 0;
    const auto size = GetFileVersionInfoSizeW(executable.c_str(), &ignored);
    if (!size || size > 1024 * 1024) return {};
    std::vector<std::byte> bytes(size);
    if (!GetFileVersionInfoW(executable.c_str(), 0, size, bytes.data())) return {};
    VS_FIXEDFILEINFO* info = nullptr; UINT length = 0;
    if (!VerQueryValueW(bytes.data(), L"\\", reinterpret_cast<void**>(&info), &length) ||
        length < sizeof(*info) || info->dwSignature != 0xfeef04bd) return {};
    return version{HIWORD(info->dwProductVersionMS), LOWORD(info->dwProductVersionMS),
        HIWORD(info->dwProductVersionLS), LOWORD(info->dwProductVersionLS)};
}
bool verify_executable(const std::filesystem::path& executable, const version& expected,
    std::uint16_t machine, std::string& error) {
    error.clear();
    if (!ordinary_file(executable)) { error = "Staged update is not an ordinary executable file."; return false; }
    std::ifstream input(executable, std::ios::binary);
    IMAGE_DOS_HEADER dos{}; input.read(reinterpret_cast<char*>(&dos), sizeof(dos));
    if (!input || dos.e_magic != IMAGE_DOS_SIGNATURE || dos.e_lfanew < sizeof(dos) || dos.e_lfanew > 1024 * 1024) {
        error = "Staged update has an invalid DOS executable header."; return false;
    }
    input.seekg(dos.e_lfanew);
    DWORD signature = 0; IMAGE_FILE_HEADER header{};
    input.read(reinterpret_cast<char*>(&signature), sizeof(signature));
    input.read(reinterpret_cast<char*>(&header), sizeof(header));
    WORD magic = 0; input.read(reinterpret_cast<char*>(&magic), sizeof(magic));
    if (!input || signature != IMAGE_NT_SIGNATURE || header.Machine != machine ||
        !(header.Characteristics & IMAGE_FILE_EXECUTABLE_IMAGE) || (header.Characteristics & IMAGE_FILE_DLL) ||
        magic != (machine == machine_x64 ? IMAGE_NT_OPTIONAL_HDR64_MAGIC : IMAGE_NT_OPTIONAL_HDR32_MAGIC)) {
        error = "Staged update has the wrong executable architecture."; return false;
    }
    const auto actual = executable_version(executable);
    if (!actual || *actual != expected) { error = "Staged executable ProductVersion does not match its release tag."; return false; }
    return true;
}
bool extract_executable(std::span<const std::byte> bytes, const std::filesystem::path& output,
    std::stop_token stop, std::string& error) {
    error.clear();
    bool created = false;
    try {
        const auto deadline = clock_type::now() + std::chrono::seconds(30);
        check_stop(stop, deadline);
        if (bytes.empty() || bytes.size() > max_archive_bytes) throw std::runtime_error("Invalid update archive size.");
        std::unique_ptr<archive, decltype(&archive_read_free)> reader(archive_read_new(), archive_read_free);
        if (!reader) throw std::runtime_error("Cannot create update archive reader.");
        archive_read_support_filter_all(reader.get());
        archive_read_support_format_7zip(reader.get());
        // ZIP is supported by the extractor's offline fixtures; production uses
        // the exact SAFC32.7z/SAFC64.7z release asset.
        archive_read_support_format_zip(reader.get());
        if (archive_read_open_memory(reader.get(), bytes.data(), bytes.size()) != ARCHIVE_OK)
            throw std::runtime_error("Cannot open update archive.");
        archive_entry* entry = nullptr;
        unsigned entries = 0;
        std::uint64_t expanded_total = 0;
        bool found = false;
        for (;;) {
            check_stop(stop, deadline);
            const auto result = archive_read_next_header(reader.get(), &entry);
            if (result == ARCHIVE_EOF) break;
            if (result != ARCHIVE_OK || ++entries > 4096) throw std::runtime_error("Invalid update archive headers.");
            const char* name = archive_entry_pathname(entry);
            if (!name || !*name || archive_entry_symlink(entry) || archive_entry_hardlink(entry))
                throw std::runtime_error("Update archive contains an invalid path or link.");
            std::string path(name);
            std::replace(path.begin(), path.end(), '\\', '/');
            if (path.front() == '/' || path.find(':') != path.npos)
                throw std::runtime_error("Update archive contains an absolute path.");
            for (std::size_t offset = 0; offset < path.size();) {
                const auto end = path.find('/', offset);
                const auto part = path.substr(offset, end == path.npos ? path.npos : end - offset);
                if (part == ".." || part == "." || part.empty()) throw std::runtime_error("Update archive contains a traversal path.");
                if (end == path.npos) break;
                offset = end + 1;
            }
            const auto kind = archive_entry_filetype(entry);
            if (kind != AE_IFREG && kind != AE_IFDIR) throw std::runtime_error("Update archive contains a special file.");
            const auto expanded = archive_entry_size(entry);
            if (expanded < 0 || expanded > max_executable_bytes ||
                (expanded_total += static_cast<std::uint64_t>(expanded)) > 2ull * max_executable_bytes)
                throw std::runtime_error("Update archive exceeds its expanded size limit.");
            std::string lower(path);
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return static_cast<char>(tolower(c)); });
            if (lower != "safc.exe") {
                if (lower.ends_with("/safc.exe")) throw std::runtime_error("Update executable must be at the archive root.");
                if (archive_read_data_skip(reader.get()) != ARCHIVE_OK) throw std::runtime_error("Cannot skip update archive member.");
                continue;
            }
            if (found || kind != AE_IFREG || archive_entry_size(entry) <= 0 ||
                archive_entry_size(entry) > max_executable_bytes)
                throw std::runtime_error("Update archive has a duplicate or invalid executable.");
            found = true;
            file_handle file{CreateFileW(output.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                FILE_ATTRIBUTE_NORMAL, nullptr)};
            if (file.value == INVALID_HANDLE_VALUE) throw std::runtime_error(win_error("Cannot stage update executable"));
            created = true;
            std::array<std::byte, 64 * 1024> buffer;
            std::size_t total = 0;
            for (;;) {
                check_stop(stop, deadline);
                const auto count = archive_read_data(reader.get(), buffer.data(), buffer.size());
                if (count < 0) throw std::runtime_error("Update executable decompression failed.");
                if (!count) break;
                if (static_cast<std::size_t>(count) > max_executable_bytes - total)
                    throw std::runtime_error("Update executable exceeds its size limit.");
                DWORD written = 0;
                if (!WriteFile(file.value, buffer.data(), static_cast<DWORD>(count), &written, nullptr) || written != count)
                    throw std::runtime_error(win_error("Cannot write update executable"));
                total += count;
            }
            if (total != archive_entry_size(entry)) throw std::runtime_error("Update executable size is inconsistent.");
            if (!FlushFileBuffers(file.value)) throw std::runtime_error(win_error("Cannot flush update executable"));
        }
        if (!found) throw std::runtime_error("Update archive has no root SAFC.exe.");
        check_stop(stop, deadline);
        return true;
    } catch (const std::exception& exception) {
        error = exception.what();
        if (created) DeleteFileW(output.c_str());
        return false;
    }
}
bool replace_executable(const std::filesystem::path& target, const std::filesystem::path& staged,
    const std::filesystem::path& backup, std::string& error) {
    error.clear();
    if (!target.is_absolute() || !staged.is_absolute() || !backup.is_absolute() ||
        !ordinary_file(target) || !ordinary_file(staged) || std::filesystem::exists(backup) ||
        !ordinary_directory(staged.parent_path()) || !ordinary_directory(backup.parent_path()) || target == staged || target == backup) {
        error = "Update installation paths are invalid or already occupied.";
        return false;
    }
    if (ReplaceFileW(target.c_str(), staged.c_str(), backup.c_str(), REPLACEFILE_IGNORE_MERGE_ERRORS, nullptr, nullptr)) return true;
    const auto replace_error = GetLastError();
    // A loaded executable may reject ReplaceFile's access request. Windows can
    // rename its mapped image while keeping that image alive for this process.
    // Never replace an existing backup, and restore the old name on failure.
    if (!ordinary_file(target) || !ordinary_file(staged) || std::filesystem::exists(backup)) {
        if (!std::filesystem::exists(target) && ordinary_file(backup))
            MoveFileExW(backup.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH);
        error = win_error("Atomic update replacement failed", replace_error);
        return false;
    }
    if (!MoveFileExW(target.c_str(), backup.c_str(), MOVEFILE_WRITE_THROUGH)) {
        error = win_error("Cannot move the previous executable aside"); return false;
    }
    if (MoveFileExW(staged.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH)) return true;
    const auto install_error = GetLastError();
    if (!MoveFileExW(backup.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH)) {
        error = win_error("Update replacement and rollback failed; the previous executable remains in the update backup folder", install_error);
        return false;
    }
    error = win_error("Update replacement failed; the previous executable was restored", install_error);
    return false;
}
}

struct update_service::impl {
    mutable std::mutex mutex;
    update_snapshot status;
    std::filesystem::path executable;
    std::filesystem::path stage;
    std::optional<version> current;
    std::optional<version> pending;
    transport fetch;
    installer install;
    std::jthread worker;
    bool preserve_backup = false;

    explicit impl(source source) : executable(source.executable.empty() ? self_path() : std::filesystem::absolute(source.executable)),
        fetch(source.download ? std::move(source.download) : download),
        install(source.install ? std::move(source.install) : replace_executable) {
        current = executable_version(executable);
        status.current_version = current ? format_version(*current) : "Unknown";
        status.message = current ? "Updates are checked against published stable GitHub releases." : "The executable has no valid ProductVersion resource.";
    }
    void run(std::stop_token stop) {
        try {
            if (!current) throw std::runtime_error("Cannot check updates without the executable ProductVersion resource.");
            const auto metadata = fetch(release_endpoint, 1024 * 1024, stop, {});
            if (stop.stop_requested()) throw std::runtime_error("Update cancelled.");
            std::string error;
            const auto selected = select_release(std::string_view(reinterpret_cast<const char*>(metadata.data()), metadata.size()), native_machine, error);
            if (!selected) throw std::runtime_error(error);
            {
                std::lock_guard lock(mutex);
                status.available_version = selected->tag;
                if (selected->number <= *current) {
                    status.phase = update_phase::up_to_date;
                    status.message = "SAFC is up to date.";
                    return;
                }
                status.phase = update_phase::downloading;
                status.message = "Downloading " + selected->tag + "...";
            }
            const auto archive = fetch(selected->url, std::min(selected->size, max_archive_bytes), stop, [&](float progress) {
                std::lock_guard lock(mutex); status.progress = std::clamp(progress, 0.0f, 1.0f);
            });
            if (stop.stop_requested()) throw std::runtime_error("Update cancelled.");
            if (archive.size() != selected->size) throw std::runtime_error("Update archive size does not match the release metadata.");
            stage = make_stage(executable);
            if (!extract_executable(archive, stage / L"SAFC.exe", stop, error) ||
                !verify_executable(stage / L"SAFC.exe", selected->number, native_machine, error))
                throw std::runtime_error(error);
            if (stop.stop_requested()) throw std::runtime_error("Update cancelled.");
            std::lock_guard lock(mutex);
            pending = selected->number;
            status.phase = update_phase::ready;
            status.progress = 1.0f;
            status.message = selected->tag + " is ready and will be installed when SAFC closes.";
        } catch (const std::exception& exception) {
            cleanup_stage(stage); stage.clear();
            std::lock_guard lock(mutex);
            pending.reset();
            status.phase = stop.stop_requested() ? update_phase::idle : update_phase::error;
            status.message = stop.stop_requested() ? "Update cancelled." : exception.what();
            status.progress = 0.0f;
        }
    }
};
update_service::update_service() : update_service(source{}) {}
update_service::update_service(source source) : p_(std::make_unique<impl>(std::move(source))) {}
update_service::~update_service() { shutdown(); if (!p_->preserve_backup) cleanup_stage(p_->stage); }
update_snapshot update_service::snapshot() const { std::lock_guard lock(p_->mutex); return p_->status; }
void update_service::check() {
    { std::lock_guard lock(p_->mutex);
        if (p_->status.phase == update_phase::checking || p_->status.phase == update_phase::downloading || p_->status.phase == update_phase::ready) return;
    }
    if (p_->worker.joinable()) p_->worker.join();
    // Cleanup is best effort and belongs to an actual update check, never to
    // constructing the application in offline smoke or command-line modes.
    try { cleanup_previous_stages(p_->executable); } catch (...) {}
    { std::lock_guard lock(p_->mutex);
        p_->status.phase = update_phase::checking;
        p_->status.message = "Checking for updates...";
        p_->status.progress = 0.0f;
        p_->status.available_version.clear();
    }
    p_->worker = std::jthread([state = p_.get()](std::stop_token stop) { state->run(stop); });
}
void update_service::shutdown() {
    if (p_->worker.joinable()) { p_->worker.request_stop(); p_->worker.join(); }
}
void update_service::cancel() {
    shutdown();
    if (!p_->preserve_backup) cleanup_stage(p_->stage);
    p_->stage.clear();
    p_->preserve_backup = false;
    std::lock_guard lock(p_->mutex);
    p_->pending.reset();
    p_->status.phase = update_phase::idle;
    p_->status.progress = 0.0f;
    p_->status.message = "Update cancelled.";
}
bool update_service::install_pending(std::string& error) {
    shutdown();
    error.clear();
    if (!p_->pending) return false;
    if (!verify_executable(p_->stage / L"SAFC.exe", *p_->pending, native_machine, error)) return false;
    if (!p_->install(p_->executable, p_->stage / L"SAFC.exe", p_->stage / L"previous.exe", error)) {
        p_->preserve_backup = ordinary_file(p_->stage / L"previous.exe");
        return false;
    }
    // Without this marker a failed rollback backup is never eligible for the
    // later-launch cleanup scan. Failure to write it simply retains the backup.
    std::ofstream installed_marker(p_->stage / L"installed.txt", std::ios::binary);
    installed_marker << format_version(*p_->pending);
    installed_marker.close();
    p_->preserve_backup = !installed_marker;
    std::lock_guard lock(p_->mutex);
    p_->current = p_->pending;
    p_->pending.reset();
    p_->status.current_version = format_version(*p_->current);
    p_->status.phase = update_phase::up_to_date;
    p_->status.message = "Update installed. It will run the next time SAFC starts.";
    return true;
}
std::filesystem::path update_service::executable_path() const { return p_->executable; }
}
