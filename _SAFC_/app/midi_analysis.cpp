#include "app_state.h"
#include "file_properties.h"

namespace props_and_sets
{
void initialize_collecting()
{
	if (current_id < 0 && current_id >= g_data.files.size())
	{
		throw_alert_error("How you have managed to select the midi beyond the list? O.o\n" + std::to_string(current_id));
		return;
	}

	auto tempo_graph = (Graphing<single_midi_info_collector::tempo_graph>*)(*(*global_window_handler)["SMIC"])["TEMPO_GRAPH"];
	auto nps_graph = (Graphing<single_midi_info_collector::notes_per_second_graph>*)(*(*global_window_handler)["SMIC"])["NPS_GRAPH"];
	if (smic_ptr)
	{
		{ std::lock_guard<std::recursive_mutex> locker(tempo_graph->lock); }
		tempo_graph->graph = nullptr;
		nps_graph->graph = nullptr;
	}

	smic_ptr = new single_midi_info_collector(g_data.files[current_id].filename, g_data.files[current_id].old_ppqn, g_data.files[current_id].allow_legacy_rsb_meta_interaction);

	auto collector = smic_ptr;
	worker_singleton<struct info_collection>::instance().push([collector](std::stop_token stop_token)
	{
		if (gui_stop_requested(stop_token))
			return;
		std::stop_callback cancellation(stop_token,
			[collector]() { collector->request_stop(); });
		global_window_handler->main_window_id = "SMIC";
		global_window_handler->disable_all_windows();

		worker_singleton<struct info_collection_watcher>::instance().push(
			[collector](std::stop_token watcher_stop)
		{
			if (gui_stop_requested(watcher_stop))
				return;
			auto export_all = (*(*global_window_handler)["SMIC"])["ALL_EXP"];
			auto export_tempo = (*(*global_window_handler)["SMIC"])["TG_EXP"];
			auto integrate_ticks = (*(*global_window_handler)["SMIC"])["INTEGRATE_TICKS"];
			auto integrate_time = (*(*global_window_handler)["SMIC"])["INTEGRATE_TIME"];
			auto error_line = (text_box*)(*(*global_window_handler)["SMIC"])["FEL"];
			auto info_line = (text_box*)(*(*global_window_handler)["SMIC"])["FLL"];
			auto delim = (input_field*)(*(*global_window_handler)["SMIC"])["DELIM"];
			auto minutes = (input_field*)(*(*global_window_handler)["SMIC"])["INT_MIN"];
			auto seconds = (input_field*)(*(*global_window_handler)["SMIC"])["INT_SEC"];
			auto tempo_graph_switch = (input_field*)(*(*global_window_handler)["SMIC"])["TG_SWITCH"];
			auto poly_graph_switch = (input_field*)(*(*global_window_handler)["SMIC"])["PG_SWITCH"];
			auto millisecs = (input_field*)(*(*global_window_handler)["SMIC"])["INT_MSC"];
			auto ticks = (input_field*)(*(*global_window_handler)["SMIC"])["INT_TIC"];
			auto ui_tempo_graph = (Graphing<single_midi_info_collector::tempo_graph>*)(*(*global_window_handler)["SMIC"])["TEMPO_GRAPH"];
			auto ui_poly_graph = (Graphing<single_midi_info_collector::polyphony_graph>*)(*(*global_window_handler)["SMIC"])["POLY_GRAPH"];
			auto ui_nps_graph = (Graphing<single_midi_info_collector::notes_per_second_graph>*)(*(*global_window_handler)["SMIC"])["NPS_GRAPH"];

			ui_poly_graph->enabled = false;
			ui_poly_graph->reset();
			ui_tempo_graph->enabled = false;
			ui_tempo_graph->reset();
			ui_nps_graph->enabled = false;
			ui_nps_graph->reset();

			delim->safe_string_replace(csv_delim);
			delim->current_string = csv_delim;
			poly_graph_switch->safe_string_replace("Enable graph B");
			tempo_graph_switch->safe_string_replace("Enable graph A");
			info_line->safe_string_replace("Please wait...");
			minutes->safe_string_replace("0");
			seconds->safe_string_replace("0");
			millisecs->safe_string_replace("0");
			ticks->safe_string_replace("0");

			export_all->disable();
			export_tempo->disable();
			integrate_ticks->disable();
			integrate_time->disable();

			while (!collector->finished.load(std::memory_order_acquire) &&
				!gui_stop_requested(watcher_stop))
			{
				auto [error_text, log_text] = collector->status_text();
				if (error_line->text != error_text)
					error_line->safe_string_replace(error_text);
				if (info_line->text != log_text)
					info_line->safe_string_replace(log_text);
				Sleep(10);
			}

			if (gui_stop_requested(watcher_stop))
				return;
			info_line->safe_string_replace("Finished");
			export_all->enable();
			export_tempo->enable();
			integrate_ticks->enable();
			integrate_time->enable();
		});

		collector->fetch_data();
		if (gui_stop_requested(stop_token))
			return;
		auto tempo_graph = (Graphing<single_midi_info_collector::tempo_graph>*)(*(*global_window_handler)["SMIC"])["TEMPO_GRAPH"];
		tempo_graph->graph = &(collector->tempo_map);
		auto poly_graph = (Graphing<single_midi_info_collector::polyphony_graph>*)(*(*global_window_handler)["SMIC"])["POLY_GRAPH"];
		poly_graph->graph = &(collector->polyphony);
		auto nps_graph = (Graphing<single_midi_info_collector::notes_per_second_graph>*)(*(*global_window_handler)["SMIC"])["NPS_GRAPH"];
		nps_graph->graph = &(collector->notes_per_second);
		nps_graph->enabled = true;

		auto midi_info = (text_box*)(*(*global_window_handler)["SMIC"])["TOTAL_INFO"];
		midi_info->safe_string_replace(
			"Total (real) tracks: " + std::to_string(collector->tracks.size()) + "; ... "
		);

		global_window_handler->main_window_id = "MAIN";
		global_window_handler->enable_window("MAIN");
		global_window_handler->enable_window("SMIC");
	});

	global_window_handler->enable_window("SMIC");
}

namespace SMIC
{
void load_time_map()
{
	if (current_id < 0 && current_id >= g_data.files.size())
	{
		throw_alert_error("How you have managed to select the midi beyond the lists end? O.o\n" + std::to_string(current_id));
		return;
	}

	if (!smic_ptr || !smic_ptr->finished)
	{
		throw_alert_warning("Time map is not ready yet...");
		return;
	}

	if (smic_ptr->internal_time_map.empty())
	{
		throw_alert_error("Time map of the selected midi is empty...");
		return;
	}

	g_data[current_id].time_map = smic_ptr->internal_time_map;
	open_file_properties(current_id);
	global_window_handler->disable_window("SMIC");
}

void enable_pg()
{
	auto poly_graph = (Graphing<single_midi_info_collector::polyphony_graph>*)(*(*global_window_handler)["SMIC"])["POLY_GRAPH"];
	auto poly_graph_switch = (button*)(*(*global_window_handler)["SMIC"])["PG_SWITCH"];

	if (poly_graph->enabled)
		poly_graph_switch->safe_string_replace("Enable graph B");
	else
		poly_graph_switch->safe_string_replace("Disable graph B");

	poly_graph->enabled ^= true;
}

void enable_tg()
{
	auto tempo_graph = (Graphing<single_midi_info_collector::tempo_graph>*)(*(*global_window_handler)["SMIC"])["TEMPO_GRAPH"];
	auto tempo_graph_switch = (button*)(*(*global_window_handler)["SMIC"])["TG_SWITCH"];

	if (tempo_graph->enabled)
		tempo_graph_switch->safe_string_replace("Enable graph A");
	else
		tempo_graph_switch->safe_string_replace("Disable graph A");

	tempo_graph->enabled ^= true;
}

void switch_personal_use()
{
	auto human_readible_switch = (button*)(*(*global_window_handler)["SMIC"])["HUMANREADIBLE"];
	human_readible ^= true;

	if (human_readible)
		human_readible_switch->safe_string_replace(".csv");
	else
		human_readible_switch->safe_string_replace(".atraw");
}

void export_tg()
{
	worker_singleton<struct info_collection>::instance().push([]()
	{
		global_window_handler->main_window_id = "SMIC";
		global_window_handler->disable_all_windows();

		auto info_text = (text_box*)(*(*global_window_handler)["SMIC"])["FLL"];
		info_text->safe_string_replace("graph A is exporting...");

		std::ofstream out(smic_ptr->filename + L".tg.csv");

		out << "tick" << csv_delim << "tempo" << '\n';
		for (auto& [tick, tempo] : smic_ptr->tempo_map)
			out << tick << csv_delim << tempo << '\n';

		out.close();

		global_window_handler->main_window_id = "MAIN";
		global_window_handler->enable_window("MAIN");
		global_window_handler->enable_window("SMIC");

		info_text->safe_string_replace("graph A was successfully exported...");
	});
}

void export_all()
{
	worker_singleton<struct info_collection>::instance().push([]()
	{
		global_window_handler->main_window_id = "SMIC";
		global_window_handler->disable_all_windows();
		auto info_line = (text_box*)(*(*global_window_handler)["SMIC"])["FLL"];
		info_line->safe_string_replace("Collecting data for exporting...");

		using line_data = struct
		{
			std::int64_t polyphony;
			double seconds;
			double tempo;
		};

		std::int64_t polyphony = 0;
		std::uint16_t ppq = smic_ptr->ppq;
		std::int64_t last_tick = 0;
		std::string header = "";

		double tempo = 0;
		double seconds = 0;
		double seconds_per_tick = 0;

		header = (
			"Tick" + csv_delim
			+ "Polyphony" + csv_delim
			+ "Time(seconds)" + csv_delim
			+ "Tempo"
			+ "\n");

		btree::btree_map<std::int64_t, line_data> info;
		for (auto& cur_pair : smic_ptr->polyphony)
			info[cur_pair.first] = line_data({
				cur_pair.second, 0., 0.
				});

		auto it_ptree = smic_ptr->polyphony.begin();
		for (auto cur_pair : smic_ptr->tempo_map)
		{
			while (it_ptree != smic_ptr->polyphony.end() && it_ptree->first < cur_pair.first)
			{
				seconds += seconds_per_tick * (it_ptree->first - last_tick);
				last_tick = it_ptree->first;
				auto& t = info[it_ptree->first];
				polyphony = t.polyphony;
				t.seconds = seconds;
				t.tempo = tempo;
				++it_ptree;
			}

			if (it_ptree->first == cur_pair.first)
				info[it_ptree->first].tempo = cur_pair.second;
			else
			{
				seconds += seconds_per_tick * (cur_pair.first - last_tick);
				info[cur_pair.first] = line_data({
					polyphony, seconds, cur_pair.second
					});
			}

			last_tick = cur_pair.first;
			tempo = cur_pair.second;
			seconds_per_tick = (60 / (tempo * ppq));
		}

		while (it_ptree != smic_ptr->polyphony.end())
		{
			seconds += seconds_per_tick * (it_ptree->first - last_tick);
			last_tick = it_ptree->first;

			auto& line_data = info[it_ptree->first];
			polyphony = line_data.polyphony;
			line_data.seconds = seconds;
			line_data.tempo = tempo;

			++it_ptree;
		}

		std::ofstream out(smic_ptr->filename + ((human_readible) ? L".a.csv" : L".atraw"),
			((human_readible) ? (std::ios::out) : (std::ios::out | std::ios::binary))
		);

		if (human_readible)
		{
			out << header;
			for (auto& cur_pair : info)
			{
				out << cur_pair.first << csv_delim
					<< cur_pair.second.polyphony << csv_delim
					<< cur_pair.second.seconds << csv_delim
					<< cur_pair.second.tempo << std::endl;
			}
		}
		else
		{
			for (auto& cur_pair : info)
			{
				out.write((const char*)(&cur_pair.first), 8);
				out.write((const char*)(&cur_pair.second.polyphony), 8);
				out.write((const char*)(&cur_pair.second.seconds), 8);
				out.write((const char*)(&cur_pair.second.tempo), 8);
			}
		}

		out.close();

		global_window_handler->main_window_id = "MAIN";
		global_window_handler->enable_window("MAIN");
		global_window_handler->enable_window("SMIC");

		info_line->safe_string_replace("graph B was successfully exported...");
	});
}

void differentiate_ticks()
{
	worker_singleton<struct info_collection>::instance().push([]()
	{
		global_window_handler->main_window_id = "SMIC";
		global_window_handler->disable_all_windows();

		auto info_line = (*(*global_window_handler)["SMIC"])["FLL"];
		auto ticks = (input_field*)(*(*global_window_handler)["SMIC"])["INT_TIC"];
		auto result = (text_box*)(*(*global_window_handler)["SMIC"])["ANSWER"];
		info_line->safe_string_replace("Integration has begun");

		std::int64_t ticks_limit = 0;
		ticks_limit = std::stoi(ticks->get_current_input("0"));

		double cur_seconds = 0;
		double prev_second = 0;
		double ppq = smic_ptr->ppq;
		double prev_tempo = 120;

		std::int64_t prev_tick = 0, cur_tick = 0;
		std::int64_t last_tick = (*smic_ptr->tempo_map.rbegin()).first;

		for (auto& cur_pair : smic_ptr->tempo_map/*; cur_pair != smic_ptr->tempo_map.end(); cur_pair++*/)
		{
			cur_tick = cur_pair.first;
			cur_seconds += (cur_tick - prev_tick) * (60 / (prev_tempo * ppq));
			if (cur_tick > ticks_limit || cur_tick == last_tick)
				break;

			prev_tempo = cur_pair.second;
			prev_second = cur_seconds;
			prev_tick = cur_tick;
		}

		auto cur = cur_tick - prev_tick;
		ticks_limit -= prev_tick;
		double rate = ((cur) ? ((double)ticks_limit / cur) : 0);
		double seconds_ans = (cur_seconds - prev_second) * rate + prev_second;
		double msec_rounded = std::round(seconds_ans * 1000);
		double milliseconds_ans = fmod(msec_rounded, 1000);
		seconds_ans = fmod(std::floor(msec_rounded / 1000), 60);
		double minutes_ans = std::floor(msec_rounded / 60000);

		result->safe_string_replace(
			"Min: " + std::to_string((int)(minutes_ans)) +
			"\nSec: " + std::to_string((int)(seconds_ans)) +
			"\nMsec: " + std::to_string((int)(milliseconds_ans))
		);

		global_window_handler->main_window_id = "MAIN";
		global_window_handler->enable_window("MAIN");
		global_window_handler->enable_window("SMIC");

		info_line->safe_string_replace("Integration was succsessfully finished");
	});
}

void integrate_time()
{
	worker_singleton<struct info_collection>::instance().push([]()
	{
		global_window_handler->main_window_id = "SMIC";
		global_window_handler->disable_all_windows();

		auto info_line = (*(*global_window_handler)["SMIC"])["FLL"];
		auto minutes = (input_field*)(*(*global_window_handler)["SMIC"])["INT_MIN"];
		auto seconds = (input_field*)(*(*global_window_handler)["SMIC"])["INT_SEC"];
		auto milliseconds = (input_field*)(*(*global_window_handler)["SMIC"])["INT_MSC"];
		auto result = (text_box*)(*(*global_window_handler)["SMIC"])["ANSWER"];

		info_line->safe_string_replace("Integration has begun");

		double seconds_limit = 0;
		seconds_limit += std::stoi(minutes->get_current_input("0")) * 60.;
		seconds_limit += std::stoi(seconds->get_current_input("0"));
		seconds_limit += std::stoi(milliseconds->get_current_input("0")) / 1000.;

		double cur_seconds = 0;
		double prev_second = 0;
		double ppq = smic_ptr->ppq;
		double prev_tempo = 120;

		std::int64_t prev_tick = 0, cur_tick = 0;
		std::int64_t last_tick = (*smic_ptr->tempo_map.rbegin()).first;

		for (auto& cur_pair : smic_ptr->tempo_map/*; cur_pair != smic_ptr->tempo_map.end(); cur_pair++*/)
		{
			cur_tick = cur_pair.first;
			cur_seconds += (cur_tick - prev_tick) * (60 / (prev_tempo * ppq));
			if (cur_seconds > seconds_limit || cur_tick == last_tick)
				break;

			prev_tempo = cur_pair.second;
			prev_second = cur_seconds;
			prev_tick = cur_tick;
		}

		cur_seconds -= prev_second;
		seconds_limit -= prev_second;
		auto rate = (seconds_limit == 0) ? 0 : seconds_limit / cur_seconds;
		std::int64_t tick = (cur_tick - prev_tick) * rate + prev_tick;

		result->safe_string_replace("Tick: " + std::to_string(tick));

		global_window_handler->main_window_id = "MAIN";
		global_window_handler->enable_window("MAIN");
		global_window_handler->enable_window("SMIC");

		info_line->safe_string_replace("Integration was succsessfully finished");
	});
}
}

}
