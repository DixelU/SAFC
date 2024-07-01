#pragma once
#ifndef SAF_SMIC
#define SAF_SMIC

//#include <Windows.h>
#include <string>
#include <array>

#include "../bbb_ffio.h"
#include "../btree/btree_map.h"

#define MTrk 1297379947
#define MThd 1297377380
#define STRICT_WARNINGS true

struct SingleMIDIInfoCollector 
{
	struct TempoEvent
	{
		std::uint8_t A;
		std::uint8_t B;
		std::uint8_t C;
		TempoEvent(std::uint8_t A, std::uint8_t B, std::uint8_t C) : A(A), B(B), C(C) {	}
		TempoEvent() :TempoEvent(0x7, 0xA1, 0x20) {}
		inline static double get_bpm(std::uint8_t A, std::uint8_t B, std::uint8_t C)
		{
			std::uint32_t L = (A << 16) | (B << 8) | (C);
			return 60000000. / L;
		}
		inline operator double() const
		{
			return get_bpm(A, B, C);
		}
		inline bool operator<(const TempoEvent& TE) const
		{
			return double(*this) < (double)TE;
		}
	};
	struct NoteOnOffCounter
	{
		std::int64_t NoteOn;
		std::int64_t NoteOff;
		NoteOnOffCounter() : NoteOn(0), NoteOff(0) {}
		NoteOnOffCounter(std::int64_t Base) : NoteOnOffCounter()
		{
			if (Base > 0)
				NoteOn += Base;
			else
				NoteOff -= Base;
		}
		NoteOnOffCounter(std::int64_t NOn, std::int64_t NOff) : NoteOn(NOn), NoteOff(NOff) {}
		inline operator std::int64_t() const
		{
			return NoteOn - NoteOff;
		}
		NoteOnOffCounter inline operator +(std::int64_t offset)
		{
			if (offset > 0)
				NoteOn += offset;
			else
				NoteOff -= offset;
			return *this;
		}
		NoteOnOffCounter inline operator +=(std::int64_t offset)
		{
			return *this = (*this + (offset));
		}
		NoteOnOffCounter inline operator -(std::int64_t offset)
		{
			return *this + (-offset);
		}
		NoteOnOffCounter inline operator -=(std::int64_t offset)
		{
			return *this = (*this + (-offset));
		}
		NoteOnOffCounter inline operator ++()
		{
			NoteOn++;
			return *this;
		}
		NoteOnOffCounter inline operator --()
		{
			NoteOff++;
			return *this;
		}
	};
	struct TrackData 
	{
		std::int64_t MTrk_pos;
	};
	bool Processing, Finished;
	std_unicode_string FileName;
	std::string LogLine;
	std::string ErrorLine;
	using tempo_graph = btree::btree_map<std::int64_t, TempoEvent>;
	using der_polyphony_graph = btree::btree_map<std::int64_t, NoteOnOffCounter>;
	using polyphony_graph = btree::btree_map<std::int64_t, std::int64_t>;
	tempo_graph TempoMap;
	//der_polyphony_graph PolyphonyFiniteDifference;
	polyphony_graph Polyphony;
	std::vector<TrackData> Tracks;
	std::uint16_t PPQ;
	bool AllowLegacyRunningStatusMetaIgnorance;
	//Locker<btree::btree_map<std::uint64_t, std::uint64_t>> Polyphony;
	//Locker<btree::btree_map<>>
	SingleMIDIInfoCollector(std_unicode_string filename, std::uint16_t PPQ, bool AllowLegacyRunningStatusMetaIgnorance = false) :
		FileName(filename),
		LogLine(" "),
		Processing(0),
		Finished(0),
		PPQ(PPQ),
		AllowLegacyRunningStatusMetaIgnorance(AllowLegacyRunningStatusMetaIgnorance)
	{

	}
	void Lookup() 
	{
		der_polyphony_graph PolyphonyFiniteDifference;
		Processing = true;
		bbb_ffr file_input(FileName.c_str());
		ErrorLine = " ";
		LogLine = " ";
		//std::array<std::uint32_t, 4096> CurHolded;
		std::uint32_t MTRK = 0, vlv = 0;
		std::uint64_t LastTick = 0;
		std::uint64_t CurTick = 0;
		std::uint8_t IO = 0, RSB = 0;
		TrackData TData;
		TempoMap[0] = TempoEvent(0x7, 0xA1, 0x20);
		PolyphonyFiniteDifference[-1] = NoteOnOffCounter();
		while (file_input.good())
		{
			std::array<std::uint64_t, 4096> polyphony;
			CurTick = 0;
			MTRK = 0;
			while (MTRK != MTrk && file_input.good())
				MTRK = (MTRK << 8) | (file_input.get());
			for (int i = 0; i < 4; i++)
				file_input.get();
			IO = RSB = 0;
			while (file_input.good())
			{
				TData.MTrk_pos = file_input.tellg() - 8;
				vlv = 0;
				do 
				{
					IO = file_input.get();
					vlv = (vlv << 7) | (IO & 0x7F);
				} while (IO & 0x80 && !file_input.eof());
				CurTick += vlv;
				if (LastTick < CurTick)
					LastTick = CurTick;
				std::uint8_t EventType = file_input.get();
				if (EventType == 0xFF)
				{
					if(AllowLegacyRunningStatusMetaIgnorance)
						RSB = 0;
					std::uint8_t type = file_input.get();
					std::uint32_t meta_data_size = 0;
					do 
					{
						IO = file_input.get();
						meta_data_size = (meta_data_size << 7) | (IO & 0x7F);
					} while (IO & 0x80 && !file_input.eof());
					if (type == 0x2F)
					{
						Tracks.push_back(TData);
						LogLine = "Passed " + std::to_string(Tracks.size()) + " tracks";
						break;
					}
					else if (type == 0x51)
					{
						TempoEvent TE;
						TE.A = file_input.get();
						TE.B = file_input.get();
						TE.C = file_input.get();
						TempoMap[CurTick] = TE;
					}
					else
					{
						while (meta_data_size--)
							file_input.get();
					}
				}
				else if (EventType >= 0x80 && EventType <= 0x9F)
				{
					RSB = EventType;
					int change = (EventType & 0x10) ? 1 : -1;
					auto it = PolyphonyFiniteDifference.find(CurTick);
					auto key = file_input.get();
					auto volume = file_input.get();

					if (!volume && change == 1)
						change = -1;

					int index = (EventType & 0xF) | (key << 4);
					if (change == -1)
					{
						if (polyphony[index] == 0)
							continue;
						polyphony[index] -= 1;
					}
					else 
						polyphony[index] += 1;

					if (it == PolyphonyFiniteDifference.end())
						PolyphonyFiniteDifference[CurTick] = change;
					else
						it->second += change;
				}
				else if ((EventType >= 0xA0 && EventType <= 0xBF) ||
					(EventType >= 0xE0 && EventType <= 0xEF))
				{
					RSB = EventType;
					file_input.get();
					file_input.get();
				}
				else if (EventType >= 0xC0 && EventType <= 0xDF)
				{
					RSB = EventType;
					file_input.get();
				}
				else if (EventType == 0xF0 || EventType == 0xF7)
				{
					if(AllowLegacyRunningStatusMetaIgnorance)
						RSB = 0;
					std::uint32_t meta_data_size = 0;
					do
					{
						IO = file_input.get();
						meta_data_size = (meta_data_size << 7) | (IO & 0x7F);
					} while (IO & 0x80 && !file_input.eof());
					while (meta_data_size--)
						file_input.get();
				}
				else 
				{
					std::uint8_t FirstParam = EventType;
					EventType = RSB;
					if (EventType >= 0x80 && EventType <= 0x9F)
					{
						int change = (EventType & 0x10) ? 1 : -1;
						auto it = PolyphonyFiniteDifference.find(CurTick);
						auto volume = file_input.get();

						if (!volume && change == 1)
							change = -1;

						int index = (EventType & 0xF) | (FirstParam << 4);
						if (change == -1) 
						{
							if (polyphony[index] == 0)
								continue;
							polyphony[index] -= 1;
						}
						else
							polyphony[index] += 1;

						if (it == PolyphonyFiniteDifference.end())
							PolyphonyFiniteDifference[CurTick] = change;
						else
							it->second += change;
					}
					else if ((EventType >= 0xA0 && EventType <= 0xBF) ||
						(EventType >= 0xE0 && EventType <= 0xEF))
					{
						file_input.get();
					}
					else if (EventType >= 0xC0 && EventType <= 0xDF)
					{
						//nothing
					}
					else
					{ // here throw an error
						ErrorLine = "Corruption detected at: " + std::to_string(file_input.tellg());
						break;
					}
				}
			}
		}
		TempoMap[LastTick] = TempoMap.rbegin()->second;
		PolyphonyFiniteDifference[LastTick + 1] = 0;
		std::int64_t CurPolyphony = 0;
		for (auto cur_pair : PolyphonyFiniteDifference)
			Polyphony[cur_pair.first] = (CurPolyphony += cur_pair.second);
		Finished = true;
		Processing = false;
		file_input.close();
	}
};

#endif
