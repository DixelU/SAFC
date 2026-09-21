#define NOMINMAX
#include <Windows.h>

#include "editor_panel.h"
#include "folded_theme.h"
#include "widgets.h"
#include "playback_session.h"
#include "../SAFC_InnerModules/midi_editor.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <stdexcept>
#include <thread>
#include <unordered_set>

namespace safc::imgui_ui
{
namespace
{

using tick = midi_editor::tick_type;
using note = midi_editor::piano_note;
using lane_type = midi_editor::control_lane;
using velocity_entry = midi_editor::recorded_velocity_op::entry;

enum class edit_mode
{
	draw,
	select,
	erase,
	split
};
enum class lane_kind
{
	velocity,
	pitch_bend,
	pan,
	volume,
	tempo
};

constexpr int midi_key_count = 128;
constexpr int midi_key_max = midi_key_count - 1;
constexpr int midi_value_max = 127;
constexpr int pitch_bend_max = 16383;
constexpr int pitch_bend_center = 8192;
// MIDI Set Tempo stores microseconds per quarter note in three bytes.
constexpr double microseconds_per_minute = 60000000.;
constexpr double minimum_tempo_bpm = microseconds_per_minute / 0xFFFFFF;
constexpr double maximum_tempo_bpm = microseconds_per_minute;
constexpr tick maximum_ramp_steps = 4096;
constexpr float note_resize_handle_width = 7.f;
constexpr float velocity_brush_radius = 4.f;

std::string utf8(const std::wstring& value)
{
	if (value.empty())
		return {};
	const auto count = WideCharToMultiByte(CP_UTF8, 0, value.data(), int(value.size()), nullptr, 0, nullptr, nullptr);
	std::string result(count, '\0');
	WideCharToMultiByte(CP_UTF8, 0, value.data(), int(value.size()), result.data(), count, nullptr, nullptr);
	return result;
}

ImU32 note_color(unsigned track, unsigned channel, bool selected, bool ghost = false)
{
	if (ghost)
		return IM_COL32(117, 136, 151, 110);
	if (selected)
		return IM_COL32(255, 208, 99, 255);
	constexpr float track_hue_step = .618034f; // Golden-ratio spacing separates neighboring tracks.
	constexpr float channel_hue_step = .1031f;
	constexpr float starting_hue = .51f;
	const float hue = std::fmod(float(track) * track_hue_step + float(channel) * channel_hue_step + starting_hue, 1.f);
	float red, green, blue;
	ImGui::ColorConvertHSVtoRGB(hue, .66f, .88f, red, green, blue);
	return ImGui::GetColorU32(ImVec4(red, green, blue, 1.f));
}

bool contains(ImVec2 p, ImVec2 low, ImVec2 high)
{
	return p.x >= low.x && p.x < high.x && p.y >= low.y && p.y < high.y;
}

bool clip_segment_to_rect(ImVec2& first, ImVec2& last, ImVec2 low, ImVec2 high)
{
	const ImVec2 origin = first;
	const ImVec2 delta(last.x - first.x, last.y - first.y);
	float enter = 0.f, leave = 1.f;

	auto clip_axis = [&](float value, float change, float minimum, float maximum)
	{
		if (std::abs(change) < 1.e-6f)
			return value >= minimum && value <= maximum;

		float lower_t = (minimum - value) / change;
		float upper_t = (maximum - value) / change;
		if (lower_t > upper_t)
			std::swap(lower_t, upper_t);
		enter = std::max(enter, lower_t);
		leave = std::min(leave, upper_t);
		return enter <= leave;
	};

	if (!clip_axis(origin.x, delta.x, low.x, high.x) ||
		!clip_axis(origin.y, delta.y, low.y, high.y))
		return false;

	first = ImVec2(origin.x + delta.x * enter, origin.y + delta.y * enter);
	last = ImVec2(origin.x + delta.x * leave, origin.y + delta.y * leave);
	return true;
}

bool segment_intersects_rect(ImVec2 first, ImVec2 last, ImVec2 low, ImVec2 high)
{
	return clip_segment_to_rect(first, last, low, high);
}

void help_tip(const char* text)
{
	if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
		ImGui::SetTooltip("%s", text);
}

void require(bool condition, const char* message)
{
	if (!condition)
		throw std::runtime_error(message);
}
}

struct editor_panel::impl
{
	playback_session& playback;
	native_dialogs dialogs;

	std::shared_ptr<midi_editor> document = std::make_shared<midi_editor>();
	std::jthread worker;
	std::atomic<bool> busy = false;
	std::atomic<std::uint64_t> progress = 0, total = 0;
	std::string status = "Open a MIDI to edit its notes and controllers.";
	std::string job_status, completed_status;
	std::shared_ptr<midi_editor> loaded_document;
	std::shared_ptr<midi_editor::editor_event_source> prepared_source;
	std::shared_ptr<playback_event_source> active_playback_source;
	double prepared_seek = 0;
	bool completion_pending = false;
	bool stopped = false;
	std::wstring requested_path;
	bool ask_discard = false;

	int draw_channel = -1, draw_velocity = 100;
	tick draw_length = 0;
	int snap_index = 2;
	edit_mode mode = edit_mode::draw;
	lane_kind lane = lane_kind::velocity;
	bool ghosts = true, show_lane = true, help = false;
	float lane_height = 110.f;
	double tempo_low = minimum_tempo_bpm;
	double tempo_high = maximum_tempo_bpm;
	double tempo_point = 120.;
	tick tempo_tick = 0;
	float scale_percent = 100.f;
	std::array<char, 256> track_name{};
	int named_track = -1;
	int audition_key = -1, audition_channel = 0;

	enum class gesture_kind
	{
		none,
		draw,
		select,
		erase,
		split,
		move,
		resize,
		stretch,
		pan,
		keyboard_pan,
		velocity,
		control,
		divider
	};

	gesture_kind gesture = gesture_kind::none;
	ImVec2 anchor_mouse{}, current_mouse{};
	tick anchor_tick = 0, current_tick = 0, pan_start = 0;
	int anchor_key = 60, current_key = 60, pan_low = 0, pan_high = midi_key_max;
	std::int64_t delta_tick = 0;
	int delta_key = 0;
	tick gesture_length = 0, stretch_begin = 0, stretch_end = 1;
	double stretch_factor = 1.;
	midi_editor::select_mode selection_mode = midi_editor::select_mode::add;
	std::vector<note> gesture_notes;
	std::unordered_set<midi_editor::note_id_type> erased_notes;
	std::vector<std::pair<midi_editor::note_id_type, tick>> split_cuts;
	std::map<midi_editor::note_id_type, velocity_entry> velocities;

	std::map<tick, double> control_values;
	double anchor_value = 0;
	bool line_gesture = false;
	ImGuiMouseButton gesture_button = ImGuiMouseButton_Left;

	enum class tool_kind
	{
		none,
		chopper,
		flip,
		claw,
		lfo
	};

	tool_kind tool = tool_kind::none;
	bool tool_open = false, preview_dirty = false;
	int divisions = 4, trash_every = 4, lfo_shape = 0;
	float time_multiplier = 1.f, gap = 0.f, period = 1.f, distortion = .5f;
	bool absolute_pattern = false, horizontal = true, vertical = false, preserve_starts = false;
	bool remove_short = true, compensate = false;
	float lfo_center = 64.f, lfo_range = 63.f, lfo_cycles = 1.f, lfo_phase = 0.f;

	explicit impl(playback_session& player, native_dialogs callbacks) : playback(player), dialogs(std::move(callbacks))
	{
	}

	template<class Function>
	void start_job(std::string message, Function function)
	{
		poll();

		if (busy || stopped)
			return;

		if (worker.joinable())
			worker.join();

		job_status = std::move(message);
		completed_status.clear();
		progress = total = 0;
		completion_pending = true;

		busy.store(true, std::memory_order_release);

		worker = std::jthread([this, function = std::move(function)](std::stop_token stop) mutable
		{
			try
			{
				function(stop);
			}
			catch (const std::exception& error)
			{
				completed_status = error.what();
			}
			catch (...)
			{
				completed_status = "Editor operation failed.";
			}

			busy.store(false, std::memory_order_release);
		});
	}

	void poll()
	{
		if (busy.load(std::memory_order_acquire) || !completion_pending)
			return;

		if (worker.joinable())
			worker.join();

		completion_pending = false;
		if (!completed_status.empty())
			status = std::move(completed_status);

		if (loaded_document)
		{
			document = std::move(loaded_document);
			named_track = -1;
			draw_channel = -1;
			draw_length = 0;
			gesture = gesture_kind::none;
		}

		if (prepared_source)
		{
			auto pristine = std::move(prepared_source);
			active_playback_source = pristine->fork_reader();
			auto factory = [pristine]() -> std::shared_ptr<playback_event_source>
			{
				return pristine->fork_reader();
			};

			if (!playback.open_external(active_playback_source, false, prepared_seek, std::move(factory)))
				status = "Playback is busy; stop it before playing the editor.";
		}
	}

	void load(std::wstring path)
	{
		if (path.empty() || busy || stopped)
			return;

		finish_gesture(false);

		if (tool != tool_kind::none)
			document->cancel_tool_preview();

		tool = tool_kind::none;
		tool_open = false;

		start_job("Loading MIDI...", [this, path = std::move(path)](std::stop_token stop)
		{
			auto next = std::make_shared<midi_editor>();
			next->on_load_progress = [this, stop](std::uint64_t done, std::uint64_t size)
			{
				progress = done;
				total = size;
				if (stop.stop_requested())
					throw std::runtime_error("Load cancelled.");
			};

			if (!next->load_file(path))
				throw std::runtime_error("Could not load the MIDI file.");

			if (stop.stop_requested())
				throw std::runtime_error("Load cancelled.");

			next->on_load_progress = {};
			completed_status = "Loaded " + utf8(std::filesystem::path(path).filename().wstring());

			loaded_document = std::move(next);
		});
	}

	void request_load(std::wstring path)
	{
		poll();

		if (path.empty() || busy)
			return;
		if (document->is_modified())
		{
			requested_path = std::move(path);
			ask_discard = true;
		}
		else
			load(std::move(path));
	}

	void save(bool export_only)
	{
		if (!dialogs.save_midi || busy || tool != tool_kind::none || !document->is_file_loaded())
			return;

		auto suggested = std::filesystem::path(document->get_filename());
		suggested.replace_filename(suggested.stem().wstring() + (export_only ? L"-export.mid" : L"-edited.mid"));
		auto path = dialogs.save_midi(suggested.wstring());
		if (path.empty())
			return;

		start_job(export_only ? "Exporting MIDI..." : "Saving MIDI...",
			[this, path = std::move(path), export_only](std::stop_token)
		{
			write_document(path, export_only);
			completed_status = (export_only ? "Exported " : "Saved ") + utf8(path);
		});
	}

	void write_document(const std::wstring& path, bool export_only)
	{
		const auto output = std::filesystem::absolute(path);

		// A mapped source remains open for the lifetime of this document.
		// Replacing another output is transactional; the source is never truncated.
		const auto source_path = std::filesystem::absolute(document->get_filename());
		const auto normalized_output = std::filesystem::weakly_canonical(output.parent_path()) / output.filename();
		const auto normalized_source =
			std::filesystem::weakly_canonical(source_path.parent_path()) / source_path.filename();

		if (_wcsicmp(normalized_output.c_str(), normalized_source.c_str()) == 0)
			throw std::runtime_error("Choose a file different from the currently mapped source MIDI.");

		const auto temporary = output.parent_path() /
			(L".safc-editor-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(GetTickCount64()) +
				L".mid");
		// CREATE_NEW reserves this exact temporary path without replacing
		// anything left by an earlier run or another document.

		const auto handle =
			CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);

		if (handle == INVALID_HANDLE_VALUE)
			throw std::runtime_error("Could not create a temporary MIDI beside the output.");

		CloseHandle(handle);
		struct cleanup
		{
			std::filesystem::path path;
			~cleanup()
			{
				std::error_code error;
				std::filesystem::remove(path, error);
			}
		} cleanup_temporary{temporary};

		if (!document->export_current(temporary.wstring()))
			throw std::runtime_error("Could not write the edited MIDI.");

		if (!MoveFileExW(temporary.c_str(), output.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
			throw std::runtime_error("Could not install edited MIDI: Windows error " + std::to_string(GetLastError()));

		if (!export_only)
			document->mark_saved();
	}

	void play(bool from_view)
	{
		const auto playback_state = playback.snapshot();
		if (playback_state.busy)
		{
			playback.stop();
			return;
		}

		if (busy || !document->is_file_loaded())
			return;

		const auto start_seconds = from_view ? document->get_seconds_at_tick(document->get_view_start_tick()) : 0.;

		start_job("Preparing editor playback...", [this, start_seconds](std::stop_token stop)
		{
			auto source = document->make_playback_source();
			if (stop.stop_requested())
				return;

			prepared_seek = source->total_duration_us()
				? std::clamp(start_seconds * 1000000. / double(source->total_duration_us()), 0., 1.)
				: 0.;

			prepared_source.reset(static_cast<midi_editor::editor_event_source*>(source.release()));
			completed_status = "Playing a snapshot of the current edits.";
		});
	}

	tick snap_ticks() const
	{
		static constexpr int denominators[] = {3, 4, 6, 8, 12, 16, 24, 32, 0};
		return denominators[snap_index] ? std::max<tick>(1, tick(document->get_ppqn()) * 4 / denominators[snap_index])
										: 1;
	}
	tick snapped(tick value, bool bypass = false) const
	{
		const auto cell = bypass ? tick(1) : snap_ticks();
		return (value / cell) * cell;
	}

	int channel() const { return draw_channel >= 0 ? draw_channel : document->get_active_track_channel(); }
	lane_type control_lane() const
	{
		switch (lane)
		{
		case lane_kind::pitch_bend:
			return lane_type::pitch_bend;
		case lane_kind::pan:
			return lane_type::pan;
		default:
			return lane_type::channel_volume;
		}
	}
	double lane_max() const { return lane == lane_kind::pitch_bend ? pitch_bend_max : midi_value_max; }

	void audition(int key)
	{
		if (key == audition_key)
			return;
		if (audition_key >= 0)
			playback.audition_note(std::uint8_t(audition_key), 0, std::uint8_t(audition_channel), false);

		audition_key = key;
		if (key >= 0)
		{
			audition_channel = channel();
			playback.audition_note(
				std::uint8_t(key), std::uint8_t(draw_velocity), std::uint8_t(audition_channel), true);
		}
	}

	void apply_control_gesture()
	{
		if (control_values.empty())
			return;

		if (lane == lane_kind::tempo)
		{
			document->set_tempo_points({control_values.begin(), control_values.end()});
			return;
		}

		std::vector<std::pair<tick, std::uint16_t>> values;
		values.reserve(control_values.size());
		for (const auto& [position, value] : control_values)
			values.emplace_back(position, std::uint16_t(std::lround(value)));

		document->set_channel_control_points(
			document->get_active_track(), std::uint8_t(channel()), control_lane(), std::move(values));
	}

	void apply_gesture()
	{
		switch (gesture)
		{
		case gesture_kind::draw:
			document->insert_note(anchor_tick,
				std::max(anchor_tick + gesture_length, snapped(current_tick) + snap_ticks()), std::uint8_t(current_key),
				std::uint8_t(draw_velocity), std::uint8_t(channel()), document->get_active_track());
			break;
		case gesture_kind::select:
			document->select_rect(std::min(anchor_tick, current_tick), std::max(anchor_tick, current_tick) + 1,
				std::uint8_t(std::min(anchor_key, current_key)), std::uint8_t(std::max(anchor_key, current_key)),
				document->get_active_track(), selection_mode);
			break;
		case gesture_kind::erase:
		{
			std::vector<midi_editor::note_id_type> ids(erased_notes.begin(), erased_notes.end());
			const auto count = document->erase_notes(std::move(ids));
			if (count)
				status = "Erased " + std::to_string(count) + (count == 1 ? " note." : " notes.");
			break;
		}
		case gesture_kind::split:
		{
			const auto count = document->split_notes_at(std::move(split_cuts));
			status = count
				? "Split " + std::to_string(count) + (count == 1 ? " note; shorter piece selected."
					: " notes; shorter pieces selected.")
				: "The split line did not cross the middle of an active-track note.";
			break;
		}
		case gesture_kind::move:
			if (delta_tick || delta_key)
				document->move_selected_notes(delta_tick, delta_key);
			break;
		case gesture_kind::resize:
		{
			if (!delta_tick)
				break;
			std::vector<midi_editor::note_id_type> ids;
			ids.reserve(gesture_notes.size());
			for (const auto& value : gesture_notes)
				ids.push_back(value.id);

			document->resize_notes_by(std::move(ids), delta_tick);
			break;
		}
		case gesture_kind::stretch:
			document->stretch_selected_notes(stretch_factor, stretch_begin);
			break;
		case gesture_kind::control:
			apply_control_gesture();
			break;
		case gesture_kind::velocity:
		{
			std::vector<velocity_entry> entries;
			entries.reserve(velocities.size());
			for (const auto& [id, value] : velocities)
			{
				std::uint8_t previous;
				document->set_note_velocity_transient(value.note, value.new_velocity, previous);
				entries.push_back(value);
			}
			document->commit_velocity_gesture(std::move(entries));
			break;
		}
		default:
			break;
		}
	}

	void finish_gesture(bool apply)
	{
		audition(-1);
		if (apply)
			apply_gesture();
		gesture = gesture_kind::none;
		gesture_notes.clear();
		erased_notes.clear();
		split_cuts.clear();
		velocities.clear();
		control_values.clear();
		delta_tick = 0;
		delta_key = 0;
	}

	void open_tool(tool_kind value)
	{
		if (busy || !document->is_file_loaded())
			return;

		finish_gesture(false);
		document->cancel_tool_preview();
		tool = value;
		tool_open = true;
		preview_dirty = true;

		if (tool == tool_kind::lfo)
		{
			lfo_center = lane == lane_kind::pitch_bend ? float(pitch_bend_center) : 64.f;
			lfo_range = lane == lane_kind::pitch_bend ? float(pitch_bend_max - pitch_bend_center) : 63.f;
		}
	}

	void preview()
	{
		if (busy || tool == tool_kind::none)
			return;

		preview_dirty = false;
		const auto kind = tool;
		const auto begin_initial = document->get_view_start_tick();
		tick begin = begin_initial, end = begin + document->get_view_duration_ticks();
		std::uint8_t low = 0, high = midi_key_max;

		document->get_selection_bounds(begin, end, low, high);

		const auto current_channel = channel();
		const auto step = snap_ticks();

		start_job("Updating tool preview...", [=, this](std::stop_token)
		{
			switch (kind)
			{
			case tool_kind::chopper:
				document->chop_tool(divisions, time_multiplier, gap, absolute_pattern, true);
				break;
			case tool_kind::flip:
				document->flip_tool(horizontal, preserve_starts, vertical, true);
				break;
			case tool_kind::claw:
				document->claw_tool(period, trash_every, distortion, remove_short, compensate, true);
				break;
			case tool_kind::lfo:
			{
				if (lane == lane_kind::velocity)
					document->lfo_velocity_tool(
						lfo_center, lfo_range, lfo_cycles, lfo_phase / 360., midi_editor::lfo_shape(lfo_shape), true);
				else if (lane != lane_kind::tempo)
					document->lfo_control_tool(control_lane(), std::uint8_t(current_channel), begin, end, step,
						lfo_center, lfo_range, lfo_cycles, lfo_phase / 360., midi_editor::lfo_shape(lfo_shape), true);
				break;
			}
			default:
				break;
			}
			completed_status = "Preview ready. Accept commits one undo step; Cancel restores the document.";
		});
	}

	void draw_tool()
	{
		if (tool == tool_kind::none)
			return;
		if (!tool_open && !busy)
		{
			document->cancel_tool_preview();
			tool = tool_kind::none;
			status = "Preview cancelled.";
			return;
		}

		const char* title = tool == tool_kind::chopper ? "Chopper"
			: tool == tool_kind::flip				  ? "Flip score"
			: tool == tool_kind::claw				  ? "Claw machine"
													   : "LFO";
		ImGui::SetNextWindowSize(ImVec2(390, tool == tool_kind::flip ? 245.f : 355.f), ImGuiCond_FirstUseEver);
		bool open = tool_open;

		if (begin_folded_window(title, &open))
		{
			ImGui::TextWrapped("Preview affects selected notes, or the active track when nothing is selected.");
			ImGui::Separator();
			ImGui::BeginDisabled(busy);
			bool changed = false;

			if (tool == tool_kind::chopper)
			{
				changed |= ImGui::SliderInt("Slices / beat", &divisions, 1, 64);
				changed |= ImGui::SliderFloat(
					"Time multiplier", &time_multiplier, .0625f, 16.f, "%.3f", ImGuiSliderFlags_Logarithmic);
				changed |= ImGui::SliderFloat("Gap (%)", &gap, 0.f, 99.f, "%.1f");
				changed |= ImGui::Checkbox("Align pattern to score grid", &absolute_pattern);
			}
			else if (tool == tool_kind::flip)
			{
				changed |= ImGui::Checkbox("Horizontal", &horizontal);
				changed |= ImGui::Checkbox("Preserve start-time pattern", &preserve_starts);
				changed |= ImGui::Checkbox("Vertical", &vertical);
			}
			else if (tool == tool_kind::claw)
			{
				changed |=
					ImGui::SliderFloat("Period (beats)", &period, .0625f, 64.f, "%.3f", ImGuiSliderFlags_Logarithmic);
				changed |= ImGui::SliderInt("Remove every", &trash_every, 2, 64);
				changed |= ImGui::SliderFloat("Time distortion", &distortion, 0.f, 1.f);
				changed |= ImGui::Checkbox("Remove short notes", &remove_short);
				changed |= ImGui::Checkbox("Stretch to original length", &compensate);
			}
			else
			{
				const char* lane_names[] = {"Note velocity", "Pitch bend", "Pan", "Channel volume", "Tempo"};
				ImGui::Text("Target: %s", lane_names[static_cast<int>(lane)]);
				if (lane == lane_kind::tempo)
					ImGui::TextWrapped(
						"LFO supports velocity, pitch bend, pan and channel volume. Choose one of those lanes first.");
				changed |= ImGui::SliderFloat("Center", &lfo_center, 0.f, float(lane_max()), "%.0f");
				changed |= ImGui::SliderFloat("Range", &lfo_range, 0.f, float(lane_max()), "%.0f");
				changed |=
					ImGui::SliderFloat("Cycles", &lfo_cycles, .0625f, 64.f, "%.3f", ImGuiSliderFlags_Logarithmic);
				changed |= ImGui::SliderFloat("Phase (degrees)", &lfo_phase, 0.f, 360.f, "%.0f");
				changed |= ImGui::Combo("Shape", &lfo_shape, "Sine\0Triangle\0Square\0");
			}
			if (changed)
				preview_dirty = true;
			ImGui::Separator();
			ImGui::BeginDisabled(preview_dirty || (tool == tool_kind::lfo && lane == lane_kind::tempo));
			if (ImGui::Button("Accept", ImVec2(100, 0)))
			{
				document->accept_tool_preview();
				tool = tool_kind::none;
				tool_open = false;
				status = "Tool accepted as one undo step.";
			}
			ImGui::EndDisabled();
			ImGui::SameLine();

			if (ImGui::Button("Cancel", ImVec2(100, 0)))
				open = false;
			ImGui::EndDisabled();
			if (busy)
				ImGui::TextUnformatted(job_status.c_str());
		}
		end_folded_window();

		if (tool != tool_kind::none)
			tool_open = open;
		if (preview_dirty && tool_open && !busy)
			preview();
	}

	void shortcuts()
	{
		auto& io = ImGui::GetIO();
		if (!ImGui::IsWindowFocused(ImGuiFocusedFlags_RootAndChildWindows) || io.WantTextInput ||
			ImGui::IsAnyItemActive() || busy)
			return;

		if (ImGui::IsKeyPressed(ImGuiKey_Escape))
		{
			finish_gesture(false);
			return;
		}

		if (tool != tool_kind::none)
			return;

		if (io.KeyCtrl)
		{
			if (ImGui::IsKeyPressed(ImGuiKey_Z))
			{
				if (io.KeyShift)
					document->redo();
				else
					document->undo();
			}
			if (ImGui::IsKeyPressed(ImGuiKey_Y))
				document->redo();
			if (ImGui::IsKeyPressed(ImGuiKey_C))
				document->copy_selected_notes();
			if (ImGui::IsKeyPressed(ImGuiKey_X))
			{
				document->copy_selected_notes();
				document->delete_selected_notes();
			}
			if (ImGui::IsKeyPressed(ImGuiKey_V))
				document->paste_clipboard();
			if (ImGui::IsKeyPressed(ImGuiKey_B))
				document->duplicate_selected();
			if (ImGui::IsKeyPressed(ImGuiKey_A))
				document->select_rect(
					0, document->get_total_ticks() + 1, 0, midi_key_max, document->get_active_track());
			if (ImGui::IsKeyPressed(ImGuiKey_D))
				document->clear_selection();
			if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
				document->move_selected_notes(0, 12);
			if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
				document->move_selected_notes(0, -12);
			if (ImGui::IsKeyPressed(ImGuiKey_S))
				save(false);
			if (ImGui::IsKeyPressed(ImGuiKey_O) && dialogs.open_midi)
				request_load(dialogs.open_midi());
		}
		else if (io.KeyAlt)
		{
			if (ImGui::IsKeyPressed(ImGuiKey_V))
				ghosts = !ghosts;
			if (ImGui::IsKeyPressed(ImGuiKey_C))
				document->change_channel_selected(std::uint8_t(channel()));
			if (ImGui::IsKeyPressed(ImGuiKey_U))
				open_tool(tool_kind::chopper);
			if (ImGui::IsKeyPressed(ImGuiKey_Y))
				open_tool(tool_kind::flip);
			if (ImGui::IsKeyPressed(ImGuiKey_W))
				open_tool(tool_kind::claw);
			if (ImGui::IsKeyPressed(ImGuiKey_O))
				open_tool(tool_kind::lfo);
		}
		else
		{
			if (io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_C))
				document->select_channel(std::uint8_t(channel()), document->get_active_track());
			if (!io.KeyShift)
			{
				if (ImGui::IsKeyPressed(ImGuiKey_C))
					mode = edit_mode::split;
				if (ImGui::IsKeyPressed(ImGuiKey_P))
					mode = edit_mode::draw;
				if (ImGui::IsKeyPressed(ImGuiKey_E))
					mode = edit_mode::select;
				if (ImGui::IsKeyPressed(ImGuiKey_D))
					mode = edit_mode::erase;
			}
			if (ImGui::IsKeyPressed(ImGuiKey_Delete))
				document->delete_selected_notes();
			if (ImGui::IsKeyPressed(ImGuiKey_Space))
				play(true);
			if (ImGui::IsKeyPressed(ImGuiKey_LeftArrow))
				document->move_selected_notes(-std::int64_t(snap_ticks()), 0);
			if (ImGui::IsKeyPressed(ImGuiKey_RightArrow))
				document->move_selected_notes(std::int64_t(snap_ticks()), 0);
			if (ImGui::IsKeyPressed(ImGuiKey_UpArrow))
				document->move_selected_notes(0, 1);
			if (ImGui::IsKeyPressed(ImGuiKey_DownArrow))
				document->move_selected_notes(0, -1);
			if (ImGui::IsKeyPressed(ImGuiKey_Q))
				document->quantize_selected(snap_ticks());
		}
	}

	void draw_toolbar()
	{
		const bool loaded = document->is_file_loaded();
		ImGui::BeginDisabled(tool != tool_kind::none);
		if (ImGui::Button("Open MIDI...") && dialogs.open_midi)
			request_load(dialogs.open_midi());

		ImGui::SameLine();
		ImGui::BeginDisabled(!loaded || busy);
		if (ImGui::Button("Save as..."))
			save(false);

		ImGui::SameLine();
		if (ImGui::Button("Export MIDI..."))
			save(true);

		ImGui::SameLine();
		if (ImGui::Button("Play"))
			play(false);

		ImGui::SameLine();
		if (ImGui::Button("From view"))
			play(true);

		ImGui::SameLine();
		if (ImGui::Button("Stop"))
			playback.stop();

		ImGui::SameLine();
		if (ImGui::Button("Help"))
			help = !help;

		ImGui::EndDisabled();
		ImGui::EndDisabled();
		if (busy || !loaded)
			return;

		ImGui::BeginDisabled(tool != tool_kind::none);
		ImGui::BeginDisabled(!document->can_undo());
		if (ImGui::Button("Undo"))
			document->undo();

		ImGui::EndDisabled();
		ImGui::SameLine();
		ImGui::BeginDisabled(!document->can_redo());
		if (ImGui::Button("Redo"))
			document->redo();

		ImGui::EndDisabled();
		ImGui::SameLine();
		if (ImGui::Button("Copy"))
			document->copy_selected_notes();

		ImGui::SameLine();
		if (ImGui::Button("Paste"))
			document->paste_clipboard();

		ImGui::SameLine();
		if (ImGui::Button("Duplicate"))
			document->duplicate_selected();

		ImGui::SameLine();
		if (ImGui::Button("Delete"))
			document->delete_selected_notes();

		ImGui::SameLine();
		if (ImGui::Button("Quantize"))
			document->quantize_selected(snap_ticks());

		ImGui::SameLine();
		if (ImGui::Button("Chopper"))
			open_tool(tool_kind::chopper);

		ImGui::SameLine();
		if (ImGui::Button("Flip"))
			open_tool(tool_kind::flip);

		ImGui::SameLine();
		if (ImGui::Button("Claw"))
			open_tool(tool_kind::claw);

		ImGui::SameLine();
		if (ImGui::Button("LFO"))
			open_tool(tool_kind::lfo);

		ImGui::EndDisabled();
	}

	void draw_tracks()
	{
		const auto active = document->get_active_track();

		ImGui::TextUnformatted("TRACKS");
		ImGui::BeginChild(
			"Track list", ImVec2(0, std::max(80.f, ImGui::GetContentRegionAvail().y * .38f)), ImGuiChildFlags_Borders);

		for (const auto& [id, info] : document->get_tracks())
		{
			ImGui::PushID(int(id));
			const auto text = document->get_track_label(id);

			if (literal_selectable("##track", text, id == active))
			{
				finish_gesture(false);
				document->set_active_track(id);
			}

			ImGui::PopID();
		}

		ImGui::EndChild();

		if (named_track != document->get_active_track())
		{
			named_track = document->get_active_track();
			track_name.fill(0);
			const auto found = document->get_tracks().find(std::uint8_t(named_track));
			if (found != document->get_tracks().end())
				std::copy_n(found->second.name.data(), std::min(found->second.name.size(), track_name.size() - 1),
					track_name.data());
		}

		ImGui::SetNextItemWidth(-1);
		if (ImGui::InputText(
				"##Track name", track_name.data(), track_name.size(), ImGuiInputTextFlags_EnterReturnsTrue))
			document->set_track_name(document->get_active_track(), track_name.data());

		help_tip("Track name; press Enter to save the name into the MIDI.");

		ImGui::TextUnformatted("DRAW CHANNEL");
		if (ImGui::Selectable("Follow track", draw_channel < 0))
			draw_channel = -1;

		for (int ch = 0; ch < 16; ++ch)
		{
			ImGui::PushID(ch);
			ImGui::PushStyleColor(ImGuiCol_Button, note_color(active, ch, false));

			const auto label = std::to_string(ch + 1);
			if (ImGui::Button(label.c_str(), ImVec2(34, 0)))
				draw_channel = ch;

			if (channel() == ch)
			{
				const auto a = ImGui::GetItemRectMin(), b = ImGui::GetItemRectMax();
				ImGui::GetWindowDrawList()->AddRect(a, b, IM_COL32_WHITE, 2.f, 0, 2.f);
			}

			ImGui::PopStyleColor();
			ImGui::PopID();

			if (ch % 4 != 3)
				ImGui::SameLine();
		}
		if (ImGui::Button("Select channel"))
			document->select_channel(std::uint8_t(channel()), active);
		if (ImGui::Button("Assign to selection"))
			document->change_channel_selected(std::uint8_t(channel()));

		ImGui::SetNextItemWidth(-1);
		ImGui::SliderInt("##Velocity", &draw_velocity, 1, midi_value_max, "Velocity: %d");
		if (ImGui::Button("Set selected velocity"))
			document->change_velocity_selected(std::uint8_t(draw_velocity));

		ImGui::SetNextItemWidth(-1);
		ImGui::SliderFloat("##Scale", &scale_percent, 12.5f, 800.f, "Time scale: %.1f%%", ImGuiSliderFlags_Logarithmic);
		if (ImGui::Button("Scale selection"))
		{
			tick begin, end;
			std::uint8_t low, high;
			if (document->get_selection_bounds(begin, end, low, high))
				document->stretch_selected_notes(scale_percent / 100., begin);
		}

		ImGui::Checkbox("Ghost tracks", &ghosts);
		ImGui::Checkbox("Controller lane", &show_lane);
	}

	void paint_velocity(tick first, tick last, double first_value, double last_value)
	{
		const auto begin = std::min(first, last), end = std::max(first, last);
		const bool selected_only = document->has_selection();

		for (const auto& value : document->get_notes_in_range(begin, end + 1))
		{
			if (value.track_index != document->get_active_track() || value.start_tick < begin ||
				value.start_tick > end || (selected_only && !document->is_note_selected(value.id)))
				continue;

			const double fraction =
				first == last ? 1. : (double(value.start_tick) - double(first)) / (double(last) - double(first));
			const auto next = std::uint8_t(
				std::clamp<long>(std::lround(first_value + (last_value - first_value) * fraction), 1, midi_value_max));

			auto found = velocities.find(value.id);

			if (found == velocities.end())
				velocities.emplace(value.id, velocity_entry{value, value.velocity, next});
			else
				found->second.new_velocity = next;
		}
	}

	struct canvas_view
	{
		static constexpr float keyboard_width = 47.f;
		static constexpr float ruler_height = 24.f;
		static constexpr float divider_height = 6.f;
		static constexpr float minimum_lane_height = 40.f;
		static constexpr float maximum_lane_fraction = .55f;

		ImVec2 origin = ImGui::GetCursorScreenPos();
		ImVec2 size = {std::max(100.f, ImGui::GetContentRegionAvail().x),
			std::max(150.f, ImGui::GetContentRegionAvail().y - 34.f)};
		ImVec2 roll_min, roll_max, lane_min, lane_maximum;
		tick start, duration;
		int low, high, keys;
		float key_height, width;
		bool tempo_lane;
		double tempo_low, tempo_high, maximum_value;

		explicit canvas_view(impl& panel)
			: start(panel.document->get_view_start_tick()), duration(panel.document->get_view_duration_ticks()),
			  low(panel.document->get_view_key_low()), high(panel.document->get_view_key_high()), keys(high - low + 1),
			  tempo_lane(panel.lane == lane_kind::tempo), tempo_low(panel.tempo_low), tempo_high(panel.tempo_high),
			  maximum_value(panel.lane_max())
		{
			panel.lane_height = std::clamp(
				panel.lane_height, minimum_lane_height, std::max(minimum_lane_height, size.y * maximum_lane_fraction));
			const float lane_size = panel.show_lane ? panel.lane_height : 0.f;
			roll_min = {origin.x + keyboard_width, origin.y + ruler_height};
			roll_max = {origin.x + size.x, origin.y + size.y - lane_size - (panel.show_lane ? divider_height : 0.f)};
			lane_min = {roll_min.x, roll_max.y + divider_height};
			lane_maximum = {origin.x + size.x, origin.y + size.y};
			key_height = (roll_max.y - roll_min.y) / float(keys);
			width = roll_max.x - roll_min.x;
		}

		float x_at(double position) const
		{
			return roll_min.x + float((position - double(start)) / double(duration)) * width;
		}
		float y_at(int key) const { return roll_min.y + float(high - key) * key_height; }
		tick tick_at(float x) const
		{
			return tick(std::max(0., double(start) + double(x - roll_min.x) / width * double(duration)));
		}
		int key_at(float y) const
		{
			return std::clamp(high - int(std::floor((y - roll_min.y) / key_height)), 0, midi_key_max);
		}
		double value_at(float y) const
		{
			const double fraction = std::clamp(double(lane_maximum.y - y) / (lane_maximum.y - lane_min.y), 0., 1.);
			if (tempo_lane)
				return std::exp(std::log(tempo_low) + fraction * std::log(tempo_high / tempo_low));
			return fraction * maximum_value;
		}
		float lane_y(double value) const
		{
			const double normalized = tempo_lane
				? (std::log(std::max(.001, value)) - std::log(tempo_low)) / std::log(tempo_high / tempo_low)
				: value / maximum_value;
			return lane_maximum.y - float(std::clamp(normalized, 0., 1.)) * (lane_maximum.y - lane_min.y);
		}
	};

	std::pair<int, int> segment_key_range(const canvas_view& view, ImVec2 first, ImVec2 last) const
	{
		const float bottom = std::nextafter(view.roll_max.y, view.roll_min.y);
		const auto key_at = [&](float y)
		{
			return view.key_at(std::clamp(y, view.roll_min.y, bottom));
		};
		const int first_key = key_at(first.y), last_key = key_at(last.y);
		const int padding = std::max(1, int(std::ceil(2.f / view.key_height)));
		return {std::max(view.low, std::min(first_key, last_key) - padding),
			std::min(view.high, std::max(first_key, last_key) + padding)};
	}

	std::pair<tick, tick> segment_tick_range(const canvas_view& view, ImVec2 first, ImVec2 last) const
	{
		const tick first_tick = view.tick_at(std::min(first.x, last.x));
		const tick last_tick = view.tick_at(std::max(first.x, last.x));
		const tick padding = std::max<tick>(1,
			tick(std::ceil(double(view.duration) * 2. / double(view.width))));
		const tick begin = first_tick > padding ? first_tick - padding : 0;
		const tick remaining = std::numeric_limits<tick>::max() - last_tick;
		const tick requested_padding = padding == std::numeric_limits<tick>::max() ? padding : padding + 1;
		const tick end_padding = std::min(remaining, requested_padding);
		return {begin, last_tick + end_padding};
	}

	void collect_erased_notes(const canvas_view& view, ImVec2 first, ImVec2 last)
	{
		if (!clip_segment_to_rect(first, last, view.roll_min, view.roll_max))
			return;

		const auto [begin, end] = segment_tick_range(view, first, last);
		const auto [low, high] = segment_key_range(view, first, last);
		const auto active_track = document->get_active_track();
		for (const auto& value : document->get_notes_in_range(
			begin, end, std::uint8_t(low), std::uint8_t(high)))
		{
			if (value.track_index != active_track)
				continue;

			const float x0 = view.x_at(double(value.start_tick));
			const float x1 = std::max(x0 + 2.f, view.x_at(double(value.end_tick)));
			const float note_y = view.y_at(value.key);
			const float y0 = note_y + .5f;
			const float y1 = note_y + std::max(2.f, view.key_height - .5f);
			if (segment_intersects_rect(first, last, ImVec2(x0, y0), ImVec2(x1, y1)))
				erased_notes.insert(value.id);
		}
	}

	std::vector<std::pair<midi_editor::note_id_type, tick>> find_split_cuts(
		const canvas_view& view, ImVec2 first, ImVec2 last) const
	{
		std::vector<std::pair<midi_editor::note_id_type, tick>> result;
		if (!clip_segment_to_rect(first, last, view.roll_min, view.roll_max) ||
			std::abs(last.y - first.y) < .5f)
			return result;

		const auto [begin, end] = segment_tick_range(view, first, last);
		const auto [low, high] = segment_key_range(view, first, last);
		const double delta_y = double(last.y - first.y);
		const auto active_track = document->get_active_track();
		for (const auto& value : document->get_notes_in_range(
			begin, end, std::uint8_t(low), std::uint8_t(high)))
		{
			if (value.track_index != active_track)
				continue;

			const double center_y = double(view.y_at(value.key) + view.key_height * .5f);
			const double ratio = (center_y - double(first.y)) / delta_y;
			if (ratio < 0. || ratio > 1.)
				continue;

			const double x = double(first.x) + double(last.x - first.x) * ratio;
			const double position = double(view.start) +
				(x - double(view.roll_min.x)) / double(view.width) * double(view.duration);
			if (!std::isfinite(position) || position < 0. ||
				position > double(std::numeric_limits<tick>::max()))
				continue;

			const auto cut = tick(std::llround(position));
			if (cut > value.start_tick && cut < value.end_tick)
				result.emplace_back(value.id, cut);
		}

		return result;
	}

	void draw_piano_roll(const canvas_view& view)
	{
		auto* draw = ImGui::GetWindowDrawList();
		for (int key = view.low; key <= view.high; ++key)
		{
			const float y = view.y_at(key);
			const int semitone = key % 12;
			const bool black = semitone == 1 || semitone == 3 || semitone == 6 || semitone == 8 || semitone == 10;

			draw->AddRectFilled(ImVec2(view.origin.x, y), ImVec2(view.roll_min.x, y + view.key_height),
				audition_key == key ? IM_COL32(69, 151, 217, 255)
					: black		 ? IM_COL32(23, 41, 57, 255)
									: IM_COL32(146, 166, 183, 255));

			draw->AddRectFilled(ImVec2(view.roll_min.x, y), ImVec2(view.roll_max.x, y + view.key_height),
				black ? IM_COL32(9, 24, 36, 255) : IM_COL32(14, 31, 45, 255));

			draw->AddLine(ImVec2(view.origin.x, y), ImVec2(view.roll_max.x, y), IM_COL32(46, 68, 85, 90));

			if (semitone == 0 && view.key_height >= 8)
			{
				const auto label = "C" + std::to_string(key / 12 - 1);
				draw->AddText(ImVec2(view.origin.x + 4, y), IM_COL32(20, 36, 47, 255), label.c_str());
			}
		}

		tick grid = snap_ticks();
		while (double(grid) / double(view.duration) * view.width < 14.)
			grid *= 2;

		const tick beat = std::max<tick>(1, document->get_ppqn());
		for (tick position = (view.start / grid) * grid; position <= view.start + view.duration; position += grid)
		{
			const float x = view.x_at(double(position));
			if (x < view.roll_min.x)
				continue;

			const bool bar = position % (beat * 4) == 0;
			draw->AddLine(ImVec2(x, view.roll_min.y), ImVec2(x, view.roll_max.y),
				bar ? IM_COL32(92, 139, 174, 170) : IM_COL32(57, 83, 104, 100));

			if (bar || (grid >= beat && view.width * double(grid) / double(view.duration) > 50))
			{
				const auto label =
					std::to_string(position / beat / 4 + 1) + "." + std::to_string(position / beat % 4 + 1);
				draw->AddText(ImVec2(x + 3, view.origin.y + 3), IM_COL32(155, 188, 211, 255), label.c_str());
			}

			if (position > std::numeric_limits<tick>::max() - grid)
				break;
		}

		const auto visible = document->get_notes_in_range(
			view.start, view.start + view.duration, std::uint8_t(view.low), std::uint8_t(view.high));
		const auto selected = document->get_selected_ids_in_range(
			view.start, view.start + view.duration, std::uint8_t(view.low), std::uint8_t(view.high));
		draw->PushClipRect(view.roll_min, view.roll_max, true);

		auto render_note =
			[&](const note& value, bool ghost, bool preview, double first = -1., double last = -1., int pitch = -1)
		{
			const float x0 = view.x_at(first >= 0 ? first : double(value.start_tick));
			const float x1 = std::max(x0 + 2.f, view.x_at(last >= 0 ? last : double(value.end_tick)));
			const float y0 = view.y_at(pitch >= 0 ? pitch : value.key);
			const ImVec2 a(x0, y0 + .5f), b(x1, y0 + std::max(2.f, view.key_height - .5f));
			const auto color =
				note_color(value.track_index, value.channel, preview || selected.contains(value.id), ghost);
			draw->AddRectFilled(a, b, color, 1.f);
			if (view.key_height >= 5 && x1 - x0 >= 5)
				draw->AddRect(a, b, preview ? IM_COL32_WHITE : IM_COL32(5, 17, 28, 155), 1.f);
		};

		if (ghosts)
		{
			for (const auto& value : visible)
			{
				if (value.track_index == document->get_active_track())
					continue;

				render_note(value, true, false);
			}
		}
		for (const auto& value : visible)
		{
			if (value.track_index != document->get_active_track())
				continue;
			if (gesture == gesture_kind::erase && erased_notes.contains(value.id))
				continue;

			render_note(value, false, false);
		}

		if (gesture == gesture_kind::move || gesture == gesture_kind::resize || gesture == gesture_kind::stretch)
		{
			for (const auto& value : gesture_notes)
			{
				auto first = static_cast<double>(value.start_tick);
				auto last = static_cast<double>(value.end_tick);
				int pitch = value.key;

				if (gesture == gesture_kind::move)
				{
					first += static_cast<double>(delta_tick);
					last += static_cast<double>(delta_tick);
					pitch += delta_key;
				}
				else if (gesture == gesture_kind::resize)
				{
					last = std::max(first + 1., last + static_cast<double>(delta_tick));
				}
				else
				{
					first = static_cast<double>(stretch_begin) +
						(first - static_cast<double>(stretch_begin)) * stretch_factor;
					last = static_cast<double>(stretch_begin) +
						(last - static_cast<double>(stretch_begin)) * stretch_factor;
				}

				render_note(value, false, true, first, last, std::clamp(pitch, 0, midi_key_max));
			}
		}
		else if (gesture == gesture_kind::draw)
		{
			note value(anchor_tick, std::max(anchor_tick + gesture_length, snapped(current_tick) + snap_ticks()),
				std::uint8_t(current_key), std::uint8_t(draw_velocity), std::uint8_t(channel()),
				document->get_active_track());

			render_note(value, false, true);
		}
		else if (gesture == gesture_kind::select)
		{
			const ImVec2 a(
				view.x_at(double(std::min(anchor_tick, current_tick))), view.y_at(std::max(anchor_key, current_key)));
			const ImVec2 b(view.x_at(double(std::max(anchor_tick, current_tick))),
				view.y_at(std::min(anchor_key, current_key)) + view.key_height);

			draw->AddRectFilled(a, b, IM_COL32(70, 169, 230, 55));
			draw->AddRect(a, b, IM_COL32(119, 207, 255, 255));
		}
		else if (gesture == gesture_kind::split)
		{
			auto first = anchor_mouse, last = current_mouse;
			if (clip_segment_to_rect(first, last, view.roll_min, view.roll_max))
			{
				draw->AddLine(first, last, IM_COL32(255, 104, 111, 255), 2.f);
				draw->AddCircleFilled(first, 3.f, IM_COL32(255, 211, 112, 255));
				draw->AddCircleFilled(last, 3.f, IM_COL32(255, 211, 112, 255));
			}
		}
		const auto playback_state = playback.snapshot();
		if (playback_state.busy && active_playback_source && playback.current_source() == active_playback_source)
		{
			const float x =
				view.x_at(double(document->get_tick_at_seconds(double(playback_state.position_us) / 1000000.)));
			draw->AddLine(ImVec2(x, view.roll_min.y), ImVec2(x, view.roll_max.y), IM_COL32(255, 200, 92, 255), 2.f);
		}

		draw->PopClipRect();
	}

	void draw_controller_lane(const canvas_view& view)
	{
		auto* draw = ImGui::GetWindowDrawList();

		draw->AddRectFilled(ImVec2(view.origin.x, view.roll_max.y),
			ImVec2(view.origin.x + view.size.x, view.lane_min.y), IM_COL32(44, 72, 94, 255));
		draw->AddRectFilled(ImVec2(view.origin.x, view.lane_min.y), view.lane_maximum, IM_COL32(7, 23, 36, 255));
		const char* labels[] = {"Vel", "Bend", "Pan", "Vol", "BPM"};

		draw->AddText(ImVec2(view.origin.x + 3, view.lane_min.y + 4), IM_COL32(144, 188, 219, 255),
			labels[static_cast<int>(lane)]);
		draw->PushClipRect(view.lane_min, view.lane_maximum, true);

		if (lane == lane_kind::velocity)
		{
			for (const auto& value : document->get_notes_in_range(view.start, view.start + view.duration))
			{
				if (value.track_index != document->get_active_track())
					continue;

				const auto pending_velocity = velocities.find(value.id);

				const float x = view.x_at(double(value.start_tick)),
					y = view.lane_y(pending_velocity == velocities.end()
						? value.velocity
						: pending_velocity->second.new_velocity);

				const auto color = note_color(value.track_index, value.channel, document->is_note_selected(value.id));

				draw->AddLine(ImVec2(x, view.lane_maximum.y), ImVec2(x, y), color, 2.f);
				draw->AddCircleFilled(ImVec2(x, y), 3.f, color);
			}
		}
		else
		{
			std::vector<std::pair<tick, double>> points;

			if (lane == lane_kind::tempo)
				points = document->get_tempo_points(view.start, view.start + view.duration);
			else
			{
				auto control_points = document->get_channel_control_points(document->get_active_track(),
					std::uint8_t(channel()), control_lane(), view.start, view.start + view.duration);

				for (const auto& value : control_points)
					points.emplace_back(value.tick, value.value);
			}

			ImVec2 previous{};
			bool first = true;
			for (const auto& [position, value] : points)
			{
				const ImVec2 point(view.x_at(double(position)), view.lane_y(value));
				if (!first)
				{
					draw->AddLine(previous, ImVec2(point.x, previous.y), IM_COL32(70, 174, 229, 255), 2.f);
					draw->AddLine(ImVec2(point.x, previous.y), point, IM_COL32(70, 174, 229, 255));
				}

				draw->AddCircleFilled(point, 3.f, IM_COL32(135, 214, 255, 255));
				previous = point;
				first = false;
			}

			if (!first)
				draw->AddLine(previous, ImVec2(view.lane_maximum.x, previous.y), IM_COL32(70, 174, 229, 255), 2.f);

			for (const auto& [position, value] : control_values)
			{
				draw->AddCircleFilled(
					ImVec2(view.x_at(double(position)), view.lane_y(value)), 3.f, IM_COL32(255, 210, 110, 255));
			}
		}

		if (line_gesture && (gesture == gesture_kind::velocity || gesture == gesture_kind::control))
		{
			draw->AddLine(ImVec2(view.x_at(double(anchor_tick)), view.lane_y(anchor_value)), current_mouse,
				IM_COL32(255, 211, 112, 255), 2.f);
		}

		draw->PopClipRect();
	}

	void handle_canvas_wheel(const canvas_view& view)
	{
		auto& io = ImGui::GetIO();

		const auto mouse = io.MousePos;
		const bool in_roll = contains(mouse, view.roll_min, view.roll_max);
		const bool in_keys =
			contains(mouse, ImVec2(view.origin.x, view.roll_min.y), ImVec2(view.roll_min.x, view.roll_max.y));
		const bool in_lane = show_lane && contains(mouse, view.lane_min, view.lane_maximum);

		if (in_keys)
		{
			const int count = std::clamp(int(std::lround(view.keys / std::pow(1.2, io.MouseWheel))), 5, midi_key_count);
			const int cursor_key = view.key_at(mouse.y);
			const double fraction = double(view.high - cursor_key) / view.keys;
			const int next_high = std::clamp(cursor_key + int(count * fraction), count - 1, midi_key_max);

			document->set_view_keys(std::uint8_t(next_high - count + 1), std::uint8_t(next_high));
		}
		else if (in_roll || in_lane)
		{
			if (io.KeyShift)
			{
				const auto shift = std::int64_t(double(view.duration) * .1 * io.MouseWheel);

				document->set_view_range(
					tick(std::max<std::int64_t>(0, std::int64_t(view.start) - shift)), view.duration);
			}
			else
			{
				const double fraction = std::clamp(double(mouse.x - view.roll_min.x) / view.width, 0., 1.);
				const double next_duration =
					std::clamp(double(view.duration) / std::pow(1.25, io.MouseWheel), 16., 1.e15);
				const tick next_start = tick(std::max(0., double(view.tick_at(mouse.x)) - fraction * next_duration));

				document->set_view_range(next_start, tick(next_duration));
			}
		}
	}

	void begin_canvas_gesture(const canvas_view& view)
	{
		auto& io = ImGui::GetIO();

		const auto mouse = io.MousePos;
		const bool in_roll = contains(mouse, view.roll_min, view.roll_max);
		const bool in_keys =
			contains(mouse, ImVec2(view.origin.x, view.roll_min.y), ImVec2(view.roll_min.x, view.roll_max.y));
		const bool in_lane = show_lane && contains(mouse, view.lane_min, view.lane_maximum);

		if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) && (in_roll || in_lane))
		{
			gesture = gesture_kind::pan;
			gesture_button = ImGuiMouseButton_Middle;
			anchor_mouse = mouse;
			pan_start = view.start;
		}
		else if (ImGui::IsMouseClicked(ImGuiMouseButton_Right) && in_keys)
		{
			gesture = gesture_kind::keyboard_pan;
			gesture_button = ImGuiMouseButton_Right;
			anchor_mouse = mouse;
			pan_low = view.low;
			pan_high = view.high;
		}
		else if (show_lane && ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
			contains(mouse, ImVec2(view.origin.x, view.roll_max.y), ImVec2(view.roll_max.x, view.lane_min.y)))
		{
			gesture = gesture_kind::divider;
			gesture_button = ImGuiMouseButton_Left;
		}
		else if (in_keys && ImGui::IsMouseDown(ImGuiMouseButton_Left))
		{
			audition(view.key_at(mouse.y));
		}
		else if (in_roll && ImGui::IsMouseClicked(ImGuiMouseButton_Right))
		{
			note hit;
			const auto position = view.tick_at(mouse.x);
			const auto key = std::uint8_t(view.key_at(mouse.y));
			const bool active_note = document->find_note_at(
				position, key, hit, 0, 0, document->get_active_track());
			if (!active_note && ghosts && document->find_note_at(position, key, hit))
				document->set_active_track(std::uint8_t(hit.track_index));
			else
			{
				gesture = gesture_kind::erase;
				gesture_button = ImGuiMouseButton_Right;
				anchor_mouse = current_mouse = mouse;
				erased_notes.clear();
				collect_erased_notes(view, mouse, mouse);
			}
		}
		else if (in_roll && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
		{
			anchor_mouse = current_mouse = mouse;
			anchor_tick = current_tick = view.tick_at(mouse.x);
			anchor_key = current_key = view.key_at(mouse.y);
			gesture_button = ImGuiMouseButton_Left;
			note hit;

			const bool found =
				document->find_note_at(anchor_tick, std::uint8_t(anchor_key), hit, 0, 0, document->get_active_track());
			if (mode == edit_mode::erase)
			{
				gesture = gesture_kind::erase;
				erased_notes.clear();
				collect_erased_notes(view, mouse, mouse);
			}
			else if (mode == edit_mode::split)
			{
				gesture = gesture_kind::split;
				split_cuts.clear();
			}
			else if (io.KeyShift || (mode == edit_mode::select && !found))
			{
				gesture = gesture_kind::select;
				selection_mode = io.KeyShift
					? (io.KeyAlt ? midi_editor::select_mode::remove : midi_editor::select_mode::add)
					: midi_editor::select_mode::replace;
			}
			else if (found)
			{
				draw_length = hit.length();
				draw_velocity = hit.velocity;
				draw_channel = hit.channel;

				if (!document->is_note_selected(hit.id))
					document->select_note(
						hit.id, io.KeyCtrl ? midi_editor::select_mode::add : midi_editor::select_mode::replace);

				gesture_notes = document->get_selected_notes();
				gesture = (view.x_at(double(hit.end_tick)) - mouse.x <= note_resize_handle_width) ? gesture_kind::resize
																								  : gesture_kind::move;
				if (gesture == gesture_kind::resize && io.KeyCtrl)
				{
					std::uint8_t low_key, high_key;
					document->get_selection_bounds(stretch_begin, stretch_end, low_key, high_key);
					gesture = gesture_kind::stretch;
					stretch_factor = 1.;
				}

				audition(hit.key);
			}
			else
			{
				gesture = gesture_kind::draw;
				anchor_tick = snapped(anchor_tick, io.KeyAlt);
				gesture_length = draw_length ? draw_length : snap_ticks();

				audition(anchor_key);
			}
		}
		else if (in_lane &&
			(ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right)))
		{
			gesture = lane == lane_kind::velocity ? gesture_kind::velocity : gesture_kind::control;
			line_gesture = ImGui::IsMouseClicked(ImGuiMouseButton_Right);
			gesture_button = line_gesture ? ImGuiMouseButton_Right : ImGuiMouseButton_Left;
			anchor_mouse = current_mouse = mouse;
			anchor_tick = current_tick = view.tick_at(mouse.x);
			anchor_value = view.value_at(mouse.y);

			if (gesture == gesture_kind::velocity && !line_gesture)
			{
				const tick radius = std::max<tick>(1, tick(double(view.duration) * velocity_brush_radius / view.width));
				paint_velocity(
					anchor_tick > radius ? anchor_tick - radius : 0, anchor_tick + radius, anchor_value, anchor_value);
			}
		}
	}

	void update_canvas_gesture(const canvas_view& view)
	{
		auto& io = ImGui::GetIO();
		const auto mouse = io.MousePos;

		const auto previous_mouse = current_mouse;
		current_mouse = mouse;
		const auto previous_tick = current_tick;
		current_tick = view.tick_at(mouse.x);
		current_key = view.key_at(mouse.y);
		if (gesture == gesture_kind::pan)
		{
			document->set_view_range(
				tick(std::max(0., double(pan_start) - (mouse.x - anchor_mouse.x) / view.width * double(view.duration))),
				view.duration);
		}
		else if (gesture == gesture_kind::keyboard_pan)
		{
			const int shift = int((mouse.y - anchor_mouse.y) / view.key_height);
			const int next_low = std::clamp(pan_low + shift, 0, midi_key_max - (pan_high - pan_low));

			document->set_view_keys(std::uint8_t(next_low), std::uint8_t(next_low + pan_high - pan_low));
		}
		else if (gesture == gesture_kind::divider)
			lane_height = std::clamp(view.origin.y + view.size.y - mouse.y, canvas_view::minimum_lane_height,
				view.size.y * canvas_view::maximum_lane_fraction);
		else if (gesture == gesture_kind::draw)
			audition(current_key);
		else if (gesture == gesture_kind::erase)
			collect_erased_notes(view, previous_mouse, current_mouse);
		else if (gesture == gesture_kind::move || gesture == gesture_kind::resize)
		{
			const tick cell = io.KeyAlt ? 1 : snap_ticks();
			delta_tick = std::int64_t(std::llround((double(current_tick) - double(anchor_tick)) / double(cell))) *
				std::int64_t(cell);
			delta_key = current_key - anchor_key;

			if (gesture == gesture_kind::move && !gesture_notes.empty())
			{
				tick earliest = gesture_notes.front().start_tick;
				int lowest = gesture_notes.front().key, highest = lowest;
				for (const auto& value : gesture_notes)
				{
					earliest = std::min(earliest, value.start_tick);
					lowest = std::min(lowest, int(value.key));
					highest = std::max(highest, int(value.key));
				}

				delta_tick = std::max(delta_tick, -std::int64_t(earliest));
				delta_key = std::clamp(delta_key, -lowest, midi_key_max - highest);
			}
		}
		else if (gesture == gesture_kind::stretch)
			stretch_factor = std::max(1. / std::max(1., double(stretch_end - stretch_begin)),
				(double(snapped(current_tick, io.KeyAlt)) - double(stretch_begin)) /
					std::max(1., double(stretch_end - stretch_begin)));
		else if (gesture == gesture_kind::velocity && !line_gesture)
			paint_velocity(
				previous_tick, current_tick, view.value_at(io.MousePos.y - io.MouseDelta.y), view.value_at(mouse.y));
		else if (gesture == gesture_kind::control && !line_gesture)
			control_values[snapped(current_tick, io.KeyAlt)] = view.value_at(mouse.y);
		if (ImGui::IsKeyPressed(ImGuiKey_Escape))
			finish_gesture(false);
		else if (!ImGui::IsMouseDown(gesture_button))
		{
			if (gesture == gesture_kind::split)
				split_cuts = find_split_cuts(view, anchor_mouse, current_mouse);
			if (line_gesture && gesture == gesture_kind::velocity)
				paint_velocity(anchor_tick, current_tick, anchor_value, view.value_at(mouse.y));
			if (line_gesture && gesture == gesture_kind::control)
			{
				const auto first = std::min(anchor_tick, current_tick), last = std::max(anchor_tick, current_tick);
				const auto step = std::max<tick>(snap_ticks(), (last - first) / maximum_ramp_steps + 1);
				for (tick position = snapped(first); position <= last; position += step)
				{
					const auto fraction = anchor_tick == current_tick
						? 1.
						: (double(position) - double(anchor_tick)) / (double(current_tick) - double(anchor_tick));
					const double ratio = std::clamp(fraction, 0., 1.);
					control_values[position] = lane == lane_kind::tempo
						? std::exp(std::log(anchor_value) + std::log(view.value_at(mouse.y) / anchor_value) * ratio)
						: anchor_value + (view.value_at(mouse.y) - anchor_value) * ratio;
					if (position > std::numeric_limits<tick>::max() - step)
						break;
				}
			}
			finish_gesture(true);
		}
	}

	void handle_canvas_input(const canvas_view& view, bool hovered)
	{
		auto& io = ImGui::GetIO();

		const auto mouse = io.MousePos;
		const bool in_roll = contains(mouse, view.roll_min, view.roll_max);
		const bool in_keys =
			contains(mouse, ImVec2(view.origin.x, view.roll_min.y), ImVec2(view.roll_min.x, view.roll_max.y));
		const bool in_lane = show_lane && contains(mouse, view.lane_min, view.lane_maximum);

		if (hovered && io.MouseWheel != 0 && gesture == gesture_kind::none)
			handle_canvas_wheel(view);
		if (hovered && gesture == gesture_kind::none)
			begin_canvas_gesture(view);
		if (gesture != gesture_kind::none)
			update_canvas_gesture(view);

		if (gesture == gesture_kind::none && in_keys && ImGui::IsMouseDown(ImGuiMouseButton_Left))
			audition(view.key_at(mouse.y));
		if (in_lane && gesture == gesture_kind::none)
			ImGui::SetTooltip("Tick %llu | %s %.2f\nLeft-drag paints; right-drag draws a ramp.",
				static_cast<unsigned long long>(view.tick_at(mouse.x)), lane == lane_kind::tempo ? "BPM" : "Value",
				view.value_at(mouse.y));
	}

	void draw_time_scrollbar(const canvas_view& view)
	{
		ImGui::SetNextItemWidth(-1);
		const tick scroll_max = std::max(document->get_total_ticks(), view.start + view.duration);
		auto position = static_cast<double>(view.start);
		auto maximum = static_cast<double>(scroll_max);
		auto zero = 0.;

		if (ImGui::SliderScalar("##Time scroll", ImGuiDataType_Double, &position, &zero, &maximum, "Start tick: %.0f"))
			document->set_view_range(tick(position), view.duration);
	}

	void draw_canvas(bool interactive)
	{
		const canvas_view view(*this);
		ImGui::InvisibleButton("Piano roll canvas", view.size,
			ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight | ImGuiButtonFlags_MouseButtonMiddle);
		const bool hovered = ImGui::IsItemHovered();
		const bool active = ImGui::IsItemActive();
		auto* draw = ImGui::GetWindowDrawList();
		const ImVec2 corner(view.origin.x + view.size.x, view.origin.y + view.size.y);
		draw->AddRectFilled(view.origin, corner, IM_COL32(6, 18, 28, 255));
		draw->PushClipRect(view.origin, corner, true);
		draw_piano_roll(view);
		if (show_lane)
			draw_controller_lane(view);
		draw->AddRect(view.origin, corner, IM_COL32(59, 106, 143, 255));
		draw->PopClipRect();

		if (interactive && (hovered || active || gesture != gesture_kind::none))
			handle_canvas_input(view, hovered);
		if (!ImGui::IsMouseDown(ImGuiMouseButton_Left) && gesture == gesture_kind::none)
			audition(-1);
		draw_time_scrollbar(view);
	}

	void draw_document()
	{
		shortcuts();
		if (busy)
			return;

		ImGui::BeginDisabled(tool != tool_kind::none);
		ImGui::SetNextItemWidth(135);
		enum_combo("Tool", mode, "Draw / move\0Select\0Erase\0Split\0");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(100);
		ImGui::Combo("Snap", &snap_index,
			"1/4\0"
			"1/8\0"
			"1/16\0"
			"1/32\0"
			"1/64\0"
			"Off\0");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(125);
		enum_combo("Lane", lane, "Velocity\0Pitch bend\0Pan\0Volume\0Tempo\0");
		ImGui::SameLine();

		if (ImGui::Button("Fit"))
			document->reset_view_to_content();

		ImGui::SameLine();
		if (ImGui::Button("-"))
			document->zoom_out();

		ImGui::SameLine();
		if (ImGui::Button("+"))
			document->zoom_in();

		if (lane == lane_kind::tempo)
		{

			ImGui::SetNextItemWidth(105);
			ImGui::InputDouble("BPM min", &tempo_low, 0, 0, "%.3f");
			ImGui::SameLine();
			ImGui::SetNextItemWidth(105);
			ImGui::InputDouble("BPM max", &tempo_high, 0, 0, "%.3f");

			tempo_low = std::isfinite(tempo_low) ? 
				std::clamp(tempo_low, minimum_tempo_bpm, maximum_tempo_bpm - .001) : minimum_tempo_bpm;
			tempo_high = std::isfinite(tempo_high) ?
				std::clamp(tempo_high, tempo_low + .001, maximum_tempo_bpm) : maximum_tempo_bpm;

			ImGui::SameLine();

			if (ImGui::Button("20-400 BPM"))
			{
				tempo_low = 20.;
				tempo_high = 400.;
			}

			ImGui::SameLine();
			ImGui::SetNextItemWidth(105);
			ImGui::InputScalar("At tick", ImGuiDataType_U64, &tempo_tick);
			ImGui::SameLine();
			ImGui::SetNextItemWidth(95);
			ImGui::InputDouble("BPM", &tempo_point, 0, 0, "%.3f");
			ImGui::SameLine();

			if (ImGui::Button("Insert tempo") && std::isfinite(tempo_point))
				document->set_tempo_point(tempo_tick, tempo_point);
		}

		ImGui::EndDisabled();
		if (help)
		{
			ImGui::TextWrapped(
				"Draw: left-drag empty space. Move: drag a note. Resize: drag its right edge; Ctrl stretches the "
				"selection. Shift-drag selects; Shift+Alt removes. Right-drag erases every note crossed between "
				"frames, or selects a ghost track when pressed on one. Split: drag a vertical or diagonal cut line; "
				"each note is divided where the line crosses its middle, and its shorter piece is selected. "
				"Middle-drag pans; wheel zooms. Alt bypasses snap. Wheel on keys zooms pitch; right-drag keys scrolls. "
				"Controller lane: left paints, right draws a ramp.");
			ImGui::TextWrapped(
				"Ctrl+Z/Y undo/redo; Ctrl+C/X/V/B copy/cut/paste/duplicate; Ctrl+A/D select track/deselect; Shift+C "
				"selects channel; Alt+C assigns channel. P/E/D/C choose Draw/Select/Erase/Split. Arrows move; "
				"Ctrl+Up/Down transposes octaves. Q quantizes. "
				"Space plays from view. Alt+U/Y/W/O opens Chopper/Flip/Claw/LFO. Esc cancels a gesture.");
		}
		ImGui::Separator();
		const float footer = ImGui::GetTextLineHeightWithSpacing() * 2.f + 8;

		if (ImGui::BeginTable("Editor workspace", 2, ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV))
		{
			ImGui::TableSetupColumn("Tracks", ImGuiTableColumnFlags_WidthFixed, 181.f);
			ImGui::TableSetupColumn("Roll", ImGuiTableColumnFlags_WidthStretch);
			ImGui::TableNextColumn();
			ImGui::BeginChild("Track controls", ImVec2(0, std::max(180.f, ImGui::GetContentRegionAvail().y - footer)));
			ImGui::BeginDisabled(tool != tool_kind::none);
			draw_tracks();
			ImGui::EndDisabled();
			ImGui::EndChild();
			ImGui::TableNextColumn();

			ImGui::BeginChild("Roll panel", 
				ImVec2(0, std::max(180.f, ImGui::GetContentRegionAvail().y - footer)),
				ImGuiChildFlags_None,
				ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);
			draw_canvas(tool == tool_kind::none);
			ImGui::EndChild();
			ImGui::EndTable();
		}

		ImGui::Text("%s | %zu notes | %zu selected | PPQN %u%s",
			document->get_track_label(document->get_active_track()).c_str(), document->get_note_count(),
			document->selection_count(), unsigned(document->get_ppqn()), document->is_modified() ? " | Modified" : "");
		ImGui::TextUnformatted(status.c_str());
	}

	void draw(bool* open)
	{
		poll();
		const auto display = ImGui::GetIO().DisplaySize;
		ImGui::SetNextWindowPos({60, 86}, ImGuiCond_FirstUseEver);
		ImGui::SetNextWindowSize(
			{std::min(1180.f, display.x - 84.f), std::max(420.f, std::min(790.f, display.y - 116.f))},
			ImGuiCond_FirstUseEver);

		if (begin_folded_window("MIDI editor", open, ImGuiWindowFlags_NoScrollWithMouse))
		{
			if (busy.load(std::memory_order_acquire))
			{
				ImGui::TextUnformatted(job_status.c_str());
				const auto size = total.load();
				ImGui::ProgressBar(
					size ? float(double(progress.load()) / double(size)) : -float(ImGui::GetTime()), ImVec2(-1, 0));
				if (job_status == "Loading MIDI..." && ImGui::Button("Cancel load"))
					worker.request_stop();
				ImGui::TextWrapped("The editor document is owned by its worker until this operation finishes.");
			}
			else
			{
				draw_toolbar();
				const auto playback_state = playback.snapshot();
				if (playback_state.preparing)
				{
					ImGui::TextWrapped("%s", playback_state.message.c_str());
					ImGui::ProgressBar(playback_state.preparation_total
							? float(double(playback_state.preparation_completed) /
								  double(playback_state.preparation_total))
							: -float(ImGui::GetTime()),
						ImVec2(-1, 0));
				}
				else if (!playback_state.error.empty())
					ImGui::TextWrapped("%s", playback_state.error.c_str());

				if (!busy && document->is_file_loaded())
					draw_document();
				else if (!busy)
					ImGui::TextWrapped(
						"Open a MIDI file to begin. Unsaved edits can play and render through the shared player.");
			}
			if (ask_discard)
			{
				ImGui::OpenPopup("Replace edited MIDI?");
				ask_discard = false;
			}
			if (ImGui::BeginPopupModal("Replace edited MIDI?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
			{
				ImGui::TextWrapped("This MIDI has unsaved edits. Opening another file will replace them.");
				if (ImGui::Button("Open requested MIDI"))
				{
					auto path = std::move(requested_path);
					ImGui::CloseCurrentPopup();
					load(std::move(path));
				}

				ImGui::SameLine();
				if (ImGui::Button("Keep editing"))
				{
					requested_path.clear();
					ImGui::CloseCurrentPopup();
				}

				ImGui::EndPopup();
			}
		}

		end_folded_window();

		if (open && !*open)
		{
			if (!busy)
				finish_gesture(false);
			tool_open = false;
		}

		draw_tool();
	}

	void shutdown()
	{
		if (stopped)
			return;

		stopped = true;
		if (worker.joinable())
		{
			worker.request_stop();
			worker.join();
		}

		busy = false;
		prepared_source.reset();

		finish_gesture(false);

		document->cancel_tool_preview();
	}
};

editor_panel::editor_panel(playback_session& playback, native_dialogs dialogs)
	: impl_(std::make_unique<impl>(playback, std::move(dialogs)))
{
}

editor_panel::~editor_panel()
{
	shutdown();
}
void editor_panel::draw(bool* open)
{
	impl_->draw(open);
}
void editor_panel::poll()
{
	impl_->poll();
}
void editor_panel::open_file(std::wstring path)
{
	impl_->request_load(std::move(path));
}
void editor_panel::shutdown()
{
	if (impl_)
		impl_->shutdown();
}
bool editor_panel::has_unsaved_changes() const
{
	return impl_->document->is_modified();
}
bool editor_panel::busy() const
{
	return impl_->busy.load(std::memory_order_acquire);
}

bool editor_panel::run_smoke(const std::wstring& output_directory, std::string& report)
{
	try
	{
		auto& state = *impl_;
		require(!state.busy, "Editor smoke started while its worker was busy.");
		const auto directory = std::filesystem::path(output_directory);
		std::filesystem::create_directories(directory);
		const auto fixture = directory / L"editor-input.mid";
		const unsigned char bytes[] = {'M', 'T', 'h', 'd', 0, 0, 0, 6, 0, 1, 0, 2, 1, 0xe0, 'M', 'T', 'r', 'k', 0, 0, 0,
			38, 0, 0xff, 0x03, 5, 'P', 'i', 'a', 'n', 'o', 0, 0xff, 0x51, 3, 7, 0xa1, 0x20, 0, 0x90, 60, 100, 0x83,
			0x60, 0x80, 60, 64, 0, 0x90, 64, 80, 0x83, 0x60, 0x80, 64, 64, 0, 0xff, 0x2f, 0, 'M', 'T', 'r', 'k', 0, 0,
			0, 17, 0, 0x91, 67, 90, 0x87, 0x40, 0x81, 67, 64, 0, 0xb1, 10, 64, 0, 0xff, 0x2f, 0};
		{
			std::ofstream out(fixture, std::ios::binary);
			out.write(reinterpret_cast<const char*>(bytes), sizeof(bytes));
		}
		state.load(fixture.wstring());
		if (state.worker.joinable())
			state.worker.join();
		state.poll();
		auto& model = *state.document;
		require(model.is_file_loaded() && model.get_note_count() == 3, "Editor owned load did not read the fixture.");
		model.set_active_track(0);
		note first_note, second_note;
		require(model.find_note_at(0, 60, first_note) && model.find_note_at(480, 64, second_note),
			"Editor split/erase fixture notes are missing.");
		model.select_note(second_note.id);
		require(model.split_notes_at({{first_note.id, 120}}) == 1 && model.get_note_count() == 4,
			"Interactive split did not create two note pieces.");
		const auto split_pieces = model.get_notes_on_key(60);
		require(split_pieces.size() == 2 && split_pieces[0].length() == 120 && split_pieces[1].length() == 360,
			"Interactive split used the wrong cut tick.");
		require(model.is_note_selected(second_note.id) && model.is_note_selected(split_pieces[0].id) &&
			!model.is_note_selected(split_pieces[1].id),
			"Interactive split did not add only the shorter half to selection.");
		model.undo();
		require(model.get_note_count() == 3 && model.find_note_at(0, 60, first_note),
			"Interactive split did not undo as one edit.");
		model.redo();
		require(model.get_note_count() == 4 && model.get_notes_on_key(60).size() == 2,
			"Interactive split redo did not restore both pieces.");
		model.undo();
		require(model.erase_notes({first_note.id, second_note.id}) == 2 && model.get_note_count() == 1,
			"Swept erase did not batch its collected notes.");
		model.undo();
		require(model.get_note_count() == 3, "Swept erase did not undo as one edit.");
		state.anchor_tick = 960;
		state.current_tick = 1080;
		state.anchor_key = state.current_key = 72;
		state.gesture_length = 120;
		state.draw_velocity = 101;
		state.draw_channel = 2;
		state.gesture = impl::gesture_kind::draw;
		state.finish_gesture(true);
		require(model.get_note_count() == 4, "Canvas draw did not insert a note.");
		state.anchor_tick = 950;
		state.current_tick = 1300;
		state.anchor_key = 71;
		state.current_key = 73;
		state.selection_mode = midi_editor::select_mode::replace;
		state.gesture = impl::gesture_kind::select;
		state.finish_gesture(true);
		require(model.selection_count() == 1, "Canvas rectangle selection did not select its note.");
		const auto before_move = model.get_selected_notes().front();
		state.delta_tick = 120;
		state.delta_key = -2;
		state.gesture = impl::gesture_kind::move;
		state.finish_gesture(true);
		auto moved = model.get_selected_notes().front();
		require(
			moved.start_tick == before_move.start_tick + 120 && moved.key == 70, "Canvas move lost timing or pitch.");
		state.gesture_notes = model.get_selected_notes();
		state.delta_tick = 120;
		state.gesture = impl::gesture_kind::resize;
		state.finish_gesture(true);
		require(model.get_selected_notes().front().length() == moved.length() + 120,
			"Canvas resize did not update the tail.");
		model.undo();
		require(model.get_selected_notes().front() == moved, "Resize undo did not restore the note.");
		model.redo();
		require(model.get_selected_notes().front().length() == moved.length() + 120, "Resize redo failed.");
		model.copy_selected_notes();
		model.paste_clipboard();
		model.duplicate_selected();
		require(model.get_note_count() == 6, "Editor clipboard or duplicate lost notes.");
		model.delete_selected_notes();
		model.undo();
		require(model.get_note_count() == 6, "Delete undo lost a note.");
		model.select_note(moved.id);
		model.change_channel_selected(3);
		model.change_velocity_selected(117);
		require(model.get_selected_notes().front().channel == 3 && model.get_selected_notes().front().velocity == 117,
			"Channel/velocity controls failed.");
		state.lane = lane_kind::pitch_bend;
		state.draw_channel = 3;
		state.control_values = {{0, 8192}, {120, 12000}};
		state.gesture = impl::gesture_kind::control;
		state.finish_gesture(true);
		require(model.get_channel_control_points(0, 3, lane_type::pitch_bend, 0, 121).size() == 2,
			"Pitch lane did not commit a batch.");
		model.undo();
		require(model.get_channel_control_points(0, 3, lane_type::pitch_bend, 0, 121).empty(),
			"Controller undo was not one transaction.");
		model.redo();
		state.lane = lane_kind::tempo;
		state.control_values = {{240, 140}, {480, 160}};
		state.gesture = impl::gesture_kind::control;
		state.finish_gesture(true);
		require(model.get_tempo_points(240, 481).size() == 2, "Tempo lane did not retain points.");
		model.set_track_name(0, "ImGui smoke piano");
		model.select_rect(0, model.get_total_ticks() + 1, 0, 127, 0);
		const auto baseline = model.get_all_notes();
		const auto baseline_selection = model.get_selected_ids();
		const std::array<std::function<void()>, 4> tools = {[&] { model.chop_tool(4, 1., 10., true, true); },
			[&] { model.flip_tool(true, false, true, true); }, [&] { model.claw_tool(1., 4, .5, true, false, true); },
			[&] { model.lfo_velocity_tool(64., 40., 2., 0., midi_editor::lfo_shape::sine, true); }};
		for (const auto& transform : tools)
		{
			transform();
			model.cancel_tool_preview();
			require(model.get_all_notes() == baseline && model.get_selected_ids() == baseline_selection,
				"Tool preview cancellation changed the document or selection.");
			transform();
			const auto accepted = model.get_all_notes();
			model.accept_tool_preview();
			model.undo();
			require(model.get_all_notes() == baseline, "Tool Accept did not undo as one edit.");
			model.redo();
			require(model.get_all_notes() == accepted, "Tool redo changed the accepted result.");
			model.undo();
		}
		model.lfo_control_tool(
			lane_type::pan, 3, 0, 960, 120, 64., 63., 1., 0., midi_editor::lfo_shape::triangle, true);
		model.cancel_tool_preview();
		require(model.get_channel_control_points(0, 3, lane_type::pan, 0, 960).empty(),
			"Control LFO cancel retained events.");
		model.lfo_control_tool(
			lane_type::channel_volume, 3, 0, 960, 120, 64., 63., 1., 0., midi_editor::lfo_shape::square, true);
		model.accept_tool_preview();
		require(!model.get_channel_control_points(0, 3, lane_type::channel_volume, 0, 960).empty(),
			"Control LFO Accept did not write events.");
		const auto saved = directory / L"editor-output.mid";
		state.start_job("Smoke save", [&](std::stop_token) { state.write_document(saved.wstring(), false); });
		if (state.worker.joinable())
			state.worker.join();
		state.poll();
		require(std::filesystem::exists(saved), "Editor worker did not write output.");
		require(!model.is_modified(), "Successful editor save did not clear the dirty flag.");
		model.select_note(model.get_all_notes().front().id);
		const auto original_velocity = model.get_selected_notes().front().velocity;
		state.gesture = impl::gesture_kind::velocity;
		state.paint_velocity(0, 0, 40., 40.);
		state.finish_gesture(false);
		require(!model.is_modified() && model.get_selected_notes().front().velocity == original_velocity,
			"Cancelling a velocity preview changed a saved document.");
		model.change_velocity_selected(original_velocity == 99 ? 100 : 99);
		const auto protected_output = directory / L"editor-protected-output.mid";
		{
			std::ofstream out(protected_output, std::ios::binary);
			out << "preserve existing output";
		}
		const auto protected_handle = CreateFileW(protected_output.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
			OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
		require(protected_handle != INVALID_HANDLE_VALUE, "Could not lock the editor output regression fixture.");
		bool blocked = false;
		try
		{
			state.write_document(protected_output.wstring(), false);
		}
		catch (const std::exception&)
		{
			blocked = true;
		}
		CloseHandle(protected_handle);
		require(blocked && model.is_modified(), "Failed output replacement incorrectly marked edits saved.");
		{
			std::ifstream in(protected_output, std::ios::binary);
			const std::string content((std::istreambuf_iterator<char>(in)), {});
			require(content == "preserve existing output", "Failed editor save replaced the existing destination.");
		}
		state.write_document((directory / L"editor-export.mid").wstring(), true);
		require(model.is_modified(), "Export incorrectly cleared the editor dirty flag.");
		state.write_document(saved.wstring(), false);
		require(!model.is_modified(), "Atomic save did not clear the editor dirty flag.");
		midi_editor reloaded;
		require(reloaded.load_file(saved.wstring()) && reloaded.get_note_count() == model.get_note_count(),
			"Saved editor MIDI did not reload.");
		require(reloaded.get_tracks().at(0).name == "ImGui smoke piano", "Saved MIDI lost track metadata.");
		require(reloaded.get_channel_control_points(0, 3, lane_type::pitch_bend, 0, 121).size() == 2,
			"Saved MIDI lost pitch bend events.");
		auto source = model.make_playback_source();
		generated_event event;
		std::uint64_t last_time = 0;
		std::size_t note_ons = 0;
		while (source->next(event))
		{
			require(event.time_us >= last_time, "Editor playback source was not time ordered.");
			last_time = event.time_us;
			if (event.k == generated_event::kind::note_on)
				++note_ons;
		}
		require(note_ons == model.get_note_count(), "Editor playback snapshot lost notes.");
		model.clear_selection();
		model.reset_view_to_content();
		state.lane = lane_kind::velocity;
		state.draw_channel = -1;
		state.status = "Editor workflow smoke passed: gestures, undo/redo, tracks, controllers, tools, save and "
					   "playback snapshot.";
		report = state.status;
		return true;
	}
	catch (const std::exception& error)
	{
		report = error.what();
		return false;
	}
}
}
