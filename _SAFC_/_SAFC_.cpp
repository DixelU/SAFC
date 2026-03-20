#define NOMINMAX

#include <algorithm>
#include <cstdlib>
#include <io.h>
#include <tuple>
#include <mutex>
#include <iostream>
#include <vector>
#include <filesystem>
#include <deque>
#include <fstream>
#include <string>
#include <iterator>
#include <map>
#include <thread>
#include <boost/algorithm/string.hpp>

#include <WinSock2.h>

#pragma comment (lib, "Version.lib")//Urlmon.lib
#pragma comment (lib, "Urlmon.lib")//Urlmon.lib
#pragma comment (lib, "wininet.lib")//Urlmon.lib
#pragma comment (lib, "dwmapi.lib")
#pragma comment (lib, "Ws2_32.Lib")
#pragma comment (lib, "Wldap32.Lib")
#pragma comment (lib, "Crypt32.Lib")
#pragma comment (lib, "XmlLite.lib")

#include "WinRegWrappers.h"

#include "JSON/JSON.h"
#include "JSON/JSON.cpp"
#include "JSON/JSONValue.cpp"

#include "btree/btree_map.h"
#include "SAFGUIF/fonted_manip.h"
#include "SAFGUIF/SAFGUIF.h"
#include "SAFC_InnerModules/include_all.h"
#include "SAFCGUIF_Local/SAFGUIF_L.h"

#include "SAFC_InnerModules/single_midi_processor_2.h"
#include "SAFC_InnerModules/bool_settings.h"
#include "consts.h"

#include "background_worker.h"

#include <boost/dll.hpp>
#include <archive.h>
#include <archive_entry.h>

std::tuple<std::uint16_t, std::uint16_t, std::uint16_t, std::uint16_t> g_version_tuple;
std::tuple<std::uint16_t, std::uint16_t, std::uint16_t, std::uint16_t> ___GetVersion() 
{
	// get the filename of the executable containing the version resource
	TCHAR szFilename[MAX_PATH + 1] = { 0 };
	if (GetModuleFileName(NULL, szFilename, MAX_PATH) == 0)
		return { 0,0,0,0 };
	// allocate a block of memory for the version info
	DWORD dummy;
	std::uint32_t dwSize = GetFileVersionInfoSize(szFilename, &dummy);
	if (dwSize == 0)
		return { 0,0,0,0 };
	std::vector<std::uint8_t> data(dwSize);
	// load the version info
	if (!GetFileVersionInfo(szFilename, 0, dwSize, &data[0]))
		return { 0,0,0,0 };
	////////////////////////////////////
	UINT                uiVerLen = 0;
	VS_FIXEDFILEINFO* pFixedInfo = 0;     // pointer to fixed file info structure
	// get the fixed file info (language-independent) 
	if (VerQueryValue(&data[0], TEXT("\\"), (void**)&pFixedInfo, (UINT*)&uiVerLen) == 0)
		return { 0,0,0,0 };
	return
	{
		HIWORD(pFixedInfo->dwProductVersionMS),
		LOWORD(pFixedInfo->dwProductVersionMS),
		HIWORD(pFixedInfo->dwProductVersionLS),
		LOWORD(pFixedInfo->dwProductVersionLS) 
	};
}

std::wstring extract_directory(const std::wstring& path) 
{
	constexpr wchar_t delim = (L"\\")[0];
	return path.substr(0, path.find_last_of(delim) + 1);
}

static int copy_data(struct archive* ar, struct archive* aw)
{
	int r;
	const void* buff;
	size_t size;
	la_int64_t offset;

	for (;;) 
	{
		r = archive_read_data_block(ar, &buff, &size, &offset);
		if (r == ARCHIVE_EOF)
			return (ARCHIVE_OK);
		if (r < ARCHIVE_OK)
			return (r);
		r = archive_write_data_block(aw, buff, size, offset);
		if (r < ARCHIVE_OK) 
		{
			fprintf(stderr, "%s\n", archive_error_string(aw));
			return (r);
		}
	}
}

static void extract(const void* data, size_t data_size)
{
	struct archive* a;
	struct archive* ext;
	struct archive_entry* entry;
	int flags;
	int r;

	/* Select which attributes we want to restore. */
	flags = ARCHIVE_EXTRACT_TIME;
	flags |= ARCHIVE_EXTRACT_PERM;
	flags |= ARCHIVE_EXTRACT_ACL;
	flags |= ARCHIVE_EXTRACT_FFLAGS;

	a = archive_read_new();
	archive_read_support_format_all(a);
	archive_read_support_filter_all(a);
	ext = archive_write_disk_new();
	archive_write_disk_set_options(ext, flags);
	archive_write_disk_set_standard_lookup(ext);

	if ((r = archive_read_open_memory(a, data, data_size)))
		throw std::runtime_error("Failed to open archive: code " + std::to_string(r) + " - " + archive_error_string(a));

	for (;;)
	{
		r = archive_read_next_header(a, &entry);
		if (r == ARCHIVE_EOF)
			break;
		if (r < ARCHIVE_OK)
			fprintf(stderr, "%s\n", archive_error_string(a));
		if (r < ARCHIVE_WARN)
			throw std::runtime_error("Failed to read the archive header: code " + std::to_string(r) + " - " + archive_error_string(a));

		r = archive_write_header(ext, entry);
		if (r < ARCHIVE_OK)
			fprintf(stderr, "%s\n", archive_error_string(ext));
		else if (archive_entry_size(entry) > 0)
		{
			r = copy_data(a, ext);

			if (r < ARCHIVE_OK)
				fprintf(stderr, "%s\n", archive_error_string(ext));
			if (r < ARCHIVE_WARN)
				throw std::runtime_error("Failed to write the archive data: code " + std::to_string(r) + " - " + archive_error_string(ext));
		}
		r = archive_write_finish_entry(ext);
		if (r < ARCHIVE_OK)
			fprintf(stderr, "%s\n", archive_error_string(ext));
		if (r < ARCHIVE_WARN)
			throw std::runtime_error("Failed to intilise write of archive data: code " + std::to_string(r) + " - " + archive_error_string(ext));
	}

	archive_read_close(a);
	archive_read_free(a);
	archive_write_close(ext);
	archive_write_free(ext);
}

bool safc_update(const std::wstring& latest_release)
{
#ifndef __X64
	constexpr wchar_t* const archive_name = (wchar_t* const)L"SAFC32.7z";;
#else
	constexpr wchar_t* const archive_name = (wchar_t* const)L"SAFC64.7z";
#endif

	bool updated_flag = false;
	std::string error_msg;

	wchar_t current_file_path[MAX_PATH];
	//GetCurrentDirectoryW(MAX_PATH, current_file_path);
	GetModuleFileNameW(NULL, current_file_path, MAX_PATH);

	//std::wstring executablepath = current_file_path;
	std::wstring filename = extract_directory(current_file_path);
	std::wstring pathway = filename;

	filename += L"update.7z";
	//wsprintfW(current_file_path, L"%S%S", filename.c_str(), L"update.7z\0");
	std::wstring link = L"https://github.com/DixelU/SAFC/releases/download/" + latest_release + L"/" + archive_name;

	HRESULT co_res = URLDownloadToFileW(NULL, link.c_str(), filename.c_str(), 0, NULL);
	
	auto executable_filename = boost::dll::program_location().filename().wstring();

	if (co_res == S_OK) 
	{
		errno = 0;
		_wrename((pathway + executable_filename).c_str(), (pathway + L"_s").c_str());
		std::cout << "Rename status " << strerror(errno) << std::endl;
		if (!errno) try
		{
			std::stringstream container_stringstream;
			std::ifstream fin(filename, std::ios_base::binary);
			container_stringstream << fin.rdbuf();
			fin.close();
			auto data = container_stringstream.str();

			extract(data.c_str(), data.size());

			if (!_waccess((pathway + L"SAFC.exe").c_str(), 0))
			{
				_wrename((pathway + L"SAFC.exe").c_str(), (pathway + executable_filename).c_str());
				updated_flag = true;
			}
			else
			{
				std::wcout << L"Failed: " << errno << std::endl;

				error_msg = std::string("No SAFC executable found in unpacked data... Aborting...\n") + strerror(errno);
			}
		}
		catch (const std::exception& e) 
		{
			error_msg = std::string("Autoudate error (unpack exception):\n") + e.what();
		}
		else 
		{
			error_msg = std::string("Autoudate error (unable to access self): \n") + strerror(errno);
		}

		_wremove(filename.c_str());
	}
	else if (check_autoupdates)
		error_msg = ("Autoupdate error: #" + std::to_string(co_res));
	else
		std::cout << "Autoupdate error: #" + std::to_string(co_res) << std::endl;

	if (check_autoupdates)
		throw_alert_error(std::move(error_msg));
	else
		std::cout << std::move(error_msg) << std::endl;

	if(error_msg.size())
		_wrename((pathway + L"_s").c_str(), (pathway + executable_filename).c_str());

	return updated_flag;
}

void safc_version_check()
{
	auto [maj, min, ver, build] = g_version_tuple;
	std::wcout << L"Current vesion: v" << maj << L"." << min << L"." << ver << L"." << build << L"\n";

	if (!check_autoupdates)
		return;

	worker_singleton<struct version_check>::instance().push([]() 
	{
		bool flag = false;
		_wremove(L"_s");
		constexpr wchar_t* SAFC_tags_link = (wchar_t* const)L"https://api.github.com/repos/DixelU/SAFC/tags";
		auto current_file_path = boost::dll::program_location().filename().wstring();
		std::wstring filename = extract_directory(current_file_path);
		std::wstring pathway = filename;
		filename += L"tags.json";
		// if you have error here -> https://github.com/Microsoft/WSL/issues/22#issuecomment-207788173
		HRESULT res = URLDownloadToFileW(NULL, SAFC_tags_link, filename.c_str(), 0, NULL);
		try 
		{
			if (res == S_OK) 
			{
				auto [maj, min, ver, build] = g_version_tuple;

				std::string json_buffer;
				std::ifstream input(filename);
				std::getline(input, json_buffer);
				input.close();

				auto value = JSON::Parse(json_buffer.c_str());
				auto& git_latest_version = ((value)->AsArray()[0])->AsObject().at(L"name")->AsString();

				std::uint16_t version_partied[4] = { 0,0,0,0 };
				std::vector<std::string> ans;
				std::wstring git_version_numbers_only = git_latest_version.substr(1);//v?.?.?.?
				boost::algorithm::split(ans, git_version_numbers_only, boost::is_any_of("."));

				int index = 0;
				for (auto& num_val : ans) 
				{
					try 
					{
						version_partied[index] = std::stoi(num_val);
					}
					catch (...)
					{
						break;
					}

					if (index == 3)
						break;

					index++;
				}

				std::wcout << L"Git latest version: v" << 
					version_partied[0] << L"." << version_partied[1] << L"." << version_partied[2] << L"." << version_partied[3] << L"\n";

				if (check_autoupdates &&
					(maj < version_partied[0] ||
						maj == version_partied[0] && min < version_partied[1] ||
						maj == version_partied[0] && min == version_partied[1] && ver < version_partied[2] ||
						maj == version_partied[0] && min == version_partied[1] && ver == version_partied[2] && build < version_partied[3]
						))
				{
					throw_alert_warning("Update found! The app might restart soon...\nUpdate: " + std::string(git_latest_version.begin(), git_latest_version.end()));

					flag = safc_update(git_latest_version);

					if (flag)
						throw_alert_warning("SAFC will restart in 3 seconds...");
				}
			}
			else if (check_autoupdates)
			{
				auto msg = "Most likely your internet connection is unstable\nSAFC cannot check for updates";

				throw_alert_warning(msg);
				std::cout << msg;
			}
		}
		catch (const std::exception& e)
		{
			throw_alert_warning("SAFC just almost crashed while checking the update...\nTell developer about that " +
				std::string() + e.what());
		}

		_wremove(filename.c_str());

		if (flag)
		{
			Sleep(3000);

			ShellExecuteW(NULL, L"open", current_file_path.c_str(), NULL, NULL, SW_SHOWNORMAL);
			//_wsystem((L"start \"" + executablepath + L"\"").c_str());
			exit(0);
		}
	});
}

size_t get_available_memory() 
{
	static std::mutex mutex; 
	std::lock_guard<std::mutex> locker(mutex);

	size_t ret = 0;

	// because compiler static links the function...
	BOOL(__stdcall * GMSEx)(LPMEMORYSTATUSEX) = 0;

	static HINSTANCE hIL = LoadLibrary(L"kernel32.dll");
	GMSEx = (BOOL(__stdcall*)(LPMEMORYSTATUSEX))GetProcAddress(hIL, "GlobalMemoryStatusEx");
	if (GMSEx) 
	{
		MEMORYSTATUSEX m{};
		m.dwLength = sizeof(m);
		if (GMSEx(&m))
		{
			ret = (int)(m.ullAvailPhys >> 20);
		}
	}
	else 
	{
		MEMORYSTATUS m{};
		m.dwLength = sizeof(m);
		GlobalMemoryStatus(&m);
		ret = (int)(m.dwAvailPhys >> 20);
	}

	return ret;
}

//////////////////////////////
////TRUE USAGE STARTS HERE////
//////////////////////////////

button_settings* bs_list_black_small = new button_settings(&system_white, 0, 0, 100, 10, 1, 0, 0, 0xFFEFDFFF, 0x00003F7F, 0x7F7F7FFF);

std::uint32_t default_bool_settings = _BoolSettings::remove_remnants | _BoolSettings::remove_empty_tracks | _BoolSettings::all_instruments_to_piano;

struct file_settings
{////per file settings
	std::wstring filename;
	std::wstring postprocessed_file_name, w_file_name_postfix;
	std::string appearance_filename, appearance_path, file_name_postfix;

	double new_tempo = 0;

	std::uint16_t new_ppqn = 0, old_ppqn = 0, old_track_number = 0, merge_multiplier = 0;
	std::int16_t group_id = 0;

	std::uint64_t filesize = 0;
	std::int64_t selection_start = 0, selection_length = -1;
	std::uint32_t bool_settings = default_bool_settings;
	std::int64_t offset_ticks = 0;

	bool
		is_midi = false,
		inplace_merge_enabled = false,
		allow_legacy_rsb_meta_interaction = false,
		rsb_compression = false,
		channels_split = true,
		collapse_midi = false,
		allow_sysex = false,
		enable_zero_velocity = false,
		apply_offset_after = true;

	std::shared_ptr<cut_and_transpose> key_map;
	std::shared_ptr<polyline_converter<std::uint8_t, std::uint8_t>> volume_map;
	std::shared_ptr<polyline_converter<std::uint16_t, std::uint16_t>> pitch_bend_map;

	single_midi_info_collector::time_graph time_map;

	file_settings(const std::wstring& filename): 
		filename(filename),
		file_name_postfix("_.mid"),
		w_file_name_postfix(L"_.mid")
	{
		auto pos = filename.rfind('\\');
		for (; pos < filename.size(); pos++)
			appearance_filename.push_back(filename[pos] & 0xFF);
		for (int i = 0; i < filename.size(); i++)
			appearance_path.push_back((char)(filename[i] & 0xFF));

		//cout << appearance_path << " ::\n";

		fast_midi_checker checker(filename);

		is_midi = checker.is_acssessible && checker.is_midi;
		old_ppqn = checker.PPQN;
		new_ppqn = checker.PPQN;
		old_track_number = checker.expected_track_number;
		filesize = checker.filesize;
	}

	inline void switch_bool_setting(std::uint32_t smp_bool_setting) 
	{
		bool_settings ^= smp_bool_setting;
	}

	inline void set_bool_setting(std::uint32_t smp_bool_setting, bool new_state) 
	{
		if (bool_settings & smp_bool_setting && new_state)
			return;
		
		if (!(bool_settings & smp_bool_setting) && !new_state)
			return;

		switch_bool_setting(smp_bool_setting);
	}

	std::shared_ptr<single_midi_processor_2::processing_data> build_smrp_processing_data() 
	{
		auto smrp_data = std::make_shared<single_midi_processor_2::processing_data>();
		auto& settings = smrp_data->settings;
		smrp_data->filename = filename;
		smrp_data->postfix = w_file_name_postfix;

		if(volume_map)
			settings.volume_map = std::make_shared<byte_plc_core>(volume_map);
		if(pitch_bend_map)
			settings.pitch_map = std::make_shared<_14bit_plc_core>(pitch_bend_map);

		settings.key_converter = key_map;
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
};

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

struct safc_data
{
	////overall settings and storing perfile settings....
	std::vector<file_settings> files;
	std::wstring save_path;
	std::uint16_t global_ppqn;
	std::int32_t global_offset;

	float global_new_tempo;
	bool incremental_ppqn;
	bool inplace_merge_flag;
	bool channels_split;
	bool rsb_compression;
	bool collapse_midi;
	bool apply_offset_after;
	bool is_cli_mode;

	std::uint16_t detected_threads;

	safc_data()
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

	void resolve_subdivision_problem_group_id_assign(std::uint16_t threads_count = 0)
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

	void set_global_ppqn(std::uint16_t new_ppqn = 0, bool force_global_ppqn_override = false)
	{
		if (!new_ppqn && force_global_ppqn_override)
			return;

		if (!force_global_ppqn_override)
			new_ppqn = global_ppqn;

		if (!force_global_ppqn_override && (!new_ppqn || incremental_ppqn))
		{
			for (int i = 0; i < files.size(); i++)
				if (new_ppqn < files[i].old_ppqn)new_ppqn = files[i].old_ppqn;
		}

		for (int i = 0; i < files.size(); i++)
			files[i].new_ppqn = new_ppqn;

		global_ppqn = new_ppqn;
	}

	void set_global_offset(std::int32_t offset)
	{
		for (int i = 0; i < files.size(); i++)
			files[i].offset_ticks = offset;

		global_offset = offset;
	}

	void set_global_tempo(float new_tempo)
	{
		for (int i = 0; i < files.size(); i++)
			files[i].new_tempo = new_tempo;

		global_new_tempo = new_tempo;
	}

	void remove_by_id(std::uint32_t id)
	{
		if (id < files.size())
			files.erase(files.begin() + id);
	}

	std::shared_ptr<midi_collection_threaded_merger> mctm_constructor()
	{
		using proc_data_ptr = std::shared_ptr<single_midi_processor_2::processing_data>;
		std::vector<proc_data_ptr> proc_data;

		for (int i = 0; i < files.size(); i++)
			proc_data.push_back(files[i].build_smrp_processing_data());

		return std::make_shared<midi_collection_threaded_merger>(proc_data, global_ppqn, save_path, is_cli_mode);
	}

	file_settings& operator[](std::int32_t id)
	{
		return files[id];
	}
};

safc_data g_data;
std::shared_ptr<midi_collection_threaded_merger> global_mctm;

void throw_alert_error(std::string&& AlertText) 
{
	std::cerr << AlertText << std::endl;

	if (global_window_handler)
		global_window_handler->throw_alert(AlertText, "ERROR!", special_signs::draw_ex_triangle, true, 0xFFAF00FF, 0xFF);
}

void throw_alert_warning(std::string&& AlertText)
{
	std::cout << AlertText << std::endl;

	if (global_window_handler)
		global_window_handler->throw_alert(AlertText, "Warning!", special_signs::draw_ex_triangle, true, 0x7F7F7FFF, 0xFFFFFFAF);
}

std::vector<std::wstring> multiple_open_file_dialog(const wchar_t* Title)
{
	OPENFILENAME ofn;       // common dialog box structure
	wchar_t szFile[50000];       // buffer for file name
	std::vector<std::wstring> InpLinks;
	ZeroMemory(&ofn, sizeof(ofn));
	ZeroMemory(szFile, 50000 * sizeof(wchar_t));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = NULL;
	ofn.lpstrFile = szFile;
	ofn.lpstrFile[0] = '\0';
	ofn.nMaxFile = sizeof(szFile);
	ofn.lpstrFilter = L"MIDI files(*.mid)\0*.mid\0";
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = NULL;
	ofn.lpstrTitle = Title;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = NULL;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_EXPLORER;
	if (GetOpenFileName(&ofn))
	{
		std::wstring Link = L"", Gen = L"";
		int i = 0, counter = 0;
		for (; i < 600 && szFile[i] != '\0'; i++)
			Link.push_back(szFile[i]);

		for (; i < 49998;)
		{
			counter++;
			Gen.clear();
			for (; i < 49998 && szFile[i] != '\0'; i++)
				Gen.push_back(szFile[i]);
			i++;
			if (szFile[i] == '\0')
			{
				if (counter == 1) InpLinks.push_back(Link);
				else InpLinks.push_back(Link + L"\\" + Gen);
				break;
			}
			else if (Gen != L"")InpLinks.push_back(Link + L"\\" + Gen);
		}
		return InpLinks;
	}
	else 
	{
		switch (CommDlgExtendedError()) 
		{
		case CDERR_DIALOGFAILURE:		 throw_alert_error("CDERR_DIALOGFAILURE\n");   break;
		case CDERR_FINDRESFAILURE:		 throw_alert_error("CDERR_FINDRESFAILURE\n");  break;
		case CDERR_INITIALIZATION:	 throw_alert_error("CDERR_INITIALIZATION\n"); break;
		case CDERR_LOADRESFAILURE:	 throw_alert_error("CDERR_LOADRESFAILURE\n"); break;
		case CDERR_LOADSTRFAILURE:	 throw_alert_error("CDERR_LOADSTRFAILURE\n"); break;
		case CDERR_LOCKRESFAILURE:	 throw_alert_error("CDERR_LOCKRESFAILURE\n"); break;
		case CDERR_MEMALLOCFAILURE:	 throw_alert_error("CDERR_MEMALLOCFAILURE\n"); break;
		case CDERR_MEMLOCKFAILURE:	 throw_alert_error("CDERR_MEMLOCKFAILURE\n"); break;
		case CDERR_NOHINSTANCE:		 throw_alert_error("CDERR_NOHINSTANCE\n"); break;
		case CDERR_NOHOOK:			 throw_alert_error("CDERR_NOHOOK\n"); break;
		case CDERR_NOTEMPLATE:		 throw_alert_error("CDERR_NOTEMPLATE\n"); break;
		case CDERR_STRUCTSIZE:		 throw_alert_error("CDERR_STRUCTSIZE\n"); break;
		case FNERR_BUFFERTOOSMALL:	 throw_alert_error("FNERR_BUFFERTOOSMALL\n"); break;
		case FNERR_INVALIDFILENAME:	 throw_alert_error("FNERR_INVALIDFILENAME\n"); break;
		case FNERR_SUBCLASSFAILURE:	 throw_alert_error("FNERR_SUBCLASSFAILURE\n"); break;
		}
		return std::vector<std::wstring>{L""};
	}
}
std::wstring save_open_file_dialog(const wchar_t* Title)
{
	wchar_t filename[MAX_PATH];
	OPENFILENAME ofn;
	ZeroMemory(&filename, sizeof(filename));
	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = NULL;  // If you have a merge_preview_container to center over, put its HANDLE here
	ofn.lpstrFilter = L"MIDI files(*.mid)\0*.mid\0";
	ofn.lpstrFile = filename;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrTitle = Title;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_NOREADONLYRETURN | OFN_HIDEREADONLY;
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = NULL;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = NULL;
	if (GetSaveFileName(&ofn)) return std::wstring(filename);
	else 
	{
		switch (CommDlgExtendedError())
		{
		case CDERR_DIALOGFAILURE:		 throw_alert_error("CDERR_DIALOGFAILURE\n");   break;
		case CDERR_FINDRESFAILURE:		 throw_alert_error("CDERR_FINDRESFAILURE\n");  break;
		case CDERR_INITIALIZATION:	 throw_alert_error("CDERR_INITIALIZATION\n"); break;
		case CDERR_LOADRESFAILURE:	 throw_alert_error("CDERR_LOADRESFAILURE\n"); break;
		case CDERR_LOADSTRFAILURE:	 throw_alert_error("CDERR_LOADSTRFAILURE\n"); break;
		case CDERR_LOCKRESFAILURE:	 throw_alert_error("CDERR_LOCKRESFAILURE\n"); break;
		case CDERR_MEMALLOCFAILURE:	 throw_alert_error("CDERR_MEMALLOCFAILURE\n"); break;
		case CDERR_MEMLOCKFAILURE:	 throw_alert_error("CDERR_MEMLOCKFAILURE\n"); break;
		case CDERR_NOHINSTANCE:		 throw_alert_error("CDERR_NOHINSTANCE\n"); break;
		case CDERR_NOHOOK:			 throw_alert_error("CDERR_NOHOOK\n"); break;
		case CDERR_NOTEMPLATE:		 throw_alert_error("CDERR_NOTEMPLATE\n"); break;
		case CDERR_STRUCTSIZE:		 throw_alert_error("CDERR_STRUCTSIZE\n"); break;
		case FNERR_BUFFERTOOSMALL:	 throw_alert_error("FNERR_BUFFERTOOSMALL\n"); break;
		case FNERR_INVALIDFILENAME:	 throw_alert_error("FNERR_INVALIDFILENAME\n"); break;
		case FNERR_SUBCLASSFAILURE:	 throw_alert_error("FNERR_SUBCLASSFAILURE\n"); break;
		}
		return L"";
	}
}

////////// IMPORTANT STUFF ABOVE ///////////

handleable_ui_part* _WH(const char* window, const char* element)
{
	return ((*(*global_window_handler)[window])[element]);
}

template<typename ui_part_type>
ui_part_type* _WH_t(const char* window, const char* element) requires std::is_base_of_v<handleable_ui_part, ui_part_type>
{
	return dynamic_cast<ui_part_type*>(_WH(window, element));
}

void add_files(const std::vector<std::wstring>& Filenames)
{
	if(global_window_handler)
		global_window_handler->disable_all_windows();

	for (int i = 0; i < Filenames.size(); i++)
	{
		if (Filenames[i].empty())
			continue;
		g_data.files.push_back(file_settings(Filenames[i]));
		auto& lastFile = g_data.files.back();
		if (lastFile.is_midi)
		{
			if (global_window_handler)
				_WH_t<selectable_properted_list>("MAIN", "List")->safe_push_back_new_string(lastFile.appearance_filename);

			std::uint32_t Counter = 0;
			lastFile.new_tempo = g_data.global_new_tempo;
			lastFile.offset_ticks = g_data.global_offset;
			lastFile.inplace_merge_enabled = g_data.inplace_merge_flag;
			lastFile.channels_split = g_data.channels_split;
			lastFile.rsb_compression = g_data.rsb_compression;
			lastFile.collapse_midi = g_data.collapse_midi;
			lastFile.apply_offset_after = g_data.apply_offset_after;

			for (int q = 0; q < g_data.files.size(); q++)
			{
				if (g_data[q].filename == lastFile.filename)
				{
					g_data[q].file_name_postfix = std::to_string(Counter) + "_.mid";
					g_data[q].w_file_name_postfix = std::to_wstring(Counter) + L"_.mid";
					Counter++;
				}
			}
		}
		else
		{
			g_data.files.pop_back();
		}
	}

	g_data.set_global_ppqn();
	g_data.resolve_subdivision_problem_group_id_assign();
}

void on_add()
{
	worker_singleton<struct midi_file_list>::instance().push([](){
		std::vector<std::wstring> Filenames = multiple_open_file_dialog(L"Select midi files");
		add_files(Filenames);
	});
}

namespace props_and_sets
{
	std::string* PPQN = new std::string(""), * OFFSET = new std::string(""), * TEMPO = new std::string("");
	int current_id = -1, cat_id = -1, vm_id = -1, pm_id = -1;
	bool human_readible = true; // tf is this
	single_midi_info_collector* smic_ptr = nullptr;
	std::string csv_delim = ";";

	void open_file_properties(int id)
	{
		if (!(id < g_data.files.size() && id >= 0))
		{
			current_id = -1;
			return;
		}

		current_id = id;
		auto settings_window_ptr = (*global_window_handler)["SMPAS"];
		auto other_settings_window_ptr = (*global_window_handler)["OTHER_SETS"];

		((text_box*)((*settings_window_ptr)["FileName"]))->safe_string_replace("..." + g_data[id].appearance_filename);
		((input_field*)((*settings_window_ptr)["PPQN"]))->update_input_string();
		((input_field*)((*settings_window_ptr)["PPQN"]))->safe_string_replace(std::to_string((g_data[id].new_ppqn) ? g_data[id].new_ppqn : g_data[id].old_ppqn));
		((input_field*)((*settings_window_ptr)["TEMPO"]))->safe_string_replace(std::to_string(g_data[id].new_tempo));
		((input_field*)((*settings_window_ptr)["OFFSET"]))->safe_string_replace(std::to_string(g_data[id].offset_ticks));
		((input_field*)((*settings_window_ptr)["GROUPID"]))->safe_string_replace(std::to_string(g_data[id].group_id));

		((input_field*)((*settings_window_ptr)["SELECT_START"]))->safe_string_replace(std::to_string(g_data[id].selection_start));
		((input_field*)((*settings_window_ptr)["SELECT_LENGTH"]))->safe_string_replace(std::to_string(g_data[id].selection_length));

		((checkbox*)((*settings_window_ptr)["BOOL_REM_TRCKS"]))->state = g_data[id].bool_settings & _BoolSettings::remove_empty_tracks;
		((checkbox*)((*settings_window_ptr)["BOOL_REM_REM"]))->state = g_data[id].bool_settings & _BoolSettings::remove_remnants;

		((checkbox*)((*other_settings_window_ptr)["BOOL_PIANO_ONLY"]))->state = g_data[id].bool_settings & _BoolSettings::all_instruments_to_piano;
		((checkbox*)((*other_settings_window_ptr)["BOOL_IGN_TEMPO"]))->state = g_data[id].bool_settings & _BoolSettings::ignore_tempos;
		((checkbox*)((*other_settings_window_ptr)["BOOL_IGN_PITCH"]))->state = g_data[id].bool_settings & _BoolSettings::ignore_pitches;
		((checkbox*)((*other_settings_window_ptr)["BOOL_IGN_NOTES"]))->state = g_data[id].bool_settings & _BoolSettings::ignore_notes;
		((checkbox*)((*other_settings_window_ptr)["BOOL_IGN_ALL_EX_TPS"]))->state = g_data[id].bool_settings & _BoolSettings::ignore_all_but_tempos_notes_and_pitch;

		((checkbox*)((*other_settings_window_ptr)["IMP_FLT_ENABLE"]))->state = g_data[id].bool_settings & _BoolSettings::enable_important_filter;
		((checkbox*)((*other_settings_window_ptr)["IMP_FLT_NOTES"]))->state = g_data[id].bool_settings & _BoolSettings::imp_filter_allow_notes;
		((checkbox*)((*other_settings_window_ptr)["IMP_FLT_TEMPO"]))->state = g_data[id].bool_settings & _BoolSettings::imp_filter_allow_tempo;
		((checkbox*)((*other_settings_window_ptr)["IMP_FLT_PITCH"]))->state = g_data[id].bool_settings & _BoolSettings::imp_filter_allow_pitch;
		((checkbox*)((*other_settings_window_ptr)["IMP_FLT_PROGC"]))->state = g_data[id].bool_settings & _BoolSettings::imp_filter_allow_progc;
		((checkbox*)((*other_settings_window_ptr)["IMP_FLT_OTHER"]))->state = g_data[id].bool_settings & _BoolSettings::imp_filter_allow_other;

		((checkbox*)((*other_settings_window_ptr)["LEGACY_META_RSB_BEHAVIOR"]))->state = g_data[id].allow_legacy_rsb_meta_interaction;
		((checkbox*)((*other_settings_window_ptr)["ALLOW_SYSEX"]))->state = g_data[id].allow_sysex;
		((checkbox*)((*other_settings_window_ptr)["ENABLE_ZERO_VELOCITY"]))->state = g_data[id].enable_zero_velocity;

		((checkbox*)((*settings_window_ptr)["SPLIT_TRACKS"]))->state = g_data[id].channels_split;
		((checkbox*)((*settings_window_ptr)["RSB_COMPRESS"]))->state = g_data[id].rsb_compression;

		((checkbox*)((*settings_window_ptr)["INPLACE_MERGE"]))->state = g_data[id].inplace_merge_enabled;

		((checkbox*)((*settings_window_ptr)["COLLAPSE_MIDI"]))->state = g_data[id].collapse_midi;
		((checkbox*)((*settings_window_ptr)["APPLY_OFFSET_AFTER"]))->state = g_data[id].apply_offset_after;

		((text_box*)((*settings_window_ptr)["CONSTANT_PROPS"]))->safe_string_replace(
			"File size: " + std::to_string(g_data[id].filesize) + "b\n" +
			"Old PPQN: " + std::to_string(g_data[id].old_ppqn) + "\n" +
			"Track number (header info): " + std::to_string(g_data[id].old_track_number) + "\n" +
			"\"Remnant\" file postfix: " + g_data[id].file_name_postfix + "\n" +
			"Time map status: " + ((g_data[id].time_map.empty()) ? "Empty" : "Present")
		);

		global_window_handler->enable_window("SMPAS");
	}

	void initialize_collecting()
	{
		if (current_id < 0 && current_id >= g_data.files.size()) 
		{
			throw_alert_error("How you have managed to select the midi beyond the list? O.o\n" + std::to_string(current_id));
			return;
		}

		auto tempo_graph = (Graphing<single_midi_info_collector::tempo_graph>*)(*(*global_window_handler)["SMIC"])["TEMPO_GRAPH"];
		if (smic_ptr)
		{
			{ std::lock_guard<std::recursive_mutex> locker(tempo_graph->lock); }
			tempo_graph->graph = nullptr;
		}

		smic_ptr = new single_midi_info_collector(g_data.files[current_id].filename, g_data.files[current_id].old_ppqn, g_data.files[current_id].allow_legacy_rsb_meta_interaction);

		worker_singleton<struct info_collection>::instance().push([]()
		{
			global_window_handler->main_window_id = "SMIC";
			global_window_handler->disable_all_windows();

			worker_singleton<struct info_collection_watcher>::instance().push([]()
			{
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

				ui_poly_graph->enabled = false;
				ui_poly_graph->reset();
				ui_tempo_graph->enabled = false;
				ui_tempo_graph->reset();

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

				while (!smic_ptr->finished)
				{
					if (error_line->text != smic_ptr->error_line)
						error_line->safe_string_replace(smic_ptr->error_line);
					if (info_line->text != smic_ptr->log_line)
						info_line->safe_string_replace(smic_ptr->log_line);
					Sleep(10);
				}

				info_line->safe_string_replace("Finished");
				export_all->enable();
				export_tempo->enable();
				integrate_ticks->enable();
				integrate_time->enable();
			});

			smic_ptr->fetch_data();
			auto tempo_graph = (Graphing<single_midi_info_collector::tempo_graph>*)(*(*global_window_handler)["SMIC"])["TEMPO_GRAPH"];
			tempo_graph->graph = &(smic_ptr->tempo_map);
			auto poly_graph = (Graphing<single_midi_info_collector::polyphony_graph>*)(*(*global_window_handler)["SMIC"])["POLY_GRAPH"];
			poly_graph->graph = &(smic_ptr->polyphony);

			auto midi_info = (text_box*)(*(*global_window_handler)["SMIC"])["TOTAL_INFO"];
			midi_info->safe_string_replace(
				"Total (real) tracks: " + std::to_string(smic_ptr->tracks.size()) + "; ... "
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

		void reset_tg()
		{
			auto tempo_graph = (Graphing<single_midi_info_collector::tempo_graph>*)(*(*global_window_handler)["SMIC"])["TEMPO_GRAPH"];
			tempo_graph->reset();
		}

		void reset_pg()
		{
			auto poly_graph = (Graphing<single_midi_info_collector::tempo_graph>*)(*(*global_window_handler)["SMIC"])["POLY_GRAPH"];
			poly_graph->reset();
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
				for (auto& [tick, tempo]  : smic_ptr->tempo_map)
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

	void on_apply_settings()
	{
		if (current_id < 0 && current_id >= g_data.files.size())
		{
			throw_alert_warning("You cannot apply current settings to file with id " + std::to_string(current_id));
			return;
		}

		std::int32_t T;
		std::string current_string = "";
		auto settings_window = (*global_window_handler)["SMPAS"];
		auto other_settings_window = (*global_window_handler)["OTHER_SETS"];

		current_string = ((input_field*)(*settings_window)["PPQN"])->get_current_input("0");
		if (current_string.size())
		{
			T = std::stoi(current_string);
			if (T)g_data[current_id].new_ppqn = T;
			else g_data[current_id].new_ppqn = g_data.global_ppqn;
		}

		current_string = ((input_field*)(*settings_window)["TEMPO"])->get_current_input("0");
		if (current_string.size())
		{
			float F = stof(current_string);
			g_data[current_id].new_tempo = F;
		}

		current_string = ((input_field*)(*settings_window)["OFFSET"])->get_current_input("0");
		if (current_string.size())
		{
			T = stoll(current_string);
			g_data[current_id].offset_ticks = T;
		}

		current_string = ((input_field*)(*settings_window)["GROUPID"])->get_current_input("0");
		if (current_string.size())
		{
			T = stoi(current_string);
			if (T != g_data[current_id].group_id)
			{
				g_data[current_id].group_id = T;
				throw_alert_warning("Manual group_id editing might cause significant drop of processing perfomance!");
			}
		}

		current_string = ((input_field*)(*settings_window)["SELECT_START"])->get_current_input("0");
		if (current_string.size())
		{
			T = stoll(current_string);
			g_data[current_id].selection_start = T;
		}

		current_string = ((input_field*)(*settings_window)["SELECT_LENGTH"])->get_current_input("-1");
		if (current_string.size())
		{
			T = stoll(current_string);
			g_data[current_id].selection_length = T;
		}

		g_data[current_id].allow_legacy_rsb_meta_interaction = (((checkbox*)(*other_settings_window)["LEGACY_META_RSB_BEHAVIOR"])->state);

		if (g_data[current_id].allow_legacy_rsb_meta_interaction)
			std::cout << "WARNING: Legacy way of treating running status events can also allow major corruptions of midi structure!" << std::endl;

		g_data[current_id].set_bool_setting(_BoolSettings::remove_empty_tracks, (((checkbox*)(*settings_window)["BOOL_REM_TRCKS"])->state));
		g_data[current_id].set_bool_setting(_BoolSettings::remove_remnants, (((checkbox*)(*settings_window)["BOOL_REM_REM"])->state));

		g_data[current_id].set_bool_setting(_BoolSettings::all_instruments_to_piano, (((checkbox*)(*other_settings_window)["BOOL_PIANO_ONLY"])->state));
		g_data[current_id].set_bool_setting(_BoolSettings::ignore_tempos, (((checkbox*)(*other_settings_window)["BOOL_IGN_TEMPO"])->state));
		g_data[current_id].set_bool_setting(_BoolSettings::ignore_pitches, (((checkbox*)(*other_settings_window)["BOOL_IGN_PITCH"])->state));
		g_data[current_id].set_bool_setting(_BoolSettings::ignore_notes, (((checkbox*)(*other_settings_window)["BOOL_IGN_NOTES"])->state));
		g_data[current_id].set_bool_setting(_BoolSettings::ignore_all_but_tempos_notes_and_pitch, (((checkbox*)(*other_settings_window)["BOOL_IGN_ALL_EX_TPS"])->state));

		g_data[current_id].set_bool_setting(_BoolSettings::enable_important_filter, (((checkbox*)(*other_settings_window)["IMP_FLT_ENABLE"])->state));
		g_data[current_id].set_bool_setting(_BoolSettings::imp_filter_allow_notes, (((checkbox*)(*other_settings_window)["IMP_FLT_NOTES"])->state));
		g_data[current_id].set_bool_setting(_BoolSettings::imp_filter_allow_tempo, (((checkbox*)(*other_settings_window)["IMP_FLT_TEMPO"])->state));
		g_data[current_id].set_bool_setting(_BoolSettings::imp_filter_allow_pitch, (((checkbox*)(*other_settings_window)["IMP_FLT_PITCH"])->state));
		g_data[current_id].set_bool_setting(_BoolSettings::imp_filter_allow_progc, (((checkbox*)(*other_settings_window)["IMP_FLT_PROGC"])->state));
		g_data[current_id].set_bool_setting(_BoolSettings::imp_filter_allow_other, (((checkbox*)(*other_settings_window)["IMP_FLT_OTHER"])->state));

		g_data[current_id].enable_zero_velocity = (((checkbox*)(*other_settings_window)["ENABLE_ZERO_VELOCITY"])->state);
		g_data[current_id].allow_sysex = (((checkbox*)(*other_settings_window)["ALLOW_SYSEX"])->state);

		g_data[current_id].rsb_compression = ((checkbox*)(*settings_window)["RSB_COMPRESS"])->state;
		g_data[current_id].channels_split = ((checkbox*)(*settings_window)["SPLIT_TRACKS"])->state;
		g_data[current_id].collapse_midi = ((checkbox*)(*settings_window)["COLLAPSE_MIDI"])->state;
		g_data[current_id].apply_offset_after = ((checkbox*)(*settings_window)["APPLY_OFFSET_AFTER"])->state;

		auto& inplaceMergeState = ((checkbox*)(*settings_window)["INPLACE_MERGE"])->state;

		inplaceMergeState &= !g_data[current_id].rsb_compression;
		g_data[current_id].inplace_merge_enabled = inplaceMergeState;
	}

	void on_apply_bs2a()
	{
		if (current_id < 0 && current_id >= g_data.files.size())
		{
			throw_alert_warning("You cannot apply current settings to file with id " + std::to_string(current_id));
			return;
		}

		on_apply_settings();

		for (auto& settings : g_data.files)
		{
			default_bool_settings = settings.bool_settings = g_data[current_id].bool_settings;
			g_data.inplace_merge_flag = settings.inplace_merge_enabled = g_data[current_id].inplace_merge_enabled;
			settings.allow_legacy_rsb_meta_interaction = g_data[current_id].allow_legacy_rsb_meta_interaction;
			settings.collapse_midi = g_data[current_id].collapse_midi;
			settings.apply_offset_after = g_data[current_id].apply_offset_after;
			settings.allow_sysex = g_data[current_id].allow_sysex;
			settings.channels_split = g_data[current_id].channels_split;
			settings.rsb_compression = g_data[current_id].rsb_compression;
		}
	}

	namespace cut_and_transpose
	{
		std::uint8_t cut_max = 0, cut_min = 0;
		std::int16_t transpose_value = 0;

		void on_cat()
		{
			auto window = (*global_window_handler)["CAT"];
			auto tool_ptr = (cut_and_transpose_piano*)((*window)["CAT_ITSELF"]);

			if (!g_data[current_id].key_map)
				g_data[current_id].key_map = std::make_shared<::cut_and_transpose>(0, 127, 0);

			tool_ptr->piano_transform = g_data[current_id].key_map;
			tool_ptr->update_info();

			global_window_handler->enable_window("CAT");
		}

		void on_reset()
		{
			auto window = (*global_window_handler)["CAT"];
			auto tool_ptr = (cut_and_transpose_piano*)((*window)["CAT_ITSELF"]);

			tool_ptr->piano_transform->max_val = 255;
			tool_ptr->piano_transform->min_val = 0;
			tool_ptr->piano_transform->transpose_val = 0;

			tool_ptr->update_info();
		}

		void on_cdt128()
		{
			auto window = (*global_window_handler)["CAT"];
			auto tool_ptr = (cut_and_transpose_piano*)((*window)["CAT_ITSELF"]);

			tool_ptr->piano_transform->max_val = 127;
			tool_ptr->piano_transform->min_val = 0;
			tool_ptr->piano_transform->transpose_val = 0;

			tool_ptr->update_info();
		}

		void on_0_127_to_128_255()
		{
			auto window = (*global_window_handler)["CAT"];
			auto tool_ptr = (cut_and_transpose_piano*)((*window)["CAT_ITSELF"]);

			tool_ptr->piano_transform->max_val = 127;
			tool_ptr->piano_transform->min_val = 0;
			tool_ptr->piano_transform->transpose_val = 128;

			tool_ptr->update_info();
		}

		void on_copy()
		{
			auto window = (*global_window_handler)["CAT"];
			auto tool_ptr = (cut_and_transpose_piano*)((*window)["CAT_ITSELF"]);

			cut_max = tool_ptr->piano_transform->max_val;
			cut_min = tool_ptr->piano_transform->min_val;
			transpose_value = tool_ptr->piano_transform->transpose_val;
		}

		void on_paste()
		{
			auto window = (*global_window_handler)["CAT"];
			auto tool_ptr = (cut_and_transpose_piano*)((*window)["CAT_ITSELF"]);

			tool_ptr->piano_transform->max_val = cut_max;
			tool_ptr->piano_transform->min_val = cut_min;
			tool_ptr->piano_transform->transpose_val = transpose_value;

			tool_ptr->update_info();
		}

		void on_delete()
		{
			auto window = (*global_window_handler)["CAT"];
			((cut_and_transpose_piano*)((*window)["CAT_ITSELF"]))->piano_transform = nullptr;
			global_window_handler->disable_window("CAT");

			g_data[current_id].key_map = nullptr;
		}
	}

	namespace volume_map
	{
		void on_vol_map()
		{
			auto window = (*global_window_handler)["VM"];
			auto tool_ptr = ((volume_graph*)(*window)["VM_PLC"]);

			((button*)(*window)["VM_SETMODE"])->safe_string_replace("Single");

			auto degree_input = ((input_field*)(*window)["VM_DEGREE"]);
			degree_input->update_input_string("1");
			degree_input->current_string.clear();

			tool_ptr->active_setting = 0;
			tool_ptr->hovered = 0;
			tool_ptr->re_put_mode = 0;

			if (!g_data[current_id].volume_map)
				g_data[current_id].volume_map = std::make_shared<polyline_converter<std::uint8_t, std::uint8_t>>();
			tool_ptr->plc_bb = g_data[current_id].volume_map;

			global_window_handler->enable_window("VM");
		}

		void on_degree_shape()
		{
			auto window = (*global_window_handler)["VM"];
			auto tool_ptr = ((volume_graph*)(*window)["VM_PLC"]);

			if (tool_ptr->plc_bb)
			{
				auto degree_input = ((input_field*)(*window)["VM_DEGREE"]);
				float degree = std::stof(degree_input->get_current_input("0"));

				tool_ptr->plc_bb->map.clear();
				tool_ptr->plc_bb->map[127] = 127;

				for (int i = 0; i < 128; i++) 
					tool_ptr->plc_bb->insert(i, std::ceil(std::pow(i / 127., degree) * 127.));
			}
			else
				throw_alert_error("If you see this message, some error might have happen, since PLC_bb is null");
		}

		void on_simplify()
		{
			auto window = (*global_window_handler)["VM"];
			auto tool_ptr = ((volume_graph*)(*window)["VM_PLC"]);

			if (tool_ptr->plc_bb)
				tool_ptr->make_map_more_simple();
			else
				throw_alert_error("If you see this message, some error might have happen, since PLC_bb is null");
		}

		void on_trace()
		{
			auto window = (*global_window_handler)["VM"];
			auto tool_ptr = ((volume_graph*)(*window)["VM_PLC"]);

			if (tool_ptr->plc_bb)
			{
				if (tool_ptr->plc_bb->map.empty())
					return;

				std::uint8_t values_array[256]{};

				for (int i = 0; i < 255; i++)
					values_array[i] = tool_ptr->plc_bb->at(i);

				for (int i = 0; i < 255; i++)
					tool_ptr->plc_bb->map[i] = values_array[i];
			}
			else
				throw_alert_error("If you see this message, some error might have happen, since PLC_bb is null");
		}

		void on_set_mode_change() 
		{
			auto window = (*global_window_handler)["VM"];
			auto tool_ptr = ((volume_graph*)(*window)["VM_PLC"]);

			if (tool_ptr->plc_bb)
			{
				tool_ptr->re_put_mode = !tool_ptr->re_put_mode;
				((button*)(*window)["VM_SETMODE"])->safe_string_replace(((tool_ptr->re_put_mode) ? "Double" : "Single"));
			}
			else
				throw_alert_error("If you see this message, some error might have happen, since PLC_bb is null");
		}

		void on_erase()
		{
			auto window = (*global_window_handler)["VM"];
			auto tool_ptr = ((volume_graph*)(*window)["VM_PLC"]);

			if (tool_ptr->plc_bb)
				tool_ptr->plc_bb->map.clear();
			else
				throw_alert_error("If you see this message, some error might have happen, since PLC_bb is null");
		}

		void on_delete()
		{
			if (g_data[current_id].volume_map)
				g_data[current_id].volume_map = nullptr;

			auto window = (*global_window_handler)["VM"];
			auto tool_ptr = ((volume_graph*)(*window)["VM_PLC"]);
			tool_ptr->plc_bb = nullptr;

			global_window_handler->disable_window("VM");
		}
	}

	void on_pitch_map()
	{
		// throw_alert_error("Having hard time thinking of how to implement it...\nNot available... yet..."); // will be never availible
	}
}

void on_rem()
{
	worker_singleton<struct midi_file_list>::instance().push([]()
	{
		auto ptr = _WH_t<selectable_properted_list>("MAIN", "List");
		for (auto id = ptr->selected_id.rbegin(); id != ptr->selected_id.rend(); ++id)
			g_data.remove_by_id(*id);

		ptr->remove_selected();

		global_window_handler->disable_all_windows();

		g_data.set_global_ppqn();
		g_data.resolve_subdivision_problem_group_id_assign();
	});
}

void on_rem_all()
{
	worker_singleton<struct midi_file_list>::instance().push([](){
		auto ptr = _WH_t<selectable_properted_list>("MAIN", "List");

		global_window_handler->disable_all_windows();

		while (g_data.files.size())
		{
			g_data.remove_by_id(0);
			ptr->safe_remove_string_by_id(0);
		}
	});
}

void on_submit_global_ppqn()
{
	auto pptr = (*global_window_handler)["PROMPT"];

	std::string input_field_data = ((input_field*)(*pptr)["FLD"])->get_current_input("0");
	std::uint16_t PPQN = (input_field_data.size()) ? stoi(input_field_data) : g_data.global_ppqn;

	g_data.set_global_ppqn(PPQN, true);
	global_window_handler->disable_window("PROMPT");

	//props_and_sets::open_file_properties(props_and_sets::current_id);
}

void on_global_ppqn()
{
	global_window_handler->throw_prompt(
		"New value will be assigned to every MIDI\n(in settings)",
		"Global PPQN",
		on_submit_global_ppqn,
		_Align::center,
		input_field::Type::NaturalNumbers,
		std::to_string(g_data.global_ppqn),
		5);
}

void on_submit_global_offset()
{
	auto pptr = (*global_window_handler)["PROMPT"];

	std::string input_field_data = ((input_field*)(*pptr)["FLD"])->get_current_input("0");
	std::uint32_t offset = (input_field_data.size()) ? std::stoi(input_field_data) : g_data.global_offset;

	g_data.set_global_offset(offset);
	global_window_handler->disable_window("PROMPT");
}

void on_global_offset()
{
	global_window_handler->throw_prompt(
		"Sets new global offset",
		"Global Offset",
		on_submit_global_offset,
		_Align::center,
		input_field::Type::WholeNumbers,
		std::to_string(g_data.global_offset),
		10);
}

void on_submit_global_tempo()
{
	auto pptr = (*global_window_handler)["PROMPT"];

	std::string input_field_data = ((input_field*)(*pptr)["FLD"])->get_current_input("0");
	float tempo = (input_field_data.size()) ? std::stof(input_field_data) : g_data.global_new_tempo;

	g_data.set_global_tempo(tempo);
	global_window_handler->disable_window("PROMPT");
}

void on_global_tempo()
{
	global_window_handler->throw_prompt("Sets specific tempo value to every MIDI\n(in settings)", "Global S. Tempo\0", on_submit_global_tempo, _Align::center, input_field::Type::FP_PositiveNumbers, std::to_string(g_data.global_new_tempo), 8);
}

void on_resolve()
{
	g_data.resolve_subdivision_problem_group_id_assign();
}

void on_rem_vol_maps()
{
	((volume_graph*)(*((*global_window_handler)["VM"]))["VM_PLC"])->plc_bb = nullptr;
	global_window_handler->disable_window("VM");

	for (int i = 0; i < g_data.files.size(); i++)
		g_data[i].volume_map = nullptr;
}

void on_rem_cats()
{
	global_window_handler->disable_window("CAT");
	for (int i = 0; i < g_data.files.size(); i++)
		g_data[i].key_map = nullptr;
}

void on_rem_pitch_maps()
{
	//global_window_handler->disable_window("CAT");
	throw_alert_warning("Currently pitch maps can not be created and/or deleted :D");
	for (int i = 0; i < g_data.files.size(); i++)
		g_data[i].pitch_bend_map = nullptr;
}

void on_rem_all_modules()
{
	on_rem_vol_maps();
	on_rem_cats();
}

namespace settings
{
	std::int32_t background_id = 0;
	WinReg::RegKey regestry_access;

	void on_settings()
	{
		global_window_handler->enable_window("APP_SETTINGS");//g_data.detected_threads
		
		auto app_settings_window = (*global_window_handler)["APP_SETTINGS"];
		((input_field*)(*app_settings_window)["AS_BCKGID"])->update_input_string(std::to_string(background_id));
		((input_field*)(*app_settings_window)["AS_ROT_ANGLE"])->update_input_string(std::to_string(dumb_rotation_angle));
		((input_field*)(*app_settings_window)["AS_THREADS_COUNT"])->update_input_string(std::to_string(g_data.detected_threads));

		((checkbox*)((*app_settings_window)["BOOL_REM_TRCKS"]))->state = default_bool_settings & _BoolSettings::remove_empty_tracks;
		((checkbox*)((*app_settings_window)["BOOL_REM_REM"]))->state = default_bool_settings & _BoolSettings::remove_remnants;
		((checkbox*)((*app_settings_window)["BOOL_PIANO_ONLY"]))->state = default_bool_settings & _BoolSettings::all_instruments_to_piano;
		((checkbox*)((*app_settings_window)["BOOL_IGN_TEMPO"]))->state = default_bool_settings & _BoolSettings::ignore_tempos;
		((checkbox*)((*app_settings_window)["BOOL_IGN_PITCH"]))->state = default_bool_settings & _BoolSettings::ignore_pitches;
		((checkbox*)((*app_settings_window)["BOOL_IGN_NOTES"]))->state = default_bool_settings & _BoolSettings::ignore_notes;
		((checkbox*)((*app_settings_window)["BOOL_IGN_ALL_EX_TPS"]))->state = default_bool_settings & _BoolSettings::ignore_all_but_tempos_notes_and_pitch;

		((checkbox*)((*app_settings_window)["SPLIT_TRACKS"]))->state = g_data.channels_split;
		((checkbox*)((*app_settings_window)["RSB_COMPRESS"]))->state = g_data.rsb_compression;
		((checkbox*)((*app_settings_window)["COLLAPSE_MIDI"]))->state = g_data.collapse_midi; 
		((checkbox*)((*app_settings_window)["APPLY_OFFSET_AFTER"]))->state = g_data.apply_offset_after;
		
		((checkbox*)((*app_settings_window)["INPLACE_MERGE"]))->state = g_data.inplace_merge_flag;
		((checkbox*)((*app_settings_window)["AUTOUPDATECHECK"]))->state = check_autoupdates;
	}

	void on_set_apply()
	{
		bool registry_opened = false;
		try
		{
			settings::regestry_access.Open(HKEY_CURRENT_USER, default_reg_path);
			registry_opened = true;
		}
		catch (...)
		{
			std::cout << "RK opening failed\n";
		}

		auto pptr = (*global_window_handler)["APP_SETTINGS"];
		std::string input_field_string;

		input_field_string = ((input_field*)(*pptr)["AS_BCKGID"])->get_current_input("0");
		std::cout << "AS_BCKGID " << input_field_string << std::endl;
		if (input_field_string.size())
		{
			background_id = std::stoi(input_field_string);
			if (registry_opened) TRY_CATCH(regestry_access.SetDwordValue(L"AS_BCKGID", background_id); , "Failed on setting AS_BCKGID")
		}
		std::cout << background_id << std::endl;

		input_field_string = ((input_field*)(*pptr)["AS_ROT_ANGLE"])->get_current_input("0");
		std::cout << "ROT_ANGLE " << input_field_string << std::endl;
		if (input_field_string.size() && !is_fonted)
			dumb_rotation_angle = stof(input_field_string);
		std::cout << dumb_rotation_angle << std::endl;

		input_field_string = ((input_field*)(*pptr)["AS_THREADS_COUNT"])->get_current_input(std::to_string(g_data.detected_threads));
		std::cout << "AS_THREADS_COUNT " << input_field_string << std::endl;
		if (input_field_string.size())
		{
			g_data.detected_threads = stoi(input_field_string);
			g_data.resolve_subdivision_problem_group_id_assign();
			if (registry_opened) TRY_CATCH(regestry_access.SetDwordValue(L"AS_THREADS_COUNT", g_data.detected_threads); , "Failed on setting AS_THREADS_COUNT")
		}
		std::cout << g_data.detected_threads << std::endl;

		default_bool_settings = (default_bool_settings & (~_BoolSettings::remove_empty_tracks)) | (_BoolSettings::remove_empty_tracks * (!!((checkbox*)(*pptr)["BOOL_REM_TRCKS"])->state));
		default_bool_settings = (default_bool_settings & (~_BoolSettings::remove_remnants)) | (_BoolSettings::remove_remnants * (!!((checkbox*)(*pptr)["BOOL_REM_REM"])->state));
		default_bool_settings = (default_bool_settings & (~_BoolSettings::all_instruments_to_piano)) | (_BoolSettings::all_instruments_to_piano * (!!((checkbox*)(*pptr)["BOOL_PIANO_ONLY"])->state));
		default_bool_settings = (default_bool_settings & (~_BoolSettings::ignore_tempos)) | (_BoolSettings::ignore_tempos * (!!((checkbox*)(*pptr)["BOOL_IGN_TEMPO"])->state));
		default_bool_settings = (default_bool_settings & (~_BoolSettings::ignore_pitches)) | (_BoolSettings::ignore_pitches * (!!((checkbox*)(*pptr)["BOOL_IGN_PITCH"])->state));
		default_bool_settings = (default_bool_settings & (~_BoolSettings::ignore_notes)) | (_BoolSettings::ignore_notes * (!!((checkbox*)(*pptr)["BOOL_IGN_NOTES"])->state));
		default_bool_settings = (default_bool_settings & (~_BoolSettings::ignore_all_but_tempos_notes_and_pitch)) | (_BoolSettings::ignore_all_but_tempos_notes_and_pitch * (!!((checkbox*)(*pptr)["BOOL_IGN_ALL_EX_TPS"])->state));

		check_autoupdates = ((checkbox*)(*pptr)["AUTOUPDATECHECK"])->state;

		g_data.channels_split = ((checkbox*)((*pptr)["SPLIT_TRACKS"]))->state;
		g_data.rsb_compression = ((checkbox*)((*pptr)["RSB_COMPRESS"]))->state;

		g_data.collapse_midi = ((checkbox*)((*pptr)["COLLAPSE_MIDI"]))->state;
		g_data.apply_offset_after = ((checkbox*)((*pptr)["APPLY_OFFSET_AFTER"]))->state;

		if (registry_opened)
		{
			TRY_CATCH(regestry_access.SetDwordValue(L"AUTOUPDATECHECK", check_autoupdates);, "Failed on setting AUTOUPDATECHECK")
			TRY_CATCH(regestry_access.SetDwordValue(L"SPLIT_TRACKS", g_data.channels_split); , "Failed on setting SPLIT_TRACKS")
			TRY_CATCH(regestry_access.SetDwordValue(L"COLLAPSE_MIDI", g_data.collapse_midi);, "Failed on setting COLLAPSE_MIDI")
			TRY_CATCH(regestry_access.SetDwordValue(L"APPLY_OFFSET_AFTER", g_data.collapse_midi);, "Failed on setting APPLY_OFFSET_AFTER")
			//TRY_CATCH(regestry_access.SetDwordValue(L"RSB_COMPRESS", check_autoupdates);, "Failed on setting RSB_COMPRESS")
			TRY_CATCH(regestry_access.SetDwordValue(L"DEFAULT_BOOL_SETTINGS", default_bool_settings);, "Failed on setting DEFAULT_BOOL_SETTINGS")
			TRY_CATCH(regestry_access.SetDwordValue(L"FONTSIZE_POST1P4", lfont_symbols_info::font_size); , "Failed on setting FONTSIZE_POST1P4")
			TRY_CATCH(regestry_access.SetDwordValue(L"FLOAT_FONTHTW_POST1P4", *(std::uint32_t*)(&font_height_to_width)); , "Failed on setting FLOAT_FONTHTW_POST1P4")
		}

		g_data.inplace_merge_flag = (((checkbox*)(*pptr)["INPLACE_MERGE"])->state);
		if (registry_opened)
			TRY_CATCH(regestry_access.SetDwordValue(L"AS_INPLACE_FLAG", g_data.inplace_merge_flag);, "Failed on setting AS_INPLACE_FLAG")

		((input_field*)(*pptr)["AS_FONT_NAME"])->put_into_source();
		std::wstring ws(default_font_name.begin(), default_font_name.end());
		if (registry_opened)
			TRY_CATCH(regestry_access.SetStringValue(L"COLLAPSEDFONTNAME_POST1P4", ws); , "Failed on setting COLLAPSEDFONTNAME_POST1P4")

		settings::regestry_access.Close();
	}

	void change_is_fonted_var()
	{
		is_fonted = !is_fonted;

		set_is_fonted_var(is_fonted);
		exit(0);
	}

	void apply_to_all()
	{
		on_set_apply();

		for (auto& settings : g_data.files)
		{
			settings.bool_settings = default_bool_settings;
			settings.inplace_merge_enabled = g_data.inplace_merge_flag && !g_data.channels_split;
			settings.channels_split = g_data.channels_split;
			settings.rsb_compression = g_data.rsb_compression;
			settings.collapse_midi = g_data.collapse_midi;
			settings.apply_offset_after = g_data.apply_offset_after;
		}
	}

	void apply_fs_wheel(double new_val)
	{
		lfont_symbols_info::font_size = new_val;
		lfont_symbols_info::initialise_font(default_font_name);
	}

	void apply_rel_wheel(double new_val)
	{
		font_height_to_width = new_val;
		lfont_symbols_info::initialise_font(default_font_name);
	}

	void feedback_open()
	{
		global_window_handler->enable_window("SUPPORT");
	}
}

std::pair<float, float> get_position_for_one_of(std::int32_t Position, std::int32_t Amount, float UnitSize, float HeightRel)
{
	std::pair<float, float> coords{ 0.f, 0.f };
	std::int32_t side_count = ceil(sqrt(Amount));

	coords.first = (0 - (Position % side_count) + ((side_count - 1) / 2.f)) * UnitSize;
	coords.second = (0 - (Position / side_count) + ((side_count - 1) / 2.f)) * UnitSize * HeightRel;

	return coords;
}

void on_start()
{
	if (g_data.files.empty())
		return;

	worker_singleton<struct merge>::instance().push([]()
	{
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
				std::uint32_t id)
			{
				std::string SID = "SMRP_C" + std::to_string(id);
				std::cout << SID << " Processing started" << std::endl;
				while (!global_mctm->is_smrp_complete())
				{
					global_mctm->with_currently_processed_item(id, [&](const auto& item)
					{
						vis_ref.set_smrp(item);
					});
					std::this_thread::sleep_for(std::chrono::milliseconds(66));
				}
				std::cout << SID << " Processing stopped" << std::endl;
			}, global_mctm, visualiser_ref, id).detach();
		}

		worker_singleton<struct merge_ri_stage>::instance().push([safc_data_pointer = &g_data, merge_preview_container]()
		{
			while (!global_mctm->is_smrp_complete())
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			global_mctm->start_ri_merge();

			std::cout << "SMRP: Out from sleep\n" << std::flush;
			for (int i = 0; i <= (int)global_mctm->currently_processed_count(); i++)
				merge_preview_container->delete_ui_element_by_name("SMRP_C" + std::to_string(i));

			merge_preview_container->safe_change_position_argumented(0, 0, 0);

			(*merge_preview_container)["IM"] =
				std::make_unique<bool_and_number_checker<decltype(global_mctm->inplace_merge_complete), decltype(global_mctm->inplace_track_count)>>
					(-100., 0., &system_white, &(global_mctm->inplace_merge_complete), &(global_mctm->inplace_track_count));
			(*merge_preview_container)["RM"] =
				std::make_unique<bool_and_number_checker<decltype(global_mctm->regular_merge_complete), decltype(global_mctm->regular_track_count)>>
					(100., 0., &system_white, &(global_mctm->regular_merge_complete), &(global_mctm->regular_track_count));
			
			worker_singleton<struct merge_ri_stage_cleanup>::instance().push([safc_data_pointer, merge_preview_container]()
			{
				while (!global_mctm->is_ri_merge_complete())
					std::this_thread::sleep_for(std::chrono::milliseconds(33));
				global_mctm->start_final_merge();

				std::cout << "RI: Out from sleep!\n";
				merge_preview_container->delete_ui_element_by_name("IM");
				merge_preview_container->delete_ui_element_by_name("RM");
				merge_preview_container->safe_change_position_argumented(0, 0, 0);

				(*merge_preview_container)["FM"] = 
					std::make_unique<bool_and_number_checker<decltype(global_mctm->complete), int>>
						(0., 0., &system_white, &(global_mctm->complete), nullptr);
			});
		});

		worker_singleton<struct merge_global_cleanup>::instance().push([start_timepoint, merge_preview_container]()
		{
			auto timer_ptr = (input_field*)(*merge_preview_container)["TIMER"];

			while (!global_mctm->complete)
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

			std::cout << "F: Out from sleep!!!\n";
			merge_preview_container->delete_ui_element_by_name("FM");

			global_window_handler->disable_window(global_window_handler->main_window_id);
			global_window_handler->main_window_id = "MAIN";
			//global_window_handler->disable_all_windows();
			global_window_handler->enable_window("MAIN");
			//global_mctm->ResetEverything();
		});
	});
}

void on_save_to()
{
	worker_singleton<struct save_file_dialog>::instance().push([](){
		g_data.save_path = save_open_file_dialog(L"Save final midi to...");
		size_t Pos = g_data.save_path.rfind(L".mid");
		if (Pos >= g_data.save_path.size() || Pos <= g_data.save_path.size() - 4)
			g_data.save_path += L".mid";
	});
}

void restore_reg_settings()
{
	bool reg_open = false;
	try
	{
		settings::regestry_access.Create(HKEY_CURRENT_USER, default_reg_path);
	}
	catch (...) { std::cout << "Exception thrown while creating registry key\n"; }
	try
	{
		settings::regestry_access.Open(HKEY_CURRENT_USER, default_reg_path);
		reg_open = true;
	}
	catch (...) { std::cout << "Exception thrown while opening RK\n"; }

	if (!reg_open)
		return;

	try
	{
		settings::background_id = settings::regestry_access.GetDwordValue(L"AS_BCKGID");
	}
	catch (...) { std::cout << "Exception thrown while restoring AS_BCKGID from registry\n"; }
	try
	{
		check_autoupdates = settings::regestry_access.GetDwordValue(L"AUTOUPDATECHECK");
	}
	catch (...) { std::cout << "Exception thrown while restoring AUTOUPDATECHECK from registry\n"; }
	try
	{
		g_data.channels_split = settings::regestry_access.GetDwordValue(L"SPLIT_TRACKS");
	}
	catch (...) { std::cout << "Exception thrown while restoring SPLIT_TRACKS from registry\n"; }
	try
	{
		g_data.collapse_midi = settings::regestry_access.GetDwordValue(L"COLLAPSE_MIDI");
	}
	catch (...) { std::cout << "Exception thrown while restoring COLLAPSE_MIDI from registry\n"; }
	try
	{
		g_data.apply_offset_after = settings::regestry_access.GetDwordValue(L"APPLY_OFFSET_AFTER");
	}
	catch (...) { std::cout << "Exception thrown while restoring APPLY_OFFSET_AFTER from registry\n"; }
	try
	{
		g_data.detected_threads = settings::regestry_access.GetDwordValue(L"AS_THREADS_COUNT");
	}
	catch (...) { std::cout << "Exception thrown while restoring AS_THREADS_COUNT from registry\n"; }
	try
	{
		default_bool_settings = settings::regestry_access.GetDwordValue(L"DEFAULT_BOOL_SETTINGS");
	}
	catch (...) { std::cout << "Exception thrown while restoring AS_INPLACE_FLAG from registry\n"; }
	try
	{
		g_data.inplace_merge_flag = settings::regestry_access.GetDwordValue(L"AS_INPLACE_FLAG");
	}
	catch (...) { std::cout << "Exception thrown while restoring INPLACE_MERGE from registry\n"; }
	try
	{
		std::wstring ws = settings::regestry_access.GetStringValue(L"COLLAPSEDFONTNAME_POST1P4");//COLLAPSEDFONTNAME
		default_font_name = std::string(ws.begin(), ws.end());
	}
	catch (...) { std::cout << "Exception thrown while restoring COLLAPSEDFONTNAME_POST1P4 from registry\n"; }
	try
	{
		lfont_symbols_info::font_size = settings::regestry_access.GetDwordValue(L"FONTSIZE_POST1P4");
	}
	catch (...) { std::cout << "Exception thrown while restoring FONTSIZE from registry\n"; }
	try
	{
		std::uint32_t B = settings::regestry_access.GetDwordValue(L"FLOAT_FONTHTW_POST1P4");
		font_height_to_width = *(float*)&B;
	}
	catch (...) { std::cout << "Exception thrown while restoring FLOAT_FONTHTW from registry\n"; }
	try
	{
		saved_midi_device_name = settings::regestry_access.GetStringValue(L"MIDI_DEVICE_NAME");
	}
	catch (...) { std::cout << "Exception thrown while restoring MIDI_DEVICE_NAME from registry\n"; }

	settings::regestry_access.Close();
}

void on_other_settings()
{
	global_window_handler->enable_window("OTHER_SETS");
}

void update_device_list();

void player_watch_func()
{
	global_window_handler->enable_window("SIMPLAYER");
	auto window = (*global_window_handler)["SIMPLAYER"];
	auto textbox = (text_box*)(*window)["TEXT"];
	auto seek_to_slider = (slider*)(*window)["SEEK_TO"];

	textbox->safe_string_replace("Opening and reading first track");

	// Populate the device list
	update_device_list();

	// Update pause button to reflect initial paused state
	auto pause_button = (button*)(*window)["PAUSE"];
	pause_button->safe_string_replace("\200");  // Play symbol (since it's paused)

	auto& info = player->get_info();
	while (true)
	{
		uint64_t scanned = info.scanned;
		uint64_t size = info.size;

		auto str = std::format("Read {} out of {} ~ {:3.2f}%", scanned, size, scanned * 100.f / size);
		textbox->safe_string_replace(str);

		if (info.open_complete)
			break;

		std::this_thread::sleep_for(std::chrono::milliseconds(20));
	}

	auto& state = player->get_state();

	bool was_playing = false;
	while (was_playing <= state.playing)
	{
		auto seconds = state.current_time_us / 1000000;
		auto parts_of_second = state.current_time_us % 1000000;

		auto position = float(state.current_tick) / player->get_info().ticks_length;
		
		auto str = std::format("{:0>2}:{:0>2}:{:0>2}", seconds / 60, seconds % 60, parts_of_second / 10000);
		textbox->safe_string_replace(str);
		
		if (!seek_to_slider->dragging)
			seek_to_slider->set_value(position, false);

		std::this_thread::sleep_for(std::chrono::milliseconds(20));
		bool is_playing = state.playing;
		if (is_playing)
			was_playing = true;
	}
}

void on_open_player()
{
	worker_singleton<struct player_thread>::instance().push([]()
	{
		auto midis_list = _WH_t<selectable_properted_list>("MAIN", "List");
		if (midis_list->selected_id.empty())
			return;

		auto& id = midis_list->selected_id.front();

		worker_singleton<struct player_watcher>::instance().push(player_watch_func);

		player->restore_device_by_name(saved_midi_device_name);
		player->simple_run(g_data[id].filename);
	});
}

void update_device_list()
{
	if (!player)
		return;

	auto device_list = _WH_t<selectable_properted_list>("SIMPLAYER", "DEVICE_LIST");

	// Clear existing items
	device_list->selectors_text.clear();
	device_list->selected_id.clear();

	// Get device names from player
	auto device_names = player->get_device_names();

	// Populate the list
	for (const auto& name : device_names)
		device_list->safe_push_back_new_string(name);

	// Select the current device
	size_t current = player->get_current_device();
	if (current < device_names.size())
		device_list->selected_id.push_back(static_cast<uint32_t>(current));

	device_list->safe_update_lines();
}

void on_device_select(int device_id)
{
	worker_singleton<struct midi_out_selct>::instance().push([device_id]()
	{
		player->set_device(device_id);

		auto device_list = _WH_t<selectable_properted_list>("SIMPLAYER", "DEVICE_LIST");

		// Clear previous selection and select the new device
		device_list->selected_id.clear();
		device_list->selected_id.push_back(static_cast<uint32_t>(device_id));

		// Save the device name to registry
		auto device_names = player->get_device_names();
		if (device_id >= 0 && device_id < device_names.size())
		{
			std::string device_name = device_names[device_id];
			std::wstring wdevice_name(device_name.begin(), device_name.end());
			saved_midi_device_name = wdevice_name;

			try
			{
				settings::regestry_access.Open(HKEY_CURRENT_USER, default_reg_path);
				settings::regestry_access.SetStringValue(L"MIDI_DEVICE_NAME", wdevice_name);
				settings::regestry_access.Close();
			}
			catch (...)
			{
				std::cout << "Exception thrown while saving MIDI_DEVICE_NAME to registry\n";
			}
		}
	});
}

void on_player_pause_toggle()
{
	player->toggle_pause();

	auto window = (*global_window_handler)["SIMPLAYER"];
	auto pause = (button*)(*window)["PAUSE"];

	if (player->is_paused())
		pause->safe_string_replace("\200");
	else
		pause->safe_string_replace("\202");
}

void on_player_stop()
{
	player->stop();

	worker_singleton<struct player_thread>::instance().push([]()
	{
		auto window = (*global_window_handler)["SIMPLAYER"];
		auto pause = (button*)(*window)["PAUSE"];
		auto textbox = (text_box*)(*window)["TEXT"];

		if (player->is_paused())
			pause->safe_string_replace("\200");
		else
			pause->safe_string_replace("\202");

		textbox->safe_string_replace("Playback was reset, please restart the player");
	});
}

void on_view_length_change(float value)
{
	auto window = (*global_window_handler)["SIMPLAYER"];
	auto player_view = (player_viewer*)(*window)["VIEW"];
	
	player_view->data->scroll_window_us = std::pow(2, value);
}

void on_unbuffered_switch()
{
	auto window = (*global_window_handler)["SIMPLAYER"];
	auto player_view = (player_viewer*)(*window)["VIEW"];
	auto buffering_switch = (button*)(*window)["BUFFERING_SWITCH"];

	player_view->data->enable_simulated_lag ^= true;

	buffering_switch->safe_string_replace(player_view->data->enable_simulated_lag ? "Simulate lag" : "Allow unbuffered");
}

void on_playback_seek_to(float value)
{
	if (!player || !player->is_playing())
		return;

	player->seek_to(value);
}

bool simplayer_maximised = false;

struct simplayer_saved_state {
	float window_x, window_y, window_width, window_height;
	float text_x, text_y;
	float pause_x, pause_y;
	float stop_x, stop_y;
	float buf_switch_x, buf_switch_y;
	float max_x, max_y;
	float vls_x, vls_y;
	float seek_x, seek_y, seek_track_length;
	float devlist_cx, devlist_y;
	float view_x, view_y, view_width, view_height;
	std::string previous_main_window_id;
} saved_simplayer_state;

void apply_simplayer_maximised_layout()
{
	float half_w = internal_range * (wind_x / window_base_width);
	float half_h = internal_range * (wind_y / window_base_height);
	float full_width = 2.0f * half_w;
	float full_height = 2.0f * half_h + moveable_window::window_header_size;

	auto window = (*global_window_handler)["SIMPLAYER"];
	auto player_view = (player_viewer*)(*window)["VIEW"];
	auto text = (text_box*)(*window)["TEXT"];
	auto pause_btn = (button*)(*window)["PAUSE"];
	auto stop_btn = (button*)(*window)["STOP"];
	auto vls = (slider*)(*window)["VIEW_LEN_SLIDER"];
	auto buf_sw = (button*)(*window)["BUFFERING_SWITCH"];
	auto seek_to = (slider*)(*window)["SEEK_TO"];
	auto max_btn = (button*)(*window)["MAXIMISE"];
	auto dev_list = (selectable_properted_list*)(*window)["DEVICE_LIST"];

	// move window so top-left aligns with viewport top-left
	float dx = (-half_w) - window->x_window_pos;
	float dy = (half_h) - window->y_window_pos + moveable_window::window_header_size;
	window->safe_move(dx, dy);

	// Resize window frame
	window->not_safe_resize(full_height, full_width);
	auto fui = (moveable_fui_window*)window;
	fui->safe_window_rename(window->window_name->current_text, false);

	// Position controls near the top
	float row1_y = half_h - 10;
	float row2_y = row1_y - 25;

	pause_btn->safe_change_position(-half_w + 10, row1_y);
	stop_btn->safe_change_position(-half_w + 25, row1_y);
	vls->safe_change_position(-half_w + 75, row1_y);
	text->safe_change_position(0, row1_y - 20);
	buf_sw->safe_change_position(half_w - 50, row1_y);

	max_btn->safe_change_position(half_w - 30, row2_y + 10);
	dev_list->safe_change_position(-half_w + 60, row2_y + 15);

	// Stretch seek slider
	float seek_y = row2_y - 15;
	seek_to->safe_change_position(0, seek_y - 10);
	seek_to->track_length = full_width - 30;

	// Compute player_viewer geometry
	float width_factor_change = full_width / player_view->data->width;
	//float keyboard_height = 40.0f;
	float notes_top = seek_y - 15;
	float notes_bottom = -half_h + player_view->data->last_keyboard_height * width_factor_change;
	float notes_height = notes_top - notes_bottom;
	float view_center_y = (notes_top + notes_bottom) * 0.5f;

	player_view->rescale_and_reposition(0, view_center_y, full_width, notes_height);
}

void switch_maximise()
{
	auto window = (*global_window_handler)["SIMPLAYER"];
	auto player_view = (player_viewer*)(*window)["VIEW"];
	auto text = (text_box*)(*window)["TEXT"];
	auto pause_btn = (button*)(*window)["PAUSE"];
	auto stop_btn = (button*)(*window)["STOP"];
	auto vls = (slider*)(*window)["VIEW_LEN_SLIDER"];
	auto buf_sw = (button*)(*window)["BUFFERING_SWITCH"];
	auto seek_to = (slider*)(*window)["SEEK_TO"];
	auto max_btn = (button*)(*window)["MAXIMISE"];
	auto dev_list = (selectable_properted_list*)(*window)["DEVICE_LIST"];

	if (!simplayer_maximised)
	{
		// Save current state
		auto& state = saved_simplayer_state;
		state.window_x = window->x_window_pos;
		state.window_y = window->y_window_pos;
		state.window_width = window->width;
		state.window_height = window->height;
		state.text_x = text->x_pos;
		state.text_y = text->y_pos;
		state.pause_x = pause_btn->x_pos;
		state.pause_y = pause_btn->y_pos;
		state.stop_x = stop_btn->x_pos;
		state.stop_y = stop_btn->y_pos;
		state.vls_x = vls->x_pos;
		state.vls_y = vls->y_pos;
		state.buf_switch_x = buf_sw->x_pos;
		state.buf_switch_y = buf_sw->y_pos;
		state.seek_x = seek_to->x_pos;
		state.seek_y = seek_to->y_pos;
		state.seek_track_length = seek_to->track_length;
		state.max_x = max_btn->x_pos;
		state.max_y = max_btn->y_pos;
		state.devlist_cx = dev_list->header_cx_pos;
		state.devlist_y = dev_list->header_y_pos;
		state.view_x = player_view->xpos;
		state.view_y = player_view->ypos;
		state.view_width = player_view->data->width;
		state.view_height = player_view->data->height;
		state.previous_main_window_id = global_window_handler->main_window_id;

		// Apply maximized layout
		apply_simplayer_maximised_layout();

		// Make SIMPLAYER the sole window
		global_window_handler->main_window_id = "SIMPLAYER";
		global_window_handler->disable_all_windows();

		simplayer_maximised = true;
		max_btn->safe_string_replace("Restore");
	}
	else
	{
		auto& state = saved_simplayer_state;

		// move window back to original position
		float dx = state.window_x - window->x_window_pos;
		float dy = state.window_y - window->y_window_pos;
		window->safe_move(dx, dy);
		window->not_safe_resize(state.window_height, state.window_width);
		auto fui = (moveable_fui_window*)window;
		fui->safe_window_rename(window->window_name->current_text, false);

		// Restore each child to saved position
		text->safe_change_position(state.text_x, state.text_y);
		pause_btn->safe_change_position(state.pause_x, state.pause_y);
		stop_btn->safe_change_position(state.stop_x, state.stop_y);
		vls->safe_change_position(state.vls_x, state.vls_y);
		buf_sw->safe_change_position(state.buf_switch_x, state.buf_switch_y);
		seek_to->safe_change_position(state.seek_x, state.seek_y);
		seek_to->track_length = state.seek_track_length;
		max_btn->safe_change_position(state.max_x, state.max_y);
		dev_list->safe_change_position(state.devlist_cx, state.devlist_y);

		// Restore player_viewer
		player_view->rescale_and_reposition(state.view_x, state.view_y, state.view_width, state.view_height);

		// Restore window management
		global_window_handler->main_window_id = state.previous_main_window_id;
		global_window_handler->disable_all_windows();
		global_window_handler->enable_window("SIMPLAYER");

		simplayer_maximised = false;
		max_btn->safe_string_replace("Maximise");
	}
}

void init()
{
	lfont_symbols_info::initialise_font(default_font_name, true);

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

	global_window_handler = std::make_shared<windows_handler>();

	const auto& [maj, min, ver, build] = g_version_tuple;

	constexpr unsigned BACKGROUND = 0x070E16AF;
	constexpr unsigned BACKGROUND_OPQ = 0x070E16DF;
	constexpr unsigned BORDER = 0xFFFFFF7F;
	constexpr unsigned HEADER = 0x285685CF;
	
	/*selectable_properted_list* SPL = new selectable_properted_list(bs_list_black_small, NULL, props_and_sets::open_file_properties, -50, 172, 300, 12, 65, 30);
	moveable_window* window = new MoveableResizeableWindow(Mainwindow_name.str(), system_white, -200, 200, 400, 400, 0x3F3F3FAF, 0x7F7F7F7F, 0, [SPL](float dH, float dW, float NewHeight, float NewWidth) {
		constexpr float TopMargin = 200 - 172;
		constexpr float BottomMargin = 12;
		float SPLNewHight = (NewHeight - TopMargin - BottomMargin);
		float SPLNewWidth = SPL->width + dW;
		SPL->safe_resize(SPLNewHight, SPLNewWidth);
		});
	((MoveableResizeableWindow*)window)->assign_min_dimensions(300, 300);
	((MoveableResizeableWindow*)window)->assign_pinned_activities({
		"ADD_Butt", "REM_Butt", "REM_ALL_Butt", "GLOBAL_PPQN_Butt", "GLOBAL_OFFSET_Butt", "GLOBAL_TEMPO_Butt", "DELETE_ALL_VM", "DELETE_ALL_CAT", "DELETE_ALL_PITCHES",
		"DELETE_ALL_MODULES", "settings", "SAVE_AS", "START"
		}, MoveableResizeableWindow::PinSide::right);
	((MoveableResizeableWindow*)window)->assign_pinned_activities({ "settings", "SAVE_AS", "START" }, MoveableResizeableWindow::PinSide::bottom);*/
	moveable_window* window = new moveable_fui_window(std::format("SAFC v{}.{}.{}.{}", maj, min, ver, build), system_white, -200, 197.5f, 400, 397.5f, 300, 2.5f, 100, 100, 5, BACKGROUND, HEADER, BORDER);

	button* button_buff;
	(*window)["List"] = new selectable_properted_list(bs_list_black_small, NULL, props_and_sets::open_file_properties, -45, 172, 295, 12, 64, 30);;

	(*window)["ADD_Butt"] = new button("Add MIDIs", system_white, on_add, 150, 167.5, 75, 12, 1, 0x00003FAF, 0xFFFFFFFF, 0x00003FFF, 0xFFFFFFFF, 0xF7F7F7FF, nullptr, " ");
	(*window)["REM_Butt"] = new button("Remove selected", system_white, on_rem, 150, 155, 75, 12, 1, 0x3F0000AF, 0xFFFFFFFF, 0x3F0000FF, 0xFFFFFFFF, 0xF7F7F7FF, nullptr, " ");
	(*window)["REM_ALL_Butt"] = new button("Remove all", system_white, on_rem_all, 150, 142.5, 75, 12, 1, 0xAF0000AF, 0xFFFFFFFF, 0xAF0000AF, 0xFFFFFFFF, 0xF7F7F7FF, &system_white, "May cause lag");

	(*window)["OPEN_SIMPLAYER"] = new button("Open Player", system_black, on_open_player, 150, 117.5, 75, 12, 1, 0xFFFFFFAF, 0x0F0F0FFF, 0xFFFFFFFF, 0x000000FF, 0xFFFFFFFF, nullptr, " ");

	(*window)["GLOBAL_PPQN_Butt"] = new button("Global PPQN", system_white, on_global_ppqn, 150, 92.5, 75, 12, 1, 0xFF3F00AF, 0xFFFFFFFF, 0xFF3F00AF, 0xFFFFFFFF, 0xF7F7F7FF, nullptr, " ");
	(*window)["GLOBAL_OFFSET_Butt"] = new button("Global offset", system_white, on_global_offset, 150, 80, 75, 12, 1, 0xFF7F00AF, 0xFFFFFFFF, 0xFF7F00FF, 0xFFFFFFFF, 0xF7F7F7FF, nullptr, " ");
	(*window)["GLOBAL_TEMPO_Butt"] = new button("Global tempo", system_white, on_global_tempo, 150, 67.5, 75, 12, 1, 0xFFAF00AF, 0xFFFFFFFF, 0xFFAF00AF, 0xFFFFFFFF, 0xF7F7F7FF, nullptr, " ");

	(*window)["DELETE_ALL_VM"] = new button("Remove vol. maps", system_white, on_rem_vol_maps, 150, 42.5, 75, 12, 1,
		0x7F7F7FAF, 0xFFFFFFFF, 0x7F7F7FAF, 0xFFFFFFFF, 0xF7F7F7FF, nullptr, " ");//0xFF007FAF
	(*window)["DELETE_ALL_CAT"] = new button("Remove C&Ts", system_white, on_rem_cats, 150, 30, 75, 12, 1,
		0x7F7F7FAF, 0xFFFFFFFF, 0x7F7F7FAF, 0xFFFFFFFF, 0xF7F7F7FF, nullptr, " ");
	(*window)["DELETE_ALL_MODULES"] = new button("Remove modules", system_white, on_rem_all_modules, 150, 17.5, 75, 12, 1,
		0x7F7F7FAF, 0xFFFFFFFF, 0x7F7F7FAF, 0xFFFFFFFF, 0xF7F7F7FF, nullptr, " ");

	(*window)["APP_SETTINGS"] = new button("Settings...", system_white, settings::on_settings, 150, -140, 75, 12, 1,
		0x5F5F5FAF, 0xFFFFFFFF, 0x5F5F5FAF, 0xFFFFFFFF, 0xF7F7F7FF, nullptr, " ");
	(*window)["SAVE_AS"] = new button("Save as...", system_white, on_save_to, 150, -152.5, 75, 12, 1,
		0x3FAF00AF, 0xFFFFFFFF, 0x3FAF00AF, 0xFFFFFFFF, 0xF7F7F7FF, nullptr, " ");
	(*window)["START"] = button_buff = new button("Start merging", system_white, on_start, 150, -177.5, 75, 12, 1,
		0x000000AF, 0xFFFFFFFF, 0x000000AF, 0xFFFFFFFF, 0xF7F7F7FF, nullptr, " ");//177.5

	(*global_window_handler)["MAIN"] = window;

	window = new moveable_fui_window("Props. and sets.", system_white, -100, 100, 200, 225, 100, 2.5f, 75, 50, 3, BACKGROUND_OPQ, HEADER, BORDER);
	(*window)["FileName"] = new text_box("_", system_white, 0, 88.5 - moveable_window::window_header_size, 6, 200 - 1.5 * moveable_window::window_header_size, 7.5, 0, 0, 0, _Align::left, text_box::VerticalOverflow::cut);
	(*window)["PPQN"] = new input_field(" ", -90 + moveable_window::window_header_size, 75 - moveable_window::window_header_size, 10, 25, system_white, props_and_sets::PPQN, 0x007FFFFF, &system_white, "PPQN is lesser than 65536.", 5, _Align::center, _Align::left, input_field::Type::NaturalNumbers);
	(*window)["TEMPO"] = new input_field(" ", -50 + moveable_window::window_header_size, 75 - moveable_window::window_header_size, 10, 45, system_white, props_and_sets::TEMPO, 0x007FFFFF, &system_white, "Specific tempo override field", 8, _Align::center, _Align::left, input_field::Type::FP_PositiveNumbers);
	(*window)["OFFSET"] = new input_field(" ", 20 + moveable_window::window_header_size, 75 - moveable_window::window_header_size, 10, 55, system_white, props_and_sets::OFFSET, 0x007FFFFF, &system_white, "Offset from begining in ticks", 10, _Align::center, _Align::right, input_field::Type::WholeNumbers);

	(*window)["APPLY_OFFSET_AFTER"] = new checkbox(12.5 - moveable_window::window_header_size, 75 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF7F00AF, 0x7FFF00AF, 1, 0, &system_white, _Align::center, "Apply offset after PPQ change");

	(*window)["BOOL_REM_TRCKS"] = new checkbox(-97.5 + moveable_window::window_header_size, 55 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 1, &system_white, _Align::left, "Remove empty tracks");
	(*window)["BOOL_REM_REM"] = new checkbox(-82.5 + moveable_window::window_header_size, 55 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 1, &system_white, _Align::left, "Remove merge \"remnants\"");
	(*window)["OTHER_CHECKBOXES"] = new button("Other settings", system_white, on_other_settings, -37.5 + moveable_window::window_header_size, 55 - moveable_window::window_header_size, 70, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F003F, 0xFF7F00FF, &system_white, "Other MIDI processing settings");

	(*window)["SPLIT_TRACKS"] = new checkbox(7.5 + moveable_window::window_header_size, 55 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF3F007F, 0x3FFF007F, 1, 0, &system_white, _Align::center, "Multichannel split");
	(*window)["RSB_COMPRESS"] = new checkbox(22.5 + moveable_window::window_header_size, 55 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF3F007F, 0x3FFF007F, 1, 0, &system_white, _Align::center, "Enable RSB compression");

	(*window)["COLLAPSE_MIDI"] = new checkbox(97.5 - moveable_window::window_header_size, 35 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF7F00AF, 0x7FFF00AF, 1, 0, &system_white, _Align::right, "Collapse all tracks of a MIDI into one");

	(*window)["BOOL_APPLY_TO_ALL_MIDIS"] = button_buff = new button("A2A", system_white, props_and_sets::on_apply_bs2a, 80 - moveable_window::window_header_size, 55 - moveable_window::window_header_size, 15, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F003F, 0xFF7F00FF, &system_white, "Sets \"bool settings\" to all midis");
	button_buff->tip->safe_change_position_argumented(_Align::right, 87.5 - moveable_window::window_header_size, button_buff->tip->cy_pos);

	(*window)["INPLACE_MERGE"] = new checkbox(97.5 - moveable_window::window_header_size, 55 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF3F007F, 0x3FFF007F, 1, 0, &system_white, _Align::right, "Enables/Disables inplace merge");

	(*window)["GROUPID"] = new input_field(" ", 92.5 - moveable_window::window_header_size, 75 - moveable_window::window_header_size, 10, 20, system_white, props_and_sets::PPQN, 0x007FFFFF, &system_white, "Group id...", 2, _Align::center, _Align::right, input_field::Type::NaturalNumbers);

	(*window)["MIDIINFO"] = button_buff = new button("Collect info", system_white, props_and_sets::initialize_collecting, 20, 15 - moveable_window::window_header_size, 65, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F003F, 0xFF7F00FF, &system_white, "Collects additional info about the midi");
	button_buff->tip->safe_move(-20, 0);

	(*window)["APPLY"] = new button("Apply", system_white, props_and_sets::on_apply_settings, 87.5 - moveable_window::window_header_size, 15 - moveable_window::window_header_size, 30, 10, 1, 0x7FAFFF3F, 0xFFFFFFFF, 0xFFAF7FFF, 0xFFAF7F3F, 0xFFAF7FFF, nullptr, " ");

	(*window)["CUT_AND_TRANSPOSE"] = (button_buff = new button("Cut & Transpose...", system_white, props_and_sets::cut_and_transpose::on_cat, 45 - moveable_window::window_header_size, 35 - moveable_window::window_header_size, 85, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F003F, 0xFF7F00FF, &system_white, "Cut and Transpose tool"));
	button_buff->tip->safe_change_position_argumented(_Align::right, 100 - moveable_window::window_header_size, button_buff->tip->cy_pos);
	(*window)["PITCH_MAP"] = (button_buff = new button("Pitch map ...", system_white, props_and_sets::on_pitch_map, -37.5 - moveable_window::window_header_size, 15 - moveable_window::window_header_size, 70, 10, 1, 0x7F7F7F3F, 0x7F7F7FFF, 0xFFFFFFFF, 0xFFFFFF3F, 0xFFFFFFFF, &system_white, "Allows to transform pitches"));
	button_buff->tip->safe_change_position_argumented(_Align::right, 100 - moveable_window::window_header_size, button_buff->tip->cy_pos);
	(*window)["VOLUME_MAP"] = (button_buff = new button("Volume map ...", system_white, props_and_sets::volume_map::on_vol_map, -37.5 - moveable_window::window_header_size, 35 - moveable_window::window_header_size, 70, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F003F, 0xFF7F00FF, &system_white, "Allows to transform volumes of notes"));
	button_buff->tip->safe_change_position_argumented(_Align::right, 100 - moveable_window::window_header_size, button_buff->tip->cy_pos);

	(*window)["SELECT_START"] = new input_field(" ", -37.5 - moveable_window::window_header_size, -5 - moveable_window::window_header_size, 10, 70, system_white, nullptr, 0x007FFFFF, &system_white, "Selection start", 13, _Align::center, _Align::right, input_field::Type::NaturalNumbers);
	(*window)["SELECT_LENGTH"] = new input_field(" ", 37.5 - moveable_window::window_header_size, -5 - moveable_window::window_header_size, 10, 70, system_white, nullptr, 0x007FFFFF, &system_white, "Selection length", 14, _Align::center, _Align::right, input_field::Type::WholeNumbers);

	(*window)["CONSTANT_PROPS"] = new text_box("_Props text example_", system_white, 0, -55 - moveable_window::window_header_size, 80 - moveable_window::window_header_size, 200 - 1.5 * moveable_window::window_header_size, 7.5, 0, 0, 1);

	(*global_window_handler)["SMPAS"] = window;//Selected midi properties and settings

	window = new moveable_fui_window("Other settings.", system_white, -75, 35, 150, 65 + moveable_window::window_header_size, 100, 2.5f, 17.5f, 17.5f, 3, BACKGROUND_OPQ, HEADER, BORDER);

	checkbox* checkbox_buff = nullptr;

	(*window)["BOOL_PIANO_ONLY"] = new checkbox(-65, 25 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 1, &system_white, _Align::left, "All instruments to piano");
	(*window)["BOOL_IGN_TEMPO"] = new checkbox(-50, 25 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 0, &system_white, _Align::left, "Remove tempo events");
	(*window)["BOOL_IGN_PITCH"] = new checkbox(-35, 25 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 0, &system_white, _Align::left, "Remove pitch events");
	(*window)["BOOL_IGN_NOTES"] = new checkbox(-20, 25 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 0, &system_white, _Align::center, "Remove notes");
	(*window)["BOOL_IGN_ALL_EX_TPS"] = new checkbox(-5, 25 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 0, &system_white, _Align::center, "Ignore other events");

	(*window)["LEGACY_META_RSB_BEHAVIOR"] = checkbox_buff = new checkbox(10, 25 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF3F007F, 0x3FFF007F, 1, false, &system_white, _Align::center, "En. legacy RSB/Meta behavior");
	checkbox_buff->tip->safe_change_position_argumented(_Align::left, -70, 15 - moveable_window::window_header_size);
	(*window)["ALLOW_SYSEX"] = new checkbox(25, 25 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF3F007F, 0x3FFF007F, 1, 0, &system_white, _Align::right, "Allow sysex events");

	(*window)["IMP_FLT_ENABLE"] = new checkbox(65, 25 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF7F1F7F, 0x5FFF007F, 1, 0, &system_white, _Align::right, "Enable important event filter");

	(*window)["0VERLAY"] = new text_box("", system_white, -10, 2.5f - moveable_window::window_header_size, 23, 120, 0, 0xFFFFFF1F, 0x007FFF7F, 1);

	(*window)["IMP_FLT_NOTES"] = new checkbox(-60, 5 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 1, &system_white, _Align::left, "Important filter: piano");
	(*window)["IMP_FLT_TEMPO"] = new checkbox(-45, 5 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 0, &system_white, _Align::left, "Important filter: tempo");
	(*window)["IMP_FLT_PITCH"] = new checkbox(-30, 5 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 0, &system_white, _Align::left, "Important filter: pitch");
	(*window)["IMP_FLT_PROGC"] = checkbox_buff = new checkbox(-15, 5 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 0, &system_white, _Align::center, "Important filter: program change");
	checkbox_buff->tip->safe_change_position_argumented(_Align::left, -70, -5 - moveable_window::window_header_size);
	(*window)["IMP_FLT_OTHER"] = new checkbox(0, 5 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 0, &system_white, _Align::center, "Important filter: other");

	(*window)["ENABLE_ZERO_VELOCITY"] = new checkbox(60, 5 - moveable_window::window_header_size, 10, 0x007FFFFF, 0x00001F3F, 0x00FF00FF, 1, 0, &system_white, _Align::right, "\"Enable\" zero velocity notes");

	(*window)["APPLY"] = new button("Apply", system_white, props_and_sets::on_apply_settings, 70 - moveable_window::window_header_size, -20 - moveable_window::window_header_size, 30, 10, 1, 0x3F7FFF3F, 0xFFFFFFEF, 0x7FFF3FFF, 0x7FFF3F1F, 0x7FFF3FFF, nullptr, " ");

	(*global_window_handler)["OTHER_SETS"] = window; // Other settings

	window = new moveable_fui_window("Cut and Transpose.", system_white, -200, 50, 400, 100, 300, 2.5f, 15, 15, 2.5f, BACKGROUND_OPQ, HEADER, BORDER);
	(*window)["CAT_ITSELF"] = new cut_and_transpose_piano(0, 20 - moveable_window::window_header_size, 1, 10, nullptr);
	(*window)["CAT_SET_DEFAULT"] = new button("reset", system_white, props_and_sets::cut_and_transpose::on_reset, -150, -10 - moveable_window::window_header_size, 40, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F003F, 0xFF7F00FF, nullptr, " ");
	(*window)["CAT_+128"] = new button("0-127 -> 128-255", system_white, props_and_sets::cut_and_transpose::on_0_127_to_128_255, -85, -10 - moveable_window::window_header_size, 80, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F003F, 0xFF7F00FF, nullptr, " ");
	(*window)["CAT_CDT128"] = new button("Cut down to 128", system_white, props_and_sets::cut_and_transpose::on_cdt128, 0, -10 - moveable_window::window_header_size, 80, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F003F, 0xFF7F00FF, nullptr, " ");
	(*window)["CAT_COPY"] = new button("Copy", system_white, props_and_sets::cut_and_transpose::on_copy, 65, -10 - moveable_window::window_header_size, 40, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F003F, 0xFF7F00FF, nullptr, " ");
	(*window)["CAT_PASTE"] = new button("Paste", system_white, props_and_sets::cut_and_transpose::on_paste, 110, -10 - moveable_window::window_header_size, 40, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F003F, 0xFF7F00FF, nullptr, " ");
	(*window)["CAT_DELETE"] = new button("Delete", system_white, props_and_sets::cut_and_transpose::on_delete, 155, -10 - moveable_window::window_header_size, 40, 10, 1, 0xFF00FF3F, 0xFF00FFFF, 0xFFFFFFFF, 0xFF003F3F, 0xFF003FFF, nullptr, " ");

	(*global_window_handler)["CAT"] = window;

	window = new moveable_fui_window("Volume map.", system_white, -150, 150, 300, 350, 200, 2.5f, 100, 100, 2.5f, BACKGROUND_OPQ, HEADER, BORDER);
	(*window)["VM_PLC"] = new volume_graph(0, 0 - moveable_window::window_header_size, 300 - moveable_window::window_header_size * 2, 300 - moveable_window::window_header_size * 2, std::make_shared<polyline_converter<std::uint8_t, std::uint8_t>>());///todo: interface
	(*window)["VM_SSBDIIF"] = button_buff = new button("Shape alike x^y", system_white, props_and_sets::volume_map::on_degree_shape, -110 + moveable_window::window_header_size, -150 - moveable_window::window_header_size, 80, 10, 1, 0xFFFFFF3F, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFAFFF3F, 0xFFAFFFFF, &system_white, "Where y is from frame bellow");///Set shape by degree in input field;
	button_buff->tip->safe_change_position_argumented(_Align::left, -150 + moveable_window::window_header_size, -160 - moveable_window::window_header_size);
	(*window)["VM_DEGREE"] = new input_field("1", -140 + moveable_window::window_header_size, -170 - moveable_window::window_header_size, 10, 20, system_white, nullptr, 0x007FFFFF, nullptr, " ", 4, _Align::center, _Align::center, input_field::Type::FP_PositiveNumbers);
	(*window)["VM_ERASE"] = button_buff = new button("Erase points", system_white, props_and_sets::volume_map::on_erase, -35 + moveable_window::window_header_size, -150 - moveable_window::window_header_size, 60, 10, 1, 0xFFFFFF3F, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFAFFF3F, 0xFFAFFFFF, nullptr, "_");
	(*window)["VM_DELETE"] = new button("Delete map", system_white, props_and_sets::volume_map::on_delete, 30 + moveable_window::window_header_size, -150 - moveable_window::window_header_size, 60, 10, 1, 0xFFAFAF3F, 0xFFAFAFFF, 0xFFEFEFFF, 0xFF7F3F7F, 0xFF1F1FFF, nullptr, "_");
	(*window)["VM_SIMP"] = button_buff = new button("Simplify map", system_white, props_and_sets::volume_map::on_simplify, -70 - moveable_window::window_header_size, -170 - moveable_window::window_header_size, 60, 10, 1, 0xFFFFFF3F, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFAFFF3F, 0xFFAFFFFF, &system_white, "Reduces amount of \"repeating\" points");
	button_buff->tip->safe_change_position_argumented(_Align::left, -100 - moveable_window::window_header_size, -160 - moveable_window::window_header_size);
	(*window)["VM_TRACE"] = button_buff = new button("Trace map", system_white, props_and_sets::volume_map::on_trace, -35 + moveable_window::window_header_size, -170 - moveable_window::window_header_size, 60, 10, 1, 0xFFFFFF3F, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFAFAF3F, 0xFFAFAFFF, &system_white, "Puts every point onto map");
	button_buff->tip->safe_change_position_argumented(_Align::left, -65 + moveable_window::window_header_size, -160 - moveable_window::window_header_size);
	(*window)["VM_SETMODE"] = button_buff = new button("Single", system_white, props_and_sets::volume_map::on_set_mode_change, 30 + moveable_window::window_header_size, -170 - moveable_window::window_header_size, 40, 10, 1, 0xFFFFFF3F, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFAFAF3F, 0xFFAFAFFF, nullptr, "_");

	(*global_window_handler)["VM"] = window;

	window = new moveable_fui_window("App settings", system_white, -100, 110, 200, 230, 125, 2.5f, 50, 50, 2.5f, BACKGROUND_OPQ, HEADER, BORDER);

	(*window)["AS_BCKGID"] = new input_field(std::to_string(settings::background_id), -35, 55 - moveable_window::window_header_size, 10, 30, system_white, nullptr, 0x007FFFFF, &system_white, "Background id", 2, _Align::center, _Align::right, input_field::Type::NaturalNumbers);

	(*window)["AS_GLOBALSETTINGS"] = new text_box("Global settings for new MIDIs", system_white, 0, 85 - moveable_window::window_header_size, 50, 200, 12, 0x007FFF1F, 0x007FFF7F, 1, _Align::center);
	(*window)["AS_APPLY"] = button_buff = new button("Apply", system_white, settings::on_set_apply, 85 - moveable_window::window_header_size, -87.5 - moveable_window::window_header_size, 40, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, nullptr, "_");
	(*window)["AS_EN_FONT"] = button_buff = new button((is_fonted) ? "Disable fonts" : "Enable fonts", system_white, settings::change_is_fonted_var, 72.5 - moveable_window::window_header_size, -67.5 - moveable_window::window_header_size, 65, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, &system_white, " ");

	auto angle_input = new input_field(std::to_string(dumb_rotation_angle), -87.5 + moveable_window::window_header_size, 55 - moveable_window::window_header_size, 10, 30, system_white, nullptr, 0x007FFFFF, &system_white, "Rotation angle", 6, _Align::center, _Align::left, input_field::Type::FP_Any);
	angle_input->disable(); // hide // legacy
	(*window)["AS_ROT_ANGLE"] = angle_input;
	(*window)["AS_FONT_SIZE"] = new wheel_variable_changer(settings::apply_fs_wheel, -37.5, -82.5, lfont_symbols_info::font_size, 1, system_white, "Font size", "Delta", wheel_variable_changer::Type::addictable);
	(*window)["AS_FONT_P"] = new wheel_variable_changer(settings::apply_rel_wheel, -37.5, -22.5, font_height_to_width, 0.01, system_white, "Font rel.", "Delta", wheel_variable_changer::Type::addictable);
	(*window)["AS_FONT_NAME"] = new input_field(default_font_name, 52.5 - moveable_window::window_header_size, 55 - moveable_window::window_header_size, 10, 100, legacy_white, &default_font_name, 0x007FFFFF, &system_white, "Font name", 32, _Align::center, _Align::left, input_field::Type::text);

	(*window)["BOOL_REM_TRCKS"] = new checkbox(-97.5 + moveable_window::window_header_size, 95 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 1, &system_white, _Align::left, "Remove empty tracks");
	(*window)["BOOL_REM_REM"] = new checkbox(-82.5 + moveable_window::window_header_size, 95 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 1, &system_white, _Align::left, "Remove merge \"remnants\"");
	(*window)["BOOL_PIANO_ONLY"] = new checkbox(-67.5 + moveable_window::window_header_size, 95 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 1, &system_white, _Align::left, "All instuments to piano");
	(*window)["BOOL_IGN_TEMPO"] = new checkbox(-52.5 + moveable_window::window_header_size, 95 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 0, &system_white, _Align::left, "Ignore tempo events");
	(*window)["BOOL_IGN_PITCH"] = new checkbox(-37.5 + moveable_window::window_header_size, 95 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 0, &system_white, _Align::center, "Ignore pitch bending events");
	(*window)["BOOL_IGN_NOTES"] = new checkbox(-22.5 + moveable_window::window_header_size, 95 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 0, &system_white, _Align::center, "Ignore note events");
	(*window)["BOOL_IGN_ALL_EX_TPS"] = new checkbox(-7.5 + moveable_window::window_header_size, 95 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 0, &system_white, _Align::center, "Ignore everything except specified");
	(*window)["SPLIT_TRACKS"] = new checkbox(7.5 + moveable_window::window_header_size, 95 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF3F007F, 0x3FFF007F, 1, 0, &system_white, _Align::center, "Multichannel split");
	(*window)["RSB_COMPRESS"] = new checkbox(22.5 + moveable_window::window_header_size, 95 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF3F007F, 0x3FFF007F, 1, 0, &system_white, _Align::center, "Enable RSB compression");

	(*window)["ALLOW_SYSEX"] = new checkbox(-97.5 + moveable_window::window_header_size, 75 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF3F007F, 0x3FFF007F, 1, 0, &system_white, _Align::left, "Allow sysex events");

	(*window)["BOOL_APPLY_TO_ALL_MIDIS"] = button_buff = new button("A2A", system_white, settings::apply_to_all, 80 - moveable_window::window_header_size, 95 - moveable_window::window_header_size, 15, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F003F, 0xFF7F00FF, &system_white, "The same as A2A in MIDI's props.");
	button_buff->tip->safe_change_position_argumented(_Align::right, 87.5 - moveable_window::window_header_size, button_buff->tip->cy_pos);

	(*window)["INPLACE_MERGE"] = new checkbox(97.5 - moveable_window::window_header_size, 95 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF3F007F, 0x3FFF007F, 1, 0, &system_white, _Align::right, "Enable/disable inplace merge");

	(*window)["COLLAPSE_MIDI"] = new checkbox(72.5 - moveable_window::window_header_size, 75 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF7F00AF, 0x7FFF00AF, 1, 0, &system_white, _Align::right, "Collapse tracks of a MIDI into one");
	(*window)["APPLY_OFFSET_AFTER"] = new checkbox(57.5 - moveable_window::window_header_size, 75 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF7F00AF, 0x7FFF00AF, 1, 0, &system_white, _Align::right, "Apply offset after PPQ change");

	(*window)["AS_THREADS_COUNT"] = new input_field(std::to_string(g_data.detected_threads), 92.5 - moveable_window::window_header_size, 75 - moveable_window::window_header_size, 10, 20, system_white, nullptr, 0x007FFFFF, &system_white, "Threads count", 2, _Align::center, _Align::right, input_field::Type::NaturalNumbers);

	(*window)["AUTOUPDATECHECK"] = new checkbox(-97.5 + moveable_window::window_header_size, 35 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF3F007F, 0x3FFF007F, 1, check_autoupdates, &system_white, _Align::left, "Check for updates automatically");
	/*(*window)["FEEDBACK"] = button_buff = new button("F/B", system_white, settings::feedback_open, 50 - moveable_window::window_header_size, -87.5 - moveable_window::window_header_size, 20, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, nullptr, "_");*/

	(*global_window_handler)["APP_SETTINGS"] = window;

	window = new moveable_window("SMRP Container", system_white, -300, 300, 600, 600, 0x000000CF, 0xFFFFFF7F);

	(*window)["TIMER"] = new input_field("0 s", 0, 195, 10, 50, system_white, nullptr, 0, &system_white, "Timer", 12, _Align::center, _Align::center, input_field::Type::text);

	(*global_window_handler)["SMRP_CONTAINER"] = window;

	window = new moveable_fui_window("MIDI Info Collector", system_white, -150, 200, 300, 400, 200, 1.25f, 100, 100, 5, BACKGROUND_OPQ, HEADER, BORDER);
	(*window)["FLL"] = new text_box("--File log line--", system_white, 0, -moveable_window::window_header_size + 185, 15, 285, 10, 0, 0, 0, _Align::left);
	(*window)["FEL"] = new text_box("--File error line--", system_red, 0, -moveable_window::window_header_size + 175, 15, 285, 10, 0, 0, 0, _Align::left);
	(*window)["TEMPO_GRAPH"] = new Graphing<single_midi_info_collector::tempo_graph>(
		0, -moveable_window::window_header_size + 145, 285, 50, (1. / 20000.), true, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F00FF, 0xFFFFFFFF, 0x7F7F7F7F, nullptr, system_white, false
		);
	(*window)["POLY_GRAPH"] = new Graphing<single_midi_info_collector::polyphony_graph>(
		0, -moveable_window::window_header_size + 95, 285, 50, (1. / 20000.), true, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F00FF, 0xFFFFFFFF, 0x7F7F7F7F, nullptr, system_white, false);
	(*window)["PG_SWITCH"] = new button("Enable graph B", system_white, props_and_sets::SMIC::enable_pg, 37.5, 60 - moveable_window::window_header_size, 70, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, &system_white, "Polyphony graph");
	(*window)["TG_SWITCH"] = new button("Enable graph A", system_white, props_and_sets::SMIC::enable_tg, -37.5, 60 - moveable_window::window_header_size, 70, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, &system_white, "Tempo graph");
	(*window)["ALL_EXP"] = new button("Export all", system_white, props_and_sets::SMIC::export_all, 110, 60 - moveable_window::window_header_size, 65, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, nullptr);
	(*window)["TG_EXP"] = new button("Export Tempo", system_white, props_and_sets::SMIC::export_tg, -110, 60 - moveable_window::window_header_size, 65, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, nullptr);
	(*window)["TG_RESET"] = new button("reset graph A", system_white, props_and_sets::SMIC::reset_tg, 45, 40 - moveable_window::window_header_size, 65, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, nullptr);
	(*window)["PG_RESET"] = new button("reset graph B", system_white, props_and_sets::SMIC::reset_pg, 45, 20 - moveable_window::window_header_size, 65, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, nullptr);
	(*window)["LD_TIME_MAP"] = new button("Copy time map", system_white, props_and_sets::SMIC::load_time_map, 45, 0 - moveable_window::window_header_size, 65, 10, 1, 0x7F7F7F3F, 0x7F7F7FFF, 0xFFFFFFFF, 0x000000FF, 0xFFFFFFFF, nullptr);
	(*window)["HUMANREADIBLE"] = button_buff = new button(".csv", system_white, props_and_sets::SMIC::switch_personal_use, 105, 40 - moveable_window::window_header_size, 45, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, &system_white, "Switches output format for `export all`");
	button_buff->tip->safe_change_position_argumented(_Align::right, button_buff->x_pos + button_buff->width * 0.5, button_buff->tip->cy_pos);
	(*window)["TOTAL_INFO"] = new text_box("----", system_white, 0, -150, 35, 285, 10, 0, 0, 0, _Align::left);
	(*window)["INT_MIN"] = new input_field("0", -132.5, 40 - moveable_window::window_header_size, 10, 20, system_white, nullptr, 0x007FFFFF, &system_white, "Minutes", 3, _Align::center, _Align::left, input_field::Type::NaturalNumbers);
	(*window)["INT_SEC"] = new input_field("0", -107.5, 40 - moveable_window::window_header_size, 10, 20, system_white, nullptr, 0x007FFFFF, &system_white, "Seconds", 2, _Align::center, _Align::left, input_field::Type::NaturalNumbers);
	(*window)["INT_MSC"] = new input_field("000", -80, 40 - moveable_window::window_header_size, 10, 25, system_white, nullptr, 0x007FFFFF, &system_white, "Milliseconds", 3, _Align::center, _Align::left, input_field::Type::NaturalNumbers);
	(*window)["INTEGRATE_TICKS"] = new button("Integrate ticks", system_white, props_and_sets::SMIC::integrate_time, -27.5, 40 - moveable_window::window_header_size, 70, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, &system_white, "Result is the closest tick to that time.");
	(*window)["INT_TIC"] = new input_field("0", -105, 20 - moveable_window::window_header_size, 10, 75, system_white, nullptr, 0x007FFFFF, &system_white, "Ticks", 17, _Align::center, _Align::left, input_field::Type::NaturalNumbers);
	(*window)["INTEGRATE_TIME"] = new button("Integrate time", system_white, props_and_sets::SMIC::differentiate_ticks, -27.5, 20 - moveable_window::window_header_size, 70, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, &system_white, "Result is the time of that tick.");
	(*window)["DELIM"] = new input_field(";", 137.5, 40 - moveable_window::window_header_size, 10, 7.5, system_white, &(props_and_sets::csv_delim), 0x007FFFFF, &system_white, "Delimiter", 1, _Align::center, _Align::right, input_field::Type::text);
	(*window)["ANSWER"] = new text_box("----", system_white, -66.25, -30, 25, 152.5, 10, 0, 0, 0, _Align::center, text_box::VerticalOverflow::recalibrate);

	(*global_window_handler)["SMIC"] = window;

	window = new moveable_fui_window("Simple MIDI player", system_white, /*-200, 197.5, 400, 397.5, 150, 2.5f, 75, 75, 5*/
		-200, 175 + moveable_window::window_header_size, 400, 375, 150, 2.5, 65, 65, 2.5, BACKGROUND_OPQ, HEADER, BORDER);

	(*window)["TEXT"] = new text_box("TIME", legacy_white, 0, 130 + moveable_window::window_header_size, 50, 175, 10, 0xFFFFFF1A, 0, 0, _Align(center | top), text_box::VerticalOverflow::cut);
	(*window)["PAUSE"] = new button("\202", legacy_white, on_player_pause_toggle, -190, 180 - moveable_window::window_header_size, 10, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, nullptr);
	(*window)["STOP"] = new button("\201", legacy_white, on_player_stop, -175, 180 - moveable_window::window_header_size, 10, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, nullptr);

	auto player_view = new player_viewer(0, -20);
	(*window)["VIEW_LEN_SLIDER"] = new slider(slider::Orientation::horizontal, -130, 180 - moveable_window::window_header_size, 65, 14, 21, log2f(player_view->data->scroll_window_us), on_view_length_change, 0x808080FF, 0xFFFFFFFF, 0xAACFFFFF, 0x007FFFFF, 0x808080FF, 10, 4);
	(*window)["BUFFERING_SWITCH"] = new button( 
		player_view->data->enable_simulated_lag ? "Simulate lag" : "Allow unbuffered",
		system_white, 
		on_unbuffered_switch,
		155, 180 - moveable_window::window_header_size, 80, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, nullptr);

	(*window)["SEEK_TO"] = new slider(slider::Orientation::horizontal, 0, 130 - moveable_window::window_header_size, 375, 0, 1, 0, on_playback_seek_to, 0x808080FF, 0xFFFFFFFF, 0xAACFFFFF, 0x007FFFFF, 0x808080FF, 10, 4);

	(*window)["MAXIMISE"] = new button("Maximise", system_white, switch_maximise, 175, 165 - moveable_window::window_header_size, 40, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, nullptr);

	(*window)["VIEW"] = player_view;

	// Device selection label and list
	auto device_list_selector = new selectable_properted_list(
		bs_list_black_small,
		on_device_select,
		nullptr,  // No properties callback
		-145, 135 + moveable_window::window_header_size,  // Position: left side, below viewport
		100,  // width
		12,   // Space between items
		20,   // Max chars per line
		2     // Max visible lines
	);
	device_list_selector->arrow_stick_height = 2.5;

	(*window)["DEVICE_LIST"] = device_list_selector;

	(*global_window_handler)["SIMPLAYER"] = window;

	window = new moveable_fui_window("Feedback/Support? O.o", system_white, -100, 100 + moveable_window::window_header_size, 200, 200 + moveable_window::window_header_size, 100, 1.25f, 50, 50, 5, BACKGROUND_OPQ, HEADER, BORDER);
	(*window)["EDITBOX"] = new edit_box(" ", &system_white, 0, 0, 150, 150, 10, 0, ~0U ^ 0b11100000, 1);

	(*global_window_handler)["SUPPORT"] = window;

	global_window_handler->enable_window("MAIN");
	//global_window_handler->enable_window("SIMPLAYER");
	//global_window_handler->enable_window("V1WT");
	//global_window_handler->enable_window("COMPILEW"); // todo: someday fix the damn editbox...
	//global_window_handler->enable_window("SMIC");
	//global_window_handler->enable_window("OR");
	//global_window_handler->enable_window("SMRP_CONTAINER");
	//global_window_handler->enable_window("APP_SETTINGS");
	//global_window_handler->enable_window("tool_ptr");
	//global_window_handler->enable_window("CAT");
	//global_window_handler->enable_window("SMPAS");//Debug line
	//global_window_handler->enable_window("PROMPT");////DEBUUUUG
	//global_window_handler->enable_window("OTHER_SETS");

	DragAcceptFiles(hWnd, true);
	OleInitialize(nullptr);

	std::cout << "Registering Drag&Drop: " << (RegisterDragDrop(hWnd, &global_drag_and_drop_handler)) << std::endl;

	safc_version_check();
}

///////////////////////////////////////
/////////////END OF USE////////////////
///////////////////////////////////////

void on_timer(int v);
void gl_display()
{
	lfont_symbols_info::initialise_font(default_font_name);

	glClear(GL_COLOR_BUFFER_BIT | GL_ACCUM_BUFFER_BIT);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	if (firstboot)
	{
		firstboot = 0;

		init();
		if (true || april_fool)
		{
			global_window_handler->throw_alert("Today is a special day! ( -w-)\nToday you'll have new background\n(-w- )", "1st of April!", special_signs::draw_wait, true, 0xFF00FFFF, 20);
			(*global_window_handler)["ALERT"]->rgba_background = 0xF;
			_WH_t<text_box>("ALERT", "AlertText")->safe_text_color_change(0xFFFFFFFF);
		}
		if (years_old >= 0)
		{
			global_window_handler->throw_alert("Interesting fact: today is exactly " + std::to_string(years_old) + " years since first SAFC release.\n(o w o  )", "SAFC birthday", special_signs::draw_wait, 1, 0xFF7F3FFF, 50);
			(*global_window_handler)["ALERT"]->rgba_background = 0xF;
			_WH_t<text_box>("ALERT", "AlertText")->safe_text_color_change(0xFFFFFFFF);
		}
		animation_is_active = !animation_is_active;
		on_timer(0);
	}

	if (years_old >= 0 || settings::background_id == 100)
	{
		glBegin(GL_QUADS);
		glColor4f(1, 1, 1, (drag_over) ? 0.25f : 1);
		glVertex2f(0 - internal_range * (wind_x / window_base_width), 0 - internal_range * (wind_y / window_base_height));
		glColor4f(1, 1, 1, (drag_over) ? 0.25f : 1);
		glVertex2f(0 - internal_range * (wind_x / window_base_width), internal_range * (wind_y / window_base_height));
		glColor4f(1, 0.9f, 0.8f, (drag_over) ? 0.25f : 1);
		glVertex2f(internal_range * (wind_x / window_base_width), internal_range * (wind_y / window_base_height));
		glColor4f(0.8f, 0.9f, 1, (drag_over) ? 0.25f : 1);
		glVertex2f(internal_range * (wind_x / window_base_width), 0 - internal_range * (wind_y / window_base_height));
		glEnd();
	}
	else if (april_fool || settings::background_id == 69)
	{
		glBegin(GL_QUADS);
		glColor4f(1, 0, 1, (drag_over) ? 0.25f : 1);
		glVertex2f(0 - internal_range * (wind_x / window_base_width), 0 - internal_range * (wind_y / window_base_height));
		glColor4f(0, 1, 0, (drag_over) ? 0.25f : 1);
		glVertex2f(0 - internal_range * (wind_x / window_base_width), internal_range * (wind_y / window_base_height));
		glColor4f(1, 0.5f, 0, (drag_over) ? 0.25f : 1);
		glVertex2f(internal_range * (wind_x / window_base_width), internal_range * (wind_y / window_base_height));
		glColor4f(0, 0.5f, 1, (drag_over) ? 0.25f : 1);
		glVertex2f(internal_range * (wind_x / window_base_width), 0 - internal_range * (wind_y / window_base_height));
		glEnd();
	}
	else if (month_beginning || settings::background_id == 42)
	{
		glBegin(GL_QUADS);
		glColor4f(1, 0.5f, 0, (drag_over) ? 0.25f : 1);
		glVertex2f(0 - internal_range * (wind_x / window_base_width), 0 - internal_range * (wind_y / window_base_height));
		glVertex2f(0 - internal_range * (wind_x / window_base_width), internal_range * (wind_y / window_base_height));
		glColor4f(0, 0.5f, 1, (drag_over) ? 0.25f : 1);
		glVertex2f(internal_range * (wind_x / window_base_width), internal_range * (wind_y / window_base_height));
		glVertex2f(internal_range * (wind_x / window_base_width), 0 - internal_range * (wind_y / window_base_height));
		glEnd();
	}
	else if (settings::background_id < 4)
	{
		glBegin(GL_QUADS);
		glColor4f(0.05f, 0.05f, 0.10f, (drag_over) ? 0.25f : 1);
		glVertex2f(0 - internal_range * (wind_x / window_base_width), 0 - internal_range * (wind_y / window_base_height));
		glVertex2f(0 - internal_range * (wind_x / window_base_width), internal_range * (wind_y / window_base_height));
		glColor4f(0.05f, 0.1f, 0.25f, (drag_over) ? 0.25f : 1);
		glVertex2f(internal_range * (wind_x / window_base_width), internal_range * (wind_y / window_base_height));
		glVertex2f(internal_range * (wind_x / window_base_width), 0 - internal_range * (wind_y / window_base_height));
		glEnd();
	}
	else
	{
		glBegin(GL_QUADS);
		glColor4f(0.00f, 0.2f, 0.4f, (drag_over) ? 0.25f : 1);
		glVertex2f(0 - internal_range * (wind_x / window_base_width), 0 - internal_range * (wind_y / window_base_height));
		glVertex2f(0 - internal_range * (wind_x / window_base_width), internal_range * (wind_y / window_base_height));
		glVertex2f(internal_range * (wind_x / window_base_width), internal_range * (wind_y / window_base_height));
		glVertex2f(internal_range * (wind_x / window_base_width), 0 - internal_range * (wind_y / window_base_height));
		glEnd();
	}

	glRotatef(dumb_rotation_angle, 0, 0, 1);

	if (global_window_handler)
		global_window_handler->draw();
	if (drag_over)
		special_signs::draw_file_sign(0, 0, 50, 0xFFFFFFFF, 0);
	glRotatef(-dumb_rotation_angle, 0, 0, 1);

	glutSwapBuffers();
	++timer_v;
}

void gl_init()
{
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluOrtho2D((0 - internal_range) * (wind_x / window_base_width), internal_range * (wind_x / window_base_width), (0 - internal_range) * (wind_y / window_base_height), internal_range * (wind_y / window_base_height));
}

void gl_close()
{
	settings::regestry_access.Close();

	if (player)
		player->stop();
}

void on_timer(int v)
{
	glutTimerFunc(15, on_timer, 0);
	if (animation_is_active)
	{
		gl_display();
		++timer_v;
	}
}

void on_resize(int x, int y)
{
	wind_x = x;
	wind_y = y;
	gl_init();
	glViewport(0, 0, x, y);
	if (global_window_handler)
	{
		auto SMRP = (*global_window_handler)["SMRP_CONTAINER"];
		SMRP->safe_change_position_argumented(0, 0, 0);
		SMRP->not_safe_resize_centered(internal_range * 3 * (wind_y / window_base_height) + 2 * moveable_window::window_header_size, internal_range * 3 * (wind_x / window_base_width));

		if (simplayer_maximised)
			apply_simplayer_maximised_layout();
	}
}
void inline rotate_view(float& x, float& y)
{
	float t = x * cos(rotation_angle()) + y * sin(rotation_angle());
	y = 0.f - x * sin(rotation_angle()) + y * cos(rotation_angle());
	x = t;
}

void inline absolute_to_actual_coords(int ix, int iy, float& x, float& y)
{
	float wx = wind_x, wy = wind_y;
	x = ((float)(ix - wx * 0.5f)) / (0.5f * (wx / (internal_range * (wind_x / window_base_width))));
	y = ((float)(0 - iy + wy * 0.5f)) / (0.5f * (wy / (internal_range * (wind_y / window_base_height))));
	rotate_view(x, y);
}

void gl_motion(int ix, int iy)
{
	float fx, fy;
	absolute_to_actual_coords(ix, iy, fx, fy);
	mouse_x_position = fx;
	mouse_y_position = fy;
	if (global_window_handler)
		global_window_handler->mouse_handler(fx, fy, 0, 0);
}

void gl_key(std::uint8_t k, int x, int y)
{
	if (global_window_handler)
		global_window_handler->keyboard_handler(k);

	if (k == 27)
	{
		gl_close();
		exit(0);
	}
}

void gl_click(int butt, int state, int x, int y)
{
	float fx, fy;
	char button;
	absolute_to_actual_coords(x, y, fx, fy);
	button = butt - 1;

	if (state == GLUT_DOWN)
		state = -1;
	else if (state == GLUT_UP)
		state = 1;

	if (global_window_handler)
		global_window_handler->mouse_handler(fx, fy, button, static_cast<char>(state));
}

void gl_drag(int x, int y)
{
	//gl_click(88, MOUSE_DRAG_EVENT, x, y);
	gl_motion(x, y);
}

void gl_special_key(int Key, int x, int y)
{
	auto modif = glutGetModifiers();
	if (!(modif & GLUT_ACTIVE_ALT))
	{
		switch (Key)
		{
			case GLUT_KEY_DOWN:		if (global_window_handler) global_window_handler->keyboard_handler(1);
				break;
			case GLUT_KEY_UP:		if (global_window_handler) global_window_handler->keyboard_handler(2);
				break;
			case GLUT_KEY_LEFT:		if (global_window_handler) global_window_handler->keyboard_handler(3);
				break;
			case GLUT_KEY_RIGHT:		if (global_window_handler) global_window_handler->keyboard_handler(4);
				break;
			case GLUT_KEY_HOME:		if (global_window_handler) global_window_handler->keyboard_handler(5);
				break;
			case GLUT_KEY_END:		if (global_window_handler) global_window_handler->keyboard_handler(6);
				break;

			case GLUT_KEY_F5:		if (global_window_handler) init();
				break;
		}
	}
	if (modif == GLUT_ACTIVE_ALT && Key == GLUT_KEY_DOWN)
	{
		internal_range *= 1.1f;
		on_resize(wind_x, wind_y);

		init();
	}
	else if (modif == GLUT_ACTIVE_ALT && Key == GLUT_KEY_UP)
	{
		internal_range /= 1.1f;
		on_resize(wind_x, wind_y);

		init();
	}
}

void gl_exit(int a)
{
}

struct safc_runtime
{
	virtual void operator()(int argc, char** argv) = 0;
};

struct safc_gui_runtime :
	public safc_runtime
{
	virtual void operator()(int argc, char** argv) override
	{
		_wremove(L"_s");
		_wremove(L"_f");
		_wremove(L"_g");

#ifdef _DEBUG 
		ShowWindow(GetConsoleWindow(), SW_SHOW);
#else // _DEBUG 
		ShowWindow(GetConsoleWindow(), SW_HIDE);
#endif
		ShowWindow(GetConsoleWindow(), SW_SHOW);

		SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
		//srand(1);
		//srand(clock());
		init_legacy_draw_map();
		//cout << to_string((std::uint16_t)0) << endl;

		srand(collect_time_data()); 
		__glutInitWithExit(&argc, argv, gl_exit);
		//cout << argv[0] << endl;
		glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA | GLUT_ALPHA | GLUT_MULTISAMPLE);
		glutInitWindowSize(window_base_width, window_base_height);
		//glutInitWindowPosition(50, 0);
		glutCreateWindow(window_title);

		hWnd = FindWindowA(NULL, window_title);
		hDc = GetDC(hWnd);

		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);//_MINUS_SRC_ALPHA
		glEnable(GL_BLEND);
		//glutSetOption(GLUT_MULTISAMPLE, 4);

		//auto vendor = glGetString(GL_VENDOR);
		//auto renderer = glGetString(GL_RENDERER);
		//auto version = glGetString(GL_VERSION);
		//printf("%s - %s.\nVersion %s\n", vendor, renderer, version);

		//glEnable(GL_POLYGON_SMOOTH);//laggy af
		//glEnable(GL_LINE_SMOOTH);//GL_POLYGON_SMOOTH
		//glEnable(GL_POINT_SMOOTH);

		//glShadeModel(GL_SMOOTH); 
		//glEnable(GLUT_MULTISAMPLE); 
		//glutSetOption(GLUT_MULTISAMPLE, 8);

		glHint(GL_LINE_SMOOTH_HINT, GL_FASTEST);//GL_FASTEST//GL_NICEST
		glHint(GL_POINT_SMOOTH_HINT, GL_FASTEST);
		glHint(GL_POLYGON_SMOOTH_HINT, GL_FASTEST);

		glutCloseFunc(gl_close);
		glutMouseFunc(gl_click);
		glutReshapeFunc(on_resize);
		glutSpecialFunc(gl_special_key);
		glutMotionFunc(gl_drag);
		glutPassiveMotionFunc(gl_motion);
		glutKeyboardFunc(gl_key);
		glutDisplayFunc(gl_display);
		gl_init();
		glutMainLoop();
	}
};

struct safc_cli_runtime:
	public safc_runtime
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

	virtual void operator()(int argc, char** argv) override
	{
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
		local_mctm->start_ri_merge();
		while (!local_mctm->is_ri_merge_complete())
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		local_mctm->start_final_merge();
		while (!local_mctm->complete)
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
};

int main(int argc, char** argv)
{
	player = std::make_shared<simple_player>();
	player->init();

	g_version_tuple = ___GetVersion();

	std::ios_base::sync_with_stdio(false); //why not

	std::shared_ptr<safc_runtime> runtime;

	restore_reg_settings();

	if (argc > 1)
		runtime = std::make_shared<safc_cli_runtime>();
	else
		runtime = std::make_shared<safc_gui_runtime>();

	(*runtime)(argc, argv);

	return 0;
}
