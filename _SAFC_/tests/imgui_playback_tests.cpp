#define NOMINMAX
#include <Windows.h>
#include <archive.h>
#include <archive_entry.h>
#include "../imgui/playback_session.h"
#include "../imgui/editor_panel.h"
#include "../imgui/video_export_panel.h"
#include "../SAFC_InnerModules/compressed_midi_event_source.h"
#include "../SAFC_InnerModules/midi_editor.h"
#include "../SAFC_InnerModules/simple_player.h"
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
#include <imgui.h>
#include <imgui_internal.h>

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

void pending_stop_checks(const fs::path& directory)
{
    const auto input = directory / "stop-before-dispatch.mid";
    write_bytes(input, midi(60));
    simple_player transport;
    transport.init();
    require(transport.use_silent_output(), "Prepare silent cancellation probe");
    transport.cancel_playback();
    // Exercise the gap between the session's final cancellation check and
    // entering the player. An ordinary stop() would be reset by either run.
    struct source_probe : playback_event_source
    {
        mutable bool touched = false;
        std::uint64_t total_duration_us() const override { touched = true; return 0; }
        void rewind() override { touched = true; }
        bool next(generated_event&) override { touched = true; return false; }
    } source;
    transport.simple_run(input.wstring(), 0, false);
    transport.run_from_external(&source, 0, false);
    require(!source.touched && !transport.get_info().open_complete && !transport.is_playing(),
        "Stop before dispatch prevents both file and editor playback from starting");
    transport.init(false);
    transport.run_from_external(&source, 0, false);
    require(source.touched, "A new dispatch clears the cancellation gate");

    ui::playback_session playback;
    for (int attempt = 0; attempt < 20; ++attempt)
    {
        require(playback.open(input.wstring(), true, true), "Dispatch immediate-stop probe");
        stop(playback);
        require(!playback.snapshot().playing, "Immediate Stop cannot leave a paused run alive");
    }
}

void editor_snapshot_checks(const fs::path& directory)
{
    const auto input = directory / "editor-snapshot.mid";
    // A long held note spans several blocks of short notes. Seeking must
    // restore it while skipping the ended prefix, even after sparse edits.
    bytes track{0, 0x90, 40, 90};
    for (unsigned i = 0; i < 4096; ++i)
        track.insert(track.end(), {1, 0x90, 60, 100, 1, 0x80, 60, 0});
    track.insert(track.end(), {100, 0x80, 40, 0, 0, 0xff, 0x2f, 0});
    bytes file{'M','T','h','d',0,0,0,6,0,0,0,1,1,224,'M','T','r','k'};
    for (int shift : {24, 16, 8, 0}) file.push_back(static_cast<unsigned char>(track.size() >> shift));
    file.insert(file.end(), track.begin(), track.end());
    write_bytes(input, file);
    auto model = std::make_unique<midi_editor>();
    require(model->load_file(input.wstring()), "Load editor snapshot fixture");
    auto original = model->make_playback_source();
    midi_editor::piano_note held;
    require(model->find_note_at(0, 40, held), "Find long held note");
    model->select_note(held.id, midi_editor::select_mode::replace);
    model->change_velocity_selected(55);
    model->change_channel_selected(3);
    model->adjust_velocity_selected(2);
    std::uint8_t old_velocity = 0;
    model->set_note_velocity_transient(held, 61, old_velocity);
    require(old_velocity == 57, "Sparse velocity operations compose correctly");
    model->insert_note(4000, 7000, 72, 80, 2, 0);
    auto edited = model->make_playback_source();
    // Undo, deletion, and reloading may not change either prior snapshot.
    model->undo();
    model->erase_note_at(0, 40);
    auto deleted = model->make_playback_source();
    require(deleted->total_duration_us() < original->total_duration_us(), "Deleted final note shortens the snapshot duration");
    require(model->load_file(input.wstring()), "Reload editor while readers retain old data");
    model.reset();
    auto* typed = static_cast<midi_editor::editor_event_source*>(edited.get());
    auto reader = typed->fork_reader();
    auto other = typed->fork_reader();
    generated_event first{}, second{};
    require(reader->next(first) && reader->next(second) && other->next(second) && first.short_msg == second.short_msg,
        "Advancing one snapshot cursor does not move the other");
    constexpr std::uint64_t target = 5000ull * 500000 / 480;
    auto verify_held = [&](playback_event_source& source, unsigned velocity, unsigned channel, bool inserted)
    {
        source.seek(target);
        generated_event event;
        unsigned found = 0, found_inserted = 0;
        std::uint64_t last = target;
        while (source.next(event))
        {
            require(event.time_us >= last, "Seek stream stays ordered");
            last = event.time_us;
            if (event.k == generated_event::kind::note_on && event.key == 40)
            {
                require(event.time_us == target && event.velocity == velocity && event.channel == channel,
                    "Snapshot preserves its held note and channel despite later edits");
                ++found;
            }
            if (event.k == generated_event::kind::note_on && event.key == 72) ++found_inserted;
        }
        require(found == 1 && found_inserted == unsigned(inserted), "Seek restores base and overlay notes exactly once");
    };
    verify_held(*original, 90, 0, false);
    verify_held(*reader, 61, 3, true);
    verify_held(*other, 61, 3, true);
    verify_held(*edited, 61, 3, true);

    simple_player transport;
    transport.init();
    require(transport.use_silent_output(), "Prepare silent editor transport");
    std::jthread run([&] { transport.run_from_external(reader.get(), .6, false); });
    struct stop_transport { simple_player& player; ~stop_transport() { player.shutdown(); } } guard{transport};
    const auto from = std::uint64_t(reader->total_duration_us() * .6);
    wait_for([&] { return transport.is_playing() && !transport.is_seeking() && transport.get_position_us() > from + 100000; },
        "Editor playback from view advances past the seek boundary");
    transport.shutdown();
    run.join();
}

// Optional integration run with a real sound bank and Windows audio endpoint.
// Releasing the key during preparation ensures the probe remains silent.
void audition_preparation_checks(const std::wstring& bank, const fs::path& directory)
{
    auto* context = ImGui::CreateContext();
    struct destroy_context { ImGuiContext* value; ~destroy_context() { ImGui::DestroyContext(value); } } context_guard{context};
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.DisplaySize = {1400, 1000};
    io.DeltaTime = 1.f / 60.f;
    unsigned char* pixels;
    int width, height;
    io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
    ui::playback_session playback;
    ui::editor_panel editor(playback, {});
    syncore_preferences preferences;
    preferences.phase_mode = syncore_phase_mode::independent_bins;
    preferences.render_threads = 1;
    playback.configure_synth(bank, preferences);
    const auto started = std::chrono::steady_clock::now();
    require(playback.audition_note(60, 100, 0, true), "Dispatch editor audition");
    playback.audition_note(60, 0, 0, false);
    require(std::chrono::steady_clock::now() - started < std::chrono::milliseconds(250),
        "Editor clicks return before sound-bank preparation");
    bool saw_progress = false, displayed_progress = false;
    std::uint64_t last = 0;
    wait_for([&] {
        const auto before = std::chrono::steady_clock::now();
        const auto state = playback.snapshot();
        playback.audition_note(61, 0, 0, false);
        require(std::chrono::steady_clock::now() - before < std::chrono::milliseconds(250),
            "Status reads and note releases stay responsive during preparation");
        if (state.preparing && state.preparation_total)
        {
            require(state.preparation_completed >= last && state.preparation_completed <= state.preparation_total,
                "Prerender counters advance monotonically");
            last = state.preparation_completed;
            saw_progress = true;
            require(state.message.find("Prerendering sample variants") != std::string::npos,
                "Preparation message includes sample variant progress");
            bool visible = true;
            ImGui::NewFrame();
            ImGui::LogToBuffer();
            editor.draw(&visible);
            displayed_progress |= std::strstr(context->LogBuffer.c_str(), "Prerendering sample variants") != nullptr;
            ImGui::LogFinish();
            ImGui::Render();
        }
        require(state.error.empty(), state.error.c_str());
        return !state.busy;
    }, "Editor audition preparation completes", 45);
    require(saw_progress && displayed_progress, "Editor displays prerender progress while the player window is closed");

    // Once SYNCore is ready, transport Stop must not discard its prepared
    // phase/sample cache. An audition immediately after Stop should therefore
    // use the existing output instead of dispatching another preparation run.
    const auto input = directory / "cache-reuse.mid";
    write_bytes(input, midi(60));
    auto wait_for_reuse = [&]
    {
        wait_for([&]
        {
            const auto state = playback.snapshot();
            require(state.error.empty(), state.error.c_str());
            require(state.message.find("Prerendering sample variants") == std::string::npos,
                "Matching playback must not prerender its existing cache again");
            return state.playing;
        }, "Playback reuses prepared SYNCore output", 5);
    };
    require(playback.open(input.wstring(), false, true), "Start playback with prepared SYNCore output");
    wait_for_reuse();
    stop(playback);
    // Velocity zero exercises ready-output audition without sounding a note.
    require(playback.audition_note(60, 0, 0, true), "Audition after transport Stop");
    require(!playback.snapshot().busy, "Transport Stop preserves the prepared SYNCore output session");
    playback.audition_note(60, 0, 0, false);
    require(playback.restart(false, true), "Restart playback with retained SYNCore output");
    wait_for_reuse();
    stop(playback);

    // EOF must keep the same session too. An empty source avoids audible notes.
    struct empty_source : playback_event_source
    {
        std::uint64_t total_duration_us() const override { return 0; }
        void rewind() override {}
        bool next(generated_event&) override { return false; }
    };
    require(playback.open_external(std::make_shared<empty_source>()), "Dispatch empty editor source");
    wait_for([&] { return !playback.snapshot().busy; }, "Natural EOF completes", 5);
    require(playback.snapshot().error.empty(), "Natural EOF preserves a healthy output");
    require(playback.open(input.wstring(), false, true), "Replay after natural EOF");
    wait_for_reuse();
    stop(playback);

    // A changed synth setting must invalidate the old session. Cancelling that
    // rebuild still uses terminal teardown and must not wait for the full cache.
    preferences.output_gain_db -= 1;
    playback.configure_synth(bank, preferences);
    require(playback.audition_note(60, 0, 0, true), "Audition applies changed SYNCore settings");
    wait_for([&]
    {
        const auto state = playback.snapshot();
        require(state.error.empty(), state.error.c_str());
        return state.preparing && state.preparation_total;
    }, "Changed settings rebuild sample variants", 15);
    const auto stopping = std::chrono::steady_clock::now();
    playback.stop();
    require(std::chrono::steady_clock::now() - stopping < std::chrono::milliseconds(250),
        "Stop during preparation returns immediately");
    wait_for([&] { return !playback.snapshot().busy; }, "Stop cancels phase preparation", 5);
    require(playback.snapshot().error.empty(), "Cancelled preparation reports no failure");
    playback.shutdown();
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

int wmain(int argc, wchar_t** argv)
{
    const auto directory = fs::temp_directory_path() /
        ("safc-imgui-service-tests-" + std::to_string(GetCurrentProcessId()));
    try
    {
        require(fs::create_directory(directory), "Create isolated test directory");
        struct cleanup { fs::path path; ~cleanup() { std::error_code error; fs::remove_all(path, error); } } cleanup{directory};
        if (argc == 2)
        {
            audition_preparation_checks(argv[1], directory);
            std::cout << "Editor preparation, SYNCore cache reuse and cancellation passed\n";
            return 0;
        }
        pending_stop_checks(directory);
        editor_snapshot_checks(directory);
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

        require(playback.open(nested.wstring(), true, true), "Replay after Stop with nested archive");
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
