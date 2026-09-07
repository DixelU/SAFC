#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stop_token>
#include <string>
#include <string_view>
#include <vector>

namespace safc::imgui_ui {

enum class update_phase { idle, checking, downloading, ready, up_to_date, error };
struct update_snapshot {
    update_phase phase = update_phase::idle;
    std::string current_version;
    std::string available_version;
    std::string message;
    float progress = 0.0f;
};

// Pure parsing and file operations are exposed for offline regression tests.
// The injected transport never changes the production release endpoint.
namespace update_detail {
using version = std::array<std::uint16_t, 4>;
constexpr std::size_t max_archive_bytes = 128u * 1024u * 1024u;
constexpr std::size_t max_executable_bytes = 256u * 1024u * 1024u;
constexpr std::uint16_t machine_x86 = 0x014c;
constexpr std::uint16_t machine_x64 = 0x8664;
struct release {
    version number{};
    std::string tag;
    std::wstring url;
    std::size_t size = 0;
};
using progress_callback = std::function<void(float)>;
using transport = std::function<std::vector<std::byte>(std::wstring_view,
    std::size_t, std::stop_token, const progress_callback&)>;
using installer = std::function<bool(const std::filesystem::path&, const std::filesystem::path&,
    const std::filesystem::path&, std::string&)>;
struct source {
    std::filesystem::path executable;
    transport download;
    installer install;
};
std::optional<version> parse_version(std::string_view text);
std::string format_version(const version& value);
std::optional<release> select_release(std::string_view json, std::uint16_t machine,
    std::string& error);
std::string fetch_release_metadata(std::stop_token stop = {});
std::optional<version> executable_version(const std::filesystem::path& executable);
bool verify_executable(const std::filesystem::path& executable, const version& expected,
    std::uint16_t machine, std::string& error);
bool extract_executable(std::span<const std::byte> archive, const std::filesystem::path& output,
    std::stop_token stop, std::string& error);
bool replace_executable(const std::filesystem::path& target,
    const std::filesystem::path& staged, const std::filesystem::path& backup,
    std::string& error);
}

class update_service {
public:
    update_service();
    explicit update_service(update_detail::source source);
    ~update_service();
    update_service(const update_service&) = delete;
    update_service& operator=(const update_service&) = delete;
    update_snapshot snapshot() const;
    void check();
    void cancel();
    // Stop in-flight work, but preserve a completed download for install_pending.
    void shutdown();
    bool install_pending(std::string& error);
    std::filesystem::path executable_path() const;
private:
    struct impl;
    std::unique_ptr<impl> p_;
};
}
