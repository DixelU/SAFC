#pragma once

#ifndef BBB_FFIO
#define BBB_FFIO

#include "header_utils.h"

#ifdef _MSC_VER
#include <fstream>
#else
#include <stdio.h>
#endif

#include <istream>
#include <ostream>

#ifdef _MSC_VER

inline errno_t fopen_wrap(FILE* file_ptr,
	const cchar_t* filename, const cchar_t* parameter)
{
	return _wfopen_s(&file_ptr, filename, L"rb");
}

inline size_t fread_wrap(void* ptr, size_t size, size_t count, FILE* filename)
{
	return _fread_nolock_s(ptr, size * count, size, count, filename);
}

#else

inline decltype(errno) fopen_wrap(FILE* file,
	const cchar_t* filename, const cchar_t* parameter)
{
	file = fopen(filename, parameter);
	return errno;
}

inline size_t fread_wrap(void* ptr, size_t size, size_t count, FILE* file)
{
	return fread(ptr, size, count, file);
}

#endif

// is only needed for MinGW/GCC 
#if !defined(__MINGW32__)

template<typename __inner_stream_type, decltype(std::ios_base::out) stream_io_type = std::ios_base::out>
std::pair<__inner_stream_type*, FILE*> open_wide_stream(std::wstring filename, const cchar_t* parameter);

template<>
std::pair<std::ostream*, FILE*> open_wide_stream<std::ostream, std::ios_base::out>(
	std::wstring filename, const cchar_t* parameter)
{
	std::ostream* ostream = new std::ofstream(filename, std::ios_base::out | std::ios_base::binary);
	return { ostream, nullptr }; // IN c++26 there will be native_handle() call, for now just return nullptr
}

template<>
std::pair<std::istream*, FILE*> open_wide_stream<std::istream, std::ios_base::in>(
	std::wstring filename, const cchar_t* parameter)
{
	std::istream* istream = new std::ifstream(filename, std::ios_base::in | std::ios_base::binary);
	return { istream, nullptr }; // IN c++26 there will be native_handle() call, for now just return nullptr
}

#endif

#if defined(__MINGW32__)

template<typename __inner_stream_type, decltype(std::ios_base::out) stream_io_type = std::ios_base::out>
std::pair<__inner_stream_type*, FILE*> open_wide_stream(
	std::string file, const cchar_t* parameter)
{
	FILE* c_file;
	fopen_wrap(c_file, file.c_str(), parameter);

	auto buffer = new __gnu_cxx::stdio_filebuf<cchar_t>(c_file, stream_io_type, 100000);
	
	return {new __inner_stream_type(buffer), c_file};
}

#endif

typedef struct byte_by_byte_fast_file_reader
{
private:
	FILE* file_ptr;
	unsigned char* buffer;
	size_t buffer_size;
	size_t inner_buffer_pos;
	signed long long int file_pos;
	bool is_open;
	bool is_eof;
	bool next_chunk_is_unavailable;
	const size_t true_buffer_size;

	inline void __read_next_chunk()
	{
		if (!next_chunk_is_unavailable)
		{
			size_t new_buffer_len = fread_wrap(buffer, 1, buffer_size, file_ptr);
			//printf("%lli\n", new_buffer_len);
			if (new_buffer_len != buffer_size)
			{
				buffer_size = new_buffer_len;
				next_chunk_is_unavailable = true;
			}

			inner_buffer_pos = 0;
		}
		else
		{
			is_eof = true;
		}
	}

	inline unsigned char __get()
	{
		unsigned char buf_ch = buffer[inner_buffer_pos];
		inner_buffer_pos++;
		file_pos++;
		return buf_ch;
	}

	inline void zero_buffer()
	{
		auto end = buffer + buffer_size;
		auto current = buffer;
		while (current != end)
		{
			*current = 0;
			current++;
		}
	}

public:
	byte_by_byte_fast_file_reader(
		const cchar_t* filename,
		int default_buffer_size = 20000000) :
			true_buffer_size(default_buffer_size)
	{
		auto err_no =
			fopen_wrap(file_ptr, filename, to_cchar_t("rb"));

		is_open = !(err_no);
		next_chunk_is_unavailable = (is_eof = (filename) ? feof(file_ptr) : true);
		if (err_no || is_eof)
		{
			file_pos = 0;
			buffer = nullptr;
			buffer_size = 0;
			inner_buffer_pos = 0;
		}
		else
		{
			file_pos = 0;
			inner_buffer_pos = 0;
			buffer = new unsigned char[default_buffer_size];
			buffer_size = default_buffer_size;
			__read_next_chunk();
		}
	}

	~byte_by_byte_fast_file_reader()
	{
		if (is_open)
			fclose(file_ptr);
		delete[] buffer;
		buffer = nullptr;
	}

	inline void reopen_next_file(
		const cchar_t* filename)
	{
		close();
		auto err_no =
			fopen_wrap(file_ptr, filename, to_cchar_t("rb"));

		is_open = !(err_no);
		next_chunk_is_unavailable = (is_eof = (filename) ? feof(file_ptr) : true);
		if ((err_no || is_eof) && buffer_size)
		{
			file_pos = 0;
			buffer_size = 0;
			inner_buffer_pos = 0;
		}
		else
		{
			file_pos = 0;
			inner_buffer_pos = 0;
			buffer_size = true_buffer_size;
			__read_next_chunk();
		}
	}

	//rdbuf analogue
	inline void put_into_ostream(std::ostream& out)
	{
		while (!is_eof)
		{
			size_t offset = 0;
			file_pos += (offset = (buffer_size - inner_buffer_pos));
			out.write((char*)(buffer + inner_buffer_pos), offset);
			__read_next_chunk();
		}
		close();
	}

	inline void seekg(unsigned long long int abs_pos)
	{
		auto chunk_begining = file_pos - inner_buffer_pos;
		auto chunk_ending = chunk_begining + buffer_size;
		if (abs_pos >= chunk_begining && abs_pos < chunk_ending)
		{
			inner_buffer_pos = abs_pos - chunk_begining;
			file_pos = abs_pos;
		}
		else
		{
#ifdef _MSC_VER
			_fseeki64_nolock
#else
			fseeko64
#endif
				(file_ptr, file_pos = abs_pos, SEEK_SET);

			__read_next_chunk();
		}
	}

	inline unsigned char get()
	{
		if (is_open && !is_eof)
		{
			if (inner_buffer_pos >= buffer_size)
				__read_next_chunk();
			return is_eof ? 0 : __get();
		}
		else
			return 0;
	}

	inline void close()
	{
		if (is_open)
			fclose(file_ptr);
		is_eof = true;
		is_open = false;
	}

	inline signed long long int tellg() const
	{
		return file_pos;
	}

	inline bool good() const
	{
		return is_open && !is_eof; // hot smh
	}

	inline bool eof() const
	{
		return is_eof;
	}

} bbb_ffr;

#endif
