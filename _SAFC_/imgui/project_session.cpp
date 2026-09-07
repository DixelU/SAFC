#define NOMINMAX
#include "project_session.h"
#include <Windows.h>
#include <atomic>
#include <chrono>
#include <fstream>
#include <mutex>
#include <limits>
#include <thread>
#include <unordered_set>

namespace safc::imgui_ui
{
namespace
{
std::string utf8_path(const std::filesystem::path& path)
{
    const auto value = path.u8string(); return {value.begin(), value.end()};
}
bool same_path(const std::filesystem::path& a, const std::filesystem::path& b)
{
    auto resolved = [](const std::filesystem::path& path)
    {
        const auto absolute = std::filesystem::absolute(path).lexically_normal();
        std::error_code error;
        const auto canonical = std::filesystem::canonical(absolute, error);
        if (!error) return canonical;
        // Resolve the containing directory for a Save As filename that has not
        // been created yet (Windows may deny opening that missing leaf).
        return std::filesystem::canonical(absolute.parent_path()) / absolute.filename();
    };
    const auto first = resolved(a).wstring();
    const auto second = resolved(b).wstring();
    return _wcsicmp(first.c_str(), second.c_str()) == 0;
}
}

struct project_session::impl
{
    std::jthread loader, merger;
    std::atomic_bool loading{false}, merging{false};
    mutable std::mutex mutex;
    std::vector<file_settings> loaded;
    std::vector<std::uint64_t> ids;
    std::uint64_t next_id = 1;
    application_preferences defaults;
    merge_progress progress;
    std::string message;
    std::shared_ptr<midi_collection_threaded_merger> active;
    void status(std::string stage, std::string error = {})
    {
        std::lock_guard lock(mutex); progress.stage = std::move(stage); progress.error = std::move(error);
    }
};

project_session::project_session() : state_(std::make_unique<impl>()) {}
project_session::~project_session() { shutdown(); }
bool project_session::loading() const { return state_->loading.load(std::memory_order_acquire); }
bool project_session::merging() const { return state_->merging.load(std::memory_order_acquire); }
std::string project_session::message() const { std::lock_guard lock(state_->mutex); return state_->message; }
std::uint64_t project_session::id_at(std::size_t index) const { return state_->ids.at(index); }
file_settings* project_session::find(std::uint64_t id)
{
    const auto it = std::find(state_->ids.begin(), state_->ids.end(), id);
    return it == state_->ids.end() ? nullptr : &data.files[it - state_->ids.begin()];
}

void project_session::set_defaults(const application_preferences& p, bool apply)
{
    state_->defaults = p;
    data.detected_threads = p.processing_threads;
    data.channels_split = p.split_channels; data.collapse_midi = p.collapse_tracks;
    data.apply_offset_after = p.apply_offset_after; data.inplace_merge_flag = p.inplace_merge;
    data.rsb_compression = p.rsb_compression;
    if (apply && !merging()) for (auto& file : data.files)
    {
        file.bool_settings = p.processing_flags; file.channels_split = p.split_channels;
        file.collapse_midi = p.collapse_tracks; file.apply_offset_after = p.apply_offset_after;
        file.inplace_merge_enabled = p.inplace_merge; file.rsb_compression = p.rsb_compression;
        file.allow_sysex = p.allow_sysex;
    }
}

void project_session::add_files(std::vector<std::wstring> paths)
{
    if (loading() || merging() || paths.empty()) return;
    poll();
    if (state_->loader.joinable()) state_->loader.join();
    state_->loading.store(true, std::memory_order_release);
    const auto defaults = state_->defaults;
    const auto tempo = data.global_new_tempo;
    const auto offset = data.global_offset;
    try { state_->loader = std::jthread([s = state_.get(), paths = std::move(paths), defaults, tempo, offset](std::stop_token stop)
    {
        std::vector<file_settings> loaded;
        std::string errors;
        try
        {
            for (const auto& path : paths)
            {
                if (stop.stop_requested()) break;
                file_settings file(path, defaults.processing_flags);
                if (!file.is_midi) { errors += "Not an accessible MIDI: " + utf8_path(path) + "\n"; continue; }
                file.new_tempo = tempo; file.offset_ticks = offset;
                file.channels_split = defaults.split_channels; file.collapse_midi = defaults.collapse_tracks;
                file.apply_offset_after = defaults.apply_offset_after; file.inplace_merge_enabled = defaults.inplace_merge;
                file.rsb_compression = defaults.rsb_compression; file.allow_sysex = defaults.allow_sysex;
                loaded.push_back(std::move(file));
            }
        }
        catch (const std::exception& error) { errors += error.what(); }
        {
            std::lock_guard lock(s->mutex); s->loaded = std::move(loaded); s->message = std::move(errors);
        }
        s->loading.store(false, std::memory_order_release);
    }); }
    catch (...) { state_->loading.store(false, std::memory_order_release); throw; }
}

void project_session::poll()
{
    std::vector<file_settings> loaded;
    { std::lock_guard lock(state_->mutex); loaded.swap(state_->loaded); }
    if (loaded.empty()) return;
    for (auto& file : loaded)
    {
        state_->ids.push_back(state_->next_id++);
        data.files.push_back(std::move(file));
    }
    const auto save = data.save_path;
    data.set_global_ppqn();
    data.resolve_subdivision_problem_group_id_assign();
    if (!save.empty()) data.save_path = save;
}

void project_session::remove(const std::vector<std::uint64_t>& ids)
{
    if (loading() || merging()) return;
    const std::unordered_set<std::uint64_t> selected(ids.begin(), ids.end());
    for (std::size_t i = data.files.size(); i-- > 0;)
        if (selected.contains(state_->ids[i]))
        {
            data.files.erase(data.files.begin() + i); state_->ids.erase(state_->ids.begin() + i);
        }
    data.set_global_ppqn();
}

bool project_session::start_merge()
{
    if (loading() || merging() || data.files.empty() || data.save_path.empty()) return false;
    for (const auto& file : data.files)
    {
        const auto limit = std::numeric_limits<std::int64_t>::max();
        if (!file.new_ppqn || file.selection_start < 0 || file.group_id < 0 || !std::isfinite(file.new_tempo) ||
            file.new_tempo < 0 || file.new_tempo > 60000000. || file.offset_ticks == std::numeric_limits<std::int64_t>::min())
        { state_->status("Invalid processing settings", "Use positive PPQN, nonnegative selection start/group, tempo 0..60000000, and an offset above INT64_MIN."); return false; }
        const auto effective_begin = std::max(file.selection_start, file.offset_ticks < 0 ? -file.offset_ticks : 0);
        if ((file.selection_length > 0 && effective_begin > limit - file.selection_length) ||
            (file.selection_length < 0 && file.selection_length < std::numeric_limits<std::int64_t>::min() + effective_begin))
        { state_->status("Invalid processing settings", "Selection arithmetic would overflow the tick range."); return false; }
        if (same_path(file.filename, data.save_path))
        { state_->status("Choose a different output", "The merge output must not overwrite an input MIDI."); return false; }
        if (file.w_file_name_postfix.find_first_of(L"\\/:*?\"<>|") != std::wstring::npos)
        { state_->status("Invalid intermediate suffix", "Use a filename suffix without path separators or reserved characters."); return false; }
    }
    if (state_->merger.joinable()) state_->merger.join();
    auto snapshot = data;
    for (auto& file : snapshot.files)
    {
        if (file.key_map) file.key_map = std::make_shared<cut_and_transpose>(*file.key_map);
        if (file.volume_map) file.volume_map = std::make_shared<decltype(file.volume_map)::element_type>(*file.volume_map);
        if (file.pitch_bend_map) file.pitch_bend_map = std::make_shared<decltype(file.pitch_bend_map)::element_type>(*file.pitch_bend_map);
    }
    { std::lock_guard lock(state_->mutex); state_->progress = {}; state_->progress.busy = true; }
    state_->merging.store(true, std::memory_order_release);
    try { state_->merger = std::jthread([s = state_.get(), snapshot = std::move(snapshot)](std::stop_token stop) mutable
    {
        const auto began = std::chrono::steady_clock::now();
        std::filesystem::path work, work_parent;
        bool owns_work = false;
        try
        {
            const auto output = std::filesystem::absolute(snapshot.save_path);
            work_parent = std::filesystem::weakly_canonical(output.parent_path());
            work = work_parent / (L".safc-work-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()));
            if (!std::filesystem::create_directory(work)) throw std::runtime_error("Cannot create merge work directory");
            owns_work = true;
            std::vector<midi_collection_threaded_merger::proc_data_ptr> requests;
            for (std::size_t i = 0; i < snapshot.files.size(); ++i)
            {
                const auto& file = snapshot.files[i];
                fast_midi_checker current(file.filename);
                if (!current.is_acssessible || !current.is_midi)
                    throw std::runtime_error("Input MIDI is no longer accessible: " + utf8_path(file.filename));
                if (current.PPQN != file.old_ppqn || current.filesize != file.filesize)
                    throw std::runtime_error("Input MIDI changed since it was added; remove and add it again: " + utf8_path(file.filename));
                auto request = snapshot.files[i].build_smrp_processing_data();
                const auto part_name = std::to_wstring(i) + L"-" + std::filesystem::path(file.filename).filename().wstring() + file.w_file_name_postfix;
                request->output_filename = (work / part_name).wstring();
                requests.push_back(std::move(request));
            }
            const auto temporary_output = work / "merged.mid";
            auto merger = std::make_shared<midi_collection_threaded_merger>(requests, snapshot.global_ppqn, temporary_output.wstring(), false);
            std::stop_callback cancellation(stop, [merger] { merger->request_cancel(); });
            { std::lock_guard lock(s->mutex); s->active = merger; }
            auto update = [&](const char* phase)
            {
                merge_progress progress;
                progress.busy = true; progress.cancelling = stop.stop_requested(); progress.stage = phase;
                progress.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count();
                for (const auto& [file, buffers] : merger->snapshot_currently_processed()) if (file && buffers)
                {
                    const auto bytes = buffers->last_input_position.load(std::memory_order_acquire);
                    const float ratio = file->settings.details.initial_filesize ? std::min(1., double(bytes) / file->settings.details.initial_filesize) : 0;
                    progress.files.push_back({file->appearance_filename, buffers->log->get_last(), ratio});
                }
                for (const auto& item : progress.files) progress.fraction += item.fraction;
                if (!progress.files.empty()) progress.fraction /= progress.files.size();
                std::lock_guard lock(s->mutex); s->progress = std::move(progress);
            };
            auto cancelled = [&] { if (stop.stop_requested()) throw std::runtime_error("Merge cancelled"); };
            s->status("Processing MIDI files"); merger->start_processing();
            while (!merger->is_smrp_complete()) { update("Processing MIDI files"); std::this_thread::sleep_for(std::chrono::milliseconds(40)); }
            merger->wait_processing();
            cancelled();
            if (merger->has_failed()) throw std::runtime_error(merger->failure_message());
            merger->start_ri_merge();
            while (!merger->is_ri_merge_complete()) { update("Merging tracks"); std::this_thread::sleep_for(std::chrono::milliseconds(40)); }
            cancelled();
            if (merger->has_failed()) throw std::runtime_error(merger->failure_message());
            merger->start_final_merge();
            while (!merger->complete) { update("Assembling output"); std::this_thread::sleep_for(std::chrono::milliseconds(40)); }
            cancelled();
            if (merger->has_failed()) throw std::runtime_error(merger->failure_message());
            // Only a completed result replaces the destination selected by the user.
            if (!MoveFileExW(temporary_output.c_str(), output.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                throw std::runtime_error("Cannot install merge output: Windows error " + std::to_string(GetLastError()));
            const bool retain = std::any_of(snapshot.files.begin(), snapshot.files.end(), [](const file_settings& f)
                { return !(f.bool_settings & remove_remnants); });
            if (retain)
            {
                const auto retained = work_parent / (output.filename().wstring() + L".parts-" + std::to_wstring(GetTickCount64()));
                // Retained intermediate files get a unique destination; never replace an existing folder.
                if (MoveFileExW(work.c_str(), retained.c_str(), MOVEFILE_WRITE_THROUGH))
                { owns_work = false; s->status("Merge complete. Intermediate files: " + utf8_path(retained)); }
                else { owns_work = false; s->status("Merge complete. Intermediate files: " + utf8_path(work)); }
            }
            else s->status("Merge complete");
        }
        catch (const std::exception& error) { s->status(stop.stop_requested() ? "Merge cancelled" : "Merge failed", stop.stop_requested() ? "" : error.what()); }
        {
            std::shared_ptr<midi_collection_threaded_merger> retire;
            { std::lock_guard lock(s->mutex); retire = std::move(s->active); }
            retire.reset(); // Join all core work before removing its temporary files.
        }
        if (owns_work && work.is_absolute() && work.parent_path() == work_parent &&
            work.filename().wstring().starts_with(L".safc-work-"))
        { std::error_code error; std::filesystem::remove_all(work, error); }
        { std::lock_guard lock(s->mutex); s->progress.busy = false; s->progress.seconds = std::chrono::duration<double>(std::chrono::steady_clock::now() - began).count(); }
        s->merging.store(false, std::memory_order_release);
    }); }
    catch (...) { state_->merging.store(false, std::memory_order_release); std::lock_guard lock(state_->mutex); state_->progress.busy = false; throw; }
    return true;
}

void project_session::cancel_merge() { state_->merger.request_stop(); }
merge_progress project_session::progress() const { std::lock_guard lock(state_->mutex); return state_->progress; }
void project_session::shutdown()
{
    state_->loader.request_stop(); state_->merger.request_stop();
    if (state_->loader.joinable()) state_->loader.join();
    if (state_->merger.joinable()) state_->merger.join();
}

bool project_session::run_smoke(const std::wstring& directory, std::string& report)
{
    try
    {
        const auto input = std::filesystem::path(directory) / "project-input.mid";
        const unsigned char midi[] = {'M','T','h','d',0,0,0,6,0,0,0,1,1,0xe0,'M','T','r','k',0,0,0,13,0,0x90,60,100,0x83,0x60,0x80,60,0,0,0xff,0x2f,0};
        { std::ofstream out(input, std::ios::binary); out.write(reinterpret_cast<const char*>(midi), sizeof(midi)); }
        add_files({input.wstring(), input.wstring()});
        while (loading()) std::this_thread::sleep_for(std::chrono::milliseconds(2));
        poll();
        if (data.files.size() != 2 || id_at(0) == id_at(1)) throw std::runtime_error("Project did not assign stable unique file identities");
        const auto survivor = id_at(1);
        remove({id_at(0)});
        if (!find(survivor) || data.files.size() != 1) throw std::runtime_error("Project selection changed identity after removal");
        data.save_path = (std::filesystem::path(directory) / "project-merged.mid").wstring();
        data.files[0].key_map = std::make_shared<cut_and_transpose>(0, 127, 12);
        if (!start_merge()) throw std::runtime_error("Merge did not start");
        while (merging()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        if (!progress().error.empty()) throw std::runtime_error(progress().error);
        fast_midi_checker check(data.save_path);
        if (!check.is_midi) throw std::runtime_error("Merge result is not a MIDI");
        std::ifstream result(data.save_path, std::ios::binary);
        const std::string bytes((std::istreambuf_iterator<char>(result)), {});
        result.close();
        const std::string transposed{char(0x90), char(72)};
        if (bytes.find(transposed) == std::string::npos) throw std::runtime_error("Processing key map was not applied");
        const auto original_input = [&] { std::ifstream in(input, std::ios::binary); return std::string(std::istreambuf_iterator<char>(in), {}); }();
        if (original_input != std::string(reinterpret_cast<const char*>(midi), sizeof(midi))) throw std::runtime_error("Merge modified its source input");
        const auto saved_bytes = bytes;
        if (!start_merge()) throw std::runtime_error("Cancellation merge did not start");
        cancel_merge();
        while (merging()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        auto read_output = [&] { std::ifstream in(data.save_path, std::ios::binary); return std::string(std::istreambuf_iterator<char>(in), {}); };
        if (progress().stage != "Merge cancelled" || read_output() != saved_bytes) throw std::runtime_error("Cancelled merge changed the destination");
        const auto old_filename = data.files[0].filename;
        data.files[0].filename = (std::filesystem::path(directory) / "missing-project-input.mid").wstring();
        if (!start_merge()) throw std::runtime_error("Failure test did not start");
        while (merging()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        data.files[0].filename = old_filename;
        if (progress().error.empty() || read_output() != saved_bytes) throw std::runtime_error("Failed merge changed the destination");
        for (const auto& entry : std::filesystem::directory_iterator(directory))
            if (entry.path().filename().wstring().starts_with(L".safc-work-")) throw std::runtime_error("Merge left a temporary work directory");
        // File retention is a visible processing option; a successful run must honor it.
        data.files[0].bool_settings &= ~std::uint32_t(remove_remnants);
        if (!start_merge()) throw std::runtime_error("Retained-parts merge did not start");
        while (merging()) std::this_thread::sleep_for(std::chrono::milliseconds(5));
        if (!progress().error.empty()) throw std::runtime_error(progress().error);
        bool retained = false;
        for (const auto& entry : std::filesystem::directory_iterator(directory))
            if (entry.path().filename().wstring().starts_with(L"project-merged.mid.parts-") &&
                std::filesystem::exists(entry.path() / "0-project-input.mid_.mid")) retained = true;
        if (!retained) throw std::runtime_error("Keep intermediate files option was ignored");
        data.files[0].bool_settings |= std::uint32_t(remove_remnants);
        report = "Project identity/removal, transpose merge, source/output preservation on cancel/failure, cleanup and retained intermediate files passed";
        return true;
    }
    catch (const std::exception& error) { report = error.what(); return false; }
}
}
