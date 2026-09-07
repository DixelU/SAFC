#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>

#include "analysis_panel.h"
#include "folded_theme.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>

namespace safc::imgui_ui
{
namespace
{
struct collection_job
{
    std::shared_ptr<single_midi_info_collector> collector;
    mutable std::mutex mutex;
    std::shared_ptr<const analysis_result> result;
    std::string status = "Reading MIDI...";
    std::atomic_bool done{false};
};

struct export_job
{
    mutable std::mutex mutex;
    std::string status = "Exporting...";
    std::atomic_bool done{false};
};

std::string utf8(const std::wstring& input)
{
    if (input.empty()) return {};
    const int length = WideCharToMultiByte(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), nullptr, 0, nullptr, nullptr);
    std::string result(length, '\0');
    WideCharToMultiByte(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), result.data(), length, nullptr, nullptr);
    return result;
}

template<class Graph>
double exact_value(const Graph& graph, std::int64_t tick)
{
    if (graph.empty()) return 0;
    auto it = graph.upper_bound(tick);
    if (it != graph.begin()) --it;
    return double(it->second);
}

// Each bin emits first/min/max/last in time order. Peaks survive decimation,
// and draw cost is bounded independently of the MIDI event count. Tooltips
// still query the original graph at the exact hovered tick.
template<class Graph>
std::vector<analysis_point> envelope(const Graph& graph, std::int64_t end, std::stop_token stop)
{
    constexpr std::size_t bin_count = 8192;
    struct bin
    {
        bool used = false;
        analysis_point first, last, minimum, maximum;
    };
    std::vector<bin> bins(bin_count);
    for (const auto& [tick, raw_value] : graph)
    {
        if (stop.stop_requested()) return {};
        const double value = double(raw_value);
        if (!std::isfinite(value)) throw std::runtime_error("MIDI contains an invalid zero tempo.");
        const std::size_t index = std::min(bin_count - 1,
            static_cast<std::size_t>(std::max(0., double(tick)) / std::max(1., double(end)) * (bin_count - 1)));
        auto& b = bins[index];
        const analysis_point p{tick, value};
        if (!b.used) { b.used = true; b.first = b.minimum = b.maximum = p; }
        b.last = p;
        if (value < b.minimum.value) b.minimum = p;
        if (value > b.maximum.value) b.maximum = p;
    }
    std::vector<analysis_point> result;
    result.reserve(bin_count * 4);
    for (const auto& b : bins)
    {
        if (!b.used) continue;
        std::array<analysis_point, 4> points{b.first, b.minimum, b.maximum, b.last};
        std::sort(points.begin(), points.end(), [](const auto& a, const auto& b) { return a.tick < b.tick; });
        for (const auto& p : points)
            if (result.empty() || result.back().tick != p.tick) result.push_back(p);
    }
    return result;
}

void validate_header(const std::wstring& path)
{
    midi_file_reader reader(path);
    if (!reader.good()) throw std::runtime_error("Cannot open MIDI for analysis.");
    std::array<unsigned char, 14> header{};
    for (auto& c : header)
    {
        const auto byte = reader.get();
        if (!byte) throw std::runtime_error("The MIDI header is incomplete.");
        c = std::to_integer<unsigned char>(*byte);
    }
    if (header[0] != 'M' || header[1] != 'T' || header[2] != 'h' || header[3] != 'd')
        throw std::runtime_error("Select an uncompressed MIDI file for analysis.");
    if (header[4] || header[5] || header[6] || header[7] != 6)
        throw std::runtime_error("This analyzer requires the standard six-byte MIDI header.");
    if (header[12] & 0x80) throw std::runtime_error("SMPTE time division is not supported by this analyzer.");
    if (!header[12] && !header[13]) throw std::runtime_error("MIDI ticks per quarter note must be nonzero.");
}

std::shared_ptr<analysis_result> make_result(std::shared_ptr<single_midi_info_collector> source, std::stop_token stop)
{
    if (source->tracks.empty()) throw std::runtime_error("No complete MIDI tracks were found.");
    auto result = std::make_shared<analysis_result>();
    result->source = source;
    result->last_tick = std::max<std::int64_t>(0, source->tempo_map.rbegin()->first - 1);
    double seconds = 0;
    double seconds_per_tick = .5 / source->ppq;
    std::int64_t previous_tick = 0;
    for (const auto& [tick, tempo] : source->tempo_map)
    {
        if (stop.stop_requested()) return {};
        if (!tempo.get_raw()) throw std::runtime_error("MIDI contains an invalid zero tempo.");
        seconds += (tick - previous_tick) * seconds_per_tick;
        seconds_per_tick = double(tempo.get_raw()) / (1000000. * source->ppq);
        result->timing.push_back({tick, seconds, seconds_per_tick, double(tempo)});
        previous_tick = tick;
    }
    result->duration_seconds = result->ticks_to_seconds(result->last_tick);
    result->tempo_plot = envelope(source->tempo_map, result->last_tick, stop);
    result->polyphony_plot = envelope(source->polyphony, result->last_tick, stop);
    result->nps_plot = envelope(source->notes_per_second, result->last_tick, stop);
    for (const auto& p : result->polyphony_plot) result->peak_polyphony = std::max(result->peak_polyphony, std::int64_t(p.value));
    for (const auto& p : result->nps_plot) result->peak_nps = std::max(result->peak_nps, std::int64_t(p.value));
    return stop.stop_requested() ? nullptr : result;
}

template<class Function>
void visit_rows(const analysis_result& result, std::stop_token stop, Function&& function)
{
    auto poly = result.source->polyphony.begin();
    auto tempo = result.source->tempo_map.begin();
    const auto poly_end = result.source->polyphony.end();
    const auto tempo_end = result.source->tempo_map.end();
    std::int64_t current_poly = 0;
    double bpm = 120;
    while (poly != poly_end || tempo != tempo_end)
    {
        if (stop.stop_requested()) throw std::runtime_error("Export cancelled.");
        const auto tick = std::min(poly != poly_end ? poly->first : INT64_MAX,
            tempo != tempo_end ? tempo->first : INT64_MAX);
        if (poly != poly_end && poly->first == tick) { current_poly = poly->second; ++poly; }
        if (tempo != tempo_end && tempo->first == tick) { bpm = double(tempo->second); ++tempo; }
        // The collector's synthetic -1 row describes the empty initial state.
        // Preserve its legacy export time of zero rather than extrapolating
        // backwards to a negative timestamp.
        function(tick, current_poly, result.ticks_to_seconds(std::max<std::int64_t>(0, tick)), bpm);
    }
}
}

double analysis_result::ticks_to_seconds(std::int64_t tick) const
{
    if (timing.empty()) return 0;
    auto it = std::upper_bound(timing.begin(), timing.end(), tick,
        [](auto t, const analysis_tempo& p) { return t < p.tick; });
    if (it != timing.begin()) --it;
    return it->seconds + (tick - it->tick) * it->seconds_per_tick;
}

std::int64_t analysis_result::seconds_to_ticks(double seconds) const
{
    if (timing.empty() || !std::isfinite(seconds)) return 0;
    auto it = std::upper_bound(timing.begin(), timing.end(), seconds,
        [](auto time, const analysis_tempo& p) { return time < p.seconds; });
    if (it != timing.begin()) --it;
    long double tick = it->tick + (static_cast<long double>(seconds) - it->seconds) / it->seconds_per_tick;
    // A time produced from an exact tick must survive its return trip. Correct
    // floating-point noise only at integer boundaries; other times round down.
    const long double nearest = std::round(tick);
    const long double tolerance = (std::abs(tick) + 1) * std::numeric_limits<double>::epsilon() * 8;
    if (std::abs(tick - nearest) <= tolerance) tick = nearest;
    if (tick >= static_cast<long double>(INT64_MAX)) return INT64_MAX;
    return static_cast<std::int64_t>(std::max(0.L, tick));
}

struct analysis_panel::impl
{
    native_dialogs dialogs;
    std::shared_ptr<collection_job> job;
    std::jthread worker;
    std::shared_ptr<export_job> exporting;
    std::jthread export_worker;
    std::wstring filename;
    std::function<void(single_midi_info_collector::time_graph)> commit;
    bool result_initialized = false;
    bool show_tempo = true, show_poly = true, show_nps = true;
    bool seconds_axis = false, auto_scale = true;
    double manual_max = 1000;
    double view_start = 0, view_end = 1;
    std::int64_t selected_tick = 0;
    double input_seconds = 0;
    std::int64_t input_tick = 0;
    std::string conversion;
    std::array<char, 16> delimiter{';', '\0'};
    int export_format = 0;
    std::string message;

    void start_export(std::shared_ptr<const analysis_result> result, bool tempo_only, bool nps_only)
    {
        if (exporting && !exporting->done.load(std::memory_order_acquire)) return;
        std::wstring suffix = tempo_only ? L".tg.csv" : nps_only ? L".nps.csv" : export_format == 0 ? L".a.csv" : L".atraw";
        std::wstring path;
        try { path = dialogs.save_data ? dialogs.save_data(filename + suffix) : std::wstring{}; }
        catch (const std::exception& error) { message = error.what(); return; }
        if (path.empty()) { if (!dialogs.save_data) message = "No save dialog is configured."; return; }
        const std::string delim = delimiter[0] ? delimiter.data() : ";";
        const bool binary = !tempo_only && !nps_only && export_format == 1;
        run_export(std::move(result), path, delim, binary, tempo_only, nps_only);
    }

    void run_export(std::shared_ptr<const analysis_result> result, const std::wstring& path,
        const std::string& delim, bool binary, bool tempo_only, bool nps_only)
    {
        exporting = std::make_shared<export_job>();
        export_worker = std::jthread([state = exporting, result = std::move(result), path, delim, binary, tempo_only, nps_only](std::stop_token stop)
        {
            auto temporary = std::filesystem::path(path);
            temporary += L".safc-analysis-" + std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count()) + L".tmp";
            std::string status;
            try
            {
                const auto destination = std::filesystem::absolute(path).lexically_normal();
                const auto source = std::filesystem::absolute(result->source->filename).lexically_normal();
                const auto normalized_output = std::filesystem::weakly_canonical(destination.parent_path()) / destination.filename();
                const auto normalized_source = std::filesystem::weakly_canonical(source.parent_path()) / source.filename();
                if (_wcsicmp(normalized_output.c_str(), normalized_source.c_str()) == 0)
                    throw std::runtime_error("Analysis data must not overwrite the source MIDI.");
                std::ofstream output(temporary, std::ios::out | std::ios::binary | std::ios::trunc);
                if (!output) throw std::runtime_error("Cannot create the export file.");
                output << std::setprecision(17);
                if (tempo_only)
                {
                    output << "tick" << delim << "tempo\n";
                    for (const auto& [tick, value] : result->source->tempo_map)
                    {
                        if (stop.stop_requested()) throw std::runtime_error("Export cancelled.");
                        output << tick << delim << double(value) << '\n';
                    }
                }
                else if (nps_only)
                {
                    output << "tick" << delim << "nps\n";
                    for (const auto& [tick, value] : result->source->notes_per_second)
                    {
                        if (stop.stop_requested()) throw std::runtime_error("Export cancelled.");
                        output << tick << delim << value << '\n';
                    }
                }
                else
                {
                    if (!binary) output << "Tick" << delim << "Polyphony" << delim << "Time(seconds)" << delim << "Tempo\n";
                    visit_rows(*result, stop, [&](std::int64_t tick, std::int64_t poly, double seconds, double tempo)
                    {
                        if (binary)
                        {
                            output.write(reinterpret_cast<const char*>(&tick), 8);
                            output.write(reinterpret_cast<const char*>(&poly), 8);
                            output.write(reinterpret_cast<const char*>(&seconds), 8);
                            output.write(reinterpret_cast<const char*>(&tempo), 8);
                        }
                        else output << tick << delim << poly << delim << seconds << delim << tempo << '\n';
                    });
                }
                output.flush();
                if (!output) throw std::runtime_error("Writing the export failed; check free space and permissions.");
                output.close();
                if (stop.stop_requested()) throw std::runtime_error("Export cancelled.");
                if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
                    throw std::runtime_error("Could not replace the destination file (Windows error " + std::to_string(GetLastError()) + ").");
                status = "Export saved: " + utf8(path);
            }
            catch (const std::exception& error) { status = error.what(); }
            std::error_code ignored;
            std::filesystem::remove(temporary, ignored);
            { std::lock_guard lock(state->mutex); state->status = std::move(status); }
            state->done.store(true, std::memory_order_release);
        });
    }

    template<class Graph>
    void plot(const char* label, const std::vector<analysis_point>& points, const Graph& exact,
        ImU32 color, const analysis_result& data)
    {
        ImGui::TextUnformatted(label);
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 size(std::max(180.f, ImGui::GetContentRegionAvail().x), 135);
        ImGui::InvisibleButton(label, size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
        auto* draw = ImGui::GetWindowDrawList();
        const ImVec2 corner(origin.x + size.x, origin.y + size.y);
        draw->AddRectFilled(origin, corner, IM_COL32(7, 19, 30, 255));
        const auto to_axis = [&](std::int64_t tick) { return seconds_axis ? data.ticks_to_seconds(tick) : double(tick); };
        const auto to_tick = [&](double x)
        {
            if (seconds_axis) return data.seconds_to_ticks(x);
            if (x >= static_cast<double>(INT64_MAX)) return INT64_MAX;
            return static_cast<std::int64_t>(std::max(0., x));
        };
        const double span = std::max(.000001, view_end - view_start);
        auto first = std::lower_bound(points.begin(), points.end(), to_tick(view_start),
            [](const analysis_point& p, auto tick) { return p.tick < tick; });
        if (first != points.begin()) --first;
        double maximum = auto_scale ? 1 : std::max(1., manual_max);
        if (auto_scale) for (auto it = first; it != points.end() && to_axis(it->tick) <= view_end; ++it)
            maximum = std::max(maximum, it->value);
        if (auto_scale) maximum *= 1.05;
        const auto position = [&](double x, double y)
        {
            return ImVec2(origin.x + float((x - view_start) / span) * size.x,
                corner.y - float(std::clamp(y / maximum, 0., 1.)) * (size.y - 9));
        };
        for (int i = 1; i < 4; ++i)
            draw->AddLine(ImVec2(origin.x, origin.y + size.y * i / 4), ImVec2(corner.x, origin.y + size.y * i / 4), IM_COL32(34, 56, 74, 190));
        draw->PushClipRect(origin, corner, true);
        ImVec2 previous = position(view_start, exact_value(exact, to_tick(view_start)));
        for (auto it = first; it != points.end(); ++it)
        {
            const double x = to_axis(it->tick);
            if (x < view_start) continue;
            if (x > view_end) break;
            const auto current = position(x, it->value);
            draw->AddLine(previous, ImVec2(current.x, previous.y), color, 1.3f);
            draw->AddLine(ImVec2(current.x, previous.y), current, color, 1.3f);
            previous = current;
        }
        draw->AddLine(previous, ImVec2(corner.x, previous.y), color, 1.3f);
        const float selection_x = position(to_axis(selected_tick), 0).x;
        draw->AddRectFilled(origin, ImVec2(selection_x, corner.y), IM_COL32(43, 125, 207, 22));
        draw->AddLine(ImVec2(selection_x, origin.y), ImVec2(selection_x, corner.y), IM_COL32(214, 181, 77, 230));
        if (ImGui::IsItemHovered() || ImGui::IsItemActive())
        {
            const auto mouse = ImGui::GetIO().MousePos;
            const double ratio = std::clamp(double(mouse.x - origin.x) / size.x, 0., 1.);
            const auto tick = to_tick(view_start + ratio * span);
            draw->AddLine(ImVec2(mouse.x, origin.y), ImVec2(mouse.x, corner.y), IM_COL32(150, 190, 220, 150));
            ImGui::SetTooltip("Tick %lld | %.6f s | %s %.6g", static_cast<long long>(tick), data.ticks_to_seconds(tick), label, exact_value(exact, tick));
            if (ImGui::IsItemClicked(ImGuiMouseButton_Left) ||
                (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left))) selected_tick = input_tick = tick;
            if (ImGui::IsItemHovered() && ImGui::GetIO().MouseWheel != 0)
            {
                const double new_span = span * std::pow(.8, ImGui::GetIO().MouseWheel);
                const double anchor = view_start + span * ratio;
                view_start = std::max(0., anchor - new_span * ratio);
                view_end = view_start + std::max(seconds_axis ? .000001 : 1., new_span);
            }
            if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Middle))
            {
                const double delta = -ImGui::GetIO().MouseDelta.x / size.x * span;
                const double next = std::max(0., view_start + delta);
                view_end += next - view_start; view_start = next;
            }
        }
        draw->PopClipRect();
        draw->AddRect(origin, corner, IM_COL32(63, 113, 151, 255));
        const auto text = "max " + std::to_string(maximum);
        draw->AddText(ImVec2(origin.x + 5, origin.y + 3), color, text.c_str());
    }
};

analysis_panel::analysis_panel(native_dialogs dialogs) : impl_(std::make_unique<impl>()) { impl_->dialogs = std::move(dialogs); }
analysis_panel::~analysis_panel() { shutdown(); }

void analysis_panel::open_file(std::wstring path, std::uint16_t ppq, bool legacy_meta,
    std::function<void(single_midi_info_collector::time_graph)> commit)
{
    shutdown();
    impl_->filename = std::move(path);
    impl_->commit = std::move(commit);
    impl_->result_initialized = false;
    impl_->exporting.reset();
    impl_->message.clear(); impl_->conversion.clear();
    impl_->input_seconds = 0; impl_->input_tick = impl_->selected_tick = 0;
    auto job = std::make_shared<collection_job>();
    job->collector = std::make_shared<single_midi_info_collector>(impl_->filename, ppq, legacy_meta);
    impl_->job = job;
    impl_->worker = std::jthread([job](std::stop_token stop)
    {
        std::stop_callback cancellation(stop, [collector = job->collector] { collector->request_stop(); });
        std::string status;
        std::shared_ptr<const analysis_result> result;
        try
        {
            validate_header(job->collector->filename);
            if (stop.stop_requested()) throw std::runtime_error("Analysis cancelled.");
            job->collector->fetch_data();
            if (stop.stop_requested()) throw std::runtime_error("Analysis cancelled.");
            const auto [error, progress] = job->collector->status_text();
            if (error.find_first_not_of(" \t\r\n") != std::string::npos) throw std::runtime_error(error);
            { std::lock_guard lock(job->mutex); job->status = "Preparing graphs..."; }
            result = make_result(job->collector, stop);
            status = result ? "Analysis complete." : "Analysis cancelled.";
        }
        catch (const std::exception& error) { status = error.what(); }
        catch (...) { status = "Analysis failed with an unknown error."; }
        { std::lock_guard lock(job->mutex); job->result = std::move(result); job->status = std::move(status); }
        job->done.store(true, std::memory_order_release);
    });
}

void analysis_panel::cancel()
{
    impl_->worker.request_stop();
    if (impl_->job) impl_->job->collector->request_stop();
}

void analysis_panel::shutdown()
{
    cancel();
    impl_->export_worker.request_stop();
    if (impl_->worker.joinable()) impl_->worker.join();
    if (impl_->export_worker.joinable()) impl_->export_worker.join();
}

bool analysis_panel::busy() const { return impl_->job && !impl_->job->done.load(std::memory_order_acquire); }
std::shared_ptr<const analysis_result> analysis_panel::result() const
{
    if (!impl_->job || !impl_->job->done.load(std::memory_order_acquire)) return {};
    std::lock_guard lock(impl_->job->mutex);
    return impl_->job->result;
}
std::string analysis_panel::status() const
{
    if (!impl_->job) return "Select a MIDI and collect its information.";
    if (busy())
    {
        const auto [error, progress] = impl_->job->collector->status_text();
        if (error.find_first_not_of(" \t\r\n") != std::string::npos) return error;
        if (!impl_->job->collector->finished.load(std::memory_order_acquire)
            && progress.find_first_not_of(" \t\r\n") != std::string::npos) return progress;
    }
    std::lock_guard lock(impl_->job->mutex);
    return impl_->job->status;
}

bool analysis_panel::export_to(std::wstring path, export_kind kind, std::string delimiter)
{
    const auto data = result();
    if (!data || path.empty() || export_busy()) return false;
    if (delimiter.empty()) delimiter = ";";
    impl_->run_export(data, path, delimiter, kind == export_kind::combined_binary,
        kind == export_kind::tempo_csv, kind == export_kind::nps_csv);
    return true;
}

bool analysis_panel::export_busy() const
{
    return impl_->exporting && !impl_->exporting->done.load(std::memory_order_acquire);
}

std::string analysis_panel::export_status() const
{
    if (!impl_->exporting) return {};
    std::lock_guard lock(impl_->exporting->mutex);
    return impl_->exporting->status;
}

void analysis_panel::cancel_export() { impl_->export_worker.request_stop(); }

void analysis_panel::draw(bool* open)
{
    const auto display = ImGui::GetIO().DisplaySize;
    ImGui::SetNextWindowPos({60, 86}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({std::min(910.f, display.x - 84.f), std::max(420.f, std::min(790.f, display.y - 116.f))}, ImGuiCond_FirstUseEver);
    if (!begin_folded_window("MIDI analysis", open)) { end_folded_window(); return; }
    ImGui::BeginDisabled(busy());
    if (ImGui::Button("Open MIDI...") && impl_->dialogs.open_midi)
    {
        try { if (auto path = impl_->dialogs.open_midi(); !path.empty()) open_file(std::move(path)); }
        catch (const std::exception& error) { impl_->message = error.what(); }
    }
    ImGui::EndDisabled();
    if (!impl_->message.empty()) ImGui::TextWrapped("%s", impl_->message.c_str());
    ImGui::TextWrapped("%s", utf8(impl_->filename).c_str());
    ImGui::TextWrapped("%s", status().c_str());
    if (busy())
    {
        ImGui::ProgressBar(-float(ImGui::GetTime()), ImVec2(-1, 0), "Collecting tracks, tempo and note counts");
        if (ImGui::Button("Cancel analysis")) cancel();
        end_folded_window(); return;
    }
    const auto data = result();
    if (!data) { end_folded_window(); return; }
    if (!impl_->result_initialized)
    {
        impl_->view_start = 0;
        impl_->view_end = std::max(1., impl_->seconds_axis ? data->duration_seconds : double(data->last_tick));
        impl_->result_initialized = true;
    }
    ImGui::Text("Tracks: %zu | PPQN: %u | Duration: %.3f s | Last tick: %lld", data->source->tracks.size(),
        unsigned(data->source->ppq), data->duration_seconds, static_cast<long long>(data->last_tick));
    ImGui::Text("Peak polyphony: %lld | Peak notes/sec: %lld | Tempo points: %zu",
        static_cast<long long>(data->peak_polyphony), static_cast<long long>(data->peak_nps), data->source->tempo_map.size());
    ImGui::BeginDisabled(!impl_->commit || data->source->internal_time_map.empty());
    if (ImGui::Button("Use time map for this MIDI"))
    {
        impl_->commit(data->source->internal_time_map);
        impl_->message = "Time map applied to the source MIDI.";
    }
    ImGui::EndDisabled();
    ImGui::SameLine(); ImGui::Checkbox("Tempo", &impl_->show_tempo);
    ImGui::SameLine(); ImGui::Checkbox("Polyphony", &impl_->show_poly);
    ImGui::SameLine(); ImGui::Checkbox("Notes/sec", &impl_->show_nps);
    if (ImGui::Checkbox("Seconds on horizontal axis", &impl_->seconds_axis))
    {
        impl_->view_start = 0;
        impl_->view_end = std::max(1., impl_->seconds_axis ? data->duration_seconds : double(data->last_tick));
    }
    ImGui::SameLine(); ImGui::Checkbox("Auto vertical scale", &impl_->auto_scale);
    if (!impl_->auto_scale) { ImGui::SameLine(); ImGui::SetNextItemWidth(100); ImGui::InputDouble("Maximum", &impl_->manual_max, 0, 0, "%.1f"); }
    ImGui::SetNextItemWidth(145); ImGui::InputDouble("From", &impl_->view_start, 0, 0, "%.3f");
    ImGui::SameLine(); ImGui::SetNextItemWidth(145); ImGui::InputDouble("To", &impl_->view_end, 0, 0, "%.3f");
    ImGui::SameLine();
    if (ImGui::Button("Fit all")) { impl_->view_start = 0; impl_->view_end = std::max(1., impl_->seconds_axis ? data->duration_seconds : double(data->last_tick)); }
    impl_->view_start = std::isfinite(impl_->view_start) ? std::max(0., impl_->view_start) : 0;
    impl_->view_end = std::isfinite(impl_->view_end) ? std::max(impl_->view_start + .000001, impl_->view_end) : impl_->view_start + 1;
    ImGui::TextDisabled("Wheel: zoom at cursor | middle-drag: pan | click: select tick (exact values in tooltip)");
    if (impl_->show_tempo) impl_->plot("Tempo (BPM)", data->tempo_plot, data->source->tempo_map, IM_COL32(230, 123, 143, 255), *data);
    if (impl_->show_poly) impl_->plot("Polyphony", data->polyphony_plot, data->source->polyphony, IM_COL32(98, 209, 191, 255), *data);
    if (impl_->show_nps) impl_->plot("Notes per second", data->nps_plot, data->source->notes_per_second, IM_COL32(232, 198, 99, 255), *data);
    ImGui::Text("Selected tick: %lld | Time: %.6f s", static_cast<long long>(impl_->selected_tick), data->ticks_to_seconds(impl_->selected_tick));

    ImGui::SeparatorText("Tick / time conversion");
    ImGui::SetNextItemWidth(170); ImGui::InputScalar("Ticks", ImGuiDataType_S64, &impl_->input_tick);
    ImGui::SameLine();
    if (ImGui::Button("Ticks -> time"))
    {
        impl_->input_seconds = data->ticks_to_seconds(impl_->input_tick);
        impl_->selected_tick = impl_->input_tick;
        if (std::abs(impl_->input_seconds) < double(INT64_MAX) / 1000 - 1)
        {
            const auto ms = static_cast<std::int64_t>(std::round(impl_->input_seconds * 1000));
            impl_->conversion = std::to_string(ms / 60000) + " min " + std::to_string(ms / 1000 % 60) + " sec " + std::to_string(ms % 1000) + " ms";
        }
        else impl_->conversion = std::to_string(impl_->input_seconds) + " seconds";
    }
    ImGui::SetNextItemWidth(170); ImGui::InputDouble("Seconds", &impl_->input_seconds, 0, 0, "%.6f");
    ImGui::SameLine();
    if (ImGui::Button("Time -> ticks"))
    {
        impl_->selected_tick = impl_->input_tick = data->seconds_to_ticks(impl_->input_seconds);
        impl_->conversion = "Tick " + std::to_string(impl_->input_tick);
    }
    if (!impl_->conversion.empty()) ImGui::TextUnformatted(impl_->conversion.c_str());
    ImGui::SeparatorText("Export");
    const bool exporting = impl_->exporting && !impl_->exporting->done.load(std::memory_order_acquire);
    ImGui::BeginDisabled(exporting);
    ImGui::SetNextItemWidth(75); ImGui::InputText("Delimiter", impl_->delimiter.data(), impl_->delimiter.size());
    ImGui::SameLine(); ImGui::SetNextItemWidth(115); ImGui::Combo("Format", &impl_->export_format, "CSV\0ATRAW (binary)\0");
    if (ImGui::Button("Export tempo CSV")) impl_->start_export(data, true, false);
    ImGui::SameLine(); if (ImGui::Button("Export combined data")) impl_->start_export(data, false, false);
    ImGui::SameLine(); if (ImGui::Button("Export notes/sec CSV")) impl_->start_export(data, false, true);
    ImGui::EndDisabled();
    if (impl_->exporting)
    {
        { std::lock_guard lock(impl_->exporting->mutex); ImGui::TextWrapped("%s", impl_->exporting->status.c_str()); }
        if (exporting && ImGui::Button("Cancel export")) impl_->export_worker.request_stop();
    }
    if (!impl_->message.empty()) ImGui::TextWrapped("%s", impl_->message.c_str());
    end_folded_window();
}
}
