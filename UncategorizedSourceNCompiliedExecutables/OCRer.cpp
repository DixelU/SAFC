#include <iostream>
#include <windows.h>
#include <iterator>
#include <fstream>
#include <vector>
#include <list>
#include <set>
using namespace std;
bool dbg=0;
struct EV {
	long long int o;
	vector<char> d;
};
void EX(string link, string olink,bool VIS){
	ifstream _in;
	vector<char>HEADER;
	_in.open(link.c_str(), std::ios::binary | std::ios::in);
	char IO;
	long long int AO=0;
	for(int i=0;i<14&&_in.get(IO);i++)HEADER.push_back(IO);//going through header
	short am=HEADER[10]*256+HEADER[11];
	vector<list<EV> > MF,AF;
	list<EV>Track;
	EV E;
	cout<<"Starting onload preprocessing\n";
	while(_in){//iterating through tracks
		cout<<"Track "<<MF.size()<<endl;
		E.o=0;
		for(int i=0;i<8;i++)_in.get(IO);
		while(_in){//events
			E.d.clear();
			int tof=0;
			while(1){
				_in.get(IO);
				tof=(((unsigned char)IO) & 0x7F)+tof*128;
				if((unsigned char)IO<0x80)break;
			}
			E.o+=tof;
			_in.get(IO);
			if(dbg)printf("%d:%x",E.o,(int)((unsigned char)(IO)));
			if((((unsigned char)IO)>=0x80&&((unsigned char)IO)<=0xBF) || (((unsigned char)IO)>=0xE0&&((unsigned char)IO)<0xF0)){//note off
				E.d.push_back(IO);
				for(int i=0;i<2;i++){
					_in.get(IO);E.d.push_back(IO);
				}
			}
			else if(((unsigned char)IO)>=0xC0 && ((unsigned char)IO)<=0xDF){
				E.d.push_back(IO);
				_in.get(IO);
				E.d.push_back(IO);
			}
			else if(((unsigned char)IO)==0xF7 || ((unsigned char)IO)==0xF0 ){
				E.d.push_back(IO);
				IO=0;
				int magic=0;
				for(;true;){//getting current offset
					_in.get(IO);
					E.d.push_back(IO);
					magic=(magic<<7)+(((unsigned char)IO)&0x7F);
					if(((unsigned char)IO)<0x80)break;
				}
				for(int e=0;e<magic&&_in.get(IO);e++)E.d.push_back(IO);
			}
			else if(((unsigned char)IO)==0xFF){//metaaaaa eveeeeent
				E.d.push_back(IO);//output
				_in.get(IO);//throw away type of data
				E.d.push_back(IO);
				if(((unsigned char)IO)==0x2F){//except track's terminating
					_in.get(IO);
					E.d.push_back(IO);
					Track.push_back(E);
					MF.push_back(Track);
					Track.clear();
					if(dbg)printf("Track term.\n");
					break;
				}
				IO=0;
				int magic=0;
				for(int i=0;true;i++){//getting current offset
					_in.get(IO);
					E.d.push_back(IO);
					magic=(magic<<7)+(((unsigned char)IO)&0x7F);
					if(((unsigned char)IO)<0x80)break;
				}
				for(int e=0;e<magic && _in.get(IO);e++)E.d.push_back((IO));
			}
			else{
				E.d.push_back(IO);
				//Track.push_back(E);
				cout<<"UNKNOWN EVENT:";//how to skip that?
				printf(" %x\nExpect corruptions...\n",(unsigned char)IO);
				//break;
			}
			if(dbg)printf("Handled data: ");
			for(int i=0;dbg&&i<E.d.size();i++){
				printf(" %x",(int)((unsigned char)(E.d[i])));
			}
			if(dbg)printf("\n");
			Track.push_back(E);
		}
	}
	_in.close();
	cout<<"File has loaded! :)"<<endl;
	vector<int> t(128,0);
	vector<vector<int> > NOD((int)MF.size(),t);//debt for note off events
	Track.clear();
	for(int i=0;i<MF.size();i++)AF.push_back(Track);
	long long int CO=0,mo=0;//current offset
	unsigned int tt;
	bool flag=1;
	cout<<"Making some magic"<<endl;
	while(true){//scanning notes as if they were played
		//cout<<mo;
		if(!flag)break;
		flag=0;
		if(dbg)cout<<mo<<" "<<CO<<endl;
		CO=mo;
		mo=0x7FFFFFFFF;
		vector<unsigned int> PNO(128,0); //FF FF FFFF  (volume, event itself, track number)
		for(int i=MF.size()-1;i>=0;i--){//through all the tracks from ending
			if(!MF[i].empty())flag=1;
			while(!MF[i].empty() && MF[i].front().o==CO){//going for every single event on this tick
				if((unsigned char)(MF[i].front().d[0])<0x80||(unsigned char)MF[i].front().d[0]>0x9F){
					AF[i].push_back(MF[i].front());
					if(dbg){
						printf("U: ");
						for(int a=0;a<MF[i].front().d.size();a++)printf("%x ",(unsigned char)MF[i].front().d[a]);
						printf("\n");
					}
					MF[i].pop_front();
				}
				else{//sheeeeet
					if((unsigned char)MF[i].front().d[0]>=0x90 && (unsigned char)MF[i].front().d[0]<=0x9F){
						if(dbg)printf("%x%x\n",(unsigned char)MF[i].front().d[0],(unsigned char)MF[i].front().d[1]);
						if(PNO[(MF[i].front().d[1])]<((int)((unsigned char)(MF[i].front().d[2]))<<24)){
							if(PNO[(MF[i].front().d[1])]==0){
								tt=((int)((unsigned char)(MF[i].front().d[2]))<<24)+((unsigned char)MF[i].front().d[0]<<16)+i;
								if(dbg)printf("Key event %x at %x is first\n",(unsigned char)MF[i].front().d[0],CO);
								PNO[(MF[i].front().d[1])]=tt;
							}
							else{
								tt=PNO[(MF[i].front().d[1])]&0xFFFFFF;
								tt+=((unsigned char)(MF[i].front().d[2])<<24);
								if(dbg)printf("Not a first key event %x at %x: new volume is %x\n",(unsigned char)MF[i].front().d[0],CO,(unsigned char)(MF[i].front().d[2]));
								PNO[(MF[i].front().d[1])]=tt;
								NOD[i][(MF[i].front().d[1])]++;
							}
						}
					}
					else if(NOD[i][(MF[i].front().d[1])]==0 || 1){
						AF[i].push_back(MF[i].front());
					}
					else if(NOD[i][(MF[i].front().d[1])]<0){
						printf("`Debt` for note off events is negative!\n");
						AF[i].push_back(MF[i].front());
						NOD[i][(MF[i].front().d[1])]=0;
					}
					else {NOD[i][(MF[i].front().d[1])]--;}
					MF[i].pop_front();
				}
			}
			if(mo>MF[i].front().o)mo=MF[i].front().o;
		}
		if(VIS)printf("\r");
		for(int i=0;i<128;i++){
			if(PNO[i]){ // (PNO[i]&0xFF000000)>>24  (PNO[i]&0xFF0000)>>16  (PNO[i]&0xFFFF) ; 
				if(VIS)printf("N");
				E.o=CO;
				E.d.clear();
				E.d.push_back( (PNO[i]&0xFF0000)>>16 );
				E.d.push_back( i );
				E.d.push_back( (PNO[i]&0xFF000000)>>24 );
				AF[(PNO[i]&0xFFFF)].push_back((E));
			}
			else if(VIS)printf(" ");
		}
		//cout<<"\r";
	}
	ofstream _out(olink.c_str(),std::ios::binary | std::ios::out);
	copy(HEADER.begin(),HEADER.end(),ostream_iterator<char>(_out,""));
	vector<char> TRACK,ad;
	list<EV>::iterator l;
	cout<<"\nFinal reparsing\n";
	for(int i=0;i<AF.size();i++){//iterating through tracks
		TRACK.clear();
		l=AF[i].begin();
		for(int q=0;l!=AF[i].end();q++){//events
			ad.clear();
			CO=(*l).o-mo;
			mo=(*l).o;
			for(;true;){
				ad.push_back((CO&0x7F));
				if(CO<128)break;
				CO=CO>>7;
			}
			for(int r=ad.size()-1;r>=0;r--)TRACK.push_back((r)?(ad[r]|0x80):(ad[r]));
			for(int r=0;r<(*l).d.size();r++)TRACK.push_back((*l).d[r]);
			advance(l,1);
		}
		ad.clear();
		CO=TRACK.size();
		_out<<'M'<<'T'<<'r'<<'k'<<(char)((CO&0xFF000000)>>24)<<(char)((CO&0xFF0000)>>16)<<(char)((CO&0xFF00)>>8)<<(char)((CO&0xFF));
		cout<<"Track "<<i<<endl;
		copy(TRACK.begin(),TRACK.end(),ostream_iterator<char>(_out,""));
	}
}

int main(int argc,char ** argv){
	system("mode CON: COLS=128 LINES=50");
	string in,out;
	if(argc<2){
		getline(cin,in);
		out=in+".OCR.mid";
	}
	else{
		in=argv[1];
		out=(in+".OCR.mid");
	}
	EX(in,out,0);
	system("pause");
}
