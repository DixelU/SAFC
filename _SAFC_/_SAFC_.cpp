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

std::tuple<std::uint16_t, std::uint16_t, std::uint16_t, std::uint16_t> __versionTuple;
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

std::wstring ExtractDirectory(const std::wstring& path) 
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

bool SAFC_Update(const std::wstring& latest_release)
{
#ifndef __X64
	constexpr wchar_t* const archive_name = (wchar_t* const)L"SAFC32.7z";;
#else
	constexpr wchar_t* const archive_name = (wchar_t* const)L"SAFC64.7z";
#endif
	wchar_t current_file_path[MAX_PATH];
	bool updated_flag = false;
	//GetCurrentDirectoryW(MAX_PATH, current_file_path);
	GetModuleFileNameW(NULL, current_file_path, MAX_PATH);
	//std::wstring executablepath = current_file_path;
	std::wstring filename = ExtractDirectory(current_file_path);
	std::wstring pathway = filename;
	filename += L"update.7z";
	//wsprintfW(current_file_path, L"%S%S", filename.c_str(), L"update.7z\0");
	std::wstring link = L"https://github.com/DixelU/SAFC/releases/download/" + latest_release + L"/" + archive_name;
	HRESULT co_res = URLDownloadToFileW(NULL, link.c_str(), filename.c_str(), 0, NULL);
	std::string error_msg;
	
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

void SAFC_VersionCheck()
{
	auto [maj, min, ver, build] = __versionTuple;
	std::wcout << L"Current vesion: v" << maj << L"." << min << L"." << ver << L"." << build << L"\n";

	if (!check_autoupdates)
		return;

	std::thread version_checker([]() 
	{
		bool flag = false;
		_wremove(L"_s");
		constexpr wchar_t* SAFC_tags_link = (wchar_t* const)L"https://api.github.com/repos/DixelU/SAFC/tags";
		auto current_file_path = boost::dll::program_location().filename().wstring();
		std::wstring filename = ExtractDirectory(current_file_path);
		std::wstring pathway = filename;
		filename += L"tags.json";
		// if you have error here -> https://github.com/Microsoft/WSL/issues/22#issuecomment-207788173
		HRESULT res = URLDownloadToFileW(NULL, SAFC_tags_link, filename.c_str(), 0, NULL);
		try 
		{
			if (res == S_OK) 
			{
				auto [maj, min, ver, build] = __versionTuple;
				std::ifstream input(filename);
				std::string temp_buffer;
				std::getline(input, temp_buffer);
				input.close();
				auto JSON_Value = JSON::Parse(temp_buffer.c_str());
				auto& git_latest_version = ((JSON_Value)->AsArray()[0])->AsObject().at(L"name")->AsString();
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
					flag = SAFC_Update(git_latest_version);
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
	version_checker.detach();
}

size_t GetAvailableMemory() 
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
		MEMORYSTATUSEX m;
		m.dwLength = sizeof(m);
		if (GMSEx(&m))
		{
			ret = (int)(m.ullAvailPhys >> 20);
		}
	}
	else 
	{
		MEMORYSTATUS m;
		m.dwLength = sizeof(m);
		GlobalMemoryStatus(&m);
		ret = (int)(m.dwAvailPhys >> 20);
	}

	return ret;
}

//////////////////////////////
////TRUE USAGE STARTS HERE////
//////////////////////////////

button_settings* BS_List_Black_Small = new button_settings(&system_white, 0, 0, 100, 10, 1, 0, 0, 0xFFEFDFFF, 0x00003F7F, 0x7F7F7FFF);

std::uint32_t DefaultBoolSettings = _BoolSettings::remove_remnants | _BoolSettings::remove_empty_tracks | _BoolSettings::all_instruments_to_piano;

struct FileSettings
{////per file settings
	std::wstring Filename;
	std::wstring PostprocessedFile_Name, WFileNamePostfix;
	std::string AppearanceFilename, AppearancePath, FileNamePostfix;
	std::uint16_t NewPPQN, OldPPQN, OldTrackNumber, MergeMultiplier;
	std::int16_t GroupID;
	double NewTempo;
	std::uint64_t filesize;
	std::int64_t SelectionStart, SelectionLength;
	bool
		is_midi,
		InplaceMergeEnabled,
		allow_legacy_rsb_meta_interaction,
		RSBCompression,
		ChannelsSplit,
		CollapseMIDI,
		AllowSysex,
		EnableZeroVelocity,
		ApplyOffsetAfter;
	std::shared_ptr<cut_and_transpose> KeyMap;
	std::shared_ptr<polyline_converter<std::uint8_t, std::uint8_t>> VolumeMap;
	std::shared_ptr<polyline_converter<std::uint16_t, std::uint16_t>> PitchBendMap;
	std::uint32_t BoolSettings;
	std::int64_t OffsetTicks;

	single_midi_info_collector::time_graph TimeMap;

	FileSettings(const std::wstring& Filename) 
	{
		this->Filename = Filename;
		AppearanceFilename = AppearancePath = "";
		auto pos = Filename.rfind('\\');
		for (; pos < Filename.size(); pos++)
			AppearanceFilename.push_back(Filename[pos] & 0xFF);
		for (int i = 0; i < Filename.size(); i++)
			AppearancePath.push_back((char)(Filename[i] & 0xFF));
		//cout << AppearancePath << " ::\n";
		fast_midi_checker FMIC(Filename);
		this->is_midi = FMIC.is_acssessible && FMIC.is_midi;
		OldPPQN = FMIC.PPQN;
		NewPPQN = FMIC.PPQN;
		MergeMultiplier = 0;
		OldTrackNumber = FMIC.expected_track_number;
		OffsetTicks = InplaceMergeEnabled = 0;
		AllowSysex = false;
		EnableZeroVelocity = false;
		filesize = FMIC.filesize;
		GroupID = NewTempo = 0;
		SelectionStart = 0;
		SelectionLength = -1;
		BoolSettings = DefaultBoolSettings;
		FileNamePostfix = "_.mid"; //_FILENAMEWITHEXTENSIONSTRING_.mid
		WFileNamePostfix = L"_.mid";
		RSBCompression = ChannelsSplit = false;
		CollapseMIDI = true;
		allow_legacy_rsb_meta_interaction = false;
		ApplyOffsetAfter = true;
	}

	inline void SwitchBoolSetting(std::uint32_t SMP_BoolSetting) 
	{
		BoolSettings ^= SMP_BoolSetting;
	}

	inline void SetBoolSetting(std::uint32_t SMP_BoolSetting, bool NewState) 
	{
		if (BoolSettings & SMP_BoolSetting && NewState)
			return;
		
		if (!(BoolSettings & SMP_BoolSetting) && !NewState)
			return;

		SwitchBoolSetting(SMP_BoolSetting);
	}

	std::shared_ptr<single_midi_processor_2::processing_data> BuildSMRPProcessingData() 
	{
		auto smrp_data = std::make_shared<single_midi_processor_2::processing_data>();
		auto& settings = smrp_data->settings;
		smrp_data->filename = Filename;
		smrp_data->postfix = WFileNamePostfix;
		if(VolumeMap)
			settings.volume_map = std::make_shared<byte_plc_core>(VolumeMap);
		if(PitchBendMap)
			settings.pitch_map = std::make_shared<_14bit_plc_core>(PitchBendMap);
		settings.key_converter = KeyMap;
		settings.new_ppqn = NewPPQN;
		settings.old_ppqn = OldPPQN;
		settings.enable_imp_events_filter = (BoolSettings & _BoolSettings::enable_important_filter);
		settings.imp_events_filter.pass_instument_cnage = settings.enable_imp_events_filter && (BoolSettings & _BoolSettings::imp_filter_allow_progc);
		settings.imp_events_filter.pass_notes = settings.enable_imp_events_filter && (BoolSettings & _BoolSettings::imp_filter_allow_notes);
		settings.imp_events_filter.pass_pitch = settings.enable_imp_events_filter && (BoolSettings & _BoolSettings::imp_filter_allow_pitch);
		settings.imp_events_filter.pass_tempo = settings.enable_imp_events_filter && (BoolSettings & _BoolSettings::imp_filter_allow_tempo);
		settings.imp_events_filter.pass_other = settings.enable_imp_events_filter && (BoolSettings & _BoolSettings::imp_filter_allow_other);
		settings.filter.pass_notes = !(BoolSettings & _BoolSettings::ignore_notes);
		settings.filter.pass_pitch = !(BoolSettings & _BoolSettings::ignore_pitches);
		settings.filter.pass_tempo = !(BoolSettings & _BoolSettings::ignore_tempos);
		settings.filter.pass_other = !(BoolSettings & _BoolSettings::ignore_all_but_tempos_notes_and_pitch);
		settings.filter.piano_only = (BoolSettings & _BoolSettings::all_instruments_to_piano);
		settings.proc_details.remove_remnants = BoolSettings & _BoolSettings::remove_remnants;
		settings.proc_details.remove_empty_tracks = BoolSettings & _BoolSettings::remove_empty_tracks;
		settings.proc_details.channel_split = ChannelsSplit;
		settings.proc_details.whole_midi_collapse = CollapseMIDI;
		settings.proc_details.apply_offset_after = ApplyOffsetAfter;
		settings.legacy.enable_zero_velocity = EnableZeroVelocity;
		settings.legacy.ignore_meta_rsb = allow_legacy_rsb_meta_interaction;
		settings.legacy.rsb_compression = RSBCompression;
		settings.filter.pass_sysex = AllowSysex;
		InplaceMergeEnabled = 
			settings.details.inplace_mergable = InplaceMergeEnabled && !RSBCompression;
		settings.details.group_id = GroupID;
		settings.details.initial_filesize = filesize;
		settings.offset = OffsetTicks;

		if (NewTempo > 3.)
			settings.tempo.set_override_value(NewTempo);
		if (OffsetTicks < 0 && -OffsetTicks > SelectionStart)
			SelectionStart = -OffsetTicks;
		if (SelectionStart && (SelectionLength < 0))
			SelectionLength -= SelectionStart;

		if (NewTempo > 3. && !TimeMap.empty())
		{
			settings.original_time_map = TimeMap;
			settings.flatten = true;
		}

		settings.selection_data = single_midi_processor_2::settings_obj::selection(SelectionStart, SelectionLength);

		smrp_data->appearance_filename = AppearanceFilename;

		return smrp_data;
	}
};
struct _SFD_RSP 
{
	std::int32_t ID;
	std::int64_t filesize;
	_SFD_RSP(std::int32_t ID, std::int64_t filesize)
	{
		this->ID = ID;
		this->filesize = filesize;
	}
	inline bool operator<(_SFD_RSP a)
	{
		return filesize < a.filesize;
	}
};

struct SAFCData 
{
	////overall settings and storing perfile settings....
	std::vector<FileSettings> Files;
	std::wstring SavePath;
	std::uint16_t GlobalPPQN;
	std::int32_t GlobalOffset;
	float GlobalNewTempo;
	bool IncrementalPPQN;
	bool InplaceMergeFlag;
	bool ChannelsSplit;
	bool RSBCompression;
	bool CollapseMIDI;
	bool ApplyOffsetAfter;
	bool IsCLIMode;
	std::uint16_t DetectedThreads;

	SAFCData()
	{
		GlobalPPQN = GlobalOffset = GlobalNewTempo = 0;
		DetectedThreads = 1;
		IncrementalPPQN = true;
		InplaceMergeFlag = false;
		IsCLIMode = false;
		CollapseMIDI = false;
		SavePath = L"";
		ChannelsSplit = RSBCompression = false;
		ApplyOffsetAfter = true;
	}

	void ResolveSubdivisionProblem_GroupIDAssign(std::uint16_t ThreadsCount = 0)
	{
		if (!ThreadsCount)
			ThreadsCount = DetectedThreads;
		if (Files.empty())
		{
			SavePath = L"";
			return;
		}
		else
			SavePath = Files[0].Filename + L".AfterSAFC.mid";

		if (Files.size() == 1)
		{
			Files.front().GroupID = 0;
			return;
		}
		std::vector<_SFD_RSP> Sizes;
		std::vector<std::int64_t> SumSize;
		std::int64_t T = 0;

		for (int i = 0; i < Files.size(); i++)
		{
			Sizes.emplace_back(i, Files[i].filesize);
		}

		std::sort(Sizes.begin(), Sizes.end());

		for (int i = 0; i < Sizes.size(); i++)
		{
			SumSize.push_back((T += Sizes[i].filesize));
		}

		for (int i = 0; i < SumSize.size(); i++)
		{
			Files[Sizes[i].ID].GroupID = (std::uint16_t)(ceil(((float)SumSize[i] / ((float)SumSize.back())) * ThreadsCount) - 1.);
			std::cout << "Thread " << Files[Sizes[i].ID].GroupID << ": " << Sizes[i].filesize << ":\t" << Sizes[i].ID << std::endl;
		}
	}

	void SetGlobalPPQN(std::uint16_t NewPPQN = 0, bool ForceGlobalPPQNOverride = false)
	{
		if (!NewPPQN && ForceGlobalPPQNOverride)
			return;
		if (!ForceGlobalPPQNOverride)
			NewPPQN = GlobalPPQN;
		if (!ForceGlobalPPQNOverride && (!NewPPQN || IncrementalPPQN))
		{
			for (int i = 0; i < Files.size(); i++)
				if (NewPPQN < Files[i].OldPPQN)NewPPQN = Files[i].OldPPQN;
		}
		for (int i = 0; i < Files.size(); i++)
			Files[i].NewPPQN = NewPPQN;
		GlobalPPQN = NewPPQN;
	}

	void SetGlobalOffset(std::int32_t Offset)
	{
		for (int i = 0; i < Files.size(); i++)
			Files[i].OffsetTicks = Offset;
		GlobalOffset = Offset;
	}

	void SetGlobalTempo(float NewTempo)
	{
		for (int i = 0; i < Files.size(); i++)
			Files[i].NewTempo = NewTempo;
		GlobalNewTempo = NewTempo;
	}

	void RemoveByID(std::uint32_t ID)
	{
		if (ID < Files.size())
			Files.erase(Files.begin() + ID);
	}

	std::shared_ptr<midi_collection_threaded_merger> MCTM_Constructor()
	{
		using proc_data_ptr = std::shared_ptr<single_midi_processor_2::processing_data>;
		std::vector<proc_data_ptr> SMRPv;
		for (int i = 0; i < Files.size(); i++)
			SMRPv.push_back(Files[i].BuildSMRPProcessingData());
		return std::make_shared<midi_collection_threaded_merger>(SMRPv, GlobalPPQN, SavePath, IsCLIMode);
	}

	FileSettings& operator[](std::int32_t ID)
	{
		return Files[ID];
	}
};

SAFCData _Data;
std::shared_ptr<midi_collection_threaded_merger> GlobalMCTM;

void throw_alert_error(std::string&& AlertText) 
{
	std::cerr << AlertText << std::endl;

	if (wh)
		wh->throw_alert(AlertText, "ERROR!", special_signs::draw_ex_triangle, true, 0xFFAF00FF, 0xFF);
}

void throw_alert_warning(std::string&& AlertText)
{
	std::cout << AlertText << std::endl;

	if (wh)
		wh->throw_alert(AlertText, "Warning!", special_signs::draw_ex_triangle, true, 0x7F7F7FFF, 0xFFFFFFAF);
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
	ofn.lpstrFilter = L"MIDI Files(*.mid)\0*.mid\0";
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
	ofn.lpstrFilter = L"MIDI Files(*.mid)\0*.mid\0";
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
	return ((*(*wh)[window])[element]);
}

template<typename ui_part_type>
ui_part_type* _WH_t(const char* window, const char* element) requires std::is_base_of_v<handleable_ui_part, ui_part_type>
{
	return dynamic_cast<ui_part_type*>(_WH(window, element));
}

void add_files(const std::vector<std::wstring>& Filenames)
{
	if(wh)
		wh->disable_all_windows();
	for (int i = 0; i < Filenames.size(); i++)
	{
		if (Filenames[i].empty())
			continue;
		_Data.Files.push_back(FileSettings(Filenames[i]));
		auto& lastFile = _Data.Files.back();
		if (lastFile.is_midi)
		{
			if (wh)
				_WH_t<selectable_properted_list>("MAIN", "List")->safe_push_back_new_string(lastFile.AppearanceFilename);

			std::uint32_t Counter = 0;
			lastFile.NewTempo = _Data.GlobalNewTempo;
			lastFile.OffsetTicks = _Data.GlobalOffset;
			lastFile.InplaceMergeEnabled = _Data.InplaceMergeFlag;
			lastFile.ChannelsSplit = _Data.ChannelsSplit;
			lastFile.RSBCompression = _Data.RSBCompression;
			lastFile.CollapseMIDI = _Data.CollapseMIDI;
			lastFile.ApplyOffsetAfter = _Data.ApplyOffsetAfter;

			for (int q = 0; q < _Data.Files.size(); q++)
			{
				if (_Data[q].Filename == lastFile.Filename)
				{
					_Data[q].FileNamePostfix = std::to_string(Counter) + "_.mid";
					_Data[q].WFileNamePostfix = std::to_wstring(Counter) + L"_.mid";
					Counter++;
				}
			}
		}
		else
		{
			_Data.Files.pop_back();
		}
	}
	_Data.SetGlobalPPQN();
	_Data.ResolveSubdivisionProblem_GroupIDAssign();
}
void OnAdd()
{
	worker_singleton<struct midi_file_list>::instance().push([](){
		std::vector<std::wstring> Filenames = multiple_open_file_dialog(L"Select midi files");
		add_files(Filenames);
	});
}

namespace PropsAndSets
{
	std::string* PPQN = new std::string(""), * OFFSET = new std::string(""), * TEMPO = new std::string("");
	int currentID = -1, CaTID = -1, VMID = -1, PMID = -1;
	bool ForPersonalUse = true; // tf is this
	single_midi_info_collector* SMICptr = nullptr;
	std::string CSV_DELIM = ";";

	void OpenFileProperties(int ID)
	{
		if (!(ID < _Data.Files.size() && ID >= 0))
		{
			currentID = -1;
			return;
		}

		currentID = ID;
		auto PASWptr = (*wh)["SMPAS"];
		auto OSptr = (*wh)["OTHER_SETS"];

		((text_box*)((*PASWptr)["FileName"]))->safe_string_replace("..." + _Data[ID].AppearanceFilename);
		((input_field*)((*PASWptr)["PPQN"]))->update_input_string();
		((input_field*)((*PASWptr)["PPQN"]))->safe_string_replace(std::to_string((_Data[ID].NewPPQN) ? _Data[ID].NewPPQN : _Data[ID].OldPPQN));
		((input_field*)((*PASWptr)["TEMPO"]))->safe_string_replace(std::to_string(_Data[ID].NewTempo));
		((input_field*)((*PASWptr)["OFFSET"]))->safe_string_replace(std::to_string(_Data[ID].OffsetTicks));
		((input_field*)((*PASWptr)["GROUPID"]))->safe_string_replace(std::to_string(_Data[ID].GroupID));

		((input_field*)((*PASWptr)["SELECT_START"]))->safe_string_replace(std::to_string(_Data[ID].SelectionStart));
		((input_field*)((*PASWptr)["SELECT_LENGTH"]))->safe_string_replace(std::to_string(_Data[ID].SelectionLength));

		((checkbox*)((*PASWptr)["BOOL_REM_TRCKS"]))->state = _Data[ID].BoolSettings & _BoolSettings::remove_empty_tracks;
		((checkbox*)((*PASWptr)["BOOL_REM_REM"]))->state = _Data[ID].BoolSettings & _BoolSettings::remove_remnants;

		((checkbox*)((*OSptr)["BOOL_PIANO_ONLY"]))->state = _Data[ID].BoolSettings & _BoolSettings::all_instruments_to_piano;
		((checkbox*)((*OSptr)["BOOL_IGN_TEMPO"]))->state = _Data[ID].BoolSettings & _BoolSettings::ignore_tempos;
		((checkbox*)((*OSptr)["BOOL_IGN_PITCH"]))->state = _Data[ID].BoolSettings & _BoolSettings::ignore_pitches;
		((checkbox*)((*OSptr)["BOOL_IGN_NOTES"]))->state = _Data[ID].BoolSettings & _BoolSettings::ignore_notes;
		((checkbox*)((*OSptr)["BOOL_IGN_ALL_EX_TPS"]))->state = _Data[ID].BoolSettings & _BoolSettings::ignore_all_but_tempos_notes_and_pitch;

		((checkbox*)((*OSptr)["IMP_FLT_ENABLE"]))->state = _Data[ID].BoolSettings & _BoolSettings::enable_important_filter;
		((checkbox*)((*OSptr)["IMP_FLT_NOTES"]))->state = _Data[ID].BoolSettings & _BoolSettings::imp_filter_allow_notes;
		((checkbox*)((*OSptr)["IMP_FLT_TEMPO"]))->state = _Data[ID].BoolSettings & _BoolSettings::imp_filter_allow_tempo;
		((checkbox*)((*OSptr)["IMP_FLT_PITCH"]))->state = _Data[ID].BoolSettings & _BoolSettings::imp_filter_allow_pitch;
		((checkbox*)((*OSptr)["IMP_FLT_PROGC"]))->state = _Data[ID].BoolSettings & _BoolSettings::imp_filter_allow_progc;
		((checkbox*)((*OSptr)["IMP_FLT_OTHER"]))->state = _Data[ID].BoolSettings & _BoolSettings::imp_filter_allow_other;

		((checkbox*)((*OSptr)["LEGACY_META_RSB_BEHAVIOR"]))->state = _Data[ID].allow_legacy_rsb_meta_interaction;
		((checkbox*)((*OSptr)["ALLOW_SYSEX"]))->state = _Data[ID].AllowSysex;
		((checkbox*)((*OSptr)["ENABLE_ZERO_VELOCITY"]))->state = _Data[ID].EnableZeroVelocity;

		((checkbox*)((*PASWptr)["SPLIT_TRACKS"]))->state = _Data[ID].ChannelsSplit;
		((checkbox*)((*PASWptr)["RSB_COMPRESS"]))->state = _Data[ID].RSBCompression;

		((checkbox*)((*PASWptr)["INPLACE_MERGE"]))->state = _Data[ID].InplaceMergeEnabled;

		((checkbox*)((*PASWptr)["COLLAPSE_MIDI"]))->state = _Data[ID].CollapseMIDI;
		((checkbox*)((*PASWptr)["APPLY_OFFSET_AFTER"]))->state = _Data[ID].ApplyOffsetAfter;

		((text_box*)((*PASWptr)["CONSTANT_PROPS"]))->safe_string_replace(
			"File size: " + std::to_string(_Data[ID].filesize) + "b\n" +
			"Old PPQN: " + std::to_string(_Data[ID].OldPPQN) + "\n" +
			"Track number (header info): " + std::to_string(_Data[ID].OldTrackNumber) + "\n" +
			"\"Remnant\" file postfix: " + _Data[ID].FileNamePostfix + "\n" +
			"Time map status: " + ((_Data[ID].TimeMap.empty()) ? "Empty" : "Present")
		);

		wh->enable_window("SMPAS");
	}

	void InitializeCollecting()
	{
		if (currentID < 0 && currentID >= _Data.Files.size()) 
		{
			throw_alert_error("How you have managed to select the midi beyond the list? O.o\n" + std::to_string(currentID));
			return;
		}

		auto UIElement = (Graphing<single_midi_info_collector::tempo_graph>*)(*(*wh)["SMIC"])["TEMPO_GRAPH"];
		if (SMICptr)
		{
			{ std::lock_guard<std::recursive_mutex> locker(UIElement->lock); }
			UIElement->graph = nullptr;
		}

		SMICptr = new single_midi_info_collector(_Data.Files[currentID].Filename, _Data.Files[currentID].OldPPQN, _Data.Files[currentID].allow_legacy_rsb_meta_interaction);

		std::thread th([]()
		{
			wh->main_window_id = "SMIC";
			wh->disable_all_windows();
			std::thread ith([]()
			{
				auto All_Exp = (*(*wh)["SMIC"])["ALL_EXP"];
				auto TG_Exp = (*(*wh)["SMIC"])["TG_EXP"];
				auto ITicks = (*(*wh)["SMIC"])["INTEGRATE_TICKS"];
				auto ITime = (*(*wh)["SMIC"])["INTEGRATE_TIME"];
				auto error_line = (text_box*)(*(*wh)["SMIC"])["FEL"];
				auto InfoLine = (text_box*)(*(*wh)["SMIC"])["FLL"];
				auto Delim = (input_field*)(*(*wh)["SMIC"])["DELIM"];
				auto UIMinutes = (input_field*)(*(*wh)["SMIC"])["INT_MIN"];
				auto UISeconds = (input_field*)(*(*wh)["SMIC"])["INT_SEC"];
				auto UIT = (input_field*)(*(*wh)["SMIC"])["TG_SWITCH"];
				auto UIP = (input_field*)(*(*wh)["SMIC"])["PG_SWITCH"];
				auto UIMilliseconds = (input_field*)(*(*wh)["SMIC"])["INT_MSC"];
				auto UITicks = (input_field*)(*(*wh)["SMIC"])["INT_TIC"];
				auto UIElement_TG = (Graphing<single_midi_info_collector::tempo_graph>*)(*(*wh)["SMIC"])["TEMPO_GRAPH"];
				auto UIElement_PG = (Graphing<single_midi_info_collector::polyphony_graph>*)(*(*wh)["SMIC"])["POLY_GRAPH"];
				UIElement_PG->enabled = false;
				UIElement_PG->reset();
				UIElement_TG->enabled = false;
				UIElement_TG->reset();
				Delim->safe_string_replace(CSV_DELIM);
				Delim->current_string = CSV_DELIM;
				UIP->safe_string_replace("enable graph B");
				UIT->safe_string_replace("enable graph A");
				InfoLine->safe_string_replace("Please wait...");
				UIMinutes->safe_string_replace("0");
				UISeconds->safe_string_replace("0");
				UIMilliseconds->safe_string_replace("0");
				UITicks->safe_string_replace("0");
				All_Exp->disable();
				TG_Exp->disable();
				ITicks->disable();
				ITime->disable();
				while (!SMICptr->finished)
				{
					if (error_line->text != SMICptr->error_line)
						error_line->safe_string_replace(SMICptr->error_line);
					if (InfoLine->text != SMICptr->log_line)
						InfoLine->safe_string_replace(SMICptr->log_line);
					Sleep(10);
				}
				InfoLine->safe_string_replace("Finished");
				All_Exp->enable();
				TG_Exp->enable();
				ITicks->enable();
				ITime->enable();
			});
			ith.detach();

			SMICptr->fetch_data();
			auto UIElement_TG = (Graphing<single_midi_info_collector::tempo_graph>*)(*(*wh)["SMIC"])["TEMPO_GRAPH"];
			UIElement_TG->graph = &(SMICptr->tempo_map);
			auto UIElement_PG = (Graphing<single_midi_info_collector::polyphony_graph>*)(*(*wh)["SMIC"])["POLY_GRAPH"];
			UIElement_PG->graph = &(SMICptr->polyphony);

			auto UIElement_TB = (text_box*)(*(*wh)["SMIC"])["TOTAL_INFO"];
			UIElement_TB->safe_string_replace(
				"Total (real) tracks: " + std::to_string(SMICptr->tracks.size()) + "; ... "
			);

			wh->main_window_id = "MAIN";
			wh->enable_window("MAIN");
			wh->enable_window("SMIC");
		});
		th.detach();

		wh->enable_window("SMIC");
	}

	namespace SMIC
	{
		void LoadTimeMap()
		{
			if (currentID < 0 && currentID >= _Data.Files.size())
			{
				throw_alert_error("How you have managed to select the midi beyond the lists end? O.o\n" + std::to_string(currentID));
				return;
			}

			if (!SMICptr || !SMICptr->finished)
			{
				throw_alert_warning("Time map is not ready yet...");
				return;
			}

			if (SMICptr->internal_time_map.empty())
			{
				throw_alert_error("Time map of the selected midi is empty...");
				return;
			}

			_Data[currentID].TimeMap = SMICptr->internal_time_map;
			OpenFileProperties(currentID);
			wh->disable_window("SMIC");
		}

		void EnablePG()
		{
			auto UIElement_PG = (Graphing<single_midi_info_collector::polyphony_graph>*)(*(*wh)["SMIC"])["POLY_GRAPH"];
			auto UIElement_Butt = (button*)(*(*wh)["SMIC"])["PG_SWITCH"];
			if (UIElement_PG->enabled)
				UIElement_Butt->safe_string_replace("enable graph B");
			else
				UIElement_Butt->safe_string_replace("disable graph B");
			UIElement_PG->enabled ^= true;
		}

		void EnableTG()
		{
			auto UIElement_TG = (Graphing<single_midi_info_collector::tempo_graph>*)(*(*wh)["SMIC"])["TEMPO_GRAPH"];
			auto UIElement_Butt = (button*)(*(*wh)["SMIC"])["TG_SWITCH"];
			if (UIElement_TG->enabled)
				UIElement_Butt->safe_string_replace("enable graph A");
			else
				UIElement_Butt->safe_string_replace("disable graph A");
			UIElement_TG->enabled ^= true;
		}

		void ResetTG()
		{//TEMPO_GRAPH
			auto UIElement_TG = (Graphing<single_midi_info_collector::tempo_graph>*)(*(*wh)["SMIC"])["TEMPO_GRAPH"];
			UIElement_TG->reset();
		}

		void ResetPG()
		{//TEMPO_GRAPH
			auto UIElement_PG = (Graphing<single_midi_info_collector::tempo_graph>*)(*(*wh)["SMIC"])["POLY_GRAPH"];
			UIElement_PG->reset();
		}

		void SwitchPersonalUse()
		{//PERSONALUSE
			auto UIElement_Butt = (button*)(*(*wh)["SMIC"])["PERSONALUSE"];
			ForPersonalUse ^= true;
			if (ForPersonalUse)
				UIElement_Butt->safe_string_replace(".csv");
			else
				UIElement_Butt->safe_string_replace(".atraw");
		}

		void ExportTG()
		{
			worker_singleton<struct info_collection>::instance().push([]()
			{
				wh->main_window_id = "SMIC";
				wh->disable_all_windows();
				auto InfoLine = (text_box*)(*(*wh)["SMIC"])["FLL"];
				InfoLine->safe_string_replace("graph A is exporting...");
				std::ofstream out(SMICptr->filename + L".tg.csv");
				out << "tick" << CSV_DELIM << "tempo" << '\n';
				for (auto& cur_pair : SMICptr->tempo_map)
					out << cur_pair.first << CSV_DELIM << cur_pair.second << '\n';
				out.close();
				wh->main_window_id = "MAIN";
				wh->enable_window("MAIN");
				wh->enable_window("SMIC");
				InfoLine->safe_string_replace("graph A was successfully exported...");
			});
		}

		void ExportAll()
		{
			worker_singleton<struct info_collection>::instance().push([]()
			{
				wh->main_window_id = "SMIC";
				wh->disable_all_windows();
				auto InfoLine = (text_box*)(*(*wh)["SMIC"])["FLL"];
				InfoLine->safe_string_replace("Collecting data for exporting...");

				using line_data = struct
				{
					std::int64_t polyphony;
					double Seconds;
					double Tempo;
				};

				std::int64_t polyphony = 0;
				std::uint16_t ppq = SMICptr->ppq;
				std::int64_t last_tick = 0;
				std::string header = "";
				double tempo = 0;
				double seconds = 0;
				double seconds_per_tick = 0;
				header = (
					"Tick" + CSV_DELIM
					+ "Polyphony" + CSV_DELIM
					+ "Time(seconds)" + CSV_DELIM
					+ "Tempo"
					+ "\n");
				btree::btree_map<std::int64_t, line_data> info;
				for (auto& cur_pair : SMICptr->polyphony)
					info[cur_pair.first] = line_data({
						cur_pair.second, 0., 0.
						});
				auto it_ptree = SMICptr->polyphony.begin();
				for (auto cur_pair : SMICptr->tempo_map)
				{
					while (it_ptree != SMICptr->polyphony.end() && it_ptree->first < cur_pair.first)
					{
						seconds += seconds_per_tick * (it_ptree->first - last_tick);
						last_tick = it_ptree->first;
						auto& t = info[it_ptree->first];
						polyphony = t.polyphony;
						t.Seconds = seconds;
						t.Tempo = tempo;
						++it_ptree;
					}
					if (it_ptree->first == cur_pair.first)
						info[it_ptree->first].Tempo = cur_pair.second;
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
				while (it_ptree != SMICptr->polyphony.end())
				{
					seconds += seconds_per_tick * (it_ptree->first - last_tick);
					last_tick = it_ptree->first;
					auto& t = info[it_ptree->first];
					polyphony = t.polyphony;
					t.Seconds = seconds;
					t.Tempo = tempo;
					++it_ptree;
				}
				std::ofstream out(SMICptr->filename + ((ForPersonalUse) ? L".a.csv" : L".atraw"),
					((ForPersonalUse) ? (std::ios::out) : (std::ios::out | std::ios::binary))
				);
				if (ForPersonalUse)
				{
					out << header;
					for (auto& cur_pair : info)
					{
						out << cur_pair.first << CSV_DELIM
							<< cur_pair.second.polyphony << CSV_DELIM
							<< cur_pair.second.Seconds << CSV_DELIM
							<< cur_pair.second.Tempo << std::endl;
					}
				}
				else
				{
					for (auto& cur_pair : info)
					{
						out.write((const char*)(&cur_pair.first), 8);
						out.write((const char*)(&cur_pair.second.polyphony), 8);
						out.write((const char*)(&cur_pair.second.Seconds), 8);
						out.write((const char*)(&cur_pair.second.Tempo), 8);
					}
				}
				out.close();
				wh->main_window_id = "MAIN";
				wh->enable_window("MAIN");
				wh->enable_window("SMIC");
				InfoLine->safe_string_replace("graph B was successfully exported...");
			});
		}

		void DiffirentiateTicks()
		{
			worker_singleton<struct info_collection>::instance().push([]()
			{
				wh->main_window_id = "SMIC";
				wh->disable_all_windows();
				auto InfoLine = (*(*wh)["SMIC"])["FLL"];
				auto UITicks = (input_field*)(*(*wh)["SMIC"])["INT_TIC"];
				auto UIOutput = (text_box*)(*(*wh)["SMIC"])["ANSWER"];
				InfoLine->safe_string_replace("Integration has begun");

				std::int64_t ticks_limit = 0;
				ticks_limit = std::stoi(UITicks->get_current_input("0"));

				std::int64_t prev_tick = 0, cur_tick = 0;
				double cur_seconds = 0;
				double prev_second = 0;
				double ppq = SMICptr->ppq;
				double prev_tempo = 120;
				std::int64_t last_tick = (*SMICptr->tempo_map.rbegin()).first;

				for (auto& cur_pair : SMICptr->tempo_map/*; cur_pair != SMICptr->tempo_map.end(); cur_pair++*/)
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

				UIOutput->safe_string_replace(
					"Min: " + std::to_string((int)(minutes_ans)) +
					"\nSec: " + std::to_string((int)(seconds_ans)) +
					"\nMsec: " + std::to_string((int)(milliseconds_ans))
				);

				wh->main_window_id = "MAIN";
				wh->enable_window("MAIN");
				wh->enable_window("SMIC");
				InfoLine->safe_string_replace("Integration was succsessfully finished");
			});
		}

		void IntegrateTime()
		{
			worker_singleton<struct info_collection>::instance().push([]()
			{
				wh->main_window_id = "SMIC";
				wh->disable_all_windows();
				auto InfoLine = (*(*wh)["SMIC"])["FLL"];
				auto UIMinutes = (input_field*)(*(*wh)["SMIC"])["INT_MIN"];
				auto UISeconds = (input_field*)(*(*wh)["SMIC"])["INT_SEC"];
				auto UIMilliseconds = (input_field*)(*(*wh)["SMIC"])["INT_MSC"];
				auto UIOutput = (text_box*)(*(*wh)["SMIC"])["ANSWER"];
				InfoLine->safe_string_replace("Integration has begun");

				double seconds_limit = 0;
				seconds_limit += std::stoi(UIMinutes->get_current_input("0")) * 60.;
				seconds_limit += std::stoi(UISeconds->get_current_input("0"));
				seconds_limit += std::stoi(UIMilliseconds->get_current_input("0")) / 1000.;

				std::int64_t prev_tick = 0, cur_tick = 0;
				double cur_seconds = 0;
				double prev_second = 0;
				double ppq = SMICptr->ppq;
				double prev_tempo = 120;
				std::int64_t last_tick = (*SMICptr->tempo_map.rbegin()).first;
				for (auto& cur_pair : SMICptr->tempo_map/*; cur_pair != SMICptr->tempo_map.end(); cur_pair++*/)
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

				UIOutput->safe_string_replace("Tick: " + std::to_string(tick));

				wh->main_window_id = "MAIN";
				wh->enable_window("MAIN");
				wh->enable_window("SMIC");
				InfoLine->safe_string_replace("Integration was succsessfully finished");
			});
		}
	}

	void OnApplySettings()
	{
		if (currentID < 0 && currentID >= _Data.Files.size())
		{
			throw_alert_warning("You cannot apply current settings to file with ID " + std::to_string(currentID));
			return;
		}

		std::int32_t T;
		std::string CurStr = "";
		auto SMPASptr = (*wh)["SMPAS"];
		auto OSptr = (*wh)["OTHER_SETS"];

		CurStr = ((input_field*)(*SMPASptr)["PPQN"])->get_current_input("0");
		if (CurStr.size())
		{
			T = std::stoi(CurStr);
			if (T)_Data[currentID].NewPPQN = T;
			else _Data[currentID].NewPPQN = _Data.GlobalPPQN;
		}

		CurStr = ((input_field*)(*SMPASptr)["TEMPO"])->get_current_input("0");
		if (CurStr.size())
		{
			float F = stof(CurStr);
			_Data[currentID].NewTempo = F;
		}

		CurStr = ((input_field*)(*SMPASptr)["OFFSET"])->get_current_input("0");
		if (CurStr.size())
		{
			T = stoll(CurStr);
			_Data[currentID].OffsetTicks = T;
		}

		CurStr = ((input_field*)(*SMPASptr)["GROUPID"])->get_current_input("0");
		if (CurStr.size())
		{
			T = stoi(CurStr);
			if (T != _Data[currentID].GroupID)
			{
				_Data[currentID].GroupID = T;
				throw_alert_warning("Manual GroupID editing might cause significant drop of processing perfomance!");
			}
		}

		CurStr = ((input_field*)(*SMPASptr)["SELECT_START"])->get_current_input("0");
		if (CurStr.size())
		{
			T = stoll(CurStr);
			_Data[currentID].SelectionStart = T;
		}

		CurStr = ((input_field*)(*SMPASptr)["SELECT_LENGTH"])->get_current_input("-1");
		if (CurStr.size())
		{
			T = stoll(CurStr);
			_Data[currentID].SelectionLength = T;
		}

		_Data[currentID].allow_legacy_rsb_meta_interaction = (((checkbox*)(*OSptr)["LEGACY_META_RSB_BEHAVIOR"])->state);

		if (_Data[currentID].allow_legacy_rsb_meta_interaction)
			std::cout << "WARNING: Legacy way of treating running status events can also allow major corruptions of midi structure!" << std::endl;

		_Data[currentID].SetBoolSetting(_BoolSettings::remove_empty_tracks, (((checkbox*)(*SMPASptr)["BOOL_REM_TRCKS"])->state));
		_Data[currentID].SetBoolSetting(_BoolSettings::remove_remnants, (((checkbox*)(*SMPASptr)["BOOL_REM_REM"])->state));

		_Data[currentID].SetBoolSetting(_BoolSettings::all_instruments_to_piano, (((checkbox*)(*OSptr)["BOOL_PIANO_ONLY"])->state));
		_Data[currentID].SetBoolSetting(_BoolSettings::ignore_tempos, (((checkbox*)(*OSptr)["BOOL_IGN_TEMPO"])->state));
		_Data[currentID].SetBoolSetting(_BoolSettings::ignore_pitches, (((checkbox*)(*OSptr)["BOOL_IGN_PITCH"])->state));
		_Data[currentID].SetBoolSetting(_BoolSettings::ignore_notes, (((checkbox*)(*OSptr)["BOOL_IGN_NOTES"])->state));
		_Data[currentID].SetBoolSetting(_BoolSettings::ignore_all_but_tempos_notes_and_pitch, (((checkbox*)(*OSptr)["BOOL_IGN_ALL_EX_TPS"])->state));

		_Data[currentID].SetBoolSetting(_BoolSettings::enable_important_filter, (((checkbox*)(*OSptr)["IMP_FLT_ENABLE"])->state));
		_Data[currentID].SetBoolSetting(_BoolSettings::imp_filter_allow_notes, (((checkbox*)(*OSptr)["IMP_FLT_NOTES"])->state));
		_Data[currentID].SetBoolSetting(_BoolSettings::imp_filter_allow_tempo, (((checkbox*)(*OSptr)["IMP_FLT_TEMPO"])->state));
		_Data[currentID].SetBoolSetting(_BoolSettings::imp_filter_allow_pitch, (((checkbox*)(*OSptr)["IMP_FLT_PITCH"])->state));
		_Data[currentID].SetBoolSetting(_BoolSettings::imp_filter_allow_progc, (((checkbox*)(*OSptr)["IMP_FLT_PROGC"])->state));
		_Data[currentID].SetBoolSetting(_BoolSettings::imp_filter_allow_other, (((checkbox*)(*OSptr)["IMP_FLT_OTHER"])->state));

		_Data[currentID].EnableZeroVelocity = (((checkbox*)(*OSptr)["ENABLE_ZERO_VELOCITY"])->state);
		_Data[currentID].AllowSysex = (((checkbox*)(*OSptr)["ALLOW_SYSEX"])->state);

		_Data[currentID].RSBCompression = ((checkbox*)(*SMPASptr)["RSB_COMPRESS"])->state;
		_Data[currentID].ChannelsSplit = ((checkbox*)(*SMPASptr)["SPLIT_TRACKS"])->state;
		_Data[currentID].CollapseMIDI = ((checkbox*)(*SMPASptr)["COLLAPSE_MIDI"])->state;
		_Data[currentID].ApplyOffsetAfter = ((checkbox*)(*SMPASptr)["APPLY_OFFSET_AFTER"])->state; 
		auto& inplaceMergeState = ((checkbox*)(*SMPASptr)["INPLACE_MERGE"])->state;

		inplaceMergeState &= !_Data[currentID].RSBCompression;
		_Data[currentID].InplaceMergeEnabled = inplaceMergeState;
	}

	void OnApplyBS2A()
	{
		if (currentID < 0 && currentID >= _Data.Files.size())
		{
			throw_alert_warning("You cannot apply current settings to file with ID " + std::to_string(currentID));
			return;
		}

		OnApplySettings();

		for (auto Y = _Data.Files.begin(); Y != _Data.Files.end(); ++Y)
		{
			DefaultBoolSettings = Y->BoolSettings = _Data[currentID].BoolSettings;
			_Data.InplaceMergeFlag = Y->InplaceMergeEnabled = _Data[currentID].InplaceMergeEnabled;
			Y->allow_legacy_rsb_meta_interaction = _Data[currentID].allow_legacy_rsb_meta_interaction;
			Y->CollapseMIDI = _Data[currentID].CollapseMIDI;
			Y->ApplyOffsetAfter = _Data[currentID].ApplyOffsetAfter;
			Y->AllowSysex = _Data[currentID].AllowSysex;
			Y->ChannelsSplit = _Data[currentID].ChannelsSplit;
			Y->RSBCompression = _Data[currentID].RSBCompression;
		}
	}

	namespace CutAndTranspose
	{
		std::uint8_t TMax = 0, TMin = 0;
		std::int16_t TTransp = 0;

		void OnCaT()
		{
			auto Wptr = (*wh)["CAT"];
			auto CATptr = (cut_and_transpose_piano*)((*Wptr)["CAT_ITSELF"]);
			if (!_Data[currentID].KeyMap)
				_Data[currentID].KeyMap = std::make_shared<cut_and_transpose>(0, 127, 0);
			CATptr->piano_transform = _Data[currentID].KeyMap;
			CATptr->update_info();
			wh->enable_window("CAT");
		}

		void OnReset()
		{
			auto Wptr = (*wh)["CAT"];
			auto CATptr = (cut_and_transpose_piano*)((*Wptr)["CAT_ITSELF"]);
			CATptr->piano_transform->max_val = 255;
			CATptr->piano_transform->min_val = 0;
			CATptr->piano_transform->transpose_val = 0;
			CATptr->update_info();
		}

		void OnCDT128()
		{
			auto Wptr = (*wh)["CAT"];
			auto CATptr = (cut_and_transpose_piano*)((*Wptr)["CAT_ITSELF"]);
			CATptr->piano_transform->max_val = 127;
			CATptr->piano_transform->min_val = 0;
			CATptr->piano_transform->transpose_val = 0;
			CATptr->update_info();
		}

		void On0_127to128_255()
		{
			auto Wptr = (*wh)["CAT"];
			auto CATptr = (cut_and_transpose_piano*)((*Wptr)["CAT_ITSELF"]);
			CATptr->piano_transform->max_val = 127;
			CATptr->piano_transform->min_val = 0;
			CATptr->piano_transform->transpose_val = 128;
			CATptr->update_info();
		}

		void OnCopy()
		{
			auto Wptr = (*wh)["CAT"];
			auto CATptr = (cut_and_transpose_piano*)((*Wptr)["CAT_ITSELF"]);
			TMax = CATptr->piano_transform->max_val;
			TMin = CATptr->piano_transform->min_val;
			TTransp = CATptr->piano_transform->transpose_val;
		}

		void OnPaste()
		{
			auto Wptr = (*wh)["CAT"];
			auto CATptr = (cut_and_transpose_piano*)((*Wptr)["CAT_ITSELF"]);
			CATptr->piano_transform->max_val = TMax;
			CATptr->piano_transform->min_val = TMin;
			CATptr->piano_transform->transpose_val = TTransp;
			CATptr->update_info();
		}

		void OnDelete()
		{
			auto Wptr = (*wh)["CAT"];
			((cut_and_transpose_piano*)((*Wptr)["CAT_ITSELF"]))->piano_transform = nullptr;
			wh->disable_window("CAT");
			_Data[currentID].KeyMap = nullptr;
		}
	}

	namespace VolumeMap
	{
		void OnVolMap()
		{
			auto Wptr = (*wh)["VM"];
			auto VM = ((volume_graph*)(*Wptr)["VM_PLC"]);
			auto IFDeg = ((input_field*)(*Wptr)["VM_DEGREE"]);
			((button*)(*Wptr)["VM_SETMODE"])->safe_string_replace("Single");
			IFDeg->update_input_string("1");
			IFDeg->current_string.clear();
			VM->active_setting = 0;
			VM->hovered = 0;
			VM->re_put_mode = 0;
			if (!_Data[currentID].VolumeMap)
				_Data[currentID].VolumeMap = std::make_shared<polyline_converter<std::uint8_t, std::uint8_t>>();
			VM->plc_bb = _Data[currentID].VolumeMap;
			wh->enable_window("VM");
		}

		void OnDegreeShape()
		{
			auto Wptr = (*wh)["VM"];
			auto VM = ((volume_graph*)(*Wptr)["VM_PLC"]);
			if (VM->plc_bb)
			{
				auto IFDeg = ((input_field*)(*Wptr)["VM_DEGREE"]);
				float Degree = std::stof(IFDeg->get_current_input("0"));
				VM->plc_bb->map.clear();
				VM->plc_bb->map[127] = 127;
				for (int i = 0; i < 128; i++) 
					VM->plc_bb->insert(i, std::ceil(std::pow(i / 127., Degree) * 127.));
			}
			else
				throw_alert_error("If you see this message, some error might have happen, since PLC_bb is null");
		}

		void OnSimplify()
		{
			auto Wptr = (*wh)["VM"];
			auto VM = ((volume_graph*)(*Wptr)["VM_PLC"]);
			if (VM->plc_bb)
			{
				VM->make_map_more_simple();
			}
			else
				throw_alert_error("If you see this message, some error might have happen, since PLC_bb is null");
		}

		void OnTrace()
		{
			auto Wptr = (*wh)["VM"];
			auto VM = ((volume_graph*)(*Wptr)["VM_PLC"]);
			if (VM->plc_bb)
			{
				if (VM->plc_bb->map.empty())return;
				std::uint8_t C[256]{};
				for (int i = 0; i < 255; i++)
					C[i] = VM->plc_bb->at(i);
				for (int i = 0; i < 255; i++)
					VM->plc_bb->map[i] = C[i];
			}
			else
				throw_alert_error("If you see this message, some error might have happen, since PLC_bb is null");
		}

		void OnSetModeChange() 
		{
			auto Wptr = (*wh)["VM"];
			auto VM = ((volume_graph*)(*Wptr)["VM_PLC"]);
			if (VM->plc_bb)
			{
				VM->re_put_mode = !VM->re_put_mode;
				((button*)(*Wptr)["VM_SETMODE"])->safe_string_replace(((VM->re_put_mode) ? "Double" : "Single"));
			}
			else
				throw_alert_error("If you see this message, some error might have happen, since PLC_bb is null");
		}

		void OnErase()
		{
			auto Wptr = (*wh)["VM"];
			auto VM = ((volume_graph*)(*Wptr)["VM_PLC"]);
			if (VM->plc_bb)
				VM->plc_bb->map.clear();
			else
				throw_alert_error("If you see this message, some error might have happen, since PLC_bb is null");
		}

		void OnDelete()
		{
			if (_Data[currentID].VolumeMap)
				_Data[currentID].VolumeMap = nullptr;
			auto Wptr = (*wh)["VM"];
			auto VM = ((volume_graph*)(*Wptr)["VM_PLC"]);
			VM->plc_bb = nullptr;
			wh->disable_window("VM");
		}
	}
	void OnPitchMap()
	{
		throw_alert_error("Having hard time thinking of how to implement it...\nNot available... yet...");
	}
}

void OnRem()
{
	worker_singleton<struct midi_file_list>::instance().push([](){
		auto ptr = _WH_t<selectable_properted_list>("MAIN", "List");
		for (auto ID = ptr->selected_id.rbegin(); ID != ptr->selected_id.rend(); ++ID)
			_Data.RemoveByID(*ID);
		ptr->remove_selected();
		wh->disable_all_windows();
		_Data.SetGlobalPPQN();
		_Data.ResolveSubdivisionProblem_GroupIDAssign();
	});
}

void OnRemAll()
{
	worker_singleton<struct midi_file_list>::instance().push([](){
		auto ptr = _WH_t<selectable_properted_list>("MAIN", "List");
		wh->disable_all_windows();
		while (_Data.Files.size())
		{
			_Data.RemoveByID(0);
			ptr->safe_remove_string_by_id(0);
		}
		//_Data.SetGlobalPPQN();
		//_Data.ResolveSubdivisionProblem_GroupIDAssign();
	});
}

void OnSubmitGlobalPPQN()
{
	auto pptr = (*wh)["PROMPT"];
	std::string t = ((input_field*)(*pptr)["FLD"])->get_current_input("0");
	std::uint16_t PPQN = (t.size()) ? stoi(t) : _Data.GlobalPPQN;
	_Data.SetGlobalPPQN(PPQN, true);
	wh->disable_window("PROMPT");
	//PropsAndSets::OpenFileProperties(PropsAndSets::currentID);
}

void OnGlobalPPQN()
{
	wh->throw_prompt(
		"New value will be assigned to every MIDI\n(in settings)",
		"Global PPQN",
		OnSubmitGlobalPPQN,
		_Align::center,
		input_field::Type::NaturalNumbers,
		std::to_string(_Data.GlobalPPQN),
		5);
}

void OnSubmitGlobalOffset()
{
	auto pptr = (*wh)["PROMPT"];
	std::string t = ((input_field*)(*pptr)["FLD"])->get_current_input("0");
	std::uint32_t O = (t.size()) ? std::stoi(t) : _Data.GlobalOffset;
	_Data.SetGlobalOffset(O);
	wh->disable_window("PROMPT");
}

void OnGlobalOffset()
{
	wh->throw_prompt(
		"Sets new global offset",
		"Global Offset",
		OnSubmitGlobalOffset,
		_Align::center,
		input_field::Type::WholeNumbers,
		std::to_string(_Data.GlobalOffset),
		10);
}

void OnSubmitGlobalTempo()
{
	auto pptr = (*wh)["PROMPT"];
	std::string t = ((input_field*)(*pptr)["FLD"])->get_current_input("0");
	float Tempo = (t.size()) ? std::stof(t) : _Data.GlobalNewTempo;
	_Data.SetGlobalTempo(Tempo);
	wh->disable_window("PROMPT");
}

void OnGlobalTempo()
{
	wh->throw_prompt("Sets specific tempo value to every MIDI\n(in settings)", "Global S. Tempo\0", OnSubmitGlobalTempo, _Align::center, input_field::Type::FP_PositiveNumbers, std::to_string(_Data.GlobalNewTempo), 8);
}

void _OnResolve()
{
	_Data.ResolveSubdivisionProblem_GroupIDAssign();
}

void OnRemVolMaps()
{
	((volume_graph*)(*((*wh)["VM"]))["VM_PLC"])->plc_bb = NULL;
	wh->disable_window("VM");
	for (int i = 0; i < _Data.Files.size(); i++)
		_Data[i].VolumeMap = nullptr;
}
void OnRemCATs()
{
	wh->disable_window("CAT");
	for (int i = 0; i < _Data.Files.size(); i++)
		_Data[i].KeyMap = nullptr;
}
void OnRemPitchMaps()
{
	//wh->disable_window("CAT");
	throw_alert_warning("Currently pitch maps can not be created and/or deleted :D");
	for (int i = 0; i < _Data.Files.size(); i++)
		_Data[i].PitchBendMap = nullptr;
}
void OnRemAllModules()
{
	OnRemVolMaps();
	OnRemCATs();
}

namespace Settings
{
	INT ShaderMode = 0;
	WinReg::RegKey RegestryAccess;

	void OnSettings()
	{
		wh->enable_window("APP_SETTINGS");//_Data.DetectedThreads
		//wh->throw_alert("Please read the docs! Changing some of these settings might cause graphics driver failure!","Warning!",special_signs::draw_ex_triangle,1,0x007FFFFF,0x7F7F7FFF);
		auto pptr = (*wh)["APP_SETTINGS"];
		((input_field*)(*pptr)["AS_BCKGID"])->update_input_string(std::to_string(ShaderMode));
		((input_field*)(*pptr)["AS_ROT_ANGLE"])->update_input_string(std::to_string(dumb_rotation_angle));
		((input_field*)(*pptr)["AS_THREADS_COUNT"])->update_input_string(std::to_string(_Data.DetectedThreads));

		((checkbox*)((*pptr)["BOOL_REM_TRCKS"]))->state = DefaultBoolSettings & _BoolSettings::remove_empty_tracks;
		((checkbox*)((*pptr)["BOOL_REM_REM"]))->state = DefaultBoolSettings & _BoolSettings::remove_remnants;
		((checkbox*)((*pptr)["BOOL_PIANO_ONLY"]))->state = DefaultBoolSettings & _BoolSettings::all_instruments_to_piano;
		((checkbox*)((*pptr)["BOOL_IGN_TEMPO"]))->state = DefaultBoolSettings & _BoolSettings::ignore_tempos;
		((checkbox*)((*pptr)["BOOL_IGN_PITCH"]))->state = DefaultBoolSettings & _BoolSettings::ignore_pitches;
		((checkbox*)((*pptr)["BOOL_IGN_NOTES"]))->state = DefaultBoolSettings & _BoolSettings::ignore_notes;
		((checkbox*)((*pptr)["BOOL_IGN_ALL_EX_TPS"]))->state = DefaultBoolSettings & _BoolSettings::ignore_all_but_tempos_notes_and_pitch;

		((checkbox*)((*pptr)["SPLIT_TRACKS"]))->state = _Data.ChannelsSplit;
		((checkbox*)((*pptr)["RSB_COMPRESS"]))->state = _Data.RSBCompression;
		((checkbox*)((*pptr)["COLLAPSE_MIDI"]))->state = _Data.CollapseMIDI; 
		((checkbox*)((*pptr)["APPLY_OFFSET_AFTER"]))->state = _Data.ApplyOffsetAfter;
		
		((checkbox*)((*pptr)["INPLACE_MERGE"]))->state = _Data.InplaceMergeFlag;
		((checkbox*)((*pptr)["AUTOUPDATECHECK"]))->state = check_autoupdates;
	}

	void OnSetApply()
	{
		bool isRegestryOpened = false;
		try
		{
			Settings::RegestryAccess.Open(HKEY_CURRENT_USER, default_reg_path);
			isRegestryOpened = true;
		}
		catch (...)
		{
			std::cout << "RK opening failed\n";
		}
		auto pptr = (*wh)["APP_SETTINGS"];
		std::string T;

		T = ((input_field*)(*pptr)["AS_BCKGID"])->get_current_input("0");
		std::cout << "AS_BCKGID " << T << std::endl;
		if (T.size())
		{
			ShaderMode = std::stoi(T);
			if (isRegestryOpened)TRY_CATCH(RegestryAccess.SetDwordValue(L"AS_BCKGID", ShaderMode); , "Failed on setting AS_BCKGID")
		}
		std::cout << ShaderMode << std::endl;

		T = ((input_field*)(*pptr)["AS_ROT_ANGLE"])->get_current_input("0");
		std::cout << "ROT_ANGLE " << T << std::endl;
		if (T.size() && !is_fonted)
			dumb_rotation_angle = stof(T);
		std::cout << dumb_rotation_angle << std::endl;

		T = ((input_field*)(*pptr)["AS_THREADS_COUNT"])->get_current_input(std::to_string(_Data.DetectedThreads));
		std::cout << "AS_THREADS_COUNT " << T << std::endl;
		if (T.size())
		{
			_Data.DetectedThreads = stoi(T);
			_Data.ResolveSubdivisionProblem_GroupIDAssign();
			if (isRegestryOpened)TRY_CATCH(RegestryAccess.SetDwordValue(L"AS_THREADS_COUNT", _Data.DetectedThreads); , "Failed on setting AS_THREADS_COUNT")
		}
		std::cout << _Data.DetectedThreads << std::endl;

		DefaultBoolSettings = (DefaultBoolSettings & (~_BoolSettings::remove_empty_tracks)) | (_BoolSettings::remove_empty_tracks * (!!((checkbox*)(*pptr)["BOOL_REM_TRCKS"])->state));
		DefaultBoolSettings = (DefaultBoolSettings & (~_BoolSettings::remove_remnants)) | (_BoolSettings::remove_remnants * (!!((checkbox*)(*pptr)["BOOL_REM_REM"])->state));
		DefaultBoolSettings = (DefaultBoolSettings & (~_BoolSettings::all_instruments_to_piano)) | (_BoolSettings::all_instruments_to_piano * (!!((checkbox*)(*pptr)["BOOL_PIANO_ONLY"])->state));
		DefaultBoolSettings = (DefaultBoolSettings & (~_BoolSettings::ignore_tempos)) | (_BoolSettings::ignore_tempos * (!!((checkbox*)(*pptr)["BOOL_IGN_TEMPO"])->state));
		DefaultBoolSettings = (DefaultBoolSettings & (~_BoolSettings::ignore_pitches)) | (_BoolSettings::ignore_pitches * (!!((checkbox*)(*pptr)["BOOL_IGN_PITCH"])->state));
		DefaultBoolSettings = (DefaultBoolSettings & (~_BoolSettings::ignore_notes)) | (_BoolSettings::ignore_notes * (!!((checkbox*)(*pptr)["BOOL_IGN_NOTES"])->state));
		DefaultBoolSettings = (DefaultBoolSettings & (~_BoolSettings::ignore_all_but_tempos_notes_and_pitch)) | (_BoolSettings::ignore_all_but_tempos_notes_and_pitch * (!!((checkbox*)(*pptr)["BOOL_IGN_ALL_EX_TPS"])->state));

		check_autoupdates = ((checkbox*)(*pptr)["AUTOUPDATECHECK"])->state;

		_Data.ChannelsSplit = ((checkbox*)((*pptr)["SPLIT_TRACKS"]))->state;
		_Data.RSBCompression = ((checkbox*)((*pptr)["RSB_COMPRESS"]))->state;

		_Data.CollapseMIDI = ((checkbox*)((*pptr)["COLLAPSE_MIDI"]))->state;
		_Data.ApplyOffsetAfter = ((checkbox*)((*pptr)["APPLY_OFFSET_AFTER"]))->state;

		if (isRegestryOpened)
		{
			TRY_CATCH(RegestryAccess.SetDwordValue(L"AUTOUPDATECHECK", check_autoupdates);, "Failed on setting AUTOUPDATECHECK")
			TRY_CATCH(RegestryAccess.SetDwordValue(L"SPLIT_TRACKS", _Data.ChannelsSplit); , "Failed on setting SPLIT_TRACKS")
			TRY_CATCH(RegestryAccess.SetDwordValue(L"COLLAPSE_MIDI", _Data.CollapseMIDI);, "Failed on setting COLLAPSE_MIDI")
			TRY_CATCH(RegestryAccess.SetDwordValue(L"APPLY_OFFSET_AFTER", _Data.CollapseMIDI);, "Failed on setting APPLY_OFFSET_AFTER")
			//TRY_CATCH(RegestryAccess.SetDwordValue(L"RSB_COMPRESS", check_autoupdates);, "Failed on setting RSB_COMPRESS")
			TRY_CATCH(RegestryAccess.SetDwordValue(L"DEFAULT_BOOL_SETTINGS", DefaultBoolSettings);, "Failed on setting DEFAULT_BOOL_SETTINGS")
			TRY_CATCH(RegestryAccess.SetDwordValue(L"FONTSIZE_POST1P4", lFontSymbolsInfo::Size); , "Failed on setting FONTSIZE_POST1P4")
			TRY_CATCH(RegestryAccess.SetDwordValue(L"FLOAT_FONTHTW_POST1P4", *(std::uint32_t*)(&lFONT_HEIGHT_TO_WIDTH)); , "Failed on setting FLOAT_FONTHTW_POST1P4")
		}

		_Data.InplaceMergeFlag = (((checkbox*)(*pptr)["INPLACE_MERGE"])->state);
		if (isRegestryOpened)
			TRY_CATCH(RegestryAccess.SetDwordValue(L"AS_INPLACE_FLAG", _Data.InplaceMergeFlag);, "Failed on setting AS_INPLACE_FLAG")

		((input_field*)(*pptr)["AS_FONT_NAME"])->put_into_source();
		std::wstring ws(default_font_name.begin(), default_font_name.end());
		if (isRegestryOpened)
			TRY_CATCH(RegestryAccess.SetStringValue(L"COLLAPSEDFONTNAME_POST1P4", ws); , "Failed on setting COLLAPSEDFONTNAME_POST1P4")

		Settings::RegestryAccess.Close();
	}

	void ChangeIsFontedVar()
	{
		is_fonted = !is_fonted;
		set_is_fonted_var(is_fonted);
		exit(0);
	}

	void ApplyToAll()
	{
		OnSetApply();
		for (auto Y = _Data.Files.begin(); Y != _Data.Files.end(); ++Y)
		{
			Y->BoolSettings = DefaultBoolSettings;
			Y->InplaceMergeEnabled = _Data.InplaceMergeFlag && !_Data.ChannelsSplit;
			Y->ChannelsSplit = _Data.ChannelsSplit;
			Y->RSBCompression = _Data.RSBCompression;
			Y->CollapseMIDI = _Data.CollapseMIDI;
			Y->ApplyOffsetAfter = _Data.ApplyOffsetAfter;
		}
	}

	void ApplyFSWheel(double new_val)
	{
		lFontSymbolsInfo::Size = new_val;
		lFontSymbolsInfo::InitialiseFont(default_font_name);
	}

	void ApplyRelWheel(double new_val)
	{
		lFONT_HEIGHT_TO_WIDTH = new_val;
		lFontSymbolsInfo::InitialiseFont(default_font_name);
	}
}

std::pair<float, float> GetPositionForOneOf(std::int32_t Position, std::int32_t Amount, float UnitSize, float HeightRel)
{
	std::pair<float, float> coords{ 0.f, 0.f };
	std::int32_t side_count = ceil(sqrt(Amount));

	coords.first = (0 - (Position % side_count) + ((side_count - 1) / 2.f)) * UnitSize;
	coords.second = (0 - (Position / side_count) + ((side_count - 1) / 2.f)) * UnitSize * HeightRel;

	return coords;
}

void OnStart()
{
	if (_Data.Files.empty())
		return;

	worker_singleton<struct merge>::instance().push([]()
	{
		wh->main_window_id = "SMRP_CONTAINER";
		wh->disable_all_windows();
		wh->enable_window("SMRP_CONTAINER");

		GlobalMCTM = _Data.MCTM_Constructor();

		auto start_timepoint = std::chrono::high_resolution_clock::now();

		GlobalMCTM->StartProcessingMIDIs();

		auto merge_preview_container = (*wh)["SMRP_CONTAINER"];
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

		decltype(GlobalMCTM->currently_processed) currently_processed_copy;

		{
			GlobalMCTM->currently_processed_locker.lock();
			currently_processed_copy = GlobalMCTM->currently_processed;
			GlobalMCTM->currently_processed_locker.unlock();
		}

		for (size_t ID = 0; ID < currently_processed_copy.size(); ID++)
		{
			auto position = GetPositionForOneOf(ID, currently_processed_copy.size(), 140, 0.7);
			auto visualiser = std::make_unique<midi_processor_visualiser>(position.first, position.second, &system_white);
			auto& visualiser_ref = *visualiser;

			std::string element_id;
			merge_preview_container->add_ui_element(element_id = "SMRP_C" + std::to_string(ID), std::move(visualiser));

			std::thread([](std::shared_ptr<midi_collection_threaded_merger> pMCTM, midi_processor_visualiser& pVIS, std::uint32_t ID)
			{
				std::string SID = "SMRP_C" + std::to_string(ID);
				std::cout << SID << " Processing started" << std::endl;
				bool finished = false;
				while (GlobalMCTM->CheckSMRPProcessing())
				{
					GlobalMCTM->currently_processed_locker.lock();
					finished = GlobalMCTM->currently_processed[ID].second->finished;
					pVIS.set_smrp(GlobalMCTM->currently_processed[ID]);
					GlobalMCTM->currently_processed_locker.unlock();

					std::this_thread::sleep_for(std::chrono::milliseconds(66));
				}
				std::cout << SID << " Processing stopped" << std::endl;
			}, GlobalMCTM, std::ref(visualiser_ref), ID).detach();
		}

		worker_singleton<struct merge_ri_stage>::instance().push([safc_data_pointer = &_Data, merge_preview_container]()
		{
			//that's some really dumb synchronization... TODO: MAKE BETTER
			while (GlobalMCTM->CheckSMRPProcessingAndStartNextStep())
				std::this_thread::sleep_for(std::chrono::milliseconds(100));

			std::cout << "SMRP: Out from sleep\n" << std::flush;
			for (int i = 0; i <= GlobalMCTM->currently_processed.size(); i++)
				merge_preview_container->delete_ui_element_by_name("SMRP_C" + std::to_string(i));

			merge_preview_container->safe_change_position_argumented(0, 0, 0);

			(*merge_preview_container)["IM"] = 
				std::make_unique<bool_and_number_checker<decltype(GlobalMCTM->IntermediateInplaceFlag), decltype(GlobalMCTM->IITrackCount)>>
					(-100., 0., &system_white, &(GlobalMCTM->IntermediateInplaceFlag), &(GlobalMCTM->IITrackCount));
			(*merge_preview_container)["RM"] =
				std::make_unique<bool_and_number_checker<decltype(GlobalMCTM->IntermediateInplaceFlag), decltype(GlobalMCTM->IRTrackCount)>>
					(100., 0., &system_white, &(GlobalMCTM->IntermediateRegularFlag), &(GlobalMCTM->IRTrackCount));
			
			worker_singleton<struct merge_ri_stage_cleanup>::instance().push([safc_data_pointer, merge_preview_container]()
			{
				while (!GlobalMCTM->CheckRIMerge())
					std::this_thread::sleep_for(std::chrono::milliseconds(33));

				std::cout << "RI: Out from sleep!\n";
				merge_preview_container->delete_ui_element_by_name("IM");
				merge_preview_container->delete_ui_element_by_name("RM");
				merge_preview_container->safe_change_position_argumented(0, 0, 0);

				(*merge_preview_container)["FM"] = 
					std::make_unique<bool_and_number_checker<decltype(GlobalMCTM->CompleteFlag), int>>
						(0., 0., &system_white, &(GlobalMCTM->CompleteFlag), nullptr);
			});
		});

		worker_singleton<struct merge_global_cleanup>::instance().push([start_timepoint, merge_preview_container]()
		{
			auto timer_ptr = (input_field*)(*merge_preview_container)["TIMER"];

			while (!GlobalMCTM->CompleteFlag)
			{
				auto now = std::chrono::high_resolution_clock::now();
				auto difference = std::chrono::duration_cast<std::chrono::duration<double>>(now - start_timepoint);

				timer_ptr->safe_string_replace(std::to_string(difference.count()) + " s");
				std::this_thread::sleep_for(std::chrono::milliseconds(50));

				auto freeMemory = GetAvailableMemory();
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

			wh->disable_window(wh->main_window_id);
			wh->main_window_id = "MAIN";
			//wh->disable_all_windows();
			wh->enable_window("MAIN");
			//GlobalMCTM->ResetEverything();
		});
	});
}

void OnSaveTo()
{
	worker_singleton<struct save_file_dialog>::instance().push([](){
		_Data.SavePath = save_open_file_dialog(L"Save final midi to...");
		size_t Pos = _Data.SavePath.rfind(L".mid");
		if (Pos >= _Data.SavePath.size() || Pos <= _Data.SavePath.size() - 4)
			_Data.SavePath += L".mid";
	});
}

void RestoreRegSettings()
{
	bool Opened = false;
	try
	{
		Settings::RegestryAccess.Create(HKEY_CURRENT_USER, default_reg_path);
	}
	catch (...) { std::cout << "Exception thrown while creating registry key\n"; }
	try
	{
		Settings::RegestryAccess.Open(HKEY_CURRENT_USER, default_reg_path);
		Opened = true;
	}
	catch (...) { std::cout << "Exception thrown while opening RK\n"; }
	if (Opened)
	{
		try
		{
			Settings::ShaderMode = Settings::RegestryAccess.GetDwordValue(L"AS_BCKGID");
		}
		catch (...) { std::cout << "Exception thrown while restoring AS_BCKGID from registry\n"; }
		try
		{
			check_autoupdates = Settings::RegestryAccess.GetDwordValue(L"AUTOUPDATECHECK");
		}
		catch (...) { std::cout << "Exception thrown while restoring AUTOUPDATECHECK from registry\n"; }
		try
		{
			_Data.ChannelsSplit = Settings::RegestryAccess.GetDwordValue(L"SPLIT_TRACKS");
		}
		catch (...) { std::cout << "Exception thrown while restoring SPLIT_TRACKS from registry\n"; }
		try
		{
			_Data.CollapseMIDI = Settings::RegestryAccess.GetDwordValue(L"COLLAPSE_MIDI");
		}
		catch (...) { std::cout << "Exception thrown while restoring COLLAPSE_MIDI from registry\n"; }
		try
		{
			_Data.ApplyOffsetAfter = Settings::RegestryAccess.GetDwordValue(L"APPLY_OFFSET_AFTER");
		}
		catch (...) { std::cout << "Exception thrown while restoring APPLY_OFFSET_AFTER from registry\n"; }
		try
		{
			_Data.DetectedThreads = Settings::RegestryAccess.GetDwordValue(L"AS_THREADS_COUNT");
		}
		catch (...) { std::cout << "Exception thrown while restoring AS_THREADS_COUNT from registry\n"; }
		try
		{
			DefaultBoolSettings = Settings::RegestryAccess.GetDwordValue(L"DEFAULT_BOOL_SETTINGS");
		}
		catch (...) { std::cout << "Exception thrown while restoring AS_INPLACE_FLAG from registry\n"; }
		try
		{
			_Data.InplaceMergeFlag = Settings::RegestryAccess.GetDwordValue(L"AS_INPLACE_FLAG");
		}
		catch (...) { std::cout << "Exception thrown while restoring INPLACE_MERGE from registry\n"; }
		try
		{
			std::wstring ws = Settings::RegestryAccess.GetStringValue(L"COLLAPSEDFONTNAME_POST1P4");//COLLAPSEDFONTNAME
			default_font_name = std::string(ws.begin(), ws.end());
		}
		catch (...) { std::cout << "Exception thrown while restoring COLLAPSEDFONTNAME_POST1P4 from registry\n"; }
		try
		{
			lFontSymbolsInfo::Size = Settings::RegestryAccess.GetDwordValue(L"FONTSIZE_POST1P4");
		}
		catch (...) { std::cout << "Exception thrown while restoring FONTSIZE from registry\n"; }
		try
		{
			std::uint32_t B = Settings::RegestryAccess.GetDwordValue(L"FLOAT_FONTHTW_POST1P4");
			lFONT_HEIGHT_TO_WIDTH = *(float*)&B;
		}
		catch (...) { std::cout << "Exception thrown while restoring FLOAT_FONTHTW from registry\n"; }
		try
		{
			saved_midi_device_name = Settings::RegestryAccess.GetStringValue(L"MIDI_DEVICE_NAME");
		}
		catch (...) { std::cout << "Exception thrown while restoring MIDI_DEVICE_NAME from registry\n"; }
		Settings::RegestryAccess.Close();
	}
}

void OnOtherSettings()
{
	wh->enable_window("OTHER_SETS");
}

void UpdateDeviceList();

void PlayerWatchFunc()
{
	wh->enable_window("SIMPLAYER");
	auto window = (*wh)["SIMPLAYER"];
	auto textbox = (text_box*)(*window)["TEXT"];
	auto seek_to_slider = (slider*)(*window)["SEEK_TO"];

	textbox->safe_string_replace("Opening and reading first track");

	// Populate the device list
	UpdateDeviceList();

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

void OnOpenPlayer()
{
	worker_singleton<struct player_thread>::instance().push([]()
	{
		auto ptr = _WH_t<selectable_properted_list>("MAIN", "List");
		if (ptr->selected_id.empty())
			return;

		auto& id = ptr->selected_id.front();

		worker_singleton<struct player_watcher>::instance().push(PlayerWatchFunc);

		player->restore_device_by_name(saved_midi_device_name);
		player->simple_run(_Data[id].Filename);
	});
}

void UpdateDeviceList()
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

void OnDeviceSelect(int device_id)
{
	worker_singleton<struct midi_out_select>::instance().push([device_id]()
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
				Settings::RegestryAccess.Open(HKEY_CURRENT_USER, default_reg_path);
				Settings::RegestryAccess.SetStringValue(L"MIDI_DEVICE_NAME", wdevice_name);
				Settings::RegestryAccess.Close();
			}
			catch (...)
			{
				std::cout << "Exception thrown while saving MIDI_DEVICE_NAME to registry\n";
			}
		}
	});
}

void OnPlayerPauseToggle()
{
	player->toggle_pause();

	auto window = (*wh)["SIMPLAYER"];
	auto pause = (button*)(*window)["PAUSE"];

	if (player->is_paused())
		pause->safe_string_replace("\200");
	else
		pause->safe_string_replace("\202");
}

void OnPlayerStop()
{
	player->stop();

	worker_singleton<struct player_thread>::instance().push([]()
	{
		auto window = (*wh)["SIMPLAYER"];
		auto pause = (button*)(*window)["PAUSE"];
		auto textbox = (text_box*)(*window)["TEXT"];

		if (player->is_paused())
			pause->safe_string_replace("\200");
		else
			pause->safe_string_replace("\202");

		textbox->safe_string_replace("Playback was reset, please restart the player");
	});
}

void OnViewLengthChange(float value)
{
	auto window = (*wh)["SIMPLAYER"];
	auto player_viewer = (PlayerViewer*)(*window)["VIEW"];
	
	player_viewer->data->scroll_window_us = std::pow(2, value);
}

void OnUnbufferedSwitch()
{
	auto window = (*wh)["SIMPLAYER"];
	auto player_viewer = (PlayerViewer*)(*window)["VIEW"];
	auto buffering_switch = (button*)(*window)["BUFFERING_SWITCH"];

	player_viewer->data->enable_simulated_lag ^= true;

	buffering_switch->safe_string_replace(player_viewer->data->enable_simulated_lag ? "Simulate lag" : "Allow unbuffered");
}

void OnPlaybackSeekTo(float value)
{
	if (!player || !player->is_playing())
		return;
	player->seek_to(value);
}

bool simplayer_maximised = false;

struct SimplayerSavedState {
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

void ApplySimplayerMaximisedLayout()
{
	float half_w = internal_range * (wind_x / window_base_width);
	float half_h = internal_range * (wind_y / window_base_height);
	float full_width = 2.0f * half_w;
	float full_height = 2.0f * half_h + moveable_window::window_header_size;

	auto window = (*wh)["SIMPLAYER"];
	auto player_viewer = (PlayerViewer*)(*window)["VIEW"];
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

	// Compute PlayerViewer geometry
	float width_factor_change = full_width / player_viewer->data->width;
	//float keyboard_height = 40.0f;
	float notes_top = seek_y - 15;
	float notes_bottom = -half_h + player_viewer->data->last_keyboard_height * width_factor_change;
	float notes_height = notes_top - notes_bottom;
	float view_center_y = (notes_top + notes_bottom) * 0.5f;

	player_viewer->RescaleAndReposition(0, view_center_y, full_width, notes_height);
}

void SwitchMaximise()
{
	auto window = (*wh)["SIMPLAYER"];
	auto player_viewer = (PlayerViewer*)(*window)["VIEW"];
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
		state.view_x = player_viewer->xpos;
		state.view_y = player_viewer->ypos;
		state.view_width = player_viewer->data->width;
		state.view_height = player_viewer->data->height;
		state.previous_main_window_id = wh->main_window_id;

		// Apply maximized layout
		ApplySimplayerMaximisedLayout();

		// Make SIMPLAYER the sole window
		wh->main_window_id = "SIMPLAYER";
		wh->disable_all_windows();

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

		// Restore PlayerViewer
		player_viewer->RescaleAndReposition(state.view_x, state.view_y, state.view_width, state.view_height);

		// Restore window management
		wh->main_window_id = state.previous_main_window_id;
		wh->disable_all_windows();
		wh->enable_window("SIMPLAYER");

		simplayer_maximised = false;
		max_btn->safe_string_replace("Maximise");
	}
}

void Init()
{
	lFontSymbolsInfo::InitialiseFont(default_font_name, true);

	_Data.DetectedThreads =
		std::max(
			std::min((std::uint16_t)(
				(std::uint16_t)std::max(
					std::thread::hardware_concurrency(),
					1u
				)
				- 1
				),
				(std::uint16_t)(ceil(GetAvailableMemory() / 2048))
			), (std::uint16_t)1
		);

	wh = std::make_shared<windows_handler>();

	auto [maj, min, ver, build] = __versionTuple;

	constexpr unsigned BACKGROUND = 0x070E16AF;
	constexpr unsigned BACKGROUND_OPQ = 0x070E16DF;
	constexpr unsigned BORDER = 0xFFFFFF7F;
	constexpr unsigned HEADER = 0x285685CF;
	
	/*selectable_properted_list* SPL = new selectable_properted_list(BS_List_Black_Small, NULL, PropsAndSets::OpenFileProperties, -50, 172, 300, 12, 65, 30);
	moveable_window* T = new MoveableResizeableWindow(Mainwindow_name.str(), system_white, -200, 200, 400, 400, 0x3F3F3FAF, 0x7F7F7F7F, 0, [SPL](float dH, float dW, float NewHeight, float NewWidth) {
		constexpr float TopMargin = 200 - 172;
		constexpr float BottomMargin = 12;
		float SPLNewHight = (NewHeight - TopMargin - BottomMargin);
		float SPLNewWidth = SPL->width + dW;
		SPL->safe_resize(SPLNewHight, SPLNewWidth);
		});
	((MoveableResizeableWindow*)T)->assign_min_dimensions(300, 300);
	((MoveableResizeableWindow*)T)->assign_pinned_activities({
		"ADD_Butt", "REM_Butt", "REM_ALL_Butt", "GLOBAL_PPQN_Butt", "GLOBAL_OFFSET_Butt", "GLOBAL_TEMPO_Butt", "DELETE_ALL_VM", "DELETE_ALL_CAT", "DELETE_ALL_PITCHES",
		"DELETE_ALL_MODULES", "SETTINGS", "SAVE_AS", "START"
		}, MoveableResizeableWindow::PinSide::right);
	((MoveableResizeableWindow*)T)->assign_pinned_activities({ "SETTINGS", "SAVE_AS", "START" }, MoveableResizeableWindow::PinSide::bottom);*/
	moveable_window* T = new moveable_fui_window(std::format("SAFC v{}.{}.{}.{}", maj, min, ver, build), system_white, -200, 197.5f, 400, 397.5f, 300, 2.5f, 100, 100, 5, BACKGROUND, HEADER, BORDER);

	button* Butt;
	(*T)["List"] = new selectable_properted_list(BS_List_Black_Small, NULL, PropsAndSets::OpenFileProperties, -45, 172, 295, 12, 64, 30);;

	(*T)["ADD_Butt"] = new button("Add MIDIs", system_white, OnAdd, 150, 167.5, 75, 12, 1, 0x00003FAF, 0xFFFFFFFF, 0x00003FFF, 0xFFFFFFFF, 0xF7F7F7FF, nullptr, " ");
	(*T)["REM_Butt"] = new button("Remove selected", system_white, OnRem, 150, 155, 75, 12, 1, 0x3F0000AF, 0xFFFFFFFF, 0x3F0000FF, 0xFFFFFFFF, 0xF7F7F7FF, nullptr, " ");
	(*T)["REM_ALL_Butt"] = new button("Remove all", system_white, OnRemAll, 150, 142.5, 75, 12, 1, 0xAF0000AF, 0xFFFFFFFF, 0xAF0000AF, 0xFFFFFFFF, 0xF7F7F7FF, &system_white, "May cause lag");

	(*T)["OPEN_SIMPLAYER"] = new button("Open Player", system_black, OnOpenPlayer, 150, 117.5, 75, 12, 1, 0xFFFFFFAF, 0x0F0F0FFF, 0xFFFFFFFF, 0x000000FF, 0xFFFFFFFF, nullptr, " ");

	(*T)["GLOBAL_PPQN_Butt"] = new button("Global PPQN", system_white, OnGlobalPPQN, 150, 92.5, 75, 12, 1, 0xFF3F00AF, 0xFFFFFFFF, 0xFF3F00AF, 0xFFFFFFFF, 0xF7F7F7FF, nullptr, " ");
	(*T)["GLOBAL_OFFSET_Butt"] = new button("Global offset", system_white, OnGlobalOffset, 150, 80, 75, 12, 1, 0xFF7F00AF, 0xFFFFFFFF, 0xFF7F00FF, 0xFFFFFFFF, 0xF7F7F7FF, nullptr, " ");
	(*T)["GLOBAL_TEMPO_Butt"] = new button("Global tempo", system_white, OnGlobalTempo, 150, 67.5, 75, 12, 1, 0xFFAF00AF, 0xFFFFFFFF, 0xFFAF00AF, 0xFFFFFFFF, 0xF7F7F7FF, nullptr, " ");

	(*T)["DELETE_ALL_VM"] = new button("Remove vol. maps", system_white, OnRemVolMaps, 150, 42.5, 75, 12, 1,
		0x7F7F7FAF, 0xFFFFFFFF, 0x7F7F7FAF, 0xFFFFFFFF, 0xF7F7F7FF, nullptr, " ");//0xFF007FAF
	(*T)["DELETE_ALL_CAT"] = new button("Remove C&Ts", system_white, OnRemCATs, 150, 30, 75, 12, 1,
		0x7F7F7FAF, 0xFFFFFFFF, 0x7F7F7FAF, 0xFFFFFFFF, 0xF7F7F7FF, nullptr, " ");
	(*T)["DELETE_ALL_MODULES"] = new button("Remove modules", system_white, OnRemAllModules, 150, 17.5, 75, 12, 1,
		0x7F7F7FAF, 0xFFFFFFFF, 0x7F7F7FAF, 0xFFFFFFFF, 0xF7F7F7FF, nullptr, " ");

	(*T)["SETTINGS"] = new button("Settings...", system_white, Settings::OnSettings, 150, -140, 75, 12, 1,
		0x5F5F5FAF, 0xFFFFFFFF, 0x5F5F5FAF, 0xFFFFFFFF, 0xF7F7F7FF, nullptr, " ");
	(*T)["SAVE_AS"] = new button("Save as...", system_white, OnSaveTo, 150, -152.5, 75, 12, 1,
		0x3FAF00AF, 0xFFFFFFFF, 0x3FAF00AF, 0xFFFFFFFF, 0xF7F7F7FF, nullptr, " ");
	(*T)["START"] = Butt = new button("Start merging", system_white, OnStart, 150, -177.5, 75, 12, 1,
		0x000000AF, 0xFFFFFFFF, 0x000000AF, 0xFFFFFFFF, 0xF7F7F7FF, nullptr, " ");//177.5

	(*wh)["MAIN"] = T;

	T = new moveable_fui_window("Props. and sets.", system_white, -100, 100, 200, 225, 100, 2.5f, 75, 50, 3, BACKGROUND_OPQ, HEADER, BORDER);
	(*T)["FileName"] = new text_box("_", system_white, 0, 88.5 - moveable_window::window_header_size, 6, 200 - 1.5 * moveable_window::window_header_size, 7.5, 0, 0, 0, _Align::left, text_box::VerticalOverflow::cut);
	(*T)["PPQN"] = new input_field(" ", -90 + moveable_window::window_header_size, 75 - moveable_window::window_header_size, 10, 25, system_white, PropsAndSets::PPQN, 0x007FFFFF, &system_white, "PPQN is lesser than 65536.", 5, _Align::center, _Align::left, input_field::Type::NaturalNumbers);
	(*T)["TEMPO"] = new input_field(" ", -50 + moveable_window::window_header_size, 75 - moveable_window::window_header_size, 10, 45, system_white, PropsAndSets::TEMPO, 0x007FFFFF, &system_white, "Specific tempo override field", 8, _Align::center, _Align::left, input_field::Type::FP_PositiveNumbers);
	(*T)["OFFSET"] = new input_field(" ", 20 + moveable_window::window_header_size, 75 - moveable_window::window_header_size, 10, 55, system_white, PropsAndSets::OFFSET, 0x007FFFFF, &system_white, "Offset from begining in ticks", 10, _Align::center, _Align::right, input_field::Type::WholeNumbers);

	(*T)["APPLY_OFFSET_AFTER"] = new checkbox(12.5 - moveable_window::window_header_size, 75 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF7F00AF, 0x7FFF00AF, 1, 0, &system_white, _Align::center, "Apply offset after PPQ change");

	(*T)["BOOL_REM_TRCKS"] = new checkbox(-97.5 + moveable_window::window_header_size, 55 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 1, &system_white, _Align::left, "Remove empty tracks");
	(*T)["BOOL_REM_REM"] = new checkbox(-82.5 + moveable_window::window_header_size, 55 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 1, &system_white, _Align::left, "Remove merge \"remnants\"");
	(*T)["OTHER_CHECKBOXES"] = new button("Other settings", system_white, OnOtherSettings, -37.5 + moveable_window::window_header_size, 55 - moveable_window::window_header_size, 70, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F003F, 0xFF7F00FF, &system_white, "Other MIDI processing settings");

	(*T)["SPLIT_TRACKS"] = new checkbox(7.5 + moveable_window::window_header_size, 55 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF3F007F, 0x3FFF007F, 1, 0, &system_white, _Align::center, "Multichannel split");
	(*T)["RSB_COMPRESS"] = new checkbox(22.5 + moveable_window::window_header_size, 55 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF3F007F, 0x3FFF007F, 1, 0, &system_white, _Align::center, "enable RSB compression");

	(*T)["COLLAPSE_MIDI"] = new checkbox(97.5 - moveable_window::window_header_size, 35 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF7F00AF, 0x7FFF00AF, 1, 0, &system_white, _Align::right, "Collapse all tracks of a MIDI into one");

	(*T)["BOOL_APPLY_TO_ALL_MIDIS"] = Butt = new button("A2A", system_white, PropsAndSets::OnApplyBS2A, 80 - moveable_window::window_header_size, 55 - moveable_window::window_header_size, 15, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F003F, 0xFF7F00FF, &system_white, "Sets \"bool settings\" to all midis");
	Butt->tip->safe_change_position_argumented(_Align::right, 87.5 - moveable_window::window_header_size, Butt->tip->cy_pos);

	(*T)["INPLACE_MERGE"] = new checkbox(97.5 - moveable_window::window_header_size, 55 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF3F007F, 0x3FFF007F, 1, 0, &system_white, _Align::right, "Enables/disables inplace merge");

	(*T)["GROUPID"] = new input_field(" ", 92.5 - moveable_window::window_header_size, 75 - moveable_window::window_header_size, 10, 20, system_white, PropsAndSets::PPQN, 0x007FFFFF, &system_white, "Group ID...", 2, _Align::center, _Align::right, input_field::Type::NaturalNumbers);

	(*T)["MIDIINFO"] = Butt = new button("Collect info", system_white, PropsAndSets::InitializeCollecting, 20, 15 - moveable_window::window_header_size, 65, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F003F, 0xFF7F00FF, &system_white, "Collects additional info about the midi");
	Butt->tip->safe_move(-20, 0);

	(*T)["APPLY"] = new button("Apply", system_white, PropsAndSets::OnApplySettings, 87.5 - moveable_window::window_header_size, 15 - moveable_window::window_header_size, 30, 10, 1, 0x7FAFFF3F, 0xFFFFFFFF, 0xFFAF7FFF, 0xFFAF7F3F, 0xFFAF7FFF, nullptr, " ");

	(*T)["CUT_AND_TRANSPOSE"] = (Butt = new button("Cut & Transpose...", system_white, PropsAndSets::CutAndTranspose::OnCaT, 45 - moveable_window::window_header_size, 35 - moveable_window::window_header_size, 85, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F003F, 0xFF7F00FF, &system_white, "Cut and Transpose tool"));
	Butt->tip->safe_change_position_argumented(_Align::right, 100 - moveable_window::window_header_size, Butt->tip->cy_pos);
	(*T)["PITCH_MAP"] = (Butt = new button("Pitch map ...", system_white, PropsAndSets::OnPitchMap, -37.5 - moveable_window::window_header_size, 15 - moveable_window::window_header_size, 70, 10, 1, 0x7F7F7F3F, 0x7F7F7FFF, 0xFFFFFFFF, 0xFFFFFF3F, 0xFFFFFFFF, &system_white, "Allows to transform pitches"));
	Butt->tip->safe_change_position_argumented(_Align::right, 100 - moveable_window::window_header_size, Butt->tip->cy_pos);
	(*T)["VOLUME_MAP"] = (Butt = new button("Volume map ...", system_white, PropsAndSets::VolumeMap::OnVolMap, -37.5 - moveable_window::window_header_size, 35 - moveable_window::window_header_size, 70, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F003F, 0xFF7F00FF, &system_white, "Allows to transform volumes of notes"));
	Butt->tip->safe_change_position_argumented(_Align::right, 100 - moveable_window::window_header_size, Butt->tip->cy_pos);

	(*T)["SELECT_START"] = new input_field(" ", -37.5 - moveable_window::window_header_size, -5 - moveable_window::window_header_size, 10, 70, system_white, nullptr, 0x007FFFFF, &system_white, "Selection start", 13, _Align::center, _Align::right, input_field::Type::NaturalNumbers);
	(*T)["SELECT_LENGTH"] = new input_field(" ", 37.5 - moveable_window::window_header_size, -5 - moveable_window::window_header_size, 10, 70, system_white, nullptr, 0x007FFFFF, &system_white, "Selection length", 14, _Align::center, _Align::right, input_field::Type::WholeNumbers);

	(*T)["CONSTANT_PROPS"] = new text_box("_Props text example_", system_white, 0, -55 - moveable_window::window_header_size, 80 - moveable_window::window_header_size, 200 - 1.5 * moveable_window::window_header_size, 7.5, 0, 0, 1);

	(*wh)["SMPAS"] = T;//Selected midi properties and settings

	T = new moveable_fui_window("Other settings.", system_white, -75, 35, 150, 65 + moveable_window::window_header_size, 100, 2.5f, 17.5f, 17.5f, 3, BACKGROUND_OPQ, HEADER, BORDER);

	checkbox* Chk = nullptr;

	(*T)["BOOL_PIANO_ONLY"] = new checkbox(-65, 25 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 1, &system_white, _Align::left, "All instruments to piano");
	(*T)["BOOL_IGN_TEMPO"] = new checkbox(-50, 25 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 0, &system_white, _Align::left, "Remove tempo events");
	(*T)["BOOL_IGN_PITCH"] = new checkbox(-35, 25 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 0, &system_white, _Align::left, "Remove pitch events");
	(*T)["BOOL_IGN_NOTES"] = new checkbox(-20, 25 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 0, &system_white, _Align::center, "Remove notes");
	(*T)["BOOL_IGN_ALL_EX_TPS"] = new checkbox(-5, 25 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 0, &system_white, _Align::center, "Ignore other events");

	(*T)["LEGACY_META_RSB_BEHAVIOR"] = Chk = new checkbox(10, 25 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF3F007F, 0x3FFF007F, 1, false, &system_white, _Align::center, "En. legacy RSB/Meta behavior");
	Chk->tip->safe_change_position_argumented(_Align::left, -70, 15 - moveable_window::window_header_size);
	(*T)["ALLOW_SYSEX"] = new checkbox(25, 25 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF3F007F, 0x3FFF007F, 1, 0, &system_white, _Align::right, "Allow sysex events");

	(*T)["IMP_FLT_ENABLE"] = new checkbox(65, 25 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF7F1F7F, 0x5FFF007F, 1, 0, &system_white, _Align::right, "enable important event filter");

	(*T)["0VERLAY"] = new text_box("", system_white, -10, 2.5f - moveable_window::window_header_size, 23, 120, 0, 0xFFFFFF1F, 0x007FFF7F, 1);

	(*T)["IMP_FLT_NOTES"] = new checkbox(-60, 5 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 1, &system_white, _Align::left, "Important filter: piano");
	(*T)["IMP_FLT_TEMPO"] = new checkbox(-45, 5 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 0, &system_white, _Align::left, "Important filter: tempo");
	(*T)["IMP_FLT_PITCH"] = new checkbox(-30, 5 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 0, &system_white, _Align::left, "Important filter: pitch");
	(*T)["IMP_FLT_PROGC"] = Chk = new checkbox(-15, 5 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 0, &system_white, _Align::center, "Important filter: program change");
	Chk->tip->safe_change_position_argumented(_Align::left, -70, -5 - moveable_window::window_header_size);
	(*T)["IMP_FLT_OTHER"] = new checkbox(0, 5 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 0, &system_white, _Align::center, "Important filter: other");

	(*T)["ENABLE_ZERO_VELOCITY"] = new checkbox(60, 5 - moveable_window::window_header_size, 10, 0x007FFFFF, 0x00001F3F, 0x00FF00FF, 1, 0, &system_white, _Align::right, "\"enable\" zero velocity notes");

	(*T)["APPLY"] = new button("Apply", system_white, PropsAndSets::OnApplySettings, 70 - moveable_window::window_header_size, -20 - moveable_window::window_header_size, 30, 10, 1, 0x3F7FFF3F, 0xFFFFFFEF, 0x7FFF3FFF, 0x7FFF3F1F, 0x7FFF3FFF, nullptr, " ");

	(*wh)["OTHER_SETS"] = T; // Other settings

	T = new moveable_fui_window("Cut and Transpose.", system_white, -200, 50, 400, 100, 300, 2.5f, 15, 15, 2.5f, BACKGROUND_OPQ, HEADER, BORDER);
	(*T)["CAT_ITSELF"] = new cut_and_transpose_piano(0, 20 - moveable_window::window_header_size, 1, 10, nullptr);
	(*T)["CAT_SET_DEFAULT"] = new button("reset", system_white, PropsAndSets::CutAndTranspose::OnReset, -150, -10 - moveable_window::window_header_size, 40, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F003F, 0xFF7F00FF, nullptr, " ");
	(*T)["CAT_+128"] = new button("0-127 -> 128-255", system_white, PropsAndSets::CutAndTranspose::On0_127to128_255, -85, -10 - moveable_window::window_header_size, 80, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F003F, 0xFF7F00FF, nullptr, " ");
	(*T)["CAT_CDT128"] = new button("Cut down to 128", system_white, PropsAndSets::CutAndTranspose::OnCDT128, 0, -10 - moveable_window::window_header_size, 80, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F003F, 0xFF7F00FF, nullptr, " ");
	(*T)["CAT_COPY"] = new button("Copy", system_white, PropsAndSets::CutAndTranspose::OnCopy, 65, -10 - moveable_window::window_header_size, 40, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F003F, 0xFF7F00FF, nullptr, " ");
	(*T)["CAT_PASTE"] = new button("Paste", system_white, PropsAndSets::CutAndTranspose::OnPaste, 110, -10 - moveable_window::window_header_size, 40, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F003F, 0xFF7F00FF, nullptr, " ");
	(*T)["CAT_DELETE"] = new button("Delete", system_white, PropsAndSets::CutAndTranspose::OnDelete, 155, -10 - moveable_window::window_header_size, 40, 10, 1, 0xFF00FF3F, 0xFF00FFFF, 0xFFFFFFFF, 0xFF003F3F, 0xFF003FFF, nullptr, " ");

	(*wh)["CAT"] = T;

	T = new moveable_fui_window("Volume map.", system_white, -150, 150, 300, 350, 200, 2.5f, 100, 100, 2.5f, BACKGROUND_OPQ, HEADER, BORDER);
	(*T)["VM_PLC"] = new volume_graph(0, 0 - moveable_window::window_header_size, 300 - moveable_window::window_header_size * 2, 300 - moveable_window::window_header_size * 2, std::make_shared<polyline_converter<std::uint8_t, std::uint8_t>>());///todo: interface
	(*T)["VM_SSBDIIF"] = Butt = new button("Shape alike x^y", system_white, PropsAndSets::VolumeMap::OnDegreeShape, -110 + moveable_window::window_header_size, -150 - moveable_window::window_header_size, 80, 10, 1, 0xFFFFFF3F, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFAFFF3F, 0xFFAFFFFF, &system_white, "Where y is from frame bellow");///Set shape by degree in input field;
	Butt->tip->safe_change_position_argumented(_Align::left, -150 + moveable_window::window_header_size, -160 - moveable_window::window_header_size);
	(*T)["VM_DEGREE"] = new input_field("1", -140 + moveable_window::window_header_size, -170 - moveable_window::window_header_size, 10, 20, system_white, nullptr, 0x007FFFFF, nullptr, " ", 4, _Align::center, _Align::center, input_field::Type::FP_PositiveNumbers);
	(*T)["VM_ERASE"] = Butt = new button("Erase points", system_white, PropsAndSets::VolumeMap::OnErase, -35 + moveable_window::window_header_size, -150 - moveable_window::window_header_size, 60, 10, 1, 0xFFFFFF3F, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFAFFF3F, 0xFFAFFFFF, nullptr, "_");
	(*T)["VM_DELETE"] = new button("Delete map", system_white, PropsAndSets::VolumeMap::OnDelete, 30 + moveable_window::window_header_size, -150 - moveable_window::window_header_size, 60, 10, 1, 0xFFAFAF3F, 0xFFAFAFFF, 0xFFEFEFFF, 0xFF7F3F7F, 0xFF1F1FFF, nullptr, "_");
	(*T)["VM_SIMP"] = Butt = new button("Simplify map", system_white, PropsAndSets::VolumeMap::OnSimplify, -70 - moveable_window::window_header_size, -170 - moveable_window::window_header_size, 60, 10, 1, 0xFFFFFF3F, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFAFFF3F, 0xFFAFFFFF, &system_white, "Reduces amount of \"repeating\" points");
	Butt->tip->safe_change_position_argumented(_Align::left, -100 - moveable_window::window_header_size, -160 - moveable_window::window_header_size);
	(*T)["VM_TRACE"] = Butt = new button("Trace map", system_white, PropsAndSets::VolumeMap::OnTrace, -35 + moveable_window::window_header_size, -170 - moveable_window::window_header_size, 60, 10, 1, 0xFFFFFF3F, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFAFAF3F, 0xFFAFAFFF, &system_white, "Puts every point onto map");
	Butt->tip->safe_change_position_argumented(_Align::left, -65 + moveable_window::window_header_size, -160 - moveable_window::window_header_size);
	(*T)["VM_SETMODE"] = Butt = new button("Single", system_white, PropsAndSets::VolumeMap::OnSetModeChange, 30 + moveable_window::window_header_size, -170 - moveable_window::window_header_size, 40, 10, 1, 0xFFFFFF3F, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFAFAF3F, 0xFFAFAFFF, nullptr, "_");

	(*wh)["VM"] = T;

	T = new moveable_fui_window("App settings", system_white, -100, 110, 200, 230, 125, 2.5f, 50, 50, 2.5f, BACKGROUND_OPQ, HEADER, BORDER);

	(*T)["AS_BCKGID"] = new input_field(std::to_string(Settings::ShaderMode), -35, 55 - moveable_window::window_header_size, 10, 30, system_white, nullptr, 0x007FFFFF, &system_white, "Background ID", 2, _Align::center, _Align::right, input_field::Type::NaturalNumbers);

	(*T)["AS_GLOBALSETTINGS"] = new text_box("Global settings for new MIDIs", system_white, 0, 85 - moveable_window::window_header_size, 50, 200, 12, 0x007FFF1F, 0x007FFF7F, 1, _Align::center);
	(*T)["AS_APPLY"] = Butt = new button("Apply", system_white, Settings::OnSetApply, 85 - moveable_window::window_header_size, -87.5 - moveable_window::window_header_size, 40, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, nullptr, "_");
	(*T)["AS_EN_FONT"] = Butt = new button((is_fonted) ? "disable fonts" : "enable fonts", system_white, Settings::ChangeIsFontedVar, 72.5 - moveable_window::window_header_size, -67.5 - moveable_window::window_header_size, 65, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, &system_white, " ");

	auto AngleInput = new input_field(std::to_string(dumb_rotation_angle), -87.5 + moveable_window::window_header_size, 55 - moveable_window::window_header_size, 10, 30, system_white, nullptr, 0x007FFFFF, &system_white, "Rotation angle", 6, _Align::center, _Align::left, input_field::Type::FP_Any);
	AngleInput->disable(); // hide // legacy
	(*T)["AS_ROT_ANGLE"] = AngleInput;
	(*T)["AS_FONT_SIZE"] = new wheel_variable_changer(Settings::ApplyFSWheel, -37.5, -82.5, lFontSymbolsInfo::Size, 1, system_white, "Font size", "Delta", wheel_variable_changer::Type::addictable);
	(*T)["AS_FONT_P"] = new wheel_variable_changer(Settings::ApplyRelWheel, -37.5, -22.5, lFONT_HEIGHT_TO_WIDTH, 0.01, system_white, "Font rel.", "Delta", wheel_variable_changer::Type::addictable);
	(*T)["AS_FONT_NAME"] = new input_field(default_font_name, 52.5 - moveable_window::window_header_size, 55 - moveable_window::window_header_size, 10, 100, legacy_white, &default_font_name, 0x007FFFFF, &system_white, "Font name", 32, _Align::center, _Align::left, input_field::Type::text);

	(*T)["BOOL_REM_TRCKS"] = new checkbox(-97.5 + moveable_window::window_header_size, 95 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 1, &system_white, _Align::left, "Remove empty tracks");
	(*T)["BOOL_REM_REM"] = new checkbox(-82.5 + moveable_window::window_header_size, 95 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 1, &system_white, _Align::left, "Remove merge \"remnants\"");
	(*T)["BOOL_PIANO_ONLY"] = new checkbox(-67.5 + moveable_window::window_header_size, 95 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 1, &system_white, _Align::left, "All instuments to piano");
	(*T)["BOOL_IGN_TEMPO"] = new checkbox(-52.5 + moveable_window::window_header_size, 95 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 0, &system_white, _Align::left, "Ignore tempo events");
	(*T)["BOOL_IGN_PITCH"] = new checkbox(-37.5 + moveable_window::window_header_size, 95 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 0, &system_white, _Align::center, "Ignore pitch bending events");
	(*T)["BOOL_IGN_NOTES"] = new checkbox(-22.5 + moveable_window::window_header_size, 95 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 0, &system_white, _Align::center, "Ignore note events");
	(*T)["BOOL_IGN_ALL_EX_TPS"] = new checkbox(-7.5 + moveable_window::window_header_size, 95 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 0, &system_white, _Align::center, "Ignore everything except specified");
	(*T)["SPLIT_TRACKS"] = new checkbox(7.5 + moveable_window::window_header_size, 95 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF3F007F, 0x3FFF007F, 1, 0, &system_white, _Align::center, "Multichannel split");
	(*T)["RSB_COMPRESS"] = new checkbox(22.5 + moveable_window::window_header_size, 95 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF3F007F, 0x3FFF007F, 1, 0, &system_white, _Align::center, "enable RSB compression");

	(*T)["ALLOW_SYSEX"] = new checkbox(-97.5 + moveable_window::window_header_size, 75 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF3F007F, 0x3FFF007F, 1, 0, &system_white, _Align::left, "Allow sysex events");

	(*T)["BOOL_APPLY_TO_ALL_MIDIS"] = Butt = new button("A2A", system_white, Settings::ApplyToAll, 80 - moveable_window::window_header_size, 95 - moveable_window::window_header_size, 15, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F003F, 0xFF7F00FF, &system_white, "The same as A2A in MIDI's props.");
	Butt->tip->safe_change_position_argumented(_Align::right, 87.5 - moveable_window::window_header_size, Butt->tip->cy_pos);

	(*T)["INPLACE_MERGE"] = new checkbox(97.5 - moveable_window::window_header_size, 95 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF3F007F, 0x3FFF007F, 1, 0, &system_white, _Align::right, "enable/disable inplace merge");

	(*T)["COLLAPSE_MIDI"] = new checkbox(72.5 - moveable_window::window_header_size, 75 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF7F00AF, 0x7FFF00AF, 1, 0, &system_white, _Align::right, "Collapse tracks of a MIDI into one");
	(*T)["APPLY_OFFSET_AFTER"] = new checkbox(57.5 - moveable_window::window_header_size, 75 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF7F00AF, 0x7FFF00AF, 1, 0, &system_white, _Align::right, "Apply offset after PPQ change");

	(*T)["AS_THREADS_COUNT"] = new input_field(std::to_string(_Data.DetectedThreads), 92.5 - moveable_window::window_header_size, 75 - moveable_window::window_header_size, 10, 20, system_white, nullptr, 0x007FFFFF, &system_white, "Threads count", 2, _Align::center, _Align::right, input_field::Type::NaturalNumbers);

	(*T)["AUTOUPDATECHECK"] = new checkbox(-97.5 + moveable_window::window_header_size, 35 - moveable_window::window_header_size, 10, 0x007FFFFF, 0xFF3F007F, 0x3FFF007F, 1, check_autoupdates, &system_white, _Align::left, "Check for updates automatically");

	(*wh)["APP_SETTINGS"] = T;

	T = new moveable_window("SMRP Container", system_white, -300, 300, 600, 600, 0x000000CF, 0xFFFFFF7F);

	(*T)["TIMER"] = new input_field("0 s", 0, 195, 10, 50, system_white, nullptr, 0, &system_white, "Timer", 12, _Align::center, _Align::center, input_field::Type::text);

	(*wh)["SMRP_CONTAINER"] = T;

	T = new moveable_fui_window("MIDI Info Collector", system_white, -150, 200, 300, 400, 200, 1.25f, 100, 100, 5, BACKGROUND_OPQ, HEADER, BORDER);
	(*T)["FLL"] = new text_box("--File log line--", system_white, 0, -moveable_window::window_header_size + 185, 15, 285, 10, 0, 0, 0, _Align::left);
	(*T)["FEL"] = new text_box("--File error line--", system_red, 0, -moveable_window::window_header_size + 175, 15, 285, 10, 0, 0, 0, _Align::left);
	(*T)["TEMPO_GRAPH"] = new Graphing<single_midi_info_collector::tempo_graph>(
		0, -moveable_window::window_header_size + 145, 285, 50, (1. / 20000.), true, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F00FF, 0xFFFFFFFF, 0x7F7F7F7F, nullptr, system_white, false
		);
	(*T)["POLY_GRAPH"] = new Graphing<single_midi_info_collector::polyphony_graph>(
		0, -moveable_window::window_header_size + 95, 285, 50, (1. / 20000.), true, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F00FF, 0xFFFFFFFF, 0x7F7F7F7F, nullptr, system_white, false
		);
	(*T)["PG_SWITCH"] = new button("enable graph B", system_white, PropsAndSets::SMIC::EnablePG, 37.5, 60 - moveable_window::window_header_size, 70, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, &system_white, "Polyphony graph");
	(*T)["TG_SWITCH"] = new button("enable graph A", system_white, PropsAndSets::SMIC::EnableTG, -37.5, 60 - moveable_window::window_header_size, 70, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, &system_white, "Tempo graph");
	(*T)["ALL_EXP"] = new button("Export all", system_white, PropsAndSets::SMIC::ExportAll, 110, 60 - moveable_window::window_header_size, 65, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, nullptr);
	(*T)["TG_EXP"] = new button("Export Tempo", system_white, PropsAndSets::SMIC::ExportTG, -110, 60 - moveable_window::window_header_size, 65, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, nullptr);
	(*T)["TG_RESET"] = new button("reset graph A", system_white, PropsAndSets::SMIC::ResetTG, 45, 40 - moveable_window::window_header_size, 65, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, nullptr);
	(*T)["PG_RESET"] = new button("reset graph B", system_white, PropsAndSets::SMIC::ResetPG, 45, 20 - moveable_window::window_header_size, 65, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, nullptr);
	(*T)["LD_TIME_MAP"] = new button("Copy time map", system_white, PropsAndSets::SMIC::LoadTimeMap, 45, 0 - moveable_window::window_header_size, 65, 10, 1, 0x7F7F7F3F, 0x7F7F7FFF, 0xFFFFFFFF, 0x000000FF, 0xFFFFFFFF, nullptr);
	(*T)["PERSONALUSE"] = Butt = new button(".csv", system_white, PropsAndSets::SMIC::SwitchPersonalUse, 105, 40 - moveable_window::window_header_size, 45, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, &system_white, "Switches output format for `export all`");
	Butt->tip->safe_change_position_argumented(_Align::right, Butt->x_pos + Butt->width * 0.5, Butt->tip->cy_pos);
	(*T)["TOTAL_INFO"] = new text_box("----", system_white, 0, -150, 35, 285, 10, 0, 0, 0, _Align::left);
	(*T)["INT_MIN"] = new input_field("0", -132.5, 40 - moveable_window::window_header_size, 10, 20, system_white, nullptr, 0x007FFFFF, &system_white, "Minutes", 3, _Align::center, _Align::left, input_field::Type::NaturalNumbers);
	(*T)["INT_SEC"] = new input_field("0", -107.5, 40 - moveable_window::window_header_size, 10, 20, system_white, nullptr, 0x007FFFFF, &system_white, "Seconds", 2, _Align::center, _Align::left, input_field::Type::NaturalNumbers);
	(*T)["INT_MSC"] = new input_field("000", -80, 40 - moveable_window::window_header_size, 10, 25, system_white, nullptr, 0x007FFFFF, &system_white, "Milliseconds", 3, _Align::center, _Align::left, input_field::Type::NaturalNumbers);
	(*T)["INTEGRATE_TICKS"] = new button("Integrate ticks", system_white, PropsAndSets::SMIC::IntegrateTime, -27.5, 40 - moveable_window::window_header_size, 70, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, &system_white, "Result is the closest tick to that time.");
	(*T)["INT_TIC"] = new input_field("0", -105, 20 - moveable_window::window_header_size, 10, 75, system_white, nullptr, 0x007FFFFF, &system_white, "Ticks", 17, _Align::center, _Align::left, input_field::Type::NaturalNumbers);
	(*T)["INTEGRATE_TIME"] = new button("Integrate time", system_white, PropsAndSets::SMIC::DiffirentiateTicks, -27.5, 20 - moveable_window::window_header_size, 70, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, &system_white, "Result is the time of that tick.");
	(*T)["DELIM"] = new input_field(";", 137.5, 40 - moveable_window::window_header_size, 10, 7.5, system_white, &(PropsAndSets::CSV_DELIM), 0x007FFFFF, &system_white, "Delimiter", 1, _Align::center, _Align::right, input_field::Type::text);
	(*T)["ANSWER"] = new text_box("----", system_white, -66.25, -30, 25, 152.5, 10, 0, 0, 0, _Align::center, text_box::VerticalOverflow::recalibrate);

	(*wh)["SMIC"] = T;

	T = new moveable_fui_window("Simple MIDI player", system_white, /*-200, 197.5, 400, 397.5, 150, 2.5f, 75, 75, 5*/
		-200, 175 + moveable_window::window_header_size, 400, 375, 150, 2.5, 65, 65, 2.5, BACKGROUND_OPQ, HEADER, BORDER);

	(*T)["TEXT"] = new text_box("TIME", legacy_white, 0, 130 + moveable_window::window_header_size, 50, 175, 10, 0xFFFFFF1A, 0, 0, _Align(center | top), text_box::VerticalOverflow::cut);
	(*T)["PAUSE"] = new button("\202", legacy_white, OnPlayerPauseToggle, -190, 180 - moveable_window::window_header_size, 10, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, nullptr);
	(*T)["STOP"] = new button("\201", legacy_white, OnPlayerStop, -175, 180 - moveable_window::window_header_size, 10, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, nullptr);

	auto player_view = new PlayerViewer(0, -20);
	(*T)["VIEW_LEN_SLIDER"] = new slider(slider::Orientation::horizontal, -130, 180 - moveable_window::window_header_size, 65, 14, 21, log2f(player_view->data->scroll_window_us), OnViewLengthChange, 0x808080FF, 0xFFFFFFFF, 0xAACFFFFF, 0x007FFFFF, 0x808080FF, 10, 4);
	(*T)["BUFFERING_SWITCH"] = new button( 
		player_view->data->enable_simulated_lag ? "Simulate lag" : "Allow unbuffered",
		system_white, 
		OnUnbufferedSwitch,
		155, 180 - moveable_window::window_header_size, 80, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, nullptr);

	(*T)["SEEK_TO"] = new slider(slider::Orientation::horizontal, 0, 130 - moveable_window::window_header_size, 375, 0, 1, 0, OnPlaybackSeekTo, 0x808080FF, 0xFFFFFFFF, 0xAACFFFFF, 0x007FFFFF, 0x808080FF, 10, 4);

	(*T)["MAXIMISE"] = new button("Maximise", system_white, SwitchMaximise, 175, 165 - moveable_window::window_header_size, 40, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, nullptr);

	(*T)["VIEW"] = player_view;

	// Device selection label and list
	(*T)["DEVICE_LIST"] = new selectable_properted_list(
		BS_List_Black_Small,
		OnDeviceSelect,
		nullptr,  // No properties callback
		-145, 135 + moveable_window::window_header_size,  // Position: left side, below viewport
		100,  // width
		12,   // Space between items
		20,   // Max chars per line
		2     // Max visible lines
	);

	(*wh)["SIMPLAYER"] = T;

	wh->enable_window("MAIN");
	//wh->enable_window("SIMPLAYER");
	//wh->enable_window("V1WT");
	//wh->enable_window("COMPILEW"); // todo: someday fix the damn editbox...
	//wh->enable_window("SMIC");
	//wh->enable_window("OR");
	//wh->enable_window("SMRP_CONTAINER");
	//wh->enable_window("APP_SETTINGS");
	//wh->enable_window("VM");
	//wh->enable_window("CAT");
	//wh->enable_window("SMPAS");//Debug line
	//wh->enable_window("PROMPT");////DEBUUUUG
	//wh->enable_window("OTHER_SETS");

	DragAcceptFiles(hWnd, TRUE);
	OleInitialize(nullptr);

	std::cout << "Registering Drag&Drop: " << (RegisterDragDrop(hWnd, &global_drag_and_drop_handler)) << std::endl;

	SAFC_VersionCheck();
}

///////////////////////////////////////
/////////////END OF USE////////////////
///////////////////////////////////////

void onTimer(int v);
void mDisplay()
{
	lFontSymbolsInfo::InitialiseFont(default_font_name);

	glClear(GL_COLOR_BUFFER_BIT | GL_ACCUM_BUFFER_BIT);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	if (firstboot)
	{
		firstboot = 0;

		Init();
		if (april_fool)
		{
			wh->throw_alert("Today is a special day! ( -w-)\nToday you'll have new background\n(-w- )", "1st of April!", special_signs::draw_wait, 1, 0xFF00FFFF, 20);
			(*wh)["ALERT"]->rgba_background = 0xF;
			_WH_t<text_box>("ALERT", "AlertText")->safe_text_color_change(0xFFFFFFFF);
		}
		if (years_old >= 0)
		{
			wh->throw_alert("Interesting fact: today is exactly " + std::to_string(years_old) + " years since first SAFC release.\n(o w o  )", "SAFC birthday", special_signs::draw_wait, 1, 0xFF7F3FFF, 50);
			(*wh)["ALERT"]->rgba_background = 0xF;
			_WH_t<text_box>("ALERT", "AlertText")->safe_text_color_change(0xFFFFFFFF);
		}
		animation_is_active = !animation_is_active;
		onTimer(0);
	}

	if (years_old >= 0 || Settings::ShaderMode == 100)
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
	else if (april_fool || Settings::ShaderMode == 69)
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
	else if (month_beginning || Settings::ShaderMode == 42)
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
	else if (Settings::ShaderMode < 4)
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
	if (wh)
		wh->draw();
	if (drag_over)
		special_signs::draw_file_sign(0, 0, 50, 0xFFFFFFFF, 0);
	glRotatef(-dumb_rotation_angle, 0, 0, 1);

	glutSwapBuffers();
	++timer_v;
}

void mInit()
{
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluOrtho2D((0 - internal_range) * (wind_x / window_base_width), internal_range * (wind_x / window_base_width), (0 - internal_range) * (wind_y / window_base_height), internal_range * (wind_y / window_base_height));
}

void mClose()
{
	Settings::RegestryAccess.Close();

	if (player)
		player->stop();
}

void onTimer(int v)
{
	glutTimerFunc(33, onTimer, 0);
	if (animation_is_active)
	{
		mDisplay();
		++timer_v;
	}
}

void OnResize(int x, int y)
{
	wind_x = x;
	wind_y = y;
	mInit();
	glViewport(0, 0, x, y);
	if (wh)
	{
		auto SMRP = (*wh)["SMRP_CONTAINER"];
		SMRP->safe_change_position_argumented(0, 0, 0);
		SMRP->not_safe_resize_centered(internal_range * 3 * (wind_y / window_base_height) + 2 * moveable_window::window_header_size, internal_range * 3 * (wind_x / window_base_width));

		if (simplayer_maximised)
			ApplySimplayerMaximisedLayout();
	}
}
void inline rotate(float& x, float& y)
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
	rotate(x, y);
}

void mMotion(int ix, int iy)
{
	float fx, fy;
	absolute_to_actual_coords(ix, iy, fx, fy);
	mouse_x_position = fx;
	mouse_y_position = fy;
	if (wh)
		wh->mouse_handler(fx, fy, 0, 0);
}

void mKey(std::uint8_t k, int x, int y)
{
	if (wh)
		wh->keyboard_handler(k);

	if (k == 27)
	{
		mClose();
		exit(0);
	}
}

void mClick(int butt, int state, int x, int y)
{
	float fx, fy;
	char button;
	absolute_to_actual_coords(x, y, fx, fy);
	button = butt - 1;

	if (state == GLUT_DOWN)
		state = -1;
	else if (state == GLUT_UP)
		state = 1;

	if (wh)
		wh->mouse_handler(fx, fy, button, static_cast<char>(state));
}

void mDrag(int x, int y)
{
	//mClick(88, MOUSE_DRAG_EVENT, x, y);
	mMotion(x, y);
}

void mSpecialKey(int Key, int x, int y)
{
	auto modif = glutGetModifiers();
	if (!(modif & GLUT_ACTIVE_ALT))
	{
		switch (Key)
		{
			case GLUT_KEY_DOWN:		if (wh)wh->keyboard_handler(1);
				break;
			case GLUT_KEY_UP:		if (wh)wh->keyboard_handler(2);
				break;
			case GLUT_KEY_LEFT:		if (wh)wh->keyboard_handler(3);
				break;
			case GLUT_KEY_RIGHT:		if (wh)wh->keyboard_handler(4);
				break;
			case GLUT_KEY_F5:		if (wh) Init();
				break;
		}
	}
	if (modif == GLUT_ACTIVE_ALT && Key == GLUT_KEY_DOWN)
	{
		internal_range *= 1.1f;
		OnResize(wind_x, wind_y);

		Init();
	}
	else if (modif == GLUT_ACTIVE_ALT && Key == GLUT_KEY_UP)
	{
		internal_range /= 1.1f;
		OnResize(wind_x, wind_y);

		Init();
	}
}

void mExit(int a)
{
}

struct SafcRuntime
{
	virtual void operator()(int argc, char** argv) = 0;
};

struct SafcGuiRuntime :
	public SafcRuntime
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
		__glutInitWithExit(&argc, argv, mExit);
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

		glutCloseFunc(mClose);
		glutMouseFunc(mClick);
		glutReshapeFunc(OnResize);
		glutSpecialFunc(mSpecialKey);
		glutMotionFunc(mDrag);
		glutPassiveMotionFunc(mMotion);
		glutKeyboardFunc(mKey);
		glutDisplayFunc(mDisplay);
		mInit();
		glutMainLoop();
	}
};

struct SafcCliRuntime:
	public SafcRuntime
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
		_Data.DetectedThreads =
			std::max(
				std::min((std::uint16_t)(
					(std::uint16_t)std::max(
						std::thread::hardware_concurrency(),
						1u
					)
					- 1
					),
					(std::uint16_t)(ceil(GetAvailableMemory() / 2048))
				), (std::uint16_t)1
			);
		_Data.IsCLIMode = true;

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
			_Data.SetGlobalPPQN(global_ppq_override->second->AsNumber());

		if (global_tempo_override != config.end())
			_Data.SetGlobalTempo(global_tempo_override->second->AsNumber());

		if (global_offset != config.end())
			_Data.SetGlobalOffset(global_offset->second->AsNumber());

		auto& filesArray = files->second->AsArray();
		std::vector<std::wstring> filenames;

		for (auto& singleEntry : filesArray)
		{
			auto& object = singleEntry->AsObject();
			auto& filename = object.at(L"filename")->AsString();
			filenames.push_back(filename);
		}

		add_files(filenames);

		size_t index = 0;
		for (auto& singleEntry : filesArray)
		{
			auto& object = singleEntry->AsObject();
			
			auto ppq_override = object.find(L"ppq_override");
			if (ppq_override != object.end())
				_Data.Files[index].NewPPQN = ppq_override->second->AsNumber();

			auto tempo_override = object.find(L"tempo_override");
			if (tempo_override != object.end())
				_Data.Files[index].NewTempo = tempo_override->second->AsNumber();

			auto offset = object.find(L"offset");
			if (offset != object.end())
				_Data.Files[index].OffsetTicks = offset->second->AsNumber();

			auto selection_start = object.find(L"selection_start");
			if (selection_start != object.end())
				_Data.Files[index].SelectionStart = selection_start->second->AsNumber();

			auto selection_length = object.find(L"selection_length");
			if (selection_length != object.end())
				_Data.Files[index].SelectionLength = selection_length->second->AsNumber();

			auto ignore_notes = object.find(L"ignore_notes");
			if (ignore_notes != object.end())
				_Data.Files[index].SetBoolSetting(_BoolSettings::ignore_notes, ignore_notes->second->AsBool());

			auto ignore_pitches = object.find(L"ignore_pitches");
			if (ignore_pitches != object.end())
				_Data.Files[index].SetBoolSetting(_BoolSettings::ignore_pitches, ignore_pitches->second->AsBool());

			auto ignore_tempos = object.find(L"ignore_tempos");
			if (ignore_tempos != object.end())
				_Data.Files[index].SetBoolSetting(_BoolSettings::ignore_tempos, ignore_tempos->second->AsBool());

			auto ignore_other = object.find(L"ignore_other");
			if (ignore_other != object.end())
				_Data.Files[index].SetBoolSetting(_BoolSettings::ignore_all_but_tempos_notes_and_pitch, ignore_other->second->AsBool());

			auto piano_only = object.find(L"piano_only");
			if (piano_only != object.end())
				_Data.Files[index].SetBoolSetting(_BoolSettings::all_instruments_to_piano, piano_only->second->AsBool());

			auto remove_remnants = object.find(L"remove_remnants");
			if (remove_remnants != object.end())
				_Data.Files[index].SetBoolSetting(_BoolSettings::remove_remnants, remove_remnants->second->AsBool());

			auto remove_empty_tracks = object.find(L"remove_empty_tracks");
			if (remove_empty_tracks != object.end())
				_Data.Files[index].SetBoolSetting(_BoolSettings::all_instruments_to_piano, remove_empty_tracks->second->AsBool());

			auto channel_split = object.find(L"channel_split");
			if (channel_split != object.end())
				_Data.Files[index].ChannelsSplit = channel_split->second->AsBool();

			auto collapse_midi = object.find(L"collapse_midi");
			if (collapse_midi != object.end())
				_Data.Files[index].CollapseMIDI = collapse_midi->second->AsBool();

			auto apply_offset_after = object.find(L"apply_offset_after");
			if (apply_offset_after != object.end())
				_Data.Files[index].ApplyOffsetAfter = apply_offset_after->second->AsBool();

			auto rsb_compression = object.find(L"rsb_compression");
			if (rsb_compression != object.end())
				_Data.Files[index].RSBCompression = rsb_compression->second->AsBool();

			auto ignore_meta_rsb = object.find(L"ignore_meta_rsb");
			if (ignore_meta_rsb != object.end())
				_Data.Files[index].allow_legacy_rsb_meta_interaction = ignore_meta_rsb->second->AsBool();

			auto inplace_mergable = object.find(L"inplace_mergable");
			if (inplace_mergable != object.end())
				_Data.Files[index].InplaceMergeEnabled = inplace_mergable->second->AsBool();

			auto allow_sysex = object.find(L"allow_sysex");
			if (allow_sysex != object.end())
				_Data.Files[index].AllowSysex = allow_sysex->second->AsBool();

			auto enable_zero_vel = object.find(L"enable_zero_velocity");
			if (enable_zero_vel != object.end())
				_Data.Files[index].EnableZeroVelocity = enable_zero_vel->second->AsBool();

			index++;
		}

		auto save_to = config.find(L"save_to");
		if (save_to != config.end())
		{
			_Data.SavePath = (save_to->second->AsString());
			size_t Pos = _Data.SavePath.rfind(L".mid");
			if (Pos >= _Data.SavePath.size() || Pos <= _Data.SavePath.size() - 4)
				_Data.SavePath += L".mid";
		}

		auto LocalMCTM = _Data.MCTM_Constructor();
		LocalMCTM->StartProcessingMIDIs();

		while (LocalMCTM->CheckSMRPProcessingAndStartNextStep())
			//that's some really dumb synchronization... TODO: MAKE BETTER
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		while (!LocalMCTM->CheckRIMerge())
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		while (!LocalMCTM->CompleteFlag)
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
	}
};

int main(int argc, char** argv)
{
	player = std::make_shared<simple_player>();
	player->init();

	__versionTuple = ___GetVersion();

	std::ios_base::sync_with_stdio(false); //why not

	std::shared_ptr<SafcRuntime> runtime;

	RestoreRegSettings();

	if (argc > 1)
		runtime = std::make_shared<SafcCliRuntime>();
	else
		runtime = std::make_shared<SafcGuiRuntime>();

	(*runtime)(argc, argv);

	return 0;
}
