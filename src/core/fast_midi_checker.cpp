#include "fast_midi_checker.h"

#include <fstream>
#include <filesystem>

#include "midi_collection_merger.h"

fast_midi_checker::fast_midi_checker(std_unicode_string filename)
{
	this->file_size = this->ppqn = this->expected_track_number = this->is_midi = 0;
	this->filename = std::move(filename);

	auto [f, fo_ptr] =
		open_wide_stream<std::istream, std::ios::in>(this->filename, to_cchar_t("rb"));

	if (*f)
		this->is_accessible = true;
	else
		this->is_accessible = false;

	if (fo_ptr)
		fclose(fo_ptr);
	delete f;

	collect();
}

void fast_midi_checker::collect() noexcept
{
	auto [f, fo_ptr] =
		open_wide_stream<std::istream, std::ios::in>(filename, to_cchar_t("rb"));

	std::uint32_t header = 0;
	for (int i = 0; i < 4; ++i)
		header = (header << 8) | (f->get());

	if (header == MThd_header && f->good())
	{
		is_midi = true;
		f->seekg(10, std::ios::beg);
		expected_track_number = (expected_track_number << 8) | (f->get());
		expected_track_number = (expected_track_number << 8) | (f->get());
		ppqn = (ppqn << 8) | (f->get());
		ppqn = (ppqn << 8) | (f->get());

		if (fo_ptr)
			fclose(fo_ptr);

		std::error_code ec;
		auto size = std::filesystem::file_size(filename, ec);
		if (ec)
			std::cout << ec.message() << std::endl;
		else
		{
			std::uintmax_t t;
			std::cout << size << std::endl;
			std::cout << (this->file_size = size) << std::endl;
		}
		//cout << FileSize;
	}
	else
	{
		is_midi = false;
		ThrowAlert_Error("Error accured while getting the MIDI file info!");
		if (fo_ptr)
			fclose(fo_ptr);
	}
	delete f;
}
