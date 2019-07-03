#include <iostream>
#include <vector>
#include <set>
#include <string>
#include <fstream>
#include <sstream>
#include <iterator>
#include <windows.h>
using namespace std;//Sorry for bad english :)
//I usually write comments on russian with a lot of\
jokes and stories. 

bool is64bit(void){
    DWORD flag = GetFileAttributes("C:\\Windows\\SysWOW64");
    if(flag == 0xFFFFFFFFUL){
        if(GetLastError() == ERROR_FILE_NOT_FOUND)
            return false;
    }
    if(! (flag & FILE_ATTRIBUTE_DIRECTORY))
        return false;
    return true;
}
bool FELOG=false,PCLOG=false,dbg=true,ORDPCHNG=false,EMPTYTRCKREM=false;//File error log//Processing console log//
struct FO{
	int offset;
	int filenum;
};
bool operator<(FO a,FO b){return a.filenum<b.filenum;}
bool operator>(FO a,FO b){return a.filenum>b.filenum;}
bool operator<=(FO a,FO b){return a.filenum<=b.filenum;}
bool operator>=(FO a,FO b){return a.filenum>=b.filenum;}
bool operator==(FO a,FO b){return a.filenum==b.filenum;}
set<FO> Fileoffset;
string exF="";
void LOG(string LG,string file){//because why not
	cout<<LG<<endl;
	if(FELOG){
		ofstream logout("Log.log", std::ios::app);
		logout<<"["<<file<<"]: "<<LG<<endl;
	}
}

vector<string> BATstart(int argc, char** arg){//easy to understand
	remove("Log.log");
	vector<string> IO;
	string t;
	for(int i=1;i<argc;i++){
		t=arg[i];
		IO.push_back(t);
	}
	return IO;
}
vector<string> CONstart(){//parses commands from console
	vector<string> VIO;
	string SI="",t="";
	getline(cin,SI);
	bool BRACK=false;
	//char IO;
	for(int i=0;i<SI.size();i++){//i don't think it's hard to understand it
		if(SI[i]!='"' && !BRACK && SI[i]!=' '){
			t.push_back(SI[i]);
		}
		else if(SI[i]=='"' && !BRACK){
			i++;
			while(i<SI.size() && SI[i]!='"'){
				t.push_back(SI[i]);
				i++;
			}
			VIO.push_back(t);
			t="";
			i++;
		}
		else{
			VIO.push_back(t);
			t="";
		}
	}
	return VIO;
}
vector<unsigned short> PPQNreader(vector<string> link){//reads ppqs in files
	unsigned short t=12;
	vector<unsigned short> PPQN;
	for(int z=0;z<link.size();z++){
		t=0;
		ifstream fin;
		fin.open(link[z].c_str(),std::ios::binary | std::ios::in);
		char IO;
		//for(int i=0;i<12&&fin.get(IO);i++);
		fin.seekg(12,std::ios::beg);
		if(!(fin.bad() || fin.eof() || fin.fail())){
			//for(int i=0;i<12&&fin.get(IO);i++);
			fin.get(IO);
			t=(short)((unsigned char)IO)*256;
			fin.get(IO);
			t+=(short)((unsigned char)IO);
			PPQN.push_back(t);
		}
	}
	return PPQN;
}
vector<int> TRAM(vector<string> link){//reads tracks number from midi files
	int t;
	vector<int> TRAM;
	for(int z=0;z<link.size();z++){
		t=0;
		ifstream fin;
		fin.open(link[z].c_str(),std::ios::binary | std::ios::in);
		char IO;
		//for(int i=0;i<10&&fin.get(IO);i++);
		fin.seekg(10,std::ios::beg);
		fin.get(IO);
		t=((int)((unsigned char)IO))<<8;
		fin.get(IO);
		t+=((int)((unsigned char)IO));
		TRAM.push_back(t);
	}
	return TRAM;
}
int PPQNch(string link,double mult,int offset,int otemp){//returns tracks count//because it will help me later .w.
	ofstream _out;
	ifstream _in;
	int EventCounter = 0;
	string _coll="";
	otemp=60000000/otemp;
	bool ORDED=0;
	unsigned char ORD1=(otemp&0xFF0000)>>16, ORD2=(otemp&0xFF00)>>8, ORD3=otemp&0xFF;
	vector<unsigned char> UCO;
	if(PCLOG)printf("ORDTMPEVENT: %x %x %x\n",ORD1,ORD2,ORD3);
	_out.open((link+"_.mid").c_str(),std::ios::binary | std::ios::out );//when it was not binary\
	i had an issue that i called Bx0D. ofc it's already fixed, but if you will try\
	to make a clone or smth, be aware of char 0x0A!!!
	_in.open(link.c_str(), std::ios::binary | std::ios::in);
	char IO;int magic,_mc,tracksAM=0,ttt;
	unsigned char PEV=0,tsp=0;
	vector<char> TRACK,t;
	double tr=0;
	for(int i=0;i<12&&_in.get(IO);i++)_out.put(IO);//going through header
	_in.get(IO);
	magic=(short)((unsigned char)IO)*256;//trying to replace ppqn
	_in.get(IO);
	magic+=(short)((unsigned char)IO);
	if(PCLOG)cout<<"Current PPQN: "<<magic<<endl;
	magic=(double)magic/mult;
	if(PCLOG)cout<<"Target PPQN: "<<magic<<endl;
	_out.put((unsigned char)(magic/256));
	_out.put((unsigned char)(magic%256));//here we are finishing this magic with ppqn
	while(_in){//iterating through tracks
		tr=offset;
		EventCounter = 0;
		for(int i=0;_coll[_coll.size()-1]!='k'&& _coll[_coll.size()-2]!='r' && _coll[_coll.size()-3]!='T' && _coll[_coll.size()-4]!='M' && _in;i++){//collecting mtrk
			if(PCLOG)if(i>5)cout<<"Weirdly long awaiting for MTrk: "<<i-5<<endl;
			_in.get(IO);
			_coll.push_back(IO);
			if(!_in){
				_in.close();
				_out.close();
				return tracksAM;
			}
		}
		TRACK.clear();
		if(PCLOG)cout<<"Header: "<<_coll<<endl;
		_coll.clear();
//		//_out<<_coll;//
//		for(int y=0;y<4;y++){
//			TRACK.push_back(_coll[_coll.size()+y-4]);
//		}
//		_coll="";
		TRACK.push_back('M');TRACK.push_back('T');TRACK.push_back('r');TRACK.push_back('k');
		for(int i=0;i<4&&_in.get(IO);i++)TRACK.push_back(0);//going through the number of events 
		IO=0;
		if(otemp>0 && !ORDED){//adding new tempoevent
			TRACK.push_back(0x00);
			TRACK.push_back(0xFF);
			TRACK.push_back(0x51);
			TRACK.push_back(0x03);
			TRACK.push_back(ORD1);
			TRACK.push_back(ORD2);
			TRACK.push_back(ORD3);
		}
		//printf("tellg_%x\n",_in.tellg());
		//_in.get(IO);///////reading first byte of the track which can be nonzero'ed
		while(_in){//here we are iterating through the events in the midi track
			magic=0;
			do{//getting current offset//more elegant solution
				_in.get(IO);
				magic=(magic<<7)|(IO&0x7F);
			}while(IO&0x80);
			if(PCLOG)cout<<"Event`s offset: "<<magic;//why it works faster than in R-SAFC?
			tr+=(double)(magic)/(double)mult;
			magic=tr;
			tr=tr-magic;//some FL studio-like hacking in ppqn switching xd
			if(PCLOG)cout<<" -> "<<magic<<" + Remnant: "<<tr<<endl;
			_mc=magic;
			_coll="";//coll like collector. needed for switching magic
			UCO.clear();
			if(PCLOG)cout<<"Recalculated offset: ";
			if(_mc==0){
				TRACK.push_back((unsigned char)0);
				if(PCLOG)cout<<0<<endl;
			}
			while(_mc!=0){//doing some more magic
				IO=_mc&0x7F;
				UCO.push_back((unsigned char)IO);
				_mc=_mc>>7;
			}
			for(int w=0;w<UCO.size();w++){//
				if(PCLOG)printf("%x ",(unsigned char)UCO[w]);
				TRACK.push_back((unsigned char)( (w==UCO.size()-1)?(UCO[UCO.size()-1-w]):(UCO[UCO.size()-1-w] | 0x80) ));
			}
			if(PCLOG)printf("\n");
			_in.get(IO);
			tsp=IO;//saving current event
			if(PCLOG)printf("Event: %x\n",(unsigned char)IO);
			if(((unsigned char)IO)==0xFF){//metaaaaa eveeeeent
				tsp=PEV=0;
				TRACK.push_back(IO);//output
				_in.get(IO);//throw away type of data
				//if((unsigned char)IO&0x80)printf("Corruption in meta event: 1st parameter (%x)\n",IO);
				TRACK.push_back(IO);
				switch(IO){
					case 0x51:
					//case 0x54:
					//case 0x58:
						++EventCounter;
					break;
				}
				if(((unsigned char)IO)==0x2F){//except track's terminating
					_in.get(IO);
					TRACK.push_back(IO);
					t.clear();
					int SZ=TRACK.size()-8;
					for(int w=0;w<4;w++){
						t.push_back(SZ%256);
						SZ/=256;
					}
					TRACK[4]=t[3];//rewriting size of track
					TRACK[5]=t[2];
					TRACK[6]=t[1];
					TRACK[7]=t[0];
					if(!(EMPTYTRCKREM && !EventCounter)){
						printf("\t%d track processed\n",tracksAM++);//just write down that this track has ended
						copy(TRACK.begin(),TRACK.end(),ostream_iterator<char>(_out,""));
					}
					else {
						TRACK.clear();
						printf("\t%d track was removed!\n",tracksAM);
					}
					break;
				}
				else if(otemp<0||(((unsigned char)IO)!=0x51&&otemp>0)){
					IO=0;
					magic=0;
					for(int i=0;true;i++){//getting current offset
						IO=_in.get();
						TRACK.push_back(IO);
						magic = (magic<<7) | (((unsigned char)IO)&0x7F);
						if(!(IO&0x80))break;
					}
					//if(PCLOG)printf("Magic%d\n",magic);
					if(magic)for(int e=0;e<magic && _in.good();e++){
						_in.get(IO);
						//if(PCLOG)printf("Magic: %x\n",IO);
						TRACK.push_back(IO);
					}
					else continue;
				}//EMPTYTRCKREM
				else{//and if we are replacing tempoevents
					_in.get(IO);_in.get(IO);_in.get(IO);_in.get(IO);//
					TRACK.push_back(3);//r e p l a c e m e n t  s t u f f s 
					TRACK.push_back(ORD1);
					TRACK.push_back(ORD2);
					TRACK.push_back(ORD3);
				}
			}
			else if( (((unsigned char)IO)>=0x80&&((unsigned char)IO)<=0xBF) || (((unsigned char)IO)>=0xE0&&((unsigned char)IO)<0xF0) ){
				++EventCounter;
				if( true || ((unsigned char)IO)!=0xB0 ){
					TRACK.push_back(IO);
					_in.get(IO);
					TRACK.push_back(IO);
					_in.get(IO);
					TRACK.push_back(IO);
				}
			}
			else if( ((unsigned char)IO)==0xF7 || ((unsigned char)IO)==0xF0 ){
				++EventCounter;
				TRACK.push_back(IO);
				magic=IO=tsp=PEV=0;
				for(int i=0;true;i++){//getting current offset
					_in.get(IO);
					TRACK.push_back(IO);
					magic=(magic<<7)|(((unsigned char)IO)&0x7F);
					if(((unsigned char)IO)<0x80)break;
				}
				for(int e=0;e<magic&&_in.get(IO);e++)TRACK.push_back(IO);
			}
			else if(((unsigned char)IO)>=0xC0 && ((unsigned char)IO)<=0xCF){
				++EventCounter;
				TRACK.push_back(IO);
				_in.get(IO);
				if(ORDPCHNG)TRACK.push_back(0);//Override program change
				else TRACK.push_back(IO);
			}
			else if(((unsigned char)IO)>=0xD0 && ((unsigned char)IO)<=0xDF){
				++EventCounter;
				TRACK.push_back(IO);
				_in.get(IO);
				TRACK.push_back(IO);
			}
			else{////RUNNING STATUS PARSER
				if(PCLOG)printf("Running status byte? %x at %d\n",IO,TRACK.size());
				if( (((unsigned char)PEV)>=0x80&&((unsigned char)PEV)<=0xBF) || (((unsigned char)PEV)>=0xE0&&((unsigned char)PEV)<=0xEF) ){
					++EventCounter;
					TRACK.push_back(PEV);
					//_in.get(IO);//running status
					TRACK.push_back(IO);
					_in.get(IO);
					TRACK.push_back(IO);
				}
				else if(((unsigned char)PEV)>=0xC0 && ((unsigned char)PEV)<=0xCF){
					++EventCounter;
					TRACK.push_back(PEV);
					if(ORDPCHNG)TRACK.push_back(0);//Override program change
					else TRACK.push_back(IO);
				}
				else if(((unsigned char)PEV)>=0xD0 && ((unsigned char)PEV)<=0xDF){
					++EventCounter;
					TRACK.push_back(PEV);
					TRACK.push_back(IO);
				}
				else{////
					EventCounter = 0;
					printf("Imparsable data sequence. R:%x I:%x at %d (AbOffset_%x)\n",(unsigned char)PEV,(unsigned char)IO,TRACK.size(),_in.tellg());
	                char I=1,II=1,III=1;//current IO, previous IO ... etc
	                while(!(I==0x2F&&(unsigned char)II==0xFF&&III==0)&&!_in.eof()){
	                    III=II;
	                    II=I;
	                    _in.get(I);
	                }
	                _in.get(IO);//last zero?
	                TRACK.push_back(0xC0);TRACK.push_back(0x0);TRACK.push_back(0x0);
	                TRACK.push_back(0xFF);TRACK.push_back(0x2F);TRACK.push_back(0x0);
	                t.clear();
	                int SZ=TRACK.size()-8;
	                for(int w=0;w<4;w++){
	                    t.push_back(SZ%256);
	                    SZ/=256;
	                }
	                TRACK[4]=t[3];
	                TRACK[5]=t[2];
	                TRACK[6]=t[1];
	                TRACK[7]=t[0];
	                if(0)for(int e=0;e<TRACK.size();e++){
	                    printf("%x ",(TRACK[e]));
	                }
	                if(!(EMPTYTRCKREM && !EventCounter)){
						printf("\tTermination of processing %d track due to corrupted data\n",tracksAM++);
	                	copy(TRACK.begin(),TRACK.end(),ostream_iterator<char>(_out,""));
					}
					else{
						TRACK.clear();
						printf("\tTerminated track %d was deleted due to having no significant data in it\n",tracksAM);
					}
	                break;
				}
				tsp=PEV;//handling the group of running status bytes
			}
			PEV=tsp;
			//_in.get(IO);
		}
	}
	_in.close();
	_out.close();
	TRACK.clear();
	_coll.clear();
	return tracksAM;
}

void MAINMERGEALGO(vector<string> filelink, vector<int> tracksAM , bool MRGTEMPOMAPS, bool _IGNORE_,string DEST,bool rrems){
	/*Technically playing in game "believe me" in midi file parsers someday will cause lots and lots of problems
	Here we should hope that midi file is pure an smooth as a gold mirror inside web's telescope...*/
	char IO;
	string T=DEST;
	int PERFtracksAM=0,passThr=0;
	if(dbg)cout<<"Merging\n";
	//long long int TMoc=0,ptime=0;//tempomaps merge is too hard for me//set<TEMPOEVENT> TM=MERGETEMPOMAPS(filelink);//TEMPOEVENT TE;
	for(int i=0;i<filelink.size();i++){
		if(!_IGNORE_)filelink[i]=filelink[i]+"_.mid";//just making IGNORER working again xd//well... Ignorer is gone
		PERFtracksAM+=tracksAM[i]-((i)?1:0);//
	}
	ifstream _base(filelink[0].c_str(), std::ios::binary | std::ios::in);
	ofstream _out(T.c_str(),std::ios::binary | std::ios::out);
	if(dbg)cout<<"Processing basement\n";
	for(int i=0;i<10&&_base.get(IO);i++){
		_out.put(IO);
	}
	_base.get(IO);//dances
	_out.put(PERFtracksAM/256);
	_base.get(IO);
	_out.put(PERFtracksAM%256);
	for(int i=0;i<2+4;i++){//ppq + conductor's MTrk
		_base.get(IO);
		_out.put(IO);//HEADER ends
	}
	if(dbg)cout<<"Basement main block copying\n";
	while(_base.get(IO)){//lul///here someday i'll try to build tempomaps merger... someday...
		_out.put(IO);
	}
	_base.close();
	for(int i=1;i<filelink.size();i++){
		ifstream _in(filelink[i].c_str(),std::ios::binary | std::ios::in);
		passThr=0;
		if(dbg)cout<<"Processing "<<i+1<<" file\n";
		for(int i=0;i<14+4&&_in.get(IO);i++);//throwing away header
		for(int i=0;i<4&&_in.get(IO);i++){
			passThr=(passThr<<8)+((unsigned char)IO);
		}
		for(int i=0;i<passThr&&_in.get(IO);i++);//and tempo map also c:
		if(dbg)cout<<"Copying its data\n";
		while(_in.get(IO))_out.put(IO);///copying midi data block cccccccc:
		_in.close();
	}
	if(!_IGNORE_&&rrems){
		if(dbg)cout<<"Removing remnants\n";
		for(int i=0;i<filelink.size();i++){
			remove(filelink[i].c_str());
			cout<<i<<endl;
		}
	}
}


void OpParser(vector<string> Ip){//lmao
	bool ignorist=0,excluder=0,m=0,err=0,rrems=0;
	int forcer=0,tempoORD=0;
	vector<string> filelink;
	string DEST="";
	for(int i=0;i<Ip.size();i++){
		if(Ip[i][0]=='\\'){
			int inc=1;
			switch(Ip[i][1]){
				case 'f':
					for(int q=Ip[i].size()-1;q>1;q--){
						forcer+=inc*(Ip[i][q]-'0');
						inc*=10;
					}
					cout<<"F:"<<forcer<<endl;
				break;
				case 'i':
					//ignorist=1;
					cout<<"Ignoring mode is not supported anymore :("<<endl;
				break;
				case 'c'://console.log enabling
					PCLOG=true;
				break;
				case 'r':
					rrems=1;
				break;
				case 't':
					for(int q=Ip[i].size()-1;q>1;q--){
						tempoORD+=inc*(Ip[i][q]-'0');
						inc*=10;
					}
					cout<<"Tempo override: "<<tempoORD<<endl;
				break;
				case 'p':
					ORDPCHNG=true;
					cout<<"Program chage event override: Piano\n";
				break;
				case 'm':
					cout<<"\"Empty\" tracks will be removed!\n";
					EMPTYTRCKREM = 1;
				break;
				default:
					cout<<Ip[i][1]<<" was not reserved\n";
				break;
			}
		}
		else{
			bool ch=true;//offset#filename
			if(Ip[i][0]>='0'&&Ip[i][0]<='9'){
				int offset=0,_o=0;
				string t="";
				bool sht=true;
				//cout<<"begin with "<<Ip[i]<<endl;
				for(int q=0;sht;q++){
					if(Ip[i][q]!='#')offset=offset*10+(Ip[i][q]-'0');
					else {
						_o=q;
						//cout<<"BREAK"<<q<<endl;
						sht=false;
						continue;
					}
					//cout<<offset<<" "<<Ip[i][q]<<endl;
				}
				for(int q=_o+1;q<Ip[i].size();q++){
					t.push_back(Ip[i][q]);
				}
				cout<<"Checking existance of "<<t<<" In case of reading offset"<<endl;
				ifstream fin;
				fin.open(t.c_str());
				if(fin){
					FO fo;
					fo.offset=offset;
					fo.filenum=filelink.size();
					filelink.push_back(t);
					Fileoffset.insert(fo);
				}
			}
			else{
				cout<<"Checking existance of "<<Ip[i]<<endl;
				ifstream fin;
				fin.open(Ip[i].c_str());
				if(fin)filelink.push_back(Ip[i]);
				else {
					ch=false;
					if(!Ip[i].find("S2:")){
						DEST="";
						for(int y=3;y<Ip[i].size();y++){
							DEST.push_back(Ip[i][y]);
						}
					}
					else{
						cout<<"Couldn`t find file: "<<Ip[i]<<endl;
						if(FELOG)LOG("Couln`t find file.",Ip[i]);
						err=1;
					}
				}
				fin.close();
			}
		}
	}
	if(DEST==""&&filelink.size()){
		DEST=filelink[0]+"_wasUsedAsMergesBasement.mid";
	}
	vector<unsigned short> PPQN=PPQNreader(filelink);
	vector<int> tracksAM;
	unsigned short MAXppqn=0;
	for(int i=0;i<PPQN.size();i++){
		if(PPQN[i]>MAXppqn)MAXppqn=PPQN[i];
		cout<<"PPQN of "<<filelink[i]<<":"<<PPQN[i]<<"\n";
	}
	MAXppqn=( (forcer)?forcer:MAXppqn );
	FO _TFO;
	if(ignorist)tracksAM=TRAM(filelink);
	for(int i=0;i<filelink.size()&&!ignorist;i++){
		_TFO.filenum=i;
		_TFO.offset=0;
		if(Fileoffset.find(_TFO)!=Fileoffset.end()){
			_TFO=*(Fileoffset.find(_TFO));
		}
		tracksAM.push_back(PPQNch(filelink[i],(double)PPQN[i]/MAXppqn,_TFO.offset,((tempoORD==0)?-1:tempoORD)));
		//rename(filelink[i].c_str(),( filelink[i]+"_nt.mid" ).c_str());
		//it's like operator's overload. \
		You are trying to calculate a summ of two "numbers", \
		but instead you nuke some nuclear bomb on the moon
		cout<<tracksAM[i]<<" tracks in "<<filelink[i]<<endl;
	}
	MAINMERGEALGO(filelink,tracksAM,m,ignorist,DEST,rrems);
	/*if(bocr){
		cout<<"Here we are starting experimental OCRer.exe :)\n";
		if(is64bit() && 0)ShellExecute(NULL,"open","OCRer64.exe",((ignorist)?(filelink[0]):(DEST)).c_str(),exF.c_str(),SW_SHOWNORMAL);
		else ShellExecute(NULL,"open","OCRer.exe",((ignorist)?(filelink[0]):(DEST)).c_str(),exF.c_str(),SW_SHOWNORMAL);
	}*/
	
}

int main(int argc, char** argv) {
	exF=argv[0];
	int pos=exF.rfind('\\');
	if(pos>0)exF.erase(exF.begin()+pos,exF.end());
	vector<string> ARGS;
	ARGS=BATstart(argc,argv);
	if(ARGS.empty()){
		cout<<"I'm Simple AF Converter created to help mortals with merging midis"<<endl;
		ARGS=CONstart();
	}
	cout<<(is64bit()?"64bit Detected":"32bit Detected")<<endl;
	//copy(ARGS.begin(),ARGS.end(),ostream_iterator<string>(cout,":"));
	OpParser(ARGS);
	if(dbg)system("pause");
}
