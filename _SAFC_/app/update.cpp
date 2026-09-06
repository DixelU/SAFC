#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "update.h"
#include "app_workers.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <stop_token>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>
#include <io.h>

#include <Windows.h>
#include <shellapi.h>
#include <urlmon.h>

#include <boost/algorithm/string.hpp>
#include <archive.h>
#include <archive_entry.h>

#include "../JSON/JSON.h"
#include "../SAFGUIF/header_utils.h"

#pragma comment(lib, "Version.lib")
#pragma comment(lib, "Urlmon.lib")

version_t g_version_tuple{};

// Fully-qualified path of the running executable. GetModuleFileNameW(NULL, ...)
// has been observed to return a bare "SAFC.exe" (no directory) when the process
// is launched from the Start Menu, which collapses every path the updater
// derives from it (backup, downloaded archive, re-extracted exe) down to the
// current working directory -- making the auto-update a no-op. QueryFullProcessImageNameW
// is kernel-backed and always yields the fully qualified image path regardless
// of how the process was started; GetModuleFileNameW is kept only as a fallback.
static std::wstring get_self_path()
{
	wchar_t buffer[MAX_PATH] = {0};
	DWORD size = MAX_PATH;
	if (QueryFullProcessImageNameW(GetCurrentProcess(), 0, buffer, &size) && size)
		return std::wstring(buffer, size);

	DWORD len = GetModuleFileNameW(NULL, buffer, MAX_PATH);
	return std::wstring(buffer, len);
}

version_t get_executable_version()
{
	// get the filename of the executable containing the version resource
	const std::wstring exe_path = get_self_path();
	if (exe_path.empty())
		return {0,0,0,0};
	const wchar_t* szFilename = exe_path.c_str();
	// allocate a block of memory for the version info
	DWORD dummy;
	std::uint32_t dwSize = GetFileVersionInfoSize(szFilename, &dummy);
	if (dwSize == 0)
		return {0,0,0,0};
	std::vector<std::uint8_t> data(dwSize);
	// load the version info
	if (!GetFileVersionInfo(szFilename, 0, dwSize, &data[0]))
		return {0,0,0,0};
	////////////////////////////////////
	UINT                uiVerLen = 0;
	VS_FIXEDFILEINFO* pFixedInfo = 0;     // pointer to fixed file info structure
	// get the fixed file info (language-independent)
	if (VerQueryValue(&data[0], TEXT("\\"), (void**)&pFixedInfo, (UINT*)&uiVerLen) == 0)
		return {0,0,0,0};
	return
	{
		HIWORD(pFixedInfo->dwProductVersionMS),
		LOWORD(pFixedInfo->dwProductVersionMS),
		HIWORD(pFixedInfo->dwProductVersionLS),
		LOWORD(pFixedInfo->dwProductVersionLS)
	};
}

static std::wstring extract_directory(const std::wstring& path)
{
	constexpr wchar_t delim = (L"\\")[0];
	auto last_delim_pos = path.find_last_of(delim);
	if (last_delim_pos == std::wstring::npos)
		return L"";

	return path.substr(0, last_delim_pos + 1);
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

class timeout_bind_status_callback : public IBindStatusCallback
{
public:
	timeout_bind_status_callback(DWORD total_timeout_ms, DWORD stall_timeout_ms,
		std::stop_token stop_token = {})
		: m_ref(1)
		, m_total_timeout_ms(total_timeout_ms)
		, m_stall_timeout_ms(stall_timeout_ms)
		, m_start_tick(GetTickCount64())
		, m_last_progress_tick(GetTickCount64())
		, m_binding(nullptr)
		, m_stop(false)
		, m_stop_token(stop_token)
	{
		m_watchdog = std::thread([this]()
		{
			std::unique_lock<std::mutex> lk(m_mtx);
			while (!m_stop)
			{
				if (m_cv.wait_for(lk, std::chrono::milliseconds(250), [this]()
				{
					return m_stop.load() || m_stop_token.stop_requested();
				}))
				{
					if (m_stop.load())
						return;

					IBinding* b = m_binding;
					if (b) b->AddRef();
					lk.unlock();
					if (b) { b->Abort(); b->Release(); }
					return;
				}

				auto now = GetTickCount64();
				bool total_expired = (now - m_start_tick) > m_total_timeout_ms;
				bool stall_expired = (now - m_last_progress_tick.load()) > m_stall_timeout_ms;
				if (!total_expired && !stall_expired)
					continue;

				IBinding* b = m_binding;
				if (b) b->AddRef();
				lk.unlock();
				if (b) { b->Abort(); b->Release(); }
				return;
			}
		});
	}

	~timeout_bind_status_callback()
	{
		{
			std::lock_guard<std::mutex> lk(m_mtx);
			m_stop = true;
		}
		m_cv.notify_all();
		if (m_watchdog.joinable()) m_watchdog.join();
		if (m_binding) { m_binding->Release(); m_binding = nullptr; }
	}

	STDMETHODIMP QueryInterface(REFIID riid, void** ppv) override
	{
		if (!ppv) return E_POINTER;
		if (riid == IID_IUnknown || riid == IID_IBindStatusCallback)
		{
			*ppv = static_cast<IBindStatusCallback*>(this);
			AddRef();
			return S_OK;
		}
		*ppv = nullptr;
		return E_NOINTERFACE;
	}
	STDMETHODIMP_(ULONG) AddRef() override { return ++m_ref; }
	STDMETHODIMP_(ULONG) Release() override
	{
		ULONG r = --m_ref;
		if (r == 0) delete this;
		return r;
	}

	STDMETHODIMP OnStartBinding(DWORD, IBinding* binding) override
	{
		std::lock_guard<std::mutex> lk(m_mtx);
		if (binding) { binding->AddRef(); m_binding = binding; }
		return S_OK;
	}
	STDMETHODIMP GetPriority(LONG*) override { return E_NOTIMPL; }
	STDMETHODIMP OnLowResource(DWORD) override { return S_OK; }
	STDMETHODIMP OnProgress(ULONG, ULONG, ULONG, LPCWSTR) override
	{
		if (m_stop_token.stop_requested())
			return E_ABORT;
		auto now = GetTickCount64();
		m_last_progress_tick = now;
		if ((now - m_start_tick) > m_total_timeout_ms)
			return E_ABORT;
		return S_OK;
	}
	STDMETHODIMP OnStopBinding(HRESULT, LPCWSTR) override
	{
		{
			std::lock_guard<std::mutex> lk(m_mtx);
			m_stop = true;
		}
		m_cv.notify_all();
		return S_OK;
	}
	STDMETHODIMP GetBindInfo(DWORD* grfBINDF, BINDINFO* pbindinfo) override
	{
		if (!grfBINDF || !pbindinfo) return E_POINTER;
		*grfBINDF = BINDF_ASYNCHRONOUS | BINDF_ASYNCSTORAGE | BINDF_PULLDATA;
		ULONG cb = pbindinfo->cbSize;
		ZeroMemory(pbindinfo, cb);
		pbindinfo->cbSize = cb;
		pbindinfo->dwBindVerb = BINDVERB_GET;
		return S_OK;
	}
	STDMETHODIMP OnDataAvailable(DWORD, DWORD, FORMATETC*, STGMEDIUM*) override { return S_OK; }
	STDMETHODIMP OnObjectAvailable(REFIID, IUnknown*) override { return S_OK; }

private:
	std::atomic<ULONG> m_ref;
	DWORD m_total_timeout_ms;
	DWORD m_stall_timeout_ms;
	ULONGLONG m_start_tick;
	std::atomic<ULONGLONG> m_last_progress_tick;
	IBinding* m_binding;
	std::atomic<bool> m_stop;
	std::thread m_watchdog;
	std::mutex m_mtx;
	std::condition_variable m_cv;
	std::stop_token m_stop_token;
};

static HRESULT url_download_to_file_with_timeout(
	LPCWSTR url, LPCWSTR filename, DWORD total_timeout_ms, DWORD stall_timeout_ms,
	std::stop_token stop_token = {})
{
	auto* cb = new timeout_bind_status_callback(total_timeout_ms, stall_timeout_ms, stop_token);
	HRESULT hr = URLDownloadToFileW(NULL, url, filename, 0, cb);
	cb->Release();
	return hr;
}

// Network/IO timeouts for the update machinery, in milliseconds.
namespace update_timeouts
{

constexpr uint32_t tags_total = 10000;
constexpr uint32_t tags_stall = 5000;
constexpr uint32_t archive_total = 120000;
constexpr uint32_t archive_stall = 30000;

}

// Drops the high byte of each wchar_t. Only valid for known-ASCII payloads
// (version tags, GitHub release names), which is all we use it for.
static std::string narrow_ascii(const std::wstring& w)
{
	std::string s(w.size(), '\0');
	std::transform(w.begin(), w.end(), s.begin(),
		[](wchar_t c) { return static_cast<char>(c); });
	return s;
}

// "vX.Y.Z.W" / "X.Y.Z" -> version_t. Missing trailing fields default to 0.
// Returns nullopt on a malformed tag instead of half-filling the result.
static std::optional<version_t> parse_version(std::wstring_view tag)
{
	if (!tag.empty() && (tag.front() == L'v' || tag.front() == L'V'))
		tag.remove_prefix(1);

	std::vector<std::wstring> parts;
	boost::algorithm::split(parts, std::wstring(tag), boost::is_any_of(L"."));

	version_t out{0, 0, 0, 0};
	if (parts.empty() || parts.size() > out.size())
		return std::nullopt;

	for (std::size_t i = 0; i < parts.size(); ++i)
	{
		try { out[i] = static_cast<std::uint16_t>(std::stoi(parts[i])); }
		catch (...) { return std::nullopt; }
	}
	return out;
}

// Builds an absolute path inside the user's %TEMP% directory. Win32 file APIs
// do not expand environment variables, so this resolves it explicitly.
static std::wstring temp_file_path(const std::wstring& name)
{
	wchar_t dir[MAX_PATH + 1] = {0};
	DWORD n = GetTempPathW(MAX_PATH, dir);
	if (n == 0 || n > MAX_PATH)
		return name; // fall back to CWD
	return std::wstring(dir, n) + name;
}

static std::string read_file_text(const std::wstring& path)
{
	std::ifstream in(path, std::ios::binary);
	std::stringstream ss;
	ss << in.rdbuf();
	return ss.str();
}

struct latest_release_info
{
	version_t version;
	std::wstring tag; // original tag name, e.g. "v1.2.3.4" — used to build the download URL
};

// Fetches the newest tag from the GitHub API and parses its version.
// Returns nullopt on any network/parse failure (caller decides how loud to be).
// NOTE: uses /tags (newest tag) to preserve existing behaviour; /releases/latest
// would be cleaner but changes semantics, so it's left as a deliberate choice.
static std::optional<latest_release_info> fetch_latest_release_version(std::stop_token stop_token = {})
{
	constexpr const wchar_t* tags_link = L"https://api.github.com/repos/DixelU/SAFC/tags";
	const auto stamp = std::to_wstring(std::chrono::duration_cast<std::chrono::microseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count());
	const std::wstring json_path = temp_file_path(stamp + L"tags.json");

	// if you have error here -> https://github.com/Microsoft/WSL/issues/22#issuecomment-207788173
	HRESULT res = url_download_to_file_with_timeout(
		tags_link, json_path.c_str(), update_timeouts::tags_total, update_timeouts::tags_stall,
		stop_token);
	if (res != S_OK)
	{
		_wremove(json_path.c_str());
		return std::nullopt;
	}

	const std::string json_text = read_file_text(json_path);
	_wremove(json_path.c_str());

	std::unique_ptr<JSONValue> root(JSON::Parse(json_text.c_str()));
	if (!root || !root->IsArray())
		return std::nullopt;

	const JSONArray& arr = root->AsArray();
	if (arr.empty() || !arr[0]->IsObject())
		return std::nullopt;

	const JSONObject& obj = arr[0]->AsObject();
	auto it = obj.find(L"name");
	if (it == obj.end() || !it->second->IsString())
		return std::nullopt;

	const std::wstring tag = it->second->AsString();
	auto parsed = parse_version(tag);
	if (!parsed)
		return std::nullopt;

	return latest_release_info{*parsed, tag};
}

// Absolute path of the backup the updater renames the running exe to. Must be
// computed the same way by both safc_update and its startup cleanup, otherwise
// a failed update leaves an orphaned backup behind.
static std::wstring self_backup_path()
{
	return extract_directory(get_self_path()) + L"_s";
}

static bool safc_update(const std::wstring& latest_release, std::wstring& file_location,
	std::stop_token stop_token = {})
{
#ifndef __X64
	constexpr const wchar_t* archive_name = L"SAFC32.7z";
#else
	constexpr const wchar_t* archive_name = L"SAFC64.7z";
#endif

	bool updated_flag = false;
	std::string error_msg;

	const std::wstring current_file_path = get_self_path();

	//std::wstring executablepath = current_file_path;
	std::wstring filename = extract_directory(current_file_path);
	std::wstring pathway = filename;
	const std::wstring backup = self_backup_path();

	filename += L"update.7z";
	//wsprintfW(current_file_path, L"%S%S", filename.c_str(), L"update.7z\0");
	std::wstring link = L"https://github.com/DixelU/SAFC/releases/download/" + latest_release + L"/" + archive_name;

	HRESULT co_res = url_download_to_file_with_timeout(
		link.c_str(), filename.c_str(), update_timeouts::archive_total, update_timeouts::archive_stall,
		stop_token);
	if (stop_token.stop_requested())
	{
		_wremove(filename.c_str());
		return false;
	}

	if (co_res == S_OK)
	{
		errno = 0;
		_wrename(current_file_path.c_str(), backup.c_str());
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
				_wrename((pathway + L"SAFC.exe").c_str(), current_file_path.c_str());
				file_location = current_file_path;
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
			error_msg = std::string("Autoupdate error (unpack exception):\n") + e.what();
		}
		else
		{
			error_msg = std::string("Autoupdate error (unable to access self): \n") + strerror(errno);
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

	if (error_msg.size())
		_wrename(backup.c_str(), current_file_path.c_str());

	return updated_flag;
}

static std::wstring format_version(const version_t& v)
{
	return L"v" + std::to_wstring(v[0]) + L"." + std::to_wstring(v[1]) +
		L"." + std::to_wstring(v[2]) + L"." + std::to_wstring(v[3]);
}

void safc_version_check()
{
	std::wcout << L"Current version: " << format_version(g_version_tuple) << L"\n";

	if (!check_autoupdates)
		return;

	worker_singleton<struct version_check>::instance().push([](std::stop_token stop_token)
	{
		// Clean up any backup left behind by a previously interrupted update.
		_wremove(self_backup_path().c_str());

		try
		{
			auto latest = fetch_latest_release_version(stop_token);
			if (stop_token.stop_requested())
				return;
			if (!latest)
			{
				const auto* msg = "Most likely your internet connection is unstable\nSAFC cannot check for updates";
				throw_alert_warning(msg);
				std::cout << msg;
				return;
			}

			std::wcout << L"Git latest version: " << format_version(latest->version) << L"\n";

			// std::array compares lexicographically, so this is the full
			// major.minor.patch.build precedence in one line.
			if (g_version_tuple >= latest->version)
				return;

			throw_alert_warning("Update found! The app might restart soon...\nUpdate: " +
				narrow_ascii(latest->tag));

			std::wstring safc_file_path;
			if (!safc_update(latest->tag, safc_file_path, stop_token) ||
				stop_token.stop_requested() || safc_file_path.empty())
				return;

			throw_alert_warning("SAFC will restart in 3 seconds...");
			for (int i = 0; i < 30 && !stop_token.stop_requested(); ++i)
				std::this_thread::sleep_for(std::chrono::milliseconds(100));
			if (stop_token.stop_requested())
				return;

			ShellExecuteW(NULL, L"open", safc_file_path.c_str(), NULL, NULL, SW_SHOWNORMAL);
			//_wsystem((L"start \"" + executablepath + L"\"").c_str());
			exit(0);
		}
		catch (const std::exception& e)
		{
			throw_alert_warning("SAFC just almost crashed while checking the update...\nTell developer about that " +
				std::string() + e.what());
		}
	});
}
