#include "app_state.h"

#pragma comment (lib, "Urlmon.lib")//Urlmon.lib
#pragma comment (lib, "wininet.lib")//Urlmon.lib
#pragma comment (lib, "dwmapi.lib")
#pragma comment (lib, "Ws2_32.Lib")
#pragma comment (lib, "Wldap32.Lib")
#pragma comment (lib, "Crypt32.Lib")
#pragma comment (lib, "XmlLite.lib")

syncore_preferences saved_syncore_preferences{};
syncore_preferences syncore_preferences_draft{};
button_settings* bs_list_black_small = new button_settings(&system_white, 0, 0, 100, 10, 1, 0, 0, 0xFFEFDFFF, 0x00003F7F, 0x7F7F7FFF);

std::uint32_t default_bool_settings = _BoolSettings::remove_remnants | _BoolSettings::remove_empty_tracks | _BoolSettings::all_instruments_to_piano;
safc_data g_data;
std::shared_ptr<midi_collection_threaded_merger> global_mctm;
std::atomic_bool application_shutting_down{false};

bool gui_stop_requested(std::stop_token stop_token) noexcept
{
	return stop_token.stop_requested() ||
		application_shutting_down.load(std::memory_order_acquire);
}

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
handleable_ui_part* _WH(const char* window, const char* element)
{
	return ((*(*global_window_handler)[window])[element]);
}
