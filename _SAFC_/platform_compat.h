#pragma once
#ifndef SAFC_PLATFORM_COMPAT
#define SAFC_PLATFORM_COMPAT

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

#ifdef _WIN32
using std_unicode_string = std::wstring;
using cchar_t = wchar_t;
#else
using std_unicode_string = std::string;
using cchar_t = char;
#endif

template<size_t N>
struct cchar_decay_string
{
	cchar_t value[N]{};

	constexpr operator const cchar_t*() const { return value; }
	constexpr const cchar_t* c_str() const { return value; }
	operator std_unicode_string() const { return std_unicode_string(value); }
};

template<size_t N>
consteval cchar_decay_string<N> to_cchar_t(const char (&text)[N])
{
	cchar_decay_string<N> result;
	for (size_t i = 0; i < N; ++i)
		result.value[i] = static_cast<cchar_t>(text[i]);
	return result;
}

inline std_unicode_string path_from_json_string(const std::wstring& value)
{
#ifdef _WIN32
	return value;
#else
	std_unicode_string out;
	out.reserve(value.size());
	for (wchar_t ch : value)
		out.push_back(static_cast<char>(ch));
	return out;
#endif
}

inline std::string path_to_display_string(const std_unicode_string& value)
{
#ifdef _WIN32
	std::string out;
	out.reserve(value.size());
	for (wchar_t ch : value)
		out.push_back(static_cast<char>(ch & 0xFF));
	return out;
#else
	return value;
#endif
}

inline std_unicode_string path_from_ascii_string(const std::string& value)
{
#ifdef _WIN32
	return std_unicode_string(value.begin(), value.end());
#else
	return value;
#endif
}

inline std::filesystem::path path_to_filesystem(const std_unicode_string& value)
{
#ifdef _WIN32
	return std::filesystem::path(value);
#else
	return std::filesystem::path(value);
#endif
}

inline int file_remove(const std_unicode_string& path)
{
#ifdef _WIN32
	return _wremove(path.c_str());
#else
	return std::remove(path.c_str());
#endif
}

inline int file_rename(const std_unicode_string& from, const std_unicode_string& to)
{
#ifdef _WIN32
	return _wrename(from.c_str(), to.c_str());
#else
	return std::rename(from.c_str(), to.c_str());
#endif
}

inline std::ofstream open_binary_output(const std_unicode_string& path)
{
	return std::ofstream(path_to_filesystem(path), std::ios::binary | std::ios::out);
}

inline std::ifstream open_binary_input(const std_unicode_string& path)
{
	return std::ifstream(path_to_filesystem(path), std::ios::binary | std::ios::in);
}

#endif
