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
#include <dwmapi.h>
//#define WIN32_LEAN_AND_MEAN

#pragma comment (lib, "Version.lib")//Urlmon.lib
#pragma comment (lib, "Urlmon.lib")//Urlmon.lib
#pragma comment (lib, "dwmapi.lib")

#include "glew.h"
#ifndef __X64
	#pragma comment (lib, "glew32.lib")
#else
	#pragma comment (lib, "lib_x64\\glew32.lib")
#endif

#include "glut.h"
#include "freeglut.h"
#ifndef __X64
	#pragma comment (lib, "freeglut.lib")
#else
	#pragma comment (lib, "lib_x64\\freeglut.lib")
#endif

#include "shader_smpclass.h"
//#include "OR.h"
#include "WinReg.h"

#include "consts.h"
#include <stack>
#include "bbb_ffio.h"

#include "JSON/JSON.h"
#include "JSON/JSON.cpp"
#include "JSON/JSONValue.h"
#include "JSON/JSONValue.cpp"

#define NULL nullptr

using namespace std;

typedef unsigned char BYTE;
typedef bool BIT;

#define BEG_RANGE 200
FLOAT RANGE = BEG_RANGE,MXPOS=0.f,MYPOS=0.f;

const char* WINDOWTITLE = "SAFC\0";
wstring RegPath = L"Software\\SAFC\\";
string FONTNAME = "Arial";
BIT is_fonted = 0;

//#define ROT_ANGLE 0.7
#define TRY_CATCH(code,msg) try{code}catch(...){cout<<msg<<endl;}

float ROT_ANGLE = 0.f;
#define ROT_RAD ANGTORAD(ROT_ANGLE)
//#define RANGE 200
#define WINDXSIZE 720
#define WINDYSIZE 720

#define ANGTORAD(a) (0.0174532925f*(a))
#define RANDFLOAT(range)  ((0 - range) + ((float)rand()/((float)RAND_MAX/(2*range))))
#define RANDSGN ((rand()&1)?-1:1)
#define SLOWDPROG(a,b,progressrate) ((a + (fract - 1)*b)/progressrate)

#define GLCOLOR(uINT) glColor4ub(((uINT & 0xFF000000) >> 24), ((uINT & 0xFF0000) >> 16), ((uINT & 0xFF00) >> 8), (uINT & 0xFF))

float WindX = WINDXSIZE, WindY = WINDYSIZE;

BIT ANIMATION_IS_ACTIVE = 0, FIRSTBOOT = 1, DRAG_OVER = 0, 
	APRIL_FOOL = 0;
DWORD TimerV = 0;
HWND hWnd;
HDC hDc;
auto HandCursor = ::LoadCursor(NULL, IDC_HAND), AllDirectCursor = ::LoadCursor(NULL, IDC_CROSS);///AAAAAAAAAAA
//const float singlepixwidth = (float)RANGE / WINDXSIZE;

void absoluteToActualCoords(int ix, int iy, float &x, float &y);
void inline rotate(float& x, float& y);
float CONSTZERO(float x, float y) { return 0.f; }
int TIMESEED() {
	SYSTEMTIME t;
	GetLocalTime(&t);
	if (t.wMonth == 4 && t.wDay == 1)APRIL_FOOL = 1;
	return t.wMilliseconds + (t.wSecond * 1000) + t.wMinute * 60000;
}

void ThrowAlert_Error(string AlertText); 
void AddFiles(vector<wstring> Filenames);
#pragma warning(disable : 4996)

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
		}
		delete[] verData;
	}
	return { 0,0,0,0 };
}

bool SAFC_Update(const wstring& link_to_commit_info) {
	//printf("Url: %S\n", SAFC_tags_link);
	wchar_t current_file_path[MAX_PATH];
	wchar_t update_file[MAX_PATH];
	bool flag = false;
	GetCurrentDirectoryW(MAX_PATH, current_file_path);
	wsprintf(update_file, L"%s\\update.7z", current_file_path);
	wsprintf(current_file_path, L"%s\\file_infos.json", current_file_path);
	//printf("Path: %S\n", current_file_path);
	HRESULT res = URLDownloadToFileW(NULL, link_to_commit_info.c_str(), current_file_path, 0, NULL);
	if (res == S_OK) {
#ifndef __X64
		constexpr wchar_t* archive_name = (wchar_t* const)L"SAFC32.7z";;
#else
		constexpr wchar_t* const archive_name = (wchar_t* const)L"SAFC64.7z";
#endif
		ifstream input(current_file_path);
		string temp_buffer;
		std::getline(input, temp_buffer);
		input.close();
		auto JSON_Value = JSON::Parse(temp_buffer.c_str());
		if (JSON_Value->IsObject()) {
			auto Object = JSON_Value->AsObject();
			auto Files = Object.find(L"sha");
			if (Files != Object.end() && Files->second->IsString()) {
				wstring link = L"https://github.com/DixelU/SAFC/raw/" + Files->second->AsString() + L"/" + archive_name;
				wcout << link << endl;
				printf("Downloading to %s...\n", update_file);
				HRESULT co_res = URLDownloadToFileW(NULL, link.c_str(), update_file, 0, NULL);
				if (co_res == S_OK) {
					flag = true;
					_wrename(L"SAFC.exe", L"_s");
					_wrename(L"freeglut.dll", L"_f");
					_wrename(L"glew32.dll", L"_g");
					system("\"C:\\Program Files\\7-Zip\\7z.exe\" x -y update.7z");
					if (_waccess(L"SAFC.exe", 0) != 0) {
						printf("x64 is not installed\n");
						system("\"C:\\Program Files (x86)\\7-Zip\\7z.exe\" x -y update.7z");
						if (_waccess(L"SAFC.exe", 0) != 0) {
							ThrowAlert_Error("Extract: To perform this operation you must have 7-Zip installed.\n");
							_wrename(L"_s", L"SAFC.exe");
							_wrename(L"_f", L"freeglut.dll");
							_wrename(L"_g", L"glew32.dll");
							flag = false;
						}
					}
					_wremove(L"update.7z");
				}
				else if (co_res == E_OUTOFMEMORY)
					ThrowAlert_Error("Update: Buffer length invalid, or insufficient memory\n");
				else if (co_res == INET_E_DOWNLOAD_FAILURE)
					ThrowAlert_Error("Update: URL is invalid\n");
				else
					ThrowAlert_Error("Update: Other error: " + to_string( co_res));
			}
		}
	}
	else if (res == E_OUTOFMEMORY)
		ThrowAlert_Error("Files: Buffer length invalid, or insufficient memory\n");
	else if (res == INET_E_DOWNLOAD_FAILURE)
		ThrowAlert_Error("Files: URL is invalid\n");
	else
		ThrowAlert_Error("Files: Other error: " + to_string(res));
	_wremove(current_file_path);
	return flag;
}

void SAFC_VersionCheck() {
	thread version_checker([]() {
		bool flag = false;
		_wremove(L"_s");
		_wremove(L"_f");
		_wremove(L"_g");
		constexpr wchar_t* SAFC_tags_link = (wchar_t* const)L"https://api.github.com/repos/DixelU/SAFC/tags";
		//printf("Url: %S\n", SAFC_tags_link);
		wchar_t current_file_path[MAX_PATH];
		GetCurrentDirectoryW(MAX_PATH, current_file_path);
		wsprintf(current_file_path, L"%s\\tags.json", current_file_path);
		//printf("Path: %S\n", current_file_path);
		HRESULT res = URLDownloadToFileW(NULL, SAFC_tags_link, current_file_path, 0, NULL);
		if (res == S_OK) {
			auto [maj, min, ver, build] = ___GetCurFileVersion();
			ifstream input(current_file_path);
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
						std::wcout << L"Git answered with version: " << git_latest_version << endl;
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
								////////UPDATE///////
								auto Commit = FirstElement->AsObject().find(L"commit");
								if (Commit != FirstElement->AsObject().end()) {
									ThrowAlert_Error("Update found! The app will restart soon...\nUpdate: " + string(git_latest_version.begin(), git_latest_version.end()));
									auto Url = Commit->second->AsObject().find(L"url")->second;
									flag = SAFC_Update(Url->AsString());
									if(flag)
										ThrowAlert_Error("SAFC will restart in 3 seconds...");
								}
							}
						}
					}
				}
			}
		}
		else if (res == E_OUTOFMEMORY)
			ThrowAlert_Error("Tags: Buffer length invalid, or insufficient memory\n");
		else if (res == INET_E_DOWNLOAD_FAILURE)
			ThrowAlert_Error("Tags: URL is invalid\n");
		else
			ThrowAlert_Error("Tags: Other error: Other error: " + to_string(res));

		_wremove(current_file_path);

		if (flag) {
			Sleep(3000);
			system("start SAFC.exe");
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

template<typename typekey, typename typevalue>
struct PLC {
	map<typekey, typevalue> ConversionMap;
	PLC(void) { return; }
	PLC(vector<pair<typekey, typevalue>> V) {
		auto Y = V.begin();
		while (Y != V.end()) {
			ConversionMap[*Y.first] = *Y.second;
			++Y;
		}
	}
	void InsertNewPoint(typekey key, typevalue value) {
		if(AskForValue(key)!=value || ConversionMap.empty())
			ConversionMap[key] = value;
	}
	typevalue AskForValue(typekey key) {

		typename map<typekey, typevalue>::iterator L, U;
		if (ConversionMap.empty())return typevalue(key);
		if (ConversionMap.size() == 1)return (*ConversionMap.begin()).second;
		if ((L = ConversionMap.find(key)) != ConversionMap.end()) {
			return L->second;
		}

		L = ConversionMap.lower_bound(key);
		U = ConversionMap.upper_bound(key);

		if (U == ConversionMap.end())
			U--;
		if (L == ConversionMap.end()) {
			L = U;
			if (L != ConversionMap.begin())
				L--;
			else
				U++;
		}
		if (U == L) {
			if (U == ConversionMap.begin())
				U++;
			else L--;
		}
		//           x(y1 - y0) + (x1y0 - x0y1) 
		//  y   =    --------------------------
		//                   (x1 - x0)
		return (key*(U->second - L->second) + (U->first*L->second - L->first*U->second)) / (U->first - L->first);
	}
};

struct BYTE_PLC_Core {
	BYTE *Core;
	BYTE_PLC_Core(PLC<BYTE, BYTE> *PLC_bb) {
		Core = new BYTE[256];
		if(PLC_bb)
			for (int i = 0; i < 256; i++) {
				Core[i] = PLC_bb->AskForValue(i);
			}
		else
			for (int i = 0; i < 256; i++) {
				Core[i] = 0;
			}
	}
	inline BYTE& operator[](BYTE Index) {
		return Core[Index];
	}
}; 

struct _14BIT_PLC_Core {
	WORD *Core;
	const WORD _14BIT = 1 << 14;
	_14BIT_PLC_Core(PLC<WORD, WORD> *PLC_bb) {
		Core = new WORD[_14BIT];
		for (int i = 0; i < (_14BIT); i++) {
			Core[i] = PLC_bb->AskForValue(i);
			if (Core[i] >= _14BIT)
				Core[i] = 0x2000;
		}
	}
	inline WORD& operator[](WORD Index) {
		if (Index < _14BIT)
			return Core[Index];
		else
			return Core[0x2000];
	}
};

struct CutAndTransposeKeys {
	BYTE Min, Max;
	SHORT TransposeVal;
	CutAndTransposeKeys(BYTE Min, BYTE Max, SHORT TransposeVal = 0) {
		this->Min = Min;
		this->Max = Max; 
		this->TransposeVal = TransposeVal;
	}
	inline optional<BYTE> Process(BYTE Value) {
		if (Value<=Max && Value>=Min) {
			SHORT SValue = (SHORT)Value + TransposeVal;
			if (SValue < 0 || SValue>255)return {};
			Value = SValue;
			return Value;
		}
		else {
			return {};
		}
	}
};

#define MTrk 1297379947
#define MThd 1297377380
#define STRICT_WARNINGS true

template<typename T>
struct Locker {
private:
	T abc;
	recursive_mutex mtx;
public:
	void lock() {
		mtx.lock();
	}
	void unlock() {
		mtx.unlock();
	}
	T& operator*() {
		return abc;
	}
	const T& operator*() const {
		return abc;
	}
};

//future iterator
struct SingleMIDIInfoCollector {
	struct TempoEvent {
		BYTE A;
		BYTE B;
		BYTE C;
		inline operator double() {
			DWORD L = (A<<16) | (B<<8) | (C);
			return 60000000. / L;
		}
	};
	wstring FileName;
	string LogLine;
	Locker<std::vector<std::pair<UINT64, TempoEvent>>> TempoMap;
	Locker<std::vector<UINT64>> TracksBeginings;
	BIT Processing;
};

struct SingleMIDIReProcessor {
	DWORD ThreadID;
	wstring FileName,Postfix;
	string LogLine, WarningLine, ErrorLine, AppearanceFilename;
	DWORD GlobalOffset;
	DWORD BoolSettings;
	DWORD NewTempo;
	WORD NewPPQN;
	WORD TrackCount;
	BIT Finished;
	BIT Processing;
	BIT InQueueToInplaceMerge;
	INT64 Start, Length;
	BYTE_PLC_Core *VolumeMapCore;
	_14BIT_PLC_Core *PitchMapCore;
	CutAndTransposeKeys *KeyConverter;
	BIT LogToConsole;
	UINT64 FilePosition,FileSize;
	static void ostream_write(vector<BYTE>& vec, const vector<BYTE>::iterator& beg, const vector<BYTE>::iterator& end, ostream& out) {
		out.write(((char*)vec.data()) + (beg - vec.begin()), end - beg);
	}
	static void ostream_write(vector<BYTE>& vec, ostream& out) {
		out.write(((char*)vec.data()), vec.size());;
	}
	SingleMIDIReProcessor(wstring filename, DWORD Settings, DWORD Tempo, DWORD GlobalOffset, WORD PPQN, DWORD ThreadID, BYTE_PLC_Core *VolumeMapCore, _14BIT_PLC_Core* PitchMapCore, CutAndTransposeKeys* KeyConverter, wstring RPostfix = L"_.mid", BIT InplaceMerge = 0, UINT64 FileSize = 0, INT64 Start = 0, INT64 Length = -1) {
		this->ThreadID = ThreadID;
		this->FileName = filename;
		this->BoolSettings = Settings;
		this->NewTempo = Tempo;
		this->NewPPQN = PPQN;
		this->GlobalOffset = GlobalOffset;
		this->VolumeMapCore = (BYTE_PLC_Core*)VolumeMapCore;
		this->PitchMapCore = (_14BIT_PLC_Core*)PitchMapCore;
		this->KeyConverter = (CutAndTransposeKeys*)KeyConverter;
		this->InQueueToInplaceMerge = InplaceMerge;
		this->Postfix = RPostfix;
		this->FileSize = FileSize;
		this->LogLine = this->WarningLine = this->ErrorLine = " ";
		this->Finished = 0;
		this->TrackCount = 0;
		this->Processing = 0;
		this->FilePosition = 0;
		this->Start = Start;
		this->Length = Length;
	}
	SingleMIDIReProcessor(wstring filename, DWORD Settings, DWORD Tempo, DWORD GlobalOffset, WORD PPQN, DWORD ThreadID, PLC<BYTE,BYTE> *VolumeMapCore, PLC<WORD, WORD>* PitchMapCore, CutAndTransposeKeys* KeyConverter, wstring RPostfix = L"_.mid", BIT InplaceMerge = 0, UINT64 FileSize = 0, INT64 Start = 0, INT64 Length = -1) {
		this->ThreadID = ThreadID;
		this->FileName = filename;
		this->BoolSettings = Settings;
		this->NewTempo = Tempo;
		this->NewPPQN = PPQN;
		this->GlobalOffset = GlobalOffset;
		if (VolumeMapCore)
			this->VolumeMapCore = new BYTE_PLC_Core(VolumeMapCore);
		else
			this->VolumeMapCore = NULL;
		if (PitchMapCore)
			this->PitchMapCore = new _14BIT_PLC_Core(PitchMapCore);
		else 
			this->PitchMapCore = NULL;
		this->KeyConverter = KeyConverter;
		this->InQueueToInplaceMerge = InplaceMerge;
		this->Postfix = RPostfix;
		this->FileSize = FileSize;
		this->LogLine = this->WarningLine = this->ErrorLine = "_";
		this->Finished = 0;
		this->TrackCount = 0;
		this->Processing = 0;
		this->FilePosition = 0;
		this->Start = Start;
		this->Length = Length;
	}
	#define PCLOG true
	void ReProcess() {
		bbb_ffr file_input(FileName.c_str());
		ofstream file_output(this->FileName + Postfix, ios::binary | ios::out);
		vector<BYTE> Track;///quite memory expensive...
		vector<BYTE> UnLoad;
		array<DWORD,4096> CurHolded;
		for (auto& v : CurHolded)
			v = 0;
		bool ActiveSelection = Length > 0;
		DWORD EventCounter=0,Header/*,MultitaskTVariable*/, UnholdedCount=0;
		WORD OldPPQN;
		double DeltaTimeTranq = GlobalOffset,TDeltaTime=0,PPQNIncreaseAmount=1;
		BYTE Tempo1, Tempo2, Tempo3;
		BYTE IO = 0, TYPEIO = 0;//register?
		BYTE RSB = 0;
		TrackCount = 0;
		Processing = 1;
		Track.reserve(10000000);//just in case...

		if (NewTempo) {
			Tempo1 = ((60000000 / NewTempo) >> 16),
			Tempo2 = (((60000000 / NewTempo) >> 8) & 0xFF),
			Tempo3 = ((60000000 / NewTempo) & 0xFF);
		}
		///processing starts here///
		for (int i = 0; i < 12 && file_input.good(); i++)
			file_output.put(file_input.get());
		///PPQN swich
		OldPPQN = ((WORD)file_input.get())<<8;
		OldPPQN |= file_input.get();
		file_output.put(NewPPQN >> 8);
		file_output.put(NewPPQN & 0xFF);
		PPQNIncreaseAmount = (double)NewPPQN / OldPPQN;
		///if statement says everything
		///just in case of having no tempoevents :)
		if (NewTempo) {
			Track.push_back(0x00);
			Track.push_back(0xFF);
			Track.push_back(0x51);
			Track.push_back(0x03);
			Track.push_back(Tempo1);
			Track.push_back(Tempo2);
			Track.push_back(Tempo3);
			EventCounter++;
		}
		///Tracks 
		while (file_input.good() && !file_input.eof()) {
			Header = 0;
			while (Header != MTrk && file_input.good() && !file_input.eof()) {//MTrk = 1297379947
				Header = (Header << 8) | file_input.get();
			}
			///here was header;
			for (int i = 0; i < 4 && file_input.good(); i++)
				file_input.get();

			if (file_input.eof())break;

			INT64 CurTick = 0;//understandable
			bool Entered = false, Exited = false;
			INT64 TickTranq = 0;

			DeltaTimeTranq += GlobalOffset;
			///Parsing events
			while (file_input.good() && !file_input.eof()) {
				if (KeyConverter) {//?
					Header = 0;
					NULL;
				}
				///Deltatime recalculation
				DWORD vlv = 0, tvlv;
				DWORD size = 0, true_delta;
				IO = 0;
				do {
					IO = file_input.get();
					vlv = vlv << 7 | IO&0x7F;
				} while (IO & 0x80 && !file_input.eof());
				//DWORD file_pos = fi.tellg();
				DeltaTimeTranq += ((double)vlv * PPQNIncreaseAmount) + TickTranq;
				DeltaTimeTranq -= (tvlv = (vlv = (DWORD)DeltaTimeTranq));
				CurTick += vlv - TickTranq;//adding to the "current tick"
				///pushing proto vlv to track 

				if (ActiveSelection) {
					if (!Entered && CurTick >= Start) {
						//printf("Entered# Cur:%li \t Edge:%li \t VLV:%i\n", CurTick, Start, vlv);
						Entered = true;
						bool EscapeFlag = true;
						for (auto& v : CurHolded) {//in case if none of the notes are pressed
							if (v) {
								EscapeFlag = false;
								//printf("Escaped\n");
								break;
							}
						}
						if (EscapeFlag)
							goto EscapeFromEntered;
						INT64 tStartTick = tvlv - (CurTick - (Start));
						DWORD tSZ = 0;
						bool Flag_FirstRun = false;
						vlv = tvlv - tStartTick;
						tvlv = vlv;
						//printf("Entered# VLV:%i \t TStart:%li\n", vlv, tStartTick);
						do {
							tSZ++;
							Track.push_back(tStartTick & 0x7F);
							tStartTick >>= 7;
						} while (tStartTick);
						///making magic with it...
						for (DWORD i = 0; i < (tSZ >> 1); i++) {
							swap(Track[Track.size() - 1 - i], Track[Track.size() - tSZ + i]);
						}
						for (DWORD i = 2; i <= tSZ; i++) {
							Track[Track.size() - i] |= 0x80;///hack (going from 2 instead of going from one)
						}
						for (int k = 0; k < 4096; k++) {//multichanneled tracks.
							for (int polyphony = 0; polyphony < CurHolded[k]; polyphony++) {
								if (Flag_FirstRun)Track.push_back(0);
								else Flag_FirstRun = true;
								Track.push_back((k & 0xF) | 0x90);
								Track.push_back(k >> 4);
								Track.push_back(1);
							}
						}
					}
					EscapeFromEntered:
					if (Entered && !Exited && (CurTick >= (Start + Length))) {
						//printf("Exited#  Cur:%li \t Edge:%li \t VLV:%i\n", CurTick, Start + Length, vlv);
						Exited = true;
						bool EscapeFlag = true;
						for (auto& v : CurHolded) {
							if (v) {
								EscapeFlag = false;
								//printf("Escaped\n");
								break;
							}
						}
						if (EscapeFlag)
							goto ActiveSelectionBorderEscapePoint;
						INT64 tStartTick = tvlv - (CurTick - (Start + Length));
						DWORD tSZ = 0;
						bool Flag_FirstRun = false;
						vlv = tvlv - tStartTick;
						tvlv = vlv;
						//printf("Exited#  VLV:%i \t TStart:%li\n", vlv, tStartTick);
						do {
							tSZ++;
							Track.push_back(tStartTick & 0x7F);
							tStartTick >>= 7;
						} while (tStartTick);
						///making magic with it...
						for (DWORD i = 0; i < (tSZ >> 1); i++) {
							swap(Track[Track.size() - 1 - i], Track[Track.size() - tSZ + i]);
						}
						for (DWORD i = 2; i <= tSZ; i++) {
							Track[Track.size() - i] |= 0x80;///hack (going from 2 instead of going from one)
						}
						for (int k = 0; k < 4096; k++) {
							for (int polyphony = 0; polyphony < CurHolded[k]; polyphony++) {
								if (Flag_FirstRun)Track.push_back(0);
								else Flag_FirstRun = true;
								Track.push_back((k & 0xF) | 0x80);
								Track.push_back(k>>4);
								Track.push_back(1);
							}
						}
					}
				}
				ActiveSelectionBorderEscapePoint:
				do {
					size++;
					Track.push_back(tvlv & 0x7F);
					tvlv >>= 7;
				} while (tvlv);
				///making magic with it...
				for (DWORD i = 0; i < (size >> 1); i++) {
					swap(Track[Track.size() - 1 - i], Track[Track.size() - size + i]);
				}
				for (DWORD i = 2; i <= size; i++) {
					Track[Track.size() - i] |= 0x80;///hack (going from 2 instead of going from one)
				}
				IO = file_input.get();
				if (IO == 0xFF) {//I love metaevents :d
					RSB = 0;
					Track.push_back(IO);
					IO = file_input.get();//MetaEventType.
					Track.push_back(IO);
					if (IO == 0x2F) {//end of track event
						file_input.get();
						Track.push_back(0);

						UnLoad.push_back(0x01);
						for (size_t k = 0; k < 4096; k++) {//in case of having not enough note offs
							for (int i = 0; i < CurHolded[k]; i++) {
								UnLoad.push_back((k & 0xF) | 0x80);
								UnLoad.push_back(k>>4);
								UnLoad.push_back(0x40);
								UnLoad.push_back(0x00);
							}
							CurHolded[k] = 0;
						}
						UnLoad.pop_back();

						if ((BoolSettings&SMP_BOOL_SETTINGS_EMPTY_TRACKS_RMV && !EventCounter)) {
							LogLine = "Track " + to_string(TrackCount) + " deleted!";
						}
						else {
							Track.insert(Track.end() - size - 3, UnLoad.begin(), UnLoad.end());
							UnLoad.clear();
							file_output.put('M'); file_output.put('T'); file_output.put('r'); file_output.put('k');
							file_output.put(Track.size() >> 24);///size of track
							file_output.put(Track.size() >> 16);
							file_output.put(Track.size() >> 8);
							file_output.put(Track.size());
							//copy(Track.begin(), Track.end(), ostream_iterator<BYTE>(fo));
							SingleMIDIReProcessor::ostream_write(Track, file_output);
							LogLine = "Track " + to_string(TrackCount++) + " is processed";
						}
						Track.clear();
						EventCounter = 0;
						break;
					}
					else {//other meta events
						if (!(BoolSettings&SMP_BOOL_SETTINGS_IGNORE_ALL_BUT_TEMPOS_NOTES_AND_PITCH)) {
							tvlv = 0;
							DWORD vlvsize = 0;
							TYPEIO = IO;
							do {
								IO = file_input.get();
								Track.push_back(IO);
								vlvsize++;
								tvlv = (tvlv << 7) | (IO & 0x7F);
							} while (IO & 0x80);
							if (TYPEIO != 0x51 ) {
								for (int i = 0; i < tvlv; i++)
									Track.push_back(file_input.get()); 
							}
							else {
								if (BoolSettings&SMP_BOOL_SETTINGS_IGNORE_TEMPOS) {//deleting
									TickTranq = vlv;
									for (int i = -2-vlvsize; i < (INT32)size; i++)
										Track.pop_back();
									for (int i = 0; i < tvlv; i++)file_input.get();
									continue;
								}
								else {
									if (NewTempo) {
										file_input.get();
										file_input.get();
										file_input.get();
										Track.push_back(Tempo1);
										Track.push_back(Tempo2);
										Track.push_back(Tempo3);
										EventCounter++;
									}
									else {
										Track.push_back(file_input.get());
										Track.push_back(file_input.get());
										Track.push_back(file_input.get());
										EventCounter++;
									}
								}
							}						
						}
						else {
							tvlv = 0;
							DWORD vlvsize = 0;
							TYPEIO = IO;
							do {
								IO = file_input.get();
								Track.push_back(IO);
								vlvsize++;
								tvlv = (tvlv << 7) | (IO & 0x7F);
							} while (IO & 0x80);
							if (TYPEIO != 0x51 || BoolSettings&SMP_BOOL_SETTINGS_IGNORE_TEMPOS) {//deleting
								TickTranq = vlv;
								for (int i = -2 - vlvsize; i < (INT32)size; i++)
									Track.pop_back();
								for (int i = 0; i < tvlv; i++)file_input.get();
								continue;
							}
							else{
								if (NewTempo) {
									file_input.get();
									file_input.get();
									file_input.get();
									Track.push_back(Tempo1);
									Track.push_back(Tempo2);
									Track.push_back(Tempo3);
									EventCounter++;
								}
								else {
									Track.push_back(file_input.get());
									Track.push_back(file_input.get());
									Track.push_back(file_input.get());
									EventCounter++;
								}
							}
						}
					}
				}
				else if (IO >= 0x80 && IO <= 0x9F) {///notes
					RSB = IO;
					if (!(BoolSettings&SMP_BOOL_SETTINGS_IGNORE_NOTES) || Length>0) {
						bool DeleteEvent = BoolSettings&SMP_BOOL_SETTINGS_IGNORE_NOTES, isnoteon = (RSB >= 0x90);
						BYTE Key = file_input.get();
						Track.push_back(IO);///Event|Channel data
						if (this->KeyConverter) {////key data processing
							auto T = (this->KeyConverter->Process(Key));
							if (T.has_value()) 
								Track.push_back(Key = T.value());
							else {
								Track.push_back(0);
								DeleteEvent = true;
							}
						}
						else Track.push_back(Key);///key data processing
						BYTE Vol = file_input.get();
						if (this->VolumeMapCore && (IO & 0x10))
							Vol = (*this->VolumeMapCore)[Vol];
						if (!Vol && isnoteon) {
							IO = (IO & 0xF) | 0x80;
							*((&Track.back()) - 1) = IO;
						}
						isnoteon = (IO >= 0x90);

						SHORT HoldIndex = (Key << 4) | (IO & 0xF);
						////////added
						if (!DeleteEvent) {
							if (isnoteon) {//if noteon
								CurHolded[HoldIndex]++;
								if (ActiveSelection && (!Entered || (Entered && Exited)))
									DeleteEvent = true;
							}
							else {//if noteoff
								if (CurHolded[HoldIndex]) {///can do something with ticks...
									//INT64 StartTick = CurHolded[Key].back();
									CurHolded[HoldIndex]--;
									if (ActiveSelection && (!Entered || (Entered && Exited)))
										DeleteEvent = true;
								}
								else {
									DeleteEvent = true;
									UnholdedCount++;
									if (STRICT_WARNINGS)
										WarningLine = "OFF of nonON Note: " + to_string(UnholdedCount);
									//cout << vlv << ":" << TickTranq << endl;
								}
							}
						}
						if (DeleteEvent) {///deletes the note from the point of writing the key data.
							TickTranq = vlv;
							for (int q = -2; q < (INT32)size; q++) {
								Track.pop_back();
							}
							continue;
						}
						else {
							Track.push_back(Vol);
						}
						EventCounter++;
					}
					else {
						TickTranq = vlv;
						for (int i = 0; i < (INT32)size; i++)
							Track.pop_back();
						file_input.get(); file_input.get();///for 1st and 2nd parameter
						continue;
					}
				}
				else if (IO >= 0xE0 && IO <= 0xEF) {///pitch bending event
					RSB = IO;
					if (!(BoolSettings&SMP_BOOL_SETTINGS_IGNORE_PITCHES)) {
						Track.push_back(IO);
						if (this->PitchMapCore) {
							WORD PitchBend = file_input.get()<<7;
							PitchBend |= file_input.get()&0x7F;
							PitchBend = (*PitchMapCore)[PitchBend];
							Track.push_back((PitchBend >> 7) & 0x7F);
							Track.push_back(PitchBend & 0x7F);
						}
						else {
							Track.push_back(file_input.get()&0x7F);
							Track.push_back(file_input.get()&0x7F);
						}
						EventCounter++;
					}
					else {
						TickTranq = vlv;
						for (int i = 0; i < (INT32)size; i++)
							Track.pop_back();
						file_input.get(); file_input.get();///for 1st and 2nd parameter
						continue;
					}
				}
				else if ((IO >= 0xA0 && IO <= 0xBF)) {///idk :t
					RSB = IO;
					if (!(BoolSettings&SMP_BOOL_SETTINGS_IGNORE_ALL_BUT_TEMPOS_NOTES_AND_PITCH)) {
						Track.push_back(IO);///event type
						Track.push_back(file_input.get());///1st parameter
						Track.push_back(file_input.get());///2nd parameter
						EventCounter++;
					}
					else {
						TickTranq = vlv;
						for (int i = 0; i < (INT32)size; i++)
							Track.pop_back();
						file_input.get(); file_input.get();
						continue;
					}
				}
				else if ((IO >= 0xC0 && IO <= 0xCF)) {///program change
					RSB = IO;
					if (!(BoolSettings&SMP_BOOL_SETTINGS_IGNORE_ALL_BUT_TEMPOS_NOTES_AND_PITCH)) {
						Track.push_back(IO);///event type
						if(BoolSettings&SMP_BOOL_SETTINGS_ALL_INSTRUMENTS_TO_PIANO)Track.push_back(file_input.get() & 0);///1st parameter
						else Track.push_back(file_input.get() & 0x7F);
						EventCounter++;
					}
					else {
						TickTranq = vlv;
						for (int i = 0; i < (INT32)size; i++)
							Track.pop_back();
						file_input.get();///for its single parameter
						continue;
					}
				}
				else if (IO >= 0xD0 && IO <= 0xDF) {///????:D
					RSB = IO;
					if (!(BoolSettings&SMP_BOOL_SETTINGS_IGNORE_ALL_BUT_TEMPOS_NOTES_AND_PITCH)) {
						Track.push_back(IO);///event type
						Track.push_back(file_input.get() & 0x7F);
						//fi.get();
						EventCounter++;
					}
					else {
						TickTranq = vlv;
						file_input.get();///...:D
						for (int i = 0; i < (INT32)size; i++)
							Track.pop_back();
						continue;
					}
				}
				else if (IO == 0xF0 || IO == 0xF7) {
					DWORD vlvsize = 0;
					RSB = 0;
					tvlv = 0;
					Track.push_back(IO);
					do {
						IO = file_input.get();
						Track.push_back(IO);
						vlvsize++;
						tvlv = (tvlv << 7) | (IO & 0x7F);
					} while (IO & 0x80);
					if (!(BoolSettings&SMP_BOOL_SETTINGS_IGNORE_ALL_BUT_TEMPOS_NOTES_AND_PITCH)) {
						for (int i = 0; i < tvlv; i++)
							Track.push_back(file_input.get());
						EventCounter++;
					}
					else {
						TickTranq = vlv;
						for (int i = -1 - vlvsize; i < (INT32)size; i++)
							Track.pop_back();
						for (int i = 0; i < tvlv; i++)file_input.get();
						continue;
					}
				}
				else {///RUNNING STATUS BYTE
					//////////////////////////
					//WarningLine = "Running status detected";
					if (RSB >= 0x80 && RSB <= 0x9F) {///notes
						if (!(BoolSettings&SMP_BOOL_SETTINGS_IGNORE_NOTES)) {
							bool DeleteEvent = (BoolSettings & SMP_BOOL_SETTINGS_IGNORE_NOTES), isnoteon = (RSB >= 0x90);
							BYTE Key = IO;
							Track.push_back(RSB);///Event|Channel data
							if (this->KeyConverter) {////key data processing
								auto T = (this->KeyConverter->Process(Key));
								if (T.has_value())
									Track.push_back(Key = T.value());
								else {
									Track.push_back(0);
									DeleteEvent = true;
								}
							}
							else Track.push_back(Key);///key data processing

							BYTE Vol = file_input.get();
							if (this->VolumeMapCore && isnoteon)
								Vol = (*this->VolumeMapCore)[Vol];
							if (!Vol && isnoteon) {
								IO = (RSB & 0xF) | 0x80;
								*((&Track.back()) - 1) = IO;
							}
							else
								IO = RSB;

							isnoteon = (IO >= 0x90);
							SHORT HoldIndex = (Key << 4) | (RSB & 0xF);
							//printf("%X %X\n", Key, file_input.tellg());

							////////added
							if (!DeleteEvent) {
								if (isnoteon) {//if noteon
									CurHolded[HoldIndex]++;
									if (ActiveSelection && (!Entered || (Entered && Exited)))
										DeleteEvent = true;
								}
								else {//if noteoff
									if (CurHolded[HoldIndex]) {///can do something with ticks...
										//INT64 StartTick = CurHolded[Key].back();
										CurHolded[HoldIndex]--;
										if (ActiveSelection && (!Entered || (Entered && Exited)))
											DeleteEvent = true;
									}
									else {
										DeleteEvent = true;
										UnholdedCount++;
										//printf("%X %X nonholded\n", Key, file_input.tellg());
										if (STRICT_WARNINGS)
											WarningLine = "(RSB) OFF of nonON Note: " + to_string(UnholdedCount);
									}
								}
							}
							if (DeleteEvent) {///deletes the note from the point of writing the key data.
								TickTranq = vlv;
								for (int q = -2; q < (INT32)size; q++) {
									Track.pop_back();
								}
								continue;
							}
							else {
								Track.push_back(Vol);
							}
							EventCounter++;
						}
						else {
							TickTranq = vlv;
							for (int i = 0; i < (INT32)size; i++)
								Track.pop_back();
							file_input.get();///for 2nd parameter
							continue;
						}
					}
					else if (RSB >= 0xE0 && RSB <= 0xEF) {///pitch bending event
						if (!(BoolSettings&SMP_BOOL_SETTINGS_IGNORE_PITCHES)) {
							Track.push_back(RSB);
							if (this->PitchMapCore) {
								WORD PitchBend = IO << 7;
								PitchBend |= file_input.get() & 0x7F;
								PitchBend = (*PitchMapCore)[PitchBend];
								Track.push_back((PitchBend >> 7) & 0x7F);
								Track.push_back(PitchBend & 0x7F);
							}
							else {
								Track.push_back(IO);
								Track.push_back(file_input.get() & 0x7F);
							}
							EventCounter++;
						}
						else {
							TickTranq = vlv;
							for (int i = 0; i < (INT32)size; i++)
								Track.pop_back();
							file_input.get();///for 2nd parameter
							continue;
						}
					}
					else if ((RSB >= 0xA0 && RSB <= 0xBF)) {///idk :t
						if (!(BoolSettings&SMP_BOOL_SETTINGS_IGNORE_ALL_BUT_TEMPOS_NOTES_AND_PITCH)) {
							Track.push_back(RSB);///event type
							Track.push_back(IO);///1st parameter
							Track.push_back(file_input.get());///2nd parameter
							EventCounter++;
						}
						else {
							TickTranq = vlv;
							for (int i = 0; i < (INT32)size; i++)
								Track.pop_back();
							file_input.get();
							continue;
						}
					}
					else if ((RSB >= 0xC0 && RSB <= 0xCF)) {///program change
						if (!(BoolSettings&SMP_BOOL_SETTINGS_IGNORE_ALL_BUT_TEMPOS_NOTES_AND_PITCH)) {
							Track.push_back(RSB);///event type
							if (BoolSettings&SMP_BOOL_SETTINGS_ALL_INSTRUMENTS_TO_PIANO)Track.push_back(file_input.get() & 0);///1st parameter
							else Track.push_back(IO);///////////////////////////////aksjdhkahkfjhak
							EventCounter++;
						}
						else {
							TickTranq = vlv;
							for (int i = 0; i < (INT32)size; i++)
								Track.pop_back();
							continue;
							//fi.get();///for its single parameter
						}
					}
					else if (RSB >= 0xD0 && RSB <= 0xDF) {///????:D
						if (!(BoolSettings&SMP_BOOL_SETTINGS_IGNORE_ALL_BUT_TEMPOS_NOTES_AND_PITCH)) {
							Track.push_back(RSB);///event type
							Track.push_back(IO);
							EventCounter++;
						}
						else {
							TickTranq = vlv;
							for (int i = 0; i < (INT32)size; i++)
								Track.pop_back();
							continue;
						}
					}
					else {
						ErrorLine = "Track corruption. Pos:" + to_string(file_input.tellg());
						//ThrowAlert_Error(ErrorLine);
						RSB = 0;
						DWORD T=0;
						while (T != 0x2FFF00 && file_input.good() && !file_input.eof()) {
							T = ((T << 8) | file_input.get())&0xFFFFFF;
						}
						Track.push_back(0x2F);
						Track.push_back(0xFF);
						Track.push_back(0x00);

						UnLoad.push_back(0x01);
						for (size_t k = 0; k < 4096; k++) {//in case of having not enough note offs
							for (int i = 0; i < CurHolded[k]; i++) {
								UnLoad.push_back((k & 0xF) | 0x80);
								UnLoad.push_back(k >> 4);
								UnLoad.push_back(0x40);
								UnLoad.push_back(0x00);
							}
							CurHolded[k] = 0;
						}
						UnLoad.pop_back();

						if (!EventCounter && BoolSettings & SMP_BOOL_SETTINGS_EMPTY_TRACKS_RMV) {
							WarningLine = "Corrupted track cleared. (" + to_string(TrackCount) + ")";
						}
						else {
							Track.insert(Track.end() - size - 3, UnLoad.begin(), UnLoad.end());
							UnLoad.clear();
							file_output.put('M'); file_output.put('T'); file_output.put('r'); file_output.put('k');
							file_output.put(Track.size() >> 24);///size of track
							file_output.put(Track.size() >> 16);
							file_output.put(Track.size() >> 8);
							file_output.put(Track.size());
							//copy(Track.begin(), Track.end(), ostream_iterator<BYTE>(file_output));
							SingleMIDIReProcessor::ostream_write(Track, file_output);
							WarningLine = "An attempt to recover the track. (" + to_string(TrackCount++) + ")";
							TrackCount++;
						}
						Track.clear();
						EventCounter = 0;
						break;
					}
				}
				TickTranq = 0;
			}
			FilePosition = file_input.tellg();
		}
		file_input.close();
		Track.clear();
		file_output.seekp(10, ios::beg);///changing amount of tracks :)
		file_output.put(TrackCount >> 8);
		file_output.put(TrackCount & 0xFF);
		file_output.close();
		Processing = 0;
		Finished = 1;
	}
};

struct MIDICollectionThreadedMerger {
	wstring SaveTo;
	BIT IntermediateRegularFlag;
	BIT IntermediateInplaceFlag;
	BIT CompleteFlag;
	BIT RemnantsRemove;
	WORD FinalPPQN;
	WORD IRTrackCount;
	WORD IITrackCount;
	vector<SingleMIDIReProcessor*> SMRP, Cur_Processing;

	struct DegeneratedTrackIterator {
		INT64 CurPosition;
		INT64 CurTick;
		BYTE* TrackData;
		UINT64 TrackSize;
		array<INT64, 4096> Holded;
		bool Processing;
		DegeneratedTrackIterator(vector<BYTE>& Vec) {
			TrackData = Vec.data();
			TrackSize = Vec.size();
			CurTick = 0; 
			CurPosition = 0;
			Processing = true;
			for (auto& a : Holded)
				a = 0;
		}
		INT64 TellPoliphony() {
			INT64 T = 0;
			for (auto& q : Holded) {
				T += q;
			}
			return T;
		}
		void SingleEventAdvance() {
			if (Processing && CurPosition < TrackSize) {
				DWORD VLV = 0, IO = 0;
				do {
					IO = TrackData[CurPosition];
					CurPosition++;
					VLV = (VLV << 7) | (IO & 0x7F);
				} while (IO & 0x80);
				CurTick += VLV;
				if (TrackData[CurPosition] == 0xFF) {
					CurPosition++;
					if (TrackData[CurPosition] == 0x2F) {
						Processing = false;
						CurPosition+=2;
					}
					else {
						IO = 0;
						CurPosition++;
						do {
							IO = TrackData[CurPosition];
							CurPosition++;
							VLV = (VLV << 7) | (IO & 0x7F);
						} while (IO & 0x80);
						for (int vlvsize = 0; vlvsize < IO; vlvsize++) 
							CurPosition++;
					}
				}
				else if (TrackData[CurPosition] >= 0x80 && TrackData[CurPosition] <= 0x9F) {
					WORD FTD = TrackData[CurPosition];
					CurPosition++;
					WORD KEY = TrackData[CurPosition];
					CurPosition++;
					CurPosition++;//volume - meh;
					WORD Argument = (KEY << 4) | (FTD & 0xF);
					if (FTD & 0x10) //if noteon
						Holded[Argument]++;
					else if (Holded[Argument] > 0) {
						Holded[Argument]--;
					}
				}
				else if ((TrackData[CurPosition] >= 0xA0 && TrackData[CurPosition] <= 0xBF) || (TrackData[CurPosition] >= 0xE0 && TrackData[CurPosition] <= 0xEF))
					CurPosition += 3;
				else if ((TrackData[CurPosition] >= 0xC0 && TrackData[CurPosition] <= 0xDF))
					CurPosition += 2;
				else if (true)
					ThrowAlert_Error("DTI Failure at " + to_string(CurPosition) + ". Type: " +to_string(TrackData[CurPosition])+ ". Tell developer about it and give hime source midi\n");
			}
			else Processing = false;
		}
		bool AdvanceUntilReachingPositionOf(INT64 Position) {
			while (Processing && CurPosition < Position)
				SingleEventAdvance();
			return Processing;
		}
		void PutCurrentHoldedNotes(vector<BYTE>& out, bool output_noteon_wall) {
			constexpr DWORD LocalDeltaTimeTrunkEdge = 0xF000000;//BC808000 in vlv
			bool first_nonzero_delta_output = output_noteon_wall;
			UINT64 LocalTick = CurTick;
			BYTE DeltaLen = 0;
			while (output_noteon_wall && LocalTick > LocalDeltaTimeTrunkEdge) {//fillers
				out.push_back(0xBC);//vlv
				out.push_back(0x80);
				out.push_back(0x80);
				out.push_back(0x00);
				out.push_back(0xFF);//empty text event
				out.push_back(0x01);
				out.push_back(0x00);
				LocalTick -= LocalDeltaTimeTrunkEdge;
			}
			if (output_noteon_wall) {
				do {
					DeltaLen++;
					out.push_back(LocalTick & 0x7F);
					LocalTick >>= 7;
				} while (LocalTick);
				for (DWORD q = 0; q < (DeltaLen >> 1); q++)
					swap(out[out.size() - 1 - q], out[out.size() - DeltaLen + q]);
				for (DWORD q = 2; q <= DeltaLen; q++)
					out[out.size() - q] |= 0x80;
			}
			else
				out.push_back(0);
			for (int i = 0; i < 4096;i++) {
				INT64 key = Holded[i];
				while (key) {
					out.push_back((0x10 * output_noteon_wall) | (i&0xF) | 0x80);//note data;
					out.push_back(i >> 4);//key;
					out.push_back(1);//almost silent
					out.push_back(0);//delta time
					key--;
				}
			}
			if(out.size())
				out.pop_back();
		}
	};
	~MIDICollectionThreadedMerger() {
		ResetEverything();
	}
	MIDICollectionThreadedMerger(vector<SingleMIDIReProcessor*> SMRP, WORD FinalPPQN, wstring SaveTo) {
		this->SaveTo = SaveTo;
		this->SMRP = SMRP;
		this->FinalPPQN = FinalPPQN;
		IntermediateRegularFlag = IntermediateInplaceFlag = CompleteFlag = IRTrackCount = IITrackCount = 0;
	}
	void ResetCurProcessing() {
		for (int i = 0; i < Cur_Processing.size(); i++) {
			Cur_Processing[i]=NULL;
		}
	}
	void ResetEverything() {
		IntermediateRegularFlag = IntermediateInplaceFlag = CompleteFlag = IRTrackCount = IITrackCount = 0;
		Cur_Processing.clear();
		for (int i = 0; i < SMRP.size(); i++) {
			delete SMRP[i];
		}
		SMRP.clear();
	}
	BIT CheckSMRPFinishedFlags() {
		for (int i = 0; i < SMRP.size(); i++)
			if (!SMRP[i]->Finished)return 0;
		this->ResetCurProcessing();
		this->Start_RI_Merge();
		return 1;
	}
	BIT CheckRIMerge() {
		if (IntermediateRegularFlag && IntermediateInplaceFlag)
			FinalMerge();
		else return 0;
		//cout << "Final_Finished\n";
		return 1;
	}
	void ProcessMIDIs() {//
		set<DWORD> IDs;
		DWORD Counter=0;
		vector<SingleMIDIReProcessor*> SMRPv;
		for (int i = 0; i < SMRP.size(); i++) {
			IDs.insert((SMRP[i])->ThreadID);
		}
		Cur_Processing = vector<SingleMIDIReProcessor*>(IDs.size(), NULL);
		for (auto Y = IDs.begin(); Y != IDs.end(); Y++) {
			for (int i = 0; i < SMRP.size(); i++) {
				if (*Y == (SMRP[i])->ThreadID) {
					SMRPv.push_back(SMRP[i]);
				}
			}
			thread TH([this](vector<SingleMIDIReProcessor*> SMRPv,DWORD Counter) {
				for (int i = 0; i < SMRPv.size(); i++) {
					this->Cur_Processing[Counter] = SMRPv[i];
					SMRPv[i]->ReProcess();
				}
				//this->Cur_Processing[Counter] = NULL;
			}, SMRPv, Counter++);
			TH.detach();
			SMRPv.clear();
		}
	}
	void InplaceMerge() {
		vector<SingleMIDIReProcessor*> *InplaceMergeCandidats = new vector<SingleMIDIReProcessor*>;
		for (int i = 0; i < SMRP.size(); i++)
			if (SMRP[i]->InQueueToInplaceMerge)InplaceMergeCandidats->push_back(SMRP[i]);
		IITrackCount = 0;
		thread IMC_Processor([](vector<SingleMIDIReProcessor*>* IMC, BIT *FinishedFlag, WORD PPQN, wstring _SaveTo, WORD *TrackCount) {
			if (IMC->empty()) {
				(*FinishedFlag) = 1; /// Will this work?
				return;
			}
			BIT ActiveStreamFlag = 1;
			BIT ActiveTrackReading = 1;
			vector<bbb_ffr*> fiv;
			#define pfiv (*fiv[i])
			vector<INT64> DecayingDeltaTimes;
			#define ddt (DecayingDeltaTimes[i])
			vector<BYTE> Track, FrontEdge, BackEdge;
			bbb_ffr *fi;
			ofstream file_output(_SaveTo + L".I.mid", ios::binary | ios::out);
			file_output << "MThd" << '\0' << '\0' << '\0' << (char)6 << '\0' << (char)1;
			for (auto Y = IMC->begin(); Y != IMC->end(); Y++) {
				fi = new bbb_ffr(((*Y)->FileName + (*Y)->Postfix).c_str());///
				//if ((*Y)->TrackCount > *TrackCount)*TrackCount = (*Y)->TrackCount;
				for (int i = 0; i < 14; i++)
					fi->get();
				DecayingDeltaTimes.push_back(0);
				fiv.push_back(fi);
			}
			file_output.put(*TrackCount >> 8);
			file_output.put(*TrackCount);
			file_output.put(PPQN>>8);
			file_output.put(PPQN);
			while (ActiveStreamFlag) {
				///reading tracks
				DWORD Header = 0, DIO, DeltaLen = 0, POS = 0;
				UINT64 InTrackDelta = 0;
				BIT ITD_Flag = 1 /*, NotNoteEvents_ProcessedFlag = 0*/;
				for (int i = 0; i < fiv.size(); i++) {
					while (pfiv.good() && Header != MTrk) {
						Header = (Header << 8) | pfiv.get();
					}
					for (int w = 0; w < 4; w++)
						pfiv.get();///adlakjfkljsdflsdkf
					Header = 0;
					if (pfiv.good())
						ddt = 0;
					else
						ddt = -1;
				}
				for (UINT64 Tick = 0; ActiveTrackReading; Tick++, InTrackDelta++) {
					BYTE IO, EVENTTYPE;///yas
					ActiveTrackReading = 0;
					ActiveStreamFlag = 0;
					//NotNoteEvents_ProcessedFlag = 0;
					for (int i = 0; i < fiv.size(); i++) {
						///every stream
						if (ddt == 0) {///there will be parser...
							if (Tick)goto eventevalution;
						deltatime_reading:
							DIO = 0;
							do {
								IO = pfiv.get();
								DIO = (DIO << 7) | (IO & 0x7F);
							} while (IO & 0x80);
							if (pfiv.eof())goto trackending;
							if (ddt = DIO)goto escape;
						eventevalution:///becasue why not
							EVENTTYPE = (pfiv.get());

							if (1 || EVENTTYPE > 0x9F) {
								//NotNoteEvents_ProcessedFlag = 1;
								DeltaLen = 0;
								do {
									DeltaLen++;
									Track.push_back(InTrackDelta & 0x7F);
									InTrackDelta >>= 7;
								} while (InTrackDelta);

								for (DWORD q = 0; q < (DeltaLen >> 1); q++) {
									swap(Track[Track.size() - 1 - q], Track[Track.size() - DeltaLen + q]);
								}
								for (DWORD q = 2; q <= DeltaLen; q++) {
									Track[Track.size() - q] |= 0x80;///hack (going from 2 instead of going from one)
								}
							}

							if (EVENTTYPE == 0xFF) {///meta
								Track.push_back(EVENTTYPE);
								Track.push_back(pfiv.get());
								if (Track.back() == 0x2F) {
									pfiv.get();
									for (int l = 0; l < DeltaLen + 2; l++)
										Track.pop_back();
									goto trackending;
								}
								else {
									DIO = 0;
									do {
										IO = pfiv.get();
										Track.push_back(IO);
										DIO = DIO << 7 | IO & 0x7F;
									} while (IO & 0x80);
									for (int vlvsize = 0; vlvsize < DIO; vlvsize++)
										Track.push_back(pfiv.get());
								}
							}
							else if ((EVENTTYPE >= 0x80 && EVENTTYPE <= 0xBF) || (EVENTTYPE >= 0xE0 && EVENTTYPE <= 0xEF)) {
								Track.push_back(EVENTTYPE);
								Track.push_back(pfiv.get());
								Track.push_back(pfiv.get());
							}
							else if ((EVENTTYPE >= 0xC0 && EVENTTYPE <= 0xDF)) {
								Track.push_back(EVENTTYPE);
								Track.push_back(pfiv.get());
							}
							else {///RSB CANNOT APPEAR ON THIS STAGE
								printf("Inplace error\n");
								Track.push_back(0xCA);
								Track.push_back(0);
								goto trackending;
							}
							goto deltatime_reading;
						trackending:
							ddt = -1;
							if (!pfiv.eof())
								ActiveStreamFlag = 1;
							continue;
						escape:
							ActiveTrackReading = 1;
							ddt--;
							continue;
						file_eof:
							ddt = -1;
							continue;
						}
						else if (ddt > 0) {
							ActiveTrackReading = 1;
							ddt--;
							continue;
						}
						else {
							if (pfiv.good()) {
								ActiveStreamFlag = 1;
							}
							continue;
						}
						///while it's zero delta time
					}
					if (!ActiveTrackReading && !Track.empty()) {
						DeltaLen = 0;///Hack from main reprocessor algo
						do {
							DeltaLen++;
							Track.push_back(InTrackDelta & 0x7F);
							InTrackDelta >>= 7;
						} while (InTrackDelta);
						for (DWORD q = 0; q < (DeltaLen >> 1); q++) {
							swap(Track[Track.size() - 1 - q], Track[Track.size() - DeltaLen + q]);
						}
						for (DWORD q = 2; q <= DeltaLen; q++) {
							Track[Track.size() - q] |= 0x80;
						}
						Track.push_back(0xFF);
						Track.push_back(0x2F);
						Track.push_back(0x00);
					}
				}
				ActiveTrackReading = 1;
				constexpr INT32 EDGE = 0x7F000000;
				if (Track.size() > 0xFFFFFFFFu)
					cout << "TrackSize overflow!!!\n";
				else if (Track.empty())continue;
				if (Track.size() > EDGE*2) {
					INT64 TotalShift = EDGE, PrevEdgePos = 0;
					DegeneratedTrackIterator DTI(Track);
					FrontEdge.clear();
					while (DTI.AdvanceUntilReachingPositionOf(TotalShift)) {
						TotalShift = DTI.CurPosition + EDGE;
						BackEdge.clear();
						DTI.PutCurrentHoldedNotes(BackEdge, false);
						UINT64 TotalSize = FrontEdge.size() + (DTI.CurPosition - PrevEdgePos) + BackEdge.size() + 4;
						file_output << "MTrk";
						file_output.put(TotalSize >> 24);
						file_output.put(TotalSize >> 16);
						file_output.put(TotalSize >> 8);
						file_output.put(TotalSize); 

						SingleMIDIReProcessor::ostream_write(FrontEdge, file_output);
						//copy(FrontEdge.begin(), FrontEdge.end(), ostream_iterator<BYTE>(file_output));
						SingleMIDIReProcessor::ostream_write(Track, Track.begin() + PrevEdgePos, Track.begin() + DTI.CurPosition, file_output);
						//copy(Track.begin() + PrevEdgePos, Track.begin() + DTI.CurPosition, ostream_iterator<BYTE>(file_output));
						SingleMIDIReProcessor::ostream_write(BackEdge, BackEdge.begin(), BackEdge.end(), file_output);
						//copy(BackEdge.begin(), BackEdge.end(), ostream_iterator<BYTE>(file_output));

						file_output.put(0);//that's why +4
						file_output.put(0xFF);
						file_output.put(0x2F);
						file_output.put(0);
						(*TrackCount)++;
						PrevEdgePos = DTI.CurPosition;
						FrontEdge.clear();
						DTI.PutCurrentHoldedNotes(FrontEdge, true);
					}
					file_output << "MTrk";
					UINT64 TotalSize = FrontEdge.size() + (DTI.CurPosition - PrevEdgePos);
					//cout << "Outside:" << TotalSize << endl;
					file_output.put(TotalSize >> 24);
					file_output.put(TotalSize >> 16);
					file_output.put(TotalSize >> 8);
					file_output.put(TotalSize);

					SingleMIDIReProcessor::ostream_write(FrontEdge, file_output);
					//copy(FrontEdge.begin(), FrontEdge.end(), ostream_iterator<BYTE>(file_output));
					SingleMIDIReProcessor::ostream_write(Track, Track.begin() + PrevEdgePos, Track.end(), file_output);
					//copy(Track.begin() + PrevEdgePos, Track.end(), ostream_iterator<BYTE>(file_output));

					FrontEdge.clear();
					BackEdge.clear();
					(*TrackCount)++;
				}
				else {
					file_output << "MTrk";
					file_output.put(Track.size() >> 24);
					file_output.put(Track.size() >> 16);
					file_output.put(Track.size() >> 8);
					file_output.put(Track.size());

					SingleMIDIReProcessor::ostream_write(Track, file_output);
					//copy(Track.begin(), Track.end(), ostream_iterator<BYTE>(file_output));
					(*TrackCount)++;
				}
				Track.clear();
			}
			for (int i = 0; i < fiv.size(); i++) {
				pfiv.close();
				if ((*IMC)[i]->BoolSettings&_BoolSettings::remove_remnants)
					_wremove(((((*IMC)[i]->FileName) + ((*IMC)[i]->Postfix)).c_str()));
			}
			file_output.seekp(10, ios::beg);
			file_output.put((*TrackCount) >> 8);
			file_output.put((*TrackCount)&0xff);
			file_output.close();
			(*FinishedFlag) = 1; /// Will this work?
			delete IMC;
		}, InplaceMergeCandidats, &IntermediateInplaceFlag, FinalPPQN, SaveTo, &IITrackCount);
		IMC_Processor.detach();
	}
	void RegularMerge() {
		vector<SingleMIDIReProcessor*> *RegularMergeCandidats = new vector<SingleMIDIReProcessor*>;
		for (int i = 0; i < SMRP.size(); i++) 
			if (!SMRP[i]->InQueueToInplaceMerge)RegularMergeCandidats->push_back(SMRP[i]);
		IRTrackCount = 0;
		thread RMC_Processor([](vector<SingleMIDIReProcessor*>* RMC,BIT *FinishedFlag,WORD PPQN,wstring _SaveTo, WORD *TrackCount) {
			if (RMC->empty()) {
				(*FinishedFlag) = 1; /// Will this work?
				return;
			}
			WORD T=0;
			BIT FirstFlag=1;
			const size_t buffer_size = 20000000;
			BYTE *buffer = new BYTE[buffer_size];
			ofstream file_output(_SaveTo + L".R.mid", ios::binary | ios::out);
			bbb_ffr file_input(((*(RMC->begin()))->FileName + (*(RMC->begin()))->Postfix).c_str());
			wstring filename = ((*(RMC->begin()))->FileName + (*(RMC->begin()))->Postfix);
			file_output.rdbuf()->pubsetbuf((char*)buffer, buffer_size);
			file_output << "MThd" << '\0' << '\0' << '\0' << (char)6 << '\0' << (char)1;
			file_output.put(0);
			file_output.put(0);
			file_output.put(PPQN>>8);
			file_output.put(PPQN);
			for (auto Y = RMC->begin(); Y != RMC->end(); Y++) {
				filename = (*Y)->FileName + (*Y)->Postfix;
				if (Y != RMC->begin())
					file_input.reopen_next_file(filename.c_str());
				*TrackCount += (*Y)->TrackCount;
				for (int i = 0; i < 14; i++)
					file_input.get();
				file_input.put_into_ostream(file_output); 
				int t;
				if ((*Y)->BoolSettings & _BoolSettings::remove_remnants)
					t = _wremove(filename.c_str());
			}
			file_output.seekp(10, ios::beg);
			file_output.put(*TrackCount >> 8);
			file_output.put(*TrackCount);
			(*FinishedFlag) = 1; /// Will this work?
			file_output.close();
			delete RMC;
		}, RegularMergeCandidats, &IntermediateRegularFlag, FinalPPQN, SaveTo, &IRTrackCount);
		RMC_Processor.detach();
	}
	void Start_RI_Merge() {
		RegularMerge();
		InplaceMerge();
	}
	void FinalMerge() {
		thread RMC_Processor([this](BIT *FinishedFlag, wstring _SaveTo) {
			bbb_ffr	*IM = new bbb_ffr((_SaveTo + L".I.mid").c_str()),
						*RM = new bbb_ffr((_SaveTo + L".R.mid").c_str());
			ofstream F(_SaveTo, ios::binary | ios::out);
			BIT IMgood= !IM->eof(), RMgood= !RM->eof();
			if (!IMgood || !RMgood) {
				IM->close();
				RM->close();
				F.close();
				_wremove(_SaveTo.c_str());
				_wrename((_SaveTo + L".I.mid").c_str(), _SaveTo.c_str());
				_wrename((_SaveTo + L".R.mid").c_str(), _SaveTo.c_str());//one of these will not work
				delete IM, RM;
				*FinishedFlag = 1;
				return;
			}
			WORD T=0;
			BYTE A=0, B=0;
			F << "MThd" << '\0' << '\0' << '\0' << (char)6 << '\0' << (char)1;
			for (int i = 0; i < 10; i++)
				IM->get();
			for (int i = 0; i < 10; i++)
				RM->get();
			A = IM->get();
			B = IM->get();
			T = (A + RM->get()) << 8;
			T += (B + RM->get());
			A = T >> 8;
			B = T;
			F.put(A);
			F.put(B);
			F.put(IM->get());
			F.put(IM->get());
			for (int i = 0; i < 4; i++)
				RM->get();
			IM->put_into_ostream(F);
			RM->put_into_ostream(F);
			IM->close();
			RM->close();
			delete IM;
			delete RM;
			*FinishedFlag = 1;
			if (RemnantsRemove) {
				_wremove((_SaveTo + L".I.mid").c_str());
				_wremove((_SaveTo + L".R.mid").c_str());
			}
		}, &CompleteFlag, SaveTo);
		RMC_Processor.detach();
	}
};

struct FastMIDIInfoCollector {
	wstring File;
	BIT IsAcssessable,IsMIDI;
	WORD PPQN,ExpectedTrackNumber;
	UINT64 FileSize;
	FastMIDIInfoCollector(wstring File) {
		this->File = File;
		ifstream f(File, ios::in);
		if (f)IsAcssessable = 1;
		else IsAcssessable = 0;
		FileSize = PPQN = ExpectedTrackNumber = IsMIDI = 0;
		f.close();
		Collect();
	}
	void Collect() {
		ifstream f(File, ios::in | ios::binary);
		DWORD MTHD=0;
		MTHD = (MTHD << 8) | (f.get());
		MTHD = (MTHD << 8) | (f.get());
		MTHD = (MTHD << 8) | (f.get());
		MTHD = (MTHD << 8) | (f.get());
		if (MTHD == MThd && f.good()) {
			IsMIDI = 1;
			f.seekg(10, ios::beg);
			ExpectedTrackNumber = (ExpectedTrackNumber << 8) | (f.get());
			ExpectedTrackNumber = (ExpectedTrackNumber << 8) | (f.get());
			PPQN = (PPQN << 8) | (f.get());
			PPQN = (PPQN << 8) | (f.get());
			f.close();
			FileSize = filesystem::file_size(File);
			//cout << FileSize;
		}
		else {
			IsMIDI = 0;
			ThrowAlert_Error("Error accured while getting the MIDI file info!");
			f.close();
		}
		f.close();
	}
};

unordered_map<char, string> ASCII;
void InitASCIIMap() {
	ASCII.clear();
	ifstream file("ascii.dotmap", ios::in);
	string T;
	if (true) {
		for (int i = 0; i <= 32; i++)ASCII[i] = " ";
		ASCII['!'] = "85 2";
		ASCII['"'] = "74 96";
		ASCII['#'] = "81 92 64";
		ASCII['$'] = "974631 82";
		ASCII['%'] = "7487 3623 91";
		ASCII['&'] = "37954126";
		ASCII['\'']= "85";
		ASCII['('] = "842";
		ASCII[')'] = "862";
		ASCII['*'] = "67 58 94~";
		ASCII['+'] = "46 82";
		ASCII[','] = "15~";
		ASCII['-'] = "46";
		ASCII['.'] = "2";
		ASCII['/'] = "81";
		ASCII['0'] = "97139";
		ASCII['1'] = "482 13";
		ASCII['2'] = "796413";
		ASCII['3'] = "7965 631";
		ASCII['4'] = "746 93";
		ASCII['5'] = "974631";
		ASCII['6'] = "9741364";
		ASCII['7'] = "7952";
		ASCII['8'] = "17931 46";
		ASCII['9'] = "1369746";
		ASCII[':'] = "8 5~";
		ASCII[';'] = "8 15~";
		ASCII['<'] = "943";
		ASCII['='] = "46 79~";
		ASCII['>'] = "761";
		ASCII['?'] = "795 2";
		ASCII['@'] = "317962486";
		ASCII['A'] = "1793 46";
		ASCII['B'] = "17954 531";
		ASCII['C'] = "9713";
		ASCII['D'] = "178621";
		ASCII['E'] = "9713 54";
		ASCII['F'] = "971 54";
		ASCII['G'] = "971365";
		ASCII['H'] = "9641 74 63";
		ASCII['I'] = "79 82 13";
		ASCII['J'] = "79 821";
		ASCII['K'] = "71 954 53";
		ASCII['L'] = "713";
		ASCII['M'] = "17593";
		ASCII['N'] = "1739";
		ASCII['O'] = "97139";
		ASCII['P'] = "17964";
		ASCII['Q'] = "179621 53";
		ASCII['R'] = "17964 53";
		ASCII['S'] = "974631";
		ASCII['T'] = "79 82";
		ASCII['U'] = "712693";
		ASCII['V'] = "729";
		ASCII['W'] = "71539";
		ASCII['X'] = "73 91";
		ASCII['Y'] = "752 95";
		ASCII['Z'] = "7913 46";
		ASCII['['] = "9823";
		ASCII['\\']= "72";
		ASCII[']'] = "7821";
		ASCII['^'] = "486";
		ASCII['_'] = "13";
		ASCII['`'] = "75";
		ASCII['a'] = "153";
		ASCII['b'] = "71364";
		ASCII['c'] = "6413";
		ASCII['d'] = "93146";
		ASCII['e'] = "31461";
		ASCII['f'] = "289 56";
		ASCII['g'] = "139746#";
		ASCII['h'] = "71 463";
		ASCII['i'] = "52 8";
		ASCII['j'] = "521 8";
		ASCII['k'] = "716 35";
		ASCII['l'] = "82";
		ASCII['m'] = "1452 563";
		ASCII['n'] = "1463";
		ASCII['o'] = "14631";
		ASCII['p'] = "17964#";
		ASCII['q'] = "39746#";
		ASCII['r'] = "146";
		ASCII['s'] = "6431";
		ASCII['t'] = "823 56";
		ASCII['u'] = "4136";
		ASCII['v'] = "426";
		ASCII['w'] = "4125 236";
		ASCII['x'] = "16 34";
		ASCII['y'] = "75 91#";
		ASCII['z'] = "4613";
		ASCII['{'] = "9854523";
		ASCII['|'] = "82";
		ASCII['}'] = "7856521";
		ASCII['~'] = "4859~";
		ASCII[127] = "71937 97 31";
		for (int i = 128; i < 256; i++)ASCII[i] = " ";
	}
	else {
		for (int i = 0; i < 256; i++) {
			getline(file, T);
			ASCII[(char)i] = T;
		}
	}
	file.close();
}

struct Coords {
	float x, y;
	void Set(float newx, float newy) {
		x = newx; y = newy;
	}
};

struct DottedSymbol {
	float Xpos, Ypos;
	BYTE R, G, B, A, LineWidth;
	Coords Points[9];
	string RenderWay;
	vector<char> PointPlacement;
	DottedSymbol(string RenderWay,float Xpos,float Ypos,float XUnitSize,float YUnitSize, BYTE LineWidth = 2,BYTE Red=255,BYTE Green=255,BYTE Blue=255, BYTE Alpha=255) {
		if (!RenderWay.size())RenderWay = " ";
		this->RenderWay = RenderWay;
		this->Xpos = Xpos;
		this->LineWidth = LineWidth;
		this->Ypos = Ypos;
		this->R = Red; this->G = Green; this->B = Blue; this->A = Alpha;
		for (int x = -1; x <= 1; x++) {
			for (int y = -1; y <= 1; y++) {
				Points[x + 1 + 3 * (y + 1)].Set(XUnitSize*x, YUnitSize*y);
			}
		}
		UpdatePointPlacementPositions();
	}
	DottedSymbol(char Symbol, float Xpos, float Ypos, float XUnitSize, float YUnitSize, BYTE LineWidth = 2, BYTE Red = 255, BYTE Green = 255, BYTE Blue = 255, BYTE Alpha = 255) : 
		DottedSymbol(ASCII[Symbol], Xpos, Ypos, XUnitSize, YUnitSize, LineWidth, Red, Green, Blue, Alpha) {}

	DottedSymbol(string RenderWay, float Xpos, float Ypos, float XUnitSize, float YUnitSize, BYTE LineWidth, DWORD *RGBAColor) : 
		DottedSymbol(RenderWay, Xpos, Ypos, XUnitSize, YUnitSize, LineWidth, *RGBAColor >> 24, (*RGBAColor >> 16) & 0xFF, (*RGBAColor >> 8) & 0xFF, (*RGBAColor) & 0xFF) {
		delete RGBAColor;
	}
	DottedSymbol(char Symbol, float Xpos, float Ypos, float XUnitSize, float YUnitSize, BYTE LineWidth, DWORD *RGBAColor) : 
		DottedSymbol(ASCII[Symbol], Xpos, Ypos, XUnitSize, YUnitSize, LineWidth, *RGBAColor >> 24, (*RGBAColor >> 16) & 0xFF, (*RGBAColor >> 8) & 0xFF, (*RGBAColor) & 0xFF) {
		delete RGBAColor;
	}
	inline static BIT IsRenderwaySymb(CHAR C) {
		return (C <= '9' && C >= '0' || C == ' ');
	}
	inline static BIT IsNumber(CHAR C) {
		return (C <= '9' && C >= '0');
	}

	void UpdatePointPlacementPositions() {
		PointPlacement.clear();
		if (RenderWay.size() > 1) {
			if (IsNumber(RenderWay[0]) && !IsNumber(RenderWay[1]))
				PointPlacement.push_back(RenderWay[0]);
			if (IsNumber(RenderWay.back()) && !IsNumber(RenderWay[RenderWay.size() - 2]))PointPlacement.push_back(RenderWay.back());
			for (int i = 1; i < RenderWay.size() - 1; ++i) {
				if (IsNumber(RenderWay[i]) && !IsNumber(RenderWay[i - 1]) && !IsNumber(RenderWay[i + 1]))
					PointPlacement.push_back(RenderWay[i]);
			}
		}
		else if (RenderWay.size() && IsNumber(RenderWay[0])) {
			PointPlacement.push_back(RenderWay[0]);
		}
	}
	void virtual Draw() {
		if (RenderWay == " ")return;
		float VerticalFlag = 0.f;
		CHAR BACK=0;
		if (RenderWay.back() == '#' || RenderWay.back() == '~') {
			BACK = RenderWay.back();
			RenderWay.back() = ' ';
			switch (BACK) {
				case '#':
					VerticalFlag = Points->y - Points[3].y;
				break;
				case '~':
					VerticalFlag = (Points->y - Points[3].y)/2;
				break;
			}
		}
		glColor4ub(R, G, B, A);
		glLineWidth(LineWidth);
		glPointSize(LineWidth);
		glBegin(GL_LINE_STRIP);
		BYTE IO;
		for (int i = 0; i < RenderWay.length(); i++) {
			if (RenderWay[i] == ' ') {
				glEnd();
				glBegin(GL_LINE_STRIP);
				continue;
			}
			IO = RenderWay[i] - '1';
			glVertex2f(Xpos + Points[IO].x, Ypos + Points[IO].y + VerticalFlag);
		}
		glEnd();
		glBegin(GL_POINTS);
		for (int i = 0; i < PointPlacement.size(); ++i) 
			glVertex2f(Xpos + Points[PointPlacement[i] - '1'].x, Ypos + Points[PointPlacement[i] - '1'].y + VerticalFlag);
		glEnd();
		if (BACK)RenderWay.back() = BACK;
	}
	void SafePositionChange(float NewXPos, float NewYPos) {
		SafeCharMove(NewXPos - Xpos, NewYPos - Ypos);
	}
	void SafeCharMove(float dx, float dy) {
		Xpos += dx; Ypos += dy;
	}
	inline float _XUnitSize() const {
		return Points[1].x - Points[0].x;
	}
	inline float _YUnitSize() const {
		return Points[3].y - Points[0].y;
	}
	void virtual RefillGradient(DWORD *RGBAColor, DWORD *gRGBAColor, BYTE BaseColorPoint, BYTE GradColorPoint) {
		return;
	}
	void virtual RefillGradient(BYTE Red = 255, BYTE Green = 255, BYTE Blue = 255, BYTE Alpha = 255,
		BYTE gRed = 255, BYTE gGreen = 255, BYTE gBlue = 255, BYTE gAlpha = 255,
		BYTE BaseColorPoint = 5, BYTE GradColorPoint = 8) {
		return;
	}
};

FLOAT lFONT_HEIGHT_TO_WIDTH = 2.5;
namespace lFontSymbolsInfo {
	bool IsInitialised = false;
	GLuint CurrentFont = 0; 
	HFONT SelectedFont;
	INT32 Size = 16;
	struct lFontSymbInfosListDestructor {
		bool abc;
		~lFontSymbInfosListDestructor() {
			glDeleteLists(CurrentFont, 256);
		}
	};
	lFontSymbInfosListDestructor __wFSILD = { 0 };
	void InitialiseFont(string FontName) {
		if (!IsInitialised) {
			CurrentFont = glGenLists(256);
			IsInitialised = true;
		}
		wglUseFontBitmaps(hDc, 0, 255, CurrentFont);
		SelectedFont = CreateFontA(
			Size*(BEG_RANGE/RANGE), 
			(Size>0)? Size * (BEG_RANGE / RANGE) / lFONT_HEIGHT_TO_WIDTH: 0,
			0, 0,
			FW_NORMAL,
			FALSE,
			FALSE, 
			FALSE, 
			DEFAULT_CHARSET, 
			OUT_TT_PRECIS, 
			CLIP_DEFAULT_PRECIS,
			ANTIALIASED_QUALITY, 
			FF_DONTCARE | DEFAULT_PITCH, 
			FontName.c_str()
		);
		if (SelectedFont)
			SelectObject(hDc, SelectedFont);
	}
	inline void CallListOnChar(char C) {
		if (IsInitialised) {
			const char PsChStr[2] = {C,0};
			glPushAttrib(GL_LIST_BIT);
			glListBase(CurrentFont);
			glCallLists(1, GL_UNSIGNED_BYTE, (const char*)(PsChStr));
			glPopAttrib();
		}
	}
	inline void CallListOnString(const string &S) {
		if (IsInitialised) {
			glPushAttrib(GL_LIST_BIT);
			glListBase(CurrentFont);
			glCallLists(1, GL_UNSIGNED_BYTE, S.c_str());
			glPopAttrib();
		}
	}
	const _MAT2 MT = { {0, 1}, {0, 0}, {0, 0}, {0, 1} };;
}
struct lFontSymbol : DottedSymbol {
	char Symb;
	GLYPHMETRICS GM = {0};
	lFontSymbol(char Symb, float CXpos, float CYpos, float XUnitSize, float YUnitSize, DWORD RGBA) :
		DottedSymbol(" ", CXpos, CYpos, XUnitSize, YUnitSize, 1, RGBA >> 24, (RGBA >> 16) & 0xFF, (RGBA >> 8) & 0xFF, RGBA & 0xFF) {
		this->Symb = Symb;
	}
	void Draw() override {
		float PixelSize = (RANGE * 2) / WINDXSIZE;
		float rotX = Xpos, rotY = Ypos;
		//rotate(rotX, rotY);
		if (fabsf(rotX) + 2.5 > PixelSize* WindX / 2 || fabsf(rotY) + 2.5 > PixelSize* WindY / 2)
			return;
		SelectObject(hDc, lFontSymbolsInfo::SelectedFont);
		GetGlyphOutline(hDc, Symb, GGO_METRICS, &GM, 0, NULL, &lFontSymbolsInfo::MT);
		glColor4ub(R, G, B, A);
		glRasterPos2f(Xpos - PixelSize*GM.gmBlackBoxX*0.5, Ypos - _YUnitSize()*0.5);
		lFontSymbolsInfo::CallListOnChar(Symb);
	}
};

struct BiColoredDottedSymbol : DottedSymbol {
	BYTE gR[9],gG[9],gB[9],gA[9];
	BYTE _PointData;
	BiColoredDottedSymbol(string RenderWay, float Xpos, float Ypos, float XUnitSize, float YUnitSize, BYTE LineWidth = 2,
		BYTE Red = 255, BYTE Green = 255, BYTE Blue = 255, BYTE Alpha = 255,
		BYTE gRed = 255, BYTE gGreen = 255, BYTE gBlue = 255, BYTE gAlpha = 255, 
		BYTE BaseColorPoint = 5, BYTE GradColorPoint = 8) : 
		DottedSymbol(RenderWay,Xpos,Ypos,XUnitSize,YUnitSize,LineWidth,new DWORD(0)) {
		this->_PointData = (((BaseColorPoint & 0xF) << 4) | (GradColorPoint & 0xF));
		if (BaseColorPoint == GradColorPoint) {
			for (int i = 0; i < 9; i++) {
				gR[i] = Red;
				gG[i] = Green;
				gB[i] = Blue;
				gA[i] = Alpha;
			}
		}
		else {
			BaseColorPoint--;
			GradColorPoint--;
			RefillGradient(Red, Green, Blue, Alpha, gRed, gGreen, gBlue, gAlpha, BaseColorPoint, GradColorPoint);
		}
	}
	BiColoredDottedSymbol(char Symbol, float Xpos, float Ypos, float XUnitSize, float YUnitSize, BYTE LineWidth = 2,
		BYTE Red = 255, BYTE Green = 255, BYTE Blue = 255, BYTE Alpha = 255,
		BYTE gRed = 255, BYTE gGreen = 255, BYTE gBlue = 255, BYTE gAlpha = 255,
		BYTE BaseColorPoint = 5, BYTE GradColorPoint = 8) : BiColoredDottedSymbol(ASCII[Symbol], Xpos, Ypos, XUnitSize, YUnitSize, LineWidth, Red, Green, Blue, Alpha, gRed, gGreen, gBlue, gAlpha, BaseColorPoint, GradColorPoint) {}

	BiColoredDottedSymbol(char Symbol, float Xpos, float Ypos, float XUnitSize, float YUnitSize, BYTE LineWidth,
		DWORD *RGBAColor, DWORD *gRGBAColor,
		BYTE BaseColorPoint = 5, BYTE GradColorPoint = 8) : BiColoredDottedSymbol(ASCII[Symbol], Xpos, Ypos, XUnitSize, YUnitSize, LineWidth, *RGBAColor >> 24, (*RGBAColor >> 16) & 0xFF, (*RGBAColor >> 8) & 0xFF, (*RGBAColor) & 0xFF, *gRGBAColor >> 24, (*gRGBAColor >> 16) & 0xFF, (*gRGBAColor >> 8) & 0xFF, *gRGBAColor & 0xFF, BaseColorPoint, GradColorPoint) {
		delete RGBAColor;
		delete gRGBAColor;
	}
	BiColoredDottedSymbol(string RenderWay, float Xpos, float Ypos, float XUnitSize, float YUnitSize, BYTE LineWidth,
		DWORD *RGBAColor, DWORD *gRGBAColor,
		BYTE BaseColorPoint = 5, BYTE GradColorPoint = 8) : BiColoredDottedSymbol(RenderWay, Xpos, Ypos, XUnitSize, YUnitSize, LineWidth, *RGBAColor >> 24, (*RGBAColor >> 16) & 0xFF, (*RGBAColor >> 8) & 0xFF, (*RGBAColor) & 0xFF, *gRGBAColor >> 24, (*gRGBAColor >> 16) & 0xFF, (*gRGBAColor >> 8) & 0xFF, *gRGBAColor & 0xFF, BaseColorPoint, GradColorPoint) {
		delete RGBAColor;
		delete gRGBAColor;
	}
	void RefillGradient(BYTE Red = 255, BYTE Green = 255, BYTE Blue = 255, BYTE Alpha = 255,
		BYTE gRed = 255, BYTE gGreen = 255, BYTE gBlue = 255, BYTE gAlpha = 255,
		BYTE BaseColorPoint = 5, BYTE GradColorPoint = 8) override {
		float xbase = (((float)(BaseColorPoint % 3)) - 1.f), ybase = (((float)(BaseColorPoint / 3)) - 1.f),
			xgrad = (((float)(GradColorPoint % 3)) - 1.f), ygrad = (((float)(GradColorPoint / 3)) - 1.f);
		float ax = xgrad - xbase, ay = ygrad - ybase, t;
		float ial = 1.f / (ax*ax + ay * ay);
		///R
		for (int x = -1; x <= 1; x++) {
			for (int y = -1; y <= 1; y++) {
				t = (ax * (x - xbase) + ay * (y - ybase)) * ial;
				t = (Red * t + (1.f - t)*gRed);
				if (t < 0)t = 0;
				if (t > 255)t = 255;
				gR[(x + 1) + (3 * (y + 1))] = round(t);
			}
		}
		///G
		for (int x = -1; x <= 1; x++) {
			for (int y = -1; y <= 1; y++) {
				t = (ax * (x - xbase) + ay * (y - ybase)) * ial;
				t = (Green * t + (1.f - t)*gGreen);
				if (t < 0)t = 0;
				if (t > 255)t = 255;
				gG[(x + 1) + (3 * (y + 1))] = round(t);
			}
		}
		///B
		for (int x = -1; x <= 1; x++) {
			for (int y = -1; y <= 1; y++) {
				t = (ax * (x - xbase) + ay * (y - ybase)) * ial;
				t = (Blue * t + (1.f - t)*gBlue);
				if (t < 0)t = 0;
				if (t > 255)t = 255;
				gB[(x + 1) + (3 * (y + 1))] = round(t);
			}
		}
		///A
		for (int x = -1; x <= 1; x++) {
			for (int y = -1; y <= 1; y++) {
				t = (ax * (x - xbase) + ay * (y - ybase)) * ial;
				t = (Alpha * t + (1.f - t)*gAlpha);
				if (t < 0)t = 0;
				if (t > 255)t = 255;
				gA[(x + 1) + (3 * (y + 1))] = round(t);
			}
		}
	}
	void RefillGradient(DWORD *RGBAColor,DWORD *gRGBAColor,BYTE BaseColorPoint, BYTE GradColorPoint) override {
		RefillGradient(*RGBAColor >> 24, (*RGBAColor >> 16) & 0xFF, (*RGBAColor >> 8) & 0xFF, (*RGBAColor) & 0xFF, *gRGBAColor >> 24, (*gRGBAColor >> 16) & 0xFF, (*gRGBAColor >> 8) & 0xFF, *gRGBAColor & 0xFF, BaseColorPoint, GradColorPoint);
		delete RGBAColor;
		delete gRGBAColor;
	}
	void Draw() override {
		if (RenderWay == " ")return;
		float VerticalFlag = 0.; 
		CHAR BACK = 0;
		if (RenderWay.back() == '#' || RenderWay.back() == '~') {
			BACK = RenderWay.back();
			RenderWay.back() = ' ';
			switch (BACK) {
			case '#':
				VerticalFlag = Points->y - Points[3].y;
				break;
			case '~':
				VerticalFlag = (Points->y - Points[3].y) / 2;
				break;
			}
		}
		if (RenderWay.back() == '#') {
			RenderWay.back() = ' ';
			VerticalFlag = Points->y - Points[4].y;
		}
		BYTE IO;
		glLineWidth(LineWidth);
		glPointSize(LineWidth);
		glBegin(GL_LINE_STRIP);
		for (int i = 0; i < RenderWay.length(); i++) {
			if (RenderWay[i] == ' ') {
				glEnd();
				glBegin(GL_LINE_STRIP);
				continue;
			}
			IO = RenderWay[i] - '1';
			//printf("%x %x %x %x\n", gR[IO], gG[IO], gB[IO], gA[IO]);
			glColor4ub(gR[IO], gG[IO], gB[IO], gA[IO]);
			glVertex2f(Xpos + Points[IO].x, Ypos + Points[IO].y + VerticalFlag);
		}
		glEnd();
		glBegin(GL_POINTS);
		for (int i = 0; i < PointPlacement.size(); ++i){
			IO = PointPlacement[i] - '1';
			glColor4ub(gR[IO], gG[IO], gB[IO], gA[IO]);
			glVertex2f(Xpos + Points[IO].x, Ypos + Points[IO].y + VerticalFlag);
		}
		glEnd();
		if (BACK)RenderWay.back() = BACK;
	}
};

struct SingleTextLine {
	string _CurrentText;
	float CXpos, CYpos;
	float SpaceWidth;
	DWORD RGBAColor,gRGBAColor;
	float CalculatedWidth, CalculatedHeight;
	BIT isBicolored, isListedFont;
	float _XUnitSize,_YUnitSize;
	vector<DottedSymbol*> Chars;
	~SingleTextLine() {
		for (auto i = Chars.begin(); i != Chars.end(); i++)
			if(*i)delete *i;
		Chars.clear();
	}
	SingleTextLine(string Text, float CXpos, float CYpos, float XUnitSize, float YUnitSize, float SpaceWidth, BYTE LineWidth = 2, DWORD RGBAColor=0xFFFFFFFF,DWORD *RGBAGradColor=NULL, BYTE OrigNGradPoints=((5<<4)|5), bool isListedFont = false) {
		if (!Text.size())Text = " ";
		this->_CurrentText = Text;
		CalculatedHeight = 2 * YUnitSize;
		CalculatedWidth = Text.size()*2.f*XUnitSize + (Text.size() - 1)*SpaceWidth;
		this->CXpos = CXpos;
		this->CYpos = CYpos;
		this->RGBAColor = RGBAColor;
		this->SpaceWidth = SpaceWidth;
		this->_XUnitSize = XUnitSize;
		this->_YUnitSize = YUnitSize;
		this->isListedFont = isListedFont;
		float CharXPosition = CXpos - (CalculatedWidth*0.5) + XUnitSize, CharXPosIncrement = 2.f*XUnitSize + SpaceWidth;
		for (int i = 0; i < Text.size(); i++) {
			if (!RGBAGradColor && !isListedFont)Chars.push_back(
				new DottedSymbol(Text[i], CharXPosition, CYpos, XUnitSize, YUnitSize, LineWidth, new DWORD(RGBAColor))
			);
			else if (!isListedFont) Chars.push_back(
				new BiColoredDottedSymbol(Text[i], CharXPosition, CYpos, XUnitSize, YUnitSize, LineWidth, new DWORD(RGBAColor), new DWORD(*RGBAGradColor), (BYTE)(OrigNGradPoints >> 4), (BYTE)(OrigNGradPoints & 0xF))
			);
			else Chars.push_back(
				new lFontSymbol(Text[i], CharXPosition, CYpos, XUnitSize, YUnitSize, RGBAColor)
			);
			CharXPosition += CharXPosIncrement;
		}
		if (RGBAGradColor) {
			this->isBicolored = true;
			this->gRGBAColor = *RGBAGradColor;
			delete RGBAGradColor;
		}
		else
			this->isBicolored = false;
	}
	void SafeColorChange(DWORD NewRGBAColor) {
		if (isBicolored) {
			SafeColorChange(NewRGBAColor, gRGBAColor,
				((BiColoredDottedSymbol*)(this->Chars.front()))->_PointData >> 4,
				((BiColoredDottedSymbol*)(this->Chars.front()))->_PointData & 0xF
			);
			return;
		}
		BYTE R = (NewRGBAColor >> 24), G = (NewRGBAColor >> 16) & 0xFF, B = (NewRGBAColor >> 8) & 0xFF, A = (NewRGBAColor) & 0xFF;
		RGBAColor = NewRGBAColor;
		for (int i = 0; i < Chars.size(); i++) {
			Chars[i]->R = R;
			Chars[i]->G = G;
			Chars[i]->B = B;
			Chars[i]->A = A;
		}
	}
	void SafeColorChange(DWORD NewBaseRGBAColor, DWORD NewGRGBAColor,BYTE BasePoint, BYTE gPoint) {
		if (!isBicolored) {
			SafeColorChange(NewBaseRGBAColor);
			return;
		}
		for (int i = 0; i < Chars.size(); i++) {
			Chars[i]->RefillGradient(new DWORD(NewBaseRGBAColor), new DWORD(NewGRGBAColor), BasePoint, gPoint);
		}
	}
	void SafeChangePosition(float NewCXPos, float NewCYPos) {
		NewCXPos = NewCXPos - CXpos;
		NewCYPos = NewCYPos - CYpos;
		SafeMove(NewCXPos, NewCYPos);
	}
	void SafeMove(float dx, float dy) {
		CXpos += dx;
		CYpos += dy;
		for (int i = 0; i < Chars.size(); i++)
			Chars[i]->SafeCharMove(dx, dy);
	}
	BIT SafeReplaceChar(int i,char CH) {
		if (i >= Chars.size())return 0;
		if (isListedFont) {
			((lFontSymbol*)Chars[i])->Symb = CH;
		}
		else {
			Chars[i]->RenderWay = ASCII[CH];
			Chars[i]->UpdatePointPlacementPositions();
		}
		return 1;
	}
	BIT SafeReplaceChar(int i,string CHrenderway) {
		if (i >= Chars.size())return 0;
		if (isListedFont) return 0;
		Chars[i]->RenderWay = CHrenderway;
		Chars[i]->UpdatePointPlacementPositions();
		return 1;
	}
	void RecalculateWidth() {
		CalculatedWidth = Chars.size()*(2.f*_XUnitSize) + (Chars.size() - 1)*SpaceWidth;
		float CharXPosition = CXpos - (CalculatedWidth*0.5f) + _XUnitSize, CharXPosIncrement = 2.f*_XUnitSize + SpaceWidth;
		for (int i = 0; i < Chars.size(); i++) {
			Chars[i]->Xpos = CharXPosition;
			CharXPosition += CharXPosIncrement;
		}
	}
	void SafeChangePosition_Argumented(BYTE Arg,float newX,float newY) {
		///STL_CHANGE_POSITION_ARGUMENT_LEFT
		float CW = 0.5f*(
				(INT32)((BIT)(GLOBAL_LEFT&Arg))
			-	(INT32)((BIT)(GLOBAL_RIGHT&Arg))
			)*CalculatedWidth,
			CH = 0.5f*(
				(INT32)((BIT)(GLOBAL_BOTTOM&Arg))
			-	(INT32)((BIT)(GLOBAL_TOP&Arg))
			)*CalculatedHeight;
		SafeChangePosition(newX + CW, newY + CH);
	}
	void SafeStringReplace(string NewString) {
		if (!NewString.size()) NewString = " ";
		_CurrentText = NewString;
		while (NewString.size() > Chars.size()) {
			if (isBicolored)
				Chars.push_back(new BiColoredDottedSymbol((*((BiColoredDottedSymbol*)(Chars.front())))));
			else if (isListedFont)
				Chars.push_back(new lFontSymbol(*((lFontSymbol*)(Chars.front()))));
			else
				Chars.push_back(new DottedSymbol(*(Chars.front())));
		}
		while (NewString.size() < Chars.size()) Chars.pop_back();
		for (int i = 0; i < Chars.size(); i++)
			SafeReplaceChar(i, NewString[i]);
		RecalculateWidth();
	}
	void Draw() {
		for (int i = 0; i < Chars.size(); i++) {
			Chars[i]->Draw();
		}
	}
};

#define CharWidthPerHeight_Fonted 0.666f
#define CharWidthPerHeight 0.5f
#define CharSpaceBetween(CharHeight) CharHeight/2.f
#define CharLineWidth(CharHeight) ceil(CharHeight/7.5f)
struct SingleTextLineSettings {
	string STLstring;
	float CXpos, CYpos, XUnitSize, YUnitSize;
	BYTE BasePoint, GradPoint, LineWidth, SpaceWidth;
	BIT isFonted;
	DWORD RGBAColor, gRGBAColor;
	SingleTextLineSettings(string Text, float CXpos, float CYpos, float XUnitSize, float YUnitSize, BYTE LineWidth, BYTE SpaceWidth, DWORD RGBAColor, DWORD gRGBAColor, BYTE BasePoint, BYTE GradPoint) {
		this->STLstring = Text;
		this->CXpos = CXpos;
		this->CYpos = CYpos;
		this->XUnitSize = XUnitSize;
		this->YUnitSize = YUnitSize;
		this->RGBAColor = RGBAColor;
		this->gRGBAColor = gRGBAColor;
		this->LineWidth = LineWidth;
		this->SpaceWidth = SpaceWidth;
		this->BasePoint = BasePoint;
		this->GradPoint = GradPoint;
		this->isFonted = 0;
	}
	SingleTextLineSettings(string Text, float CXpos, float CYpos, float XUnitSize, float YUnitSize, BYTE LineWidth, BYTE SpaceWidth, DWORD RGBAColor) :
		SingleTextLineSettings(Text, CXpos, CYpos, XUnitSize, YUnitSize, LineWidth, SpaceWidth, RGBAColor, 0, 255, 255) {}
	SingleTextLineSettings(string Text, float CXpos, float CYpos, float CharHeight, DWORD RGBAColor, DWORD gRGBAColor, BYTE BasePoint, BYTE GradPoint) :
		SingleTextLineSettings(Text, CXpos, CYpos, CharHeight* CharWidthPerHeight / 2, CharHeight / 2, CharLineWidth(CharHeight), CharSpaceBetween(CharHeight), RGBAColor, gRGBAColor, BasePoint, GradPoint) {}
	SingleTextLineSettings(string Text, float CXpos, float CYpos, float CharHeight, DWORD RGBAColor) :
		SingleTextLineSettings(Text, CXpos, CYpos, CharHeight, RGBAColor, 0, 255, 255) {}
	SingleTextLineSettings(float XUnitSize, float YUnitSize, DWORD RGBAColor) :
		SingleTextLineSettings("_", 0, 0, YUnitSize, RGBAColor) {
		this->isFonted = 1;
		this->XUnitSize = XUnitSize;
		this->YUnitSize = YUnitSize;
		this->RGBAColor = RGBAColor;
	}
	SingleTextLineSettings(float CharHeight, DWORD RGBAColor) :
		SingleTextLineSettings(CharHeight* CharWidthPerHeight / 4, CharHeight / 2, RGBAColor) {}
	SingleTextLineSettings(SingleTextLine* Example, BIT KeepText = false) :
		SingleTextLineSettings(
		((KeepText) ? Example->_CurrentText : " "), Example->CXpos, Example->CYpos, Example->_XUnitSize, Example->_YUnitSize, Example->Chars.front()->LineWidth, Example->SpaceWidth, Example->RGBAColor, Example->gRGBAColor,
			((Example->isBicolored) ? (((BiColoredDottedSymbol*)(Example->Chars.front()))->_PointData & 0xF0) >> 4 : 0xF), ((Example->isBicolored) ? (((BiColoredDottedSymbol*)(Example->Chars.front()))->_PointData) & 0xF : 0xF)
	) {
		this->isFonted = Example->isListedFont;
	}
	SingleTextLine* CreateOne() {
		if (GradPoint & 0xF0 && !isFonted)
			return new SingleTextLine(STLstring, CXpos, CYpos, XUnitSize, YUnitSize, SpaceWidth, LineWidth, RGBAColor);
		if(!isFonted) return new SingleTextLine(STLstring, CXpos, CYpos, XUnitSize, YUnitSize, SpaceWidth, LineWidth, RGBAColor, new DWORD(gRGBAColor), ((BasePoint & 0xF) << 4) | (GradPoint & 0xF));
		return new SingleTextLine(STLstring, CXpos, CYpos, XUnitSize, YUnitSize, SpaceWidth, LineWidth, RGBAColor, nullptr, 0xF, true);
	}
	SingleTextLine* CreateOne(string TextOverride) {
		if (GradPoint & 0xF0 && !isFonted)
			return new SingleTextLine(TextOverride, CXpos, CYpos, XUnitSize, YUnitSize, SpaceWidth, LineWidth, RGBAColor);
		if (!isFonted) return new SingleTextLine(TextOverride, CXpos, CYpos, XUnitSize, YUnitSize, SpaceWidth, LineWidth, RGBAColor, new DWORD(gRGBAColor), ((BasePoint & 0xF) << 4) | (GradPoint & 0xF));
		return new SingleTextLine(TextOverride, CXpos, CYpos, XUnitSize, YUnitSize, SpaceWidth, LineWidth, RGBAColor, nullptr, 0xF, true);
	}
	void SetNewPos(float NewXPos, float NewYPos) {
		this->CXpos = NewXPos;
		this->CYpos = NewYPos;
	}
	void Move(float dx, float dy) {
		this->CXpos += dx;
		this->CYpos += dy;
	}
};

struct HandleableUIPart {
	recursive_mutex Lock;
	~HandleableUIPart() {}
	HandleableUIPart() {}
	BIT virtual MouseHandler(float mx, float my, CHAR Button/*-1 left, 1 right*/,CHAR State /*-1 down, 1 up*/) = 0;
	void virtual Draw() = 0;
	void virtual SafeMove(float, float) = 0;
	void virtual SafeChangePosition(float, float) = 0;
	void virtual SafeChangePosition_Argumented(BYTE, float, float) = 0; 
	void virtual SafeStringReplace(string) = 0;
	void virtual KeyboardHandler(char CH) = 0;
	inline DWORD virtual TellType() {
		return TT_UNSPECIFIED;
	}
};

struct CheckBox : HandleableUIPart {///NeedsTest
	float Xpos, Ypos, SideSize;
	DWORD BorderRGBAColor, UncheckedRGBABackground, CheckedRGBABackground;
	SingleTextLine *Tip;
	BIT State,Focused;
	BYTE BorderWidth;
	~CheckBox() {
		Lock.lock();
		if (Tip)delete Tip;
		Lock.unlock();
	}
	CheckBox(float Xpos,float Ypos,float SideSize, DWORD BorderRGBAColor,DWORD UncheckedRGBABackground, DWORD CheckedRGBABackground, BYTE BorderWidth, BIT StartState=false, SingleTextLineSettings *TipSettings=NULL, _Align TipAlign = _Align::left, string TipText=" ") {
		this->Xpos = Xpos;
		this->Ypos = Ypos;
		this->SideSize = SideSize;
		this->BorderRGBAColor = BorderRGBAColor;
		this->UncheckedRGBABackground = UncheckedRGBABackground;
		this->CheckedRGBABackground = CheckedRGBABackground;
		this->State = StartState;
		this->Focused = 0;
		this->BorderWidth = BorderWidth; 
		if (TipSettings) {
			this->Tip = TipSettings->CreateOne(TipText);
			this->Tip->SafeChangePosition_Argumented(TipAlign, Xpos - ((TipAlign == _Align::left) ? 0.5f : ((TipAlign == _Align::right) ? -0.5f : 0))*SideSize, Ypos - SideSize);
		}
	}
	void Draw() override {
		Lock.lock();
		float hSideSize = 0.5f*SideSize;
		if (State)
			GLCOLOR(CheckedRGBABackground);
		else
			GLCOLOR(UncheckedRGBABackground);
		glBegin(GL_QUADS);
		glVertex2f(Xpos + hSideSize, Ypos + hSideSize);
		glVertex2f(Xpos - hSideSize, Ypos + hSideSize);
		glVertex2f(Xpos - hSideSize, Ypos - hSideSize);
		glVertex2f(Xpos + hSideSize, Ypos - hSideSize);
		glEnd();
		if ((BYTE)BorderRGBAColor && BorderWidth) {
			GLCOLOR(BorderRGBAColor);
			glLineWidth(BorderWidth);
			glBegin(GL_LINE_LOOP);
			glVertex2f(Xpos + hSideSize, Ypos + hSideSize);
			glVertex2f(Xpos - hSideSize, Ypos + hSideSize);
			glVertex2f(Xpos - hSideSize, Ypos - hSideSize);
			glVertex2f(Xpos + hSideSize, Ypos - hSideSize);
			glEnd();
			glPointSize(BorderWidth);
			glBegin(GL_POINTS);
			glVertex2f(Xpos + hSideSize, Ypos + hSideSize);
			glVertex2f(Xpos - hSideSize, Ypos + hSideSize);
			glVertex2f(Xpos - hSideSize, Ypos - hSideSize);
			glVertex2f(Xpos + hSideSize, Ypos - hSideSize);
			glEnd();
		}
		if (Focused && Tip)Tip->Draw();
		Lock.unlock();
	}
	void SafeMove(float dx, float dy) override {
		Lock.lock();
		Xpos += dx;
		Ypos += dy;
		if (Tip)Tip->SafeMove(dx, dy);
		Lock.unlock();
	}
	void SafeChangePosition(float NewX, float NewY) override {
		Lock.lock();
		NewX -= Xpos;
		NewY -= Ypos;
		SafeMove(NewX, NewY);
		Lock.unlock();
	}
	void SafeChangePosition_Argumented(BYTE Arg, float NewX, float NewY) override {
		Lock.lock();
		float CW = 0.5f*(
			(INT32)((BIT)(GLOBAL_LEFT&Arg))
			- (INT32)((BIT)(GLOBAL_RIGHT&Arg))
			)*SideSize,
			CH = 0.5f*(
			(INT32)((BIT)(GLOBAL_BOTTOM&Arg))
				- (INT32)((BIT)(GLOBAL_TOP&Arg))
				)*SideSize;
		SafeChangePosition(NewX + CW, NewY + CH);
		Lock.unlock();
	}
	void KeyboardHandler(CHAR CH) override {
		return;
	}
	void SafeStringReplace(string TipString) override {
		Lock.lock();
		if (Tip)Tip->SafeStringReplace(TipString);
		Lock.unlock();
	}
	void FocusChange() {
		Lock.lock();
		this->Focused = !this->Focused;
		BorderRGBAColor = (((~(BorderRGBAColor >> 8)) << 8) | (BorderRGBAColor & 0xFF));
		Lock.unlock();
	}
	BIT MouseHandler(float mx, float my, CHAR Button, CHAR State) override {
		Lock.lock();
		if (fabsf(mx - Xpos) < 0.5*SideSize && fabsf(my - Ypos) < 0.5*SideSize) {
			if (!Focused)
				FocusChange();
			if (Button) {
				//cout << "State switch from " << State << endl;
				if (State == 1)
					this->State = !this->State;
				Lock.unlock();
				return 1;
			}
			else { 
				Lock.unlock();
				return 0; 
			}
		}
		else {
			if (Focused)
				FocusChange();
			Lock.unlock();
			return 0;
		}
	}
	inline DWORD TellType() override {
		return _TellType::checkbox;
	}
};

struct InputField : HandleableUIPart {
	enum PassCharsType {
		PassNumbers = 0b1,
		PassFirstPoint = 0b10,
		PassFrontMinusSign = 0b100,
		PassAll = 0xFF
	};
	enum Type {
		NaturalNumbers = PassCharsType::PassNumbers,
		WholeNumbers = PassCharsType::PassNumbers | PassCharsType::PassFrontMinusSign,
		FP_PositiveNumbers = PassCharsType::PassNumbers | PassCharsType::PassFirstPoint,
		FP_Any = PassCharsType::PassNumbers | PassCharsType::PassFirstPoint | PassCharsType::PassFrontMinusSign,
		Text = PassCharsType::PassAll,
	};
	_Align InputAlign, TipAlign;
	Type InputType;
	string CurrentString,DefaultString;
	string *OutputSource;
	DWORD MaxChars,BorderRGBAColor;
	float Xpos, Ypos, Height, Width;
	BIT Focused,FirstInput;
	SingleTextLine *STL;
	SingleTextLine *Tip;
	InputField(string DefaultString ,float Xpos, float Ypos, float Height, float Width, SingleTextLineSettings* DefaultStringSettings, string *OutputSource, DWORD BorderRGBAColor, SingleTextLineSettings* TipLineSettings = NULL, string TipLineText = " ",  DWORD MaxChars = 0, _Align InputAlign = _Align::left,_Align TipAlign= _Align::center, Type InputType = Type::Text) {
		this->DefaultString = DefaultString;
		DefaultStringSettings->SetNewPos(Xpos, Ypos);
		this->STL = DefaultStringSettings->CreateOne(DefaultString); 
		if (TipLineSettings) {
			this->Tip = TipLineSettings->CreateOne(TipLineText);
			this->Tip->SafeChangePosition_Argumented(TipAlign, Xpos - ((TipAlign == _Align::left) ? 0.5f : ((TipAlign == _Align::right) ? -0.5f : 0))*Width, Ypos - Height);
		}
		else this->Tip = NULL;
		this->InputAlign = InputAlign;
		this->InputType = InputType;
		this->TipAlign = TipAlign;

		this->MaxChars = MaxChars;
		this->BorderRGBAColor = BorderRGBAColor; 
		this->Xpos = Xpos;
		this->Ypos = Ypos;
		this->Height = (DefaultStringSettings->YUnitSize * 2 > Height) ? DefaultStringSettings->YUnitSize * 2 : Height;
		this->Width = Width;
		this->CurrentString = "";//DefaultString;
		this->FirstInput = this->Focused = false;
		this->OutputSource = OutputSource;
	}
	BIT MouseHandler(float mx, float my, CHAR Button/*-1 left, 1 right, 0 move*/, CHAR State /*-1 down, 1 up*/) override {
		if (abs(mx - Xpos) < 0.5*Width && abs(my - Ypos) < 0.5*Height) {
			if (!Focused) 
				FocusChange();
			if (Button)return 1;
			else return 0;
		}
		else {
			if (Focused) 
				FocusChange();
			return 0;
		}
	}
	void SafeMove(float dx, float dy) {
		Lock.lock();
		STL->SafeMove(dx,dy);
		if (Tip)Tip->SafeMove(dx, dy);
		Xpos += dx;
		Ypos += dy;
		Lock.unlock();
	}
	void SafeChangePosition(float NewX,float NewY) {
		Lock.lock();
		NewX = Xpos - NewX;
		NewY = Ypos - NewY;
		SafeMove(NewX, NewY);
		Lock.unlock();
	}
	void FocusChange() {
		Lock.lock();
		this->Focused = !this->Focused;
		BorderRGBAColor = (((~(BorderRGBAColor >> 8)) << 8) | (BorderRGBAColor & 0xFF));
		Lock.unlock();
	}
	void UpdateInputString(string NewString="") {
		Lock.lock();
		if(NewString.size())CurrentString = "";
		float x = Xpos - ((InputAlign == _Align::left) ? 1 : ((InputAlign == _Align::right) ? -1 : 0))*(0.5f*Width - STL->_XUnitSize);
		this->STL->SafeStringReplace((NewString.size())?NewString.substr(0,this->MaxChars):CurrentString);
		this->STL->SafeChangePosition_Argumented(InputAlign, x, Ypos);
		Lock.unlock();
	}
	void BackSpace() {
		Lock.lock();
		ProcessFirstInput();
		if (CurrentString.size()) {
			CurrentString.pop_back();
			UpdateInputString();
		}
		else {
			this->STL->SafeStringReplace(" ");
		}
		Lock.unlock();
	}
	void FlushCurrentStringWithoutGUIUpdate(BIT SetDefault = false) {
		Lock.lock();
		this->CurrentString = (SetDefault) ? this->DefaultString : "";
		Lock.unlock();
	}
	void PutIntoSource(string *AnotherSource=NULL) {
		Lock.lock();
		if (OutputSource) {
			if(CurrentString.size())
				*OutputSource = CurrentString;
		}
		else if (AnotherSource)
			if (CurrentString.size())
				*AnotherSource = CurrentString;
		Lock.unlock();
	}
	void ProcessFirstInput() {
		Lock.lock();
		if (FirstInput) {
			FirstInput = 0;
			CurrentString = "";
		}
		Lock.unlock();
	}
	void KeyboardHandler(char CH) {
		Lock.lock();
		if (Focused) {
			if (CH >= 32) {
				if (InputType & PassCharsType::PassNumbers) {
					if (CH >= '0' && CH <= '9') {
						Input(CH);
						Lock.unlock();
						return;
					}
				}
				if (InputType & PassCharsType::PassFrontMinusSign) {
					if (CH == '-' && CurrentString.empty()) {
						Input(CH);
						Lock.unlock();
						return;
					}
				}
				if (InputType & PassCharsType::PassFirstPoint) {
					if (CH == '.' && CurrentString.find('.') >= CurrentString.size()) {
						Input(CH);
						Lock.unlock();
						return;
					}
				}

				if(InputType == PassCharsType::PassAll)Input(CH);
			}
			else if (CH == 13)PutIntoSource();
			else if (CH == 8)BackSpace();
		}
		Lock.unlock();
	}
	void Input(char CH) {
		Lock.lock();
		ProcessFirstInput();
		if (!MaxChars || CurrentString.size()<MaxChars) {
			CurrentString.push_back(CH);
			UpdateInputString();
		}
		Lock.unlock();
	}
	void SafeChangePosition_Argumented(BYTE Arg, float NewX, float NewY) {
		Lock.lock();
		float CW = 0.5f*(
			(INT32)((BIT)(GLOBAL_LEFT&Arg))
			- (INT32)((BIT)(GLOBAL_RIGHT&Arg))
			)*Width,
			CH = 0.5f*(
			(INT32)((BIT)(GLOBAL_BOTTOM&Arg))
				- (INT32)((BIT)(GLOBAL_TOP&Arg))
				)*Height;
		SafeChangePosition(NewX + CW, NewY + CH);
		Lock.unlock();
	}
	void SafeStringReplace(string NewString) override {
		Lock.lock();
		CurrentString = NewString.substr(0, this->MaxChars);
		UpdateInputString(NewString);
		FirstInput = 1;
		Lock.unlock();
	}
	void Draw() override {
		Lock.lock();
		GLCOLOR(BorderRGBAColor);
		glLineWidth(1);
		glBegin(GL_LINE_LOOP);
		glVertex2f(Xpos + 0.5f*Width, Ypos + 0.5f*Height);
		glVertex2f(Xpos - 0.5f*Width, Ypos + 0.5f*Height);
		glVertex2f(Xpos - 0.5f*Width, Ypos - 0.5f*Height);
		glVertex2f(Xpos + 0.5f*Width, Ypos - 0.5f*Height);
		glEnd();
		this->STL->Draw();
		if (Focused && Tip)Tip->Draw();
		Lock.unlock();
	}
	inline DWORD TellType() override {
		return TT_INPUT_FIELD;
	}
};

struct Button : HandleableUIPart {
	SingleTextLine *STL,*Tip;
	float Xpos, Ypos;
	float Width, Height;
	DWORD RGBAColor, RGBABackground, RGBABorder;
	DWORD HoveredRGBAColor, HoveredRGBABackground, HoveredRGBABorder;
	BYTE BorderWidth;
	BIT Hovered;
	void(*OnClick)();
	~Button() {
		delete STL;
		if (Tip)delete Tip;
	}
	Button(string ButtonText, void(*OnClick)(), float Xpos, float Ypos, float Width, float Height, float CharHeight, DWORD RGBAColor, DWORD gRGBAColor, BYTE BasePoint/*15 if gradient is disabled*/, BYTE GradPoint, BYTE BorderWidth,DWORD RGBABackground, DWORD RGBABorder, DWORD HoveredRGBAColor, DWORD HoveredRGBABackground, DWORD HoveredRGBABorder, SingleTextLineSettings *Tip, string TipText=" ") {
		SingleTextLineSettings STLS(ButtonText, Xpos, Ypos, CharHeight, RGBAColor, gRGBAColor, BasePoint, GradPoint);
		this->STL = STLS.CreateOne();
		if (Tip) {
			Tip->SetNewPos(Xpos, Ypos - Height);
			this->Tip = Tip->CreateOne(TipText);
		}
		else this->Tip = NULL;
		this->BorderWidth = BorderWidth;
		this->Xpos = Xpos;
		this->Ypos = Ypos;
		this->Width = Width;
		this->Height = Height;
		this->RGBAColor = RGBAColor;
		this->RGBABorder = RGBABorder;
		this->RGBABackground = RGBABackground;
		this->HoveredRGBAColor = HoveredRGBAColor;
		this->HoveredRGBABorder = HoveredRGBABorder;
		this->HoveredRGBABackground = HoveredRGBABackground;
		this->Hovered = 0;
		this->OnClick = OnClick;
	}
	Button(string ButtonText,SingleTextLineSettings *ButtonTextSTLS, void(*OnClick)(), float Xpos, float Ypos, float Width, float Height, BYTE BorderWidth, DWORD RGBABackground, DWORD RGBABorder, DWORD HoveredRGBAColor, DWORD HoveredRGBABackground, DWORD HoveredRGBABorder, SingleTextLineSettings *Tip,string TipText = " ") {
		ButtonTextSTLS->SetNewPos(Xpos, Ypos);
		this->STL = ButtonTextSTLS->CreateOne(ButtonText);
		if (Tip) {
			Tip->SetNewPos(Xpos, Ypos - Height);
			this->Tip = Tip->CreateOne(TipText);
		}
		else this->Tip = NULL;
		this->BorderWidth = BorderWidth;
		this->Xpos = Xpos;
		this->Ypos = Ypos;
		this->Width = Width;
		this->Height = Height;
		this->RGBAColor = ButtonTextSTLS->RGBAColor;
		this->RGBABorder = RGBABorder;
		this->RGBABackground = RGBABackground;
		this->HoveredRGBAColor = HoveredRGBAColor;
		this->HoveredRGBABorder = HoveredRGBABorder;
		this->HoveredRGBABackground = HoveredRGBABackground;
		this->Hovered = 0;
		this->OnClick = OnClick;
	}
	BIT MouseHandler(float mx, float my, CHAR Button/*-1 left, 1 right, 0 move*/, CHAR State /*-1 down, 1 up*/)  override {
		Lock.lock();
		mx = Xpos - mx;
		my = Ypos - my;
		if (Hovered) {
			if (fabsf(mx) > Width*0.5 || fabsf(my) > Height*0.5) {
				//cout << "UNHOVERED\n";
				Hovered = 0;
				STL->SafeColorChange(RGBAColor);
			}
			else {
				SetCursor(HandCursor);
				if (Button && State==1) {

					if(Button == -1 && OnClick)OnClick();
					Lock.unlock();
					return 1;
				}

			}
		}
		else {
			if (fabsf(mx) <= Width*0.5 && fabsf(my) <= Height*0.5) {
				Hovered = 1;
				STL->SafeColorChange(HoveredRGBAColor);
			}
		}
		Lock.unlock();
		return 0;
	}
	void SafeMove(float dx, float dy) {
		Lock.lock();
		if (Tip)Tip->SafeMove(dx, dy);
		STL->SafeMove(dx, dy);
		Xpos += dx;
		Ypos += dy;
		Lock.unlock();
	}
	void KeyboardHandler(char CH) override {
		return;
	}
	void SafeStringReplace(string NewString) override {
		Lock.lock();
		this->STL->SafeStringReplace(NewString);
		Lock.unlock();
	}
	void SafeChangePosition(float NewX, float NewY) {
		Lock.lock();
		NewX -= Xpos;
		NewY -= Ypos;
		SafeMove(NewX, NewY);
		Lock.unlock();
	}
	void SafeChangePosition_Argumented(BYTE Arg, float NewX, float NewY) {
		Lock.lock();
		float CW = 0.5f*(
				(INT32)((BIT)(GLOBAL_LEFT&Arg))
			-	(INT32)((BIT)(GLOBAL_RIGHT&Arg))
			)*Width,
			CH = 0.5f*(
				(INT32)((BIT)(GLOBAL_BOTTOM&Arg))
			-	(INT32)((BIT)(GLOBAL_TOP&Arg))
			)*Height;
		SafeChangePosition(NewX + CW, NewY + CH);
		Lock.unlock();
	}
	void Draw() {
		Lock.lock();
		if (Hovered) {
			if ((BYTE)HoveredRGBABackground) {
				GLCOLOR(HoveredRGBABackground);
				glBegin(GL_QUADS);
				glVertex2f(Xpos - Width * 0.5f, Ypos + 0.5f*Height);
				glVertex2f(Xpos + Width * 0.5f, Ypos + 0.5f*Height);
				glVertex2f(Xpos + Width * 0.5f, Ypos - 0.5f*Height);
				glVertex2f(Xpos - Width * 0.5f, Ypos - 0.5f*Height);
				glEnd();
			}
			if ((BYTE)HoveredRGBABorder) {
				GLCOLOR(HoveredRGBABorder);
				glLineWidth(BorderWidth);
				glBegin(GL_LINE_LOOP);
				glVertex2f(Xpos - Width * 0.5f, Ypos + 0.5f*Height);
				glVertex2f(Xpos + Width * 0.5f, Ypos + 0.5f*Height);
				glVertex2f(Xpos + Width * 0.5f, Ypos - 0.5f*Height);
				glVertex2f(Xpos - Width * 0.5f, Ypos - 0.5f*Height);
				glEnd();
			}
		}
		else {
			if ((BYTE)RGBABackground) {
				GLCOLOR(RGBABackground);
				glBegin(GL_QUADS);
				glVertex2f(Xpos - Width * 0.5f, Ypos + 0.5f*Height);
				glVertex2f(Xpos + Width * 0.5f, Ypos + 0.5f*Height);
				glVertex2f(Xpos + Width * 0.5f, Ypos - 0.5f*Height);
				glVertex2f(Xpos - Width * 0.5f, Ypos - 0.5f*Height);
				glEnd();
			}
			if ((BYTE)RGBABorder) {
				GLCOLOR(RGBABorder);
				glLineWidth(BorderWidth);
				glBegin(GL_LINE_LOOP);
				glVertex2f(Xpos - Width * 0.5f, Ypos + 0.5f*Height);
				glVertex2f(Xpos + Width * 0.5f, Ypos + 0.5f*Height);
				glVertex2f(Xpos + Width * 0.5f, Ypos - 0.5f*Height);
				glVertex2f(Xpos - Width * 0.5f, Ypos - 0.5f*Height);
				glEnd();
			}
		}
		if (Tip && Hovered)Tip->Draw();
		STL->Draw();
		Lock.unlock();
	}
	inline DWORD TellType() override {
		return TT_BUTTON;
	}
};

struct ButtonSettings {
	string ButtonText,TipText; 
	void(*OnClick)();
	float Xpos, Ypos, Width, Height, CharHeight;
	DWORD RGBAColor, gRGBAColor,RGBABackground,RGBABorder, HoveredRGBAColor,HoveredRGBABackground,HoveredRGBABorder;
	BYTE BasePoint, GradPoint,BorderWidth;
	BIT STLSBasedSettings;
	SingleTextLineSettings *Tip, *STLS;
	ButtonSettings(string ButtonText, void(*OnClick)(), float Xpos, float Ypos, float Width, float Height, float CharHeight, DWORD RGBAColor, DWORD gRGBAColor, BYTE BasePoint, BYTE GradPoint, BYTE BorderWidth, DWORD RGBABackground, DWORD RGBABorder, DWORD HoveredRGBAColor, DWORD HoveredRGBABackground, DWORD HoveredRGBABorder, SingleTextLineSettings *Tip, string TipText) {
		this->STLSBasedSettings = 0;
		this->ButtonText = ButtonText;
		this->TipText = TipText;
		this->OnClick = OnClick;
		this->Tip = Tip;
		this->Xpos = Xpos;
		this->Ypos = Ypos;
		this->Width = Width;
		this->Height = Height;
		this->CharHeight = CharHeight;
		this->RGBAColor = RGBAColor;
		this->gRGBAColor = gRGBAColor;
		this->BasePoint = BasePoint;
		this->GradPoint = GradPoint;
		this->BorderWidth = BorderWidth;
		this->RGBABackground = RGBABackground;
		this->RGBABorder = RGBABorder;
		this->HoveredRGBABackground = HoveredRGBABackground;
		this->HoveredRGBABorder = HoveredRGBABorder;
	}
	ButtonSettings(string ButtonText, SingleTextLineSettings *ButtonTextSTLS, void(*OnClick)(), float Xpos, float Ypos, float Width, float Height, BYTE BorderWidth, DWORD RGBABackground, DWORD RGBABorder, DWORD HoveredRGBAColor, DWORD HoveredRGBABackground, DWORD HoveredRGBABorder, SingleTextLineSettings *Tip, string TipText = " ") {
		this->STLSBasedSettings = 1;
		this->ButtonText = ButtonText;
		this->TipText = TipText;
		this->OnClick = OnClick;
		this->Tip = Tip;
		this->Xpos = Xpos;
		this->Ypos = Ypos;
		this->Width = Width;
		this->Height = Height;
		this->STLS = ButtonTextSTLS;
		this->BorderWidth = BorderWidth;
		this->RGBAColor = ButtonTextSTLS->RGBAColor;
		this->RGBABackground = RGBABackground;
		this->RGBABorder = RGBABorder;
		this->HoveredRGBAColor = HoveredRGBAColor;
		this->HoveredRGBABackground = HoveredRGBABackground;
		this->HoveredRGBABorder = HoveredRGBABorder;
	}
	ButtonSettings(string ButtonText, void(*OnClick)(), float Xpos, float Ypos, float Width, float Height, float CharHeight, DWORD RGBAColor, BYTE BorderWidth, DWORD RGBABackground, DWORD RGBABorder, DWORD HoveredRGBAColor, DWORD HoveredRGBABackground, DWORD HoveredRGBABorder, SingleTextLineSettings *Tip, string TipText) :ButtonSettings(ButtonText, OnClick, Xpos, Ypos, Width, Height, CharHeight, RGBAColor, 0, 15, 15, BorderWidth, RGBABackground, RGBABorder, HoveredRGBAColor, HoveredRGBABackground, HoveredRGBABorder, Tip, TipText) {}
	ButtonSettings(string ButtonText, SingleTextLineSettings *ButtonTextSTLS, void(*OnClick)(), float Xpos, float Ypos, float Width, float Height, BYTE BorderWidth, DWORD RGBABackground, DWORD RGBABorder, DWORD HoveredRGBAColor, DWORD HoveredRGBABackground, DWORD HoveredRGBABorder):ButtonSettings(ButtonText,ButtonTextSTLS,OnClick,Xpos,Ypos,Width,Height,BorderWidth, RGBABackground, RGBABorder, HoveredRGBAColor, HoveredRGBABackground, HoveredRGBABorder, NULL, " "){}
	ButtonSettings(string ButtonText, void(*OnClick)(), float Xpos, float Ypos, float Width, float Height, float CharHeight, DWORD RGBAColor, BYTE BorderWidth, DWORD RGBABackground, DWORD RGBABorder, DWORD HoveredRGBAColor, DWORD HoveredRGBABackground, DWORD HoveredRGBABorder) :ButtonSettings(ButtonText, OnClick, Xpos, Ypos, Width, Height, CharHeight, RGBAColor, 0, 15, 15, BorderWidth, RGBABackground, RGBABorder, HoveredRGBAColor, HoveredRGBABackground, HoveredRGBABorder, NULL, " ") {}

	ButtonSettings(SingleTextLineSettings *ButtonTextSTLS, void(*OnClick)(), float Xpos, float Ypos, float Width, float Height, BYTE BorderWidth, DWORD RGBABackground, DWORD RGBABorder, DWORD HoveredRGBAColor, DWORD HoveredRGBABackground, DWORD HoveredRGBABorder) :ButtonSettings(" ", ButtonTextSTLS, OnClick, Xpos, Ypos, Width, Height, BorderWidth, RGBABackground, RGBABorder, HoveredRGBAColor, HoveredRGBABackground, HoveredRGBABorder, NULL, " ") {}
	ButtonSettings(string ButtonText, void(*OnClick)(), float Xpos, float Ypos, float Width, float Height, float CharHeight, DWORD RGBAColor, DWORD gRGBAColor, BYTE BasePoint, BYTE GradPoint, BYTE BorderWidth, DWORD RGBABackground, DWORD RGBABorder, DWORD HoveredRGBAColor, DWORD HoveredRGBABackground, DWORD HoveredRGBABorder) :ButtonSettings(ButtonText, OnClick, Xpos, Ypos, Width, Height, CharHeight, RGBAColor, gRGBAColor, BasePoint, GradPoint, BorderWidth, RGBABackground, RGBABorder, HoveredRGBAColor, HoveredRGBABackground, HoveredRGBABorder, NULL, " ") {}
	ButtonSettings(SingleTextLineSettings *ButtonTextSTLS, float Xpos, float Ypos, float Width, float Height, BYTE BorderWidth, DWORD RGBABackground, DWORD RGBABorder, DWORD HoveredRGBAColor, DWORD HoveredRGBABackground, DWORD HoveredRGBABorder) :ButtonSettings(" ", ButtonTextSTLS, NULL, Xpos, Ypos, Width, Height, BorderWidth, RGBABackground, RGBABorder, HoveredRGBAColor, HoveredRGBABackground, HoveredRGBABorder, NULL, " ") {}
	ButtonSettings(float Xpos, float Ypos, float Width, float Height, float CharHeight, DWORD RGBAColor, BYTE BorderWidth, DWORD RGBABackground, DWORD RGBABorder, DWORD HoveredRGBAColor, DWORD HoveredRGBABackground, DWORD HoveredRGBABorder) :ButtonSettings(" ", NULL, Xpos, Ypos, Width, Height, CharHeight, RGBAColor, 0, 15, 15, BorderWidth, RGBABackground, RGBABorder, HoveredRGBAColor, HoveredRGBABackground, HoveredRGBABorder, NULL, " ") {}
	ButtonSettings(Button* Example, BIT KeepText=false) {
		this->STLSBasedSettings = 1;
		this->ButtonText = (KeepText)?Example->STL->_CurrentText:" ";
		if(!this->ButtonText.size())this->ButtonText=" ";
		if (Example->Tip) {
			this->Tip = Tip;
			if (TipText.size())
				this->TipText = TipText;
			else
				this->TipText = " ";
		}
		else {
			this->TipText = " ";
			this->Tip = NULL;
		}
		this->OnClick = Example->OnClick;
		this->Xpos = Example->Xpos;
		this->Ypos = Example->Ypos;
		this->Width = Example->Width;
		this->Height = Example->Height;
		this->HoveredRGBAColor = Example->HoveredRGBAColor;
		this->RGBAColor = Example->RGBAColor;
		this->STLS = new SingleTextLineSettings(Example->STL);
		this->gRGBAColor = this->STLS->gRGBAColor;
		this->BorderWidth = Example->BorderWidth;
		this->RGBABackground = Example->RGBABackground;
		this->RGBABorder = Example->RGBABorder;
		this->HoveredRGBABackground = Example->HoveredRGBABackground;
		this->HoveredRGBABorder = Example->HoveredRGBABorder;
	}
	void Move(float dx, float dy) {
		Xpos += dx;
		Ypos += dy;
	}
	void ChangePosition(float NewX, float NewY) {
		NewX -= Xpos;
		NewY -= Ypos;
		Move(NewX, NewY);
	}
	Button* CreateOne(string ButtonText,BIT KeepText=false) {
		if (STLS && STLSBasedSettings) 
			return new Button(((KeepText) ? this->ButtonText : ButtonText), STLS, OnClick, Xpos, Ypos, Width, Height, BorderWidth, RGBABackground, RGBABorder, HoveredRGBAColor, HoveredRGBABackground, HoveredRGBABorder, Tip, TipText);
		else 
			return new Button(((KeepText) ? this->ButtonText : ButtonText), OnClick, Xpos, Ypos, Width, Height, CharHeight, RGBAColor, gRGBAColor, BasePoint, GradPoint, BorderWidth, RGBABackground, RGBABorder, HoveredRGBAColor, HoveredRGBABackground, HoveredRGBABorder, Tip, TipText);
	}
};

struct TextBox : HandleableUIPart {
	enum class VerticalOverflow{cut,display,recalibrate};
	_Align TextAlign;
	VerticalOverflow VOverflow;
	string Text;
	vector<SingleTextLine*> Lines;
	float Xpos, Ypos;
	float Width, Height;
	float VerticalOffset,CalculatedTextHeight;
	SingleTextLineSettings *STLS;
	BYTE BorderWidth;
	DWORD RGBABorder, RGBABackground, SymbolsPerLine;
	~TextBox() {
		Lock.lock();
		for (auto i = Lines.begin(); i != Lines.end(); i++)
			if(*i)delete *i;
		Lines.clear();
		Lock.unlock();
	}
	TextBox(string Text, SingleTextLineSettings *STLS, float Xpos, float Ypos, float Height, float Width, float VerticalOffset,DWORD RGBABackground,DWORD RGBABorder,BYTE BorderWidth,_Align TextAlign=_Align::left,VerticalOverflow VOverflow = VerticalOverflow::cut) {
		this->TextAlign = TextAlign;
		this->VOverflow = VOverflow;
		this->BorderWidth = BorderWidth;
		this->VerticalOffset = VerticalOffset;
		this->STLS = STLS;
		this->Xpos = Xpos;
		this->Ypos = Ypos;
		this->Width = Width;
		this->RGBABorder = RGBABorder;
		this->RGBABackground = RGBABackground;
		this->Height = Height;
		this->Text = (Text.size()) ? Text : " ";
		RecalculateAvailableSpaceForText();
		TextReformat();
	}
	void TextReformat() {
		Lock.lock();
		vector<vector<string>>SplittedText;
		vector<string> Paragraph;
		string Line;
		STLS->SetNewPos(Xpos, Ypos + 0.5f*Height + 0.5f*VerticalOffset);
		////OOOOOF... Someone help me with these please :d
		for (int i = 0; i < Text.size(); i++) {
			if (Text[i] == ' ') {
				Paragraph.push_back(Line);
				Line.clear();
			}
			else if (Text[i] == '\n') {
				Paragraph.push_back(Line);
				Line.clear();
				SplittedText.push_back(Paragraph);
				Paragraph.clear();
			}
			else Line.push_back(Text[i]);
			if(i == Text.size()-1) {
				Paragraph.push_back(Line);
				SplittedText.push_back(Paragraph);
				Line.clear();
				Paragraph.clear();
			}
		}
		for (int i = 0; i < SplittedText.size(); i++) {				
			#define Para SplittedText[i]
			#define LINES Paragraph
			for (int q = 0; q < Para.size(); q++) {
				if ((Para[q].size() + 1 + Line.size()) < SymbolsPerLine)
					if(Line.size())Line = (Line + " " + Para[q]);
					else Line = Para[q];
				else {
					if (Line.size())LINES.push_back(Line);
					Line = Para[q];
				}
				while (Line.size() >= SymbolsPerLine) {
					Paragraph.push_back(Line.substr(0, SymbolsPerLine));
					Line = Line.erase(0, SymbolsPerLine);
				}
			}
			Paragraph.push_back(Line);
			Line.clear();
			#undef LINES	
			#undef Para
		}
		for (int i = 0; i < Paragraph.size(); i++) {
			STLS->Move(0, 0 - VerticalOffset);
			//cout << Paragraph[i] << endl;
			if (VOverflow == VerticalOverflow::cut && STLS->CYpos < Ypos - Height)break;
			Lines.push_back( STLS->CreateOne(Paragraph[i]));
			if (TextAlign == _Align::right)Lines.back()->SafeChangePosition_Argumented(GLOBAL_RIGHT, ((this->Xpos) + (0.5f*Width) - this->STLS->XUnitSize), Lines.back()->CYpos);
			else if (TextAlign == _Align::left)Lines.back()->SafeChangePosition_Argumented(GLOBAL_LEFT, ((this->Xpos) - (0.5f*Width) + this->STLS->XUnitSize), Lines.back()->CYpos);
		}
		CalculatedTextHeight = (Lines.front()->CYpos - Lines.back()->CYpos) + Lines.front()->CalculatedHeight;
		if (VOverflow == VerticalOverflow::recalibrate) {
			float dy = (CalculatedTextHeight - this->Height);
			for (auto Y = Lines.begin(); Y != Lines.end(); Y++) {
				(*Y)->SafeMove(0, (dy + Lines.front()->CalculatedHeight)*0.5f);
			}
		}
		Lock.unlock();
	}
	void SafeTextColorChange(DWORD NewColor) {
		Lock.lock();
		for (auto i = Lines.begin(); i != Lines.end(); i++) {
			(*i)->SafeColorChange(NewColor);
		}
		Lock.unlock();
	}
	BIT MouseHandler(float mx, float my, CHAR Button/*-1 left, 1 right, 0 move*/, CHAR State /*-1 down, 1 up*/)  override {
		return 0;
	}
	void SafeStringReplace(string NewString) override {
		Lock.lock();
		this->Text = NewString;
		Lines.clear();
		RecalculateAvailableSpaceForText();
		TextReformat();
		Lock.unlock();
	}
	void KeyboardHandler(char CH) {
		return;
	}
	void RecalculateAvailableSpaceForText() {
		Lock.lock();
		SymbolsPerLine = floor((Width + STLS->XUnitSize * 2) / (STLS->XUnitSize * 2 + STLS->SpaceWidth));
		Lock.unlock();
	}
	void SafeMove(float dx, float dy) {
		Lock.lock();
		Xpos += dx;
		Ypos += dy;
		STLS->Move(dx, dy);
		for (int i = 0; i < Lines.size(); i++) {
			Lines[i]->SafeMove(dx, dy);
		}
		Lock.unlock();
	}
	void SafeChangePosition(float NewX, float NewY) {
		Lock.lock();
		NewX -= Xpos;
		NewY -= Ypos;
		SafeMove(NewX, NewY);
		Lock.unlock();
	}
	void SafeChangePosition_Argumented(BYTE Arg, float NewX, float NewY) {
		Lock.lock();
		float CW = 0.5f*(
				(INT32)((BIT)(GLOBAL_LEFT&Arg))
			-	(INT32)((BIT)(GLOBAL_RIGHT&Arg))
			)*Width,
			CH = 0.5f*(
				(INT32)((BIT)(GLOBAL_BOTTOM&Arg))
			-	(INT32)((BIT)(GLOBAL_TOP&Arg))
				)*Height;
		SafeChangePosition(NewX + CW, NewY + CH);
		Lock.unlock();
	}
	void Draw() override {
		Lock.lock();
		if ((BYTE)RGBABackground) {
			GLCOLOR(RGBABackground);
			glBegin(GL_QUADS);
			glVertex2f(Xpos - (Width * 0.5f), Ypos + (0.5f*Height));
			glVertex2f(Xpos + (Width * 0.5f), Ypos + (0.5f*Height));
			glVertex2f(Xpos + (Width * 0.5f), Ypos - (0.5f*Height));
			glVertex2f(Xpos - (Width * 0.5f), Ypos - (0.5f*Height));
			glEnd();
		}
		if ((BYTE)RGBABorder) {
			GLCOLOR(RGBABorder);
			glLineWidth(BorderWidth);
			glBegin(GL_LINE_LOOP);
			glVertex2f(Xpos - (Width * 0.5f), Ypos + (0.5f*Height));
			glVertex2f(Xpos + (Width * 0.5f), Ypos + (0.5f*Height));
			glVertex2f(Xpos + (Width * 0.5f), Ypos - (0.5f*Height));
			glVertex2f(Xpos - (Width * 0.5f), Ypos - (0.5f*Height));
			glEnd();
		}
		for (int i = 0; i < Lines.size(); i++) Lines[i]->Draw();
		Lock.unlock();
	}
	inline DWORD TellType() override {
		return TT_TEXTBOX;
	}
};

#define ARROW_STICK_HEIGHT 10
struct SelectablePropertedList : HandleableUIPart {
	_Align TextInButtonsAlign;
	void(*OnSelect)(int ID);
	void(*OnGetProperties)(int ID);
	float HeaderCXPos, HeaderYPos, CalculatedHeight, SpaceBetween, Width;
	ButtonSettings *ButtSettings;
	deque<string> SelectorsText;
	deque<Button*> Selectors;
	DWORD SelectedID;
	DWORD MaxVisibleLines,CurrentTopLineID,MaxCharsInLine;
	BYTE TopArrowHovered,BottomArrowHovered;
	~SelectablePropertedList() {
		Lock.lock();
		for (auto Y = Selectors.begin(); Y != Selectors.end(); Y++)
			delete (*Y);
		Lock.unlock();
	}
	SelectablePropertedList(ButtonSettings* ButtSettings, void(*OnSelect)(int SelectedID), void(*OnGetProperties)(int ID),float HeaderCXPos,float HeaderYPos,float Width, float SpaceBetween,DWORD MaxCharsInLine=0,DWORD MaxVisibleLines=0,_Align TextInButtonsAlign = _Align::left) {
		this->MaxCharsInLine = MaxCharsInLine;
		this->MaxVisibleLines = MaxVisibleLines;
		this->ButtSettings = ButtSettings;
		this->OnSelect = OnSelect;
		this->OnGetProperties=OnGetProperties;
		this->HeaderCXPos = HeaderCXPos;
		this->Width = Width;
		this->SpaceBetween = SpaceBetween;
		this->HeaderCXPos = HeaderCXPos;
		this->HeaderYPos = HeaderYPos;
		this->CurrentTopLineID = 0;
		this->TextInButtonsAlign = TextInButtonsAlign;
		SelectedID = 0xFFFFFFFF;
	}
	void RecalculateCurrentHeight() {
		Lock.lock();
		CalculatedHeight = SpaceBetween * Selectors.size();
		Lock.unlock();
	}
	BIT MouseHandler(float mx, float my, CHAR Button/*-1 left, 1 right, 0 move*/, CHAR State /*-1 down, 1 up*/)  override {
		Lock.lock();
		TopArrowHovered = BottomArrowHovered = 0;
		if (fabsf(mx - HeaderCXPos) < 0.5*Width && my < HeaderYPos && my > HeaderYPos - CalculatedHeight) {
			if (Button == 2 /*UP*/) {
				if (State == -1) {
					SafeRotateList(-3);
				}
			}
			else if (Button == 3 /*DOWN*/) {
				if (State == -1) {
					SafeRotateList(3);
				}
			}
		}
		if (MaxVisibleLines && Selectors.size()<SelectorsText.size()) {
			if (fabsf(mx - HeaderCXPos) < 0.5*Width) {
				if (my>HeaderYPos && my<HeaderYPos + ARROW_STICK_HEIGHT) {
					TopArrowHovered = 1;
					if (Button == -1 && State==-1) {
						SafeRotateList(-1);
						Lock.unlock();
						return 1;
					}
				}
				else if (my < HeaderYPos - CalculatedHeight && my > HeaderYPos - CalculatedHeight - ARROW_STICK_HEIGHT) {
					BottomArrowHovered = 1;
					if (Button == -1 && State == -1) {
						SafeRotateList(1);
						Lock.unlock();
						return 1;
					}
				}
			}
		}
		if (Button) {
			BIT flag = 1;
			for (int i = 0; i < Selectors.size(); i++) {
				if (Selectors[i]->MouseHandler(mx, my, Button, State) && flag) {
					if (Button == -1) {
						SelectedID = i + CurrentTopLineID;
						if (OnSelect)OnSelect(i + CurrentTopLineID);
					}
					if (Button == 1) {
						//cout << "PROP\n";
						if (OnGetProperties)OnGetProperties(i + CurrentTopLineID);
					}
					flag = 0;
					Lock.unlock();
					return 1;
				}
			}
		}
		else {
			for (int i=0;i<Selectors.size();i++)
				Selectors[i]->MouseHandler(mx, my, 0, 0);
		}
		if (SelectedID < SelectorsText.size())
			if (SelectedID >= CurrentTopLineID && SelectedID < CurrentTopLineID + MaxVisibleLines)
				Selectors[SelectedID - CurrentTopLineID]->MouseHandler(Selectors[SelectedID - CurrentTopLineID]->Xpos, Selectors[SelectedID - CurrentTopLineID]->Ypos, 0, 0);
		Lock.unlock();
		return 0;
	}
	void SafeStringReplace(string NewString) override {
		Lock.lock();
		this->SafeStringReplace(NewString, 0xFFFFFFFF);
		Lock.unlock();
	}
	void SafeStringReplace(string NewString,DWORD LineID) {
		Lock.lock();
		if (LineID == 0xFFFFFFFF) {
			SelectorsText[SelectedID] = NewString;
			if (SelectedID < SelectorsText.size() && MaxVisibleLines)
				if (SelectedID > CurrentTopLineID && SelectedID < CurrentTopLineID + MaxVisibleLines)
					Selectors[SelectedID - CurrentTopLineID]->SafeStringReplace(NewString);
		}
		else {
			SelectorsText[LineID] = NewString;
			if (LineID > CurrentTopLineID && LineID - CurrentTopLineID < MaxVisibleLines)
				Selectors[LineID - CurrentTopLineID]->SafeStringReplace(NewString);
		}
		Lock.unlock();
	}
	void SafeUpdateLines() {
		Lock.lock();
		while (SelectorsText.size() < Selectors.size())Selectors.pop_back();
		if (CurrentTopLineID + MaxVisibleLines > SelectorsText.size()) {
			if(SelectorsText.size()>=MaxVisibleLines)CurrentTopLineID = SelectorsText.size() - MaxVisibleLines;
			else CurrentTopLineID = 0;
		}
		for (int i = 0; i < Selectors.size(); i++)
			if (i + CurrentTopLineID < SelectorsText.size())
				Selectors[i]->SafeStringReplace(
				(MaxCharsInLine) ?
					(SelectorsText[i + CurrentTopLineID].substr(0, MaxCharsInLine))
					:
					(SelectorsText[i + CurrentTopLineID])
				);
		ReSetAlign_All(TextInButtonsAlign);
		Lock.unlock();
	}
	void SafeRotateList(INT32 Delta) {
		Lock.lock();
		if (!MaxVisibleLines) {
			Lock.unlock(); return;
		}
		if (Delta < 0 && CurrentTopLineID < 0-Delta)CurrentTopLineID = 0;
		else if (Delta > 0 && CurrentTopLineID + Delta + MaxVisibleLines > SelectorsText.size())
			CurrentTopLineID = SelectorsText.size() - MaxVisibleLines;
		else CurrentTopLineID += Delta;
		SafeUpdateLines();
		Lock.unlock();
	}
	void SafeRemoveStringByID(DWORD ID) {
		Lock.lock();
		if (ID >= SelectorsText.size()) {
			Lock.unlock(); return;
		}
		if (SelectorsText.empty()) {
			Lock.unlock(); return;
		}
		if (MaxVisibleLines) {
			if (ID < CurrentTopLineID) {
				CurrentTopLineID--;
			}
			else if (ID == CurrentTopLineID) {
				if (CurrentTopLineID == SelectorsText.size() - 1)CurrentTopLineID--;
			}
		}
		SelectorsText.erase(SelectorsText.begin() + ID);
		SafeUpdateLines();
		SelectedID = 0xFFFFFFFF;
		Lock.unlock();
	}
	void ReSetAlignFor(DWORD ID, _Align Align) {
		Lock.lock();
		if (ID>=Selectors.size()) {
			Lock.unlock(); return;
		}
		float nx = HeaderCXPos - ((Align == _Align::left) ? 0.5f : ((Align == _Align::right)? 0 - 0.5f :0))*(Width - SpaceBetween);
		Selectors[ID]->STL->SafeChangePosition_Argumented(Align, nx, Selectors[ID]->Ypos);
		Lock.unlock();
	}
	void ReSetAlign_All(_Align Align) {
		Lock.lock();
		if (!Align) {
			Lock.unlock(); return;
		}
		float nx = HeaderCXPos - ((Align == _Align::left) ? 0.5f : ((Align == _Align::right) ? 0 - 0.5f : 0))*(Width - SpaceBetween);
		for (int i = 0; i < Selectors.size(); i++)
			Selectors[i]->STL->SafeChangePosition_Argumented(Align, nx, Selectors[i]->Ypos);
		Lock.unlock();
	}
	void SafePushBackNewString(string ButtonText) {
		Lock.lock();
		if (MaxCharsInLine)ButtonText = ButtonText.substr(0, MaxCharsInLine);
		SelectorsText.push_back(ButtonText);
		if (MaxVisibleLines && SelectorsText.size() > MaxVisibleLines) {
			SafeUpdateLines(); {
				Lock.unlock(); return;
			}
		}
		ButtSettings->ChangePosition(HeaderCXPos, HeaderYPos - (SelectorsText.size() - 0.5f)*SpaceBetween);
		ButtSettings->Height = SpaceBetween;
		ButtSettings->Width = Width;
		ButtSettings->OnClick = NULL;
		Button *ptr;
		Selectors.push_back(ptr = ButtSettings->CreateOne(ButtonText));
		//free(ptr);
		RecalculateCurrentHeight();
		ReSetAlignFor(SelectorsText.size()-1,this->TextInButtonsAlign);
		Lock.unlock();
	}
	void PushStrings(list<string> LStrings) {
		Lock.lock();
		for (auto Y = LStrings.begin(); Y != LStrings.end(); Y++)
			SafePushBackNewString(*Y);
		Lock.unlock();
	}
	void PushStrings(vector<string> LStrings) {
		Lock.lock();
		for (auto Y = LStrings.begin(); Y != LStrings.end(); Y++)
			SafePushBackNewString(*Y);
		Lock.unlock();
	}
	void PushStrings(initializer_list<string> LStrings) {
		Lock.lock();
		for (auto Y = LStrings.begin(); Y != LStrings.end(); Y++)
			SafePushBackNewString(*Y);
		Lock.unlock();
	}
	void SafeChangePosition_Argumented(BYTE Arg, float NewX, float NewY) {
		Lock.lock();
		float CW = 0.5f*(
				(INT32)((BIT)(GLOBAL_LEFT&Arg))
			-	(INT32)((BIT)(GLOBAL_RIGHT&Arg))
			)*Width,
			CH = 0.5f*(
				(INT32)((BIT)(GLOBAL_BOTTOM&Arg))
			-	(INT32)((BIT)(GLOBAL_TOP&Arg))
			)*CalculatedHeight;
		SafeChangePosition(NewX + CW, NewY - 0.5f*CalculatedHeight + CH);
		Lock.unlock();
	}
	void KeyboardHandler(CHAR CH) override {
		return;
	}
	void SafeChangePosition(float NewCXPos, float NewHeaderYPos) override {
		Lock.lock();
		NewCXPos -= HeaderCXPos;
		NewHeaderYPos -= HeaderYPos;
		SafeMove(NewCXPos, NewHeaderYPos);
		Lock.unlock();
	}
	void SafeMove(float dx, float dy) override {
		Lock.lock();
		HeaderCXPos += dx;
		HeaderYPos += dy;
		for (auto Y = Selectors.begin(); Y != Selectors.end(); Y++)
			(*Y)->SafeMove(dx, dy);
		Lock.unlock();
	}
	void Draw() override {
		Lock.lock();
		if (Selectors.size() < SelectorsText.size()) {
			///TOP BAR
			if (TopArrowHovered)GLCOLOR(ButtSettings->HoveredRGBABorder);
			else GLCOLOR(ButtSettings->RGBABorder);
			glBegin(GL_QUADS);
			glVertex2f(HeaderCXPos - 0.5f*Width, HeaderYPos);
			glVertex2f(HeaderCXPos + 0.5f*Width, HeaderYPos);
			glVertex2f(HeaderCXPos + 0.5f*Width, HeaderYPos + ARROW_STICK_HEIGHT);
			glVertex2f(HeaderCXPos - 0.5f*Width, HeaderYPos + ARROW_STICK_HEIGHT);
			///BOTTOM BAR
			if (BottomArrowHovered)GLCOLOR(ButtSettings->HoveredRGBABorder);
			else GLCOLOR(ButtSettings->RGBABorder);
			glVertex2f(HeaderCXPos - 0.5f*Width, HeaderYPos - CalculatedHeight);
			glVertex2f(HeaderCXPos + 0.5f*Width, HeaderYPos - CalculatedHeight);
			glVertex2f(HeaderCXPos + 0.5f*Width, HeaderYPos - CalculatedHeight - ARROW_STICK_HEIGHT);
			glVertex2f(HeaderCXPos - 0.5f*Width, HeaderYPos - CalculatedHeight - ARROW_STICK_HEIGHT);
			glEnd();
			///TOP ARROW
			if (TopArrowHovered)
				if (ButtSettings->HoveredRGBAColor & 0xFF)GLCOLOR(ButtSettings->HoveredRGBAColor);
				else GLCOLOR(ButtSettings->RGBAColor);
			else GLCOLOR(ButtSettings->RGBAColor);
			glBegin(GL_TRIANGLES);
			glVertex2f(HeaderCXPos, HeaderYPos + 9 * ARROW_STICK_HEIGHT / 10);
			glVertex2f(HeaderCXPos + ARROW_STICK_HEIGHT * 0.5f, HeaderYPos + ARROW_STICK_HEIGHT / 10);
			glVertex2f(HeaderCXPos - ARROW_STICK_HEIGHT * 0.5f, HeaderYPos + ARROW_STICK_HEIGHT / 10);
			///BOTTOM ARROW
			if (BottomArrowHovered)
				if (ButtSettings->HoveredRGBAColor & 0xFF)GLCOLOR(ButtSettings->HoveredRGBAColor);
				else GLCOLOR(ButtSettings->RGBAColor);
			else GLCOLOR(ButtSettings->RGBAColor);
			glVertex2f(HeaderCXPos, HeaderYPos - CalculatedHeight - 9 * ARROW_STICK_HEIGHT / 10);
			glVertex2f(HeaderCXPos + ARROW_STICK_HEIGHT * 0.5f, HeaderYPos - CalculatedHeight - ARROW_STICK_HEIGHT / 10);
			glVertex2f(HeaderCXPos - ARROW_STICK_HEIGHT * 0.5f, HeaderYPos - CalculatedHeight - ARROW_STICK_HEIGHT / 10);
			glEnd();
		}
		for (auto Y = Selectors.begin(); Y != Selectors.end(); Y++) 
			(*Y)->Draw();
		Lock.unlock();
	}
	inline DWORD TellType() override {
		return TT_SELPROPLIST;
	}
};

#define HTSQ2 (2)
struct SpecialSigns {
	static void DrawOK(float x, float y,float SZParam,DWORD RGBAColor,DWORD NOARGUMENT=0) {
		GLCOLOR(RGBAColor);
		glLineWidth(ceil(SZParam / 2));
		glBegin(GL_LINE_STRIP);
		glVertex2f(x - SZParam*0.766f, y + SZParam*0.916f);
		glVertex2f(x - SZParam * 0.1f, y + SZParam * 0.25f);
		glVertex2f(x + SZParam*0.9f, y + SZParam*1.25f);
		glEnd();
		glPointSize(ceil(SZParam / 2));
		glBegin(GL_POINTS);
		glVertex2f(x - SZParam * 0.1f, y + SZParam * 0.25f);
		glEnd();
	}
	static void DrawExTriangle(float x, float y, float SZParam, DWORD RGBAColor, DWORD SecondaryRGBAColor) {
		GLCOLOR(RGBAColor);
		glBegin(GL_TRIANGLES);
		glVertex2f(x, y + HTSQ2 * SZParam);
		glVertex2f(x - SZParam, y);
		glVertex2f(x + SZParam, y);
		glEnd();
		GLCOLOR(SecondaryRGBAColor);
		glLineWidth(ceil(SZParam / 8));
		glBegin(GL_LINE_LOOP);
		glVertex2f(x, y + HTSQ2 * SZParam);
		glVertex2f(x - SZParam, y);
		glVertex2f(x + SZParam, y);
		glEnd();
		glLineWidth(ceil(SZParam / 4));
		glBegin(GL_LINES);
		glVertex2f(x, y + SZParam * 0.6f);
		glVertex2f(x, y + SZParam * 1.40f);
		glVertex2f(x, y + SZParam * 0.2f);
		glVertex2f(x, y + SZParam * 0.4f);
		glEnd();
	}
	static void DrawFileSign(float x, float y, float SZParam, DWORD RGBAColor, DWORD SecondaryRGBAColor) {
		GLCOLOR(RGBAColor);
		glLineWidth(ceil(SZParam / 5));
		glBegin(GL_LINE_LOOP);
		glVertex2f(x, y + SZParam);
		glVertex2f(x - SZParam, y);
		glVertex2f(x - SZParam, y - SZParam);
		glVertex2f(x + SZParam, y - SZParam);
		glVertex2f(x + SZParam, y + SZParam);
		glEnd();
		glBegin(GL_LINES);
		glVertex2f(x, y + SZParam);
		glVertex2f(x, y);
		glVertex2f(x, y);
		glVertex2f(x - SZParam, y);
		glEnd();
		glPointSize(ceil(SZParam / 5));
		glBegin(GL_POINTS);
		glVertex2f(x, y);
		glVertex2f(x, y + SZParam);
		glVertex2f(x - SZParam, y);
		glVertex2f(x - SZParam, y - SZParam);
		glVertex2f(x + SZParam, y - SZParam);
		glVertex2f(x + SZParam, y + SZParam);
		glEnd();
	}
	static void DrawACircle(float x, float y, float SZParam, DWORD RGBAColor, DWORD SecondaryRGBAColor) {
		GLCOLOR(SecondaryRGBAColor);
		glBegin(GL_POLYGON);
		for (float a = -90; a < 270; a += 5) 
			glVertex2f(SZParam*1.25f*(cos(ANGTORAD(a))) + x, SZParam*1.25f*(sin(ANGTORAD(a))) + y + SZParam*0.75f);
		glEnd();
		GLCOLOR(RGBAColor);
		glLineWidth(ceil(SZParam / 10));
		glBegin(GL_LINE_LOOP);
		for (float a = -90; a < 270; a += 5)
			glVertex2f(SZParam*1.25f*(cos(ANGTORAD(a))) + x, SZParam*1.25f*(sin(ANGTORAD(a))) + y + SZParam*0.75f);
		glEnd();
	}
	static void DrawNo(float x, float y, float SZParam, DWORD RGBAColor, DWORD NOARGUMENT=0) {
		GLCOLOR(RGBAColor);
		glLineWidth(ceil(SZParam / 2));
		glBegin(GL_LINES);
		glVertex2f(x - SZParam*0.5f, y + SZParam*0.25f);
		glVertex2f(x + SZParam*0.5f, y + SZParam*1.25f);
		glVertex2f(x + SZParam*0.5f, y + SZParam*0.25f);
		glVertex2f(x - SZParam*0.5f, y + SZParam*1.25f);
		glEnd();
	}
	static void DrawWait(float x, float y, float SZParam, DWORD RGBAColor, DWORD TotalStages) {
		float Start = ((float)((TimerV%TotalStages) * 360))/(float)(TotalStages),t;
		BYTE R = (RGBAColor >> 24), G = (RGBAColor >> 16) & 0xFF, B = (RGBAColor >> 8) & 0xFF, A = (RGBAColor) & 0xFF;
		//printf("%x\n", CurStage_TotalStages);
		glLineWidth(ceil(SZParam / 1.5f));
		glBegin(GL_LINES);
		for (float a = 0; a < 360.5f; a += (180.f / (TotalStages))) {
			t = a / 360.f;
			glColor4ub(
				255 * (t) + R * (1 - t),
				255 * (t) + G * (1 - t),
				255 * (t) + B * (1 - t),
				255 * (t) + A * (1 - t)
			);
			glVertex2f(SZParam*1.25f*(cos(ANGTORAD(a+Start))) + x, SZParam*1.25f*(sin(ANGTORAD(a+Start))) + y + SZParam * 0.75f);
		}
		glEnd();
	}
};

struct SpecialSignHandler : HandleableUIPart {
	float x, y, SZParam;
	DWORD FRGBA, SRGBA;
	void(*DrawFunc)(float, float, float, DWORD, DWORD);
	SpecialSignHandler(void(*DrawFunc)(float,float,float,DWORD,DWORD),float x, float y, float SZParam, DWORD FRGBA, DWORD SRGBA){
		this->DrawFunc = DrawFunc;
		this->x = x;
		this->y = y;
		this->SZParam = SZParam;
		this->FRGBA = FRGBA;
		this->SRGBA = SRGBA;
	}
	void Draw() override {
		Lock.lock();
		if(this->DrawFunc)this->DrawFunc(x, y, SZParam, FRGBA, SRGBA);
		Lock.unlock();
	}
	void SafeMove(float dx, float dy) override {
		Lock.lock();
		x += dx;
		y += dy;
		Lock.unlock();
	}
	void SafeChangePosition(float NewX, float NewY) override {
		Lock.lock();
		x = NewX;
		y = NewY;
		Lock.unlock();
	}
	void _ReplaceVoidFunc(void(*NewDrawFunc)(float, float, float, DWORD, DWORD) ) {
		Lock.lock();
		this->DrawFunc = NewDrawFunc;
		Lock.unlock();
	}
	void SafeChangePosition_Argumented(BYTE Arg, float NewX, float NewY) override {

	}
	void KeyboardHandler(CHAR CH) override {
		return;
	}
	void SafeStringReplace(string Meaningless) override {
		return;
	}
	BIT MouseHandler(float mx, float my, CHAR Button, CHAR State) override {
		return 0;
	}
};

struct WheelVariableChanger :HandleableUIPart {
	enum class Type { exponential, addictable };
	enum class Sensitivity { on_enter, on_click, on_wheel };
	Type type;
	Sensitivity Sen;
	InputField* var_if, * fac_if;
	float Width, Height;
	float Xpos, Ypos;
	string var_s, fact_s;
	double variable;
	double factor;
	bool IsHovered, WheelFieldHovered;
	void(*OnApply)(double);
	~WheelVariableChanger() {
		if (var_if)
			delete var_if;
		if (var_if)
			delete fac_if;
	}
	WheelVariableChanger(void(*OnApply)(double), float Xpos, float Ypos, double default_var, double default_fact, SingleTextLineSettings* STLS, string var_string = " ", string fac_string = " ", Type type = Type::exponential) : Width(100), Height(50) {
		this->OnApply = OnApply;
		this->Xpos = Xpos;
		this->Ypos = Ypos;
		this->variable = default_var;
		this->factor = default_fact;
		this->IsHovered = WheelFieldHovered = false;
		this->type = type;
		this->Sen = Sensitivity::on_wheel;
		var_if = new InputField(to_string(default_var).substr(0, 8), Xpos - 25., Ypos + 15, 10, 40, STLS, nullptr, 0x007FFFFF, STLS, var_string, 8, _Align::center, _Align::center, InputField::Type::FP_PositiveNumbers);
		fac_if = new InputField(to_string(default_fact).substr(0, 8), Xpos - 25., Ypos - 5, 10, 40, STLS, nullptr, 0x007FFFFF, STLS, fac_string, 8, _Align::center, _Align::center, InputField::Type::FP_PositiveNumbers);
	}
	void Draw() override {
		GLCOLOR(0xFFFFFF3F + WheelFieldHovered * 0x3F);
		glBegin(GL_QUADS);
		glVertex2f(Xpos, Ypos + 25);
		glVertex2f(Xpos, Ypos - 25);
		glVertex2f(Xpos + 50, Ypos - 25);
		glVertex2f(Xpos + 50, Ypos + 25);
		glEnd();
		GLCOLOR((0x007FFF3F + WheelFieldHovered * 0x3F));
		glBegin(GL_LINE_LOOP);
		glVertex2f(Xpos, Ypos + 25);
		glVertex2f(Xpos, Ypos - 25);
		glVertex2f(Xpos + 50, Ypos - 25);
		glVertex2f(Xpos + 50, Ypos + 25);
		glEnd();
		glBegin(GL_LINE_LOOP);
		glVertex2f(Xpos - 50, Ypos + 25);
		glVertex2f(Xpos - 50, Ypos - 25);
		glVertex2f(Xpos + 50, Ypos - 25);
		glVertex2f(Xpos + 50, Ypos + 25);
		glEnd();
		var_if->Draw();
		fac_if->Draw();
	}
	void SafeMove(float dx, float dy) override {
		Xpos += dx;
		Ypos += dy;
		var_if->SafeMove(dx, dy);
		fac_if->SafeMove(dx, dy);
	}
	void SafeChangePosition(float NewX, float NewY) override {
		NewX -= Xpos;
		NewY -= Ypos;
		SafeMove(NewX, NewY);
	}
	void SafeChangePosition_Argumented(BYTE Arg, float NewX, float NewY) override {
		float CW = 0.5f * (
			(INT32)((BIT)(GLOBAL_LEFT & Arg))
			- (INT32)((BIT)(GLOBAL_RIGHT & Arg))
			) * Width,
			CH = 0.5f * (
			(INT32)((BIT)(GLOBAL_BOTTOM & Arg))
				- (INT32)((BIT)(GLOBAL_TOP & Arg))
				) * Height;
		SafeChangePosition(NewX + CW, NewY + CH);
	}
	void CheckupInputs() {
		variable = stod(var_if->STL->_CurrentText);
		factor = stod(fac_if->STL->_CurrentText);
	}
	void KeyboardHandler(CHAR CH) override {
		fac_if->KeyboardHandler(CH);
		var_if->KeyboardHandler(CH);
		if (IsHovered) {
			if (CH == 13) {
				CheckupInputs();
				if (OnApply)
					OnApply(variable);
			}
		}
	}
	void SafeStringReplace(string Meaningless) override {

	}
	BIT MouseHandler(float mx, float my, CHAR Button, CHAR State) override {
		this->fac_if->MouseHandler(mx, my, Button, State);
		this->var_if->MouseHandler(mx, my, Button, State);
		mx -= Xpos;
		my -= Ypos;
		if (fabsf(mx) < Width * 0.5 && fabsf(my) < Height * 0.5) {
			IsHovered = true;
			if (mx >= 0 && mx <= Width * 0.5 && fabsf(my) < Height * 0.5) {
				if (Sen == Sensitivity::on_click && State == 1)
					if (OnApply)
						OnApply(variable);
				WheelFieldHovered = true;
				if (Button) {
					CheckupInputs();
					if (Button == 2 /*UP*/) {
						if (State == -1) {
							switch (type) {
								case WheelVariableChanger::Type::exponential: {variable *= factor; break; }
								case WheelVariableChanger::Type::addictable: {variable += factor;	break; }
							}
							var_if->UpdateInputString(to_string(variable));
							if (Sen == Sensitivity::on_wheel)
								if (OnApply)
									OnApply(variable);
						}
					}
					else if (Button == 3 /*DOWN*/) {
						if (State == -1) {
							switch (type) {
								case WheelVariableChanger::Type::exponential: {variable /= factor; break; }
								case WheelVariableChanger::Type::addictable: {variable -= factor;	break; }
							}
							var_if->UpdateInputString(to_string(variable));
							if (Sen == Sensitivity::on_wheel)
								if (OnApply)
									OnApply(variable);
						}
					}
				}
			}
		}
		else {
			IsHovered = false;
			WheelFieldHovered = false;
		}
		return 0;
	}
};

#define WindowHeapSize 15
struct MoveableWindow:HandleableUIPart {
	float XWindowPos, YWindowPos;//leftup corner coordinates
	float Width, Height;
	DWORD RGBABackground,RGBAThemeColor,RGBAGradBackground;
	SingleTextLine* WindowName;
	map<string, HandleableUIPart*> WindowActivities;
	BIT Drawable;
	BIT HoveredCloseButton;
	BIT CursorFollowMode;
	BIT HUIP_MapWasChanged;
	float PCurX, PCurY;
	~MoveableWindow() {
		Lock.lock();
		delete WindowName;
		for (auto i = WindowActivities.begin(); i != WindowActivities.end(); i++)
			delete i->second;
		WindowActivities.clear();
		Lock.unlock();
	}
	MoveableWindow(string WindowName, SingleTextLineSettings *WindowNameSettings,float XPos,float YPos,float Width, float Height,DWORD RGBABackground,DWORD RGBAThemeColor,DWORD RGBAGradBackground=0) {
		if (WindowNameSettings) {
			WindowNameSettings->SetNewPos(XPos, YPos);
			this->WindowName = WindowNameSettings->CreateOne(WindowName);
			this->WindowName->SafeMove(this->WindowName->CalculatedWidth*0.5 + WindowHeapSize * 0.5f, 0 - WindowHeapSize * 0.5f);
		}
		this->HUIP_MapWasChanged = false;
		this->XWindowPos = XPos;
		this->YWindowPos = YPos;
		this->Width = Width;
		this->Height = (Height < WindowHeapSize) ? WindowHeapSize : Height;
		this->RGBABackground = RGBABackground;
		this->RGBAThemeColor = RGBAThemeColor;
		this->RGBAGradBackground = RGBAGradBackground;
		this->CursorFollowMode = 0;
		this->HoveredCloseButton = 0;
		this->Drawable = 1;
		this->PCurX = 0.;
		this->PCurY = 0.;
	}
	void KeyboardHandler(char CH) {
		Lock.lock();
		for (auto i = WindowActivities.begin(); i != WindowActivities.end(); i++) {
			i->second->KeyboardHandler(CH);
			if (HUIP_MapWasChanged) {
				HUIP_MapWasChanged = false;
				break;
			}
		}
		Lock.unlock();
	}
	BIT MouseHandler(float mx, float my, CHAR Button/*-1 left, 1 right, 0 move*/, CHAR State /*-1 down, 1 up*/) override {
		Lock.lock();
		if (!Drawable) {
			Lock.unlock(); 
			return 0; 
		}
		HoveredCloseButton = 0;
		if (mx > XWindowPos + Width - WindowHeapSize && mx < XWindowPos + Width && my < YWindowPos && my > YWindowPos - WindowHeapSize) {///close button
			if (Button && State == 1) {
				Drawable = 0;
				CursorFollowMode = false;
				Lock.unlock();
				return 1;
			}
			else if (!Button) {
				HoveredCloseButton = 1;
			}
		}
		else if (mx - XWindowPos < Width && mx - XWindowPos>0 && my<YWindowPos && my>YWindowPos - WindowHeapSize) {
			if (Button == -1) {///window header
				if (State == -1) {
					CursorFollowMode = !CursorFollowMode;
					PCurX = mx;
					PCurY = my;
				}
				else if (State == 1) {
					CursorFollowMode = !CursorFollowMode;
				}
			}
		}
		if (CursorFollowMode) {
			SafeMove(mx - PCurX, my - PCurY);
			PCurX = mx;
			PCurY = my;
			Lock.unlock();
			return 1;
		}

		BIT flag=0;
		auto Y = WindowActivities.begin();
		while (Y != WindowActivities.end()) {
			flag=Y->second->MouseHandler(mx, my, Button, State);
			if (HUIP_MapWasChanged) {
				HUIP_MapWasChanged = false;
				break;
			}
			Y++;
		}

		if (mx - XWindowPos < Width && mx - XWindowPos > 0 && YWindowPos - my > 0 && YWindowPos - my < Height)
			if (Button) {
				Lock.unlock();
				return 1;
			}
			else {
				Lock.unlock();
				return flag;
			}
		else {
			Lock.unlock();
			return flag;
		}
			//return 1;

	}
	void SafeChangePosition(float NewXpos, float NewYpos) override {
		Lock.lock();
		NewXpos -= XWindowPos;
		NewYpos -= YWindowPos;
		SafeMove(NewXpos,NewYpos);
		Lock.unlock();
	}
	BIT DeleteUIElementByName(string ElementName) {
		Lock.lock();
		HUIP_MapWasChanged = true;
		auto ptr = WindowActivities.find(ElementName);
		if (ptr == WindowActivities.end()) {
			Lock.unlock();
			return 0;
		}
		auto deletable = ptr->second;
		WindowActivities.erase(ElementName);
		delete deletable;
		Lock.unlock();
		return 1;
	}
	BIT AddUIElement(string ElementName, HandleableUIPart* Elem) {
		Lock.lock();
		HUIP_MapWasChanged = true;
		auto ans = WindowActivities.insert_or_assign(ElementName, Elem);
		Lock.unlock();
		return ans.second;
	}
	void SafeMove(float dx, float dy) override {
		Lock.lock();
		XWindowPos += dx;
		YWindowPos += dy;
		WindowName->SafeMove(dx, dy);
		for (auto Y = WindowActivities.begin(); Y != WindowActivities.end(); Y++)
			Y->second->SafeMove(dx, dy);
		Lock.unlock();
	}
	void SafeChangePosition_Argumented(BYTE Arg, float NewX, float NewY) {
		Lock.lock();
		float CW = 0.5f*(
				(INT32)(!!(GLOBAL_LEFT&Arg))
			-	(INT32)(!!(GLOBAL_RIGHT&Arg))
			-	1)*Width,
			CH = 0.5f*(
				(INT32)(!!(GLOBAL_BOTTOM&Arg))
			-	(INT32)(!!(GLOBAL_TOP&Arg))
			+	1)*Height;
		SafeChangePosition(NewX + CW, NewY + CH);
		Lock.unlock();
	}
	void Draw() override {
		Lock.lock();
		if (!Drawable) {
			Lock.unlock();
			return;
		}
		GLCOLOR(RGBABackground);
		glBegin(GL_QUADS);
		glVertex2f(XWindowPos, YWindowPos);
		glVertex2f(XWindowPos + Width, YWindowPos);
		if (RGBAGradBackground)GLCOLOR(RGBAGradBackground);
		glVertex2f(XWindowPos + Width, YWindowPos - Height);
		glVertex2f(XWindowPos, YWindowPos - Height);
		glEnd();
		GLCOLOR(RGBAThemeColor);
		glLineWidth(1);
		glBegin(GL_LINE_LOOP);
		glVertex2f(XWindowPos, YWindowPos);
		glVertex2f(XWindowPos + Width, YWindowPos);
		glVertex2f(XWindowPos + Width, YWindowPos - Height);
		glVertex2f(XWindowPos, YWindowPos - Height);
		glEnd();
		glBegin(GL_QUADS);
		glVertex2f(XWindowPos, YWindowPos);
		glVertex2f(XWindowPos + Width, YWindowPos);
		glVertex2f(XWindowPos + Width, YWindowPos - WindowHeapSize);
		glVertex2f(XWindowPos, YWindowPos - WindowHeapSize);
		glColor4ub(255, 32 + 32*HoveredCloseButton, 32 + 32*HoveredCloseButton, 255);
		glVertex2f(XWindowPos + Width, YWindowPos);
		glVertex2f(XWindowPos + Width, YWindowPos + 1 - WindowHeapSize);
		glVertex2f(XWindowPos + Width - WindowHeapSize, YWindowPos + 1 - WindowHeapSize);
		glVertex2f(XWindowPos + Width - WindowHeapSize, YWindowPos);
		glEnd();

		if (WindowName)WindowName->Draw();

		for (auto Y = WindowActivities.begin(); Y != WindowActivities.end(); Y++)
			Y->second->Draw();
		Lock.unlock();
	}
	void _NotSafeResize(float NewHeight, float NewWidth) {
		Lock.lock();
		this->Height = NewHeight;
		this->Width = NewWidth;
		Lock.unlock();
	}
	void _NotSafeResize_Centered(float NewHeight, float NewWidth) {
		Lock.lock();
		float dx, dy;
		XWindowPos += (dx = -0.5f*(NewWidth - Width));
		YWindowPos += (dy = 0.5f*(NewHeight - Height));
		WindowName->SafeMove(dx, dy);
		Width = NewWidth;
		Height = NewHeight;
		Lock.unlock();
	}
	void SafeStringReplace(string NewWindowTitle) override {
		Lock.lock();
		SafeWindowRename(NewWindowTitle);
		Lock.unlock();
	}
	void SafeWindowRename(string NewWindowTitle) {
		Lock.lock();
		if (WindowName) { 
			WindowName->SafeStringReplace(NewWindowTitle); 
			WindowName->SafeChangePosition_Argumented(GLOBAL_LEFT, XWindowPos + WindowHeapSize * 0.5f, WindowName->CYpos);
		}
		Lock.unlock();
	}
	HandleableUIPart*& operator[](string ID) {
		return WindowActivities[ID];
	}
	DWORD TellType() {
		return TT_MOVEABLE_WINDOW;
	}
};


SingleTextLineSettings
						* _STLS_WhiteSmall = new SingleTextLineSettings("_", 0, 0, 5, 0xFFFFFFFF),
						* _STLS_BlackSmall = new SingleTextLineSettings("_", 0, 0, 5, 0x000000FF),

						* System_Black = (is_fonted)? new SingleTextLineSettings(10, 0x000000FF) : new SingleTextLineSettings("_", 0, 0, 5, 0x000000FF),
						* System_White = (is_fonted) ? new SingleTextLineSettings(10, 0xFFFFFFFF) : new SingleTextLineSettings("_", 0, 0, 5, 0xFFFFFFFF);

struct WindowsHandler {
	map<string, MoveableWindow*> Map;
	#define Map(WindowName,ElementName) (*Map[WindowName])[ElementName]
	list<map<string, MoveableWindow*>::iterator> ActiveWindows;
	string MainWindow_ID,MW_ID_Holder;
	BIT WindowWasDisabledDuringMouseHandling;
	std::recursive_mutex locker;
	WindowsHandler() {
		MW_ID_Holder = "";
		MainWindow_ID = "MAIN";
		WindowWasDisabledDuringMouseHandling = 0;
		MoveableWindow* ptr;
		Map["ALERT"] = ptr = new MoveableWindow("Alert window", System_White, -100, 25, 200, 50, 0x3F3F3FCF, 0x7F7F7F7F);
		(*ptr)["AlertText"] = new TextBox("_", System_White, 17.5, -7.5, 37, 160, 7.5, 0, 0, 0, _Align::left, TextBox::VerticalOverflow::recalibrate);
		(*ptr)["AlertSign"] = new SpecialSignHandler(SpecialSigns::DrawACircle,-80,-12.5,7.5,0x000000FF,0x001FFFFF);

		Map["PROMPT"] = ptr = new MoveableWindow("prompt", System_White, -50, 50, 100, 100, 0x3F3F3FCF, 0x7F7F7F7F);
		(*ptr)["FLD"] = new InputField("", 0, 35 - WindowHeapSize, 10, 80, System_White, NULL, 0x007FFFFF, NULL, "", 0, _Align::center);
		(*ptr)["TXT"] = new TextBox("_abc_", System_White, 0, 7.5-WindowHeapSize, 10, 80, 7.5, 0, 0, 2,_Align::center,TextBox::VerticalOverflow::recalibrate); 
		(*ptr)["BUTT"] = new Button("Submit", System_White, NULL, -0, -20 - WindowHeapSize, 80, 10, 1, 0x007FFF3F, 0x007FFFFF, 0xFF7F00FF, 0xFFFFFFFF, 0xFF7F00FF, NULL, " ");
	}
	void MouseHandler(float mx,float my, CHAR Button, CHAR State) {
		locker.lock();
		//printf("%X\n", Button);
		list<map<string, MoveableWindow*>::iterator>::iterator AWIterator = ActiveWindows.begin(),CurrentAW;
		CurrentAW = AWIterator;
		BIT flag=0;
		if (!Button && !ActiveWindows.empty())(*ActiveWindows.begin())->second->MouseHandler(mx, my, 0, 0);
		else {
			while (AWIterator != ActiveWindows.end() && !((*AWIterator)->second->MouseHandler(mx, my, Button, State)) && !WindowWasDisabledDuringMouseHandling)
				AWIterator++;
			if (!WindowWasDisabledDuringMouseHandling && ActiveWindows.size() > 1 && AWIterator != ActiveWindows.end() && AWIterator != ActiveWindows.begin())
				if (CurrentAW == ActiveWindows.begin())EnableWindow(*AWIterator);
			if (WindowWasDisabledDuringMouseHandling)
				WindowWasDisabledDuringMouseHandling = 0;
		}
		locker.unlock();
	}
	void ThrowPrompt(string StaticTipText,string WindowTitle, void(*OnSubmit)(),_Align STipAlign,InputField::Type InputType,string DefaultString="",DWORD MaxChars=0) {
		locker.lock();
		auto wptr = Map["PROMPT"];
		auto ifptr = ((InputField*)(*wptr)["FLD"]);
		auto tbptr = ((TextBox*)(*wptr)["TXT"]);
		wptr->SafeWindowRename(WindowTitle);
		ifptr->InputType = InputType;
		ifptr->MaxChars = MaxChars;
		ifptr->UpdateInputString(DefaultString);
		tbptr->TextAlign = STipAlign;
		tbptr->SafeStringReplace(StaticTipText);
		((Button*)(*wptr)["BUTT"])->OnClick = OnSubmit;

		wptr->SafeChangePosition(-50, 50);
		EnableWindow("PROMPT");
		locker.unlock();
	}
	void ThrowAlert(string AlertText, string AlertHeader, void(*SpecialSignsDrawFunc)(float, float, float, DWORD, DWORD), BIT Update = false, DWORD FRGBA = 0, DWORD SRGBA = 0) {
		locker.lock();
		auto AlertWptr = Map["ALERT"];
		AlertWptr->SafeWindowRename(AlertHeader);
		AlertWptr->_NotSafeResize_Centered(50, 200);
		AlertWptr->SafeChangePosition_Argumented(0, 0, 0);
		TextBox *AlertWTptr = (TextBox*)((*AlertWptr)["AlertText"]);
		AlertWTptr->SafeStringReplace(AlertText);
		if (AlertWTptr->CalculatedTextHeight > AlertWTptr->Height) {
			AlertWptr->_NotSafeResize_Centered(AlertWTptr->CalculatedTextHeight + WindowHeapSize, AlertWptr->Width);
		}
		auto AlertWSptr = ((SpecialSignHandler*)(*AlertWptr)["AlertSign"]);
		AlertWSptr->_ReplaceVoidFunc(SpecialSignsDrawFunc);
		if (Update){
			AlertWSptr->FRGBA = FRGBA;
			AlertWSptr->SRGBA = SRGBA;
		}
		EnableWindow("ALERT");
		locker.unlock();
	}
	void DisableWindow(string ID) {
		locker.lock();
		auto Y = Map.find(ID);
		if (Y != Map.end()) {
			WindowWasDisabledDuringMouseHandling = 1;
			Y->second->Drawable = 1;
			ActiveWindows.remove(Y);
		}
		locker.unlock();
	}
	void DisableAllWindows() {
		locker.lock();
		WindowWasDisabledDuringMouseHandling = 1;
		ActiveWindows.clear();
		EnableWindow(MainWindow_ID);
		locker.unlock();
	}
	void TurnOnMainWindow() {
		locker.lock();
		if (this->MW_ID_Holder != "") 
			swap(this->MainWindow_ID, this->MW_ID_Holder);
		this->EnableWindow(MainWindow_ID);
		locker.unlock();
	}
	void TurnOffMainWindow() {
		locker.lock();
		if (this->MW_ID_Holder == "")
			swap(this->MainWindow_ID, this->MW_ID_Holder);
		this->DisableWindow(MainWindow_ID);
		locker.unlock();
	}
	void DisableWindow(list<map<string, MoveableWindow*>::iterator>::iterator Window) {
		locker.lock();
		if (Window == ActiveWindows.end()) {
			locker.lock(); 
			return; 
		}
		WindowWasDisabledDuringMouseHandling = 1;
		(*Window)->second->Drawable = 1;
		ActiveWindows.erase(Window);
		locker.unlock();
	}
	void EnableWindow(map<string, MoveableWindow*>::iterator Window) {
		locker.lock();
		if (Window == Map.end()) {
			locker.lock(); 
			return; 
		}
		for (auto Y = ActiveWindows.begin(), Q = ActiveWindows.begin(); Y != ActiveWindows.end(); Y++) {
			if (*Y == Window) {
				Window->second->Drawable = 1;
				if (Y != ActiveWindows.begin()) {
					Q = Y;
					Q--;
					ActiveWindows.erase(Y);
					Y = Q;
				}
				else {
					ActiveWindows.erase(Y);
					if(ActiveWindows.size())Y = ActiveWindows.begin();
					else break;
				}
			}
		}

		ActiveWindows.push_front(Window);
		locker.unlock();
		//cout << Window->first << " " << ActiveWindows.front()->first << endl;
	}
	void EnableWindow(string ID) {
		locker.lock();
		this->EnableWindow(Map.find(ID));
		locker.unlock();
	}
	void KeyboardHandler(char CH) {
		locker.lock();
		if (ActiveWindows.size())
			(*(ActiveWindows.begin()))->second->KeyboardHandler(CH);
		locker.unlock();
	}
	void Draw() {
		BIT MetMain = 0;
		locker.lock();
		if (!ActiveWindows.empty()) {//if only reverse iterators could be easily converted to usual iterators...
			auto Y = (++ActiveWindows.rbegin()).base();

			while (true) {
				//cout << ((*Y)->first) << endl;
				if (!((*Y)->second->Drawable)) {
					if ((*Y)->first != MainWindow_ID)
						if (Y == ActiveWindows.begin()) {
							DisableWindow(Y);
							break;
						}
						else DisableWindow(Y);
					else (*Y)->second->Drawable = true;
					continue;
				}
				(*Y)->second->Draw();
				if ((*Y)->first == MainWindow_ID)MetMain = 1;
				if (Y == ActiveWindows.begin())break;
				Y--;
			}
		}
		//cout << endl;
		if (!MetMain)this->EnableWindow(MainWindow_ID);
		locker.unlock();
	}
	inline MoveableWindow*& operator[](string ID) {
		return (this->Map[ID]);
	}
};
WindowsHandler* WH;

//////////////////////////////
////TRUE USAGE STARTS HERE////
//////////////////////////////


struct DragNDropHandler : IDropTarget {
	ULONG m_refCount;
	wchar_t m_data[8000];
	DragNDropHandler() {
		m_refCount = 1;
	}
	HRESULT STDMETHODCALLTYPE QueryInterface(const IID& riid, void** ppvObject) override {
		if (riid == IID_IUnknown || riid == IID_IDropTarget) {
			*ppvObject = this;
			AddRef();
			return NOERROR;
		}
		*ppvObject = NULL;
		return ResultFromScode(E_NOINTERFACE);
	}
	ULONG STDMETHODCALLTYPE AddRef() override {
		return ++m_refCount;
	}
	ULONG STDMETHODCALLTYPE Release() override {
		if (--m_refCount == 0) {
			delete this;
			return 0;
		}
		return m_refCount;
	}
	HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* dataObject, DWORD grfKeyState, POINTL mousePos, DWORD* effect) override {
		//MessageBox(hWnd, "dragenter", "Drag", MB_ICONINFORMATION);
		//cout << "a" << endl;
		DRAG_OVER = 1;
		*effect = DROPEFFECT_COPY;
		return NOERROR;
	}
	HRESULT STDMETHODCALLTYPE DragOver(DWORD keyState, POINTL mousePos, DWORD* effect) override {
		//MessageBox(hWnd, "dragover", "Drag", MB_ICONINFORMATION);
		*effect = DROPEFFECT_COPY;
		return NOERROR;
	}
	HRESULT STDMETHODCALLTYPE DragLeave() override {
		//MessageBox(hWnd, "dragleave", "Drag", MB_ICONINFORMATION);
		DRAG_OVER = 0;
		return NOERROR;
	}
	HRESULT STDMETHODCALLTYPE Drop(IDataObject* dataObject, DWORD keyState, POINTL mousePos, DWORD* effect) override {
		//MessageBox(hWnd, "drop", "Drag", MB_ICONINFORMATION);
		//cout << "drop " << m_refCount << endl;
		LPOLESTR szFile = 0;
		FORMATETC fdrop = { CF_HDROP, 0, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
		vector<wstring> WC(1, L"");

		if (SUCCEEDED(dataObject->QueryGetData(&fdrop))) {
			STGMEDIUM stgMedium = { 0 };
			stgMedium.tymed = TYMED_HGLOBAL;
			HRESULT hr = dataObject->GetData(&fdrop, &stgMedium);
			if (SUCCEEDED(hr)) {
				PVOID data = GlobalLock(stgMedium.hGlobal);
				wchar_t *ws = NULL;
				for (int i = *((BYTE*)(data));; i += 2) {
					WC.back().push_back(((*((BYTE*)(data)+i + 1)) << 8) | (*((BYTE*)(data)+i)));
					if (WC.back().back() == 0 && WC.back().size() > 1) {
						WC.back().pop_back();
						WC.push_back(L"");
						continue;
					}
					else if (WC.back().back() == 0) {
						WC.pop_back();
						break;
					}
				}
				AddFiles(WC);
			}
			else ThrowAlert_Error("DND: Get data is not succeeded!");
		}
		else ThrowAlert_Error("DND: Query get data is not succeeded!");
		DRAG_OVER = 0;
		*effect = DROPEFFECT_COPY;
		return NOERROR;
	}
};
DragNDropHandler DNDH_Global;

BIT IsWhiteKey(BYTE Key) {
	Key %= 12;
	if (Key < 5)return !(Key & 1);
	else return (Key & 1);
}
struct CAT_Piano :HandleableUIPart {
	CutAndTransposeKeys *PianoTransform;
	SingleTextLine *MinCont, *MaxCont, *Transp;
	float CalculatedHeight, CalculatedWidth;
	float BaseXPos, BaseYPos, PianoHeight, KeyWidth;
	BIT Focused;
	~CAT_Piano() {
		delete MinCont;
		delete MaxCont;
		delete Transp;
	}
	CAT_Piano(float BaseXPos, float BaseYPos, float KeyWidth, float PianoHeight, CutAndTransposeKeys *PianoTransform) {
		this->BaseXPos = BaseXPos;
		this->BaseYPos = BaseYPos;
		this->KeyWidth = KeyWidth;
		this->PianoHeight = PianoHeight;
		this->CalculatedHeight = PianoHeight * 4;
		this->CalculatedWidth = KeyWidth * (128 * 3);
		this->PianoTransform = PianoTransform;
		this->Focused = 0;
		System_White->SetNewPos(BaseXPos + KeyWidth * (128 * 1.25f), BaseYPos - 0.75f*PianoHeight);
		this->MinCont = System_White->CreateOne("_");
		System_White->SetNewPos(BaseXPos - KeyWidth * (128 * 1.25f), BaseYPos - 0.75f*PianoHeight);
		this->MaxCont = System_White->CreateOne("_");
		this->Transp = System_White->CreateOne("_");
		UpdateInfo();
	}
	void UpdateInfo() {
		Lock.lock();
		if (!PianoTransform) {
			Lock.unlock(); return;
		}
		MinCont->SafeStringReplace("Min: " + to_string(PianoTransform->Min));
		MaxCont->SafeStringReplace("Max: " + to_string(PianoTransform->Max));
		Transp->SafeStringReplace("Transp: " + to_string(PianoTransform->TransposeVal));
		Transp->SafeChangePosition(BaseXPos + ((PianoTransform->TransposeVal >= 0)?1:-1)* KeyWidth * (128 * 1.25f), BaseYPos +
			0.75*PianoHeight
		);
		Lock.unlock();
	}
	void Draw() override {
		Lock.lock();
		float x = BaseXPos - 128 * KeyWidth, pixsz = RANGE / WINDXSIZE;
		BIT Inside = 0;

		MinCont->Draw();
		MaxCont->Draw();
		Transp->Draw();
		if (!PianoTransform) {
			Lock.unlock(); 
			return;
		}

		glLineWidth(0.5f*KeyWidth / pixsz);
		glBegin(GL_LINES);
		for (int i = 0; i < 256; i++, x += KeyWidth) {//Main piano
			Inside = ((i >= (PianoTransform->Min)) && (i <= (PianoTransform->Max)));
			if (IsWhiteKey(i)) {
				glColor4f(1, 1, 1, (Inside ? 0.9f : 0.25f));
				glVertex2f(x, BaseYPos - 1.25f*PianoHeight);
				glVertex2f(x, BaseYPos - 0.25f*PianoHeight);
				continue;
			}
			else {
				glColor4f(0, 0, 0, (Inside ? 0.9f : 0.25f));
				glVertex2f(x, BaseYPos - 0.75f*PianoHeight);
				glVertex2f(x, BaseYPos - 0.25f*PianoHeight);
			}
			glColor4f(1, 1, 1, (Inside ? 0.9f : 0.25f));
			glVertex2f(x, BaseYPos - 0.75f*PianoHeight);
			glVertex2f(x, BaseYPos - 1.25f*PianoHeight);
		}
		x = BaseXPos - (128 + PianoTransform->TransposeVal)*KeyWidth;
		for (int i = 0; i < 256; i++, x += KeyWidth) {//CAT Piano
			if (fabs(x - BaseXPos) >= 0.5*CalculatedWidth)
				continue;
			Inside = ((i - (PianoTransform->TransposeVal) >= (PianoTransform->Min)) && (i - (PianoTransform->TransposeVal) <= (PianoTransform->Max)));
			if (IsWhiteKey(i)) {
				glColor4f(1, 1, 1, (Inside ? 0.9f : 0.25f));
				glVertex2f(x, BaseYPos + 1.25f*PianoHeight);
				glVertex2f(x, BaseYPos + 0.25f*PianoHeight);
				continue;
			}
			else {
				glColor4f(0, 0, 0, (Inside ? 0.9f : 0.25f));
				glVertex2f(x, BaseYPos + 0.75f*PianoHeight);
				glVertex2f(x, BaseYPos + 0.25f*PianoHeight);
			}
			glColor4f(1, 1, 1, (Inside ? 0.9f : 0.25f));
			glVertex2f(x, BaseYPos + 1.25f*PianoHeight);
			glVertex2f(x, BaseYPos + 0.75f*PianoHeight);
		}
		glEnd();
		
		x = BaseXPos - (128 - PianoTransform->Min + 0.5f)*KeyWidth;//Square
		glLineWidth(1);
		glColor4f(0, 1, 0, 0.75f);
		glBegin(GL_LINE_LOOP);
		glVertex2f(x, BaseYPos - 1.5f * PianoHeight);
		glVertex2f(x, BaseYPos + 1.5f * PianoHeight);
		x += KeyWidth * (PianoTransform->Max - PianoTransform->Min + 1);
		glVertex2f(x, BaseYPos + 1.5f * PianoHeight);
		glVertex2f(x, BaseYPos - 1.5f * PianoHeight);
		glEnd();
		if (!Focused) {
			Lock.unlock();
			return;
		}
		glColor4f(1, 0.5, 0, 1);
		glBegin(GL_LINE_LOOP);
		glVertex2f(BaseXPos - 0.5f*CalculatedWidth, BaseYPos - 0.5f*CalculatedHeight);
		glVertex2f(BaseXPos - 0.5f*CalculatedWidth, BaseYPos + 0.5f*CalculatedHeight);
		glVertex2f(BaseXPos + 0.5f*CalculatedWidth, BaseYPos + 0.5f*CalculatedHeight);
		glVertex2f(BaseXPos + 0.5f*CalculatedWidth, BaseYPos - 0.5f*CalculatedHeight);
		glEnd();
		Lock.unlock();
	}
	void SafeMove(float dx, float dy) override {
		Lock.lock();
		BaseXPos += dx;
		BaseYPos += dy;
		this->MaxCont->SafeMove(dx, dy);
		this->MinCont->SafeMove(dx, dy);
		this->Transp->SafeMove(dx, dy);
		Lock.unlock();
	}
	void SafeChangePosition(float NewX, float NewY) override {
		Lock.lock();
		NewX -= BaseXPos;
		NewY -= BaseYPos;
		SafeMove(NewX, NewY);
		Lock.unlock();
	}
	void SafeChangePosition_Argumented(BYTE Arg, float NewX, float NewY) override {
		Lock.lock();
		float CW = 0.5f*(
				(INT32)((BIT)(GLOBAL_LEFT&Arg))
			-	(INT32)((BIT)(GLOBAL_RIGHT&Arg))
		)*CalculatedWidth,
			  CH = 0.5f*(
				(INT32)((BIT)(GLOBAL_BOTTOM&Arg))
			-	(INT32)((BIT)(GLOBAL_TOP&Arg))
		)*CalculatedHeight;
		SafeChangePosition(NewX + CW, NewY + CH);
		Lock.unlock();
	}
	void KeyboardHandler(CHAR CH) override {
		Lock.lock();
		if (Focused) {
			if (!PianoTransform) {
				Lock.unlock();
				return;
			}
			switch (CH) {
			case 'W':
			case 'w':
				if (PianoTransform->TransposeVal < 255) {
					PianoTransform->TransposeVal += 1;
					UpdateInfo();
				}
				break;
			case 'S':
			case 's':
				if (PianoTransform->TransposeVal > -255) { 
					PianoTransform->TransposeVal -= 1; 
					UpdateInfo();
				}
				break;
			case 'D':
			case 'd':
				if (PianoTransform->Min < 255) {
					PianoTransform->Min += 1; 
					UpdateInfo();
				}
				break;
			case 'A':
			case 'a':
				if (PianoTransform->Min > 0) {
					PianoTransform->Min -= 1;
					UpdateInfo();
				}
				break;
			case 'E':
			case 'e':
				if (PianoTransform->Max < 255) {
					PianoTransform->Max += 1;
					UpdateInfo();
				}
				break;
			case 'Q':
			case 'q':
				if (PianoTransform->Max > 0) {
					PianoTransform->Max -= 1;
					UpdateInfo();
				}
				break;
			}
		}
		Lock.unlock();
	}
	void SafeStringReplace(string Meaningless) override {
		return;
	}
	BIT MouseHandler(float mx, float my, CHAR Button, CHAR State) override {
		Lock.lock();
		mx -= BaseXPos;
		my -= BaseYPos;
		if (fabsf(mx) <= CalculatedWidth * 0.5f && fabsf(my) <= CalculatedHeight * 0.5)
			Focused = 1;
		else Focused = 0;
		Lock.unlock();
		return 0;
	}
};

struct PLC_VolumeWorker:HandleableUIPart {
	PLC<BYTE, BYTE> *PLC_bb;
	pair<BYTE, BYTE> _HoveredPoint;
	SingleTextLine *STL_MSG;
	float CXPos, CYPos, HorizontalSidesSize,VerticalSidesSize;
	float MouseX, MouseY,_xsqsz,_ysqsz;
	BIT Hovered,ActiveSetting,RePutMode/*JustPut=0*/;
	BYTE XCP, YCP;
	BYTE FPX, FPY;
	~PLC_VolumeWorker() {
		delete STL_MSG;
	}
	PLC_VolumeWorker(float CXPos,float CYPos,float Width,float Height,PLC<BYTE,BYTE> *PLC_bb =NULL) {
		this->PLC_bb = PLC_bb;
		this->CXPos = CXPos;
		this->CYPos = CYPos;
		this->_xsqsz = Width/256;
		this->_ysqsz = Height/256;
		this->HorizontalSidesSize = Width;
		this->VerticalSidesSize = Height;

		this->STL_MSG = new SingleTextLine("_", CXPos, CYPos, System_White->XUnitSize, System_White->YUnitSize, System_White->SpaceWidth, 2, 0xFFAFFFCF, new DWORD(0xAFFFAFCF), (7 << 4) | 3);
		this->MouseX = this->MouseY = 0.f;
		this->_HoveredPoint = pair<BYTE, BYTE>(0, 0);
		ActiveSetting = Hovered = FPX = FPY = XCP = YCP = 0;
	}
	void Draw() override {
		Lock.lock();
		float begx = CXPos - 0.5f*HorizontalSidesSize, begy = CYPos - 0.5f*VerticalSidesSize;
		glColor4f(1, 1, 1, (Hovered)? 0.85f : 0.5f);
		glLineWidth(1);
		glBegin(GL_LINE_LOOP);
		glVertex2f(begx, begy);
		glVertex2f(begx, begy + VerticalSidesSize);
		glVertex2f(begx + HorizontalSidesSize, begy + VerticalSidesSize);
		glVertex2f(begx + HorizontalSidesSize, begy);
		glEnd();
		glColor4f(0.5, 1, 0.5, 0.05);//showing "safe" for volumes square 
		glBegin(GL_QUADS);
		glVertex2f(begx, begy);
		glVertex2f(begx, begy + 0.5f*VerticalSidesSize);
		glVertex2f(begx + 0.5f*HorizontalSidesSize, begy + 0.5f*VerticalSidesSize);
		glVertex2f(begx + 0.5f*HorizontalSidesSize, begy);
		glEnd();

		if (Hovered) {
			glColor4f(1, 1, 1, 0.25);
			glBegin(GL_LINES);
			glVertex2f(begx + _xsqsz * XCP, begy);
			glVertex2f(begx + _xsqsz * XCP, begy + VerticalSidesSize);
			glVertex2f(begx, begy + _ysqsz * YCP);
			glVertex2f(begx + HorizontalSidesSize, begy + _ysqsz * YCP);
			glVertex2f(begx + _xsqsz * (XCP + 1), begy);
			glVertex2f(begx + _xsqsz * (XCP + 1), begy + VerticalSidesSize);
			glVertex2f(begx, begy + _ysqsz * (YCP + 1));
			glVertex2f(begx + HorizontalSidesSize, begy + _ysqsz * (YCP + 1));
			glEnd();
		}
		if (PLC_bb && PLC_bb->ConversionMap.size()) {
			glLineWidth(3);
			glColor4f(1, 0.75f, 1, 0.5f);
			glBegin(GL_LINE_STRIP);
			glVertex2f(begx, begy + _ysqsz * (PLC_bb->AskForValue(0) + 0.5f));

			for (auto Y = PLC_bb->ConversionMap.begin(); Y != PLC_bb->ConversionMap.end(); Y++)
				glVertex2f(begx + (Y->first + 0.5f)*_xsqsz, begy + (Y->second + 0.5f)*_ysqsz);

			glVertex2f(begx + 255.5f*_xsqsz, begy + _ysqsz * (PLC_bb->AskForValue(255) + 0.5f));
			glEnd();
			glColor4f(1, 1, 1, 0.75f);
			glPointSize(5);
			glBegin(GL_POINTS);
			for (auto Y = PLC_bb->ConversionMap.begin(); Y != PLC_bb->ConversionMap.end(); Y++)
				glVertex2f(begx + (Y->first + 0.5f)*_xsqsz, begy + (Y->second + 0.5f)*_ysqsz);
			glEnd();
		}
		if (ActiveSetting) {
			map<BYTE, BYTE>::iterator Y;
			glBegin(GL_LINES);
			glVertex2f(begx + (FPX + 0.5f) * _xsqsz, begy + (FPY + 0.5f) * _ysqsz);
			glVertex2f(begx + (XCP + 0.5f) * _xsqsz, begy + (YCP + 0.5f) * _ysqsz);
			if (FPX < XCP) {
				Y = PLC_bb->ConversionMap.upper_bound(XCP);
				if (Y != PLC_bb->ConversionMap.end()) {
					glVertex2f(begx + (Y->first + 0.5f) * _xsqsz, begy + (Y->second + 0.5f) * _ysqsz);
					glVertex2f(begx + (XCP + 0.5f) * _xsqsz, begy + (YCP + 0.5f) * _ysqsz);
				}
				Y = PLC_bb->ConversionMap.lower_bound(FPX);
				if (Y != PLC_bb->ConversionMap.end() && Y != PLC_bb->ConversionMap.begin()) {
					Y--;
					glVertex2f(begx + (Y->first + 0.5f) * _xsqsz, begy + (Y->second + 0.5f) * _ysqsz);
					glVertex2f(begx + (FPX + 0.5f) * _xsqsz, begy + (FPY + 0.5f) * _ysqsz);
				}
			}
			else {
				Y = PLC_bb->ConversionMap.upper_bound(FPX);
				if (Y != PLC_bb->ConversionMap.end()) {
					glVertex2f(begx + (Y->first + 0.5f) * _xsqsz, begy + (Y->second + 0.5f) * _ysqsz);
					glVertex2f(begx + (FPX + 0.5f) * _xsqsz, begy + (FPY + 0.5f) * _ysqsz);
				}
				Y = PLC_bb->ConversionMap.lower_bound(XCP);
				if (Y != PLC_bb->ConversionMap.end() && Y != PLC_bb->ConversionMap.begin()) {
					Y--;
					glVertex2f(begx + (Y->first + 0.5f) * _xsqsz, begy + (Y->second + 0.5f) * _ysqsz);
					glVertex2f(begx + (XCP + 0.5f) * _xsqsz, begy + (YCP + 0.5f) * _ysqsz);
				}
			}
			glEnd();
		}
		STL_MSG->Draw();
		Lock.unlock();
	}
	void UpdateInfo() {
		Lock.lock();
		SHORT tx = (128. + floor(255 * (MouseX - CXPos) / HorizontalSidesSize)),
			  ty = (128. + floor(255 * (MouseY - CYPos) / VerticalSidesSize));
		if (tx < 0 || tx>255 || ty < 0 || ty>255) {
			if (Hovered) {
				STL_MSG->SafeStringReplace("-:-");
				Hovered = 0;
			}
		}
		else {
			Hovered = 1;
			STL_MSG->SafeStringReplace(to_string(XCP = tx) + ":" + to_string(YCP = ty));
		}
		Lock.unlock();
	}
	void _MakeMapMoreSimple(){
		Lock.lock();
		if (PLC_bb) {
			BYTE TF, TS;
			for (auto Y = PLC_bb->ConversionMap.begin(); Y != PLC_bb->ConversionMap.end();) {
				TF = Y->first;
				TS = Y->second;
				Y = PLC_bb->ConversionMap.erase(Y);
				if (PLC_bb->AskForValue(TF) != TS) {
					PLC_bb->ConversionMap[TF] = TS;
				}
			}
		}
		Lock.unlock();
	}
	void SafeMove(float dx, float dy) override {
		Lock.lock();
		CXPos += dx;
		CYPos += dy;
		this->STL_MSG->SafeMove(dx, dy);
		Lock.unlock();
	}
	void SafeChangePosition(float NewX, float NewY) override {
		Lock.lock();
		NewX -= CXPos;
		NewY -= CYPos;
		SafeMove(NewX, NewY);
		Lock.unlock();
	}
	void SafeChangePosition_Argumented(BYTE Arg, float NewX, float NewY) override {
		Lock.lock();
		float CW = 0.5f*(
				(INT32)((BIT)(GLOBAL_LEFT&Arg))
			-	(INT32)((BIT)(GLOBAL_RIGHT&Arg))
			)*HorizontalSidesSize,
			CH = 0.5f*(
				(INT32)((BIT)(GLOBAL_BOTTOM&Arg))
			-	(INT32)((BIT)(GLOBAL_TOP&Arg))
			)*VerticalSidesSize;
		SafeChangePosition(NewX + CW, NewY + CH);
		Lock.unlock();
	}
	void KeyboardHandler(CHAR CH) override {
		///hmmmm ... should i... meh...
	}
	void SafeStringReplace(string Meaningless) override {
		/// ... ///
	}
	void RePutFromAtoB(BYTE A, BYTE B, BYTE ValA, BYTE ValB) {
		Lock.lock();
		if (PLC_bb) {
			if (B < A) {
				swap(A, B);
				swap(ValA, ValB);
			}
			auto itA = PLC_bb->ConversionMap.lower_bound(A),itB = PLC_bb->ConversionMap.upper_bound(B);
			while (itA != itB)
				itA = PLC_bb->ConversionMap.erase(itA);

			PLC_bb->InsertNewPoint(A, ValA);
			PLC_bb->InsertNewPoint(B, ValB);
		}
		Lock.unlock();
	}
	void JustPutNewValue(BYTE A, BYTE ValA) {
		Lock.lock();
		if (PLC_bb)
			PLC_bb->InsertNewPoint(A, ValA);
		Lock.unlock();
	}
	BIT MouseHandler(float mx, float my, CHAR Button, CHAR State) override {
		Lock.lock();
		this->MouseX = mx;
		this->MouseY = my;
		UpdateInfo();
		if (Hovered) {
			if (Button) {
				if (Button == -1) {
					if (this->ActiveSetting) {
						if (State == 1) {
							ActiveSetting = 0;
							RePutFromAtoB(FPX, XCP, FPY, YCP);
						}
					}
					else {
						if (this->RePutMode) {
							if (State == 1) {
								ActiveSetting = 1;
								FPX = XCP;
								FPY = YCP;
							}
						}
						else {
							if (State == 1) {
								JustPutNewValue(XCP, YCP);
							}
						}
					}
				}
				else {//Removing some point
					///i give up
				}
			}
			else {
				if (PLC_bb) {
					///no ne///here i should've implement approaching to point.... meh
				}
			}
		}
		Lock.unlock();
		return 0;
	}
};

struct SMRP_Vis: HandleableUIPart {
	SingleMIDIReProcessor *SMRP;
	
	float XPos, YPos;
	BIT Processing, Finished, Hovered;
	SingleTextLine *STL_Log,*STL_War,*STL_Err,*STL_Info;
	SMRP_Vis(float XPos, float YPos, SingleTextLineSettings *STLS) {
		Lock.lock();
		SMRP = nullptr;
		DWORD BASERGBA;
		this->Processing = this->Hovered = this->Finished = 0;
		this->XPos = XPos;
		this->YPos = YPos;
		BASERGBA = STLS->RGBAColor;
		STLS->RGBAColor = 0xFFFFFFFF;
		STLS->SetNewPos(XPos, YPos - 20);
		this->STL_Log = STLS->CreateOne("_");
		STLS->RGBAColor = 0xFFFF00FF;
		STLS->SetNewPos(XPos, YPos - 30);
		this->STL_War = STLS->CreateOne("_");
		STLS->RGBAColor = 0xFF3F00FF;
		STLS->SetNewPos(XPos, YPos - 40);
		this->STL_Err = STLS->CreateOne("_");
		STLS->RGBAColor = 0xFFFFFFFF;
		STLS->SetNewPos(XPos, YPos + 40);
		this->STL_Info = STLS->CreateOne("_");
		STLS->RGBAColor = BASERGBA;
		Lock.unlock();
	}
	void SafeMove(float dx, float dy) override {
		Lock.lock();
		XPos += dx;
		YPos += dy;
		STL_Err->SafeMove(dx, dy);
		STL_War->SafeMove(dx, dy);
		STL_Log->SafeMove(dx, dy);
		STL_Info->SafeMove(dx, dy);
		Lock.unlock();
	}
	void SafeChangePosition(float NewX, float NewY) override {
		Lock.lock();
		NewX -= XPos;
		NewY -= YPos;
		SafeMove(NewX, NewY);
		Lock.unlock();
	}
	void SafeChangePosition_Argumented(BYTE Arg, float NewX, float NewY) override {
		return;
	}
	void KeyboardHandler(CHAR CH) override {
		return;
	}
	void SafeStringReplace(string Meaningless) override {
		return;
	}
	void SetInfoString(string NewInfoString) {
		Lock.lock();
		this->STL_Info->SafeStringReplace(NewInfoString);
		Lock.unlock();
	}
	void UpdateInfo() {
		if (!SMRP)return;
		Lock.lock();
		string T;
		if (SMRP->LogLine.size() && SMRP->LogLine != STL_Log->_CurrentText)
			STL_Log->SafeStringReplace(SMRP->LogLine);
		if (SMRP->WarningLine.size() && SMRP->WarningLine != STL_War->_CurrentText)
			STL_War->SafeStringReplace(SMRP->WarningLine);
		if (SMRP->ErrorLine.size() && SMRP->ErrorLine != STL_Err->_CurrentText)
			STL_Err->SafeStringReplace(SMRP->ErrorLine);
		if (Processing != SMRP->Processing)
			Processing = SMRP->Processing;
		if (Finished != SMRP->Finished)
			Finished = SMRP->Finished;
		T = to_string((SMRP->FilePosition * 100.) / (SMRP->FileSize)).substr(0,5) + "%";
		if (Hovered && Processing) {
			T = SMRP->AppearanceFilename.substr(0,30) + " " + T;
			STL_Info->SafeColorChange(0x9FCFFFFF);
		}
		else {
			STL_Info->SafeColorChange(0xFFFFFFFF);
		}
		if (STL_Info->_CurrentText != T)
			STL_Info->SafeStringReplace(T);
		Lock.unlock();
	}
	void Draw() override {
		Lock.lock();
		if (!SMRP) {
			if (STL_Err->_CurrentText != "SMRP is NULL!")
				STL_Err->SafeStringReplace("SMRP is NULL!");
		}
		else {
			if (STL_Err->_CurrentText == "SMRP is NULL!")
				STL_Err->SafeStringReplace("_");
			UpdateInfo();
		}
		this->STL_Err->Draw();
		this->STL_War->Draw();
		this->STL_Log->Draw();
		this->STL_Info->Draw();
		if (SMRP) {
			if (Processing && !Finished)SpecialSigns::DrawWait(XPos, YPos, 15, 0x007FFF7F, 20);
			else {
				if (Finished)SpecialSigns::DrawOK(XPos, YPos, 20, 0x00FFFFFF);
				else SpecialSigns::DrawNo(XPos, YPos, 20, 0xFF3F00FF);
			}
		}
		Lock.unlock();
	}
	BIT MouseHandler(float mx, float my, CHAR Button, CHAR State) override {
		Lock.lock();
		mx -= XPos;
		my -= YPos;
		if (mx * mx + my * my < 900)
			Hovered = 1;
		else
			Hovered = 0;
		Lock.unlock();
		return 0;
	}
};

struct BoolAndWORDChecker : HandleableUIPart {
	float XPos, YPos;
	BIT *Flag;
	WORD *Number;
	SingleTextLine *STL_Info;
	BoolAndWORDChecker(float XPos, float YPos, SingleTextLineSettings *STLS, BIT *Flag, WORD *Number) {
		this->XPos = XPos;
		this->YPos = YPos;
		this->Flag = Flag;
		this->Number = Number;
		STLS->SetNewPos(XPos, YPos + 40);
		this->STL_Info = STLS->CreateOne("_");
	}
	void SafeMove(float dx, float dy) override {
		Lock.lock();
		XPos += dx;
		YPos += dy;
		STL_Info->SafeMove(dx, dy);
		Lock.unlock();
	}
	void SafeChangePosition(float NewX, float NewY) override {
		Lock.lock();
		NewX -= XPos;
		NewY -= YPos;
		SafeMove(NewX, NewY);
		Lock.unlock();
	}
	void SafeChangePosition_Argumented(BYTE Arg, float NewX, float NewY) override {
		return;
	}
	void KeyboardHandler(CHAR CH) override {
		return;
	}
	void SafeStringReplace(string Meaningless) override {
		return;
	}
	void SetInfoString(string NewInfoString) {
		return;
	}
	void UpdateInfo() {
		Lock.lock();
		if (Number) {
			string T = to_string(*Number);
			if(T!=STL_Info->_CurrentText)STL_Info->SafeStringReplace(T);
		}
		Lock.unlock();
	}
	void Draw() override {
		Lock.lock();
		UpdateInfo();
		if (Flag) {
			if (*Flag)SpecialSigns::DrawOK(XPos, YPos, 15, 0x00FFFFFF);
			else SpecialSigns::DrawWait(XPos, YPos, 15, 0x007FFFFF, 20);
		}
		else SpecialSigns::DrawNo(XPos, YPos, 15, 0xFF0000FF);
		STL_Info->Draw();
		Lock.unlock();
	}
	BIT MouseHandler(float mx, float my, CHAR Button, CHAR State) override {
		return 0;
	}
};

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
		WH->ThrowAlert(AlertText, "ERROR!", SpecialSigns::DrawExTriangle, 1, 0xFF7F00AF, 0xFF);
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
		ThrowAlert_Error("If you see this, then midi info collector is not done yet :)");
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
	_Data.RemoveByID(ptr->SelectedID);
	ptr->SafeRemoveStringByID(ptr->SelectedID);
	WH->DisableWindow("SMPAS");
	WH->DisableWindow("VM");
	WH->DisableWindow("CAT");
	_Data.SetGlobalPPQN();
	_Data.ResolveSubdivisionProblem_GroupIDAssign();
}

void OnRemAll() {
	auto ptr = _WH_t("MAIN", "List", SelectablePropertedList*);
	WH->DisableWindow("SMPAS");
	WH->DisableWindow("VM");
	WH->DisableWindow("CAT");
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
		if (T.size()) {
			ROT_ANGLE = stof(T);
			//not save
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
	printf("MCTM Prcessing has begun\n");
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
			MW->AddUIElement("SMRP_C" + to_string(ID), Vis);
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

	(*T)["MIDIINFO"] = Butt = new Button("Collect info", System_White, PropsAndSets::InitializeCollecting, 20, 15 - WindowHeapSize, 65, 10, 1, 0x7F7F7F3F, 0x7F7F7FFF, 0xFFFFFFFF, 0xFFFFFF3F, 0xFFFFFFFF, System_White, "Collects additional info about the midi");
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

	T = new MoveableWindow("Sustains/Overlaps remover", System_White, -100, 25, 200, 45, 0x3F3F3FCF, 0x7F7F7F7F);
	(*T)["TEXT"] = new TextBox("sdokflsdkflk", System_White, 0, -WindowHeapSize + 15, 15, 180, 10, 0, 0, 0, _Align::center);

	(*WH)["OR"] = T;

	WH->EnableWindow("MAIN");
	//WH->EnableWindow("OR");
	//WH->EnableWindow("SMRP_CONTAINER");
	//WH->EnableWindow("APP_SETTINGS");
	//WH->EnableWindow("VM");
	//WH->EnableWindow("CAT");
	//WH->EnableWindow("SMPAS");//Debug line
	//WH->EnableWindow("PROMPT");////DEBUUUUG
	
	DragAcceptFiles(hWnd, TRUE);
	OleInitialize(NULL);
	cout << "RDD " << (RegisterDragDrop(hWnd, &DNDH_Global)) << endl;
	
	SAFC_VersionCheck();
}


///////////////////////////////////////
/////////////END OF USE////////////////
///////////////////////////////////////

const GLchar *vertex_shader[] = {
	"#version 120\n",
	"void main(void) {\n",//rand(gl_FragCoord.xy + vec2(Time,2.*Time))/2.
	"    vec4 pos = ftransform();\n",
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
	if(!(TimerV&0xF))lFontSymbolsInfo::InitialiseFont(FONTNAME);
	glClear(GL_COLOR_BUFFER_BIT | GL_ACCUM_BUFFER_BIT);
	glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
	if (FIRSTBOOT) {
		FIRSTBOOT = 0;

		//Settings::ShaderMode = rand() & 3;
		if (glCreateShader) {
			SP = new shader_prog(vertex_shader, fragment_shader);
			cout << "GLEW is working! Yeee ( -3-)\n";
		}
		else cout << "GLEW is not initialised\n";

		WH = new WindowsHandler();
		Init();
		//WH->ThrowAlert("kokoko", "alerttest", SpecialSigns::DrawWait, 1, 0xFF00FF7F, 20);//
		if (APRIL_FOOL) {
			WH->ThrowAlert("Today is a special day! ( -w-)\nTake a walk and come back tommorow\n(-w- )", "1st of April!", SpecialSigns::DrawWait, 1, 0xFF00FFFF, 20);
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
		SMRP->_NotSafeResize_Centered(RANGE * 3 *(WindY / WINDYSIZE), RANGE * 3 *(WindX / WINDXSIZE));
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
	glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA);
	glutInitWindowSize(WINDXSIZE, WINDYSIZE);
	//glutInitWindowPosition(50, 0);
	glutCreateWindow(WINDOWTITLE);

	hWnd = FindWindowA(NULL, WINDOWTITLE);

	if(APRIL_FOOL)
		glBlendFunc(GL_SRC_ALPHA, GL_ONE);//_MINUS_SRC_ALPHA
	else
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