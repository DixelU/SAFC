#pragma once

#include <string.h>
#include <stdio.h>
#include <share.h>

#include <type_traits>
#include <vector>
#include <memory>
#include <cinttypes>

struct mmbestream
	// memory mapped buffer edit
	// created primarily for random accessing file data
	// calls are expected to be mostly within a half of buffer size (i guess)
{
private:
	FILE* file;
	unsigned char* buffer;
	size_t buffer_size;
	size_t inner_buffer_pos;
	signed long long int file_pos;
	signed long long int external_buf_pos;
	signed long long int file_size;
	bool is_open;
	bool is_eof;
	bool is_sync;
	bool next_chunk_is_unavailable;
	const size_t true_buffer_size;

	constexpr static float advance_buffer_by = 0.61;

	inline void __sync()
	{
		if (!is_sync)
		{
			_fseeki64_nolock(file, external_buf_pos, SEEK_SET);
			_fwrite_nolock(buffer, buffer_size, 1, file);
			is_sync = true;
		}
	}

	inline void __read_next_chunk()
	{
		if (!next_chunk_is_unavailable)
		{
			size_t buffer_advancement_value = buffer_size * advance_buffer_by;
			size_t current_buf_pos_after_advancement = buffer_size - buffer_advancement_value;

			__sync();
			__expand_to_fit(file_pos + true_buffer_size);

			if (file_pos < current_buf_pos_after_advancement)
				current_buf_pos_after_advancement = file_pos;

			_fseeki64_nolock(file, file_pos - current_buf_pos_after_advancement, SEEK_SET);

			external_buf_pos = file_pos - current_buf_pos_after_advancement;

			size_t new_buffer_len = 
				_fread_nolock_s(buffer, buffer_size, 1, buffer_size, file);

			buffer_size = new_buffer_len;

			if (new_buffer_len != true_buffer_size) 
				next_chunk_is_unavailable = true;

			inner_buffer_pos = current_buf_pos_after_advancement;
		}
		else
		{
			is_eof = true;
		}
	}

	template<typename trivially_serialisible>
	inline void __wide_set(const trivially_serialisible& data, bool with_advance)
	{
		if (inner_buffer_pos > buffer_size - sizeof(trivially_serialisible))
		{
			__sync();
			__read_next_chunk();
		}

		is_sync = false;
		memcpy(buffer + inner_buffer_pos, &data, sizeof(trivially_serialisible));

		if (with_advance)
		{
			inner_buffer_pos++;
			file_pos++;
		}
	}

	template<typename trivially_serialisible>
	inline void __wide_get_into(trivially_serialisible& data, bool with_advance)
	{
		if (inner_buffer_pos > buffer_size - sizeof(trivially_serialisible))
		{
			__sync();
			__read_next_chunk();
		}

		is_sync = false;
		memcpy(&data, buffer + inner_buffer_pos, sizeof(trivially_serialisible));

		if (with_advance)
		{
			inner_buffer_pos++;
			file_pos++;
		}
	}

public:
	struct mmbeproxy
	{
	private:
		friend struct mmbestream;
		mmbestream& streamref;

		unsigned long long int tracked_position;

		explicit mmbeproxy(mmbestream& streamref) :
			streamref(streamref),
			tracked_position(streamref.file_pos)
		{

		}
	public:
		mmbeproxy(const mmbeproxy&) = delete;
		mmbeproxy(mmbeproxy&&) = delete;
		mmbeproxy& operator=(const mmbeproxy&) = delete;
		mmbeproxy& operator=(mmbeproxy&&) = delete;

		operator bool() const
		{
			return streamref.good();
		}

		void operator+=(signed long long int offset)
		{
			tracked_position = tracked_position + offset;
			streamref.seekg(tracked_position);
		}

		void operator-=(signed long long int offset)
		{
			tracked_position = tracked_position - offset;
			streamref.seekg(tracked_position);
		}

		void setpos(unsigned long long int abs_pos)
		{
			tracked_position = abs_pos;
			streamref.seekg(abs_pos);
		}

		template<typename trivially_serialisable = unsigned char,
			typename = typename std::enable_if<
				!std::is_same<mmbeproxy, trivially_serialisable>::value,
				trivially_serialisable>::type>
		trivially_serialisable get()
		{
			trivially_serialisable data;
			streamref.seekg(tracked_position);
			streamref.__wide_get_into(data, false);
			return data;
		}

		template<
			typename trivially_serialisable,
			typename = typename std::enable_if<
				!std::is_same<mmbeproxy, trivially_serialisable>::value,
				trivially_serialisable>::type>
		mmbeproxy& operator=(trivially_serialisable data)
		{
			streamref.seekg(tracked_position);
			streamref.__wide_set(data, false);
			return *this;
		}

		void flush()
		{
			streamref.__sync();
		}
	};

	enum class mode { read, write };

	mmbestream(
		const wchar_t* filename,
		int default_buffer_size = 20000000, 
		mode mode = mode::write):
		true_buffer_size(default_buffer_size),
		file_size(0),
		is_sync(false)
	{
		auto err_no = 
			_wfopen_s(
				&file,
				filename,
				(mode == mode::read ? L"rb+" : L"wb+"));
		is_open = !(err_no);
		next_chunk_is_unavailable = (is_eof = (file) ? feof(file) : true);
		if (err_no || is_eof)
		{
			file_pos = 0;
			buffer = nullptr;
			buffer_size = 0;
			inner_buffer_pos = 0;
			external_buf_pos = 0;
		}
		else
		{
			file_pos = 0;
			inner_buffer_pos = 0;
			buffer = new unsigned char[default_buffer_size];
			buffer_size = default_buffer_size;
			external_buf_pos = 0;
			__read_next_chunk();
			file_size = buffer_size;
		}
	}

	~mmbestream()
	{
		close();
	}

	inline void __expand_to_fit(unsigned long long int new_file_size)
	{
		if (new_file_size > file_size)
		{
			_fseeki64_nolock(file, file_size, SEEK_SET);

			long long int fill_block_size = new_file_size - file_size;

			while (fill_block_size > 0)
			{
				_fwrite_nolock(buffer, 1, true_buffer_size, file);
				fill_block_size -= true_buffer_size;
			}

			_fseeki64_nolock(file, file_pos, SEEK_SET);
		}
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
			__expand_to_fit(abs_pos + true_buffer_size);

			_fseeki64_nolock(file, file_pos = abs_pos, SEEK_SET);
			__read_next_chunk();
		}
	}

	inline mmbeproxy get()
	{
		return mmbeproxy(*this);
	}
	inline void close()
	{
		if (is_open)
			fclose(file);
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
};

struct vector_like_mmbe_wrapper
{
	std::unique_ptr<mmbestream> stream_ptr;
	mmbestream::mmbeproxy head_proxy;
	uint64_t size;

	vector_like_mmbe_wrapper(std::unique_ptr<mmbestream> stream_ptr) :
		stream_ptr(std::move(stream_ptr)),
		head_proxy(this->stream_ptr->get())
	{
		if (!stream_ptr->good())
			throw std::runtime_error("MMBE buffer was not allocated");
	}

	void reserve(uint64_t reserve)
	{
		stream_ptr->seekg(reserve);
	}

	uint64_t size() const
	{
		return size;
	}

	template<typename T>
	struct trivial_element_proxy
	{
		trivial_element_proxy(mmbestream* _stream_ref, uint64_t position) :
			_stream_ref(_stream_ref),
			position(position)
		{
		}

	private:
		mmbestream* _stream_ref;
		uint64_t position;
	public:

		operator T()
		{
			auto proxy = _stream_ref->get();
			proxy.setpos(position);
			return proxy.get<T>();
		}

		void operator=(T value)
		{
			auto proxy = _stream_ref->get();
			proxy.setpos(position);
			proxy = value;
		}

		void advance(int64_t offset) { position += offset; }
		void operator+=(int64_t offset) { advance(offset); }
		void operator-=(int64_t offset) { advance(-offset); }

		trivial_element_proxy<T> operator[](int64_t offset)
		{
			return { _stream_ref, position + offset };
		}

		bool operator==(const trivial_element_proxy& rhs)
		{
			return _stream_ref == rhs._stream_ref && position == rhs.position;
		}

		template<typename U>
		trivial_element_proxy<U> rebind() const
		{
			return { _stream_ref, position };
		}
	};

	template<typename T>
	trivial_element_proxy<T>
		get_value(uint64_t index) const
	{
		return trivial_element_proxy<T>(stream_ptr.get(), index);
	}

	template<typename _head, typename... _tail>
	void copy_back(_head head_value, _tail... tail_values)
	{
		push_back(head_value);

		copy_back(tail_values...);
	}

	template<typename _head, typename... _tail>
	void copy_back(_head head_value)
	{
		push_back(head_value);
	}

	template<typename T>
	void push_back(T value)
	{
		head_proxy.setpos(size);
		size += sizeof(T);
		head_proxy = value;
	}

	void resize(uint64_t size)
	{
		reserve(size );
		this->size = size;
	}

	trivial_element_proxy<unsigned char>
		operator[](uint64_t index) const
	{
		return get_value<unsigned char>(index);
	}

	trivial_element_proxy<unsigned char> begin() const
	{
		return trivial_element_proxy<unsigned char>(stream_ptr.get(), 0);
	}

	trivial_element_proxy<unsigned char> end() const
	{
		return trivial_element_proxy<unsigned char>(stream_ptr.get(), size);
	}

	// todo: proper implementation using the buffers
	void copy_to(uint64_t position, unsigned char* data, size_t size)
	{
		auto proxy = get_value<unsigned char>(position);
		while (size)
		{
			*data = proxy;
			proxy.advance(1);
			data++;
			size--;
		}
	}

	// todo: proper implementation using the buffers
	void copy_back_from(unsigned char* data, size_t size)
	{
		auto proxy = get_value<unsigned char>(size);
		while (size)
		{
			proxy = *data;
			proxy.advance(1);
			data++;
			size--;
		}
	}
};

void test()
{
	char asdf[50];
	tmpnam_s(asdf, 50);

	mmbestream stream(L"asdf", 10); // open file "asdf" with in-ram buffer of 10. It is opened as wb+ => recreated (file is empty)
	auto proxy = stream.get(); // get proxy object
	auto value = proxy.get(); // get value (char by default)
	value = '6';
	proxy = value;
	proxy = 23; // overrite value with a new one


	proxy += 5; // advance proxy (no fseek called. only pointer within buffer did)
	proxy = '3'; // assign stuff
	proxy += 29; // advance proxy 
	proxy = '!'; // i want to put this value at this byte.
	proxy.flush(); // flush to save last state of buffer
	proxy -= 29;
	auto three = proxy.get();

	std::cout << three << std::endl;
}

