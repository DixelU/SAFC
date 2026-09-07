#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "../SAFC_InnerModules/single_midi_info_collector.h"
#include "platform_dialogs.h"

namespace safc::imgui_ui
{
struct analysis_point { std::int64_t tick = 0; double value = 0; };
struct analysis_tempo { std::int64_t tick = 0; double seconds = 0; double seconds_per_tick = 0; double bpm = 120; };
struct analysis_result
{
    // Immutable only after the analysis worker has finished. Exact btree
    // queries/export coexist with bounded display envelopes for dense files.
    std::shared_ptr<const single_midi_info_collector> source;
    std::vector<analysis_point> tempo_plot, polyphony_plot, nps_plot;
    std::vector<analysis_tempo> timing;
    std::int64_t last_tick = 0;
    double duration_seconds = 0;
    std::int64_t peak_polyphony = 0, peak_nps = 0;
    double ticks_to_seconds(std::int64_t tick) const;
    std::int64_t seconds_to_ticks(double seconds) const;
};

class analysis_panel
{
public:
    enum class export_kind { combined_csv, combined_binary, tempo_csv, nps_csv };
    explicit analysis_panel(native_dialogs dialogs = {});
    ~analysis_panel();
    analysis_panel(const analysis_panel&) = delete;
    analysis_panel& operator=(const analysis_panel&) = delete;

    void open_file(std::wstring path, std::uint16_t ppq = 0, bool legacy_meta = false,
        std::function<void(single_midi_info_collector::time_graph)> commit_time_map = {});
    void draw(bool* open);
    void cancel();
    void shutdown();
    bool busy() const;
    std::shared_ptr<const analysis_result> result() const;
    std::string status() const;
    bool export_to(std::wstring path, export_kind kind, std::string delimiter = ";");
    bool export_busy() const;
    std::string export_status() const;
    void cancel_export();

private:
    struct impl;
    std::unique_ptr<impl> impl_;
};
}
