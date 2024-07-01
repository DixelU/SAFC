#define NOMINMAX
#define _WIN32_WINNT 0x0A00
#include <algorithm>
#include <cstdlib>
#include <wchar.h>
//#include <io.h>
#include <tuple>
#include <ctime>
#include <mutex>
#include <iostream>
#include <cstdlib>
#include <cmath>
#include <vector>
#include <filesystem>
#include <deque>
#include <unordered_set>
#include <unordered_map>
#include <list>
#include <optional>
#include <limits>
#include <fstream>
#include <set>
#include <string>
#include <iterator>
#include <map>
#include <deque>
#include <array>
#include <thread>
#include <boost/algorithm/string.hpp>

#ifdef WINDOWS
#include <WinSock2.h>
#include <WinInet.h>
#endif // WINDOWS

//#pragma comment (lib, "Version.lib")//Urlmon.lib
//#pragma comment (lib, "Urlmon.lib")//Urlmon.lib
//#pragma comment (lib, "wininet.lib")//Urlmon.lib
//#pragma comment (lib, "dwmapi.lib")
//#pragma comment (lib, "Ws2_32.Lib")
//#pragma comment (lib, "Wldap32.Lib")
//#pragma comment (lib, "Crypt32.Lib")

#include "allocator.h"
#ifdef WINDOWS
#include "WinReg.h"
#endif
#include "resource.h"

#include <stack>
#include "bbb_ffio.h"

#include "JSON/JSON.h"
#include "JSON/JSON.cpp"
#include "JSON/JSONValue.h"
#include "JSON/JSONValue.cpp"

#include "btree/btree_map.h"
#include "SAFGUIF/fonted_manip.h"
#include "SAFGUIF/SAFGUIF.h"
#include "SAFC_InnerModules/SAFC_IM.h"
#include "SAFCGUIF_Local/SAFGUIF_L.h"

#include "SAFC_InnerModules/SMRP2.h"

#include <nfd.hpp>

/*
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

	/* Select which attributes we want to restore. 
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
		ThrowAlert_Error(std::move(error_msg));
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
				std::getline(*input, temp_buffer);
				fclose(fi_ptr);
				delete input;
				input = nullptr;
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
					ThrowAlert_Warning("Update found! The app might restart soon...\nUpdate: " + std::string(git_latest_version.begin(), git_latest_version.end()));
					flag = SAFC_Update(git_latest_version);
					if (flag)
						ThrowAlert_Warning("SAFC will restart in 3 seconds...");
				}
			}
			else if (check_autoupdates)
			{
				auto msg = "Most likely your internet connection is unstable\nSAFC cannot check for updates";

				ThrowAlert_Warning(msg);
				std::cout << msg;
			}
		}
		catch (const std::exception& e)
		{
			ThrowAlert_Warning("SAFC just almost crashed while checking the update...\nTell developer about that " +
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
*/

size_t GetAvailableMemory() 
{
	static std::mutex mutex; 
	std::lock_guard<std::mutex> locker(mutex);

	size_t ret = 0;
#ifdef WINDOWS
	// because compiler static links the function...
	BOOL(__stdcall * GMSEx)(LPMEMORYSTATUSEX) = 0;

	static HINSTANCE hIL = LoadLibraryW(L"kernel32.dll");
	GMSEx = (BOOL(__stdcall*)(LPMEMORYSTATUSEX))GetProcAddress(hIL, "GlobalMemoryStatusEx");
	if (GMSEx) 
	{
		MEMORYSTATUSEX m;
		m.dwLength = sizeof(m);
		if (GMSEx(&m)) {
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
#endif
	return ret;
}

//////////////////////////////
////TRUE USAGE STARTS HERE////
//////////////////////////////

ButtonSettings
* BS_List_Black_Small = new ButtonSettings(System_White, 0, 0, 100, 10, 1, 0, 0, 0xFFEFDFFF, 0x00003F7F, 0x7F7F7FFF);

std::uint32_t DefaultBoolSettings = _BoolSettings::remove_remnants | _BoolSettings::remove_empty_tracks | _BoolSettings::all_instruments_to_piano;

struct FileSettings
{////per file settings
	std_unicode_string Filename;
	std_unicode_string PostprocessedFile_Name, WFileNamePostfix;
	std::string AppearanceFilename, AppearancePath, FileNamePostfix;
	std::uint16_t NewPPQN, OldPPQN, OldTrackNumber, MergeMultiplier;
	std::int16_t GroupID;
	double NewTempo;
	std::uint64_t FileSize;
	std::int64_t SelectionStart, SelectionLength;
	bool
		IsMIDI,
		InplaceMergeEnabled,
		AllowLegacyRunningStatusMetaIgnorance,
		RSBCompression,
		ChannelsSplit,
		CollapseMIDI,
		AllowSysex,
		ApplyOffsetAfter;
	std::shared_ptr<CutAndTransposeKeys> KeyMap;
	std::shared_ptr<PLC<std::uint8_t, std::uint8_t>> VolumeMap;
	std::shared_ptr<PLC<std::uint16_t, std::uint16_t>> PitchBendMap;
	std::uint32_t BoolSettings;
	std::int64_t OffsetTicks;
	FileSettings(const std_unicode_string& Filename)
	{
		this->Filename = Filename;
		AppearanceFilename = AppearancePath = "";
		auto pos = Filename.rfind('/');
		for (; pos < Filename.size(); pos++)
			AppearanceFilename.push_back(Filename[pos] & 0xFF);
		for (int i = 0; i < Filename.size(); i++)
			AppearancePath.push_back((char)(Filename[i] & 0xFF));
		//cout << AppearancePath << " ::\n";
		FastMIDIChecker FMIC(Filename);
		this->IsMIDI = FMIC.IsAcssessable && FMIC.IsMIDI;
		OldPPQN = FMIC.PPQN;
		NewPPQN = FMIC.PPQN;
		MergeMultiplier = 0;
		OldTrackNumber = FMIC.ExpectedTrackNumber;
		OffsetTicks = InplaceMergeEnabled = 0;
		AllowSysex = false;
		FileSize = FMIC.FileSize;
		GroupID = NewTempo = 0;
		SelectionStart = 0;
		SelectionLength = -1;
		BoolSettings = DefaultBoolSettings;
		FileNamePostfix = "_.mid"; //_FILENAMEWITHEXTENSIONSTRING_.mid
		WFileNamePostfix =
#ifdef WINDOWS
			L"_.mid";
#else
			"_.mid";
#endif
		RSBCompression = ChannelsSplit = false;
		CollapseMIDI = true;
		AllowLegacyRunningStatusMetaIgnorance = false;
		ApplyOffsetAfter = true;
	}
	inline void SwitchBoolSetting(std::uint32_t SMP_BoolSetting) 
	{
		BoolSettings ^= SMP_BoolSetting;
	}
	inline void SetBoolSetting(std::uint32_t SMP_BoolSetting, bool NewState) 
	{
		if (BoolSettings & SMP_BoolSetting && NewState)return;
		if (!(BoolSettings & SMP_BoolSetting) && !NewState)return;
		SwitchBoolSetting(SMP_BoolSetting);
	}
	std::shared_ptr<single_midi_processor_2::processing_data> BuildSMRPProcessingData() 
	{
		auto smrp_data = std::make_shared<single_midi_processor_2::processing_data>();
		auto& settings = smrp_data->settings;
		smrp_data->filename = Filename;
		smrp_data->postfix = WFileNamePostfix;
		if(VolumeMap)
			settings.volume_map = std::make_shared<BYTE_PLC_Core>(VolumeMap);
		if(PitchBendMap)
			settings.pitch_map = std::make_shared<_14BIT_PLC_Core>(PitchBendMap);
		settings.key_converter = KeyMap;
		settings.new_ppqn = NewPPQN;
		settings.old_ppqn = OldPPQN;
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
		settings.legacy.ignore_meta_rsb = AllowLegacyRunningStatusMetaIgnorance;
		settings.legacy.rsb_compression = RSBCompression;
		settings.filter.pass_sysex = AllowSysex;
		InplaceMergeEnabled = 
			settings.details.inplace_mergable = InplaceMergeEnabled && !RSBCompression;
		settings.details.group_id = GroupID;
		settings.details.initial_filesize = FileSize;
		settings.offset = OffsetTicks;
		if(NewTempo > 3.)
			settings.tempo.set_override_value(NewTempo);
		if (OffsetTicks < 0 && -OffsetTicks > SelectionStart)
			SelectionStart = -OffsetTicks;
		if (SelectionStart && (SelectionLength < 0))
			SelectionLength -= SelectionStart;
		settings.selection_data = single_midi_processor_2::settings_obj::selection(SelectionStart, SelectionLength);

		smrp_data->appearance_filename = AppearanceFilename;

		return smrp_data;
	}
};
struct _SFD_RSP 
{
	std::int32_t ID;
	std::int64_t FileSize;
	_SFD_RSP(std::int32_t ID, std::int64_t FileSize)
	{
		this->ID = ID;
		this->FileSize = FileSize;
	}
	inline bool operator<(_SFD_RSP a)
	{
		return FileSize < a.FileSize;
	}
};

struct SAFCData 
{ ////overall settings and storing perfile settings....
	std::vector<FileSettings> Files;
	std_unicode_string SavePath;
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
		SavePath.clear();
		ChannelsSplit = RSBCompression = false;
		ApplyOffsetAfter = true;
	}
	void ResolveSubdivisionProblem_GroupIDAssign(std::uint16_t ThreadsCount = 0)
	{
		if (!ThreadsCount)
			ThreadsCount = DetectedThreads;
		if (Files.empty())
		{
			SavePath.clear();
			return;
		}
		else if (Files.size())
			SavePath = Files[0].Filename +
#ifdef WINDOWS
				L".AfterSAFC.mid";
#else
				".AfterSAFC.mid";
#endif

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
			Sizes.emplace_back(i, Files[i].FileSize);
		}

		sort(Sizes.begin(), Sizes.end());

		for (int i = 0; i < Sizes.size(); i++)
		{
			SumSize.push_back((T += Sizes[i].FileSize));
		}

		for (int i = 0; i < SumSize.size(); i++)
		{
			Files[Sizes[i].ID].GroupID = (std::uint16_t)(ceil(((float)SumSize[i] / ((float)SumSize.back())) * ThreadsCount) - 1.);
			std::cout << "Thread " << Files[Sizes[i].ID].GroupID << ": " << Sizes[i].FileSize << ":\t" << Sizes[i].ID << std::endl;
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
	std::shared_ptr<MIDICollectionThreadedMerger> MCTM_Constructor()
	{
		using proc_data_ptr = std::shared_ptr<single_midi_processor_2::processing_data>;
		std::vector<proc_data_ptr> SMRPv;
		for (int i = 0; i < Files.size(); i++)
			SMRPv.push_back(Files[i].BuildSMRPProcessingData());
		return std::make_shared<MIDICollectionThreadedMerger>(SMRPv, GlobalPPQN, SavePath, IsCLIMode);
	}
	FileSettings& operator[](std::int32_t ID)
	{
		return Files[ID];
	}
};

SAFCData _Data;
std::shared_ptr<MIDICollectionThreadedMerger> GlobalMCTM;

void ThrowAlert_Error(std::string&& AlertText) 
{
	if (WH)
		WH->ThrowAlert(AlertText, "ERROR!", SpecialSigns::DrawExTriangle, 1, 0xFFAF00FF, 0xFF);
}

void ThrowAlert_Warning(std::string&& AlertText)
{
	if (WH)
		WH->ThrowAlert(AlertText, "Warning!", SpecialSigns::DrawExTriangle, 1, 0x7F7F7FFF, 0xFFFFFFAF);
}



std::vector<std_unicode_string> MOFD(std_unicode_string::pointer Title)
{
#ifdef WINDOWS
	OPENFILENAMEW ofn;       // common dialog box structure
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
	if (GetOpenFileNameW(&ofn))
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
		case CDERR_DIALOGFAILURE:		 ThrowAlert_Error("CDERR_DIALOGFAILURE\n");   break;
		case CDERR_FINDRESFAILURE:		 ThrowAlert_Error("CDERR_FINDRESFAILURE\n");  break;
		case CDERR_INITIALIZATION:	 ThrowAlert_Error("CDERR_INITIALIZATION\n"); break;
		case CDERR_LOADRESFAILURE:	 ThrowAlert_Error("CDERR_LOADRESFAILURE\n"); break;
		case CDERR_LOADSTRFAILURE:	 ThrowAlert_Error("CDERR_LOADSTRFAILURE\n"); break;
		case CDERR_LOCKRESFAILURE:	 ThrowAlert_Error("CDERR_LOCKRESFAILURE\n"); break;
		case CDERR_MEMALLOCFAILURE:	 ThrowAlert_Error("CDERR_MEMALLOCFAILURE\n"); break;
		case CDERR_MEMLOCKFAILURE:	 ThrowAlert_Error("CDERR_MEMLOCKFAILURE\n"); break;
		case CDERR_NOHINSTANCE:		 ThrowAlert_Error("CDERR_NOHINSTANCE\n"); break;
		case CDERR_NOHOOK:			 ThrowAlert_Error("CDERR_NOHOOK\n"); break;
		case CDERR_NOTEMPLATE:		 ThrowAlert_Error("CDERR_NOTEMPLATE\n"); break;
		case CDERR_STRUCTSIZE:		 ThrowAlert_Error("CDERR_STRUCTSIZE\n"); break;
		case FNERR_BUFFERTOOSMALL:	 ThrowAlert_Error("FNERR_BUFFERTOOSMALL\n"); break;
		case FNERR_INVALIDFILENAME:	 ThrowAlert_Error("FNERR_INVALIDFILENAME\n"); break;
		case FNERR_SUBCLASSFAILURE:	 ThrowAlert_Error("FNERR_SUBCLASSFAILURE\n"); break;
		}
		return std::vector<std::wstring> {L""};
	}
#else
	auto onReturnNFDQuitter = makeOnDestroyExecutor([](){NFD::Quit();});
	std::vector<std_unicode_string> paths;
	NFD::Init();
	NFD::UniquePathSet pathsSet;
	nfdfilteritem_t filterItem[1] = { { "MIDI File", "mid,midi,MID,MIDI" } };
	auto status = NFD::OpenDialogMultiple(pathsSet, filterItem, 1);
	switch(status)
	{
		case NFD_OKAY:
			nfdpathsetsize_t size;
			NFD::PathSet::Count(pathsSet, size);
			for(size_t i = 0; i < size; ++i)
			{
				NFD::UniquePathSetPathN singlePath;
				NFD::PathSet::GetPath(pathsSet, i, singlePath);
				paths.push_back(singlePath.get());
			}
			break;
		case NFD_ERROR:
			ThrowAlert_Error(std::string("NFD ERROR:\n") + NFD_GetError());
		case NFD_CANCEL:
			break;
	}
	return paths;
#endif
}

std_unicode_string SOFD(std_unicode_string::pointer Title)
{
#ifdef WINDOWS
	wchar_t filename[MAX_PATH];
	OPENFILENAMEW ofn;
	ZeroMemory(&filename, sizeof(filename));
	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = NULL;  // If you have a window to center over, put its HANDLE here
	ofn.lpstrFilter = L"MIDI Files(*.mid)\0*.mid\0";
	ofn.lpstrFile = filename;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrTitle = Title;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_NOREADONLYRETURN | OFN_HIDEREADONLY;
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = NULL;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = NULL;
	if (GetSaveFileNameW(&ofn)) return std::wstring(filename);
	else 
	{
		switch (CommDlgExtendedError())
		{
		case CDERR_DIALOGFAILURE:		 ThrowAlert_Error("CDERR_DIALOGFAILURE\n");   break;
		case CDERR_FINDRESFAILURE:		 ThrowAlert_Error("CDERR_FINDRESFAILURE\n");  break;
		case CDERR_INITIALIZATION:	 ThrowAlert_Error("CDERR_INITIALIZATION\n"); break;
		case CDERR_LOADRESFAILURE:	 ThrowAlert_Error("CDERR_LOADRESFAILURE\n"); break;
		case CDERR_LOADSTRFAILURE:	 ThrowAlert_Error("CDERR_LOADSTRFAILURE\n"); break;
		case CDERR_LOCKRESFAILURE:	 ThrowAlert_Error("CDERR_LOCKRESFAILURE\n"); break;
		case CDERR_MEMALLOCFAILURE:	 ThrowAlert_Error("CDERR_MEMALLOCFAILURE\n"); break;
		case CDERR_MEMLOCKFAILURE:	 ThrowAlert_Error("CDERR_MEMLOCKFAILURE\n"); break;
		case CDERR_NOHINSTANCE:		 ThrowAlert_Error("CDERR_NOHINSTANCE\n"); break;
		case CDERR_NOHOOK:			 ThrowAlert_Error("CDERR_NOHOOK\n"); break;
		case CDERR_NOTEMPLATE:		 ThrowAlert_Error("CDERR_NOTEMPLATE\n"); break;
		case CDERR_STRUCTSIZE:		 ThrowAlert_Error("CDERR_STRUCTSIZE\n"); break;
		case FNERR_BUFFERTOOSMALL:	 ThrowAlert_Error("FNERR_BUFFERTOOSMALL\n"); break;
		case FNERR_INVALIDFILENAME:	 ThrowAlert_Error("FNERR_INVALIDFILENAME\n"); break;
		case FNERR_SUBCLASSFAILURE:	 ThrowAlert_Error("FNERR_SUBCLASSFAILURE\n"); break;
		}
		return L"";
	}
#else
	auto onReturnNFDQuitter = makeOnDestroyExecutor([](){NFD::Quit();});
	std_unicode_string path;
	NFD::Init();
	NFD::UniquePathN outputPath;
	nfdfilteritem_t filterItem[1] = { { "MIDI File", "mid,midi,MID,MIDI" } };
	auto status = NFD::SaveDialog(outputPath, filterItem, 1);
	switch(status)
	{
		case NFD_OKAY:
			path = outputPath.get();
			break;
		case NFD_ERROR:
			ThrowAlert_Error(std::string("NFD ERROR:\n") + NFD_GetError());
		case NFD_CANCEL:
			break;
	}
	return path;
#endif
}

//////////IMPORTANT STUFFS ABOVE///////////

#define _WH(Window,Element) ((*(*WH)[Window])[Element])//...uh
#define _WH_t(Window,Element,Type) ((Type)_WH(Window,Element))

void AddFiles(const std::vector<std_unicode_string>& Filenames)
{
	if(WH)
		WH->DisableAllWindows();
	for (int i = 0; i < Filenames.size(); i++)
	{
		if (Filenames[i].empty())
			continue;
		_Data.Files.push_back(FileSettings(Filenames[i]));
		auto& lastFile = _Data.Files.back();
		if (lastFile.IsMIDI)
		{
			if (WH)
				_WH_t("MAIN", "List", SelectablePropertedList*)->SafePushBackNewString(lastFile.AppearanceFilename);

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
#ifdef WINDOWS
					_Data[q].WFileNamePostfix = std::to_wstring(Counter) + L"_.mid";
#else
					_Data[q].WFileNamePostfix = std::to_string(Counter) + "_.mid";
#endif
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
#ifdef WINDOWS
	auto filenames = MOFD(L"Select midi files");
#else
	auto filenames = MOFD("Select midi files");
#endif
	AddFiles(filenames);
}

namespace PropsAndSets
{
	std::string* PPQN = new std::string(""), * OFFSET = new std::string(""), * TEMPO = new std::string("");
	int currentID = -1, CaTID = -1, VMID = -1, PMID = -1;
	bool ForPersonalUse = true;
	SingleMIDIInfoCollector* SMICptr = nullptr;
	std::string CSV_DELIM = ";";
	void OGPInMIDIList(int ID)
	{
		if (ID < _Data.Files.size() && ID >= 0)
		{
			currentID = ID;
			auto PASWptr = (*WH)["SMPAS"];
			((TextBox*)((*PASWptr)["FileName"]))->SafeStringReplace("..." + _Data[ID].AppearanceFilename);
			((InputField*)((*PASWptr)["PPQN"]))->UpdateInputString();
			((InputField*)((*PASWptr)["PPQN"]))->SafeStringReplace(std::to_string((_Data[ID].NewPPQN) ? _Data[ID].NewPPQN : _Data[ID].OldPPQN));
			((InputField*)((*PASWptr)["TEMPO"]))->SafeStringReplace(std::to_string(_Data[ID].NewTempo));
			((InputField*)((*PASWptr)["OFFSET"]))->SafeStringReplace(std::to_string(_Data[ID].OffsetTicks));
			((InputField*)((*PASWptr)["GROUPID"]))->SafeStringReplace(std::to_string(_Data[ID].GroupID));

			((InputField*)((*PASWptr)["SELECT_START"]))->SafeStringReplace(std::to_string(_Data[ID].SelectionStart));
			((InputField*)((*PASWptr)["SELECT_LENGTH"]))->SafeStringReplace(std::to_string(_Data[ID].SelectionLength));

			((CheckBox*)((*PASWptr)["BOOL_REM_TRCKS"]))->State = _Data[ID].BoolSettings & _BoolSettings::remove_empty_tracks;
			((CheckBox*)((*PASWptr)["BOOL_REM_REM"]))->State = _Data[ID].BoolSettings & _BoolSettings::remove_remnants;
			((CheckBox*)((*PASWptr)["BOOL_PIANO_ONLY"]))->State = _Data[ID].BoolSettings & _BoolSettings::all_instruments_to_piano;
			((CheckBox*)((*PASWptr)["BOOL_IGN_TEMPO"]))->State = _Data[ID].BoolSettings & _BoolSettings::ignore_tempos;
			((CheckBox*)((*PASWptr)["BOOL_IGN_PITCH"]))->State = _Data[ID].BoolSettings & _BoolSettings::ignore_pitches;
			((CheckBox*)((*PASWptr)["BOOL_IGN_NOTES"]))->State = _Data[ID].BoolSettings & _BoolSettings::ignore_notes;
			((CheckBox*)((*PASWptr)["BOOL_IGN_ALL_EX_TPS"]))->State = _Data[ID].BoolSettings & _BoolSettings::ignore_all_but_tempos_notes_and_pitch;
			((CheckBox*)((*PASWptr)["SPLIT_TRACKS"]))->State = _Data[ID].ChannelsSplit;
			((CheckBox*)((*PASWptr)["RSB_COMPRESS"]))->State = _Data[ID].RSBCompression;

			((CheckBox*)((*PASWptr)["INPLACE_MERGE"]))->State = _Data[ID].InplaceMergeEnabled;
			((CheckBox*)((*PASWptr)["LEGACY_META_RSB_BEHAVIOR"]))->State = _Data[ID].AllowLegacyRunningStatusMetaIgnorance;
			((CheckBox*)((*PASWptr)["ALLOW_SYSEX"]))->State = _Data[ID].AllowSysex;

			((CheckBox*)((*PASWptr)["COLLAPSE_MIDI"]))->State = _Data[ID].CollapseMIDI;
			((CheckBox*)((*PASWptr)["APPLY_OFFSET_AFTER"]))->State = _Data[ID].ApplyOffsetAfter;

			((TextBox*)((*PASWptr)["CONSTANT_PROPS"]))->SafeStringReplace(
			    "File size: " + std::to_string(_Data[ID].FileSize) + "b\n" +
			    "Old PPQN: " + std::to_string(_Data[ID].OldPPQN) + "\n" +
			    "Track number (header info): " + std::to_string(_Data[ID].OldTrackNumber) + "\n" +
			    "\"Remnant\" file postfix: " + _Data[ID].FileNamePostfix
			);

			WH->EnableWindow("SMPAS");
		}
		else
		{
			currentID = -1;
		}
	}
	void InitializeCollecting()
	{
		//ThrowAlert_Warning("Still testing :)");
		if (currentID < 0 && currentID >= _Data.Files.size()) 
		{
			ThrowAlert_Error("How you've managed to select the midi beyond the list? O.o\n" + std::to_string(currentID));
			return;
		}
		auto UIElement = (Graphing<SingleMIDIInfoCollector::tempo_graph>*)(*(*WH)["SMIC"])["TEMPO_GRAPH"];
		if (SMICptr)
		{
			{ std::lock_guard<std::recursive_mutex> locker(UIElement->Lock); }
			UIElement->Graph = nullptr;
		}
		SMICptr = new SingleMIDIInfoCollector(_Data.Files[currentID].Filename, _Data.Files[currentID].OldPPQN, _Data.Files[currentID].AllowLegacyRunningStatusMetaIgnorance);
		std::thread th([]()
		{
			WH->MainWindow_ID = "SMIC";
			WH->DisableAllWindows();
			std::thread ith([]()
			{
				auto All_Exp = (*(*WH)["SMIC"])["ALL_EXP"];
				auto TG_Exp = (*(*WH)["SMIC"])["TG_EXP"];
				auto ITicks = (*(*WH)["SMIC"])["INTEGRATE_TICKS"];
				auto ITime = (*(*WH)["SMIC"])["INTEGRATE_TIME"];
				auto ErrorLine = (TextBox*)(*(*WH)["SMIC"])["FEL"];
				auto InfoLine = (TextBox*)(*(*WH)["SMIC"])["FLL"];
				auto Delim = (InputField*)(*(*WH)["SMIC"])["DELIM"];
				auto UIMinutes = (InputField*)(*(*WH)["SMIC"])["INT_MIN"];
				auto UISeconds = (InputField*)(*(*WH)["SMIC"])["INT_SEC"];
				auto UIT = (InputField*)(*(*WH)["SMIC"])["TG_SWITCH"];
				auto UIP = (InputField*)(*(*WH)["SMIC"])["PG_SWITCH"];
				auto UIMilliseconds = (InputField*)(*(*WH)["SMIC"])["INT_MSC"];
				auto UITicks = (InputField*)(*(*WH)["SMIC"])["INT_TIC"];
				auto UIElement_TG = (Graphing<SingleMIDIInfoCollector::tempo_graph>*)(*(*WH)["SMIC"])["TEMPO_GRAPH"];
				auto UIElement_PG = (Graphing<SingleMIDIInfoCollector::polyphony_graph>*)(*(*WH)["SMIC"])["POLY_GRAPH"];
				UIElement_PG->Enabled = false;
				UIElement_PG->Reset();
				UIElement_TG->Enabled = false;
				UIElement_TG->Reset();
				Delim->SafeStringReplace(CSV_DELIM);
				Delim->CurrentString = CSV_DELIM;
				UIP->SafeStringReplace("Enable graph B");
				UIT->SafeStringReplace("Enable graph A");
				InfoLine->SafeStringReplace("Please wait...");
				UIMinutes->SafeStringReplace("0");
				UISeconds->SafeStringReplace("0");
				UIMilliseconds->SafeStringReplace("0");
				UITicks->SafeStringReplace("0");
				All_Exp->Disable();
				TG_Exp->Disable();
				ITicks->Disable();
				ITime->Disable();
				while (!SMICptr->Finished)
				{
					if (ErrorLine->Text != SMICptr->ErrorLine)
						ErrorLine->SafeStringReplace(SMICptr->ErrorLine);
					if (InfoLine->Text != SMICptr->LogLine)
						InfoLine->SafeStringReplace(SMICptr->LogLine);
					std::this_thread::sleep_for(std::chrono::milliseconds(10));
				}
				InfoLine->SafeStringReplace("Finished");
				All_Exp->Enable();
				TG_Exp->Enable();
				ITicks->Enable();
				ITime->Enable();
			});
			ith.detach();

			SMICptr->Lookup();
			auto UIElement_TG = (Graphing<SingleMIDIInfoCollector::tempo_graph>*)(*(*WH)["SMIC"])["TEMPO_GRAPH"];
			UIElement_TG->Graph = &(SMICptr->TempoMap);
			auto UIElement_PG = (Graphing<SingleMIDIInfoCollector::polyphony_graph>*)(*(*WH)["SMIC"])["POLY_GRAPH"];
			UIElement_PG->Graph = &(SMICptr->Polyphony);

			auto UIElement_TB = (TextBox*)(*(*WH)["SMIC"])["TOTAL_INFO"];
			UIElement_TB->SafeStringReplace(
				"Total (real) tracks: " + std::to_string(SMICptr->Tracks.size()) + "; ... "
			);

			WH->MainWindow_ID = "MAIN";
			WH->EnableWindow("MAIN");
			WH->EnableWindow("SMIC");
		});
		th.detach();
		WH->EnableWindow("SMIC");
	}
	namespace SMIC
	{
		void EnablePG()
		{
			auto UIElement_PG = (Graphing<SingleMIDIInfoCollector::polyphony_graph>*)(*(*WH)["SMIC"])["POLY_GRAPH"];
			auto UIElement_Butt = (Button*)(*(*WH)["SMIC"])["PG_SWITCH"];
			if (UIElement_PG->Enabled)
				UIElement_Butt->SafeStringReplace("Enable graph B");
			else
				UIElement_Butt->SafeStringReplace("Disable graph B");
			UIElement_PG->Enabled ^= true;
		}
		void EnableTG()
		{
			auto UIElement_TG = (Graphing<SingleMIDIInfoCollector::tempo_graph>*)(*(*WH)["SMIC"])["TEMPO_GRAPH"];
			auto UIElement_Butt = (Button*)(*(*WH)["SMIC"])["TG_SWITCH"];
			if (UIElement_TG->Enabled)
				UIElement_Butt->SafeStringReplace("Enable graph A");
			else
				UIElement_Butt->SafeStringReplace("Disable graph A");
			UIElement_TG->Enabled ^= true;
		}
		void ResetTG()
		{//TEMPO_GRAPH
			auto UIElement_TG = (Graphing<SingleMIDIInfoCollector::tempo_graph>*)(*(*WH)["SMIC"])["TEMPO_GRAPH"];
			UIElement_TG->Reset();
		}
		void ResetPG()
		{//TEMPO_GRAPH
			auto UIElement_PG = (Graphing<SingleMIDIInfoCollector::tempo_graph>*)(*(*WH)["SMIC"])["POLY_GRAPH"];
			UIElement_PG->Reset();
		}
		void SwitchPersonalUse()
		{//PERSONALUSE
			auto UIElement_Butt = (Button*)(*(*WH)["SMIC"])["PERSONALUSE"];
			ForPersonalUse ^= true;
			if (ForPersonalUse)
				UIElement_Butt->SafeStringReplace(".csv");
			else
				UIElement_Butt->SafeStringReplace(".atraw");
		}
		void ExportTG()
		{
			std::thread th([]()
			{
				WH->MainWindow_ID = "SMIC";
				WH->DisableAllWindows();
				auto InfoLine = (TextBox*)(*(*WH)["SMIC"])["FLL"];
				InfoLine->SafeStringReplace("Graph A is exporting...");
				auto [out, fo_ptr] = open_wide_stream<std::ostream>
#ifdef WINDOWS
				    (SMICptr->FileName + L".tg.csv", L"wb");
#else
					(SMICptr->FileName + ".tg.csv", "wb");
#endif
				(*out) << "tick" << CSV_DELIM << "tempo" << '\n';
				for (auto cur_pair : SMICptr->TempoMap)
					(*out) << cur_pair.first << CSV_DELIM << cur_pair.second << '\n';
				fclose(fo_ptr);
				delete out;
				out = nullptr;
				WH->MainWindow_ID = "MAIN";
				WH->EnableWindow("MAIN");
				WH->EnableWindow("SMIC");
				InfoLine->SafeStringReplace("Graph A was successfully exported...");
			});
			th.detach();
		}
		void ExportAll()
		{
			std::thread th([]()
			{
				WH->MainWindow_ID = "SMIC";
				WH->DisableAllWindows();
				auto InfoLine = (TextBox*)(*(*WH)["SMIC"])["FLL"];
				InfoLine->SafeStringReplace("Collecting data for exporting...");

				using line_data = struct
				{
					std::int64_t Polyphony;
					double Seconds;
					double Tempo;
				};

				std::int64_t Polyphony = 0;
				std::uint16_t PPQ = SMICptr->PPQ;
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
				for (auto& cur_pair : SMICptr->Polyphony)
					info[cur_pair.first] = line_data({
						cur_pair.second, 0., 0.
						});
				auto it_ptree = SMICptr->Polyphony.begin();
				for (auto cur_pair : SMICptr->TempoMap)
				{
					while (it_ptree != SMICptr->Polyphony.end() && it_ptree->first < cur_pair.first)
					{
						seconds += seconds_per_tick * (it_ptree->first - last_tick);
						last_tick = it_ptree->first;
						auto& t = info[it_ptree->first];
						Polyphony = t.Polyphony;
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
							Polyphony, seconds, cur_pair.second
							});
					}
					last_tick = cur_pair.first;
					tempo = cur_pair.second;
					seconds_per_tick = (60 / (tempo * PPQ));
				}
				while (it_ptree != SMICptr->Polyphony.end())
				{
					seconds += seconds_per_tick * (it_ptree->first - last_tick);
					last_tick = it_ptree->first;
					auto& t = info[it_ptree->first];
					Polyphony = t.Polyphony;
					t.Seconds = seconds;
					t.Tempo = tempo;
					++it_ptree;
				}
				auto [out, fo_ptr] = open_wide_stream<std::ostream>(SMICptr->FileName +
#ifdef WINDOWS
				                     ((ForPersonalUse)?L".a.csv":L".a.atraw"),
				                     ((ForPersonalUse)?L"w":L"wb"));
#else
									((ForPersonalUse) ? ".a.csv" : ".a.atraw"),
									((ForPersonalUse) ? "w" : "wb"));
#endif

				if (ForPersonalUse) {
					(*out) << header;
					for (auto cur_pair : info) {
						(*out)
							<< cur_pair.first << CSV_DELIM
							<< cur_pair.second.Polyphony << CSV_DELIM
							<< cur_pair.second.Seconds << CSV_DELIM
							<< cur_pair.second.Tempo << std::endl;
					}
				}
				else
				{
					for (auto& cur_pair : info)
					{
						out->write((const char*)(&cur_pair.first), 8);
						out->write((const char*)(&cur_pair.second.Polyphony), 8);
						out->write((const char*)(&cur_pair.second.Seconds), 8);
						out->write((const char*)(&cur_pair.second.Tempo), 8);
					}
				}
				fclose(fo_ptr);
				delete out;
				out = nullptr;
				WH->MainWindow_ID = "MAIN";
				WH->EnableWindow("MAIN");
				WH->EnableWindow("SMIC");
				InfoLine->SafeStringReplace("Graph B was successfully exported...");
			});
			th.detach();
		}
		void DiffirentiateTicks()
		{
			std::thread th([]()
				{
				WH->MainWindow_ID = "SMIC";
				WH->DisableAllWindows();
				auto InfoLine = (*(*WH)["SMIC"])["FLL"];
				auto UITicks = (InputField*)(*(*WH)["SMIC"])["INT_TIC"];
				auto UIOutput = (TextBox*)(*(*WH)["SMIC"])["ANSWER"];
				InfoLine->SafeStringReplace("Integration has begun");

				std::int64_t ticks_limit = 0;
				ticks_limit = std::stoi(UITicks->GetCurrentInput("0"));

				std::int64_t prev_tick = 0, cur_tick = 0;
				double cur_seconds = 0;
				double prev_second = 0;
				double PPQ = SMICptr->PPQ;
				double prev_tempo = 120;
				std::int64_t last_tick = (*SMICptr->TempoMap.rbegin()).first;
				for (auto& cur_pair : SMICptr->TempoMap/*; cur_pair != SMICptr->TempoMap.end(); cur_pair++*/)
				{
					cur_tick = cur_pair.first;
					cur_seconds += (cur_tick - prev_tick) * (60 / (prev_tempo * PPQ));
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

				UIOutput->SafeStringReplace(
					"Min: " + std::to_string((int)(minutes_ans)) +
					"\nSec: " + std::to_string((int)(seconds_ans)) +
					"\nMsec: " + std::to_string((int)(milliseconds_ans))
				);

				WH->MainWindow_ID = "MAIN";
				WH->EnableWindow("MAIN");
				WH->EnableWindow("SMIC");
				InfoLine->SafeStringReplace("Integration was succsessfully finished");
			});
			th.detach();
		}
		void IntegrateTime()
		{
			WH->MainWindow_ID = "SMIC";
			WH->DisableAllWindows();
			auto InfoLine = (*(*WH)["SMIC"])["FLL"];
			auto UIMinutes = (InputField*)(*(*WH)["SMIC"])["INT_MIN"];
			auto UISeconds = (InputField*)(*(*WH)["SMIC"])["INT_SEC"];
			auto UIMilliseconds = (InputField*)(*(*WH)["SMIC"])["INT_MSC"];
			auto UIOutput = (TextBox*)(*(*WH)["SMIC"])["ANSWER"];
			InfoLine->SafeStringReplace("Integration has begun");

			double seconds_limit = 0;
			seconds_limit += std::stoi(UIMinutes->GetCurrentInput("0")) * 60.;
			seconds_limit += std::stoi(UISeconds->GetCurrentInput("0"));
			seconds_limit += std::stoi(UIMilliseconds->GetCurrentInput("0")) / 1000.;

			std::int64_t prev_tick = 0, cur_tick = 0;
			double cur_seconds = 0;
			double prev_second = 0;
			double PPQ = SMICptr->PPQ;
			double prev_tempo = 120;
			std::int64_t last_tick = (*SMICptr->TempoMap.rbegin()).first;
			for (auto& cur_pair : SMICptr->TempoMap/*; cur_pair != SMICptr->TempoMap.end(); cur_pair++*/)
			{
				cur_tick = cur_pair.first;
				cur_seconds += (cur_tick - prev_tick) * (60 / (prev_tempo * PPQ));
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

			UIOutput->SafeStringReplace("Tick: " + std::to_string(tick));

			WH->MainWindow_ID = "MAIN";
			WH->EnableWindow("MAIN");
			WH->EnableWindow("SMIC");
			InfoLine->SafeStringReplace("Integration was succsessfully finished");
		}
	}
	void OnApplySettings()
	{
		if (currentID < 0 && currentID >= _Data.Files.size())
		{
			ThrowAlert_Error("You cannot apply current settings to file with ID " + std::to_string(currentID));
			return;
		}
		std::int32_t T;
		std::string CurStr = "";
		auto SMPASptr = (*WH)["SMPAS"];

		CurStr = ((InputField*)(*SMPASptr)["PPQN"])->GetCurrentInput("0");
		if (CurStr.size())
		{
			T = std::stoi(CurStr);
			if (T)_Data[currentID].NewPPQN = T;
			else _Data[currentID].NewPPQN = _Data.GlobalPPQN;
		}

		CurStr = ((InputField*)(*SMPASptr)["TEMPO"])->GetCurrentInput("0");
		if (CurStr.size())
		{
			float F = stof(CurStr);
			_Data[currentID].NewTempo = F;
		}

		CurStr = ((InputField*)(*SMPASptr)["OFFSET"])->GetCurrentInput("0");
		if (CurStr.size())
		{
			T = stoll(CurStr);
			_Data[currentID].OffsetTicks = T;
		}

		CurStr = ((InputField*)(*SMPASptr)["GROUPID"])->GetCurrentInput("0");
		if (CurStr.size())
		{
			T = stoi(CurStr);
			if (T != _Data[currentID].GroupID)
			{
				_Data[currentID].GroupID = T;
				ThrowAlert_Error("Manual GroupID editing might cause significant drop of processing perfomance!");
			}
		}

		CurStr = ((InputField*)(*SMPASptr)["SELECT_START"])->GetCurrentInput("0");
		if (CurStr.size())
		{
			T = stoll(CurStr);
			_Data[currentID].SelectionStart = T;
		}

		CurStr = ((InputField*)(*SMPASptr)["SELECT_LENGTH"])->GetCurrentInput("-1");
		if (CurStr.size())
		{
			T = stoll(CurStr);
			_Data[currentID].SelectionLength = T;
		}

		_Data[currentID].AllowLegacyRunningStatusMetaIgnorance = (((CheckBox*)(*SMPASptr)["LEGACY_META_RSB_BEHAVIOR"])->State);

		if (_Data[currentID].AllowLegacyRunningStatusMetaIgnorance)
			std::cout << "WARNING: Legacy way of treating running status events can also allow major corruptions of midi structure!" << std::endl;

		_Data[currentID].SetBoolSetting(_BoolSettings::remove_empty_tracks, (((CheckBox*)(*SMPASptr)["BOOL_REM_TRCKS"])->State));
		_Data[currentID].SetBoolSetting(_BoolSettings::remove_remnants, (((CheckBox*)(*SMPASptr)["BOOL_REM_REM"])->State));
		_Data[currentID].SetBoolSetting(_BoolSettings::all_instruments_to_piano, (((CheckBox*)(*SMPASptr)["BOOL_PIANO_ONLY"])->State));
		_Data[currentID].SetBoolSetting(_BoolSettings::ignore_tempos, (((CheckBox*)(*SMPASptr)["BOOL_IGN_TEMPO"])->State));
		_Data[currentID].SetBoolSetting(_BoolSettings::ignore_pitches, (((CheckBox*)(*SMPASptr)["BOOL_IGN_PITCH"])->State));
		_Data[currentID].SetBoolSetting(_BoolSettings::ignore_notes, (((CheckBox*)(*SMPASptr)["BOOL_IGN_NOTES"])->State));
		_Data[currentID].SetBoolSetting(_BoolSettings::ignore_all_but_tempos_notes_and_pitch, (((CheckBox*)(*SMPASptr)["BOOL_IGN_ALL_EX_TPS"])->State));

		_Data[currentID].AllowSysex = (((CheckBox*)(*SMPASptr)["ALLOW_SYSEX"])->State);
		_Data[currentID].RSBCompression = ((CheckBox*)(*SMPASptr)["RSB_COMPRESS"])->State;
		_Data[currentID].ChannelsSplit = ((CheckBox*)(*SMPASptr)["SPLIT_TRACKS"])->State;
		_Data[currentID].CollapseMIDI = ((CheckBox*)(*SMPASptr)["COLLAPSE_MIDI"])->State;
		_Data[currentID].ApplyOffsetAfter = ((CheckBox*)(*SMPASptr)["APPLY_OFFSET_AFTER"])->State; 
		auto& inplaceMergeState = ((CheckBox*)(*SMPASptr)["INPLACE_MERGE"])->State;

		inplaceMergeState &= !_Data[currentID].RSBCompression;
		_Data[currentID].InplaceMergeEnabled = inplaceMergeState;
	}
	void OnApplyBS2A()
	{
		if (currentID < 0 && currentID >= _Data.Files.size())
		{
			ThrowAlert_Error("You cannot apply current settings to file with ID " + std::to_string(currentID));
			return;
		}
		OnApplySettings();
		for (auto Y = _Data.Files.begin(); Y != _Data.Files.end(); ++Y)
		{
			DefaultBoolSettings = Y->BoolSettings = _Data[currentID].BoolSettings;
			_Data.InplaceMergeFlag = Y->InplaceMergeEnabled = _Data[currentID].InplaceMergeEnabled;
			Y->AllowLegacyRunningStatusMetaIgnorance = _Data[currentID].AllowLegacyRunningStatusMetaIgnorance;
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
			auto Wptr = (*WH)["CAT"];
			auto CATptr = (CAT_Piano*)((*Wptr)["CAT_ITSELF"]);
			if (!_Data[currentID].KeyMap)
				_Data[currentID].KeyMap = std::make_shared<CutAndTransposeKeys>(0, 127, 0);
			CATptr->PianoTransform = _Data[currentID].KeyMap;
			CATptr->UpdateInfo();
			WH->EnableWindow("CAT");
		}
		void OnReset()
		{
			auto Wptr = (*WH)["CAT"];
			auto CATptr = (CAT_Piano*)((*Wptr)["CAT_ITSELF"]);
			CATptr->PianoTransform->Max = 255;
			CATptr->PianoTransform->Min = 0;
			CATptr->PianoTransform->TransposeVal = 0;
			CATptr->UpdateInfo();
		}
		void OnCDT128()
		{
			auto Wptr = (*WH)["CAT"];
			auto CATptr = (CAT_Piano*)((*Wptr)["CAT_ITSELF"]);
			CATptr->PianoTransform->Max = 127;
			CATptr->PianoTransform->Min = 0;
			CATptr->PianoTransform->TransposeVal = 0;
			CATptr->UpdateInfo();
		}
		void On0_127to128_255()
		{
			auto Wptr = (*WH)["CAT"];
			auto CATptr = (CAT_Piano*)((*Wptr)["CAT_ITSELF"]);
			CATptr->PianoTransform->Max = 127;
			CATptr->PianoTransform->Min = 0;
			CATptr->PianoTransform->TransposeVal = 128;
			CATptr->UpdateInfo();
		}
		void OnCopy()
		{
			auto Wptr = (*WH)["CAT"];
			auto CATptr = (CAT_Piano*)((*Wptr)["CAT_ITSELF"]);
			TMax = CATptr->PianoTransform->Max;
			TMin = CATptr->PianoTransform->Min;
			TTransp = CATptr->PianoTransform->TransposeVal;
		}
		void OnPaste()
		{
			auto Wptr = (*WH)["CAT"];
			auto CATptr = (CAT_Piano*)((*Wptr)["CAT_ITSELF"]);
			CATptr->PianoTransform->Max = TMax;
			CATptr->PianoTransform->Min = TMin;
			CATptr->PianoTransform->TransposeVal = TTransp;
			CATptr->UpdateInfo();
		}
		void OnDelete()
		{
			auto Wptr = (*WH)["CAT"];
			((CAT_Piano*)((*Wptr)["CAT_ITSELF"]))->PianoTransform = nullptr;
			WH->DisableWindow("CAT");
			_Data[currentID].KeyMap = nullptr;
		}
	}
	namespace VolumeMap
	{
		void OnVolMap()
		{
			auto Wptr = (*WH)["VM"];
			auto VM = ((PLC_VolumeWorker*)(*Wptr)["VM_PLC"]);
			auto IFDeg = ((InputField*)(*Wptr)["VM_DEGREE"]);
			((Button*)(*Wptr)["VM_SETMODE"])->SafeStringReplace("Single");
			IFDeg->UpdateInputString("1");
			IFDeg->CurrentString.clear();
			VM->ActiveSetting = 0;
			VM->Hovered = 0;
			VM->RePutMode = 0;
			if (!_Data[currentID].VolumeMap)
				_Data[currentID].VolumeMap = std::make_shared<PLC<std::uint8_t, std::uint8_t>>();
			VM->PLC_bb = _Data[currentID].VolumeMap;
			WH->EnableWindow("VM");
		}
		void OnDegreeShape()
		{
			auto Wptr = (*WH)["VM"];
			auto VM = ((PLC_VolumeWorker*)(*Wptr)["VM_PLC"]);
			if (VM->PLC_bb)
			{
				auto IFDeg = ((InputField*)(*Wptr)["VM_DEGREE"]);
				float Degree = std::stof(IFDeg->GetCurrentInput("0"));
				VM->PLC_bb->ConversionMap.clear();
				VM->PLC_bb->ConversionMap[127] = 127;
				for (int i = 0; i < 128; i++) 
					VM->PLC_bb->InsertNewPoint(i, std::ceil(std::pow(i / 127., Degree) * 127.));
			}
			else
				ThrowAlert_Error("If you see this message, some error might have happen, since PLC_bb is null");
		}
		void OnSimplify()
		{
			auto Wptr = (*WH)["VM"];
			auto VM = ((PLC_VolumeWorker*)(*Wptr)["VM_PLC"]);
			if (VM->PLC_bb)
			{
				VM->_MakeMapMoreSimple();
			} else
				ThrowAlert_Error("If you see this message, some error might have happen, since PLC_bb is null");
		}
		void OnTrace()
		{
			auto Wptr = (*WH)["VM"];
			auto VM = ((PLC_VolumeWorker*)(*Wptr)["VM_PLC"]);
			if (VM->PLC_bb)
			{
				if (VM->PLC_bb->ConversionMap.empty())return;
				std::uint8_t C[256]{};
				for (int i = 0; i < 255; i++)
					C[i] = VM->PLC_bb->AskForValue(i);
				for (int i = 0; i < 255; i++)
					VM->PLC_bb->ConversionMap[i] = C[i];
			}
			else
				ThrowAlert_Error("If you see this message, some error might have happen, since PLC_bb is null");
		}
		void OnSetModeChange() 
		{
			auto Wptr = (*WH)["VM"];
			auto VM = ((PLC_VolumeWorker*)(*Wptr)["VM_PLC"]);
			if (VM->PLC_bb)
			{
				VM->RePutMode = !VM->RePutMode;
				((Button*)(*Wptr)["VM_SETMODE"])->SafeStringReplace(((VM->RePutMode) ? "Double" : "Single"));
			}
			else
				ThrowAlert_Error("If you see this message, some error might have happen, since PLC_bb is null");
		}
		void OnErase()
		{
			auto Wptr = (*WH)["VM"];
			auto VM = ((PLC_VolumeWorker*)(*Wptr)["VM_PLC"]);
			if (VM->PLC_bb)
				VM->PLC_bb->ConversionMap.clear();
			else
				ThrowAlert_Error("If you see this message, some error might have happen, since PLC_bb is null");
		}
		void OnDelete()
		{
			if (_Data[currentID].VolumeMap)
				_Data[currentID].VolumeMap = nullptr;
			auto Wptr = (*WH)["VM"];
			auto VM = ((PLC_VolumeWorker*)(*Wptr)["VM_PLC"]);
			VM->PLC_bb = nullptr;
			WH->DisableWindow("VM");
		}
	}
	void OnPitchMap()
	{
		ThrowAlert_Error("Having hard time thinking of how to implement it...\nNot available... yet...");
	}
}

void OnRem()
{
	auto ptr = _WH_t("MAIN", "List", SelectablePropertedList*);
	for (auto ID = ptr->SelectedID.rbegin(); ID != ptr->SelectedID.rend(); ++ID)
		_Data.RemoveByID(*ID);
	ptr->RemoveSelected();
	WH->DisableAllWindows();
	_Data.SetGlobalPPQN();
	_Data.ResolveSubdivisionProblem_GroupIDAssign();
}

void OnRemAll()
{
	auto ptr = _WH_t("MAIN", "List", SelectablePropertedList*);
	WH->DisableAllWindows();
	while (_Data.Files.size())
	{
		_Data.RemoveByID(0);
		ptr->SafeRemoveStringByID(0);
	}
	//_Data.SetGlobalPPQN();
	//_Data.ResolveSubdivisionProblem_GroupIDAssign();
}

void OnSubmitGlobalPPQN()
{
	auto pptr = (*WH)["PROMPT"];
	std::string t = ((InputField*)(*pptr)["FLD"])->GetCurrentInput("0");
	std::uint16_t PPQN = (t.size()) ? stoi(t) : _Data.GlobalPPQN;
	_Data.SetGlobalPPQN(PPQN, true);
	WH->DisableWindow("PROMPT");
	//PropsAndSets::OGPInMIDIList(PropsAndSets::currentID);
}

void OnGlobalPPQN()
{
	WH->ThrowPrompt("New value will be assigned to every MIDI\n(in settings)", "Global PPQN", OnSubmitGlobalPPQN, _Align::center, InputField::Type::NaturalNumbers, std::to_string(_Data.GlobalPPQN), 5);
}

void OnSubmitGlobalOffset()
{
	auto pptr = (*WH)["PROMPT"];
	std::string t = ((InputField*)(*pptr)["FLD"])->GetCurrentInput("0");
	std::uint32_t O = (t.size()) ? std::stoi(t) : _Data.GlobalOffset;
	_Data.SetGlobalOffset(O);
	WH->DisableWindow("PROMPT");
	//PropsAndSets::OGPInMIDIList(PropsAndSets::currentID);
}
void OnGlobalOffset()
{
	WH->ThrowPrompt("Sets new global offset", "Global Offset", OnSubmitGlobalOffset, _Align::center, InputField::Type::WholeNumbers, std::to_string(_Data.GlobalOffset), 10);
	std::cout << _Data.GlobalOffset << " " << std::to_string(_Data.GlobalOffset) << std::endl;
}

void OnSubmitGlobalTempo()
{
	auto pptr = (*WH)["PROMPT"];
	std::string t = ((InputField*)(*pptr)["FLD"])->GetCurrentInput("0");
	float Tempo = (t.size()) ? std::stof(t) : _Data.GlobalNewTempo;
	_Data.SetGlobalTempo(Tempo);
	WH->DisableWindow("PROMPT");
	//PropsAndSets::OGPInMIDIList(PropsAndSets::currentID);
}
void OnGlobalTempo()
{
	WH->ThrowPrompt("Sets specific tempo value to every MIDI\n(in settings)", "Global S. Tempo\0", OnSubmitGlobalTempo, _Align::center, InputField::Type::FP_PositiveNumbers, std::to_string(_Data.GlobalNewTempo), 8);
}

void _OnResolve()
{
	_Data.ResolveSubdivisionProblem_GroupIDAssign();
}

void OnRemVolMaps()
{
	((PLC_VolumeWorker*)(*((*WH)["VM"]))["VM_PLC"])->PLC_bb = NULL;
	WH->DisableWindow("VM");
	for (int i = 0; i < _Data.Files.size(); i++)
		_Data[i].VolumeMap = nullptr;
}
void OnRemCATs()
{
	WH->DisableWindow("CAT");
	for (int i = 0; i < _Data.Files.size(); i++)
		_Data[i].KeyMap = nullptr;
}
void OnRemPitchMaps()
{
	//WH->DisableWindow("CAT");
	ThrowAlert_Error("Currently pitch maps can not be created and/or deleted :D");
	for (int i = 0; i < _Data.Files.size(); i++)
		_Data[i].PitchBendMap = nullptr;
}
void OnRemAllModules()
{
	OnRemVolMaps();
	OnRemCATs();
	//OnRemPitchMaps();
}

namespace Settings
{
	int ShaderMode = 0;
#ifdef WINDOWS
	WinReg::RegKey RegestryAccess;
#endif
	void OnSettings()
	{
		WH->EnableWindow("APP_SETTINGS");//_Data.DetectedThreads
		//WH->ThrowAlert("Please read the docs! Changing some of these settings might cause graphics driver failure!","Warning!",SpecialSigns::DrawExTriangle,1,0x007FFFFF,0x7F7F7FFF);
		auto pptr = (*WH)["APP_SETTINGS"];
		((InputField*)(*pptr)["AS_BCKGID"])->UpdateInputString(std::to_string(ShaderMode));
		((InputField*)(*pptr)["AS_ROT_ANGLE"])->UpdateInputString(std::to_string(dumb_rotation_angle));
		((InputField*)(*pptr)["AS_THREADS_COUNT"])->UpdateInputString(std::to_string(_Data.DetectedThreads));

		((CheckBox*)((*pptr)["BOOL_REM_TRCKS"]))->State = DefaultBoolSettings & _BoolSettings::remove_empty_tracks;
		((CheckBox*)((*pptr)["BOOL_REM_REM"]))->State = DefaultBoolSettings & _BoolSettings::remove_remnants;
		((CheckBox*)((*pptr)["BOOL_PIANO_ONLY"]))->State = DefaultBoolSettings & _BoolSettings::all_instruments_to_piano;
		((CheckBox*)((*pptr)["BOOL_IGN_TEMPO"]))->State = DefaultBoolSettings & _BoolSettings::ignore_tempos;
		((CheckBox*)((*pptr)["BOOL_IGN_PITCH"]))->State = DefaultBoolSettings & _BoolSettings::ignore_pitches;
		((CheckBox*)((*pptr)["BOOL_IGN_NOTES"]))->State = DefaultBoolSettings & _BoolSettings::ignore_notes;
		((CheckBox*)((*pptr)["BOOL_IGN_ALL_EX_TPS"]))->State = DefaultBoolSettings & _BoolSettings::ignore_all_but_tempos_notes_and_pitch;

		((CheckBox*)((*pptr)["SPLIT_TRACKS"]))->State = _Data.ChannelsSplit;
		((CheckBox*)((*pptr)["RSB_COMPRESS"]))->State = _Data.RSBCompression;
		((CheckBox*)((*pptr)["COLLAPSE_MIDI"]))->State = _Data.CollapseMIDI; 
		((CheckBox*)((*pptr)["APPLY_OFFSET_AFTER"]))->State = _Data.ApplyOffsetAfter;
		
		((CheckBox*)((*pptr)["INPLACE_MERGE"]))->State = _Data.InplaceMergeFlag;
		((CheckBox*)((*pptr)["AUTOUPDATECHECK"]))->State = check_autoupdates;

	}
	void OnSetApply()
	{
		bool isRegestryOpened = false;
		try
		{
#ifdef WINDOWS
			Settings::RegestryAccess.Open(HKEY_CURRENT_USER, default_reg_path);
#endif
			isRegestryOpened = true;
		}
		catch (...)
		{
			std::cout << "RK opening failed\n";
		}
		auto pptr = (*WH)["APP_SETTINGS"];
		std::string T;

		T = ((InputField*)(*pptr)["AS_BCKGID"])->GetCurrentInput("0");
		std::cout << "AS_BCKGID " << T << std::endl;
		if (T.size())
		{
			ShaderMode = std::stoi(T);
#ifdef WINDOWS
			if (isRegestryOpened)TRY_CATCH(RegestryAccess.SetDwordValue(L"AS_BCKGID", ShaderMode); , "Failed on setting AS_BCKGID")
#endif
		}
		std::cout << ShaderMode << std::endl;

		T = ((InputField*)(*pptr)["AS_ROT_ANGLE"])->GetCurrentInput("0");
		std::cout << "ROT_ANGLE " << T << std::endl;
		if (T.size() && !is_fonted)
			dumb_rotation_angle = stof(T);
		std::cout << dumb_rotation_angle << std::endl;

		T = ((InputField*)(*pptr)["AS_THREADS_COUNT"])->GetCurrentInput(std::to_string(_Data.DetectedThreads));
		std::cout << "AS_THREADS_COUNT " << T << std::endl;
		if (T.size())
		{
			_Data.DetectedThreads = stoi(T);
			_Data.ResolveSubdivisionProblem_GroupIDAssign();
#ifdef WINDOWS
			if (isRegestryOpened)TRY_CATCH(RegestryAccess.SetDwordValue(L"AS_THREADS_COUNT", _Data.DetectedThreads); , "Failed on setting AS_THREADS_COUNT")
#endif
		}
		std::cout << _Data.DetectedThreads << std::endl;

		DefaultBoolSettings = (DefaultBoolSettings & (~_BoolSettings::remove_empty_tracks)) | (_BoolSettings::remove_empty_tracks * (!!((CheckBox*)(*pptr)["BOOL_REM_TRCKS"])->State));
		DefaultBoolSettings = (DefaultBoolSettings & (~_BoolSettings::remove_remnants)) | (_BoolSettings::remove_remnants * (!!((CheckBox*)(*pptr)["BOOL_REM_REM"])->State));
		DefaultBoolSettings = (DefaultBoolSettings & (~_BoolSettings::all_instruments_to_piano)) | (_BoolSettings::all_instruments_to_piano * (!!((CheckBox*)(*pptr)["BOOL_PIANO_ONLY"])->State));
		DefaultBoolSettings = (DefaultBoolSettings & (~_BoolSettings::ignore_tempos)) | (_BoolSettings::ignore_tempos * (!!((CheckBox*)(*pptr)["BOOL_IGN_TEMPO"])->State));
		DefaultBoolSettings = (DefaultBoolSettings & (~_BoolSettings::ignore_pitches)) | (_BoolSettings::ignore_pitches * (!!((CheckBox*)(*pptr)["BOOL_IGN_PITCH"])->State));
		DefaultBoolSettings = (DefaultBoolSettings & (~_BoolSettings::ignore_notes)) | (_BoolSettings::ignore_notes * (!!((CheckBox*)(*pptr)["BOOL_IGN_NOTES"])->State));
		DefaultBoolSettings = (DefaultBoolSettings & (~_BoolSettings::ignore_all_but_tempos_notes_and_pitch)) | (_BoolSettings::ignore_all_but_tempos_notes_and_pitch * (!!((CheckBox*)(*pptr)["BOOL_IGN_ALL_EX_TPS"])->State));

		check_autoupdates = ((CheckBox*)(*pptr)["AUTOUPDATECHECK"])->State;

		_Data.ChannelsSplit = ((CheckBox*)((*pptr)["SPLIT_TRACKS"]))->State;
		_Data.RSBCompression = ((CheckBox*)((*pptr)["RSB_COMPRESS"]))->State;

		_Data.CollapseMIDI = ((CheckBox*)((*pptr)["COLLAPSE_MIDI"]))->State;
		_Data.ApplyOffsetAfter = ((CheckBox*)((*pptr)["APPLY_OFFSET_AFTER"]))->State;

		if (isRegestryOpened)
		{
#ifdef WINDOWS
			TRY_CATCH(RegestryAccess.SetDwordValue(L"AUTOUPDATECHECK", AutoUpdatesCheck);, "Failed on setting AUTOUPDATECHECK")
			TRY_CATCH(RegestryAccess.SetDwordValue(L"SPLIT_TRACKS", _Data.ChannelsSplit);, "Failed on setting SPLIT_TRACKS")
			TRY_CATCH(RegestryAccess.SetDwordValue(L"COLLAPSE_MIDI", _Data.CollapseMIDI);, "Failed on setting COLLAPSE_MIDI")
			TRY_CATCH(RegestryAccess.SetDwordValue(L"APPLY_OFFSET_AFTER", _Data.CollapseMIDI);, "Failed on setting APPLY_OFFSET_AFTER")
			//TRY_CATCH(RegestryAccess.SetDwordValue(L"RSB_COMPRESS", check_autoupdates);, "Failed on setting RSB_COMPRESS")
			TRY_CATCH(RegestryAccess.SetDwordValue(L"DEFAULT_BOOL_SETTINGS", DefaultBoolSettings);, "Failed on setting DEFAULT_BOOL_SETTINGS")
			TRY_CATCH(RegestryAccess.SetDwordValue(L"FONTSIZE", lFontSymbolsInfo::Size); , "Failed on setting FONTSIZE")
			TRY_CATCH(RegestryAccess.SetDwordValue(L"FLOAT_FONTHTW", *(std::uint32_t*)(&lFONT_HEIGHT_TO_WIDTH)); , "Failed on setting FLOAT_FONTHTW")
#endif
		}

		_Data.InplaceMergeFlag = (((CheckBox*)(*pptr)["INPLACE_MERGE"])->State);

#ifdef WINDOWS
		if (isRegestryOpened)
			TRY_CATCH(RegestryAccess.SetDwordValue(L"AS_INPLACE_FLAG", _Data.InplaceMergeFlag);, "Failed on setting AS_INPLACE_FLAG")
#endif

		((InputField*)(*pptr)["AS_FONT_NAME"])->PutIntoSource();
		std_unicode_string ws(default_font_name.begin(), default_font_name.end());

#ifdef WINDOWS
		if (isRegestryOpened)
			TRY_CATCH(RegestryAccess.SetStringValue(L"COLLAPSEDFONTNAME_POST1P4", ws); , "Failed on setting COLLAPSEDFONTNAME_POST1P4")

		Settings::RegestryAccess.Close();
#endif
	}
	void ChangeIsFontedVar()
	{
		is_fonted = !is_fonted;
		SetIsFontedVar(is_fonted);
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
	std::pair<float, float> T{ 0.f, 0.f };
	std::int32_t SideAmount = ceil(sqrt(Amount));
	T.first = (0 - (Position % SideAmount) + ((SideAmount - 1) / 2.f)) * UnitSize;
	T.second = (0 - (Position / SideAmount) + ((SideAmount - 1) / 2.f)) * UnitSize * HeightRel;
	return T;
}

void OnStart()
{
	if (_Data.Files.empty())
		return;

	WH->MainWindow_ID = "SMRP_CONTAINER";
	WH->DisableAllWindows();
	WH->EnableWindow("SMRP_CONTAINER");

	GlobalMCTM = _Data.MCTM_Constructor();

	auto start_timepoint = std::chrono::high_resolution_clock::now();

	GlobalMCTM->StartProcessingMIDIs();

	MoveableWindow* MW;
	std::uint32_t ID = 0;

	MW = (*WH)["SMRP_CONTAINER"];
	std::vector<std::string> undesired_window_activities;

	for (auto& single_activity_pair : MW->WindowActivities) 
	{
		if (single_activity_pair.first.substr(0, 6) == "SMRP_C")
			undesired_window_activities.push_back(single_activity_pair.first);
		//MW->DeleteUIElementByName(singleActivity.first);
	}

	for(auto& singleActivityName: undesired_window_activities)
		MW->DeleteUIElementByName(singleActivityName);

	auto timer_ptr = (InputField*)(*MW)["TIMER"];
	auto now = std::chrono::high_resolution_clock::now();
	auto difference = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_timepoint);
	timer_ptr->SafeStringReplace(std::to_string(difference.count() * 0.001) + " s");

	std::this_thread::sleep_for(std::chrono::milliseconds(100));

	decltype(GlobalMCTM->currently_processed) currently_processed_copy;

	{
		GlobalMCTM->currently_processed_locker.lock();
		currently_processed_copy = GlobalMCTM->currently_processed;
		GlobalMCTM->currently_processed_locker.unlock();
	}

	for (ID = 0; ID < currently_processed_copy.size(); ID++)
	{
		auto Q = GetPositionForOneOf(ID, currently_processed_copy.size(), 140, 0.7);
		auto Vis = new SMRP_Vis(Q.first, Q.second, System_White);
		std::string temp = "";
		MW->AddUIElement(temp = "SMRP_C" + std::to_string(ID), Vis);


		std::thread([](std::shared_ptr<MIDICollectionThreadedMerger> pMCTM, SMRP_Vis* pVIS, std::uint32_t ID)
		{
			std::string SID = "SMRP_C" + std::to_string(ID);
			std::cout << SID << " Processing started" << std::endl;
			bool finished = false;
			while (GlobalMCTM->CheckSMRPProcessing()) 
			{
				GlobalMCTM->currently_processed_locker.lock();
				finished = GlobalMCTM->currently_processed[ID].second->finished;
				pVIS->SetSMRP(GlobalMCTM->currently_processed[ID]);
				GlobalMCTM->currently_processed_locker.unlock();

				std::this_thread::sleep_for(std::chrono::milliseconds(66));
			}
			std::cout << SID << " Processing stopped" << std::endl;
		}, GlobalMCTM, Vis, ID).detach();
	}

	std::thread([](std::shared_ptr<MIDICollectionThreadedMerger> pMCTM, SAFCData* SD, MoveableWindow* MW)
	{
		while (pMCTM->CheckSMRPProcessingAndStartNextStep())
			//that's some really dumb synchronization... TODO: MAKE BETTER
			std::this_thread::sleep_for(std::chrono::milliseconds(100));

		std::cout << "SMRP: Out from sleep\n" << std::flush;
		for (int i = 0; i <= pMCTM->currently_processed.size(); i++)
			MW->DeleteUIElementByName("SMRP_C" + std::to_string(i));

		MW->SafeChangePosition_Argumented(0, 0, 0);
		(*MW)["IM"] = new BoolAndWORDChecker<decltype(pMCTM->IntermediateInplaceFlag), decltype(pMCTM->IITrackCount)>
			(-100., 0., System_White, &(pMCTM->IntermediateInplaceFlag), &(pMCTM->IITrackCount));
		(*MW)["RM"] = new BoolAndWORDChecker<decltype(pMCTM->IntermediateInplaceFlag), decltype(pMCTM->IRTrackCount)>
			(100., 0., System_White, &(pMCTM->IntermediateRegularFlag), &(pMCTM->IRTrackCount));
		std::thread ILO([](std::shared_ptr<MIDICollectionThreadedMerger> pMCTM, SAFCData* SD, MoveableWindow* MW)
		{
			while (!pMCTM->CheckRIMerge())
			{
				std::this_thread::sleep_for(std::chrono::milliseconds(66));
			}

			std::cout << "RI: Out from sleep!\n";
			MW->DeleteUIElementByName("IM");
			MW->DeleteUIElementByName("RM");
			MW->SafeChangePosition_Argumented(0, 0, 0);
			(*MW)["FM"] = new BoolAndWORDChecker<decltype(pMCTM->CompleteFlag), int>
				(0., 0., System_White, &(pMCTM->CompleteFlag), NULL);
		}, pMCTM, SD, MW);
		ILO.detach();
	}, GlobalMCTM, &_Data, MW).detach();

	std::thread([](std::shared_ptr<MIDICollectionThreadedMerger> pMCTM, MoveableWindow* MW, std::chrono::high_resolution_clock::time_point start_timepoint)
	{
		auto timer_ptr = (InputField*)(*MW)["TIMER"];
		while (!pMCTM->CompleteFlag)
		{
			auto now = std::chrono::high_resolution_clock::now();
			auto difference = std::chrono::duration_cast<std::chrono::microseconds>(now - start_timepoint);
			timer_ptr->SafeStringReplace(std::to_string(difference.count() * 1.0e-6) + " s");
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
				ThrowAlert_Warning(std::move(message));
			}
		}
		std::cout << "F: Out from sleep!!!\n";
		MW->DeleteUIElementByName("FM");

		WH->DisableWindow(WH->MainWindow_ID);
		WH->MainWindow_ID = "MAIN";
		//WH->DisableAllWindows();
		WH->EnableWindow("MAIN");
		//pMCTM->ResetEverything();
	}, GlobalMCTM, MW, start_timepoint).detach();

	//ThrowAlert_Error("It's not done yet :p");
}

void OnSaveTo()
{
#ifdef WINDOWS
	_Data.SavePath = SOFD(L"Save final midi to...");
	size_t Pos = _Data.SavePath.rfind(L".mid");
	if (Pos >= _Data.SavePath.size() || Pos <= _Data.SavePath.size() - 4)
		_Data.SavePath += L".mid";
#else
	_Data.SavePath = SOFD("Save final midi to...");
	size_t Pos = _Data.SavePath.rfind(".mid");
	if (Pos >= _Data.SavePath.size() || Pos <= _Data.SavePath.size() - 4)
		_Data.SavePath += ".mid";
#endif
}

void RestoreRegSettings()
{
#ifdef WINDOWS
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
			std_unicode_string ws = Settings::RegestryAccess.GetStringValue(L"COLLAPSEDFONTNAME_POST1P4");//COLLAPSEDFONTNAME
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
		} catch (...) {	std::cout << "Exception thrown while restoring FLOAT_FONTHTW from registry\n"; }
		Settings::RegestryAccess.Close();
	}
#endif
}

void Init()
{
	//RestoreRegSettings();
#ifdef WINDOWS
	lFontSymbolsInfo::InitialiseFont(default_font_name, true);
#endif

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

	WH = std::make_shared<WindowsHandler>();

	std::stringstream MainWindowName;

#ifdef WINDOWS
	auto [maj, min, ver, build] = __versionTuple;
	MainWindowName << "SAFC v" << maj << "." << min << "." << ver << "." << build << "";
	#else
	MainWindowName << "SAFC for linux (custom v1.4.4.1)";
#endif
	SelectablePropertedList* SPL = new SelectablePropertedList(BS_List_Black_Small, NULL, PropsAndSets::OGPInMIDIList, -50, 172, 300, 12, 65, 30);
	MoveableWindow* T = new MoveableResizeableWindow(MainWindowName.str(), System_White, -200, 200, 400, 400, 0x3F3F3FAF, 0x7F7F7F7F, 0, [SPL](float dH, float dW, float NewHeight, float NewWidth) {
		constexpr float TopMargin = 200 - 172;
		constexpr float BottomMargin = 12;
		float SPLNewHight = (NewHeight - TopMargin - BottomMargin);
		float SPLNewWidth = SPL->Width + dW;
		SPL->SafeResize(SPLNewHight, SPLNewWidth);
	});
	((MoveableResizeableWindow*)T)->AssignMinDimentions(300, 300);
	((MoveableResizeableWindow*)T)->AssignPinnedActivities({
		"ADD_Butt", "REM_Butt", "REM_ALL_Butt", "GLOBAL_PPQN_Butt", "GLOBAL_OFFSET_Butt", "GLOBAL_TEMPO_Butt", "DELETE_ALL_VM", "DELETE_ALL_CAT", "DELETE_ALL_PITCHES",
		"DELETE_ALL_MODULES", "SETTINGS", "SAVE_AS", "START"
	}, MoveableResizeableWindow::PinSide::right);
	((MoveableResizeableWindow*)T)->AssignPinnedActivities({ "SETTINGS", "SAVE_AS", "START" }, MoveableResizeableWindow::PinSide::bottom);

	Button* Butt;
	(*T)["List"] = SPL;

	(*T)["ADD_Butt"] = new Button("Add MIDIs", System_White, OnAdd, 150, 172.5, 75, 12, 1, 0x00003FAF, 0xFFFFFFFF, 0x00003FFF, 0xFFFFFFFF, 0xF7F7F7FF, NULL, " ");
	(*T)["REM_Butt"] = new Button("Remove selected", System_White, OnRem, 150, 160, 75, 12, 1, 0x3F0000AF, 0xFFFFFFFF, 0x3F0000FF, 0xFFFFFFFF, 0xF7F7F7FF, NULL, " ");
	(*T)["REM_ALL_Butt"] = new Button("Remove all", System_White, OnRemAll, 150, 147.5, 75, 12, 1, 0xAF0000AF, 0xFFFFFFFF, 0xAF0000AF, 0xFFFFFFFF, 0xF7F7F7FF, System_White, "May cause lag");
	(*T)["GLOBAL_PPQN_Butt"] = new Button("Global PPQN", System_White, OnGlobalPPQN, 150, 122.5, 75, 12, 1, 0xFF3F00AF, 0xFFFFFFFF, 0xFF3F00AF, 0xFFFFFFFF, 0xF7F7F7FF, NULL, " ");
	(*T)["GLOBAL_OFFSET_Butt"] = new Button("Global offset", System_White, OnGlobalOffset, 150, 110, 75, 12, 1, 0xFF7F00AF, 0xFFFFFFFF, 0xFF7F00FF, 0xFFFFFFFF, 0xF7F7F7FF, NULL, " ");
	(*T)["GLOBAL_TEMPO_Butt"] = new Button("Global tempo", System_White, OnGlobalTempo, 150, 97.5, 75, 12, 1, 0xFFAF00AF, 0xFFFFFFFF, 0xFFAF00AF, 0xFFFFFFFF, 0xF7F7F7FF, NULL, " ");
	//(*T)["_FRESOLVE"] = new Button("_ForceResolve", System_White, _OnResolve, 150, 0, 75, 12, 1, 0x7F007F3F, 0xFFFFFF3F, 0x000000FF, 0xFFFFFF3F, 0xF7F7F73F, NULL, " ");
	(*T)["DELETE_ALL_VM"] = new Button("Remove vol. maps", System_White, OnRemVolMaps, 150, 72.5, 75, 12, 1,
		0x7F7F7FAF, 0xFFFFFFFF, 0x7F7F7FAF, 0xFFFFFFFF, 0xF7F7F7FF, NULL, " ");//0xFF007FAF
	(*T)["DELETE_ALL_CAT"] = new Button("Remove C&Ts", System_White, OnRemCATs, 150, 60, 75, 12, 1,
		0x7F7F7FAF, 0xFFFFFFFF, 0x7F7F7FAF, 0xFFFFFFFF, 0xF7F7F7FF, NULL, " ");
	(*T)["DELETE_ALL_PITCHES"] = new Button("Remove p. maps", System_White, OnRemPitchMaps, 150, 47.5, 75, 12, 1,
		0x7F7F7FAF, 0xFFFFFFFF, 0x7F7F7FAF, 0xFFFFFFFF, 0xF7F7F7FF, NULL, " ");
	(*T)["DELETE_ALL_MODULES"] = new Button("Remove modules", System_White, OnRemAllModules, 150, 35, 75, 12, 1,
		0x7F7F7FAF, 0xFFFFFFFF, 0x7F7F7FAF, 0xFFFFFFFF, 0xF7F7F7FF, NULL, " ");

	(*T)["SETTINGS"] = new Button("Settings...", System_White, Settings::OnSettings, 150, -140, 75, 12, 1,
		0x5F5F5FAF, 0xFFFFFFFF, 0x5F5F5FAF, 0xFFFFFFFF, 0xF7F7F7FF, NULL, " ");
	(*T)["SAVE_AS"] = new Button("Save as...", System_White, OnSaveTo, 150, -152.5, 75, 12, 1,
		0x3FAF00AF, 0xFFFFFFFF, 0x3FAF00AF, 0xFFFFFFFF, 0xF7F7F7FF, NULL, " ");
	(*T)["START"] = Butt = new Button("Start merging", System_White, OnStart, 150, -177.5, 75, 12, 1,
		0x000000AF, 0xFFFFFFFF, 0x000000AF, 0xFFFFFFFF, 0xF7F7F7FF, NULL, " ");//177.5

	(*WH)["MAIN"] = T;

	T = new MoveableWindow("Props. and sets.", System_White, -100, 100, 200, 200, 0x3F3F3FCF, 0x7F7F7F7F);
	(*T)["FileName"] = new TextBox("_", System_White, 0, 88.5 - WindowHeapSize, 6, 200 - 1.5 * WindowHeapSize, 7.5, 0, 0, 0, _Align::left, TextBox::VerticalOverflow::cut);
	(*T)["PPQN"] = new InputField(" ", -90 + WindowHeapSize, 75 - WindowHeapSize, 10, 25, System_White, PropsAndSets::PPQN, 0x007FFFFF, System_White, "PPQN is lesser than 65536.", 5, _Align::center, _Align::left, InputField::Type::NaturalNumbers);
	(*T)["TEMPO"] = new InputField(" ", -50 + WindowHeapSize, 75 - WindowHeapSize, 10, 45, System_White, PropsAndSets::TEMPO, 0x007FFFFF, System_White, "Specific tempo override field", 8, _Align::center, _Align::left, InputField::Type::FP_PositiveNumbers);
	(*T)["OFFSET"] = new InputField(" ", 20 + WindowHeapSize, 75 - WindowHeapSize, 10, 55, System_White, PropsAndSets::OFFSET, 0x007FFFFF, System_White, "Offset from begining in ticks", 10, _Align::center, _Align::right, InputField::Type::WholeNumbers);

	(*T)["APPLY_OFFSET_AFTER"] = new CheckBox(12.5 - WindowHeapSize, 75 - WindowHeapSize, 10, 0x007FFFFF, 0xFF7F00AF, 0x7FFF00AF, 1, 0, System_White, _Align::center, "Apply offset after PPQ change");

	(*T)["BOOL_REM_TRCKS"] = new CheckBox(-97.5 + WindowHeapSize, 55 - WindowHeapSize, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 1, System_White, _Align::left, "Remove empty tracks");
	(*T)["BOOL_REM_REM"] = new CheckBox(-82.5 + WindowHeapSize, 55 - WindowHeapSize, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 1, System_White, _Align::left, "Remove merge \"remnants\"");
	(*T)["BOOL_PIANO_ONLY"] = new CheckBox(-67.5 + WindowHeapSize, 55 - WindowHeapSize, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 1, System_White, _Align::left, "Replace all instruments with piano");
	(*T)["BOOL_IGN_TEMPO"] = new CheckBox(-52.5 + WindowHeapSize, 55 - WindowHeapSize, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 0, System_White, _Align::left, "Ignore tempo events");
	(*T)["BOOL_IGN_PITCH"] = new CheckBox(-37.5 + WindowHeapSize, 55 - WindowHeapSize, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 0, System_White, _Align::center, "Ignore pitch bending events");
	(*T)["BOOL_IGN_NOTES"] = new CheckBox(-22.5 + WindowHeapSize, 55 - WindowHeapSize, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 0, System_White, _Align::center, "Ignore note events");
	(*T)["BOOL_IGN_ALL_EX_TPS"] = new CheckBox(-7.5 + WindowHeapSize, 55 - WindowHeapSize, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 0, System_White, _Align::center, "Ignore everything except specified");
	(*T)["SPLIT_TRACKS"] = new CheckBox(7.5 + WindowHeapSize, 55 - WindowHeapSize, 10, 0x007FFFFF, 0xFF3F007F, 0x3FFF007F, 1, 0, System_White, _Align::center, "Multichannel split");
	(*T)["RSB_COMPRESS"] = new CheckBox(22.5 + WindowHeapSize, 55 - WindowHeapSize, 10, 0x007FFFFF, 0xFF3F007F, 0x3FFF007F, 1, 0, System_White, _Align::center, "Enable RSB compression");
	
	(*T)["BOOL_APPLY_TO_ALL_MIDIS"] = Butt = new Button("A2A", System_White, PropsAndSets::OnApplyBS2A, 80 - WindowHeapSize, 55 - WindowHeapSize, 15, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F003F, 0xFF7F00FF, System_White, "Sets \"bool settings\" to all midis");
	Butt->Tip->SafeChangePosition_Argumented(_Align::right, 87.5 - WindowHeapSize, Butt->Tip->CYpos);

	(*T)["INPLACE_MERGE"] = new CheckBox(97.5 - WindowHeapSize, 55 - WindowHeapSize, 10, 0x007FFFFF, 0xFF3F007F, 0x3FFF007F, 1, 0, System_White, _Align::right, "Enables/disables inplace merge");

	(*T)["GROUPID"] = new InputField(" ", 92.5 - WindowHeapSize, 75 - WindowHeapSize, 10, 20, System_White, PropsAndSets::PPQN, 0x007FFFFF, System_White, "Group ID...", 2, _Align::center, _Align::right, InputField::Type::NaturalNumbers);

	//(*T)["OR"] = Butt = new Button("OR", System_White, PropsAndSets::OR, 37.5, 15 - WindowHeapSize, 20, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F003F, 0xFF7F00FF, System_White, "Overlaps remover");
	//(*T)["SR"] = new Button("SR", System_White, PropsAndSets::SR, 12.5, 15 - WindowHeapSize, 20, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F003F, 0xFF7F00FF, System_White, "Sustains remover");

	(*T)["MIDIINFO"] = Butt = new Button("Collect info", System_White, PropsAndSets::InitializeCollecting, 20, 15 - WindowHeapSize, 65, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F003F, 0xFF7F00FF, System_White, "Collects additional info about the midi");
	Butt->Tip->SafeMove(-20, 0);

	(*T)["APPLY"] = new Button("Apply", System_White, PropsAndSets::OnApplySettings, 87.5 - WindowHeapSize, 15 - WindowHeapSize, 30, 10, 1, 0x7FAFFF3F, 0xFFFFFFFF, 0xFFAF7FFF, 0xFFAF7F3F, 0xFFAF7FFF, NULL, " ");

	(*T)["CUT_AND_TRANSPOSE"] = (Butt = new Button("Cut & Transpose...", System_White, PropsAndSets::CutAndTranspose::OnCaT, 45 - WindowHeapSize, 35 - WindowHeapSize, 85, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F003F, 0xFF7F00FF, System_White, "Cut and Transpose tool"));
	Butt->Tip->SafeChangePosition_Argumented(_Align::right, 100 - WindowHeapSize, Butt->Tip->CYpos);
	(*T)["PITCH_MAP"] = (Butt = new Button("Pitch map ...", System_White, PropsAndSets::OnPitchMap, -37.5 - WindowHeapSize, 15 - WindowHeapSize, 70, 10, 1, 0x7F7F7F3F, 0x7F7F7FFF, 0xFFFFFFFF, 0xFFFFFF3F, 0xFFFFFFFF, System_White, "Allows to transform pitches"));
	Butt->Tip->SafeChangePosition_Argumented(_Align::right, 100 - WindowHeapSize, Butt->Tip->CYpos);
	(*T)["VOLUME_MAP"] = (Butt = new Button("Volume map ...", System_White, PropsAndSets::VolumeMap::OnVolMap, -37.5 - WindowHeapSize, 35 - WindowHeapSize, 70, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F003F, 0xFF7F00FF, System_White, "Allows to transform volumes of notes"));
	Butt->Tip->SafeChangePosition_Argumented(_Align::right, 100 - WindowHeapSize, Butt->Tip->CYpos);

	(*T)["SELECT_START"] = new InputField(" ", -37.5 - WindowHeapSize, -5 - WindowHeapSize, 10, 70, System_White, NULL, 0x007FFFFF, System_White, "Selection start", 13, _Align::center, _Align::right, InputField::Type::NaturalNumbers);
	(*T)["SELECT_LENGTH"] = new InputField(" ", 37.5 - WindowHeapSize, -5 - WindowHeapSize, 10, 70, System_White, NULL, 0x007FFFFF, System_White, "Selection length", 14, _Align::center, _Align::right, InputField::Type::WholeNumbers);

	(*T)["LEGACY_META_RSB_BEHAVIOR"] = new CheckBox(97.5 - WindowHeapSize, -5 - WindowHeapSize, 10, 0x007FFFFF, 0xFF3F007F, 0x3FFF007F, 1, false, System_White, _Align::right, "Enables legacy RSB/Meta behavior");
	(*T)["ALLOW_SYSEX"] = new CheckBox(82.5 - WindowHeapSize, -5 - WindowHeapSize, 10, 0x007FFFFF, 0xFF3F007F, 0x3FFF007F, 1, 0, System_White, _Align::right, "Allow sysex events");	

	(*T)["COLLAPSE_MIDI"] = new CheckBox(97.5 - WindowHeapSize, 35 - WindowHeapSize, 10, 0x007FFFFF, 0xFF7F00AF, 0x7FFF00AF, 1, 0, System_White, _Align::right, "Collapse all tracks of a MIDI into one");

	(*T)["CONSTANT_PROPS"] = new TextBox("_Props text example_", System_White, 0, -57.5 - WindowHeapSize, 80 - WindowHeapSize, 200 - 1.5 * WindowHeapSize, 7.5, 0, 0, 1);

	(*WH)["SMPAS"] = T;//Selected midi properties and settings

	T = new MoveableWindow("Cut and Transpose.", System_White, -200, 50, 400, 100, 0x3F3F3FCF, 0x7F7F7F7F);
	(*T)["CAT_ITSELF"] = new CAT_Piano(0, 25 - WindowHeapSize, 1, 10, NULL);
	(*T)["CAT_SET_DEFAULT"] = new Button("Reset", System_White, PropsAndSets::CutAndTranspose::OnReset, -150, -10 - WindowHeapSize, 40, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F003F, 0xFF7F00FF, NULL, " ");
	(*T)["CAT_+128"] = new Button("0-127 -> 128-255", System_White, PropsAndSets::CutAndTranspose::On0_127to128_255, -85, -10 - WindowHeapSize, 80, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F003F, 0xFF7F00FF, NULL, " ");
	(*T)["CAT_CDT128"] = new Button("Cut down to 128", System_White, PropsAndSets::CutAndTranspose::OnCDT128, 0, -10 - WindowHeapSize, 80, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F003F, 0xFF7F00FF, NULL, " ");
	(*T)["CAT_COPY"] = new Button("Copy", System_White, PropsAndSets::CutAndTranspose::OnCopy, 65, -10 - WindowHeapSize, 40, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F003F, 0xFF7F00FF, NULL, " ");
	(*T)["CAT_PASTE"] = new Button("Paste", System_White, PropsAndSets::CutAndTranspose::OnPaste, 110, -10 - WindowHeapSize, 40, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F003F, 0xFF7F00FF, NULL, " ");
	(*T)["CAT_DELETE"] = new Button("Delete", System_White, PropsAndSets::CutAndTranspose::OnDelete, 155, -10 - WindowHeapSize, 40, 10, 1, 0xFF00FF3F, 0xFF00FFFF, 0xFFFFFFFF, 0xFF003F3F, 0xFF003FFF, NULL, " ");

	(*WH)["CAT"] = T;

	T = new MoveableWindow("Volume map.", System_White, -150, 150, 300, 350, 0x3F3F3FCF, 0x7F7F7F7F, 0x7F6F8FCF);
	(*T)["VM_PLC"] = new PLC_VolumeWorker(0, 0 - WindowHeapSize, 300 - WindowHeapSize * 2, 300 - WindowHeapSize * 2, std::make_shared<PLC<std::uint8_t, std::uint8_t>>());///todo: interface
	(*T)["VM_SSBDIIF"] = Butt = new Button("Shape alike x^y", System_White, PropsAndSets::VolumeMap::OnDegreeShape, -110 + WindowHeapSize, -150 - WindowHeapSize, 80, 10, 1, 0xFFFFFF3F, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFAFFF3F, 0xFFAFFFFF, System_White, "Where y is from frame bellow");///Set shape by degree in input field;
	Butt->Tip->SafeChangePosition_Argumented(_Align::left, -150 + WindowHeapSize, -160 - WindowHeapSize);
	(*T)["VM_DEGREE"] = new InputField("1", -140 + WindowHeapSize, -170 - WindowHeapSize, 10, 20, System_White, NULL, 0x007FFFFF, NULL, " ", 4, _Align::center, _Align::center, InputField::Type::FP_PositiveNumbers);
	(*T)["VM_ERASE"] = Butt = new Button("Erase points", System_White, PropsAndSets::VolumeMap::OnErase, -35 + WindowHeapSize, -150 - WindowHeapSize, 60, 10, 1, 0xFFFFFF3F, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFAFFF3F, 0xFFAFFFFF, NULL, "_");
	(*T)["VM_DELETE"] = new Button("Delete map", System_White, PropsAndSets::VolumeMap::OnDelete, 30 + WindowHeapSize, -150 - WindowHeapSize, 60, 10, 1, 0xFFAFAF3F, 0xFFAFAFFF, 0xFFEFEFFF, 0xFF7F3F7F, 0xFF1F1FFF, NULL, "_");
	(*T)["VM_SIMP"] = Butt = new Button("Simplify map", System_White, PropsAndSets::VolumeMap::OnSimplify, -70 - WindowHeapSize, -170 - WindowHeapSize, 60, 10, 1, 0xFFFFFF3F, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFAFFF3F, 0xFFAFFFFF, System_White, "Reduces amount of \"repeating\" points");
	Butt->Tip->SafeChangePosition_Argumented(_Align::left, -100 - WindowHeapSize, -160 - WindowHeapSize);
	(*T)["VM_TRACE"] = Butt = new Button("Trace map", System_White, PropsAndSets::VolumeMap::OnTrace, -35 + WindowHeapSize, -170 - WindowHeapSize, 60, 10, 1, 0xFFFFFF3F, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFAFAF3F, 0xFFAFAFFF, System_White, "Puts every point onto map");
	Butt->Tip->SafeChangePosition_Argumented(_Align::left, -65 + WindowHeapSize, -160 - WindowHeapSize);
	(*T)["VM_SETMODE"] = Butt = new Button("Single", System_White, PropsAndSets::VolumeMap::OnSetModeChange, 30 + WindowHeapSize, -170 - WindowHeapSize, 40, 10, 1, 0xFFFFFF3F, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFAFAF3F, 0xFFAFAFFF, NULL, "_");

	(*WH)["VM"] = T;

	T = new MoveableWindow("App settings", System_White, -100, 110, 200, 240, 0x3F3F3FCF, 0x7F7F7F7F);

	(*T)["AS_BCKGID"] = new InputField(std::to_string(Settings::ShaderMode), -35, 55 - WindowHeapSize, 10, 30, System_White, NULL, 0x007FFFFF, System_White, "Background ID", 2, _Align::center, _Align::right, InputField::Type::NaturalNumbers);

	(*T)["AS_GLOBALSETTINGS"] = new TextBox("Global settings for new MIDIs", System_White, 0, 85 - WindowHeapSize, 50, 200, 12, 0x007FFF1F, 0x007FFF7F, 1, _Align::center);
	(*T)["AS_APPLY"] = Butt = new Button("Apply", System_White, Settings::OnSetApply, 85 - WindowHeapSize, -87.5 - WindowHeapSize, 40, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, NULL, "_");
	(*T)["AS_EN_FONT"] = Butt = new Button((is_fonted) ? "Disable fonts" : "Enable fonts", System_White, Settings::ChangeIsFontedVar, 72.5 - WindowHeapSize, -67.5 - WindowHeapSize, 65, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, System_White, " ");
	(*T)["AS_ROT_ANGLE"] = new InputField(std::to_string(dumb_rotation_angle), -87.5 + WindowHeapSize, 55 - WindowHeapSize, 10, 30, System_White, NULL, 0x007FFFFF, System_White, "Rotation angle", 6, _Align::center, _Align::left, InputField::Type::FP_Any);
	(*T)["AS_FONT_SIZE"] = new WheelVariableChanger(Settings::ApplyFSWheel, -37.5, -82.5, lFontSymbolsInfo::Size, 1, System_White, "Font size", "Delta", WheelVariableChanger::Type::addictable);
	(*T)["AS_FONT_P"] = new WheelVariableChanger(Settings::ApplyRelWheel, -37.5, -22.5, lFONT_HEIGHT_TO_WIDTH, 0.01, System_White, "Font rel.", "Delta", WheelVariableChanger::Type::addictable);
	(*T)["AS_FONT_NAME"] = new InputField(default_font_name, 52.5 - WindowHeapSize, 55 - WindowHeapSize, 10, 100, _STLS_WhiteSmall, &default_font_name, 0x007FFFFF, System_White, "Font name", 32, _Align::center, _Align::left, InputField::Type::Text);

	(*T)["BOOL_REM_TRCKS"] = new CheckBox(-97.5 + WindowHeapSize, 95 - WindowHeapSize, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 1, System_White, _Align::left, "Remove empty tracks");
	(*T)["BOOL_REM_REM"] = new CheckBox(-82.5 + WindowHeapSize, 95 - WindowHeapSize, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 1, System_White, _Align::left, "Remove merge \"remnants\"");
	(*T)["BOOL_PIANO_ONLY"] = new CheckBox(-67.5 + WindowHeapSize, 95 - WindowHeapSize, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 1, System_White, _Align::left, "All instuments to piano");
	(*T)["BOOL_IGN_TEMPO"] = new CheckBox(-52.5 + WindowHeapSize, 95 - WindowHeapSize, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 0, System_White, _Align::left, "Ignore tempo events");
	(*T)["BOOL_IGN_PITCH"] = new CheckBox(-37.5 + WindowHeapSize, 95 - WindowHeapSize, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 0, System_White, _Align::center, "Ignore pitch bending events");
	(*T)["BOOL_IGN_NOTES"] = new CheckBox(-22.5 + WindowHeapSize, 95 - WindowHeapSize, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 0, System_White, _Align::center, "Ignore note events");
	(*T)["BOOL_IGN_ALL_EX_TPS"] = new CheckBox(-7.5 + WindowHeapSize, 95 - WindowHeapSize, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 0, System_White, _Align::center, "Ignore everything except specified");
	(*T)["SPLIT_TRACKS"] = new CheckBox(7.5 + WindowHeapSize, 95 - WindowHeapSize, 10, 0x007FFFFF, 0xFF3F007F, 0x3FFF007F, 1, 0, System_White, _Align::center, "Multichannel split");
	(*T)["RSB_COMPRESS"] = new CheckBox(22.5 + WindowHeapSize, 95 - WindowHeapSize, 10, 0x007FFFFF, 0xFF3F007F, 0x3FFF007F, 1, 0, System_White, _Align::center, "Enable RSB compression");	

	(*T)["ALLOW_SYSEX"] = new CheckBox(-97.5 + WindowHeapSize, 75 - WindowHeapSize, 10, 0x007FFFFF, 0xFF3F007F, 0x3FFF007F, 1, 0, System_White, _Align::left, "Allow sysex events");

	(*T)["BOOL_APPLY_TO_ALL_MIDIS"] = Butt = new Button("A2A", System_White, Settings::ApplyToAll, 80 - WindowHeapSize, 95 - WindowHeapSize, 15, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F003F, 0xFF7F00FF, System_White, "The same as A2A in MIDI's props.");
	Butt->Tip->SafeChangePosition_Argumented(_Align::right, 87.5 - WindowHeapSize, Butt->Tip->CYpos);

	(*T)["INPLACE_MERGE"] = new CheckBox(97.5 - WindowHeapSize, 95 - WindowHeapSize, 10, 0x007FFFFF, 0xFF3F007F, 0x3FFF007F, 1, 0, System_White, _Align::right, "Enable/disable inplace merge");

	(*T)["COLLAPSE_MIDI"] = new CheckBox(72.5 - WindowHeapSize, 75 - WindowHeapSize, 10, 0x007FFFFF, 0xFF7F00AF, 0x7FFF00AF, 1, 0, System_White, _Align::right, "Collapse tracks of a MIDI into one");
	(*T)["APPLY_OFFSET_AFTER"] = new CheckBox(57.5 - WindowHeapSize, 75 - WindowHeapSize, 10, 0x007FFFFF, 0xFF7F00AF, 0x7FFF00AF, 1, 0, System_White, _Align::right, "Apply offset after PPQ change");

	(*T)["AS_THREADS_COUNT"] = new InputField(std::to_string(_Data.DetectedThreads), 92.5 - WindowHeapSize, 75 - WindowHeapSize, 10, 20, System_White, NULL, 0x007FFFFF, System_White, "Threads count", 2, _Align::center, _Align::right, InputField::Type::NaturalNumbers);

	(*T)["AUTOUPDATECHECK"] = new CheckBox(-97.5 + WindowHeapSize, 35 - WindowHeapSize, 10, 0x007FFFFF, 0xFF3F007F, 0x3FFF007F, 1, check_autoupdates, System_White, _Align::left, "Check for updates automatically"); 


	(*WH)["APP_SETTINGS"] = T;

	T = new MoveableWindow("SMRP Container", System_White, -300, 300, 600, 600, 0x000000CF, 0xFFFFFF7F);

	(*T)["TIMER"] = new InputField("0 s", 0, 195, 10, 50, System_White, NULL, 0, System_White, "Timer", 12, _Align::center, _Align::center, InputField::Type::Text);

	(*WH)["SMRP_CONTAINER"] = T;

	T = new MoveableWindow("MIDI Info Collector", System_White, -150, 200, 300, 400, 0x3F3F3FCF, 0x7F7F7F7F);
	(*T)["FLL"] = new TextBox("--File log line--", System_White, 0, -WindowHeapSize + 185, 15, 285, 10, 0, 0, 0, _Align::left);
	(*T)["FEL"] = new TextBox("--File error line--", System_Red, 0, -WindowHeapSize + 175, 15, 285, 10, 0, 0, 0, _Align::left);
	(*T)["TEMPO_GRAPH"] = new Graphing<SingleMIDIInfoCollector::tempo_graph>(
		0, -WindowHeapSize + 145, 285, 50, (1. / 20000.), true, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F00FF, 0xFFFFFFFF, 0x7F7F7F7F, nullptr, System_White, false
		);
	(*T)["POLY_GRAPH"] = new Graphing<SingleMIDIInfoCollector::polyphony_graph>(
		0, -WindowHeapSize + 95, 285, 50, (1. / 20000.), true, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F00FF, 0xFFFFFFFF, 0x7F7F7F7F, nullptr, System_White, false
		);
	(*T)["PG_SWITCH"] = new Button("Enable graph B", System_White, PropsAndSets::SMIC::EnablePG, 37.5, 60 - WindowHeapSize, 70, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, System_White, "Polyphony graph");
	(*T)["TG_SWITCH"] = new Button("Enable graph A", System_White, PropsAndSets::SMIC::EnableTG, -37.5, 60 - WindowHeapSize, 70, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, System_White, "Tempo graph");
	(*T)["ALL_EXP"] = new Button("Export all", System_White, PropsAndSets::SMIC::ExportAll, 110, 60 - WindowHeapSize, 65, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, nullptr);
	(*T)["TG_EXP"] = new Button("Export Tempo", System_White, PropsAndSets::SMIC::ExportTG, -110, 60 - WindowHeapSize, 65, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, nullptr);
	(*T)["TG_RESET"] = new Button("Reset graph A", System_White, PropsAndSets::SMIC::ResetTG, 45, 40 - WindowHeapSize, 65, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, nullptr);
	(*T)["PG_RESET"] = new Button("Reset graph B", System_White, PropsAndSets::SMIC::ResetPG, 45, 20 - WindowHeapSize, 65, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, nullptr);
	(*T)["PERSONALUSE"] = Butt = new Button(".csv", System_White, PropsAndSets::SMIC::SwitchPersonalUse, 105, 40 - WindowHeapSize, 45, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, System_White, "Switches output format for `export all`");
	Butt->Tip->SafeChangePosition_Argumented(_Align::right, Butt->Xpos + Butt->Width * 0.5, Butt->Tip->CYpos);
	(*T)["TOTAL_INFO"] = new TextBox("----", System_White, 0, -150, 35, 285, 10, 0, 0, 0, _Align::left);
	(*T)["INT_MIN"] = new InputField("0", -132.5, 40 - WindowHeapSize, 10, 20, System_White, NULL, 0x007FFFFF, System_White, "Minutes", 3, _Align::center, _Align::left, InputField::Type::NaturalNumbers);
	(*T)["INT_SEC"] = new InputField("0", -107.5, 40 - WindowHeapSize, 10, 20, System_White, NULL, 0x007FFFFF, System_White, "Seconds", 2, _Align::center, _Align::left, InputField::Type::NaturalNumbers);
	(*T)["INT_MSC"] = new InputField("000", -80, 40 - WindowHeapSize, 10, 25, System_White, NULL, 0x007FFFFF, System_White, "Milliseconds", 3, _Align::center, _Align::left, InputField::Type::NaturalNumbers);
	(*T)["INTEGRATE_TICKS"] = new Button("Integrate ticks", System_White, PropsAndSets::SMIC::IntegrateTime, -27.5, 40 - WindowHeapSize, 70, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, System_White, "Result is the closest tick to that time.");
	(*T)["INT_TIC"] = new InputField("0", -105, 20 - WindowHeapSize, 10, 75, System_White, NULL, 0x007FFFFF, System_White, "Ticks", 17, _Align::center, _Align::left, InputField::Type::NaturalNumbers);
	(*T)["INTEGRATE_TIME"] = new Button("Integrate time", System_White, PropsAndSets::SMIC::DiffirentiateTicks, -27.5, 20 - WindowHeapSize, 70, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, System_White, "Result is the time of that tick.");
	(*T)["DELIM"] = new InputField(";", 137.5, 40 - WindowHeapSize, 10, 7.5, System_White, &(PropsAndSets::CSV_DELIM), 0x007FFFFF, System_White, "Delimiter", 1, _Align::center, _Align::right, InputField::Type::Text);
	(*T)["ANSWER"] = new TextBox("----", System_White, -66.25, -30, 25, 152.5, 10, 0, 0, 0, _Align::center, TextBox::VerticalOverflow::recalibrate);

	(*WH)["SMIC"] = T;

	auto InnerWindow = new MoveableResizeableWindow("Inner Test", System_White, -100, 100, 200, 200, 0x0000007F, 0x7F7F7FFF, 0x0000007F);
	T = new MoveableResizeableWindow("Test", System_White, -150, 150, 300, 300, 0x0000007F, 0x7F7F7FFF, 0x0000007F, [InnerWindow](float dH, float dW, float NewHeight, float NewWidth) {
		InnerWindow->SafeResize(InnerWindow->Height + dH, InnerWindow->Width + dW);
	});

	(*T)["WINDOW"] = InnerWindow;
	(*T)["TEXTAREA"] = new EditBox("", System_White, 0, 0, 200, 200, 10, 0, 0xFFFFFFFF, 2);
	(*InnerWindow)["BUTT"] = new Button("o3o", System_White, nullptr, -50, -55, 75, 12, 1,
		0x000000AF, 0xFFFFFFFF, 0x000000AF, 0xFFFFFFFF, 0xF7F7F7FF, NULL, " ");
	InnerWindow->AssignPinnedActivities({ "BUTT" }, MoveableResizeableWindow::PinSide::bottom);
	InnerWindow->AssignPinnedActivities({ "BUTT" }, MoveableResizeableWindow::PinSide::left);

	(*WH)["COMPILEW"] = T;

	WH->EnableWindow("MAIN");
	//WH->EnableWindow("COMPILEW");
	//WH->EnableWindow("SMIC");
	//WH->EnableWindow("OR");
	//WH->EnableWindow("SMRP_CONTAINER");
	//WH->EnableWindow("APP_SETTINGS");
	//WH->EnableWindow("VM");
	//WH->EnableWindow("CAT");
	//WH->EnableWindow("SMPAS");//Debug line
	//WH->EnableWindow("PROMPT");////DEBUUUUG
#ifdef WINDOWS
	DragAcceptFiles(hWnd, TRUE);
	OleInitialize(NULL);
	std::cout << "Registering Drag&Drop: " << (RegisterDragDrop(hWnd, &DNDH_Global)) << std::endl;
#endif

	//SAFC_VersionCheck();
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
	if (FIRSTBOOT)
	{
		FIRSTBOOT = 0;

		Init();
		if (APRIL_FOOL)
		{
			WH->ThrowAlert("Today is a special day! ( -w-)\nToday you'll have new background\n(-w- )", "1st of April!", SpecialSigns::DrawWait, 1, 0xFF00FFFF, 20);
			(*WH)["ALERT"]->RGBABackground = 0xF;
			_WH_t("ALERT", "AlertText", TextBox*)->SafeTextColorChange(0xFFFFFFFF);
		}
		if (YearsOld >= 0)
		{
			WH->ThrowAlert("Interesting fact: today is exactly " + std::to_string(YearsOld) + " years since first SAFC release.\n(o w o  )", "SAFC birthday", SpecialSigns::DrawWait, 1, 0xFF7F3FFF, 50);
			(*WH)["ALERT"]->RGBABackground = 0xF;
			_WH_t("ALERT", "AlertText", TextBox*)->SafeTextColorChange(0xFFFFFFFF);
		}
		ANIMATION_IS_ACTIVE = !ANIMATION_IS_ACTIVE;
		onTimer(0);
	}

	if (YearsOld >= 0 || Settings::ShaderMode == 100)
	{
		glBegin(GL_QUADS);
		glColor4f(1, 1, 1, (DRAG_OVER) ? 0.25f : 1);
		glVertex2f(0 - internal_range * (WindX / window_base_width), 0 - internal_range * (WindY / window_base_height));
		glColor4f(1, 1, 1, (DRAG_OVER) ? 0.25f : 1);
		glVertex2f(0 - internal_range * (WindX / window_base_width), internal_range * (WindY / window_base_height));
		glColor4f(1, 0.9f, 0.8f, (DRAG_OVER) ? 0.25f : 1);
		glVertex2f(internal_range * (WindX / window_base_width), internal_range * (WindY / window_base_height));
		glColor4f(0.8f, 0.9f, 1, (DRAG_OVER) ? 0.25f : 1);
		glVertex2f(internal_range * (WindX / window_base_width), 0 - internal_range * (WindY / window_base_height));
		glEnd();
	}
	else if (APRIL_FOOL || Settings::ShaderMode == 69)
	{
		glBegin(GL_QUADS);
		glColor4f(1, 0, 1, (DRAG_OVER) ? 0.25f : 1);
		glVertex2f(0 - internal_range * (WindX / window_base_width), 0 - internal_range * (WindY / window_base_height));
		glColor4f(0, 1, 0, (DRAG_OVER) ? 0.25f : 1);
		glVertex2f(0 - internal_range * (WindX / window_base_width), internal_range * (WindY / window_base_height));
		glColor4f(1, 0.5f, 0, (DRAG_OVER) ? 0.25f : 1);
		glVertex2f(internal_range * (WindX / window_base_width), internal_range * (WindY / window_base_height));
		glColor4f(0, 0.5f, 1, (DRAG_OVER) ? 0.25f : 1);
		glVertex2f(internal_range * (WindX / window_base_width), 0 - internal_range * (WindY / window_base_height));
		glEnd();
	}
	else if (Settings::ShaderMode < 4)
	{
		glBegin(GL_QUADS);
		glColor4f(1, 0.5f, 0, (DRAG_OVER) ? 0.25f : 1);
		glVertex2f(0 - internal_range * (WindX / window_base_width), 0 - internal_range * (WindY / window_base_height));
		glVertex2f(0 - internal_range * (WindX / window_base_width), internal_range * (WindY / window_base_height));
		glColor4f(0, 0.5f, 1, (DRAG_OVER) ? 0.25f : 1);
		glVertex2f(internal_range * (WindX / window_base_width), internal_range * (WindY / window_base_height));
		glVertex2f(internal_range * (WindX / window_base_width), 0 - internal_range * (WindY / window_base_height));
		glEnd();
	}
	else
	{
		glBegin(GL_QUADS);
		glColor4f(0.25f, 0.25f, 0.25f, (DRAG_OVER) ? 0.25f : 1);
		glVertex2f(0 - internal_range * (WindX / window_base_width), 0 - internal_range * (WindY / window_base_height));
		glVertex2f(0 - internal_range * (WindX / window_base_width), internal_range * (WindY / window_base_height));
		glVertex2f(internal_range * (WindX / window_base_width), internal_range * (WindY / window_base_height));
		glVertex2f(internal_range * (WindX / window_base_width), 0 - internal_range * (WindY / window_base_height));
		glEnd();
	}
	//ROT_ANGLE += 0.02;

	glRotatef(dumb_rotation_angle, 0, 0, 1);
	if (WH)
		WH->Draw();
	if (DRAG_OVER)SpecialSigns::DrawFileSign(0, 0, 50, 0xFFFFFFFF, 0);
	glRotatef(-dumb_rotation_angle, 0, 0, 1);

	glutSwapBuffers();
}

void mInit(
) {
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluOrtho2D((0 - internal_range) * (WindX / window_base_width), internal_range * (WindX / window_base_width), (0 - internal_range) * (WindY / window_base_height), internal_range * (WindY / window_base_height));
}

void onTimer(int v)
{
	glutTimerFunc(33, onTimer, 0);
	if (ANIMATION_IS_ACTIVE)
	{
		mDisplay();
		++TimerV;
	}
}

void OnResize(int x, int y)
{
	WindX = x;
	WindY = y;
	mInit();
	glViewport(0, 0, x, y);
	if (WH)
	{
		auto SMRP = (*WH)["SMRP_CONTAINER"];
		SMRP->SafeChangePosition_Argumented(0, 0, 0);
		SMRP->_NotSafeResize_Centered(internal_range * 3 * (WindY / window_base_height) + 2 * WindowHeapSize, internal_range * 3 * (WindX / window_base_width));
	}
}
void inline rotate(float& x, float& y)
{
	float t = x * cos(rotation_angle()) + y * sin(rotation_angle());
	y = 0.f - x * sin(rotation_angle()) + y * cos(rotation_angle());
	x = t;
}

void inline absoluteToActualCoords(int ix, int iy, float& x, float& y)
{
	float wx = WindX, wy = WindY;
	x = ((float)(ix - wx * 0.5f)) / (0.5f * (wx / (internal_range * (WindX / window_base_width))));
	y = ((float)(0 - iy + wy * 0.5f)) / (0.5f * (wy / (internal_range * (WindY / window_base_height))));
	rotate(x, y);
}

void mMotion(int ix, int iy)
{
	float fx, fy;
	absoluteToActualCoords(ix, iy, fx, fy);
	mouse_x_position = fx;
	mouse_y_position = fy;
	if (WH)
		WH->MouseHandler(fx, fy, 0, 0);
}

void mKey(std::uint8_t k, int x, int y)
{
	if (WH)
		WH->KeyboardHandler(k);

	if (k == 27)
		exit(0);
}

void mClick(int butt, int state, int x, int y)
{
	float fx, fy;
	char Button, State = state;
	absoluteToActualCoords(x, y, fx, fy);
	Button = butt - 1;

	if (state == GLUT_DOWN)
		State = -1;
	else if (state == GLUT_UP)
		State = 1;

	if (WH)
		WH->MouseHandler(fx, fy, Button, State);
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
		switch (Key) {
		case GLUT_KEY_DOWN:		if (WH)WH->KeyboardHandler(1);
			break;
		case GLUT_KEY_UP:		if (WH)WH->KeyboardHandler(2);
			break;
		case GLUT_KEY_LEFT:		if (WH)WH->KeyboardHandler(3);
			break;
		case GLUT_KEY_RIGHT:	if (WH)WH->KeyboardHandler(4);
			break;
		case GLUT_KEY_F5:		if (WH) Init();
			break;
		}
	}
	if (modif == GLUT_ACTIVE_ALT && Key == GLUT_KEY_DOWN)
	{
		internal_range *= 1.1f;
		OnResize(WindX, WindY);
	}
	else if (modif == GLUT_ACTIVE_ALT && Key == GLUT_KEY_UP)
	{
		internal_range /= 1.1f;
		OnResize(WindX, WindY);
	}
}

void mExit(int a)
{
#ifdef WINDOWS
	Settings::RegestryAccess.Close();
#endif
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
#ifdef WINDOWS
		_wremove(L"_s");
		_wremove(L"_f");
		_wremove(L"_g");
#endif

#ifdef WINDOWS
#ifdef _DEBUG 
		ShowWindow(GetConsoleWindow(), SW_SHOW);
#else // _DEBUG 
		ShowWindow(GetConsoleWindow(), SW_HIDE);
#endif
#endif

#ifdef WINDOWS
		SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
#endif
		//srand(1);
		//srand(clock());
		InitASCIIMap();
		//cout << to_string((std::uint16_t)0) << endl;

		srand(TimeCheckAndReturnTimeseed());
#ifdef WINDOWS
		__glutInitWithExit(&argc, argv, mExit);
#else
		glutInit(&argc, argv);
#endif
		//cout << argv[0] << endl;
		glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA | GLUT_ALPHA | GLUT_MULTISAMPLE);
		glutInitWindowSize(window_base_width, window_base_height);
		//glutInitWindowPosition(50, 0);
		glutCreateWindow(window_title);

#ifdef WINDOWS
		hWnd = FindWindowA(NULL, window_title);
		hDc = GetDC(hWnd);
#endif

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


/*

{
	"global_ppq_override": 3860,					// optional; signed long long int;
	"global_tempo_override": 485,					// optional; double;
	"global_offset_override": 4558,					// optional; signed long long int;
	"save_to": "C:\\MIDIs\\merge.mid",				// optional; string (utf8)
	"files":
	[
		{
			"filename": "D:\\Download\\Downloads\\Paprika's Aua Ah Community Merge (FULL).mid", // string (utf8)
			"ppq_override": 960, 					// optional; unisnged short;
			"tempo_override": 3.94899, 				// optional; double;
			"offset": 0, 							// optional; signed long long int;
			"selection_start": 50, 					// optional; signed long long int;
			"selection_length": 50, 				// optional; signed long long int;
			"ignore_notes": false, 					// optional; bool;
			"ignore_pitches": false, 				// optional; bool;
			"ignore_tempos": false, 				// optional; bool;
			"ignore_other": false, 					// optional; bool;
			"piano_only": true, 					// optional; bool;
			"remove_remnants": true, 				// optional; bool;
			"remove_empty_tracks": true, 			// optional; bool;
			"channel_split": false, 				// optional; bool;
			"ignore_meta_rsb": false, 				// optional; bool;
			"rsb_compression": false, 				// optional; bool;
			"inplace_mergable": false, 				// optional; bool;
		}
	]
}

*/

struct SafcCliRuntime:
	public SafcRuntime
{
	virtual void operator()(int argc, char** argv) override
	{
#ifdef WINDOWS
		ShowWindow(GetConsoleWindow(), SW_SHOW);
#endif
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
			throw std::runtime_error("No config provided");

		auto config_path = std::filesystem::u8path(argv[1]);
		std::ifstream fin(config_path);
		std::stringstream ss;
		ss << fin.rdbuf();
		auto config_content = ss.str();
		auto config_object = JSON::Parse(config_content.c_str());
		auto config = config_object->AsObject();

		auto global_ppq_override = config.find(L"global_ppq_override");
		auto global_tempo_override = config.find(L"global_tempo_override");
		auto global_offset = config.find(L"global_offset");
		auto files = config.find(L"files");

		if (files == config.end())
			return;

		if (global_ppq_override != config.end())
			_Data.SetGlobalPPQN(global_ppq_override->second->AsNumber());

		if (global_tempo_override != config.end())
			_Data.SetGlobalTempo(global_tempo_override->second->AsNumber());

		if (global_offset != config.end())
			_Data.SetGlobalOffset(global_offset->second->AsNumber());

		auto& filesArray = files->second->AsArray();
		std::vector<std_unicode_string> filenames;

		for (auto& singleEntry : filesArray)
		{
			auto& object = singleEntry->AsObject();
			auto& filename = object.at(L"filename")->AsString();
			filenames.push_back(std_unicode_string(filename.begin(), filename.end()));
		}

		AddFiles(filenames);

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
				_Data.Files[index].AllowLegacyRunningStatusMetaIgnorance = ignore_meta_rsb->second->AsBool();

			auto inplace_mergable = object.find(L"inplace_mergable");
			if (inplace_mergable != object.end())
				_Data.Files[index].InplaceMergeEnabled = inplace_mergable->second->AsBool();

			auto allow_sysex = object.find(L"allow_sysex");
			if (allow_sysex != object.end())
				_Data.Files[index].AllowSysex = allow_sysex->second->AsBool();

			index++;
		}

		auto save_to = config.find(L"save_to");
		if (save_to != config.end())
		{
#ifdef WINDOWS
			_Data.SavePath = (save_to->second->AsString());
			size_t Pos = _Data.SavePath.rfind(L".mid");
			if (Pos >= _Data.SavePath.size() || Pos <= _Data.SaveDirectory.size() - 4)
				_Data.SavePath += L".mid";
#else
			auto wideSaveDirectory = save_to->second->AsString();
			_Data.SavePath = std::string(wideSaveDirectory.begin(), wideSaveDirectory.end());
			size_t Pos = _Data.SavePath.rfind(".mid");
			if (Pos >= _Data.SavePath.size() || Pos <= _Data.SavePath.size() - 4)
				_Data.SavePath += ".mid";
#endif
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
#ifdef WINDOWS
	__versionTuple = ___GetVersion();
#endif

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
