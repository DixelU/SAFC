#include "project_model.h"

namespace
{
struct sfd_rsp
{
	std::int32_t id;
	std::int64_t filesize;
	sfd_rsp(std::int32_t id, std::int64_t filesize)
	{
		this->id = id;
		this->filesize = filesize;
	}
	inline bool operator<(sfd_rsp a)
	{
		return filesize < a.filesize;
	}
};
}

file_settings::file_settings(const std::wstring& filename, std::uint32_t initial_flags) :
	filename(filename), bool_settings(initial_flags),
	file_name_postfix("_.mid"),
	w_file_name_postfix(L"_.mid")
{
	const auto name = std::filesystem::path(filename).filename().u8string();
	appearance_filename.assign(name.begin(), name.end());
	const auto path = std::filesystem::path(filename).u8string();
	appearance_path.assign(path.begin(), path.end());

	//cout << appearance_path << " ::\n";

	fast_midi_checker checker(filename);

	is_midi = checker.is_acssessible && checker.is_midi;
	old_ppqn = checker.PPQN;
	new_ppqn = checker.PPQN;
	old_track_number = checker.expected_track_number;
	filesize = checker.filesize;
}

void file_settings::switch_bool_setting(std::uint32_t smp_bool_setting)
{
	bool_settings ^= smp_bool_setting;
}

void file_settings::set_bool_setting(std::uint32_t smp_bool_setting, bool new_state)
{
	if (bool_settings & smp_bool_setting && new_state)
		return;

	if (!(bool_settings & smp_bool_setting) && !new_state)
		return;

	switch_bool_setting(smp_bool_setting);
}

std::shared_ptr<single_midi_processor_2::processing_data> file_settings::build_smrp_processing_data()
{
	auto smrp_data = std::make_shared<single_midi_processor_2::processing_data>();
	auto& settings = smrp_data->settings;
	smrp_data->filename = filename;
	smrp_data->postfix = w_file_name_postfix;

	if (volume_map)
		settings.volume_map = std::make_shared<dixelu::byte_polyline_lookup_table>(
			*volume_map, dixelu::polyline_extrapolation::linear);
	if (pitch_bend_map)
		settings.pitch_map = std::make_shared<dixelu::midi14_polyline_lookup_table>(
			dixelu::make_midi14_polyline_lookup_table(
				*pitch_bend_map, dixelu::polyline_extrapolation::linear));

	if (key_map)
		settings.key_converter = std::make_shared<cut_and_transpose>(*key_map);
	settings.new_ppqn = new_ppqn;
	settings.old_ppqn = old_ppqn;
	settings.enable_imp_events_filter = (bool_settings & _BoolSettings::enable_important_filter);
	settings.imp_events_filter.pass_instument_cnage = settings.enable_imp_events_filter && (bool_settings & _BoolSettings::imp_filter_allow_progc);
	settings.imp_events_filter.pass_notes = settings.enable_imp_events_filter && (bool_settings & _BoolSettings::imp_filter_allow_notes);
	settings.imp_events_filter.pass_pitch = settings.enable_imp_events_filter && (bool_settings & _BoolSettings::imp_filter_allow_pitch);
	settings.imp_events_filter.pass_tempo = settings.enable_imp_events_filter && (bool_settings & _BoolSettings::imp_filter_allow_tempo);
	settings.imp_events_filter.pass_other = settings.enable_imp_events_filter && (bool_settings & _BoolSettings::imp_filter_allow_other);
	settings.filter.pass_notes = !(bool_settings & _BoolSettings::ignore_notes);
	settings.filter.pass_pitch = !(bool_settings & _BoolSettings::ignore_pitches);
	settings.filter.pass_tempo = !(bool_settings & _BoolSettings::ignore_tempos);
	settings.filter.pass_other = !(bool_settings & _BoolSettings::ignore_all_but_tempos_notes_and_pitch);
	settings.filter.piano_only = (bool_settings & _BoolSettings::all_instruments_to_piano);
	settings.proc_details.remove_remnants = bool_settings & _BoolSettings::remove_remnants;
	settings.proc_details.remove_empty_tracks = bool_settings & _BoolSettings::remove_empty_tracks;
	settings.proc_details.channel_split = channels_split;
	settings.proc_details.whole_midi_collapse = collapse_midi;
	settings.proc_details.apply_offset_after = apply_offset_after;
	settings.legacy.enable_zero_velocity = enable_zero_velocity;
	settings.legacy.ignore_meta_rsb = allow_legacy_rsb_meta_interaction;
	settings.legacy.rsb_compression = rsb_compression;
	settings.filter.pass_sysex = allow_sysex;
	inplace_merge_enabled =
		settings.details.inplace_mergable = inplace_merge_enabled && !rsb_compression;
	settings.details.group_id = group_id;
	settings.details.initial_filesize = filesize;
	settings.offset = offset_ticks;

	if (new_tempo > 3.)
		settings.tempo.set_override_value(new_tempo);
	if (offset_ticks < 0 && -offset_ticks > selection_start)
		selection_start = -offset_ticks;
	if (selection_start && (selection_length < 0))
		selection_length -= selection_start;

	if (new_tempo > 3. && !time_map.empty())
	{
		settings.original_time_map = time_map;
		settings.flatten = true;
	}

	settings.selection_data = single_midi_processor_2::settings_obj::selection(selection_start, selection_length);

	smrp_data->appearance_filename = appearance_filename;

	return smrp_data;
}

safc_data::safc_data()
{
	global_ppqn = global_offset = global_new_tempo = 0;
	detected_threads = 1;
	incremental_ppqn = true;
	inplace_merge_flag = false;
	is_cli_mode = false;
	collapse_midi = false;
	save_path = L"";
	channels_split = rsb_compression = false;
	apply_offset_after = true;
}

void safc_data::resolve_subdivision_problem_group_id_assign(std::uint16_t threads_count)
{
	if (!threads_count)
		threads_count = detected_threads;

	if (files.empty())
	{
		save_path = L"";
		return;
	}
	else
		save_path = files[0].filename + L".AfterSAFC.mid";

	if (files.size() == 1)
	{
		files.front().group_id = 0;
		return;
	}

	std::vector<sfd_rsp> sizes;
	std::vector<std::int64_t> sum_size;
	std::int64_t current_total = 0;

	for (int i = 0; i < files.size(); i++)
		sizes.emplace_back(i, files[i].filesize);

	std::sort(sizes.begin(), sizes.end());

	for (int i = 0; i < sizes.size(); i++)
		sum_size.push_back((current_total += sizes[i].filesize));

	for (int i = 0; i < sum_size.size(); i++)
	{
		files[sizes[i].id].group_id = (std::uint16_t)(ceil(((float)sum_size[i] / ((float)sum_size.back())) * threads_count) - 1.);
		std::cout << "Thread " << files[sizes[i].id].group_id << ": " << sizes[i].filesize << ":\t" << sizes[i].id << std::endl;
	}
}

void safc_data::set_global_ppqn(std::uint16_t new_ppqn, bool force_global_ppqn_override)
{
	if (!new_ppqn && force_global_ppqn_override)
		return;

	if (!force_global_ppqn_override)
		new_ppqn = global_ppqn;

	if (!force_global_ppqn_override && (!new_ppqn || incremental_ppqn))
	{
		for (int i = 0; i < files.size(); i++)
			if (new_ppqn < files[i].old_ppqn)
				new_ppqn = files[i].old_ppqn;
	}

	for (int i = 0; i < files.size(); i++)
	{
		if (!force_global_ppqn_override && files[i].ppqn_manually_set)
			continue;
		files[i].new_ppqn = new_ppqn;
	}

	if (force_global_ppqn_override)
		for (int i = 0; i < files.size(); i++)
			files[i].ppqn_manually_set = false;

	global_ppqn = new_ppqn;
}

void safc_data::set_global_offset(std::int32_t offset)
{
	for (int i = 0; i < files.size(); i++)
		files[i].offset_ticks = offset;

	global_offset = offset;
}

void safc_data::set_global_tempo(float new_tempo)
{
	for (int i = 0; i < files.size(); i++)
		files[i].new_tempo = new_tempo;

	global_new_tempo = new_tempo;
}

void safc_data::remove_by_id(std::uint32_t id)
{
	if (id < files.size())
		files.erase(files.begin() + id);
}

std::shared_ptr<midi_collection_threaded_merger> safc_data::mctm_constructor()
{
	using proc_data_ptr = std::shared_ptr<single_midi_processor_2::processing_data>;
	std::vector<proc_data_ptr> proc_data;

	for (int i = 0; i < files.size(); i++)
		proc_data.push_back(files[i].build_smrp_processing_data());

	return std::make_shared<midi_collection_threaded_merger>(proc_data, global_ppqn, save_path, is_cli_mode);
}

file_settings& safc_data::operator[](std::int32_t id)
{
	return files[id];
}


