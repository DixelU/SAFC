#pragma once
#include "../app/project_model.h"
#include "preferences.h"
#include <memory>
#include <string>
#include <vector>

namespace safc::imgui_ui
{
struct merge_progress
{
    bool busy = false, cancelling = false;
    float fraction = 0;
    double seconds = 0;
    std::string stage, error;
    struct item { std::string file, message; float fraction{}; };
    std::vector<item> files;
};

// UI owns project editing. Workers consume snapshots and publish results.
class project_session
{
public:
    project_session();
    ~project_session();
    project_session(const project_session&) = delete;
    safc_data data;
    void set_defaults(const application_preferences&, bool apply_to_files = false);
    void add_files(std::vector<std::wstring>);
    void poll();
    bool loading() const;
    bool merging() const;
    std::string message() const;
    std::uint64_t id_at(std::size_t) const;
    file_settings* find(std::uint64_t);
    void remove(const std::vector<std::uint64_t>&);
    bool start_merge();
    void cancel_merge();
    merge_progress progress() const;
    void shutdown();
    bool run_smoke(const std::wstring& directory, std::string& report);
private:
    struct impl;
    std::unique_ptr<impl> state_;
};
}
