#pragma once
#ifndef SAFC_SIMPLE_PLAYER
#define SAFC_SIMPLE_PLAYER

#include "Windows.h"

struct simple_player
{
	simple_player() = default;

	void init()
	{
		enum_devices();

		if (!kdmapi_status)
			init_kdmapi();
	}

private:
	void init_kdmapi()
	{
		kdmapi_status = nullptr;

		auto moduleHandle = GetModuleHandleW(L"OmniMIDI");
		if (!moduleHandle)
			return;

		kdmapi_status = (decltype(kdmapi_status))GetProcAddress(moduleHandle, "IsKDMAPIAvailable");
		if (!kdmapi_status || !kdmapi_status())
			return;

		short_msg = (decltype(short_msg))GetProcAddress(moduleHandle, "SendDirectData");
	}

	void enum_devices()
	{
		devices.clear();
		current_device = ~0ULL;

		auto count = midiOutGetNumDevs();
		devices.reserve(count);

		for (int i = 0; i < count; i++)
		{
			MIDIOUTCAPS out;
			auto ret = midiOutGetDevCapsW(i, &out, sizeof(out));
			if (ret != MMSYSERR_NOERROR)
				continue;

			devices.emplace_back(std::move(out));
		}

		if (devices.size())
			current_device = 0; // select first one
	}


	void close_midi_out(void)
	{
		int attempts = 0;

		if (hout)
		{
			all_notes_off();
			auto hout_copy = hout;
			hout = NULL;

			midiOutReset(hout_copy);
			while (midiOutClose(hout_copy) != MMSYSERR_NOERROR && attempts++ < 10)
				Sleep(200);

			if (attempts == 10)
				ThrowAlert_Error("Enable to close the MIDI out");
		}
	}

	void all_notes_off_channel(uint8_t channel)
	{
		channel &= 0x0F;
		short_msg(make_smsg(0xB0 | channel, 120));
		short_msg(make_smsg(0xB0 | channel, 121));
		short_msg(make_smsg(0xB0 | channel, 123));
	}

	// Turns all notes off on all channels
	void all_notes_off(void)
	{
		for (uint8_t c = 0; c < 16; c++)
			all_notes_off_channel(c);
	}

	inline static uint32_t make_smsg(uint8_t prog, uint8_t arg1, uint8_t arg2 = 0)
	{
		return (uint32_t)prog | (arg1 << 8) | (arg2 << 16);
	}

	size_t current_device = ~0ULL;
	std::vector<MIDIOUTCAPS> devices;
	HMIDIOUT hout = nullptr;

	void(WINAPI* short_msg)(uint32_t msg) = nullptr;

	bool(WINAPI* kdmapi_status)() = nullptr;
};

#endif