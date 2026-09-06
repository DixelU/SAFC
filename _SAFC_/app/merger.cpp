#include "app_state.h"
#include "merger.h"
#include "dialogs.h"

std::pair<float, float> get_position_for_one_of(std::int32_t Position, std::int32_t Amount, float UnitSize, float HeightRel)
{
	std::pair<float, float> coords{0.f, 0.f};
	std::int32_t side_count = ceil(sqrt(Amount));

	coords.first = (0.f - static_cast<float>(Position % side_count) + ((side_count - 1) / 2.f)) * UnitSize;
	coords.second = (0.f - static_cast<float>(Position / side_count) + ((side_count - 1) / 2.f)) * UnitSize * HeightRel;

	return coords;
}

void on_start()
{
	if (g_data.files.empty())
		return;

	worker_singleton<struct merge>::instance().push([](std::stop_token stop_token)
	{
		if (gui_stop_requested(stop_token))
			return;
		global_window_handler->main_window_id = "SMRP_CONTAINER";
		global_window_handler->disable_all_windows();
		global_window_handler->enable_window("SMRP_CONTAINER");

		global_mctm = g_data.mctm_constructor();

		auto start_timepoint = std::chrono::high_resolution_clock::now();

		global_mctm->start_processing();

		auto merge_preview_container = (*global_window_handler)["SMRP_CONTAINER"];
		std::vector<std::string> undesired_window_activities;

		for (auto& single_activity_pair : merge_preview_container->window_activities)
		{
			if (single_activity_pair.first.substr(0, 6) == "SMRP_C")
				undesired_window_activities.push_back(single_activity_pair.first);
		}

		for (auto& name : undesired_window_activities)
			merge_preview_container->delete_ui_element_by_name(name);

		auto timer_ptr = (input_field*)(*merge_preview_container)["TIMER"];
		auto now = std::chrono::high_resolution_clock::now();
		auto difference = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_timepoint);
		timer_ptr->safe_string_replace(std::to_string(difference.count() * 0.001) + " s");

		std::this_thread::sleep_for(std::chrono::milliseconds(100));

		auto currently_processed_copy = global_mctm->snapshot_currently_processed();

		for (size_t id = 0; id < currently_processed_copy.size(); id++)
		{
			auto position = get_position_for_one_of(id, currently_processed_copy.size(), 140, 0.7);
			auto visualiser = std::make_unique<midi_processor_visualiser>(position.first, position.second, &system_white);
			auto visualiser_ref = std::ref(*visualiser);

			std::string element_id;
			merge_preview_container->add_ui_element(element_id = "SMRP_C" + std::to_string(id), std::move(visualiser));

			std::thread([](
				std::shared_ptr<midi_collection_threaded_merger> merger_ptr,
				midi_processor_visualiser& vis_ref,
				size_t id)
			{
				std::string SID = "SMRP_C" + std::to_string(id);
				std::cout << SID << " Processing started" << std::endl;
				while (!merger_ptr->is_smrp_complete() &&
					!application_shutting_down.load(std::memory_order_acquire))
				{
					merger_ptr->with_currently_processed_item(id, [&](const auto& item)
					{
						vis_ref.set_smrp(item);
					});
					std::this_thread::sleep_for(std::chrono::milliseconds(66));
				}
				std::cout << SID << " Processing stopped" << std::endl;
			}, global_mctm, visualiser_ref, id).detach();
		}

		worker_singleton<struct merge_ri_stage>::instance().push(
			[safc_data_pointer = &g_data, merge_preview_container](std::stop_token stage_stop)
		{
			while (!global_mctm->is_smrp_complete() && !gui_stop_requested(stage_stop))
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			if (gui_stop_requested(stage_stop))
				return;
			if (global_mctm->has_failed())
			{
				global_mctm->complete.store(true, std::memory_order_release);
				return;
			}
			global_mctm->start_ri_merge();

			std::cout << "SMRP: Out from sleep\n" << std::flush;
			for (int i = 0; i <= (int)global_mctm->currently_processed_count(); i++)
				merge_preview_container->delete_ui_element_by_name("SMRP_C" + std::to_string(i));

			merge_preview_container->safe_change_position_argumented(0, 0, 0);

			(*merge_preview_container)["IM"] =
				std::make_unique<bool_and_number_checker<decltype(global_mctm->inplace_merge_complete), decltype(global_mctm->inplace_track_count)>>
				(-100.f, 0.f, &system_white, &(global_mctm->inplace_merge_complete), &(global_mctm->inplace_track_count));
			(*merge_preview_container)["RM"] =
				std::make_unique<bool_and_number_checker<decltype(global_mctm->regular_merge_complete), decltype(global_mctm->regular_track_count)>>
				(100.f, 0.f, &system_white, &(global_mctm->regular_merge_complete), &(global_mctm->regular_track_count));

			worker_singleton<struct merge_ri_stage_cleanup>::instance().push(
				[safc_data_pointer, merge_preview_container](std::stop_token cleanup_stop)
			{
				while (!global_mctm->is_ri_merge_complete() && !gui_stop_requested(cleanup_stop))
					std::this_thread::sleep_for(std::chrono::milliseconds(33));
				if (gui_stop_requested(cleanup_stop))
					return;
				global_mctm->start_final_merge();

				std::cout << "RI: Out from sleep!\n";
				merge_preview_container->delete_ui_element_by_name("IM");
				merge_preview_container->delete_ui_element_by_name("RM");
				merge_preview_container->safe_change_position_argumented(0, 0, 0);

				(*merge_preview_container)["FM"] =
					std::make_unique<bool_and_number_checker<decltype(global_mctm->complete), int>>
					(0.f, 0.f, &system_white, &(global_mctm->complete), nullptr);
			});
		});

		worker_singleton<struct merge_global_cleanup>::instance().push(
			[start_timepoint, merge_preview_container](std::stop_token cleanup_stop)
		{
			auto timer_ptr = (input_field*)(*merge_preview_container)["TIMER"];

			while (!global_mctm->complete && !gui_stop_requested(cleanup_stop))
			{
				auto now = std::chrono::high_resolution_clock::now();
				auto difference = std::chrono::duration_cast<std::chrono::duration<double>>(now - start_timepoint);

				timer_ptr->safe_string_replace(std::to_string(difference.count()) + " s");
				std::this_thread::sleep_for(std::chrono::milliseconds(50));

				auto freeMemory = get_available_memory();
				if (freeMemory < 512)
				{
					auto message =
						"There is less than " +
						std::to_string(freeMemory) +
						"MB of available RAM!\n"
						"SAFC may corrupt MIDI data or fail to finish the processing!";

					std::cout << message << std::endl;
					throw_alert_warning(std::move(message));
				}
			}

			if (gui_stop_requested(cleanup_stop))
				return;
			std::cout << "F: Out from sleep!!!\n";
			merge_preview_container->delete_ui_element_by_name("FM");

			global_window_handler->disable_window(global_window_handler->main_window_id);
			global_window_handler->main_window_id = "MAIN";
			//global_window_handler->disable_all_windows();
			global_window_handler->enable_window("MAIN");
			if (global_mctm->has_failed())
				throw_alert_error(global_mctm->failure_message());
			//global_mctm->ResetEverything();
		});
	});
}

void on_save_to()
{
	if (gui_stop_requested())
		return;
	g_data.save_path = save_open_file_dialog(L"Save final midi to...");
	if (gui_stop_requested())
		return;
	size_t Pos = g_data.save_path.rfind(L".mid");
	if (Pos >= g_data.save_path.size() || Pos <= g_data.save_path.size() - 4)
		g_data.save_path += L".mid";
}

