#define NOMINMAX
#include <Windows.h>
#include <archive.h>
#include <archive_entry.h>
#include "../imgui/playback_session.h"
#include "../imgui/video_export_panel.h"
#include "../SAFC_InnerModules/compressed_midi_event_source.h"
#include "../SAFC_InnerModules/single_midi_processor_lean.h"
#include "../SAFC_InnerModules/midi_collection_threaded_merger.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <sstream>
#include <thread>
#include <vector>

namespace ui = safc::imgui_ui;
namespace fs = std::filesystem;
using bytes = std::vector<unsigned char>;

namespace
{
void require(bool value, const char* message)
{
    if (!value) throw std::runtime_error(message);
}

bytes midi(unsigned char key)
{
    return {'M','T','h','d', 0,0,0,6, 0,0, 0,1, 1,224,
        'M','T','r','k', 0,0,0,12, 0,0x90,key,100, 120,0x80,key,0, 0,0xff,0x2f,0};
}

bytes read_bytes(const fs::path& path)
{
    std::ifstream stream(path, std::ios::binary);
    return bytes(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}

void write_bytes(const fs::path& path, const bytes& data)
{
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    stream.write(reinterpret_cast<const char*>(data.data()), data.size());
    require(static_cast<bool>(stream), "Write test fixture");
}

void zip(const fs::path& path, const std::vector<std::pair<std::string, bytes>>& entries)
{
    auto* writer = archive_write_new();
    require(writer != nullptr, "Allocate ZIP writer");
    struct cleanup { archive* value; ~cleanup() { archive_write_free(value); } } cleanup{writer};
    require(archive_write_set_format_zip(writer) == ARCHIVE_OK, "Set ZIP format");
    require(archive_write_open_filename_w(writer, path.c_str()) == ARCHIVE_OK, "Open ZIP fixture");
    for (const auto& [name, contents] : entries)
    {
        auto* entry = archive_entry_new();
        archive_entry_set_pathname(entry, name.c_str());
        archive_entry_set_filetype(entry, AE_IFREG);
        archive_entry_set_perm(entry, 0644);
        archive_entry_set_size(entry, contents.size());
        const auto header = archive_write_header(writer, entry);
        archive_entry_free(entry);
        require(header == ARCHIVE_OK, "Write ZIP entry header");
        require(archive_write_data(writer, contents.data(), contents.size()) == contents.size(), "Write ZIP entry");
    }
    require(archive_write_close(writer) == ARCHIVE_OK, "Finish ZIP fixture");
}

template<class Predicate> void wait_for(Predicate predicate, const char* message, int seconds = 15)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
    while (!predicate())
    {
        if (std::chrono::steady_clock::now() >= deadline) throw std::runtime_error(message);
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void verify_selected(ui::playback_session& playback, unsigned char key)
{
    wait_for([&] { return playback.snapshot().playing; }, "Prepared source should start paused");
    const auto state = playback.snapshot();
    require(state.paused && state.source_tracks == 1 && state.source_events == 2,
        "Prepared source metadata is published consistently");
    auto factory = playback.export_source_factory();
    require(static_cast<bool>(factory), "Prepared archive exposes export factory");
    auto first = factory();
    auto second = factory();
    generated_event a{}, b{};
    require(first.get() != second.get() && first->next(a) && second->next(b), "Independent archive readers");
    require(a.key == key && b.key == key && a.k == generated_event::kind::note_on,
        "Selected archive member reaches playback and both export readers");
    require(first->next(a) && a.k == generated_event::kind::note_off,
        "First cursor can advance independently");
    require(second->next(b) && b.k == generated_event::kind::note_off && a.time_us == b.time_us,
        "Second cursor preserves time ordering");
}

void stop(ui::playback_session& playback)
{
    playback.stop();
    wait_for([&] { return !playback.snapshot().busy; }, "Stop must retire scheduler and preparation");
}

void cancellation_checks(const fs::path& directory)
{
    const auto fixture = directory / "cancel-reader.bin";
    write_bytes(fixture, bytes(20000, 1));
    std::atomic_bool cancel{false};
    midi_file_reader reader(fixture.wstring());
    reader.set_cancellation(&cancel);
    static_cast<void>(read_midi_byte(reader));
    cancel.store(true);
    bool stopped = false;
    try { for (int i = 0; i < 4096; ++i) static_cast<void>(read_midi_byte(reader)); }
    catch (const midi_processing_cancelled&) { stopped = true; }
    require(stopped && reader.position() <= 4096, "Byte parser observes cancellation within a bounded batch");
    reader.reopen(fixture.wstring());
    std::ostringstream copied;
    stopped = false;
    try { reader.copy_to(copied); }
    catch (const midi_processing_cancelled&) { stopped = true; }
    require(stopped && copied.str().empty(), "Concatenation stops before copying a cancelled chunk");

    single_midi_processor_2::processing_data data{};
    data.filename = fixture.wstring();
    data.output_filename = (directory / "cancel-output.mid").wstring();
    for (bool lean : {true, false})
    {
        single_midi_processor_2::message_buffers buffers;
        buffers.cancel_requested.store(true);
        stopped = false;
        try
        {
            if (lean) single_midi_processor_lean::sync_processing(data, buffers);
            else single_midi_processor_2::sync_processing<false>(data, buffers);
        }
        catch (const midi_processing_cancelled&) { stopped = true; }
        require(stopped && !buffers.processing && buffers.finished && !fs::exists(data.output_filename),
            "Both processors retire cleanly without writing a cancelled job");
    }
    std::vector<midi_collection_threaded_merger::proc_data_ptr> requests;
    requests.push_back(std::make_shared<single_midi_processor_2::processing_data>());
    midi_collection_threaded_merger merger(requests, 480, (directory / "cancel-merge.mid").wstring(), false);
    merger.request_cancel();
    merger.start_processing();
    merger.wait_processing();
    merger.start_ri_merge();
    merger.start_final_merge();
    require(merger.cancelled() && merger.is_smrp_complete() && merger.is_ri_merge_complete() && merger.complete &&
        !fs::exists(directory / "cancel-merge.mid"), "Cancelled merger stages publish completion without work");
}

void output_failure_checks(const fs::path& directory)
{
    const auto input = directory / "write-failure-input.mid";
    write_bytes(input, midi(60));
    const auto unwritable_file = directory / "output-is-directory";
    fs::create_directory(unwritable_file);
    for (const bool lean : {true, false})
    {
        single_midi_processor_2::processing_data data{};
        data.filename = input.wstring();
        data.output_filename = unwritable_file.wstring();
        single_midi_processor_2::message_buffers buffers;
        bool failed = false;
        try
        {
            if (lean) single_midi_processor_lean::sync_processing(data, buffers);
            else single_midi_processor_2::sync_processing<false>(data, buffers);
        }
        catch (const std::ios_base::failure&) { failed = true; }
        require(failed && buffers.finished && !buffers.processing,
            "Both processor paths report output-open failure and retire their flags");
    }
    auto data = std::make_shared<single_midi_processor_2::processing_data>();
    data->filename = input.wstring();
    data->output_filename = unwritable_file.wstring();
    const auto destination = directory / "preserved-output.mid";
    const bytes previous{'p','r','e','v','i','o','u','s'};
    write_bytes(destination, previous);
    midi_collection_threaded_merger merger({data}, 480, destination.wstring(), false);
    merger.start_processing();
    wait_for([&] { return merger.is_smrp_complete(); }, "Failed output must retire processor worker");
    merger.wait_processing();
    require(merger.has_failed(), "Output stream failure must reach merger failure state");
    merger.start_ri_merge();
    merger.start_final_merge();
    require(merger.complete && read_bytes(destination) == previous,
        "Failed processing must prevent final output replacement");
}
}

int main()
{
    const auto directory = fs::temp_directory_path() /
        ("safc-imgui-service-tests-" + std::to_string(GetCurrentProcessId()));
    try
    {
        require(fs::create_directory(directory), "Create isolated test directory");
        struct cleanup { fs::path path; ~cleanup() { std::error_code error; fs::remove_all(path, error); } } cleanup{directory};
        cancellation_checks(directory);
        output_failure_checks(directory);
        const auto multi = directory / "multiple.zip";
        const auto nested = directory / "nested.zip";
        zip(multi, {{"notes.txt", {'n','o'}}, {"first.mid", midi(60)}, {"second.mid", midi(67)}});
        zip(nested, {{"nested.zip", read_bytes(multi)}});

        std::string error;
        auto legacy = compressed_midi_event_source::open(multi.wstring(), {}, nullptr, error);
        require(legacy != nullptr, "Legacy first-member archive open still works");
        generated_event first{};
        require(legacy->next(first) && first.key == 60, "Legacy default preserves first playable member");

        ui::playback_session playback;
        require(playback.open(multi.wstring(), true, true), "Dispatch ZIP playback");
        wait_for([&] { return playback.snapshot().waiting_for_member; }, "ZIP member selection must appear");
        const auto choices = playback.snapshot();
        require(choices.archive_members.size() == 2 && choices.archive_members[1] == "second.mid",
            "Only playable ZIP members appear in archive order");
        playback.choose_archive_member(1);
        verify_selected(playback, 67);

        if (simple_player_video_export_available())
        {
            ui::video_export_panel exporter(playback, {});
            auto settings = exporter.settings();
            settings.width = 128;
            settings.height = 96;
            settings.fps = 10;
            settings.tail_seconds = 0;
            settings.video_bitrate_kbps = 256;
            exporter.set_settings(settings);
            const auto output = directory / "archive.mp4";
            require(exporter.start_export(output.wstring()), "Dispatch independent archive MP4 export");
            wait_for([&] { return !exporter.snapshot().busy; }, "MP4 worker must complete", 30);
            const auto exported = exporter.snapshot();
            if (!exported.result.ok) std::cerr << exported.result.error << '\n';
            require(exported.result.ok && read_bytes(output).size() > 100,
                "Archive MP4 job must produce actual encoded output");
            require(playback.snapshot().paused, "Independent export must preserve paused playback");
            const bytes sentinel{'p','r','e','v','i','o','u','s'};
            write_bytes(output, sentinel);
            settings.tail_seconds = 30;
            exporter.set_settings(settings);
            require(exporter.start_export(output.wstring()), "Dispatch cancellable MP4 export");
            exporter.cancel();
            wait_for([&] { return !exporter.snapshot().busy; }, "Cancelled MP4 worker must retire", 30);
            require(exporter.snapshot().result.cancelled && read_bytes(output) == sentinel,
                "Cancelled MP4 export preserves previous output");
            exporter.shutdown();
        }
        stop(playback);

        require(playback.open(nested.wstring(), true, true), "Replay after terminal Stop with nested archive");
        wait_for([&] { return playback.snapshot().waiting_for_member; }, "Nested ZIP member selection must appear");
        require(playback.snapshot().archive_layer == 2, "Selection identifies nested archive layer");
        playback.choose_archive_member(0);
        verify_selected(playback, 60);
        require(playback.snapshot().source_archive_depth == 2, "Nested preparation retains layer count");
        stop(playback);

        require(playback.open(multi.wstring(), true, true), "Dispatch selection cancellation case");
        wait_for([&] { return playback.snapshot().waiting_for_member; }, "Wait for cancellable selection");
        stop(playback);
        require(!playback.snapshot().waiting_for_member && playback.snapshot().error.empty(),
            "Stop cancels pending selection without error or dangling wait");

        const auto invalid = directory / "invalid.zip";
        write_bytes(invalid, {'n','o','t','-','a','r','c','h','i','v','e'});
        require(playback.open(invalid.wstring(), true, true), "Dispatch invalid archive");
        wait_for([&] { return !playback.snapshot().busy; }, "Invalid archive must retire");
        require(!playback.snapshot().error.empty(), "Invalid archive publishes actionable failure");
        playback.shutdown();
        std::cout << "ImGui playback, archive selection, cancellation and MP4 service tests passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
