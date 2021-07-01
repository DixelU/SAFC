#pragma once
#ifndef BBB_FFIO
#define BBB_FFIO

#include <stdio.h>
#include <fileapi.h>

typedef struct byte_by_byte_fast_file_reader {
private:
	FILE* file;
	unsigned char* buffer;
	size_t buffer_size;
	size_t inner_buffer_pos;
	signed long long int file_pos;
	bool is_open;
	bool is_eof;
	bool next_chunk_is_unavailable;
	const size_t true_buffer_size;
	inline void __read_next_chunk() {
		if (!next_chunk_is_unavailable) {
			size_t new_buffer_len = _fread_nolock_s(buffer, buffer_size, 1, buffer_size, file);
			//printf("%lli\n", new_buffer_len);
			if (new_buffer_len != buffer_size) {
				buffer_size = new_buffer_len;
				next_chunk_is_unavailable = true;
			}
			inner_buffer_pos = 0;
		}
		else {
			is_eof = true;
		}
	}
	inline unsigned char __get() {
		unsigned char buf_ch = buffer[inner_buffer_pos];
		inner_buffer_pos++;
		file_pos++;
		return buf_ch;
	}
	inline void zero_buffer() {
		auto end = buffer + buffer_size;
		auto current = buffer;
		while (current != end) {
			*current = 0;
			current++;
		}
	}
public:
	byte_by_byte_fast_file_reader(const wchar_t* filename, int default_buffer_size = 20000000) :true_buffer_size(default_buffer_size) {
		auto err_no = _wfopen_s(&file, filename, L"rb");
		is_open = !(err_no);
		next_chunk_is_unavailable = (is_eof = (file) ? feof(file) : true);
		if (err_no | is_eof) {
			file_pos = 0;
			buffer = nullptr;
			buffer_size = 0;
			inner_buffer_pos = 0;
		}
		else {
			file_pos = 0;
			inner_buffer_pos = 0;
			buffer = new unsigned char[default_buffer_size];
			buffer_size = default_buffer_size; 
			__read_next_chunk();
		}
	}
	~byte_by_byte_fast_file_reader() {
		if (is_open)
			fclose(file);
		delete[] buffer;
		buffer = nullptr;
	}
	inline void reopen_next_file(const wchar_t* filename) {
		close(); 
		auto err_no = _wfopen_s(&file, filename, L"rb");
		is_open = !(err_no);
		next_chunk_is_unavailable = (is_eof = (file) ? feof(file) : true);
		if ((err_no | is_eof) && buffer_size) {
			file_pos = 0;
			buffer_size = 0;
			inner_buffer_pos = 0;
		}
		else {
			file_pos = 0;
			inner_buffer_pos = 0;
			buffer_size = true_buffer_size;
			__read_next_chunk();
		}
	}
	//rdbuf analogue
	inline void put_into_ostream(std::ostream& out) {
		while (!is_eof) {
			size_t offset = 0;
			file_pos += (offset = (buffer_size - inner_buffer_pos));
			out.write((char*)(buffer + inner_buffer_pos), offset);
			__read_next_chunk();
		}
		close();
	}
	inline void seekg(unsigned long long int abs_pos) {
		auto chunk_begining = file_pos - inner_buffer_pos;
		auto chunk_ending = chunk_begining + buffer_size;
		if (abs_pos >= chunk_begining && abs_pos < chunk_ending) {
			inner_buffer_pos = abs_pos - chunk_begining;
			file_pos = abs_pos;
		}
		else {
			_fseeki64_nolock(file, file_pos = abs_pos, SEEK_SET);
			__read_next_chunk();
		}
	}
	inline unsigned char get() {
		if (is_open && !is_eof) {
			if (inner_buffer_pos >= buffer_size)
				__read_next_chunk();
			return is_eof ? 0 : __get();
		}
		else
			return 0;
	}
	inline void close() {
		if(is_open)
			fclose(file);
		is_eof = true;
		is_open = false;
	}
	inline signed long long int tellg() const {
		return file_pos;
	}
	inline bool good() {
		return is_open && !is_eof;
	}
	inline bool eof() {
		return is_eof;
	}
} bbb_ffr;

#endif