#pragma once
#include <iostream>
#include <set>
#include <stack>
#include <vector>
#include <fstream>
#include <iterator>
#include <list>
#include <map>
#include <boost/container/flat_set.hpp>
#include <windows.h>

#define ULI unsigned long long int
#define LTE ULI   //Least top edge//whatever we wanna to have as Tick and len...
#define TNT DWORD   //track number type
#define KDT BYTE        //key data type

#define VOLUMEMASK ((ULI)0xFFFFFFFFFFFFFF)
#define MTHD 1297377380
#define MTRK 1297379947
using namespace std;

#pragma pack(push, 1)

namespace OR {
	bool dbg = 1;
	ULI NC = 0, PC = 0, ONC = 0;
	struct DC {//   
		LTE Tick;
		KDT Key;//0xWWQQ ww-findable/seekable state, QQ-key itself
		mutable BYTE Vol;
		TNT TrackN;
		DWORD Len;
	};
	struct ME {
		LTE Tick;
		BYTE A, B, C, D;
	};
	struct DEC {
		LTE Tick;
		DWORD Len;
		DWORD TrackN;
	};
	bool operator<(ME a, ME b) {
		if (a.Tick < b.Tick)return 1;
		else return 0;
	}
	bool operator<(DC a, DC b) {
		if (a.Tick < b.Tick)return 1;
		else if (a.Tick == b.Tick) {
			if (a.Key < b.Key)return 1;
			else return 0;
		}
		else return 0;
	}
	bool operator<(DEC a, DEC b) {
		if (a.Tick < b.Tick)return 1;
		else return 0;
	}
	bool ShouldBReplaced(DC O, DC &N) {//old and new//0 - save both//1 replce O with N
		if (O.Key != N.Key || O.Tick != N.Tick)return 0;//
		else if (O.Key == 0xFF && N.TrackN < O.TrackN)return 0;
		else if (N.TrackN > O.TrackN && N.Len < O.Len)return 0;
		else if (N.TrackN == O.TrackN && N.Len < O.Len)return 0;
		else if (/*!RemovingSustains &&*/ O.Vol > N.Vol) {//N.Vol=O.Vol;
	//		if(RemovingSustains){
	//			if(N.Tick==0)N.Vol=127;
	//			else N.Vol=1;
	//		}else{
			N.Vol = O.Vol;
			//		}
		}
		return 1;
	}
	ostream& operator<<(ostream& stream, DC a) {
		return (stream << "K" << (int)a.Key << "L" << a.Len << "T" << a.Tick << "TN" << a.TrackN);
	}
	struct OverlapsRemover {
		ULI FileSize, CurFilePos;
		BYTE RSB, Finished;
		WORD PPQN;
		DWORD CTrack;
		multiset<DC> SET;
		multiset<TNT> TRS;//tracks
		map<DWORD, boost::container::flat_multiset<ME>> OUTPUT;
		list<ULI> *PNO;//first 128 is first channel, next 128 are the second... etc//quite huge boi 
		string s_out;
		ifstream fin;
		OverlapsRemover(ULI FileSize = 1) {
			s_out = " ";
			Finished = 0;
			RSB = PPQN = CTrack = 0;
			PNO = new list<ULI>[2048];
		}
		void ClearPNO() {
			for (int i = 0; i < 2048; i++)PNO[i].clear();
		}
		void InitializeNPrepare(wstring link) {
			fin.open(link.c_str(), std::ios::binary | std::ios::in);
			DWORD MThd;
			//if(dbg)cout<<"Max set size:"<<SET.max_size()<<endl;
			BYTE IO;
			for (int i = 0; i < 4; i++) {
				IO = fin.get();
				MThd = (MThd << 8) | (IO);
			}
			if (MThd == MTHD) {
				fin.seekg(12);
				for (int i = 0; i < 2; i++) {
					IO = fin.get();
					PPQN = (PPQN << 8) | (IO);
				}
				fin.seekg(14);
				SET.clear();
				RSB = 0;
				CTrack = 0;
				if (dbg)s_out = "Header\n";
			}
			else {
				fin.close();
				s_out = "Input file doesn't begin with MThd";
			}
		}
		bool ReadSingleTrackFromCurPos() {//continue?
			BYTE MTrk[4];
			RSB = 0;
			ULI CTick = 0;//current tick
			DWORD RTick = 0, RData;//real tick (which we just read rn)//read data
			RtlZeroMemory(MTrk, 4);
			ClearPNO();
			for (int i = 0; i < 4 && !fin.eof() && !fin.bad(); i++)fin >> MTrk[i];
			while (MTrk[0] != 'M' && MTrk[1] != 'T' && MTrk[2] != 'r' && MTrk[3] != 'k' && !fin.eof() && !fin.bad()) {//such a hack//seeking MTrk
				swap(MTrk[0], MTrk[1]);
				swap(MTrk[1], MTrk[2]);
				swap(MTrk[2], MTrk[3]);
				MTrk[3] = fin.get();
				//s_out = ("Seeking for MTrk\n");///It's standart :t
			}
			for (int i = 0; i < 4 && !fin.bad(); i++)fin.get();//itterating through track's lenght//because its SAF branch app :3
			while (!fin.bad() && !fin.eof()) {
				//cout<<"MP: "<<CountMomentalPolyphony()<<endl;
				RTick = ReadVLV();
				if (!ParseEvent(CTick += RTick))return 1;
			}
			return 0;
		}
		DWORD CountMomentalPolyphony() {//debug purposes
			DWORD N = 0;
			for (int i = 0; i < 2048; i++)N += PNO[i].size();
			return N;
		}
		void PushNote(DC& Ev) {//ez
			PC++; ONC++;//ShouldBReplaced
			pair<multiset<DC>::iterator, multiset<DC>::iterator> P = SET.equal_range(Ev);
			//if(dbg)printf("INSERTED\n");
			multiset<DC>::iterator Y = (SET.size() > 0) ? ((--SET.end())) : SET.end();
			if (P.first != SET.end()) {
				//cout<<ONC<<" "<<Ev<<endl;
				while (P.first != P.second && P.first != Y) {
					if (ShouldBReplaced(*P.first, Ev)) {
						P.first = SET.erase(P.first);
						ONC--;
					}
					if (P.first != SET.end() && P.first != P.second && P.first != Y)
						P.first++;
				}
				if (ShouldBReplaced(*P.first, Ev) && P.first != Y && P.first != SET.end()) {
					P.first = SET.erase(P.first);
					ONC--;
				}
			}
			SET.insert(Ev);
		}
		DWORD ReadVLV() {//from current position
			DWORD VLV = 0;
			BYTE B = 0;
			if (!fin.eof() && !fin.bad()) {
				do {
					B = fin.get();
					VLV = (VLV << 7) | (B & 0x7F);
				} while (B & 0x80);
				return VLV;
			}
			else {
				s_out = "Failed to read VLV at " + to_string(fin.tellg());//people shouldnt know about VLV being corrupted *lenny face*
				return 0;
			}
		}
		ULI FindAndPopOut(LTE pos, ULI CTick) {
			list<ULI>::iterator Y = PNO[pos].begin();
			ULI q = PNO[pos].size();
			/*
			while(q>0 && ((*Y)&VOLUMEMASK)==CTick){
				Y++;
				q--;
			}
			*/
			if (q > 0) {
				q = (*Y);
				PNO[pos].erase(Y);
				return q;
			}
			else {
				s_out = "FaPO error " + to_string(pos) + " " + to_string(CTick);//not critical error//but still annoying
				return CTick | (VOLUMEMASK + 1);
			}
		}
		bool ParseEvent(ULI absTick) {//should we continue?
			BYTE IO, T; LTE pos; ULI FAPO;
			if (!fin.bad() && !fin.eof()) {
				IO = fin.get();
				if (IO >= 0x80 && IO <= 0x8F) {//NOTEOFF
					RSB = IO;
					NC++;
					IO = fin.get() & 0x7F;
					pos = ((RSB & 0x0F) << 7) | IO;//position of stack for this key/channel pair
					fin.get();//file thing ended
					//if(dbg)printf("NOTEOFF:%x%x00 at %llu\n",RSB,IO,absTick);
					DC Ev;//event push prepairings
					Ev.Key = IO;
					Ev.TrackN = RSB & 0x0F | ((CTrack) << 4);
					if (!PNO[pos].empty()) {
						FAPO = FindAndPopOut(pos, absTick);
						Ev.Tick = FAPO & VOLUMEMASK;
						Ev.Len = absTick - Ev.Tick;
						Ev.Vol = FAPO >> 56;
						PushNote(Ev);
					}
					//else if (0)cout << "Detected empty stack pop-attempt (N):" << (unsigned int)(RSB & 0x0F) << '-' << (unsigned int)IO << endl;
				}
				else if (IO >= 0x90 && IO <= 0x9F) {//NOTEON
					RSB = IO;
					IO = fin.get() & 0x7F;
					T = fin.get() & 0x7F;
					//if(dbg)printf("NOTEON:%x%x%x at %llu\n",RSB,IO,T,absTick);
					pos = ((RSB & 0x0F) << 7) | IO;
					if (T != 0)PNO[pos].push_front(absTick | (((ULI)T) << 56));
					else {//quite weird way to represent note off event...
						DC Ev;//event push prepairings
						Ev.Key = IO;
						Ev.TrackN = RSB & 0x0F | ((CTrack) << 4);
						if (!PNO[pos].empty()) {
							FAPO = FindAndPopOut(pos, absTick);
							Ev.Tick = FAPO & VOLUMEMASK;
							Ev.Len = absTick - Ev.Tick;
							Ev.Vol = FAPO >> 56;
							PushNote(Ev);
						}
						//else if (0)cout << "Detected empty stack pop-attempt (0):" << (RSB & 0x0F) << '-' << (unsigned int)IO << endl;
					}
					//cout<<PNO[pos].front()<<endl;
				}
				else if ((IO >= 0xA0 && IO <= 0xBF) || (IO >= 0xE0 && IO <= 0xEF)) {//stupid unusual vor visuals stuff 
					RSB = IO;
					fin.get(); fin.get();
				}
				else if (IO >= 0xC0 && IO <= 0xDF) {
					RSB = IO;
					fin.get();
				}
				else if (IO >= 0xF0 && IO <= 0xF7) {
					RSB = 0;
					DWORD vlv = ReadVLV();
					//fin.seekg(vlv,std::ios::cur);
					for (int i = 0; i < vlv; i++)fin.get();
				}
				else if (IO == 0xFF) {
					RSB = 0;
					IO = fin.get();
					DWORD vlv = 0;
					if (IO == 0x2F) {
						ReadVLV();
						return 0;
					}
					else if (IO == 0x51) {
						fin.get();//vlv
						for (int i = 0; i < 3; i++) {//tempochange data
							IO = fin.get();
							vlv = (vlv << 8) | IO;
						}//in vlv we have tempo data :)

						//if(dbg)printf("TEMPO:%x at %x\n",vlv,fin.tellg());
						DC Ev;//event push prepairings
						Ev.Key = 0xFF;
						Ev.TrackN = 0;
						Ev.Tick = absTick;
						Ev.Len = vlv;
						PushNote(Ev);
					}
					else {
						vlv = ReadVLV();
						//if(dbg)printf("REGMETASIZE:%x at %x\n",vlv,fin.tellg());
						//fin.seekg(vlv,std::ios::cur);
						for (int i = 0; i < vlv; i++)fin.get();
					}
				}
				else {
					if (RSB >= 0x80 && RSB <= 0x8F) {//NOTEOFF
						NC++;
						RSB = RSB;
						fin.get();//same
						pos = ((RSB & 0x0F) << 7) | IO;//position of stack for this key/channel pair
						DC Ev;//event push prepairings
						Ev.Key = IO;
						Ev.TrackN = RSB & 0x0F | ((CTrack) << 4);
						if (!PNO[pos].empty()) {
							FAPO = FindAndPopOut(pos, absTick);
							Ev.Tick = FAPO & VOLUMEMASK;
							Ev.Len = absTick - Ev.Tick;
							Ev.Vol = FAPO >> 56;
							PushNote(Ev);
						}
						//else if (0)cout << "Detected empty stack pop-attempt (RN):" << (unsigned int)(RSB & 0x0F) << '-' << (unsigned int)IO << endl;
					}
					else if (RSB >= 0x90 && RSB <= 0x9F) {//NOTEON
						RSB = RSB;
						T = fin.get() & 0x7F;//magic finished//volume
						pos = ((RSB & 0x0F) << 7) | IO;
						if (T != 0)PNO[pos].push_front(absTick | (((ULI)T) << 56));
						else {//quite weird way to represent note off event...
							DC Ev;//event push prepairings
							Ev.Key = IO;
							Ev.TrackN = RSB & 0x0F | ((CTrack) << 4);
							if (!PNO[pos].empty()) {
								FAPO = FindAndPopOut(pos, absTick);
								Ev.Tick = FAPO & VOLUMEMASK;
								Ev.Len = absTick - Ev.Tick;
								Ev.Vol = FAPO >> 56;
								PushNote(Ev);
							}
							//else if (0)cout << "Detected empty stack pop-attempt (R0):" << (unsigned int)(RSB & 0x0F) << '-' << (unsigned int)IO << endl;
						}
					}
					else if ((RSB >= 0xA0 && RSB <= 0xBF) || (RSB >= 0xE0 && RSB <= 0xEF)) {//stupid unusual for visuals stuff 
						RSB = RSB;
						fin.get();
					}
					else if (RSB >= 0xC0 && RSB <= 0xDF) {
						RSB = RSB;
					}
					else {
						s_out = "Imaprseable data...\n\tdebug:" + to_string((unsigned int)RSB) + ":" + to_string((unsigned int)IO) + ":Off(FBegin):" + to_string(fin.tellg());
						BYTE I = 0, II = 0, III = 0;
						while (!(I == 0x2F && II == 0xFF && III == 0) && !fin.eof()) {
							III = II;
							II = I;
							I = fin.get();
						}
						fin.get();
						return 0;
					}
				}
			}
			else return 0;
			return 1;
		}
		void SinglePassMapFiller() {
			s_out = "Single pass scan has started... it might take a while...\n";
			multiset<DC>::iterator Y = SET.begin();
			ME Event;
			DC Note;//prev out, out
			while (Y != SET.end()) {
				Note = *Y;
				if (!(Note.Key ^ 0xFF)) {
					Event.Tick = Note.Tick;
					Event.A = 0x03;
					Event.B = (Note.Len & 0xFF0000) >> 16;
					Event.C = (Note.Len & 0xFF00) >> 8;
					Event.D = (Note.Len & 0xFF);
					OUTPUT[Note.TrackN].insert(Event);
				}
				else {
					//if(dbg)printf("NOTE\t");
					Note.Key &= 0xFF;
					Event.Tick = Note.Tick;//noteon event
					Event.A = 0;
					Event.B = 0x90 | (Note.TrackN & 0xF);
					Event.C = Note.Key;
					Event.D = ((Note.Vol) ? Note.Vol : 1);
					OUTPUT[Note.TrackN].insert(Event);
					Event.Tick += Note.Len;//note off event
					Event.B ^= 0x10;
					Event.D = 0x40;
					OUTPUT[Note.TrackN].insert(Event);
					//if(dbg)printf("F\n");
				}
				Y = SET.erase(Y);
			}

			SET.clear();
			s_out = "Single pass scan has finished...\n";
		}
		void FormMIDI(wstring Link) {
			s_out = ("Starting enhanced output algorithm\n");
			SinglePassMapFiller();
			vector<BYTE> Track;
			ofstream fout;
			fout.open((Link + L".OR.mid").c_str(), std::ios::binary | std::ios::out);
			map<DWORD, boost::container::flat_multiset<ME>>::iterator Y = OUTPUT.begin();
			boost::container::flat_multiset<ME>::iterator U;
			boost::container::flat_multiset<ME> *pMS;
			ME Event, PrevEvent;
			s_out = ("Output..\n");
			//fout<<'M'<<'T'<<'h'<<'d'<<(BYTE)0<<(BYTE)0<<(BYTE)0<<(BYTE)6<<(BYTE)0<<(BYTE)1;//header
			//fout<<(BYTE)((TRS.size()>>8))<<(BYTE)((TRS.size()&0xFF));//track number
			//fout<<(PPQN>>8)<<(PPQN&0xFF);//ppqn
			fout.put('M'); fout.put('T'); fout.put('h'); fout.put('d');
			fout.put(0); fout.put(0); fout.put(0); fout.put(6); fout.put(0); fout.put(1);
			fout.put((char)((TRS.size() >> 8))); fout.put((char)((TRS.size() & 0xFF)));
			fout.put((char)(PPQN >> 8)); fout.put((char)(PPQN & 0xFF));
			TRS.clear();
			s_out = ("Header...\n");
			while (Y != OUTPUT.end()) {
				Track.push_back('M');
				Track.push_back('T');
				Track.push_back('r');
				Track.push_back('k');
				Track.push_back(0);//size
				Track.push_back(0);//of
				Track.push_back(0);//track
				Track.push_back(0);//aslkflkasdflksdf
				s_out = ("Track header...\nCurrent track size: " + to_string((*Y).second.size()));
				pMS = &((*Y).second);
				U = pMS->begin();
				Event.Tick = 0;
				s_out = ("Converting back to MIDI standard");
				while (U != pMS->end()) {
					PrevEvent = Event;
					Event = *U;
					DWORD tTick = Event.Tick - PrevEvent.Tick, clen = 0;
					//if(dbg)printf("dtFormat... %d\n",tTick);
					do {//delta time formatiing begins here
						Track.push_back(tTick & 0x7F);
						tTick >>= 7;
						clen++;
					} while (tTick != 0);
					for (int i = 0; i < (clen >> 1); i++) {
						swap(Track[Track.size() - 1 - i], Track[Track.size() - clen + i]);
					}
					for (int i = 2; i <= clen; i++) {
						Track[Track.size() - i] |= 0x80;///hack (going from 2 instead of going from one)
					}
					if (Event.A == 0x03) {
						Track.push_back(0xFF);
						Track.push_back(0x51);
						Track.push_back(Event.A);//03
						Track.push_back(Event.B);
						Track.push_back(Event.C);
						Track.push_back(Event.D);
					}
					else {
						Track.push_back(Event.B);
						Track.push_back(Event.C);
						Track.push_back(Event.D);
					}
					U++; //= pMS->erase(U);
				}
				pMS->clear();
				Track.push_back(0x00); Track.push_back(0xFF); Track.push_back(0x2F); Track.push_back(0x00);
				DWORD sz = Track.size() - 8;
				Track[4] = (sz & 0xFF000000) >> 24;
				Track[5] = (sz & 0xFF0000) >> 16;
				Track[6] = (sz & 0xFF00) >> 8;
				Track[7] = (sz & 0xFF);
				copy(Track.begin(), Track.end(), ostream_iterator<BYTE>(fout));
				s_out = "Track " + to_string((*Y).first) + " went to output\n";
				Track.clear();
				Y++;
			}
			fout.close();
		}
		void MapNotesAndReadBack() {
			vector<DWORD> PERKEYMAP;
			vector<DEC> KEYVEC;
			DC ImNote;
			DEC VecInsertable;
			ULI T, size, LastEdge = 0;
			multiset<DC>::iterator Y = --SET.end();
			if (!SET.size())return;
			for (int key = 0; key < 128; key++) {
				ImNote.Key = key;
				ImNote.Vol = 0;
				Y = SET.begin();
				while (Y != SET.end()) {
					if ((*Y).Key == key) {
						VecInsertable.Tick = (*Y).Tick;
						VecInsertable.TrackN = (*Y).TrackN;
						VecInsertable.Len = (*Y).Len;
						KEYVEC.push_back(VecInsertable);
						Y = SET.erase(Y);
						continue;
					}
					else {
						Y++;
					}
				}
				if (KEYVEC.back().Tick + KEYVEC.back().Len <= PERKEYMAP.size())
					PERKEYMAP.resize(KEYVEC.back().Tick + KEYVEC.back().Len, 0);
				for (auto it = KEYVEC.begin(); it != KEYVEC.end(); it++) {
					size = (*it).Tick + (*it).Len;
					if (size >= PERKEYMAP.size())
						PERKEYMAP.resize(size, 0);
					PERKEYMAP[(*it).Tick] = ((*it).TrackN << 1) | 1;
					for (ULI tick = (*it).Tick + 1; tick < size; tick++)
						PERKEYMAP[tick] = ((*it).TrackN << 1);
				}
				KEYVEC.clear();
				T = 0;
				LastEdge = 0;
				size = PERKEYMAP.size();
				while (T < size) {
					LastEdge = T;
					for (T++; T < size; T++) {
						if (PERKEYMAP[T] >> 1 != PERKEYMAP[T - 1] >> 1 || PERKEYMAP[T] & 1) {
							ImNote.Len = T - LastEdge;
							ImNote.Tick = LastEdge;
							ImNote.TrackN = (PERKEYMAP[T - 1] >> 1);
							LastEdge = T;
							if (ImNote.TrackN)SET.insert(ImNote);
							break;
						}
					}
				}
				PERKEYMAP.clear();
				s_out = ("Key "+to_string(key)+" processed in sustains removing algorithm");
			}
		}
		void Load(wstring Link) {
			InitializeNPrepare(Link);
			//s_out = ("Notecount : Successfully pushed notes (Count) : Notes and tempo count without overlaps\n");
			CTrack = 2;//fix//
			while (ReadSingleTrackFromCurPos()) { CTrack++; s_out = to_string(NC) + " : " + to_string(PC) + " : " + to_string(ONC); }
			fin.close();
			s_out = "Magic finished with set size " + to_string(SET.size());
			multiset<DC>::iterator Y = SET.begin();
			while (Y != SET.end()) {//bcuz i want so
				if (TRS.find(((*Y).TrackN)) == TRS.end())TRS.insert(((*Y).TrackN));
				Y++;
			}
			s_out = ("Prepaired for output...\n");
			s_out = "Tracks used: " + to_string(TRS.size());
			FormMIDI(Link);
			Finished = 1;
			s_out = "Finished!";
		}
	};

	struct Data {
		int cellcolor;
		bool isbeginborder;
	};

	struct SustainsRemover {
		//using Keys = array<Data, 129>;
		//vector<Keys> Map;
		ULI FileSize, CurFilePos;
		BYTE RSB, Finished;
		WORD PPQN;
		DWORD CTrack;
		list<ULI> *PNO;//first 128 is first channel, next 128 are the second... etc//quite huge boi 
		string s_out;
		ifstream fin;
		SustainsRemover(ULI FileSize = 1) {
			s_out = " ";
			Finished = 0;
			RSB = PPQN = CTrack = 0;
			PNO = new list<ULI>[2048];
		}
		void ClearPNO() {
			for (int i = 0; i < 2048; i++)PNO[i].clear();
		}
		void InitializeNPrepare(wstring link) {
			fin.open(link.c_str(), std::ios::binary | std::ios::in);
			DWORD MThd;
			//if(dbg)cout<<"Max set size:"<<SET.max_size()<<endl;
			BYTE IO;
			for (int i = 0; i < 4; i++) {
				IO = fin.get();
				MThd = (MThd << 8) | (IO);
			}
			if (MThd == MTHD) {
				fin.seekg(12);
				for (int i = 0; i < 2; i++) {
					IO = fin.get();
					PPQN = (PPQN << 8) | (IO);
				}
				fin.seekg(14);
				RSB = 0;
				CTrack = 0;
				if (dbg)s_out = "Header\n";
			}
			else {
				fin.close();
				s_out = "Input file doesn't begin with MThd";
			}
		}
		bool ReadSingleTrackFromCurPos() {//continue?
			BYTE MTrk[4];
			RSB = 0;
			ULI CTick = 0;//current tick
			DWORD RTick = 0, RData;//real tick (which we just read rn)//read data
			RtlZeroMemory(MTrk, 4);
			ClearPNO();
			for (int i = 0; i < 4 && !fin.eof() && !fin.bad(); i++)fin >> MTrk[i];
			while (MTrk[0] != 'M' && MTrk[1] != 'T' && MTrk[2] != 'r' && MTrk[3] != 'k' && !fin.eof() && !fin.bad()) {//such a hack//seeking MTrk
				swap(MTrk[0], MTrk[1]);
				swap(MTrk[1], MTrk[2]);
				swap(MTrk[2], MTrk[3]);
				MTrk[3] = fin.get();
				s_out = ("Seeking for MTrk\n");///It's standart :t
			}
			for (int i = 0; i < 4 && !fin.bad(); i++)fin.get();//itterating through track's lenght//because its SAF branch app :3
			while (!fin.bad() && !fin.eof()) {
				//cout<<"MP: "<<CountMomentalPolyphony()<<endl;
				RTick = ReadVLV();
				if (!ParseEvent(CTick += RTick))return 1;
			}
			return 0;
		}
		DWORD CountMomentalPolyphony() {//debug purposes
			DWORD N = 0;
			for (int i = 0; i < 2048; i++)N += PNO[i].size();
			return N;
		}
		DWORD ReadVLV() {//from current position
			DWORD VLV = 0;
			BYTE B = 0;
			if (!fin.eof() && !fin.bad()) {
				do {
					B = fin.get();
					VLV = (VLV << 7) | (B & 0x7F);
				} while (B & 0x80);
				return VLV;
			}
			else {
				s_out = "Failed to read VLV at " + to_string(fin.tellg());//people shouldnt know about VLV being corrupted *lenny face*
				return 0;
			}
		}
		ULI FindAndPopOut(LTE pos, ULI CTick) {
			list<ULI>::iterator Y = PNO[pos].begin();
			ULI q = PNO[pos].size();
			/*
			while(q>0 && ((*Y)&VOLUMEMASK)==CTick){
				Y++;
				q--;
			}
			*/
			if (q > 0) {
				q = (*Y);
				PNO[pos].erase(Y);
				return q;
			}
			else {
				s_out = "FaPO error " + to_string(pos) + " " + to_string(CTick);//not critical error//but still annoying
				return CTick | (VOLUMEMASK + 1);
			}
		}
		bool ParseEvent(ULI absTick) {//should we continue?
			BYTE IO, T; LTE pos; ULI FAPO;
			if (!fin.bad() && !fin.eof()) {
				IO = fin.get();
				if (IO >= 0x80 && IO <= 0x8F) {//NOTEOFF
					RSB = IO;
					NC++;
					IO = fin.get() & 0x7F;
					pos = ((RSB & 0x0F) << 7) | IO;//position of stack for this key/channel pair
					fin.get();//file thing ended
					//if(dbg)printf("NOTEOFF:%x%x00 at %llu\n",RSB,IO,absTick);
					DC Ev;//event push prepairings
					Ev.Key = IO;
					Ev.TrackN = RSB & 0x0F | ((CTrack) << 4);
					if (!PNO[pos].empty()) {
						FAPO = FindAndPopOut(pos, absTick);
						Ev.Tick = FAPO & VOLUMEMASK;
						Ev.Len = absTick - Ev.Tick;
						Ev.Vol = FAPO >> 56;
						PushNote(Ev);
					}
					//else if (0)cout << "Detected empty stack pop-attempt (N):" << (unsigned int)(RSB & 0x0F) << '-' << (unsigned int)IO << endl;
				}
				else if (IO >= 0x90 && IO <= 0x9F) {//NOTEON
					RSB = IO;
					IO = fin.get() & 0x7F;
					T = fin.get() & 0x7F;
					//if(dbg)printf("NOTEON:%x%x%x at %llu\n",RSB,IO,T,absTick);
					pos = ((RSB & 0x0F) << 7) | IO;
					if (T != 0)PNO[pos].push_front(absTick | (((ULI)T) << 56));
					else {//quite weird way to represent note off event...
						DC Ev;//event push prepairings
						Ev.Key = IO;
						Ev.TrackN = RSB & 0x0F | ((CTrack) << 4);
						if (!PNO[pos].empty()) {
							FAPO = FindAndPopOut(pos, absTick);
							Ev.Tick = FAPO & VOLUMEMASK;
							Ev.Len = absTick - Ev.Tick;
							Ev.Vol = FAPO >> 56;
							PushNote(Ev);
						}
						//else if (0)cout << "Detected empty stack pop-attempt (0):" << (RSB & 0x0F) << '-' << (unsigned int)IO << endl;
					}
					//cout<<PNO[pos].front()<<endl;
				}
				else if ((IO >= 0xA0 && IO <= 0xBF) || (IO >= 0xE0 && IO <= 0xEF)) {//stupid unusual vor visuals stuff 
					RSB = IO;
					fin.get(); fin.get();
				}
				else if (IO >= 0xC0 && IO <= 0xDF) {
					RSB = IO;
					fin.get();
				}
				else if (IO >= 0xF0 && IO <= 0xF7) {
					RSB = 0;
					DWORD vlv = ReadVLV();
					//fin.seekg(vlv,std::ios::cur);
					for (int i = 0; i < vlv; i++)fin.get();
				}
				else if (IO == 0xFF) {
					RSB = 0;
					IO = fin.get();
					DWORD vlv = 0;
					if (IO == 0x2F) {
						ReadVLV();
						return 0;
					}
					else if (IO == 0x51) {
						fin.get();//vlv
						for (int i = 0; i < 3; i++) {//tempochange data
							IO = fin.get();
							vlv = (vlv << 8) | IO;
						}//in vlv we have tempo data :)

						//if(dbg)printf("TEMPO:%x at %x\n",vlv,fin.tellg());
						DC Ev;//event push prepairings
						Ev.Key = 0xFF;
						Ev.TrackN = 0;
						Ev.Tick = absTick;
						Ev.Len = vlv;
						PushNote(Ev);
					}
					else {
						vlv = ReadVLV();
						//if(dbg)printf("REGMETASIZE:%x at %x\n",vlv,fin.tellg());
						//fin.seekg(vlv,std::ios::cur);
						for (int i = 0; i < vlv; i++)fin.get();
					}
				}
				else {
					if (RSB >= 0x80 && RSB <= 0x8F) {//NOTEOFF
						NC++;
						RSB = RSB;
						fin.get();//same
						pos = ((RSB & 0x0F) << 7) | IO;//position of stack for this key/channel pair
						DC Ev;//event push prepairings
						Ev.Key = IO;
						Ev.TrackN = RSB & 0x0F | ((CTrack) << 4);
						if (!PNO[pos].empty()) {
							FAPO = FindAndPopOut(pos, absTick);
							Ev.Tick = FAPO & VOLUMEMASK;
							Ev.Len = absTick - Ev.Tick;
							Ev.Vol = FAPO >> 56;
							PushNote(Ev);
						}
						//else if (0)cout << "Detected empty stack pop-attempt (RN):" << (unsigned int)(RSB & 0x0F) << '-' << (unsigned int)IO << endl;
					}
					else if (RSB >= 0x90 && RSB <= 0x9F) {//NOTEON
						RSB = RSB;
						T = fin.get() & 0x7F;//magic finished//volume
						pos = ((RSB & 0x0F) << 7) | IO;
						if (T != 0)PNO[pos].push_front(absTick | (((ULI)T) << 56));
						else {//quite weird way to represent note off event...
							DC Ev;//event push prepairings
							Ev.Key = IO;
							Ev.TrackN = RSB & 0x0F | ((CTrack) << 4);
							if (!PNO[pos].empty()) {
								FAPO = FindAndPopOut(pos, absTick);
								Ev.Tick = FAPO & VOLUMEMASK;
								Ev.Len = absTick - Ev.Tick;
								Ev.Vol = FAPO >> 56;
								PushNote(Ev);
							}
							//else if (0)cout << "Detected empty stack pop-attempt (R0):" << (unsigned int)(RSB & 0x0F) << '-' << (unsigned int)IO << endl;
						}
					}
					else if ((RSB >= 0xA0 && RSB <= 0xBF) || (RSB >= 0xE0 && RSB <= 0xEF)) {//stupid unusual for visuals stuff 
						RSB = RSB;
						fin.get();
					}
					else if (RSB >= 0xC0 && RSB <= 0xDF) {
						RSB = RSB;
					}
					else {
						s_out = "Imaprseable data...\n\tdebug:" + to_string((unsigned int)RSB) + ":" + to_string((unsigned int)IO) + ":Off(FBegin):" + to_string(fin.tellg());
						BYTE I = 0, II = 0, III = 0;
						while (!(I == 0x2F && II == 0xFF && III == 0) && !fin.eof()) {
							III = II;
							II = I;
							I = fin.get();
						}
						fin.get();
						return 0;
					}
				}
			}
			else return 0;
			return 1;
		}
		void PushNote(DC& Ev) {
			ULI Tck = Ev.Tick;
			Tck += Ev.Len;


		}

		void Load(wstring Link) {
			InitializeNPrepare(Link);
			printf("Notecount : Successfully pushed notes (Count) : Notes and tempo count without overlaps\n");
			CTrack = 2;//fix//
			while (ReadSingleTrackFromCurPos()) { CTrack++; cout << NC << " : " << PC << " : " << ONC << endl; }
			fin.close();
			s_out = ("Prepaired for output...\n");
			Finished = 1;
		}
	};
}

#pragma pack(pop)
#undef ULI
#undef LTE
#undef TNT
#undef KDT
#undef VOLUMEMASK
#undef MTHD
#undef MTRK