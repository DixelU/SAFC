#include "app_state.h"
#include "runtime.h"
#include "settings.h"
#include "file_actions.h"

namespace
{
	constexpr static auto CLI_inplace_doc = "SAFC CLI (Beta) wiki page: https://github.com/DixelU/SAFC/wiki/SAFC-CLI-(Beta)\n"
		"To run SAFC in CLI mode you need to pass the path to the JSON config file as an argument:\n\t(example:) SAFC.exe \"X:\\SAFC configs\\my_merge_config.json\"\n\n"
		"Or drop it on top of the executable.\n\n"
		"JSON file is expected to have the following structure \n\t(Field's optionality and type is provided after the double slashes on each line //)\n\n"
		R"({
	"global_ppq_override": 3860,                       // optional; signed long long int;
	"global_tempo_override" : 485,                     // optional; double;
	"global_offset_override" : 4558,                   // optional; signed long long int;
	"save_to" : "C:\\MIDIs\\merge.mid",                // optional; string (utf8)
	"files" :
	[
		{
			"filename": "D:\\Download\\Downloads\\Paprika's Aua Ah Community Merge (FULL).mid", // string (utf8)
			"ppq_override" : 960,                      // optional; unisnged short;
			"tempo_override" : 3.94899,                // optional; double;
			"offset" : 0,                              // optional; signed long long int;
			"selection_start" : 50,                    // optional; signed long long int;
			"selection_length" : 50,                   // optional; signed long long int;
			"ignore_notes" : false,                    // optional; bool;
			"ignore_pitches" : false,                  // optional; bool;
			"ignore_tempos" : false,                   // optional; bool;
			"ignore_other" : false,                    // optional; bool;
			"piano_only" : true,                       // optional; bool;
			"remove_remnants" : true,                  // optional; bool;
			"remove_empty_tracks" : true,              // optional; bool;
			"channel_split" : false,                   // optional; bool;
			"ignore_meta_rsb" : false,                 // optional; bool;
			"rsb_compression" : false,                 // optional; bool;
			"inplace_mergable" : false,                // optional; bool;
			"allow_sysex" : false                      // optional; bool;
			"enable_zero_velocity" : false,            // optional; bool;
			"apply_offset_after" : false               // optional; bool;
		}
	]
}
)";
}

void run_cli(int argc, char** argv)
{
	restore_reg_settings();

	ShowWindow(GetConsoleWindow(), SW_SHOW);
	g_data.detected_threads =
		std::max(
			std::min((std::uint16_t)(
				(std::uint16_t)std::max(
					std::thread::hardware_concurrency(),
					1u
				)
				- 1
				),
				(std::uint16_t)(ceil(get_available_memory() / 2048))
			), (std::uint16_t)1
		);
	g_data.is_cli_mode = true;

	if (argc < 2)
	{
		std::cerr << CLI_inplace_doc << std::flush;

		throw std::runtime_error("No config provided");
	}

	auto first_argument = std::string_view(argv[1]);
	if (first_argument == "/?" || first_argument == "-?" || first_argument == "--help" || first_argument == "/help")
	{
		std::cout << CLI_inplace_doc << std::flush;
		return;
	}

	JSONObject config;

	try
	{
		auto config_path = std::filesystem::u8path(argv[1]);
		std::ifstream fin(config_path);
		std::stringstream ss;
		ss << fin.rdbuf();
		auto config_content = ss.str();
		auto config_object = JSON::Parse(config_content.c_str());
		if (!config_object)
			throw std::runtime_error("No JSON data found");

		config = config_object->AsObject();
	}
	catch (const std::exception& ex)
	{
		std::cerr << "Error parsing JSON config object at '" << argv[1] << "'\n" << std::flush;
		std::cerr << "\t" << ex.what() << "\n" << std::endl;

		std::cerr << CLI_inplace_doc << std::flush;

		return;
	}

	auto global_ppq_override = config.find(L"global_ppq_override");
	auto global_tempo_override = config.find(L"global_tempo_override");
	auto global_offset = config.find(L"global_offset");
	auto files = config.find(L"files");

	if (files == config.end())
	{
		std::cerr << "Error finding the 'files' field\n\n" << std::flush;
		std::cerr << CLI_inplace_doc << std::flush;

		return;
	}

	if (global_ppq_override != config.end())
		g_data.set_global_ppqn(global_ppq_override->second->AsNumber());

	if (global_tempo_override != config.end())
		g_data.set_global_tempo(global_tempo_override->second->AsNumber());

	if (global_offset != config.end())
		g_data.set_global_offset(global_offset->second->AsNumber());

	auto& files_array = files->second->AsArray();
	std::vector<std::wstring> filenames;

	for (auto& single_entry : files_array)
	{
		auto& object = single_entry->AsObject();
		auto& filename = object.at(L"filename")->AsString();
		filenames.push_back(filename);
	}

	add_files(filenames);

	size_t index = 0;
	for (auto& single_entry : files_array)
	{
		auto& object = single_entry->AsObject();

		auto ppq_override = object.find(L"ppq_override");
		if (ppq_override != object.end())
			g_data.files[index].new_ppqn = ppq_override->second->AsNumber();

		auto tempo_override = object.find(L"tempo_override");
		if (tempo_override != object.end())
			g_data.files[index].new_tempo = tempo_override->second->AsNumber();

		auto offset = object.find(L"offset");
		if (offset != object.end())
			g_data.files[index].offset_ticks = offset->second->AsNumber();

		auto selection_start = object.find(L"selection_start");
		if (selection_start != object.end())
			g_data.files[index].selection_start = selection_start->second->AsNumber();

		auto selection_length = object.find(L"selection_length");
		if (selection_length != object.end())
			g_data.files[index].selection_length = selection_length->second->AsNumber();

		auto ignore_notes = object.find(L"ignore_notes");
		if (ignore_notes != object.end())
			g_data.files[index].set_bool_setting(_BoolSettings::ignore_notes, ignore_notes->second->AsBool());

		auto ignore_pitches = object.find(L"ignore_pitches");
		if (ignore_pitches != object.end())
			g_data.files[index].set_bool_setting(_BoolSettings::ignore_pitches, ignore_pitches->second->AsBool());

		auto ignore_tempos = object.find(L"ignore_tempos");
		if (ignore_tempos != object.end())
			g_data.files[index].set_bool_setting(_BoolSettings::ignore_tempos, ignore_tempos->second->AsBool());

		auto ignore_other = object.find(L"ignore_other");
		if (ignore_other != object.end())
			g_data.files[index].set_bool_setting(_BoolSettings::ignore_all_but_tempos_notes_and_pitch, ignore_other->second->AsBool());

		auto piano_only = object.find(L"piano_only");
		if (piano_only != object.end())
			g_data.files[index].set_bool_setting(_BoolSettings::all_instruments_to_piano, piano_only->second->AsBool());

		auto remove_remnants = object.find(L"remove_remnants");
		if (remove_remnants != object.end())
			g_data.files[index].set_bool_setting(_BoolSettings::remove_remnants, remove_remnants->second->AsBool());

		auto remove_empty_tracks = object.find(L"remove_empty_tracks");
		if (remove_empty_tracks != object.end())
			g_data.files[index].set_bool_setting(_BoolSettings::all_instruments_to_piano, remove_empty_tracks->second->AsBool());

		auto channel_split = object.find(L"channel_split");
		if (channel_split != object.end())
			g_data.files[index].channels_split = channel_split->second->AsBool();

		auto collapse_midi = object.find(L"collapse_midi");
		if (collapse_midi != object.end())
			g_data.files[index].collapse_midi = collapse_midi->second->AsBool();

		auto apply_offset_after = object.find(L"apply_offset_after");
		if (apply_offset_after != object.end())
			g_data.files[index].apply_offset_after = apply_offset_after->second->AsBool();

		auto rsb_compression = object.find(L"rsb_compression");
		if (rsb_compression != object.end())
			g_data.files[index].rsb_compression = rsb_compression->second->AsBool();

		auto ignore_meta_rsb = object.find(L"ignore_meta_rsb");
		if (ignore_meta_rsb != object.end())
			g_data.files[index].allow_legacy_rsb_meta_interaction = ignore_meta_rsb->second->AsBool();

		auto inplace_mergable = object.find(L"inplace_mergable");
		if (inplace_mergable != object.end())
			g_data.files[index].inplace_merge_enabled = inplace_mergable->second->AsBool();

		auto allow_sysex = object.find(L"allow_sysex");
		if (allow_sysex != object.end())
			g_data.files[index].allow_sysex = allow_sysex->second->AsBool();

		auto enable_zero_vel = object.find(L"enable_zero_velocity");
		if (enable_zero_vel != object.end())
			g_data.files[index].enable_zero_velocity = enable_zero_vel->second->AsBool();

		index++;
	}

	auto save_to = config.find(L"save_to");
	if (save_to != config.end())
	{
		g_data.save_path = (save_to->second->AsString());
		size_t Pos = g_data.save_path.rfind(L".mid");
		if (Pos >= g_data.save_path.size() || Pos <= g_data.save_path.size() - 4)
			g_data.save_path += L".mid";
	}

	auto local_mctm = g_data.mctm_constructor();
	local_mctm->start_processing();

	while (!local_mctm->is_smrp_complete())
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	if (local_mctm->has_failed())
		throw std::runtime_error(local_mctm->failure_message());
	local_mctm->start_ri_merge();
	while (!local_mctm->is_ri_merge_complete())
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	if (local_mctm->has_failed())
		throw std::runtime_error(local_mctm->failure_message());
	local_mctm->start_final_merge();
	while (!local_mctm->complete)
		std::this_thread::sleep_for(std::chrono::milliseconds(5));
	if (local_mctm->has_failed())
		throw std::runtime_error(local_mctm->failure_message());
}
