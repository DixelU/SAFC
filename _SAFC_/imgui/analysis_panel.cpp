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
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>

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
	if (input.empty())
		return {};
	const int length =
		WideCharToMultiByte(CP_UTF8, 0, input.data(), static_cast<int>(input.size()), nullptr, 0, nullptr, nullptr);
	std::string result(length, '\0');
	WideCharToMultiByte(
		CP_UTF8, 0, input.data(), static_cast<int>(input.size()), result.data(), length, nullptr, nullptr);
	return result;
}

template<class Graph>
double exact_value(const Graph& graph, std::int64_t tick)
{
	if (graph.empty())
		return 0;
	auto it = graph.upper_bound(tick);
	if (it != graph.begin())
		--it;
	return double(it->second);
}

// Builds the block min/max index the plots draw from. Memory is about one byte
// per raw point, so dense files keep exact detail at every zoom level.
template<class Graph>
analysis_plot_index build_plot_index(const Graph& graph, std::stop_token stop)
{
	constexpr auto block = analysis_plot_index::block;
	analysis_plot_index index;
	const std::size_t count = (graph.size() + block - 1) / block;
	index.block_start.reserve(count);
	index.low.assign(2 * count, std::numeric_limits<double>::infinity());
	index.high.assign(2 * count, -std::numeric_limits<double>::infinity());
	std::size_t position = 0;
	for (const auto& [tick, raw_value] : graph)
	{
		if (stop.stop_requested())
			return {};
		const double value = double(raw_value);
		if (!std::isfinite(value))
			throw std::runtime_error("MIDI contains an invalid zero tempo.");
		const std::size_t b = position++ / block;
		if (b == index.block_start.size())
			index.block_start.push_back(tick);
		index.low[count + b] = std::min(index.low[count + b], value);
		index.high[count + b] = std::max(index.high[count + b], value);
	}
	for (std::size_t i = count; i-- > 1;)
	{
		index.low[i] = std::min(index.low[2 * i], index.low[2 * i + 1]);
		index.high[i] = std::max(index.high[2 * i], index.high[2 * i + 1]);
	}
	return index;
}

// Min/max of the raw points whose ticks are in [first, last), the value in force
// just before `first`, and the value in force at `last - 1`.
struct plot_column
{
	bool any = false;
	double minimum = 0, maximum = 0, exit = 0;
};

template<class Graph>
plot_column column_stats(const Graph& graph, const analysis_plot_index& index, std::int64_t first,
	std::int64_t last, double enter)
{
	plot_column column{false, enter, enter, enter};
	if (first >= last || graph.empty())
		return column;
	const auto scan = [&](auto it, std::int64_t stop_tick)
	{
		for (; it != graph.end() && it->first < stop_tick; ++it)
		{
			const double value = double(it->second);
			column.any = true;
			column.minimum = std::min(column.minimum, value);
			column.maximum = std::max(column.maximum, value);
			column.exit = value;
		}
	};
	const auto head = index.block_of(first);
	const auto tail = index.block_of(last - 1);
	if (tail <= head + 1)
	{
		scan(graph.lower_bound(first), last);
		return column;
	}
	// Partial head block, whole blocks in between, partial tail block.
	scan(graph.lower_bound(first), index.block_start[head + 1]);
	double low, high;
	index.range(head + 1, tail, low, high);
	column.any = true;
	column.minimum = std::min(column.minimum, low);
	column.maximum = std::max(column.maximum, high);
	scan(graph.lower_bound(index.block_start[tail]), last);
	return column;
}

void validate_header(const std::wstring& path)
{
	midi_file_reader reader(path);
	if (!reader.good())
		throw std::runtime_error("Cannot open MIDI for analysis.");
	std::array<unsigned char, 14> header{};
	for (auto& c : header)
	{
		const auto byte = reader.get();
		if (!byte)
			throw std::runtime_error("The MIDI header is incomplete.");
		c = std::to_integer<unsigned char>(*byte);
	}
	if (header[0] != 'M' || header[1] != 'T' || header[2] != 'h' || header[3] != 'd')
		throw std::runtime_error("Select an uncompressed MIDI file for analysis.");
	if (header[4] || header[5] || header[6] || header[7] != 6)
		throw std::runtime_error("This analyzer requires the standard six-byte MIDI header.");
	if (header[12] & 0x80)
		throw std::runtime_error("SMPTE time division is not supported by this analyzer.");
	if (!header[12] && !header[13])
		throw std::runtime_error("MIDI ticks per quarter note must be nonzero.");
}

std::shared_ptr<analysis_result> make_result(std::shared_ptr<single_midi_info_collector> source, std::stop_token stop)
{
	if (source->tracks.empty())
		throw std::runtime_error("No complete MIDI tracks were found.");
	auto result = std::make_shared<analysis_result>();
	result->source = source;
	result->last_tick = std::max<std::int64_t>(0, source->tempo_map.rbegin()->first - 1);
	double seconds = 0;
	double seconds_per_tick = .5 / source->ppq;
	std::int64_t previous_tick = 0;
	for (const auto& [tick, tempo] : source->tempo_map)
	{
		if (stop.stop_requested())
			return {};
		if (!tempo.get_raw())
			throw std::runtime_error("MIDI contains an invalid zero tempo.");
		seconds += (tick - previous_tick) * seconds_per_tick;
		seconds_per_tick = double(tempo.get_raw()) / (1000000. * source->ppq);
		result->timing.push_back({tick, seconds, seconds_per_tick, double(tempo)});
		previous_tick = tick;
	}
	result->duration_seconds = result->ticks_to_seconds(result->last_tick);
	result->tempo_plot = build_plot_index(source->tempo_map, stop);
	result->polyphony_plot = build_plot_index(source->polyphony, stop);
	result->nps_plot = build_plot_index(source->notes_per_second, stop);
	result->peak_polyphony = std::max<std::int64_t>(0, std::llround(result->polyphony_plot.peak()));
	result->peak_nps = std::max<std::int64_t>(0, std::llround(result->nps_plot.peak()));
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
		if (stop.stop_requested())
			throw std::runtime_error("Export cancelled.");
		const auto tick =
			std::min(poly != poly_end ? poly->first : INT64_MAX, tempo != tempo_end ? tempo->first : INT64_MAX);
		if (poly != poly_end && poly->first == tick)
		{
			current_poly = poly->second;
			++poly;
		}
		if (tempo != tempo_end && tempo->first == tick)
		{
			bpm = double(tempo->second);
			++tempo;
		}
		// The collector's synthetic -1 row describes the empty initial state.
		// Preserve its legacy export time of zero rather than extrapolating
		// backwards to a negative timestamp.
		function(tick, current_poly, result.ticks_to_seconds(std::max<std::int64_t>(0, tick)), bpm);
	}
}

void write_analysis_export(const analysis_result& result, const std::wstring& path,
	const std::filesystem::path& temporary, const std::string& delim, analysis_panel::export_kind kind,
	std::stop_token stop)
{
	const auto destination = std::filesystem::absolute(path).lexically_normal();
	const auto source = std::filesystem::absolute(result.source->filename).lexically_normal();
	const auto normalized_output =
		std::filesystem::weakly_canonical(destination.parent_path()) / destination.filename();
	const auto normalized_source = std::filesystem::weakly_canonical(source.parent_path()) / source.filename();
	if (_wcsicmp(normalized_output.c_str(), normalized_source.c_str()) == 0)
		throw std::runtime_error("Analysis data must not overwrite the source MIDI.");
	std::ofstream output(temporary, std::ios::out | std::ios::binary | std::ios::trunc);
	if (!output)
		throw std::runtime_error("Cannot create the export file.");
	output << std::setprecision(17);
	if (kind == analysis_panel::export_kind::tempo_csv)
	{
		output << "tick" << delim << "tempo\n";
		for (const auto& [tick, value] : result.source->tempo_map)
		{
			if (stop.stop_requested())
				throw std::runtime_error("Export cancelled.");
			output << tick << delim << double(value) << '\n';
		}
	}
	else if (kind == analysis_panel::export_kind::nps_csv)
	{
		output << "tick" << delim << "nps\n";
		for (const auto& [tick, value] : result.source->notes_per_second)
		{
			if (stop.stop_requested())
				throw std::runtime_error("Export cancelled.");
			output << tick << delim << value << '\n';
		}
	}
	else
	{
		if (kind != analysis_panel::export_kind::combined_binary)
			output << "Tick" << delim << "Polyphony" << delim << "Time(seconds)" << delim << "Tempo\n";
		visit_rows(result, stop, [&](std::int64_t tick, std::int64_t poly, double seconds, double tempo)
		{
			if (kind == analysis_panel::export_kind::combined_binary)
			{
				output.write(reinterpret_cast<const char*>(&tick), sizeof(tick));
				output.write(reinterpret_cast<const char*>(&poly), sizeof(poly));
				output.write(reinterpret_cast<const char*>(&seconds), sizeof(seconds));
				output.write(reinterpret_cast<const char*>(&tempo), sizeof(tempo));
			}
			else
				output << tick << delim << poly << delim << seconds << delim << tempo << '\n';
		});
	}
	output.flush();
	if (!output)
		throw std::runtime_error("Writing the export failed; check free space and permissions.");
	output.close();
	if (stop.stop_requested())
		throw std::runtime_error("Export cancelled.");
	if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
		throw std::runtime_error(
			"Could not replace the destination file (Windows error " + std::to_string(GetLastError()) + ").");
}

}

std::size_t analysis_plot_index::block_of(std::int64_t tick) const
{
	const auto it = std::upper_bound(block_start.begin(), block_start.end(), tick);
	return it == block_start.begin() ? 0 : std::size_t(it - block_start.begin()) - 1;
}

void analysis_plot_index::range(std::size_t first, std::size_t last, double& minimum, double& maximum) const
{
	minimum = std::numeric_limits<double>::infinity();
	maximum = -std::numeric_limits<double>::infinity();
	const std::size_t size = blocks();
	for (first += size, last += size; first < last; first >>= 1, last >>= 1)
	{
		if (first & 1)
		{
			minimum = std::min(minimum, low[first]);
			maximum = std::max(maximum, high[first++]);
		}
		if (last & 1)
		{
			minimum = std::min(minimum, low[--last]);
			maximum = std::max(maximum, high[last]);
		}
	}
}

double analysis_result::ticks_to_seconds(std::int64_t tick) const
{
	if (timing.empty())
		return 0;
	auto it = std::upper_bound(
		timing.begin(), timing.end(), tick, [](auto t, const analysis_tempo& p) { return t < p.tick; });
	if (it != timing.begin())
		--it;
	return it->seconds + (tick - it->tick) * it->seconds_per_tick;
}

std::int64_t analysis_result::seconds_to_ticks(double seconds) const
{
	if (timing.empty() || !std::isfinite(seconds))
		return 0;
	auto it = std::upper_bound(
		timing.begin(), timing.end(), seconds, [](auto time, const analysis_tempo& p) { return time < p.seconds; });
	if (it != timing.begin())
		--it;
	long double tick = it->tick + (static_cast<long double>(seconds) - it->seconds) / it->seconds_per_tick;
	// A time produced from an exact tick must survive its return trip. Correct
	// floating-point noise only at integer boundaries; other times round down.
	const long double nearest = std::round(tick);
	const long double tolerance = (std::abs(tick) + 1) * std::numeric_limits<double>::epsilon() * 8;
	if (std::abs(tick - nearest) <= tolerance)
		tick = nearest;
	if (tick >= static_cast<long double>(INT64_MAX))
		return INT64_MAX;
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

	void start_export(std::shared_ptr<const analysis_result> result, export_kind kind)
	{
		if (exporting && !exporting->done.load(std::memory_order_acquire))
			return;
		message.clear();
		if (!dialogs.save_data)
		{
			message = "No save dialog is configured.";
			return;
		}
		const wchar_t* suffix = L".a.csv";
		switch (kind)
		{
		case export_kind::combined_csv:
			break;
		case export_kind::combined_binary:
			suffix = L".atraw";
			break;
		case export_kind::tempo_csv:
			suffix = L".tg.csv";
			break;
		case export_kind::nps_csv:
			suffix = L".nps.csv";
			break;
		}
		try
		{
			auto path = dialogs.save_data(filename + suffix);
			if (path.empty())
				return;
			const std::string separator = delimiter[0] ? delimiter.data() : ";";
			run_export(std::move(result), path, separator, kind);
		}
		catch (const std::exception& error)
		{
			message = error.what();
		}
	}

	void run_export(std::shared_ptr<const analysis_result> result, const std::wstring& path, const std::string& delim,
		export_kind kind)
	{
		exporting = std::make_shared<export_job>();
		export_worker =
			std::jthread([state = exporting, result = std::move(result), path, delim, kind](std::stop_token stop)
		{
			auto temporary = std::filesystem::path(path);
			temporary += L".safc-analysis-" +
				std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count()) + L".tmp";
			std::string status;
			try
			{
				write_analysis_export(*result, path, temporary, delim, kind, stop);
				status = "Export saved: " + utf8(path);
			}
			catch (const std::exception& error)
			{
				status = error.what();
			}
			std::error_code ignored;
			std::filesystem::remove(temporary, ignored);
			{
				std::lock_guard lock(state->mutex);
				state->status = std::move(status);
			}
			state->done.store(true, std::memory_order_release);
		});
	}

	template<class Graph>
	void plot(const char* id, const char* label, const analysis_plot_index& index, const Graph& exact,
		ImU32 color, const analysis_result& data)
	{
		ImGui::TextUnformatted(label);
		const ImVec2 origin = ImGui::GetCursorScreenPos();
		const ImVec2 size(std::max(scaled(180), ImGui::GetContentRegionAvail().x), scaled(135));
		ImGui::InvisibleButton(id, size, ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);
		// The wheel zooms this plot; keep it from also scrolling the window.
		ImGui::SetItemKeyOwner(ImGuiKey_MouseWheelY);
		auto* draw = ImGui::GetWindowDrawList();
		const ImVec2 corner(origin.x + size.x, origin.y + size.y);
		draw->AddRectFilled(origin, corner, IM_COL32(7, 19, 30, 255));
		const auto to_axis = [&](std::int64_t tick)
		{
			return seconds_axis ? data.ticks_to_seconds(tick) : double(tick);
		};
		const auto to_tick = [&](double x)
		{
			if (seconds_axis)
				return data.seconds_to_ticks(x);
			if (x >= static_cast<double>(INT64_MAX))
				return INT64_MAX;
			return static_cast<std::int64_t>(std::max(0., x));
		};
		const double span = std::max(.000001, view_end - view_start);
		const int columns = std::max(1, int(size.x));
		const auto first_tick = to_tick(view_start);
		const auto last_tick = to_tick(view_end);
		// Few raw points in view: draw their exact steps. Otherwise reduce the
		// raw data to one min/max column per screen pixel, so the drawn shape
		// always matches the exact values the tooltip reports.
		const auto first_block = index.block_of(first_tick);
		const std::size_t visible_points =
			(std::max(first_block, index.block_of(last_tick)) - first_block + 1) * analysis_plot_index::block;
		const bool exact_steps = visible_points <= std::size_t(columns) * 2;
		std::vector<std::pair<double, double>> steps; // axis x, value
		std::vector<plot_column> pixel_columns;
		double enter = 0;
		double peak = -std::numeric_limits<double>::infinity();
		if (exact_steps)
		{
			enter = exact_value(exact, first_tick);
			peak = enter;
			for (auto it = exact.upper_bound(first_tick); it != exact.end(); ++it)
			{
				const double x = to_axis(it->first);
				if (x > view_end)
					break;
				steps.emplace_back(x, double(it->second));
				peak = std::max(peak, steps.back().second);
			}
		}
		else
		{
			enter = exact_value(exact, first_tick - 1);
			peak = enter;
			pixel_columns.resize(columns);
			std::int64_t column_start = first_tick;
			double carried = enter;
			for (int c = 0; c < columns; ++c)
			{
				const std::int64_t column_end = c + 1 == columns
					? (last_tick == INT64_MAX ? INT64_MAX : last_tick + 1)
					: std::max(column_start, to_tick(view_start + span * (c + 1) / columns));
				pixel_columns[c] = column_stats(exact, index, column_start, column_end, carried);
				carried = pixel_columns[c].exit;
				peak = std::max(peak, pixel_columns[c].maximum);
				column_start = column_end;
			}
		}
		double maximum = auto_scale ? std::max(1., peak) * 1.05 : std::max(1., manual_max);
		const auto value_y = [&](double y)
		{
			return corner.y - float(std::clamp(y / maximum, 0., 1.)) * (size.y - 9);
		};
		const auto position = [&](double x, double y)
		{
			return ImVec2(origin.x + float((x - view_start) / span) * size.x, value_y(y));
		};
		for (int i = 1; i < 4; ++i)
			draw->AddLine(ImVec2(origin.x, origin.y + size.y * i / 4), ImVec2(corner.x, origin.y + size.y * i / 4),
				IM_COL32(34, 56, 74, 190));
		draw->PushClipRect(origin, corner, true);
		ImVec2 previous(origin.x, value_y(enter));
		if (exact_steps)
		{
			for (const auto& [x, value] : steps)
			{
				const auto current = position(x, value);
				draw->AddLine(previous, ImVec2(current.x, previous.y), color, 1.3f);
				draw->AddLine(ImVec2(current.x, previous.y), current, color, 1.3f);
				previous = current;
			}
		}
		else
		{
			double carried = enter;
			for (int c = 0; c < columns; ++c)
			{
				const auto& column = pixel_columns[c];
				if (!column.any)
					continue;
				const float x = origin.x + float(c) * size.x / columns;
				draw->AddLine(previous, ImVec2(x, previous.y), color, 1.3f);
				draw->AddLine(ImVec2(x, value_y(std::min(carried, column.minimum))),
					ImVec2(x, value_y(std::max(carried, column.maximum))), color, 1.3f);
				carried = column.exit;
				previous = ImVec2(x, value_y(carried));
			}
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
			if constexpr (std::is_integral_v<typename Graph::mapped_type>)
				ImGui::SetTooltip("Tick %lld | %.6f s | %s %lld", static_cast<long long>(tick), data.ticks_to_seconds(tick),
					label, static_cast<long long>(std::llround(exact_value(exact, tick))));
			else
				ImGui::SetTooltip("Tick %lld | %.6f s | %s %.6g", static_cast<long long>(tick), data.ticks_to_seconds(tick),
					label, exact_value(exact, tick));
			if (ImGui::IsItemClicked(ImGuiMouseButton_Left) ||
				(ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left)))
				selected_tick = input_tick = tick;
			if (ImGui::IsItemHovered() && ImGui::GetIO().MouseWheel != 0)
			{
				const double limit = view_limit(data);
				const double new_span =
					std::clamp(span * std::pow(.8, ImGui::GetIO().MouseWheel), seconds_axis ? .000001 : 1., limit);
				const double anchor = view_start + span * ratio;
				view_start = std::clamp(anchor - new_span * ratio, 0., limit - new_span);
				view_end = view_start + new_span;
			}
			if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Middle))
			{
				const double delta = -ImGui::GetIO().MouseDelta.x / size.x * span;
				const double next = std::clamp(view_start + delta, 0., std::max(0., view_limit(data) - span));
				view_end += next - view_start;
				view_start = next;
			}
		}
		draw->PopClipRect();
		draw->AddRect(origin, corner, IM_COL32(63, 113, 151, 255));
		char text[48];
		if constexpr (std::is_integral_v<typename Graph::mapped_type>)
			std::snprintf(text, sizeof(text), "max %lld", static_cast<long long>(std::llround(maximum)));
		else
			std::snprintf(text, sizeof(text), "max %.6g", maximum);
		draw->AddText(ImVec2(origin.x + 5, origin.y + 3), color, text);
	}

	// End of the horizontal data range in the current axis unit.
	double view_limit(const analysis_result& data) const
	{
		return std::max(1., seconds_axis ? data.duration_seconds : double(data.last_tick));
	}

	void fit_view(const analysis_result& data)
	{
		view_start = 0;
		view_end = view_limit(data);
	}

	void draw_summary(const analysis_result& data)
	{
		ImGui::Text("Tracks: %zu | PPQN: %u | Duration: %.3f s | Last tick: %lld", data.source->tracks.size(),
			unsigned(data.source->ppq), data.duration_seconds, static_cast<long long>(data.last_tick));
		ImGui::Text("Peak polyphony: %lld | Peak notes/sec: %lld | Tempo points: %zu",
			static_cast<long long>(data.peak_polyphony), static_cast<long long>(data.peak_nps),
			data.source->tempo_map.size());
		ImGui::BeginDisabled(!commit || data.source->internal_time_map.empty());
		if (ImGui::Button("Use time map for this MIDI"))
		{
			commit(data.source->internal_time_map);
			message = "Time map applied to the source MIDI.";
		}
		ImGui::EndDisabled();
	}

	void draw_graphs(const analysis_result& data)
	{
		ImGui::SameLine();
		ImGui::Checkbox("Tempo", &show_tempo);
		ImGui::SameLine();
		ImGui::Checkbox("Polyphony", &show_poly);
		ImGui::SameLine();
		ImGui::Checkbox("Notes/sec", &show_nps);
		if (ImGui::Checkbox("Seconds on horizontal axis", &seconds_axis))
		{
			fit_view(data);
		}
		ImGui::SameLine();
		ImGui::Checkbox("Auto vertical scale", &auto_scale);
		if (!auto_scale)
		{
			ImGui::SameLine();
			ImGui::SetNextItemWidth(scaled(100));
			ImGui::InputDouble("Maximum", &manual_max, 0, 0, "%.1f");
		}
		ImGui::SetNextItemWidth(scaled(145));
		ImGui::InputDouble("From", &view_start, 0, 0, "%.3f");
		ImGui::SameLine();
		ImGui::SetNextItemWidth(scaled(145));
		ImGui::InputDouble("To", &view_end, 0, 0, "%.3f");
		ImGui::SameLine();
		if (ImGui::Button("Fit all"))
		{
			fit_view(data);
		}
		const double limit = view_limit(data);
		view_start = std::isfinite(view_start) ? std::clamp(view_start, 0., limit - .000001) : 0;
		view_end = std::isfinite(view_end) ? std::min(limit, std::max(view_start + .000001, view_end)) : limit;
		ImGui::TextDisabled("Wheel: zoom at cursor | middle-drag: pan | click: select tick (exact values in tooltip)");
		if (show_tempo)
			plot("##tempo-plot", "Tempo (BPM)", data.tempo_plot, data.source->tempo_map, IM_COL32(230, 123, 143, 255),
				data);
		if (show_poly)
			plot("##polyphony-plot", "Polyphony", data.polyphony_plot, data.source->polyphony,
				IM_COL32(98, 209, 191, 255), data);
		if (show_nps)
			plot("##nps-plot", "Notes per second", data.nps_plot, data.source->notes_per_second,
				IM_COL32(232, 198, 99, 255), data);
		ImGui::Text("Selected tick: %lld | Time: %.6f s", static_cast<long long>(selected_tick),
			data.ticks_to_seconds(selected_tick));
	}

	void draw_time_conversion(const analysis_result& data)
	{
		ImGui::SeparatorText("Tick / time conversion");
		ImGui::SetNextItemWidth(scaled(170));
		ImGui::InputScalar("Ticks", ImGuiDataType_S64, &input_tick);
		input_tick = std::max<std::int64_t>(0, input_tick);
		ImGui::SameLine();
		if (ImGui::Button("Ticks -> time"))
		{
			input_seconds = data.ticks_to_seconds(input_tick);
			selected_tick = input_tick;
			if (std::abs(input_seconds) < double(INT64_MAX) / 1000 - 1)
			{
				const auto ms = static_cast<std::int64_t>(std::round(input_seconds * 1000));
				conversion = std::to_string(ms / 60000) + " min " + std::to_string(ms / 1000 % 60) + " sec " +
					std::to_string(ms % 1000) + " ms";
			}
			else
				conversion = std::to_string(input_seconds) + " seconds";
		}
		ImGui::SetNextItemWidth(scaled(170));
		ImGui::InputDouble("Seconds", &input_seconds, 0, 0, "%.6f");
		ImGui::SameLine();
		if (ImGui::Button("Time -> ticks"))
		{
			selected_tick = input_tick = data.seconds_to_ticks(input_seconds);
			conversion = "Tick " + std::to_string(input_tick);
		}
		if (!conversion.empty())
			ImGui::TextUnformatted(conversion.c_str());
	}

	void draw_export_controls(const std::shared_ptr<const analysis_result>& data)
	{
		ImGui::SeparatorText("Export");
		const bool export_running = exporting && !exporting->done.load(std::memory_order_acquire);
		ImGui::BeginDisabled(export_running);
		ImGui::SetNextItemWidth(scaled(75));
		ImGui::InputText("Delimiter", delimiter.data(), delimiter.size());
		ImGui::SameLine();
		ImGui::SetNextItemWidth(scaled(115));
		ImGui::Combo("Format", &export_format, "CSV\0ATRAW (binary)\0");
		if (ImGui::Button("Export tempo CSV"))
			start_export(data, export_kind::tempo_csv);
		ImGui::SameLine();
		if (ImGui::Button("Export combined data"))
			start_export(data, export_format == 0 ? export_kind::combined_csv : export_kind::combined_binary);
		ImGui::SameLine();
		if (ImGui::Button("Export notes/sec CSV"))
			start_export(data, export_kind::nps_csv);
		ImGui::EndDisabled();
		if (exporting)
		{
			{
				std::lock_guard lock(exporting->mutex);
				ImGui::TextWrapped("%s", exporting->status.c_str());
			}
			if (export_running && ImGui::Button("Cancel export"))
				export_worker.request_stop();
		}
	}
};

analysis_panel::analysis_panel(native_dialogs dialogs) : impl_(std::make_unique<impl>())
{
	impl_->dialogs = std::move(dialogs);
}
analysis_panel::~analysis_panel()
{
	shutdown();
}

void analysis_panel::open_file(std::wstring path, std::uint16_t ppq, bool legacy_meta,
	std::function<void(single_midi_info_collector::time_graph)> commit)
{
	shutdown();
	impl_->filename = std::move(path);
	impl_->commit = std::move(commit);
	impl_->result_initialized = false;
	impl_->exporting.reset();
	impl_->message.clear();
	impl_->conversion.clear();
	impl_->input_seconds = 0;
	impl_->input_tick = impl_->selected_tick = 0;
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
			if (stop.stop_requested())
				throw std::runtime_error("Analysis cancelled.");
			job->collector->fetch_data();
			if (stop.stop_requested())
				throw std::runtime_error("Analysis cancelled.");
			const auto [error, progress] = job->collector->status_text();
			if (error.find_first_not_of(" \t\r\n") != std::string::npos)
				throw std::runtime_error(error);
			{
				std::lock_guard lock(job->mutex);
				job->status = "Preparing graphs...";
			}
			result = make_result(job->collector, stop);
			status = result ? "Analysis complete." : "Analysis cancelled.";
		}
		catch (const std::exception& error)
		{
			status = error.what();
		}
		catch (...)
		{
			status = "Analysis failed with an unknown error.";
		}
		{
			std::lock_guard lock(job->mutex);
			job->result = std::move(result);
			job->status = std::move(status);
		}
		job->done.store(true, std::memory_order_release);
	});
}

void analysis_panel::cancel()
{
	impl_->worker.request_stop();
	if (impl_->job)
		impl_->job->collector->request_stop();
}

void analysis_panel::shutdown()
{
	cancel();
	impl_->export_worker.request_stop();
	if (impl_->worker.joinable())
		impl_->worker.join();
	if (impl_->export_worker.joinable())
		impl_->export_worker.join();
}

bool analysis_panel::busy() const
{
	return impl_->job && !impl_->job->done.load(std::memory_order_acquire);
}
std::shared_ptr<const analysis_result> analysis_panel::result() const
{
	if (!impl_->job || !impl_->job->done.load(std::memory_order_acquire))
		return {};
	std::lock_guard lock(impl_->job->mutex);
	return impl_->job->result;
}
std::string analysis_panel::status() const
{
	if (!impl_->job)
		return "Select a MIDI and collect its information.";
	if (busy())
	{
		const auto [error, progress] = impl_->job->collector->status_text();
		if (error.find_first_not_of(" \t\r\n") != std::string::npos)
			return error;
		if (!impl_->job->collector->finished.load(std::memory_order_acquire) &&
			progress.find_first_not_of(" \t\r\n") != std::string::npos)
			return progress;
	}
	std::lock_guard lock(impl_->job->mutex);
	return impl_->job->status;
}

bool analysis_panel::export_to(std::wstring path, export_kind kind, std::string delimiter)
{
	const auto data = result();
	if (!data || path.empty() || export_busy())
		return false;
	if (delimiter.empty())
		delimiter = ";";
	impl_->run_export(data, path, delimiter, kind);
	return true;
}

bool analysis_panel::export_busy() const
{
	return impl_->exporting && !impl_->exporting->done.load(std::memory_order_acquire);
}

std::string analysis_panel::export_status() const
{
	if (!impl_->exporting)
		return {};
	std::lock_guard lock(impl_->exporting->mutex);
	return impl_->exporting->status;
}

void analysis_panel::cancel_export()
{
	impl_->export_worker.request_stop();
}

void analysis_panel::draw(bool* open)
{
	const auto display = ImGui::GetIO().DisplaySize;
	ImGui::SetNextWindowPos({scaled(60), scaled(86)}, ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize({std::min(scaled(910), display.x - scaled(84)),
								 std::max(scaled(420), std::min(scaled(790), display.y - scaled(116)))},
		ImGuiCond_FirstUseEver);
	if (!begin_folded_window("MIDI analysis", open))
	{
		end_folded_window();
		return;
	}
	// Opening another MIDI joins the export worker, which would cancel it.
	ImGui::BeginDisabled(busy() || export_busy());
	if (ImGui::Button("Open MIDI...") && impl_->dialogs.open_midi)
	{
		impl_->message.clear();
		try
		{
			if (auto path = impl_->dialogs.open_midi(); !path.empty())
				open_file(std::move(path));
		}
		catch (const std::exception& error)
		{
			impl_->message = error.what();
		}
	}
	ImGui::EndDisabled();
	if (!impl_->message.empty())
		ImGui::TextWrapped("%s", impl_->message.c_str());
	ImGui::TextWrapped("%s", utf8(impl_->filename).c_str());
	ImGui::TextWrapped("%s", status().c_str());
	if (busy())
	{
		ImGui::ProgressBar(-float(ImGui::GetTime()), ImVec2(-1, 0), "Collecting tracks, tempo and note counts");
		if (ImGui::Button("Cancel analysis"))
			cancel();
		end_folded_window();
		return;
	}
	const auto data = result();
	if (!data)
	{
		end_folded_window();
		return;
	}
	if (!impl_->result_initialized)
	{
		impl_->fit_view(*data);
		impl_->result_initialized = true;
	}

	impl_->draw_summary(*data);
	impl_->draw_graphs(*data);
	impl_->draw_time_conversion(*data);
	impl_->draw_export_controls(data);
	end_folded_window();
}
}
