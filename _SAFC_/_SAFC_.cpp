//#include "httplib.h"
#include <algorithm>
#include <cstdlib>
#include <wchar.h>
#include <io.h>
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

#include <WinSock2.h>
#include <WinInet.h>
//#define WIN32_LEAN_AND_MEAN

#pragma comment (lib, "Version.lib")//Urlmon.lib
#pragma comment (lib, "Urlmon.lib")//Urlmon.lib
#pragma comment (lib, "wininet.lib")//Urlmon.lib
#pragma comment (lib, "dwmapi.lib")

#include "allocator.h"
#include "WinReg.h"

#include <stack>
#include "bbb_ffio.h"

#include "JSON/JSON.h"
#include "JSON/JSON.cpp"
#include "JSON/JSONValue.h"
#include "JSON/JSONValue.cpp"

#include "btree/btree_map.h"

#include "SAFGUIF/SAFGUIF.h"
#include "SAFC_InnerModules/SAFC_IM.h"
#include "SAFCGUIF_Local/SAFGUIF_L.h"

using namespace std;

const char* WINDOWTITLE = "SAFC\0";
wstring RegPath = L"Software\\SAFC\\";

std::tuple<WORD, WORD, WORD, WORD> ___GetCurFileVersion() {
	constexpr int
		filename_len = 500;
	wchar_t		szExeFileName[filename_len];
	DWORD		verHandle = 0;
	UINT		size = 0;
	LPBYTE		lpBuffer = NULL;
	GetModuleFileNameW(NULL, szExeFileName, filename_len);
	DWORD		verSize = GetFileVersionInfoSizeW(szExeFileName, &verHandle);
	if (verSize != 0) {
		LPSTR verData = new char[verSize];
		if (GetFileVersionInfoW(szExeFileName, verHandle, verSize, verData)) {
			if (VerQueryValueW(verData, L"\\", (VOID FAR * FAR*) & lpBuffer, &size)) {
				if (size) {
					VS_FIXEDFILEINFO* verInfo = (VS_FIXEDFILEINFO*)lpBuffer;
					if (verInfo->dwSignature == 0xfeef04bd) {
						delete[] verData;

						// Doesn't matter if you are on 32 bit or 64 bit,
						// DWORD is always 32 bits, so first two revision numbers
						// come from dwFileVersionMS, last two come from dwFileVersionLS
						return {
							HIWORD(verInfo->dwProductVersionMS),
							LOWORD(verInfo->dwProductVersionMS),
							HIWORD(verInfo->dwProductVersionLS),
							LOWORD(verInfo->dwProductVersionLS) };
					}
				}
			}
			delete[] verData;
		}
	}
	return { 0,0,0,0 };
}
std::wstring ExtractDirectory(const std::wstring& path) {
	constexpr wchar_t delim = (L"\\")[0];
	return path.substr(0, path.find_last_of(delim) + 1);
}
bool SAFC_Update(const wstring& latest_release) {
#ifndef __X64
	constexpr wchar_t* archive_name = (wchar_t* const)L"SAFC32.7z";;
#else
	constexpr wchar_t* const archive_name = (wchar_t* const)L"SAFC64.7z";
#endif
	wchar_t current_file_path[MAX_PATH];
	bool flag = false;
	//GetCurrentDirectoryW(MAX_PATH, current_file_path);
	GetModuleFileNameW(NULL, current_file_path, MAX_PATH);
	wstring executablepath = current_file_path;
	wstring filename = ExtractDirectory(current_file_path);
	wstring pathway = filename;
	filename += L"update.7z";
	//wsprintfW(current_file_path, L"%S%S", filename.c_str(), L"update.7z\0");
	wstring link = L"https://github.com/DixelU/SAFC/releases/download/" + latest_release + L"/" + archive_name;
	HRESULT co_res = URLDownloadToFileW(NULL, link.c_str(), filename.c_str(), 0, NULL);
	const vector<pair<wstring, wstring>> unpack_lines = {
		{L"C:\\Program Files\\7-Zip\\7z.exe", (L"x -y \"" + filename + L"\"")},
		{L"C:\\Program Files (x86)\\7-Zip\\7z.exe", (L"x -y \"" + filename + L"\"")},
		{L"C:\\Program Files\\WinRAR\\WinRAR.exe", (L"x -y \"" + filename + L"\"")},
		{L"C:\\Program Files (x86)\\WinRAR\\WinRAR.exe", (L"x -y \"" + filename + L"\"")}
	};
	if (co_res == S_OK) { 
		errno = 0;
		_wrename((pathway + L"SAFC.exe").c_str(), (pathway + L"_s").c_str());
		cout << strerror(errno) << endl;
		_wrename((pathway + L"freeglut.dll").c_str(), (pathway + L"_f").c_str());
		cout << strerror(errno) << endl;
		_wrename((pathway + L"glew32.dll").c_str(), (pathway + L"_g").c_str()); 
		cout << strerror(errno) << endl; 
		//wcout << pathway << endl;
		if (!errno) {
			wstring dir = pathway.substr(0, pathway.length() - 1);
			for (auto& line : unpack_lines) {
				wstring t = (L"\"" + line.first + L"\" " + line.second);

				if (_waccess(line.first.c_str(), 0)) {
					wcout << L"Unable to find: " << line.first << endl;
					continue;
				}

				SHELLEXECUTEINFO ShExecInfo = { 0 };
				ShExecInfo.cbSize = sizeof(SHELLEXECUTEINFO);
				ShExecInfo.fMask = SEE_MASK_NOCLOSEPROCESS;
				ShExecInfo.hwnd = NULL;
				ShExecInfo.lpVerb = NULL;
				ShExecInfo.lpFile = line.first.data();
				ShExecInfo.lpParameters = line.second.data();
				ShExecInfo.lpDirectory = dir.data();
				ShExecInfo.nShow = SW_SHOW;
				ShExecInfo.hInstApp = NULL;
				ShellExecuteExW(&ShExecInfo);
				WaitForSingleObject(ShExecInfo.hProcess, INFINITE);
				CloseHandle(ShExecInfo.hProcess);

				if (!_waccess((pathway + L"SAFC.exe").c_str(), 0)) { 
					flag = true;
					break;
				}
				wcout << L"Failed: " << t << endl;
			}
			if (!flag) {
				ThrowAlert_Error("Extraction:\nAutoupdate requres latest 7-Zip or WinRAR installed to default directory.\n");
				_wrename((pathway + L"_s").c_str(), (pathway + L"SAFC.exe").c_str());
				_wrename((pathway + L"_f").c_str(), (pathway + L"freeglut.dll").c_str());
				_wrename((pathway + L"_g").c_str(), (pathway + L"glew32.dll").c_str());
			}
		}
		else {
			ThrowAlert_Error(string("Autoudate error:\n") + strerror(errno));
			_wrename((pathway + L"_s").c_str(), (pathway + L"SAFC.exe").c_str());
			_wrename((pathway + L"_f").c_str(), (pathway + L"freeglut.dll").c_str());
			_wrename((pathway + L"_g").c_str(), (pathway + L"glew32.dll").c_str());
		}
		_wremove(filename.c_str());
	}
	else
		ThrowAlert_Error("Autoupdate error: #" + to_string(co_res));
	return flag;
}
void SAFC_VersionCheck() {
	thread version_checker([]() {
		bool flag = false;
		_wremove(L"_s");
		_wremove(L"_f");
		_wremove(L"_g");
		constexpr wchar_t* SAFC_tags_link = (wchar_t* const)L"https://api.github.com/repos/DixelU/SAFC/tags";
		wchar_t current_file_path[MAX_PATH];
		GetModuleFileNameW(NULL, current_file_path, MAX_PATH);
		wstring executablepath = current_file_path;
		wstring filename = ExtractDirectory(current_file_path);
		wstring pathway = filename;
		filename += L"tags.json";
		HRESULT res = URLDownloadToFileW(NULL, SAFC_tags_link, filename.c_str(), 0, NULL);
		if (res == S_OK) {
			auto [maj, min, ver, build] = ___GetCurFileVersion();
			ifstream input(filename);
			string temp_buffer;
			std::getline(input, temp_buffer);
			input.close();
			auto JSON_Value = JSON::Parse(temp_buffer.c_str());
			if (JSON_Value->IsArray()) {
				auto FirstElement = JSON_Value->AsArray()[0];
				if (FirstElement->IsObject()) {
					auto Name = FirstElement->AsObject().find(L"name");
					if (FirstElement->AsObject().end() != Name &&
						Name->second->IsString()) {
						auto git_latest_version = Name->second->AsString();
						std::wcout << L"Git latest version: " << git_latest_version << endl;
						bool was_digit = false;
						WORD version_partied[4] = {0,0,0,0};
						int cur_index = -1;
						if (git_latest_version.size() <= 100) {
							for (auto& ch : git_latest_version) {
								if (isdigit(ch)) {
									if (!was_digit) {
										was_digit = true;
										if (cur_index < 4)
											cur_index++;
										else
											break;
									}
									version_partied[cur_index] *= 10;
									version_partied[cur_index] += (ch - '0') % 10;
								}
								else {
									was_digit = false;
								}
							}
							if (maj < version_partied[0] ||
								maj == version_partied[0] && min < version_partied[1] ||
								maj == version_partied[0] && min == version_partied[1] && ver < version_partied[2] ||
								maj == version_partied[0] && min == version_partied[1] && ver == version_partied[2] && build < version_partied[3]) {
								ThrowAlert_Warning("Update found! The app might restart soon...\nUpdate: " + string(git_latest_version.begin(), git_latest_version.end()));
								flag = SAFC_Update(git_latest_version);
								if (flag)
									ThrowAlert_Warning("SAFC will restart in 3 seconds..."); 
							}
						}
					}
				}
			}
		}
		else 
			ThrowAlert_Warning("Most likely your internet connection is unstable\nSAFC cannot check for updates");

		_wremove(filename.c_str());
		if (flag) {
			Sleep(3000);
			ShellExecuteW(NULL, L"open", executablepath.c_str(), NULL, NULL, SW_SHOWNORMAL);
			//_wsystem((L"start \"" + executablepath + L"\"").c_str());
			exit(0);
		}
	});
	version_checker.detach();
}
size_t GetAvailableMemory(){
	size_t ret = 0;
	DWORD v = GetVersion();
	DWORD major = (DWORD)(LOBYTE(LOWORD(v)));
	DWORD minor = (DWORD)(HIBYTE(LOWORD(v)));
	DWORD build;
	if (v < 0x80000000) build = (DWORD)(HIWORD(v));
	else build = 0;

	// because compiler static links the function...
	BOOL(__stdcall*GMSEx)(LPMEMORYSTATUSEX) = 0;

	HINSTANCE hIL = LoadLibrary(L"kernel32.dll");
	GMSEx = (BOOL(__stdcall*)(LPMEMORYSTATUSEX))GetProcAddress(hIL, "GlobalMemoryStatusEx");
	if (GMSEx) {
		MEMORYSTATUSEX m;
		m.dwLength = sizeof(m);
		if (GMSEx(&m))
		{
			ret = (int)(m.ullAvailPhys >> 20);
		}
	}
	else {
		MEMORYSTATUS m;
		m.dwLength = sizeof(m);
		GlobalMemoryStatus(&m);
		ret = (int)(m.dwAvailPhys >> 20);
	}
	return ret;
}
BIT RestoreIsFontedVar() {
	bool RK_OP = false;
	WinReg::RegKey RK;
	try {
		RK.Open(HKEY_CURRENT_USER, RegPath);
		RK_OP = true;
	}
	catch (...) {
		cout << "RK opening failed\n";
	}
	if (RK_OP) {
		try {
			is_fonted = RK.GetDwordValue(L"FONTS_ENABLED");
		}
		catch (...) { cout << "Exception thrown while restoring FONTS_ENABLED from registry\n"; }
	}
	if (RK_OP)
		RK.Close();
	return false;
}
void SetIsFontedVar(BIT VAL) {
	bool RK_OP = false;
	WinReg::RegKey RK;
	try {
		RK.Open(HKEY_CURRENT_USER, RegPath);
		RK_OP = true;
	}
	catch (...) {
		cout << "RK opening failed\n";
	}
	if (RK_OP) {
		try {
			RK.SetDwordValue(L"FONTS_ENABLED", VAL);
		}
		catch (...) { cout << "Exception thrown while saving FONTS_ENABLED from registry\n"; }
	}
	if (RK_OP)
		RK.Close();
}
BIT _______unused = RestoreIsFontedVar();

//////////////////////////////
////TRUE USAGE STARTS HERE////
//////////////////////////////

ButtonSettings 
					*BS_List_Black_Small = new ButtonSettings(System_White, 0, 0, 100, 10, 1, 0, 0, 0xFFEFDFFF, 0x00003F7F, 0x7F7F7FFF);

DWORD DefaultBoolSettings = _BoolSettings::remove_remnants | /*_BoolSettings::remove_empty_tracks*/ 0 | _BoolSettings::all_instruments_to_piano;

struct FileSettings {////per file settings
	wstring Filename;
	wstring PostprocessedFile_Name, WFileNamePostfix;;
	string AppearanceFilename,AppearancePath,FileNamePostfix;
	WORD NewPPQN,OldPPQN,OldTrackNumber,MergeMultiplier;
	INT16 GroupID;
	INT32 NewTempo;
	UINT64 FileSize;
	INT64 Start, Length;
	BIT IsMIDI, InplaceMergeEnabled;
	CutAndTransposeKeys *KeyMap;
	PLC<BYTE, BYTE> *VolumeMap;
	PLC<WORD, WORD> *PitchBendMap;
	DWORD OffsetTicks,BoolSettings;///use ump to see how far you wanna offset all that.
	~FileSettings() {
		if (KeyMap)delete KeyMap;
		if (VolumeMap)delete VolumeMap;
		if (PitchBendMap)delete PitchBendMap;
	}
	FileSettings(wstring Filename) {
		this->Filename = Filename;
		AppearanceFilename = AppearancePath = "";
		auto pos = Filename.rfind('\\');
		for (; pos < Filename.size(); pos++)
			AppearanceFilename.push_back(Filename[pos] & 0xFF);
		for (int i = 0; i < Filename.size(); i++) 
			AppearancePath.push_back((char)(Filename[i] & 0xFF));
		//cout << AppearancePath << " ::\n";
		FastMIDIInfoCollector FMIC(Filename);
		this->IsMIDI = FMIC.IsAcssessable && FMIC.IsMIDI;
		OldPPQN = FMIC.PPQN;
		OldTrackNumber = FMIC.ExpectedTrackNumber;
		OffsetTicks = InplaceMergeEnabled = 0;
		VolumeMap = NULL;
		PitchBendMap = NULL;
		KeyMap = NULL;
		FileSize = FMIC.FileSize;
		GroupID = NewTempo = 0;
		Start = 0;
		Length = -1;
		BoolSettings = DefaultBoolSettings;
		FileNamePostfix = "_.mid";//_FILENAMEWITHEXTENSIONSTRING_.mid
		WFileNamePostfix = L"_.mid";
	}
	inline void SwitchBoolSetting(DWORD SMP_BoolSetting) {
		BoolSettings ^= SMP_BoolSetting;
	}
	inline void SetBoolSetting(DWORD SMP_BoolSetting, BIT NewState) {
		if (BoolSettings&SMP_BoolSetting && NewState)return;
		if (!(BoolSettings&SMP_BoolSetting) && !NewState)return;
		SwitchBoolSetting(SMP_BoolSetting);
	}
	SingleMIDIReProcessor* BuildSMRP() {
		SingleMIDIReProcessor *SMRP = new SingleMIDIReProcessor(this->Filename, this->BoolSettings, this->NewTempo, this->OffsetTicks, this->NewPPQN, this->GroupID, this->VolumeMap, this->PitchBendMap, this->KeyMap, this->WFileNamePostfix, this->InplaceMergeEnabled, this->FileSize, this->Start, this->Length);
		SMRP->AppearanceFilename = this->AppearanceFilename;
		return SMRP;
	}
};
struct _SFD_RSP {
	INT32 ID;
	INT64 FileSize;
	_SFD_RSP(INT32 ID, INT64 FileSize) {
		this->ID = ID;
		this->FileSize = FileSize;
	}
	inline BIT operator<(_SFD_RSP a) {
		return FileSize < a.FileSize;
	}
};
struct SAFCData {////overall settings and storing perfile settings....
	vector<FileSettings> Files;
	wstring SaveDirectory;
	WORD GlobalPPQN;
	DWORD GlobalOffset;
	DWORD GlobalNewTempo;
	BIT IncrementalPPQN;
	BIT InplaceMergeFlag;
	WORD DetectedThreads;
	SAFCData() {
		GlobalPPQN = GlobalOffset = GlobalNewTempo = 0;
		IncrementalPPQN = 1;
		DetectedThreads = 1;
		InplaceMergeFlag = 0;
		SaveDirectory = L"";
	}
	void ResolveSubdivisionProblem_GroupIDAssign(INT32 ThreadsCount=0) {
		if (!ThreadsCount)ThreadsCount = DetectedThreads;
		if (Files.empty()) {
			SaveDirectory = L"";
			return;
		}
		else if(Files.size())
			SaveDirectory = Files[0].Filename + L".AfterSAFC.mid";

		if (Files.size() == 1) {
			Files.front().GroupID = 0;
			return;
		}
		vector<_SFD_RSP> Sizes;
		vector<INT64> SumSize;
		INT64 T=0;
		for (int i = 0; i < Files.size(); i++) {
			Sizes.push_back(_SFD_RSP(i, Files[i].FileSize));
		}
		sort(Sizes.begin(), Sizes.end());
		for (int i = 0; i < Sizes.size(); i++) {
			SumSize.push_back((T += Sizes[i].FileSize));
		}

		for (int i = 0; i < SumSize.size(); i++) {
			Files[Sizes[i].ID].GroupID = (WORD)(ceil(((float)SumSize[i] / ((float)SumSize.back()))*ThreadsCount) - 1.);
			cout << "Thread " << Files[Sizes[i].ID].GroupID << ": " << Sizes[i].FileSize << ":\t" << Sizes[i].ID << endl;
		}
	}
	void SetGlobalPPQN(WORD NewPPQN=0,BIT ForceGlobalPPQNOverride=false) {
		if (!NewPPQN && ForceGlobalPPQNOverride)
			return;
		if (!ForceGlobalPPQNOverride)
			NewPPQN = GlobalPPQN;
		if (!ForceGlobalPPQNOverride && (!NewPPQN || IncrementalPPQN)) {
			for (int i = 0; i < Files.size(); i++)
				if (NewPPQN < Files[i].OldPPQN)NewPPQN = Files[i].OldPPQN;
		}
		for (int i = 0; i < Files.size(); i++)
			Files[i].NewPPQN = NewPPQN;
		GlobalPPQN = NewPPQN;
	}
	void SetGlobalOffset(DWORD Offset) {
		for (int i = 0; i < Files.size(); i++)
			Files[i].OffsetTicks = Offset;
		GlobalOffset = Offset;
	}
	void SetGlobalTempo(INT32 NewTempo) {
		for (int i = 0; i < Files.size(); i++)
			Files[i].NewTempo = NewTempo;
		GlobalNewTempo = NewTempo;
	}
	void RemoveByID(DWORD ID) {
		if (ID < Files.size()) {
			Files.erase(Files.begin() + ID);
		}
	}
	MIDICollectionThreadedMerger* MCTM_Constructor() {
		vector<SingleMIDIReProcessor*> SMRPv;
		for (int i = 0; i < Files.size(); i++)
			SMRPv.push_back(Files[i].BuildSMRP());
		MIDICollectionThreadedMerger *MCTM = new MIDICollectionThreadedMerger(SMRPv,GlobalPPQN,SaveDirectory);
		MCTM->RemnantsRemove = DefaultBoolSettings & _BoolSettings::remove_remnants;
		return MCTM;
	}
	FileSettings& operator[](INT32 ID) {
		return Files[ID];
	}
};
SAFCData _Data;
MIDICollectionThreadedMerger *GlobalMCTM = NULL;

void ThrowAlert_Error(string AlertText) {
	if (WH)
		WH->ThrowAlert(AlertText, "ERROR!", SpecialSigns::DrawExTriangle, 1, 0xFFAF00FF, 0xFF);
}

void ThrowAlert_Warning(string AlertText) {
	if (WH)
		WH->ThrowAlert(AlertText, "Warning!", SpecialSigns::DrawExTriangle, 1, 0x7F7F7FFF, 0xFFFFFFAF);
}

vector<wstring> MOFD(const wchar_t* Title) {
	OPENFILENAME ofn;       // common dialog box structure
	wchar_t szFile[50000];       // buffer for file name
	vector<wstring> InpLinks;
	ZeroMemory(&ofn, sizeof(ofn));
	ZeroMemory(szFile, 50000);
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
	if (GetOpenFileName(&ofn)) {
		wstring Link = L"", Gen = L"";
		int i = 0, counter = 0;
		for (; i < 600 && szFile[i] != '\0'; i++) {
			Link.push_back(szFile[i]);
		}
		for (; i < 49998;) {
			counter++;
			Gen = L"";
			for (; i < 49998 && szFile[i] != '\0'; i++) {
				Gen.push_back(szFile[i]);
			}
			i++;
			if (szFile[i] == '\0') {
				if (counter == 1) InpLinks.push_back(Link);
				else InpLinks.push_back(Link + L"\\" + Gen);
				break;
			}
			else {
				if (Gen != L"")InpLinks.push_back(Link + L"\\" + Gen);
			}
		}
		return InpLinks;
	}
	else {
		switch (CommDlgExtendedError()) {
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
		return vector<wstring>{L""};
	}
}
wstring SOFD(const wchar_t* Title) {
	wchar_t filename[MAX_PATH];
	OPENFILENAME ofn;
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
	if (GetSaveFileName(&ofn)) return wstring(filename);
	else {
		switch (CommDlgExtendedError()) {
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
}

//////////IMPORTANT STUFFS ABOVE///////////

#define _WH(Window,Element) ((*(*WH)[Window])[Element])//...uh
#define _WH_t(Window,Element,Type) ((Type)_WH(Window,Element))

void AddFiles(vector<wstring> Filenames) {
	WH->DisableAllWindows();
	for (int i = 0; i < Filenames.size(); i++) {
		if (Filenames[i].empty())continue;
		_Data.Files.push_back(FileSettings(Filenames[i]));
		if (_Data.Files.back().IsMIDI) {
			DWORD Counter = 0;
			_WH_t("MAIN", "List", SelectablePropertedList*)->SafePushBackNewString(_Data.Files.back().AppearanceFilename);
			_Data.Files.back().NewTempo = _Data.GlobalNewTempo;
			_Data.Files.back().OffsetTicks = _Data.GlobalOffset;
			_Data.Files.back().InplaceMergeEnabled = _Data.InplaceMergeFlag;
			for (int q = 0; q < _Data.Files.size(); q++) {
				if (_Data[q].Filename == _Data.Files.back().Filename) {
					_Data[q].FileNamePostfix = to_string(Counter) + "_.mid";
					_Data[q].WFileNamePostfix = to_wstring(Counter) + L"_.mid";
					Counter++;
				}
			}
		}
		else {
			_Data.Files.pop_back();
		}
	}
	_Data.SetGlobalPPQN();
	_Data.ResolveSubdivisionProblem_GroupIDAssign();
}
void OnAdd() {
	//throw "";
	vector<wstring> Filenames = MOFD(L"Select midi files");
	AddFiles(Filenames);
}
namespace PropsAndSets {
	string *PPQN=new string(""), *OFFSET = new string(""), *TEMPO = new string("");
	int currentID=-1,CaTID=-1,VMID=-1,PMID=-1;
	SingleMIDIInfoCollector* SMICptr = nullptr;
	string CSV_DELIM = ",";
	void OGPInMIDIList(int ID) {
		if (ID<_Data.Files.size() && ID>=0) {
			currentID = ID;
			auto PASWptr = (*WH)["SMPAS"];
			((TextBox*)((*PASWptr)["FileName"]))->SafeStringReplace("..." + _Data[ID].AppearanceFilename);
			((InputField*)((*PASWptr)["PPQN"]))->SafeStringReplace(to_string((_Data[ID].NewPPQN)? _Data[ID].NewPPQN : _Data[ID].OldPPQN));
			((InputField*)((*PASWptr)["TEMPO"]))->SafeStringReplace(to_string(_Data[ID].NewTempo));
			((InputField*)((*PASWptr)["OFFSET"]))->SafeStringReplace(to_string(_Data[ID].OffsetTicks));
			((InputField*)((*PASWptr)["GROUPID"]))->SafeStringReplace(to_string(_Data[ID].GroupID));

			((InputField*)((*PASWptr)["SELECT_START"]))->SafeStringReplace(to_string(_Data[ID].Start));
			((InputField*)((*PASWptr)["SELECT_LENGTH"]))->SafeStringReplace(to_string(_Data[ID].Length));

			((CheckBox*)((*PASWptr)["BOOL_REM_TRCKS"]))->State = _Data[ID].BoolSettings&_BoolSettings::remove_empty_tracks;
			((CheckBox*)((*PASWptr)["BOOL_REM_REM"]))->State = _Data[ID].BoolSettings&_BoolSettings::remove_remnants;
			((CheckBox*)((*PASWptr)["BOOL_PIANO_ONLY"]))->State = _Data[ID].BoolSettings&_BoolSettings::all_instruments_to_piano;
			((CheckBox*)((*PASWptr)["BOOL_IGN_TEMPO"]))->State = _Data[ID].BoolSettings&_BoolSettings::ignore_tempos;
			((CheckBox*)((*PASWptr)["BOOL_IGN_PITCH"]))->State = _Data[ID].BoolSettings&_BoolSettings::ignore_pitches;
			((CheckBox*)((*PASWptr)["BOOL_IGN_NOTES"]))->State = _Data[ID].BoolSettings&_BoolSettings::ignore_notes;
			((CheckBox*)((*PASWptr)["BOOL_IGN_ALL_EX_TPS"]))->State = _Data[ID].BoolSettings&_BoolSettings::ignore_all_but_tempos_notes_and_pitch;

			((CheckBox*)((*PASWptr)["INPLACE_MERGE"]))->State = _Data[ID].InplaceMergeEnabled;


			((TextBox*)((*PASWptr)["CONSTANT_PROPS"]))->SafeStringReplace(
				"File size: " + to_string(_Data[ID].FileSize) + "b\n" +
				"Old PPQN: " + to_string(_Data[ID].OldPPQN) + "\n" +
				"Track number (header info): " + to_string(_Data[ID].OldTrackNumber) + "\n" +
				"\"Remnant\" file postfix: " + _Data[ID].FileNamePostfix
			);

			WH->EnableWindow("SMPAS");
		}
		else {
			currentID = -1;
		}
	}
	void InitializeCollecting() {
		//ThrowAlert_Warning("Still testing :)");
		if (currentID < 0 && currentID >= _Data.Files.size()) {
			ThrowAlert_Error("How you've managed to select the midi beyond the list? O.o\n" + to_string(currentID));
			return;
		}
		auto UIElement = (Graphing<SingleMIDIInfoCollector::tempo_graph>*)(*(*WH)["SMIC"])["TEMPO_GRAPH"];
		if (SMICptr) {
			UIElement->Lock.lock();
			UIElement->Lock.unlock();
			UIElement->Graph = nullptr;
			//delete SMICptr;
		}
		SMICptr = new SingleMIDIInfoCollector(_Data.Files[currentID].Filename, _Data.Files[currentID].OldPPQN);
		thread th([]() {
			WH->MainWindow_ID = "SMIC";
			WH->DisableAllWindows();
			thread ith([]() {
				auto PG_Exp = (*(*WH)["SMIC"])["PG_EXP"];
				auto TG_Exp = (*(*WH)["SMIC"])["TG_EXP"];
				auto ITicks = (*(*WH)["SMIC"])["INTEGRATE_TICKS"];
				auto ITime = (*(*WH)["SMIC"])["INTEGRATE_TIME"];
				auto ErrorLine = (TextBox*)(*(*WH)["SMIC"])["FEL"];
				auto InfoLine = (TextBox*)(*(*WH)["SMIC"])["FLL"];
				auto UIMinutes = (InputField*)(*(*WH)["SMIC"])["INT_MIN"];
				auto UISeconds = (InputField*)(*(*WH)["SMIC"])["INT_SEC"];
				auto UIT = (InputField*)(*(*WH)["SMIC"])["TG_SWITCH"];
				auto UIP= (InputField*)(*(*WH)["SMIC"])["PG_SWITCH"];
				auto UIMilliseconds = (InputField*)(*(*WH)["SMIC"])["INT_MSC"];
				auto UITicks = (InputField*)(*(*WH)["SMIC"])["INT_TIC"];
				auto UIElement_TG = (Graphing<SingleMIDIInfoCollector::tempo_graph>*)(*(*WH)["SMIC"])["TEMPO_GRAPH"];
				auto UIElement_PG = (Graphing<SingleMIDIInfoCollector::polyphony_graph>*)(*(*WH)["SMIC"])["POLY_GRAPH"];
				UIElement_PG->Enabled = false;
				UIElement_PG->Reset();
				UIElement_TG->Enabled = false;
				UIElement_TG->Reset();
				UIP->SafeStringReplace("Enable graph B");
				UIT->SafeStringReplace("Enable graph A");
				InfoLine->SafeStringReplace("Please wait...");
				UIMinutes->SafeStringReplace("0");
				UISeconds->SafeStringReplace("0");
				UIMilliseconds->SafeStringReplace("0");
				UITicks->SafeStringReplace("0");
				PG_Exp->Disable();
				TG_Exp->Disable();
				ITicks->Disable();
				ITime->Disable();
				while (!SMICptr->Finished){
					if (ErrorLine->Text != SMICptr->ErrorLine)
						ErrorLine->SafeStringReplace(SMICptr->ErrorLine);
					if (InfoLine->Text != SMICptr->LogLine)
						InfoLine->SafeStringReplace(SMICptr->LogLine);
					Sleep(100);
				}
				InfoLine->SafeStringReplace("Finished");
				PG_Exp->Enable();
				TG_Exp->Enable();
				ITicks->Enable();
				ITime->Enable();
			});
			ith.detach();
			SMICptr->Lookup();
			auto UIElement_TG = (Graphing<SingleMIDIInfoCollector::tempo_graph>*)(*(*WH)["SMIC"])["TEMPO_GRAPH"];
			UIElement_TG->Graph = &(SMICptr->TempoMap);
			auto UIElement_PG = (Graphing<SingleMIDIInfoCollector::polyphony_graph>*)(*(*WH)["SMIC"])["POLY_GRAPH"];
			UIElement_PG->Graph = &(SMICptr->PolyphonyFiniteDifference);
			auto UIElement_TB = (TextBox*)(*(*WH)["SMIC"])["TOTAL_INFO"];
			UIElement_TB->SafeStringReplace(
				"Total (real) tracks: " + to_string(SMICptr->Tracks.size()) + "; ... "
				);

			WH->MainWindow_ID = "MAIN";
			WH->EnableWindow("MAIN");
			WH->EnableWindow("SMIC");
		});
		th.detach();
		WH->EnableWindow("SMIC");
	}
	namespace SMIC {
		void EnablePG() {
			auto UIElement_PG = (Graphing<SingleMIDIInfoCollector::polyphony_graph>*)(*(*WH)["SMIC"])["POLY_GRAPH"];
			auto UIElement_Butt = (Button*)(*(*WH)["SMIC"])["PG_SWITCH"];
			if (UIElement_PG->Enabled)
				UIElement_Butt->SafeStringReplace("Enable graph B");
			else
				UIElement_Butt->SafeStringReplace("Disable graph B");
			UIElement_PG->Enabled ^= true;
		}
		void EnableTG() {
			auto UIElement_TG = (Graphing<SingleMIDIInfoCollector::tempo_graph>*)(*(*WH)["SMIC"])["TEMPO_GRAPH"];
			auto UIElement_Butt = (Button*)(*(*WH)["SMIC"])["TG_SWITCH"];
			if (UIElement_TG->Enabled)
				UIElement_Butt->SafeStringReplace("Enable graph A");
			else
				UIElement_Butt->SafeStringReplace("Disable graph A");
			UIElement_TG->Enabled ^= true;
		}
		void ExportTG() {
			thread th([]() {
				WH->MainWindow_ID = "SMIC";
				WH->DisableAllWindows();
				auto InfoLine = (TextBox*)(*(*WH)["SMIC"])["FLL"];
				InfoLine->SafeStringReplace("Graph A is exporting...");
				ofstream out(SMICptr->FileName+L".tg.csv");
				out << "tick" << CSV_DELIM << "tempo" << '\n';
				for (auto cur_pair : SMICptr->TempoMap)
					out << cur_pair.first << CSV_DELIM << cur_pair.second << '\n';
				out.close();
				WH->MainWindow_ID = "MAIN";
				WH->EnableWindow("MAIN");
				WH->EnableWindow("SMIC");
				InfoLine->SafeStringReplace("Graph A was successfully exported...");
			});
			th.detach();
		}
		void ResetTG() {//TEMPO_GRAPH
			auto UIElement_TG = (Graphing<SingleMIDIInfoCollector::tempo_graph>*)(*(*WH)["SMIC"])["TEMPO_GRAPH"];
			UIElement_TG->Reset();
		}
		void ResetPG() {//TEMPO_GRAPH
			auto UIElement_PG = (Graphing<SingleMIDIInfoCollector::tempo_graph>*)(*(*WH)["SMIC"])["POLY_GRAPH"];
			UIElement_PG->Reset();
		}
		void ExportPG() {
			thread th([]() {
				WH->MainWindow_ID = "SMIC";
				WH->DisableAllWindows();
				auto InfoLine = (TextBox*)(*(*WH)["SMIC"])["FLL"];
				InfoLine->SafeStringReplace("Graph B is exporting...");
				ofstream out(SMICptr->FileName + L".pg.csv");
				out << "tick" << CSV_DELIM << "NoteOffs" << CSV_DELIM << "NoteOns" << CSV_DELIM << "PolyphonyDifference" << '\n';
				for (auto cur_pair : SMICptr->PolyphonyFiniteDifference)
					out << cur_pair.first << CSV_DELIM << cur_pair.second.NoteOff << CSV_DELIM << cur_pair.second.NoteOn << CSV_DELIM << cur_pair.second << '\n';
				out.close();
				WH->MainWindow_ID = "MAIN";
				WH->EnableWindow("MAIN");
				WH->EnableWindow("SMIC");
				InfoLine->SafeStringReplace("Graph B was successfully exported...");
			});
			th.detach();
		}
		void DiffirentiateTicks() {
			thread th([]() {
				WH->MainWindow_ID = "SMIC";
				WH->DisableAllWindows();
				auto InfoLine = (*(*WH)["SMIC"])["FLL"];
				auto UITicks = (InputField*)(*(*WH)["SMIC"])["INT_TIC"];
				auto UIOutput = (TextBox*)(*(*WH)["SMIC"])["ANSWER"];
				InfoLine->SafeStringReplace("Integration has begun");

				INT64 ticks_limit = 0;
				if (UITicks->CurrentString.size())
					ticks_limit = stoi(UITicks->CurrentString);

				INT64 prev_tick = 0, cur_tick = 0;
				double cur_seconds = 0;
				double prev_second = 0;
				double PPQ = SMICptr->PPQ;
				INT64 last_tick = (*SMICptr->TempoMap.rbegin()).first;
				for (auto cur_pair : SMICptr->TempoMap/*; cur_pair != SMICptr->TempoMap.end(); cur_pair++*/) {
					cur_tick = cur_pair.first;
					double cur_tempo = cur_pair.second;
					cur_seconds += (cur_tick - prev_tick) * (60 / (cur_tempo * PPQ));
					if (cur_tick > ticks_limit || cur_tick == last_tick)
						break;
					prev_second = cur_seconds;
					prev_tick = cur_tick;
				}
				auto cur = cur_tick - prev_tick;
				ticks_limit -= prev_tick;
				double rate = ((cur) ? ((double)ticks_limit / cur) : 0);
				double seconds_ans = (cur_seconds - prev_second) * rate + prev_second;
				cout << seconds_ans << endl;
				double milliseconds_ans = seconds_ans;
				double minutes_ans = 0;
				milliseconds_ans = modf(milliseconds_ans, &seconds_ans);
				seconds_ans = modf(seconds_ans / 60, &minutes_ans)*60;
				//swap(seconds_ans, minutes_ans);
				//seconds_ans *= 60;
				milliseconds_ans *= 1000;

				UIOutput->SafeStringReplace(
					"Min: " + to_string((int)(minutes_ans)) + 
					"\nSec: " + to_string((int)(seconds_ans)) +
					"\nMsec: "+ to_string((int)(milliseconds_ans))
					);

				WH->MainWindow_ID = "MAIN";
				WH->EnableWindow("MAIN");
				WH->EnableWindow("SMIC");
				InfoLine->SafeStringReplace("Integration was succsessfully finished");
				});
			th.detach();
		}
		void IntegrateTime() {
			WH->MainWindow_ID = "SMIC";
			WH->DisableAllWindows();
			auto InfoLine = (*(*WH)["SMIC"])["FLL"];
			auto UIMinutes = (InputField*)(*(*WH)["SMIC"])["INT_MIN"];
			auto UISeconds = (InputField*)(*(*WH)["SMIC"])["INT_SEC"];
			auto UIMilliseconds = (InputField*)(*(*WH)["SMIC"])["INT_MSC"];
			auto UIOutput = (TextBox*)(*(*WH)["SMIC"])["ANSWER"];
			InfoLine->SafeStringReplace("Integration has begun");

			double seconds_limit = 0;
			if (UIMinutes->CurrentString.size())
				seconds_limit += stoi(UIMinutes->CurrentString)*60.; 
			if (UISeconds->CurrentString.size())
				seconds_limit += stoi(UISeconds->CurrentString);
			if (UIMilliseconds->CurrentString.size())
				seconds_limit += stoi(UIMilliseconds->CurrentString)/1000.;

			INT64 prev_tick = 0, cur_tick = 0;
			double cur_seconds = 0;
			double prev_second = 0;
			double PPQ = SMICptr->PPQ;
			INT64 last_tick = (*SMICptr->TempoMap.rbegin()).first;
			for (auto cur_pair : SMICptr->TempoMap/*; cur_pair != SMICptr->TempoMap.end(); cur_pair++*/) {
				cur_tick = cur_pair.first;
				double cur_tempo = cur_pair.second;
				cur_seconds += (cur_tick - prev_tick) * (60 / (cur_tempo * PPQ));
				if (cur_seconds > seconds_limit || cur_tick == last_tick)
					break;
				prev_second = cur_seconds;
				prev_tick = cur_tick;
			}
			cur_seconds -= prev_second;
			seconds_limit -= prev_second;
			auto rate = (seconds_limit == 0) ? 0 : seconds_limit / cur_seconds;
			INT64 tick = (cur_tick - prev_tick) * rate + prev_tick;

			UIOutput->SafeStringReplace("Tick: " + to_string(tick));

			WH->MainWindow_ID = "MAIN";
			WH->EnableWindow("MAIN");
			WH->EnableWindow("SMIC");
			InfoLine->SafeStringReplace("Integration was succsessfully finished");
		}
	}
	void OR() {
		if (currentID > -1) {
			/*auto Win = (*WH)["OR"];
			OR::OverlapsRemover *_OR = new OR::OverlapsRemover(_Data[currentID].FileSize);
			thread th([&](OR::OverlapsRemover *OR, DWORD id) {
				OR->Load(_Data[id].Filename);
			}, _OR, currentID);
			th.detach();
			thread _th([&](OR::OverlapsRemover *OR, MoveableWindow *MW) {
				while (true) {
					Sleep(33);
					((TextBox*)(*MW)["TEXT"])->SafeStringReplace(OR->s_out);
					if (OR->Finished)
						break;
				}
				delete OR;
				}, _OR, Win);
			_th.detach();
			Win->SafeWindowRename(
				_Data[currentID].AppearanceFilename.size() <= 40 ? 
					_Data[currentID].AppearanceFilename :
					_Data[currentID].AppearanceFilename.substr(40)
			);
			WH->EnableWindow("OR");*/
		}
	}
	void SR() {
		if (currentID > -1) {

		}
	}
	void OnApplySettings() {
		if (currentID < 0 && currentID >= _Data.Files.size()) {
			ThrowAlert_Error("You cannot apply current settings to file with ID " + to_string(currentID));
			return;
		}
		INT32 T;
		string CurStr="";
		auto SMPASptr = (*WH)["SMPAS"];

		CurStr = ((InputField*)(*SMPASptr)["PPQN"])->CurrentString; 
		if (CurStr.size()) {
			T = stoi(CurStr);
			if (T)_Data[currentID].NewPPQN = T;
		}

		CurStr = ((InputField*)(*SMPASptr)["TEMPO"])->CurrentString;
		if (CurStr.size()) {
			T = stoi(CurStr);
			_Data[currentID].NewTempo = T;
		}

		CurStr = ((InputField*)(*SMPASptr)["OFFSET"])->CurrentString;
		if (CurStr.size()) {
			T = stoi(CurStr);
			_Data[currentID].OffsetTicks = T;
		}

		CurStr = ((InputField*)(*SMPASptr)["GROUPID"])->CurrentString;
		if (CurStr.size()) {
			T = stoi(CurStr);
			if (T != _Data[currentID].GroupID) {
				_Data[currentID].GroupID = T;
				ThrowAlert_Error("Manual GroupID editing might cause significant drop of processing perfomance!");
			}
		}

		CurStr = ((InputField*)(*SMPASptr)["SELECT_START"])->CurrentString;
		if (CurStr.size()) {
			T = stoll(CurStr);
			if (T) _Data[currentID].Start = T;
		}

		CurStr = ((InputField*)(*SMPASptr)["SELECT_LENGTH"])->CurrentString;
		if (CurStr.size()) {
			T = stoll(CurStr);
			if (T) _Data[currentID].Length = T;
		}

		_Data[currentID].SetBoolSetting(_BoolSettings::remove_empty_tracks, (((CheckBox*)(*SMPASptr)["BOOL_REM_TRCKS"])->State));
		_Data[currentID].SetBoolSetting(_BoolSettings::remove_remnants, (((CheckBox*)(*SMPASptr)["BOOL_REM_REM"])->State));
		_Data[currentID].SetBoolSetting(_BoolSettings::all_instruments_to_piano, (((CheckBox*)(*SMPASptr)["BOOL_PIANO_ONLY"])->State));
		_Data[currentID].SetBoolSetting(_BoolSettings::ignore_tempos, (((CheckBox*)(*SMPASptr)["BOOL_IGN_TEMPO"])->State));
		_Data[currentID].SetBoolSetting(_BoolSettings::ignore_pitches, (((CheckBox*)(*SMPASptr)["BOOL_IGN_PITCH"])->State));
		_Data[currentID].SetBoolSetting(_BoolSettings::ignore_notes, (((CheckBox*)(*SMPASptr)["BOOL_IGN_NOTES"])->State));
		_Data[currentID].SetBoolSetting(_BoolSettings::ignore_all_but_tempos_notes_and_pitch, (((CheckBox*)(*SMPASptr)["BOOL_IGN_ALL_EX_TPS"])->State));

		_Data[currentID].InplaceMergeEnabled = ((CheckBox*)(*SMPASptr)["INPLACE_MERGE"])->State;
	}
	void OnApplyBS2A() {
		if (currentID < 0 && currentID >= _Data.Files.size()) {
			ThrowAlert_Error("You cannot apply current settings to file with ID " + to_string(currentID));
			return;
		}
		OnApplySettings();
		for (auto Y = _Data.Files.begin(); Y != _Data.Files.end(); Y++) {
			DefaultBoolSettings = Y->BoolSettings = _Data[currentID].BoolSettings;
			_Data.InplaceMergeFlag = Y->InplaceMergeEnabled = _Data[currentID].InplaceMergeEnabled;
		}
	}

	namespace CutAndTranspose {
		BYTE TMax=0, TMin=0;
		SHORT TTransp=0;
		void OnCaT() {
			auto Wptr = (*WH)["CAT"];
			auto CATptr = (CAT_Piano*)((*Wptr)["CAT_ITSELF"]);
			if (!_Data[currentID].KeyMap) _Data[currentID].KeyMap = new CutAndTransposeKeys(0,127,0);
			CATptr->PianoTransform = _Data[currentID].KeyMap;
			CATptr->UpdateInfo();
			WH->EnableWindow("CAT");
		}
		void OnReset() {
			auto Wptr = (*WH)["CAT"];
			auto CATptr = (CAT_Piano*)((*Wptr)["CAT_ITSELF"]);
			CATptr->PianoTransform->Max = 255;
			CATptr->PianoTransform->Min = 0;
			CATptr->PianoTransform->TransposeVal = 0;
			CATptr->UpdateInfo();
		}
		void OnCDT128() {
			auto Wptr = (*WH)["CAT"];
			auto CATptr = (CAT_Piano*)((*Wptr)["CAT_ITSELF"]);
			CATptr->PianoTransform->Max = 127;
			CATptr->PianoTransform->Min = 0;
			CATptr->PianoTransform->TransposeVal = 0;
			CATptr->UpdateInfo();
		}
		void On0_127to128_255() {
			auto Wptr = (*WH)["CAT"];
			auto CATptr = (CAT_Piano*)((*Wptr)["CAT_ITSELF"]);
			CATptr->PianoTransform->Max = 127;
			CATptr->PianoTransform->Min = 0;
			CATptr->PianoTransform->TransposeVal = 128;
			CATptr->UpdateInfo();
		}
		void OnCopy() {
			auto Wptr = (*WH)["CAT"];
			auto CATptr = (CAT_Piano*)((*Wptr)["CAT_ITSELF"]);
			TMax = CATptr->PianoTransform->Max;
			TMin = CATptr->PianoTransform->Min;
			TTransp = CATptr->PianoTransform->TransposeVal;
		}
		void OnPaste() {
			auto Wptr = (*WH)["CAT"];
			auto CATptr = (CAT_Piano*)((*Wptr)["CAT_ITSELF"]);
			CATptr->PianoTransform->Max = TMax;
			CATptr->PianoTransform->Min = TMin;
			CATptr->PianoTransform->TransposeVal = TTransp;
			CATptr->UpdateInfo();
		}
		void OnDelete() {
			auto Wptr = (*WH)["CAT"];
			((CAT_Piano*)((*Wptr)["CAT_ITSELF"]))->PianoTransform = NULL;
			WH->DisableWindow("CAT");
			delete _Data[currentID].KeyMap;
			_Data[currentID].KeyMap = NULL;
		}
	}
	namespace VolumeMap {
		void OnVolMap() {
			auto Wptr = (*WH)["VM"];
			auto VM = ((PLC_VolumeWorker*)(*Wptr)["VM_PLC"]);
			auto IFDeg = ((InputField*)(*Wptr)["VM_DEGREE"]);
			((Button*)(*Wptr)["VM_SETMODE"])->SafeStringReplace("Single");
			IFDeg->UpdateInputString("1");
			IFDeg->CurrentString = "";
			VM->ActiveSetting = 0;
			VM->Hovered = 0;
			VM->RePutMode = 0;
			if (!_Data[currentID].VolumeMap)_Data[currentID].VolumeMap = new PLC<BYTE, BYTE>();
			VM->PLC_bb = _Data[currentID].VolumeMap;
			WH->EnableWindow("VM");
		}
		void OnDegreeShape() {
			auto Wptr = (*WH)["VM"];
			auto VM = ((PLC_VolumeWorker*)(*Wptr)["VM_PLC"]);
			if (VM->PLC_bb) {
				auto IFDeg = ((InputField*)(*Wptr)["VM_DEGREE"]);
				float Degree=1.;
				if (IFDeg->CurrentString.size())Degree = stof(IFDeg->CurrentString);
				VM->PLC_bb->ConversionMap.clear();
				VM->PLC_bb->ConversionMap[127] = 127;
				for (int i = 0; i < 128; i++) {
					VM->PLC_bb->InsertNewPoint(i, ceil(pow(i / 127., Degree)*127.));
				}
			}
			else 
				ThrowAlert_Error("If you see this message, some error might have happen, since PLC_bb is null");
		}
		void OnSimplify() {
			auto Wptr = (*WH)["VM"];
			auto VM = ((PLC_VolumeWorker*)(*Wptr)["VM_PLC"]);
			if (VM->PLC_bb) {
				VM->_MakeMapMoreSimple();
			}
			else
				ThrowAlert_Error("If you see this message, some error might have happen, since PLC_bb is null");
		}
		void OnTrace() {
			auto Wptr = (*WH)["VM"];
			auto VM = ((PLC_VolumeWorker*)(*Wptr)["VM_PLC"]);
			if (VM->PLC_bb) {
				if (VM->PLC_bb->ConversionMap.empty())return;
				BYTE C[256];
				for (int i = 0; i < 255; i++) {
					C[i] = VM->PLC_bb->AskForValue(i);
				}
				for (int i = 0; i < 255; i++) {
					VM->PLC_bb->ConversionMap[i] = C[i];
				}
			}
			else
				ThrowAlert_Error("If you see this message, some error might have happen, since PLC_bb is null");
		}
		void OnSetModeChange() {
			auto Wptr = (*WH)["VM"];
			auto VM = ((PLC_VolumeWorker*)(*Wptr)["VM_PLC"]);
			if (VM->PLC_bb) {
				VM->RePutMode = !VM->RePutMode;
				((Button*)(*Wptr)["VM_SETMODE"])->SafeStringReplace(((VM->RePutMode)?"Double":"Single"));
			}
			else
				ThrowAlert_Error("If you see this message, some error might have happen, since PLC_bb is null");
		}
		void OnErase() {
			auto Wptr = (*WH)["VM"];
			auto VM = ((PLC_VolumeWorker*)(*Wptr)["VM_PLC"]);
			if (VM->PLC_bb)
				VM->PLC_bb->ConversionMap.clear();
			else
				ThrowAlert_Error("If you see this message, some error might have happen, since PLC_bb is null");
		}
		void OnDelete() {
			if (_Data[currentID].VolumeMap) {
				delete _Data[currentID].VolumeMap;
				_Data[currentID].VolumeMap = NULL;
			}
			auto Wptr = (*WH)["VM"];
			auto VM = ((PLC_VolumeWorker*)(*Wptr)["VM_PLC"]);
			VM->PLC_bb = NULL;
			WH->DisableWindow("VM");
		}
	}
	void OnPitchMap() {
		ThrowAlert_Error("Having hard time thinking of how to implement it...\nNot available... yet...");
	}
}

void OnRem() {
	auto ptr = _WH_t("MAIN", "List", SelectablePropertedList*);
	for (auto ID = ptr->SelectedID.rbegin(); ID != ptr->SelectedID.rend(); ID++) {
		_Data.RemoveByID(*ID);
	}
	ptr->RemoveSelected();
	WH->DisableAllWindows();
	_Data.SetGlobalPPQN();
	_Data.ResolveSubdivisionProblem_GroupIDAssign();
}

void OnRemAll() {
	auto ptr = _WH_t("MAIN", "List", SelectablePropertedList*);
	WH->DisableAllWindows();
	while(_Data.Files.size()){
		_Data.RemoveByID(0);
		ptr->SafeRemoveStringByID(0);
	}
	//_Data.SetGlobalPPQN();
	//_Data.ResolveSubdivisionProblem_GroupIDAssign();
}

void OnSubmitGlobalPPQN() {
	auto pptr = (*WH)["PROMPT"];
	string t = ((InputField*)(*pptr)["FLD"])->CurrentString;
	WORD PPQN = (t.size())?stoi(t):_Data.GlobalPPQN;
	_Data.SetGlobalPPQN(PPQN, true);
	WH->DisableWindow("PROMPT");
	//PropsAndSets::OGPInMIDIList(PropsAndSets::currentID);
}
void OnGlobalPPQN() {
	WH->ThrowPrompt("New value will be assigned to every MIDI\n(in settings)", "Global PPQN", OnSubmitGlobalPPQN, _Align::center, InputField::Type::NaturalNumbers, to_string(_Data.GlobalPPQN), 5);
}

void OnSubmitGlobalOffset() {
	auto pptr = (*WH)["PROMPT"];
	string t = ((InputField*)(*pptr)["FLD"])->CurrentString;
	DWORD O = (t.size()) ? stoi(t) : _Data.GlobalOffset;
	_Data.SetGlobalOffset(O);
	WH->DisableWindow("PROMPT");
	//PropsAndSets::OGPInMIDIList(PropsAndSets::currentID);
}
void OnGlobalOffset() {
	WH->ThrowPrompt("Sets new global Offset", "Global Offset", OnSubmitGlobalOffset, _Align::center, InputField::Type::NaturalNumbers, to_string(_Data.GlobalOffset), 10);
	cout << _Data.GlobalOffset << to_string(_Data.GlobalOffset) << endl;
}

void OnSubmitGlobalTempo() {
	auto pptr = (*WH)["PROMPT"];
	string t = ((InputField*)(*pptr)["FLD"])->CurrentString;
	INT32 Tempo = (t.size()) ? stoi(t) : _Data.GlobalNewTempo;
	_Data.SetGlobalTempo(Tempo);
	WH->DisableWindow("PROMPT");
	//PropsAndSets::OGPInMIDIList(PropsAndSets::currentID);
}
void OnGlobalTempo() {
	WH->ThrowPrompt("Sets specific tempo value to every MIDI\n(in settings)", "Global S. Tempo\0", OnSubmitGlobalTempo, _Align::center, InputField::Type::WholeNumbers, to_string(_Data.GlobalNewTempo), 8);
}

void _OnResolve() {
	_Data.ResolveSubdivisionProblem_GroupIDAssign();
}

void OnRemVolMaps() {
	((PLC_VolumeWorker*)(*((*WH)["VM"]))["VM_PLC"])->PLC_bb = NULL;
	WH->DisableWindow("VM");
	for (int i = 0; i < _Data.Files.size(); i++) {
		if(_Data[i].VolumeMap)delete _Data[i].VolumeMap;
		_Data[i].VolumeMap = NULL;
	}
}
void OnRemCATs() {
	WH->DisableWindow("CAT");
	for (int i = 0; i < _Data.Files.size(); i++) {
		if(_Data[i].KeyMap)delete _Data[i].KeyMap;
		_Data[i].KeyMap = NULL;
	}
}
void OnRemPitchMaps() {
	//WH->DisableWindow("CAT");
	ThrowAlert_Error("Currently pitch maps can not be created and/or deleted :D");
	for (int i = 0; i < _Data.Files.size(); i++) {
		if(_Data[i].PitchBendMap)delete _Data[i].PitchBendMap;
		_Data[i].PitchBendMap = NULL;
	}
}
void OnRemAllModules() {
	OnRemVolMaps(); 
	OnRemCATs();
	//OnRemPitchMaps();
}

namespace Settings {
	INT ShaderMode = 0;
	float SinewaveWidth=0., Basewave=1., Param3=1.;
	WinReg::RegKey RK_Access;
	void OnSettings() {
		WH->EnableWindow("APP_SETTINGS");//_Data.DetectedThreads
		//WH->ThrowAlert("Please read the docs! Changing some of these settings might cause graphics driver failure!","Warning!",SpecialSigns::DrawExTriangle,1,0x007FFFFF,0x7F7F7FFF);
		auto pptr = (*WH)["APP_SETTINGS"];
		((InputField*)(*pptr)["AS_SHADERMODE"])->UpdateInputString(to_string(ShaderMode));
		((InputField*)(*pptr)["AS_SWW"])->UpdateInputString(to_string(SinewaveWidth));
		((InputField*)(*pptr)["AS_BW"])->UpdateInputString(to_string(Basewave));
		((InputField*)(*pptr)["AS_P3"])->UpdateInputString(to_string(Param3));
		((InputField*)(*pptr)["AS_ROT_ANGLE"])->UpdateInputString(to_string(ROT_ANGLE));
		((InputField*)(*pptr)["AS_THREADS_COUNT"])->UpdateInputString(to_string(_Data.DetectedThreads));

		((CheckBox*)((*pptr)["BOOL_REM_TRCKS"]))->State = DefaultBoolSettings & _BoolSettings::remove_empty_tracks;
		((CheckBox*)((*pptr)["BOOL_REM_REM"]))->State = DefaultBoolSettings & _BoolSettings::remove_remnants;
		((CheckBox*)((*pptr)["BOOL_PIANO_ONLY"]))->State = DefaultBoolSettings & _BoolSettings::all_instruments_to_piano;
		((CheckBox*)((*pptr)["BOOL_IGN_TEMPO"]))->State = DefaultBoolSettings & _BoolSettings::ignore_tempos;
		((CheckBox*)((*pptr)["BOOL_IGN_PITCH"]))->State = DefaultBoolSettings & _BoolSettings::ignore_pitches;
		((CheckBox*)((*pptr)["BOOL_IGN_NOTES"]))->State = DefaultBoolSettings & _BoolSettings::ignore_notes;
		((CheckBox*)((*pptr)["BOOL_IGN_ALL_EX_TPS"]))->State = DefaultBoolSettings & _BoolSettings::ignore_all_but_tempos_notes_and_pitch;

		((CheckBox*)((*pptr)["INPLACE_MERGE"]))->State = _Data.InplaceMergeFlag;
	}
	void OnSetApply() {
		bool RK_OP = false;
		try {
			Settings::RK_Access.Open(HKEY_CURRENT_USER, RegPath);
			RK_OP = true;
		}
		catch (...) {
			cout << "RK opening failed\n";
		}
		auto pptr = (*WH)["APP_SETTINGS"];
		string T;

		T = ((InputField*)(*pptr)["AS_SHADERMODE"])->CurrentString;
		cout << "AS_SHADERMODE " << T << endl;
		if (T.size()) {
			ShaderMode = stoi(T); 
			if (RK_OP)TRY_CATCH(RK_Access.SetDwordValue(L"AS_SHADERMODE",ShaderMode);,"Failed on setting AS_SHADERMODE")
		}
		cout << ShaderMode << endl;

		T = ((InputField*)(*pptr)["AS_SWW"])->CurrentString;
		cout << "AS_SWW " << T << endl;
		if (T.size()){
			SinewaveWidth = stof(T);
			if (RK_OP)TRY_CATCH(RK_Access.SetDwordValue(L"AS_SWW", *((DWORD*)&SinewaveWidth)); , "Failed on setting AS_SWW")
		}
		cout << SinewaveWidth << endl;

		T = ((InputField*)(*pptr)["AS_BW"])->CurrentString;
		cout << "AS_BW " << T << endl;
		if (T.size()) {
			Basewave = stof(T);
			if (RK_OP)TRY_CATCH(RK_Access.SetDwordValue(L"AS_BW", *((DWORD*)&Basewave));, "Failed on setting AS_BW")
		}
		cout << Basewave << endl;

		T = ((InputField*)(*pptr)["AS_P3"])->CurrentString;
		cout << "AS_P3 " << T << endl;
		if (T.size()) {
			Param3 = stof(T);
			if (RK_OP)TRY_CATCH(RK_Access.SetDwordValue(L"AS_P3", *((DWORD*)&Param3));, "Failed on setting AS_P3")
		}
		cout << Param3 << endl;

		T = ((InputField*)(*pptr)["AS_ROT_ANGLE"])->CurrentString;
		cout << "ROT_ANGLE " << T << endl;
		if (T.size() && !is_fonted) {
			ROT_ANGLE = stof(T);
		}
		cout << ROT_ANGLE << endl;

		T = ((InputField*)(*pptr)["AS_THREADS_COUNT"])->CurrentString;
		cout << "AS_THREADS_COUNT " << T << endl;
		if (T.size()) {
			_Data.DetectedThreads = stoi(T);
			_Data.ResolveSubdivisionProblem_GroupIDAssign();
			if (RK_OP)TRY_CATCH(RK_Access.SetDwordValue(L"AS_THREADS_COUNT", _Data.DetectedThreads);, "Failed on setting AS_THREADS_COUNT")
		}
		cout << _Data.DetectedThreads << endl;

		DefaultBoolSettings = (DefaultBoolSettings & (~_BoolSettings::remove_empty_tracks)) | (_BoolSettings::remove_empty_tracks * (!!((CheckBox*)(*pptr)["BOOL_REM_TRCKS"])->State));
		DefaultBoolSettings = (DefaultBoolSettings & (~_BoolSettings::remove_remnants)) | (_BoolSettings::remove_remnants * (!!((CheckBox*)(*pptr)["BOOL_REM_REM"])->State));
		DefaultBoolSettings = (DefaultBoolSettings & (~_BoolSettings::all_instruments_to_piano)) | (_BoolSettings::all_instruments_to_piano * (!!((CheckBox*)(*pptr)["BOOL_PIANO_ONLY"])->State));
		DefaultBoolSettings = (DefaultBoolSettings & (~_BoolSettings::ignore_tempos)) | (_BoolSettings::ignore_tempos * (!!((CheckBox*)(*pptr)["BOOL_IGN_TEMPO"])->State));
		DefaultBoolSettings = (DefaultBoolSettings & (~_BoolSettings::ignore_pitches)) | (_BoolSettings::ignore_pitches * (!!((CheckBox*)(*pptr)["BOOL_IGN_PITCH"])->State));
		DefaultBoolSettings = (DefaultBoolSettings & (~_BoolSettings::ignore_notes)) | (_BoolSettings::ignore_notes * (!!((CheckBox*)(*pptr)["BOOL_IGN_NOTES"])->State));
		DefaultBoolSettings = (DefaultBoolSettings & (~_BoolSettings::ignore_all_but_tempos_notes_and_pitch)) | (_BoolSettings::ignore_all_but_tempos_notes_and_pitch * (!!((CheckBox*)(*pptr)["BOOL_IGN_ALL_EX_TPS"])->State));

		if (RK_OP) { TRY_CATCH(RK_Access.SetDwordValue(L"DEFAULT_BOOL_SETTINGS", DefaultBoolSettings); , "Failed on setting DEFAULT_BOOL_SETTINGS") }

		if (RK_OP) { TRY_CATCH(RK_Access.SetDwordValue(L"FONTSIZE", lFontSymbolsInfo::Size); , "Failed on setting FONTSIZE") }

		if (RK_OP) { TRY_CATCH(RK_Access.SetDwordValue(L"FLOAT_FONTHTW", *(DWORD*)(&lFONT_HEIGHT_TO_WIDTH)); , "Failed on setting FLOAT_FONTHTW") }

		_Data.InplaceMergeFlag = (((CheckBox*)(*pptr)["INPLACE_MERGE"])->State);
		if (RK_OP)TRY_CATCH(RK_Access.SetDwordValue(L"AS_INPLACE_FLAG", _Data.InplaceMergeFlag); , "Failed on setting AS_INPLACE_FLAG")

		((InputField*)(*pptr)["AS_FONT_NAME"])->PutIntoSource();
		wstring ws(FONTNAME.begin(), FONTNAME.end());
		if (RK_OP)TRY_CATCH(RK_Access.SetStringValue(L"COLLAPSEDFONTNAME", ws.c_str());, "Failed on setting AS_SHADERMODE")
		if(RK_OP)
			Settings::RK_Access.Close();
	}
	void ChangeIsFontedVar() {
		is_fonted = !is_fonted;
		SetIsFontedVar(is_fonted);
		exit(1);
	}
	void ApplyToAll() {
		OnSetApply();
		for (auto Y = _Data.Files.begin(); Y != _Data.Files.end(); Y++) {
			Y->BoolSettings = DefaultBoolSettings;
			Y->InplaceMergeEnabled = _Data.InplaceMergeFlag;
		}
	}
	void ApplyFSWheel(double new_val) {
		lFontSymbolsInfo::Size = new_val;
	}
	void ApplyRelWheel(double new_val) {
		lFONT_HEIGHT_TO_WIDTH = new_val;
	}
}

pair<float, float> GetPositionForOneOf(INT32 Position, INT32 Amount, float UnitSize, float HeightRel) {
	pair<float, float> T(0,0);
	INT32 SideAmount = ceil(sqrt(Amount));
	T.first = (0 - (Position%SideAmount) + ((SideAmount - 1) / 2.f))*UnitSize;
	T.second = (0 - (Position / SideAmount) + ((SideAmount - 1) / 2.f))*UnitSize * HeightRel;
	return T;
}

void OnStart() {
	if (_Data.Files.empty())
		return;
	printf("Begining\n");
	WH->MainWindow_ID = "SMRP_CONTAINER";
	printf("MWID_OVERRIDE\n");
	WH->DisableAllWindows();
	printf("DisableAllWindows\n");
	WH->EnableWindow("SMRP_CONTAINER");
	printf("EnableSMRP\n");
	if (GlobalMCTM) {
		printf("MCTM detected\n");
		delete GlobalMCTM;
		printf("MCTM deleted\n");
		GlobalMCTM = NULL;
		printf("MCTM cleared\n");
	}
	GlobalMCTM = _Data.MCTM_Constructor();
	printf("MCTM constructed\n");
	GlobalMCTM->ProcessMIDIs();
	printf("MCTM Processing has begun\n");
	MoveableWindow *MW;
	INT32 ID = 0;
	if (WH) {
		Sleep(100);
		MW = (*WH)["SMRP_CONTAINER"];
		for (auto El : MW->WindowActivities) {
			if (El.first.substr(0, 6) == "SMRP_C")
				MW->DeleteUIElementByName(El.first);
		}
		for (ID = 0; ID < GlobalMCTM->Cur_Processing.size(); ID++) {
			cout << ID << endl;
			if (!GlobalMCTM->Cur_Processing[ID])
				continue;
			auto Q = GetPositionForOneOf(ID, GlobalMCTM->Cur_Processing.size(),140,0.7);
			auto Vis = new SMRP_Vis(Q.first, Q.second, System_White);
			string temp = "";
			MW->AddUIElement(temp = "SMRP_C" + to_string(ID), Vis);
			thread TH([](MIDICollectionThreadedMerger *pMCTM, MoveableWindow *MW, DWORD ID) {
				string SID = "SMRP_C" + to_string(ID);
				cout << SID << " Processing started" << endl;
				auto pVIS = ((SMRP_Vis*)(*MW)[SID]);
				while (GlobalMCTM->Cur_Processing[ID]) {
					pVIS->SMRP = GlobalMCTM->Cur_Processing[ID];
					Sleep(100);
				}
				cout << ID << " Processing stopped" << endl;
			}, GlobalMCTM, MW, ID);
			TH.detach();
		}

		thread LO([](MIDICollectionThreadedMerger *pMCTM, SAFCData *SD, MoveableWindow *MW) {
			while (!pMCTM->CheckSMRPFinishedFlags()) {
				Sleep(100);
			}
			cout << "SMRP: Out from sleep\n";
			for (int i = 0; i <= SD->DetectedThreads; i++) {
				MW->DeleteUIElementByName("SMRP_C" + to_string(i));
			}
			MW->SafeChangePosition_Argumented(0, 0, 0);
			(*MW)["IM"] = new BoolAndWORDChecker(-100., 0., System_White, &(pMCTM->IntermediateInplaceFlag), &(pMCTM->IITrackCount));
			(*MW)["RM"] = new BoolAndWORDChecker(100., 0., System_White, &(pMCTM->IntermediateRegularFlag), &(pMCTM->IRTrackCount));
			thread ILO([](MIDICollectionThreadedMerger *pMCTM, SAFCData *SD, MoveableWindow *MW) {
				while (!pMCTM->CheckRIMerge()) {
					Sleep(100);
				}
				cout << "RI: Out from sleep!\n";
				MW->DeleteUIElementByName("IM");
				MW->DeleteUIElementByName("RM");
				MW->SafeChangePosition_Argumented(0, 0, 0);
				(*MW)["FM"] = new BoolAndWORDChecker(0., 0., System_White, &(pMCTM->CompleteFlag), NULL);
			}, pMCTM, SD, MW);
			ILO.detach();
		}, GlobalMCTM, &_Data, MW);
		LO.detach();

		thread FLO([](MIDICollectionThreadedMerger *pMCTM, MoveableWindow *MW) {
			while (!pMCTM->CompleteFlag) {
				Sleep(100);
			}
			cout << "F: Out from sleep!!!\n";
			MW->DeleteUIElementByName("FM");

			WH->DisableWindow(WH->MainWindow_ID);
			WH->MainWindow_ID = "MAIN";
			//WH->DisableAllWindows();
			WH->EnableWindow("MAIN");
			//pMCTM->ResetEverything();
		},GlobalMCTM,MW);
		FLO.detach();

	}
	//ThrowAlert_Error("It's not done yet :p");
}

void OnSaveTo() {
	_Data.SaveDirectory = SOFD(L"Save final midi to...");
	size_t Pos = _Data.SaveDirectory.rfind(L".mid");
	if (Pos >= _Data.SaveDirectory.size() || Pos <= _Data.SaveDirectory.size() - 4) {
		_Data.SaveDirectory += L".mid";
	}
}

void RestoreRegSettings() {
	bool Opened = false;
	try{
		Settings::RK_Access.Create(HKEY_CURRENT_USER, RegPath);
	}
	catch(...){ cout << "Exception thrown while creating registry key\n"; }
	try {
		Settings::RK_Access.Open(HKEY_CURRENT_USER, RegPath);
		Opened = true;
	}
	catch (...) { cout << "Exception thrown while opening RK\n"; }
	if (Opened) {
		try {
			Settings::ShaderMode = Settings::RK_Access.GetDwordValue(L"AS_SHADERMODE");
		}
		catch (...) { cout << "Exception thrown while restoring AS_SHADERMODE from registry\n"; }
		try {
			DWORD B = Settings::RK_Access.GetDwordValue(L"AS_SWW");
			Settings::SinewaveWidth = *(float*)&B;
		}
		catch (...) { cout << "Exception thrown while restoring AS_SWW from registry\n"; }
		try {
			DWORD B = Settings::RK_Access.GetDwordValue(L"AS_BW");
			Settings::Basewave = *(float*)&B;
		}
		catch (...) { cout << "Exception thrown while restoring AS_BW from registry\n"; }
		try {
			DWORD B = Settings::RK_Access.GetDwordValue(L"AS_P3");
			Settings::Param3 = *(float*)&B;
		}
		catch (...) { cout << "Exception thrown while restoring AS_P3 from registry\n"; }
		try {
			_Data.DetectedThreads = Settings::RK_Access.GetDwordValue(L"AS_THREADS_COUNT");
		}
		catch (...) { cout << "Exception thrown while restoring AS_THREADS_COUNT from registry\n"; }
		try {
			DefaultBoolSettings = Settings::RK_Access.GetDwordValue(L"DEFAULT_BOOL_SETTINGS");
		}
		catch (...) { cout << "Exception thrown while restoring AS_INPLACE_FLAG from registry\n"; }
		try {
			_Data.InplaceMergeFlag = Settings::RK_Access.GetDwordValue(L"AS_INPLACE_FLAG");
		}
		catch (...) { cout << "Exception thrown while restoring INPLACE_MERGE from registry\n"; }
		try {
			wstring ws = Settings::RK_Access.GetStringValue(L"COLLAPSEDFONTNAME");//COLLAPSEDFONTNAME
			FONTNAME = string(ws.begin(), ws.end());
		}
		catch (...) { cout << "Exception thrown while restoring COLLAPSEDFONTNAME from registry\n"; }
		try {
			lFontSymbolsInfo::Size = Settings::RK_Access.GetDwordValue(L"FONTSIZE");//COLLAPSEDFONTNAME
		}
		catch (...) { cout << "Exception thrown while restoring FONTSIZE from registry\n"; }
		try {
			DWORD B = Settings::RK_Access.GetDwordValue(L"FLOAT_FONTHTW");//COLLAPSEDFONTNAME
			lFONT_HEIGHT_TO_WIDTH = *(float*)&B;
		}
		catch (...) { cout << "Exception thrown while restoring FLOAT_FONTHTW from registry\n"; }
		Settings::RK_Access.Close();
	}
}

void Init() {///SetIsFontedVar
	RestoreRegSettings();
	hDc = GetDC(hWnd);
	_Data.DetectedThreads = min((int)thread::hardware_concurrency() - 1,(int)(ceil(GetAvailableMemory() / 2048.)));

	MoveableWindow *T = new MoveableWindow("Main window", System_White, -200, 200, 400, 400, 0x3F3F3FAF, 0x7F7F7F7F);
	SelectablePropertedList *SPL = new SelectablePropertedList(BS_List_Black_Small, NULL, PropsAndSets::OGPInMIDIList, -50, 172, 300, 12, 65, 30);
	Button *Butt;
	(*T)["List"] = SPL;

	(*T)["ADD_Butt"] = new Button("Add MIDIs", System_White, OnAdd, 150, 172.5, 75, 12, 1, 0x00003FAF, 0xFFFFFFFF, 0x00003FFF, 0xFFFFFFFF, 0x7F7F7F7FF,NULL," ");
	(*T)["REM_Butt"] = new Button("Remove selected", System_White, OnRem, 150, 160, 75, 12, 1, 0x3F0000AF, 0xFFFFFFFF, 0x3F0000FF, 0xFFFFFFFF, 0x7F7F7F7FF, NULL, " ");
	(*T)["REM_ALL_Butt"] = new Button("Remove all", System_White, OnRemAll, 150, 147.5, 75, 12, 1, 0xAF0000AF, 0xFFFFFFFF, 0xAF0000AF, 0xFFFFFFFF, 0x7F7F7F7FF, System_White, "May cause lag");
	(*T)["GLOBAL_PPQN_Butt"] = new Button("Global PPQN", System_White, OnGlobalPPQN, 150, 122.5, 75, 12, 1, 0xFF3F00AF, 0xFFFFFFFF, 0xFF3F00AF, 0xFFFFFFFF, 0x7F7F7F7FF, NULL, " ");
	(*T)["GLOBAL_OFFSET_Butt"] = new Button("Global offset", System_White, OnGlobalOffset, 150, 110, 75, 12, 1, 0xFF7F00AF, 0xFFFFFFFF, 0xFF7F00FF, 0xFFFFFFFF, 0x7F7F7F7FF, NULL, " ");
	(*T)["GLOBAL_TEMPO_Butt"] = new Button("Global tempo", System_White, OnGlobalTempo, 150, 97.5, 75, 12, 1, 0xFFAF00AF, 0xFFFFFFFF, 0xFFAF00AF, 0xFFFFFFFF, 0x7F7F7F7FF, NULL, " ");
	//(*T)["_FRESOLVE"] = new Button("_ForceResolve", System_White, _OnResolve, 150, 0, 75, 12, 1, 0x7F007F3F, 0xFFFFFF3F, 0x000000FF, 0xFFFFFF3F, 0x7F7F7F73F, NULL, " ");
	(*T)["DELETE_ALL_VM"] = new Button("Remove vol. maps", System_White, OnRemVolMaps, 150, 72.5, 75, 12, 1,
		0x7F7F7FAF, 0xFFFFFFFF, 0x7F7F7FAF, 0xFFFFFFFF, 0x7F7F7F7FF, NULL, " ");//0xFF007FAF
	(*T)["DELETE_ALL_CAT"] = new Button("Remove C&Ts", System_White, OnRemCATs, 150, 60, 75, 12, 1, 
		0x7F7F7FAF, 0xFFFFFFFF, 0x7F7F7FAF, 0xFFFFFFFF, 0x7F7F7F7FF, NULL, " ");
	(*T)["DELETE_ALL_PITCHES"] = new Button("Remove p. maps", System_White, OnRemPitchMaps, 150, 47.5, 75, 12, 1, 
		0x7F7F7FAF, 0xFFFFFFFF, 0x7F7F7FAF, 0xFFFFFFFF, 0x7F7F7F7FF, NULL, " ");
	(*T)["DELETE_ALL_MODULES"] = new Button("Remove modules", System_White, OnRemAllModules, 150, 35, 75, 12, 1, 
		0x7F7F7FAF, 0xFFFFFFFF, 0x7F7F7FAF, 0xFFFFFFFF, 0x7F7F7F7FF, NULL, " ");

	(*T)["SETTINGS"] = new Button("Settings...", System_White, Settings::OnSettings, 150, -140, 75, 12, 1,
		0x5F5F5FAF, 0xFFFFFFFF, 0x5F5F5FAF, 0xFFFFFFFF, 0x7F7F7F7FF, NULL, " ");
	(*T)["SAVE_AS"] = new Button("Save as...", System_White, OnSaveTo, 150, -152.5, 75, 12, 1,
		0x3FAF00AF, 0xFFFFFFFF, 0x3FAF00AF, 0xFFFFFFFF, 0x7F7F7F7FF, NULL, " ");
	(*T)["START"] = Butt = new Button("Start merging", System_White, OnStart, 150, -177.5, 75, 12, 1,
		0x000000AF, 0xFFFFFFFF, 0x000000AF, 0xFFFFFFFF, 0x7F7F7F7FF, NULL, " ");//177.5

	(*WH)["MAIN"] = T;

	T = new MoveableWindow("Props. and sets.", System_White, -100, 100, 200, 200, 0x3F3F3FCF, 0x7F7F7F7F);
	(*T)["FileName"] = new TextBox("_", System_White, 0, 88.5 - WindowHeapSize, 6, 200 - 1.5*WindowHeapSize, 7.5, 0, 0, 0, _Align::left, TextBox::VerticalOverflow::cut); 
	(*T)["PPQN"] = new InputField(" ", -90 + WindowHeapSize, 75 - WindowHeapSize, 10, 25, System_White, PropsAndSets::PPQN, 0x007FFFFF, System_White, "PPQN is lesser than 65536.", 5, _Align::center, _Align::left, InputField::Type::NaturalNumbers);
	(*T)["TEMPO"] = new InputField(" ", -45 + WindowHeapSize, 75 - WindowHeapSize, 10, 55, System_White, PropsAndSets::TEMPO, 0x007FFFFF, System_White, "Specific tempo override field", 7, _Align::center, _Align::left, InputField::Type::NaturalNumbers);
	(*T)["OFFSET"] = new InputField(" ", 17.5 + WindowHeapSize, 75 - WindowHeapSize, 10, 60, System_White, PropsAndSets::OFFSET, 0x007FFFFF, System_White, "Offset from begining in ticks", 10, _Align::center, _Align::right, InputField::Type::NaturalNumbers);

	(*T)["BOOL_REM_TRCKS"] = new CheckBox(-97.5 + WindowHeapSize, 55 - WindowHeapSize, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 1, System_White, _Align::left, "Remove empty tracks");
	(*T)["BOOL_REM_REM"] = new CheckBox(-82.5 + WindowHeapSize, 55 - WindowHeapSize, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 1, System_White, _Align::left, "Remove merge \"remnants\"");
	(*T)["BOOL_PIANO_ONLY"] = new CheckBox(-67.5 + WindowHeapSize, 55 - WindowHeapSize, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 1, System_White, _Align::left, "Replace all instruments with piano");
	(*T)["BOOL_IGN_TEMPO"] = new CheckBox(-52.5 + WindowHeapSize, 55 - WindowHeapSize, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 0, System_White, _Align::left, "Ignore tempo events");
	(*T)["BOOL_IGN_PITCH"] = new CheckBox(-37.5 + WindowHeapSize, 55 - WindowHeapSize, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 0, System_White, _Align::center, "Ignore pitch bending events");
	(*T)["BOOL_IGN_NOTES"] = new CheckBox(-22.5 + WindowHeapSize, 55 - WindowHeapSize, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 0, System_White, _Align::center, "Ignore note events");
	(*T)["BOOL_IGN_ALL_EX_TPS"] = new CheckBox(-7.5 + WindowHeapSize, 55 - WindowHeapSize, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 0, System_White, _Align::center, "Ignore everything except specified");
	(*T)["BOOL_APPLY_TO_ALL_MIDIS"] = Butt = new Button("A2A", System_White, PropsAndSets::OnApplyBS2A, 80 - WindowHeapSize, 55 - WindowHeapSize, 15, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F003F, 0xFF7F00FF, System_White, "Sets \"bool settings\" to all midis");
	Butt->Tip->SafeChangePosition_Argumented(_Align::right, 87.5 - WindowHeapSize, Butt->Tip->CYpos);

	(*T)["INPLACE_MERGE"] = new CheckBox(97.5 - WindowHeapSize, 55 - WindowHeapSize, 10, 0x007FFFFF, 0xFF3F007F, 0x3FFF007F, 1, 0, System_White, _Align::right, "Enables/disables inplace merge");

	(*T)["GROUPID"] = new InputField(" ", 92.5 - WindowHeapSize, 75 - WindowHeapSize, 10, 20, System_White, PropsAndSets::PPQN, 0x007FFFFF, System_White, "Group ID...", 2, _Align::center, _Align::right, InputField::Type::NaturalNumbers);

	//(*T)["OR"] = Butt = new Button("OR", System_White, PropsAndSets::OR, 37.5, 15 - WindowHeapSize, 20, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F003F, 0xFF7F00FF, System_White, "Overlaps remover");
	//(*T)["SR"] = new Button("SR", System_White, PropsAndSets::SR, 12.5, 15 - WindowHeapSize, 20, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F003F, 0xFF7F00FF, System_White, "Sustains remover");

	(*T)["MIDIINFO"] = Butt = new Button("Collect info", System_White, PropsAndSets::InitializeCollecting, 20, 15 - WindowHeapSize, 65, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F003F, 0xFF7F00FF, System_White, "Collects additional info about the midi");
	Butt->Tip->SafeMove(-20, 0);

	(*T)["APPLY"] = new Button("Apply", System_White, PropsAndSets::OnApplySettings, 87.5 - WindowHeapSize, 15 - WindowHeapSize, 30, 10, 1, 0x7FAFFF3F, 0xFFFFFFFF, 0xFFAF7FFF, 0xFFAF7F3F, 0xFFAF7FFF, NULL, " ");

	(*T)["CUT_AND_TRANSPOSE"] = (Butt = new Button("Cut & Transpose...", System_White, PropsAndSets::CutAndTranspose::OnCaT, 52.5 - WindowHeapSize, 35 - WindowHeapSize, 100, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F003F, 0xFF7F00FF, System_White, "Cut and Transpose tool"));
	Butt->Tip->SafeChangePosition_Argumented(_Align::right, 100 - WindowHeapSize, Butt->Tip->CYpos);
	(*T)["PITCH_MAP"] = (Butt = new Button("Pitch map ...", System_White, PropsAndSets::OnPitchMap, -37.5 - WindowHeapSize, 15 - WindowHeapSize, 70, 10, 1, 0x7F7F7F3F, 0x7F7F7FFF, 0xFFFFFFFF, 0xFFFFFF3F, 0xFFFFFFFF, System_White, "Allows to transform pitches"));
	Butt->Tip->SafeChangePosition_Argumented(_Align::right, 100 - WindowHeapSize, Butt->Tip->CYpos);
	(*T)["VOLUME_MAP"] = (Butt = new Button("Volume map ...", System_White, PropsAndSets::VolumeMap::OnVolMap, -37.5 - WindowHeapSize, 35 - WindowHeapSize, 70, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F003F, 0xFF7F00FF, System_White, "Allows to transform volumes of notes"));
	Butt->Tip->SafeChangePosition_Argumented(_Align::right, 100 - WindowHeapSize, Butt->Tip->CYpos);

	(*T)["SELECT_START"] = new InputField(" ", -37.5 - WindowHeapSize, -5 - WindowHeapSize, 10, 70, System_White, NULL, 0x007FFFFF, System_White, "Selection start", 13, _Align::center, _Align::right, InputField::Type::NaturalNumbers);
	(*T)["SELECT_LENGTH"] = new InputField(" ", 37.5 - WindowHeapSize, -5 - WindowHeapSize, 10, 70, System_White, NULL, 0x007FFFFF, System_White, "Selection length", 14, _Align::center, _Align::right, InputField::Type::WholeNumbers);

	(*T)["CONSTANT_PROPS"] = new TextBox("_Props text example_", System_White, 0, -57.5 - WindowHeapSize, 80 - WindowHeapSize, 200 - 1.5*WindowHeapSize, 7.5, 0, 0, 1);

	(*WH)["SMPAS"] = T;//Selected midi properties and settings

	T = new MoveableWindow("Cut and Transpose.", System_White, -200, 50, 400, 100, 0x3F3F3FCF, 0x7F7F7F7F);
	(*T)["CAT_ITSELF"] = new CAT_Piano(0, 25-WindowHeapSize, 1, 10, NULL);
	(*T)["CAT_SET_DEFAULT"] = new Button("Reset", System_White, PropsAndSets::CutAndTranspose::OnReset, -150, -10 - WindowHeapSize, 40, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F003F, 0xFF7F00FF, NULL, " ");
	(*T)["CAT_+128"] = new Button("0-127 -> 128-255", System_White, PropsAndSets::CutAndTranspose::On0_127to128_255, -85, -10 - WindowHeapSize, 80, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F003F, 0xFF7F00FF, NULL, " ");
	(*T)["CAT_CDT128"] = new Button("Cut down to 128", System_White, PropsAndSets::CutAndTranspose::OnCDT128, 0, -10 - WindowHeapSize, 80, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F003F, 0xFF7F00FF, NULL, " ");
	(*T)["CAT_COPY"] = new Button("Copy", System_White, PropsAndSets::CutAndTranspose::OnCopy, 65, -10 - WindowHeapSize, 40, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F003F, 0xFF7F00FF, NULL, " ");
	(*T)["CAT_PASTE"] = new Button("Paste", System_White, PropsAndSets::CutAndTranspose::OnPaste, 110, -10 - WindowHeapSize, 40, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F003F, 0xFF7F00FF, NULL, " ");
	(*T)["CAT_DELETE"] = new Button("Delete", System_White, PropsAndSets::CutAndTranspose::OnDelete, 155, -10 - WindowHeapSize, 40, 10, 1, 0xFF00FF3F, 0xFF00FFFF, 0xFFFFFFFF, 0xFF003F3F, 0xFF003FFF, NULL, " ");

	(*WH)["CAT"] = T;

	T = new MoveableWindow("Volume map.", System_White, -150, 150, 300, 350, 0x3F3F3FCF, 0x7F7F7F7F,0x7F6F8FCF);
	(*T)["VM_PLC"] = new PLC_VolumeWorker(0, 0 - WindowHeapSize, 300 - WindowHeapSize * 2, 300 - WindowHeapSize * 2, new PLC<BYTE, BYTE>());///todo: interface
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

	T = new MoveableWindow("App settings", System_White, -100, 100, 200, 220, 0x3F3F3FCF, 0x7F7F7F7F);

	(*T)["AS_SHADERMODE"] = new InputField(to_string(Settings::ShaderMode), -87.5 + WindowHeapSize, 85 - WindowHeapSize, 10, 30, System_White, NULL, 0x007FFFFF, System_White, "Shader ID", 2, _Align::center, _Align::left, InputField::Type::NaturalNumbers);
	(*T)["AS_SWW"] = new InputField(to_string(Settings::SinewaveWidth), -47.5 + WindowHeapSize, 85 - WindowHeapSize, 10, 40, System_White, NULL, 0x007FFFFF, System_White, "Wave \"height\"", 8, _Align::center, _Align::left, InputField::Type::FP_Any);
	(*T)["AS_BW"] = new InputField(to_string(Settings::Basewave), -2.5 + WindowHeapSize, 85 - WindowHeapSize, 10, 40, System_White, NULL, 0x007FFFFF, System_White, "Base level", 8, _Align::center, _Align::center, InputField::Type::FP_Any);
	(*T)["AS_P3"] = new InputField(to_string(Settings::Basewave), 42.5 + WindowHeapSize, 85 - WindowHeapSize, 10, 40, System_White, NULL, 0x007FFFFF, System_White, "3rd parameter", 8, _Align::center, _Align::right, InputField::Type::FP_Any);
	(*T)["AS_SHADERWARNING"] = new TextBox("Shader settings", System_White, 0, 85 - WindowHeapSize, 30, 200, 12, 0xFF7F001F, 0xFF7F007F, 1, _Align::center);
	(*T)["AS_APPLY"] = Butt = new Button("Apply", System_White, Settings::OnSetApply, 85 - WindowHeapSize, -87.5 - WindowHeapSize, 40, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, NULL, "_");
	(*T)["AS_EN_FONT"] = Butt = new Button((is_fonted)?"Disable fonts":"Enable fonts", System_White, Settings::ChangeIsFontedVar, 72.5 - WindowHeapSize, -67.5 - WindowHeapSize, 65, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0x007FFFFF, 0xFFFFFFFF, System_White, " ");
	(*T)["AS_ROT_ANGLE"] = new InputField(to_string(ROT_ANGLE), -87.5 + WindowHeapSize, 55 - WindowHeapSize, 10, 30, System_White, NULL, 0x007FFFFF, System_White, "Rotation angle", 6, _Align::center, _Align::left, InputField::Type::FP_Any);
	(*T)["AS_THREADS_COUNT"] = new InputField(to_string(_Data.DetectedThreads), -57.5 + WindowHeapSize, 55 - WindowHeapSize, 10, 20, System_White, NULL, 0x007FFFFF, System_White, "Threads count", 2, _Align::center, _Align::left, InputField::Type::NaturalNumbers);
	(*T)["AS_FONT_SIZE"] = new WheelVariableChanger(Settings::ApplyFSWheel, -37.5, -82.5, lFontSymbolsInfo::Size, 1, System_White, "Font size", "Delta", WheelVariableChanger::Type::addictable);
	(*T)["AS_FONT_P"] = new WheelVariableChanger(Settings::ApplyRelWheel, -37.5, -22.5, lFONT_HEIGHT_TO_WIDTH, 0.01, System_White, "Font rel.", "Delta", WheelVariableChanger::Type::addictable);
	(*T)["AS_FONT_NAME"] = new InputField(FONTNAME, 52.5 - WindowHeapSize, 55 - WindowHeapSize, 10, 100, _STLS_WhiteSmall, &FONTNAME, 0x007FFFFF, System_White, "Font name", 32, _Align::center, _Align::left, InputField::Type::Text);

	(*T)["BOOL_REM_TRCKS"] = new CheckBox(-97.5 + WindowHeapSize, 35 - WindowHeapSize, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 1, System_White, _Align::left, "DV: Remove empty tracks");
	(*T)["BOOL_REM_REM"] = new CheckBox(-82.5 + WindowHeapSize, 35 - WindowHeapSize, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 1, System_White, _Align::left, "DV: Remove merge \"remnants\"");
	(*T)["BOOL_PIANO_ONLY"] = new CheckBox(-67.5 + WindowHeapSize, 35 - WindowHeapSize, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 1, System_White, _Align::left, "DV: All instuments to piano");
	(*T)["BOOL_IGN_TEMPO"] = new CheckBox(-52.5 + WindowHeapSize, 35 - WindowHeapSize, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 0, System_White, _Align::left, "DV: Ignore tempo events");
	(*T)["BOOL_IGN_PITCH"] = new CheckBox(-37.5 + WindowHeapSize, 35 - WindowHeapSize, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 0, System_White, _Align::center, "DV: Ignore pitch bending events");
	(*T)["BOOL_IGN_NOTES"] = new CheckBox(-22.5 + WindowHeapSize, 35 - WindowHeapSize, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 0, System_White, _Align::center, "DV: Ignore note events");
	(*T)["BOOL_IGN_ALL_EX_TPS"] = new CheckBox(-7.5 + WindowHeapSize, 35 - WindowHeapSize, 10, 0x007FFFFF, 0xFF00007F, 0x00FF007F, 1, 0, System_White, _Align::center, "DV: Ignore everything except specified");

	(*T)["BOOL_APPLY_TO_ALL_MIDIS"] = Butt = new Button("A2A", System_White, Settings::ApplyToAll, 80 - WindowHeapSize, 35 - WindowHeapSize, 15, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFFFFFFFF, 0xFF7F003F, 0xFF7F00FF, System_White, "The same as A2A in MIDI's props.");
	Butt->Tip->SafeChangePosition_Argumented(_Align::right, 87.5 - WindowHeapSize, Butt->Tip->CYpos);

	(*T)["INPLACE_MERGE"] = new CheckBox(97.5 - WindowHeapSize, 35 - WindowHeapSize, 10, 0x007FFFFF, 0xFF3F007F, 0x3FFF007F, 1, 0, System_White, _Align::right, "DV: Enable/disable inplace merge");

	(*WH)["APP_SETTINGS"] = T;

	T = new MoveableWindow("SMRP Container", System_White, -300, 300, 600, 600, 0x000000CF, 0xFFFFFF7F);
	
	(*WH)["SMRP_CONTAINER"] = T;

	T = new MoveableWindow("MIDI Info Collector", System_Black, -150, 200, 300, 400, 0xFFFFFFCF, 0x7F7F7F7F);
	(*T)["FLL"] = new TextBox("--File log line--", System_Black, 0, -WindowHeapSize + 185, 15, 285, 10, 0, 0, 0, _Align::left);
	(*T)["FEL"] = new TextBox("--File error line--", System_Red, 0, -WindowHeapSize + 175, 15, 285, 10, 0, 0, 0, _Align::left);
	(*T)["TEMPO_GRAPH"] = new Graphing<SingleMIDIInfoCollector::tempo_graph>(
		0, -WindowHeapSize + 145, 285, 50, (1. / 20000.), true, 0x007FFFFF, 0x000000FF, 0xFF7F00FF, 0xFF00FFFF, 0x7F7F7F7F, nullptr, System_Black, false
	);
	(*T)["POLY_GRAPH"] = new Graphing<SingleMIDIInfoCollector::polyphony_graph>(
		0, -WindowHeapSize + 95, 285, 50, (1. / 20000.), true, 0x007FFFFF, 0x000000FF, 0xFF7F00FF, 0xFF00FFFF, 0x7F7F7F7F, nullptr, System_Black, false
	);
	(*T)["PG_SWITCH"] = new Button("Enable graph B", System_Black, PropsAndSets::SMIC::EnablePG, 37.5, 60 - WindowHeapSize, 70, 10, 1, 0xAFAFAFAF, 0x000000FF, 0xFFFFFFFF, 0x000000FF, 0x007FFFFF, nullptr);
	(*T)["TG_SWITCH"] = new Button("Enable graph A", System_Black, PropsAndSets::SMIC::EnableTG, -37.5, 60 - WindowHeapSize, 70, 10, 1, 0xAFAFAFAF, 0x000000FF, 0xFFFFFFFF, 0x000000FF, 0x007FFFFF, nullptr);
	(*T)["PG_EXP"] = new Button("Export B .csv", System_Black, PropsAndSets::SMIC::ExportPG, 110, 60 - WindowHeapSize, 65, 10, 1, 0xAFAFAFAF, 0x000000FF, 0xFFFFFFFF, 0x000000FF, 0x007FFFFF, nullptr);
	(*T)["TG_EXP"] = new Button("Export A .csv", System_Black, PropsAndSets::SMIC::ExportTG, -110, 60 - WindowHeapSize, 65, 10, 1, 0xAFAFAFAF, 0x000000FF, 0xFFFFFFFF, 0x000000FF, 0x007FFFFF, nullptr);
	(*T)["TG_RESET"] = new Button("Reset graph A", System_Black, PropsAndSets::SMIC::ResetTG, -110, 40 - WindowHeapSize, 65, 10, 1, 0xAFAFAFAF, 0x000000FF, 0xFFFFFFFF, 0x000000FF, 0x007FFFFF, nullptr);
	(*T)["PG_RESET"] = new Button("Reset graph B", System_Black, PropsAndSets::SMIC::ResetPG, -37.5, 40 - WindowHeapSize, 65, 10, 1, 0xAFAFAFAF, 0x000000FF, 0xFFFFFFFF, 0x000000FF, 0x007FFFFF, nullptr);
	(*T)["TOTAL_INFO"] = new TextBox("----", System_Black, 0, -150, 35, 285, 10, 0, 0, 0, _Align::left);
	(*T)["INT_MIN"] = new InputField("0", -132.5, 40 - WindowHeapSize, 10, 20, System_Black, NULL, 0x000000FF, System_Black, "Minutes", 3, _Align::center, _Align::left, InputField::Type::NaturalNumbers);
	(*T)["INT_SEC"] = new InputField("0", -107.5, 40 - WindowHeapSize, 10, 20, System_Black, NULL, 0x000000FF, System_Black, "Seconds", 2, _Align::center, _Align::left, InputField::Type::NaturalNumbers);
	(*T)["INT_MSC"] = new InputField("000", -80, 40 - WindowHeapSize, 10, 25, System_Black, NULL, 0x000000FF, System_Black, "Milliseconds", 3, _Align::center, _Align::left, InputField::Type::NaturalNumbers);
	(*T)["INTEGRATE_TICKS"] = new Button("Integrate ticks", System_Black, PropsAndSets::SMIC::IntegrateTime, -25, 40 - WindowHeapSize, 70, 10, 1, 0xAFAFAFAF, 0x000000FF, 0xFFFFFFFF, 0x000000FF, 0x007FFFFF, System_White, "Result is the closest tick to that time.");
	(*T)["INT_TIC"] = new InputField("0", -105, 20 - WindowHeapSize, 10, 75, System_Black, NULL, 0x000000FF, System_Black, "Ticks", 17, _Align::center, _Align::left, InputField::Type::NaturalNumbers);
	(*T)["INTEGRATE_TIME"] = new Button("Integrate time", System_Black, PropsAndSets::SMIC::DiffirentiateTicks, -25, 20 - WindowHeapSize, 70, 10, 1, 0xAFAFAFAF, 0x000000FF, 0xFFFFFFFF, 0x000000FF, 0x007FFFFF, System_White, "Result is the time of that tick.");
	(*T)["DELIM"] = new InputField(";", 137.5, 40 - WindowHeapSize, 10, 7.5, System_Black, &(PropsAndSets::CSV_DELIM), 0x000000FF, System_Black, "Delimiter", 1, _Align::center, _Align::right, InputField::Type::Text);
	(*T)["ANSWER"] = new TextBox("----", System_Black, -66.25, -30, 25, 152.5, 10, 0, 0, 0, _Align::center, TextBox::VerticalOverflow::recalibrate);

	(*WH)["SMIC"] = T;

	WH->EnableWindow("MAIN");
	//WH->EnableWindow("SMIC");
	//WH->EnableWindow("OR");
	//WH->EnableWindow("SMRP_CONTAINER");
	//WH->EnableWindow("APP_SETTINGS");
	//WH->EnableWindow("VM");
	//WH->EnableWindow("CAT");
	//WH->EnableWindow("SMPAS");//Debug line
	//WH->EnableWindow("PROMPT");////DEBUUUUG

	//ThrowAlert_Warning("aksdlkadskl\nalksd;laksd");

	DragAcceptFiles(hWnd, TRUE);
	OleInitialize(NULL);
	cout << "Registering Drag&Drop: " << (RegisterDragDrop(hWnd, &DNDH_Global)) << endl;
	
	SAFC_VersionCheck();
}


///////////////////////////////////////
/////////////END OF USE////////////////
///////////////////////////////////////

const GLchar *vertex_shader[] = {
	"#version 120\n",
	"void main(void) {\n",//rand(gl_FragCoord.xy + vec2(Time,2.*Time))/2.
	"    vec4 pos = ftransform();\n",
	//"    pos = pos*sin(pos.x-pos.y);\n",
	//"    pos.x += recalc(rand(pos.xy + vec2(Time,2.*Time))*1.,-1,1)/625.;\n",
	//"    pos.y += recalc(rand(pos.xy + vec2(2.*Time,Time))*1.,-1,1)/625.;\n",
	"    gl_Position = pos;\n",
	"    gl_FrontColor = gl_Color;\n",
	"}"
};

const GLchar *fragment_shader[] = { //uniform float time;
	"#version 120\n",
	"uniform float Time = 0.;\n",//
	"uniform float SinewaveWidth = 0.;\n",//
	"uniform float Basewave = 1.;\n",//
	"uniform float Param3 = 1.;\n",//
	"uniform int Mode = 0;\n",//
	"uniform vec2 MousePos;\n",//
	"uniform vec2 resolution;\n",//
	"float rand(vec2 co){\n",
	"    float a = 12.9898;\n",
	"    float b = 78.233;\n",
	"    float c = 43758.5453;\n",
	"    float dt= dot(co.xy ,vec2(a,b));\n",
	"    float sn= mod(dt,3.14);\n",
	"    return fract(sin(sn) * c);\n",
	"}\n",
	"vec3 palette( in float t, in vec3 a, in vec3 b, in vec3 c, in vec3 d ){\n",
	"    return a + b*cos( 6.28318*(c*t+d) );\n",
	"}\n",
	"vec4 spirals(float Time, vec2 p){\n",
	"	float col;\n",
	"        for(float i = 0.25; i < 15.0; i++){\n",
	"		p.x += 0.25 / (i) * sin(i * 4.0 * p.y + Time + cos(Time / (15. * i + SinewaveWidth*tan(i)) * i));\n",
	"     		p.y -= 0.1 / (i)* cos(i * 8.0 * p.x + Time + sin((Time / (30. * i) + SinewaveWidth*atan(i)) * i));\n",
	"     	}\n",
	"     	col = abs(p.y * p.x)/(max(abs(Basewave),0.1)*sign(Basewave)*10.);\n",
	"	return vec4(palette(col, vec3(0.5, 0.5, 0.5), vec3(0.5, 0.5, 0.5), vec3(1.0, 1.0, 1.0), vec3(0.00, 0.10, 0.20)), 1.0) + vec4(0.2,0.1,0.1,0.1);\n",
	"}"
	"vec4 flare(float Time, vec2 p, float y, float param1 , float param2){\n",
	"	p = ( p / resolution.xy ) - y;\n",
	"	float sx = param2 * (p.x + 1.) * sin(cos( 5.0 * p.x - 1. * Time));\n",
	"	float dy = param1 / ( 250. * abs(p.y - sx));\n",
	"	dy += 1./ (60. * length(p - vec2(p.x, 0.))/param1);\n",
	"	p.x = (p.x + 0.24) * dy;\n",
	"	return vec4( p.x*p.x, dy*0.3, dy, 1.0 );\n",
	"}",
	"vec4 flare2(float Time, vec2 p, float y, float param1 , float param2, float source_alpha){\n",
	"	vec2 sp = (p.xy * 2.0 - resolution.xy) / min(resolution.x, resolution.y);\n",
	"	sp.y = -dot(sp,sp);\n",
	"	float color = param2;\n",
	"	for (int i = 0; i < 20; i++) {\n",
	"		float t = float(i)+sin(Time*param1*0.1+float(i));\n",
	"		color += 0.025/distance(sp,vec2(sp.x,cos(t+sp.x)));\n",
	"	}\n",
	"	return color*vec4(vec3((sp.x)*0.1, 0.05, -(sp.x)*0.1), 1.0-source_alpha);"
	"}",
	"void main() {\n",
	"	 vec4 Color; float Transp = gl_Color[3];\n",
	"    //if (Mode==1)Color = pow(gl_Color,vec4(Basewave + rand(gl_FragCoord.xy + vec2(Time,2.*Time))*SinewaveWidth));\n",
	"    //else if(Mode==2)Color = pow(gl_Color,normalize(vec4(gl_FragCoord.xyz,Param3)));\n",
	"    //else if(Mode==3)Color = pow(gl_Color,vec4(sin(Time*4. + gl_FragCoord.y/200.)*SinewaveWidth + Basewave + SinewaveWidth));\n",
	//"    else if(Mode==4)Color = (SinewaveWidth + 1.)*gl_Color - (Basewave)*spirals(Time, gl_FragCoord.xy / 1600.);\n",
	"    //else if(Mode==4)Color = pow(gl_Color,spirals(Time, gl_FragCoord.xy / (resolution.x * Param3 * 3.)));\n",
	"    //else if(Mode==5)Color = pow(flare(Time, gl_FragCoord.xy, MousePos.y/resolution.y*1.8*Param3 + 0.5, Basewave, SinewaveWidth+0.15),vec4(1.)-gl_Color);\n",
	"    //else if(Mode==6)Color = gl_Color * (Param3 * 20.) * flare2(Time, gl_FragCoord.xy, MousePos.y/resolution.y*1.8*Param3 + 0.5, Basewave + 1., SinewaveWidth+0.15,Transp);\n",
	"	 //else\n",
	"	     Color = gl_Color;\n",
	"	 Color.w = Transp;\n",
	"    gl_FragColor = Color;\n",
	"}"
};

shader_prog *SP;

void onTimer(int v);
void mDisplay() {
	if(!(TimerV&0xF))
		lFontSymbolsInfo::InitialiseFont(FONTNAME);
	glClear(GL_COLOR_BUFFER_BIT | GL_ACCUM_BUFFER_BIT);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	if (FIRSTBOOT) {
		FIRSTBOOT = 0;

		//Settings::ShaderMode = rand() & 3;
		if (glCreateShader) {
			SP = new shader_prog(vertex_shader, fragment_shader);
			cout << "GLEW is working! Yeee ( -3-)\n";
		}
		else 
			cout << "GLEW is not initialised ( o.o)\n";

		WH = new WindowsHandler();
		Init();
		//WH->ThrowAlert("kokoko", "alerttest", SpecialSigns::DrawWait, 1, 0xFF00FF7F, 20);//
		if (APRIL_FOOL) {
			WH->ThrowAlert("Today is a special day! ( -w-)\nToday you'll have new background\n(-w- )", "1st of April!", SpecialSigns::DrawWait, 1, 0xFF00FFFF, 20);
			(*WH)["ALERT"]->RGBABackground = 0; 
			_WH_t("ALERT", "AlertText", TextBox*)->SafeTextColorChange(0xFFFFFFFF);
		}
		ANIMATION_IS_ACTIVE = !ANIMATION_IS_ACTIVE;
		onTimer(0);
	}
	if (SP) {
		glUniform1f(glGetUniformLocation(SP->prog, "Time"), TimerV / 100.f);
		glUniform1i(glGetUniformLocation(SP->prog, "Mode"), Settings::ShaderMode);
		glUniform1f(glGetUniformLocation(SP->prog, "SinewaveWidth"), Settings::SinewaveWidth);
		glUniform1f(glGetUniformLocation(SP->prog, "Basewave"), Settings::Basewave);
		glUniform1f(glGetUniformLocation(SP->prog, "Param3"), Settings::Param3);
		glUniform2f(glGetUniformLocation(SP->prog, "MousePos"), MXPOS, MYPOS);
		glUniform2f(glGetUniformLocation(SP->prog, "resolution"), WindX, WindY);
		SP->use(); 
	}
	if (APRIL_FOOL) {
		Settings::ShaderMode = 1;
		Settings::Basewave = 3.5f;
		glBegin(GL_QUADS);
		glColor4f(1, 0, 1, (DRAG_OVER) ? 0.25f : 1);
		glVertex2f(0 - RANGE * (WindX / WINDXSIZE), 0 - RANGE * (WindY / WINDYSIZE));
		glColor4f(0, 1, 0, (DRAG_OVER) ? 0.25f : 1);
		glVertex2f(0 - RANGE * (WindX / WINDXSIZE), RANGE * (WindY / WINDYSIZE));
		glColor4f(1, 0.5f, 0, (DRAG_OVER) ? 0.25f : 1);
		glVertex2f(RANGE * (WindX / WINDXSIZE), RANGE * (WindY / WINDYSIZE));
		glColor4f(0, 0.5f, 1, (DRAG_OVER) ? 0.25f : 1);
		glVertex2f(RANGE * (WindX / WINDXSIZE), 0 - RANGE * (WindY / WINDYSIZE));
		glEnd();
	}
	else if (Settings::ShaderMode < 4) {
		glBegin(GL_QUADS);
		glColor4f(1, 0.5f, 0, (DRAG_OVER) ? 0.25f : 1);
		glVertex2f(0 - RANGE * (WindX / WINDXSIZE), 0 - RANGE * (WindY / WINDYSIZE));
		glVertex2f(0 - RANGE * (WindX / WINDXSIZE), RANGE * (WindY / WINDYSIZE));
		glColor4f(0, 0.5f, 1, (DRAG_OVER) ? 0.25f : 1);
		glVertex2f(RANGE * (WindX / WINDXSIZE), RANGE * (WindY / WINDYSIZE));
		glVertex2f(RANGE * (WindX / WINDXSIZE), 0 - RANGE * (WindY / WINDYSIZE));
		glEnd();
	}
	else {
		glBegin(GL_QUADS);
		glColor4f(0.25f, 0.25f, 0.25f, (DRAG_OVER) ? 0.25f : 1);
		glVertex2f(0 - RANGE * (WindX / WINDXSIZE), 0 - RANGE * (WindY / WINDYSIZE));
		glVertex2f(0 - RANGE * (WindX / WINDXSIZE), RANGE * (WindY / WINDYSIZE));
		glVertex2f(RANGE * (WindX / WINDXSIZE), RANGE * (WindY / WINDYSIZE));
		glVertex2f(RANGE * (WindX / WINDXSIZE), 0 - RANGE * (WindY / WINDYSIZE));
		glEnd();
	}
	//ROT_ANGLE += 0.02;

	glRotatef(ROT_ANGLE, 0, 0, 1);
	if (WH)WH->Draw();
	if (DRAG_OVER)SpecialSigns::DrawFileSign(0, 0, 50, 0xFFFFFFCF,0);
	glRotatef(-ROT_ANGLE, 0, 0, 1);

	glutSwapBuffers();
}

void mInit() {
	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();
	gluOrtho2D((0 - RANGE)*(WindX / WINDXSIZE), RANGE*(WindX / WINDXSIZE), (0 - RANGE)*(WindY / WINDYSIZE), RANGE*(WindY / WINDYSIZE));
}

void onTimer(int v) {
	glutTimerFunc(33, onTimer, 0);
	if (ANIMATION_IS_ACTIVE) {
		mDisplay();
		++TimerV;
	}
}

void OnResize(int x, int y) {
	WindX = x;
	WindY = y;
	mInit();
	glViewport(0, 0, x, y);
	if (WH) {
		auto SMRP = (*WH)["SMRP_CONTAINER"];
		SMRP->SafeChangePosition_Argumented(0, 0, 0);
		SMRP->_NotSafeResize_Centered(RANGE * 3 *(WindY / WINDYSIZE) + 2*WindowHeapSize, RANGE * 3 *(WindX / WINDXSIZE));
	}
}
void inline rotate(float& x, float& y) {
	float t = x * cos(ROT_RAD) + y * sin(ROT_RAD);
	y = 0 - x * sin(ROT_RAD) + y * cos(ROT_RAD);
	x = t;
}
void inline absoluteToActualCoords(int ix, int iy, float &x, float &y) {
	float wx = WindX, wy = WindY,t;
	x = ((float)(ix - wx * 0.5)) / (0.5*(wx / (RANGE*(WindX / WINDXSIZE))));
	y = ((float)(0 - iy + wy * 0.5)) / (0.5*(wy / (RANGE*(WindY / WINDYSIZE))));
	rotate(x, y);
}
void mMotion(int ix, int iy) {
	float fx, fy;
	absoluteToActualCoords(ix, iy, fx, fy);
	MXPOS = fx;
	MYPOS = fy;
	if (WH)WH->MouseHandler(fx, fy, 0, 0);
}
void mKey(BYTE k, int x, int y) {
	if (WH)WH->KeyboardHandler(k);

	if (k == '=') { ANIMATION_IS_ACTIVE = !ANIMATION_IS_ACTIVE; }
	else if (k == 27)exit(1);
	else {
		//cout << (int)k << ' ' << k << endl;
	}
}
void mClick(int butt, int state, int x, int y) {
	float fx, fy;
	CHAR Button,State=state;
	absoluteToActualCoords(x, y, fx, fy);
	Button = butt - 1;
	if (state == GLUT_DOWN)State = -1;
	else if (state == GLUT_UP)State = 1;
	if (WH)WH->MouseHandler(fx, fy, Button, State);
}
void mDrag(int x, int y) {
	//mClick(88, MOUSE_DRAG_EVENT, x, y);
	mMotion(x, y);
}
void mSpecialKey(int Key,int x, int y) {
	//cout << "Spec: " << Key << endl;
	if (Key == GLUT_KEY_DOWN) {
		RANGE *= 1.1;
		OnResize(WindX, WindY);
	}
	else if(Key == GLUT_KEY_UP){
		RANGE /= 1.1;
		OnResize(WindX, WindY);
	}
}
void mExit(int a) {
	Settings::RK_Access.Close();
	if (SP)(~(*SP));
}

int main(int argc, char ** argv) {
	_wremove(L"_s");
	_wremove(L"_f");
	_wremove(L"_g");
	ios_base::sync_with_stdio(false);//why not
#ifdef _DEBUG 
	ShowWindow(GetConsoleWindow(), SW_SHOW);
#else // _DEBUG 
	ShowWindow(GetConsoleWindow(), SW_HIDE);
#endif
	ShowWindow(GetConsoleWindow(), SW_SHOW);

	SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
	//srand(1);
	//srand(clock());
	InitASCIIMap();
	//cout << to_string((WORD)0) << endl;

	srand(TIMESEED());
	__glutInitWithExit(&argc, argv, mExit);
	//cout << argv[0] << endl;
	glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA | GLUT_ALPHA);
	glutInitWindowSize(WINDXSIZE, WINDYSIZE);
	//glutInitWindowPosition(50, 0);
	glutCreateWindow(WINDOWTITLE);

	hWnd = FindWindowA(NULL, WINDOWTITLE);

	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);//_MINUS_SRC_ALPHA
	glEnable(GL_BLEND); 
	
	//glEnable(GL_POLYGON_SMOOTH);//laggy af
	glEnable(GL_LINE_SMOOTH);//GL_POLYGON_SMOOTH
	glEnable(GL_POINT_SMOOTH);

	glShadeModel(GL_SMOOTH);

	glHint(GL_LINE_SMOOTH_HINT, GL_NICEST);//GL_FASTEST//GL_NICEST
	glHint(GL_POINT_SMOOTH_HINT, GL_NICEST);
	glHint(GL_POLYGON_SMOOTH_HINT, GL_NICEST);

	glewExperimental = GL_TRUE;
	DWORD glewInitVal;
	if (glewInitVal = glewInit())
		cout << "Something is wrong with glew. Code: " << glewInitVal << endl;

	glutMouseFunc(mClick);
	glutReshapeFunc(OnResize);
	glutSpecialFunc(mSpecialKey);
	glutMotionFunc(mDrag);
	glutPassiveMotionFunc(mMotion);
	glutKeyboardFunc(mKey);
	glutDisplayFunc(mDisplay);
	mInit();
	glutMainLoop();
	return 0;
}