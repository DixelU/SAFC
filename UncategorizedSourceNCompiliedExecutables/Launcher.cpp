//#include <windows.h>
#include <conio.h>
//#include <iostream>
#include <vector>
#include <string>
#include <cmath>

#include "UCH.h"
using namespace std;
int _Fx=6,_Cx=100;//fontsizex windowsizex
int _Fy=9,_Cy=50;//fontsizey windowsizey
int _ppos;

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

struct CS{
	vector<string> inf;
	string ouf;
	int OFF,FOR,TMP;
	bool IGN,CON,REM,PORD,EMPTTRCKREM;
	void _(void){
		inf.clear();
		ouf="";
		EMPTTRCKREM=PORD=TMP=OFF=FOR=IGN=CON=0;
		REM=1;
	}
};
CS _CCC;
bool FStart=true/*,ocr=0*/;
string wayto="";
HWND __CNSL = GetConsoleWindow();

void CreateMess(const char abc[],const char header[]){
	MessageBoxA(0,abc,header, MB_OK);
}
void CreateMess(int number,string header){
	char t[50];
	sprintf(t,"%x",number);
	MessageBoxA(0,t,header.c_str(), MB_OK);
}
void CreateMess(string abc,string header){
	MessageBoxA(0,abc.c_str(),header.c_str(), MB_OK);
}
void CreateMess(char abc[],string header){
	MessageBoxA(0,abc,header.c_str(), MB_OK);
}
//////////////////////////////////
void GSFontsize(){
	RECT rc;
    GetClientRect(__CNSL,&rc);
    _Fx=rc.right/101;
    _Fy=rc.bottom/51;
}
///////////////////////////////////
POINT GetCCC(){//коорды мыши
    GSFontsize();
	POINT _pt,_tpt;
	GetCursorPos(&_pt);
	RECT *r=new RECT;
	GetWindowRect(__CNSL,r);
	_tpt.x=_pt.x - r->left -7; //Положение по x
	_tpt.y=_pt.y - r->top - 7 - 23; //Положение по у
	_pt=_tpt;
	return _tpt;
}
int Charlink(POINT _P){//0x FF(x) FF(y)
	if(_P.x<0||_P.x>100*_Fx||_P.y<0||_P.y>50*_Fy)return 0x10000;
	else{
		return ((_P.x/_Fx<<8)|((_P.y/(_Fy))));
	}
}
void ButtonDraw(TConsole &IO, string BMESS,int x,int y,int _col){
	//IO.TextBackground( ((_st!=1)?(0):(15)) );
	//IO.TextColor( ((_st==1)?0:( (_st)?12:15 )) );
	IO.TextColor(_col);
	IO.GotoXY(x,y);
	IO.Out<<(char)0xC9;
	for(int i=0;i<BMESS.size();i++)IO.Out<<(char)0xCD;
	IO.Out<<(char)0xBB;
	IO.GotoXY(x,y+1);
	IO.Out<<(char)0xBA<<BMESS<<(char)0xBA;
	IO.GotoXY(x,y+2);
	IO.Out<<(char)0xC8;
	for(int i=0;i<BMESS.size();i++)IO.Out<<(char)0xCD;
	IO.Out<<(char)0xBC;
	//IO.TextBackground(0);
	IO.TextColor(15);
}
void UIDraw(void);
int SEL=0;//selected // first viewed
void FSWt(){
	TConsole _cn;
	_cn.TextBackground(0);
	_cn.TextColor(15);
	int i;
	for(i=0;i<_CCC.inf.size() && i<47;i++){
		_cn.GotoXY(1,1+i);
		if(1+i==SEL){
			_cn.TextBackground(15);
			_cn.TextColor(0);
		}
		for(int j=0;j<84&&j<_CCC.inf[i].size();j++)_cn.Out<<_CCC.inf[i][j];
		for(int j=_CCC.inf[i].size();j<84;j++)_cn.Out<<" ";
		if(_CCC.inf[i].size()>=85)_cn.Out<<(char)0xDB;
		if(1+i==SEL){
			_cn.TextBackground(0);
			_cn.TextColor(15);
		}
	}
	for(;i<47;i++){
		_cn.GotoXY(1,1+i);
		for(int j=0;j<84;j++)_cn.Out<<" ";
	}
}
void EvalClickPos(int _hdpos){
	TConsole _tc;
	int x=_hdpos>>8,y=_hdpos&0xFF;
	if(x>0&&x<85){//list click
		if(y<=_CCC.inf.size() && y<47 &&y>0){
			SEL=y;
        	FSWt();
			_tc.TextColor(0);
			_tc.TextBackground(15);
			_tc.GotoXY(1,y);
        	for(int i=0;i<84&&i<_CCC.inf[y-1].size();i++){
        		_tc.Out<<_CCC.inf[y-1][i];
			}
			if(_CCC.inf[y-1].size()>=85)_tc.Out<<(char)0xDB;
			_tc.TextColor(15);
			_tc.TextBackground(0);
		}
	}
	else if(x>=88&&x<=96&&(y>0&&y<4)){//addfile dialog
		OPENFILENAME ofn;       // common dialog box structure
        char szFile[50000];       // buffer for file name
        HWND hwnd;              // owner window
        HANDLE hf;              // file handle

        // Initialize OPENFILENAME
        ZeroMemory(&ofn, sizeof(ofn));
        ZeroMemory(szFile, 50000);
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = NULL;
        ofn.lpstrFile = szFile;
        // Set lpstrFile[0] to '\0' so that GetOpenFileName does not 
        // use the contents of szFile to initialize itself.
        ofn.lpstrFile[0] = '\0';
        ofn.nMaxFile = sizeof(szFile);
        ofn.lpstrFilter = "MIDI Files(*.mid)\0*.mid\0";
        ofn.nFilterIndex = 1;
        ofn.lpstrFileTitle = NULL;
        ofn.nMaxFileTitle = 0;
        ofn.lpstrInitialDir = NULL;
        ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_EXPLORER;
        // Display the Open dialog box.
        if ( GetOpenFileName(&ofn)==TRUE ) {
        	string Link="",Gen="";
        	int i=0,counter=0;
        	for(;i<600&&szFile[i]!='\0';i++){
        		Link.push_back(szFile[i]);
			}
			for(;i<49998;){
				counter++;
				Gen="";
				for(;i<49998&&szFile[i]!='\0';i++){
					Gen.push_back(szFile[i]);
				}
	        	i++;
				//CreateMess(Gen,"");
	        	if(szFile[i]=='\0'){
	        		if(counter==1){
	        			_CCC.inf.push_back(Link);
					}
					else{
	        			_CCC.inf.push_back(Link+"\\"+Gen);
					}
	        		break;
				}
				else{
	        		if(Gen!="")_CCC.inf.push_back(Link+"\\"+Gen);
				}
			}
        	FSWt();
		}
	}
	else if(x>=88&&x<=96&&(y>3&&y<7)){//remfile
		if(SEL!=0)_CCC.inf.erase(_CCC.inf.begin()+SEL-1);
        SEL=0;
        FSWt();
	}
	else if(x>=87&&x<=97&&(y>6&&y<10)){//ppq force
		int FPPQ=0;
		string t="";
		_tc.GotoXY(88,8);
		_tc.Out<<"         ";
		_tc.GotoXY(88,8);
		_tc.In>>FPPQ;
		_CCC.FOR=FPPQ;
		while(FPPQ){
			t="0"+t;
			t[0]+=FPPQ%10;
			FPPQ/=10;
		}
		while(t.size()<5)t=" "+t;
		if(!_CCC.FOR)ButtonDraw(_tc,"PPQ:FORCE",87,7,7);
		else ButtonDraw(_tc,"PPQ:"+t,87,7,15);
	}
	else if(x>=87&&x<=97&&(y>9&&y<13)){//offset
		int OFF=0;
		string t="";
		_tc.GotoXY(88,11);
		_tc.Out<<"         ";
		_tc.GotoXY(88,11);
		_tc.In>>OFF;
		_tc.GotoXY(1,1);
		while(OFF){
			t="0"+t;
			t[0]+=OFF%10;
			OFF/=10;
		}
		if(SEL){
			//cout<<SEL<<" "<<t;
			if((_CCC.inf[SEL-1][0]<'0' || _CCC.inf[SEL-1][0]>'9') && _CCC.inf[SEL-1][0]!='#'){
				if(t!="")_CCC.inf[SEL-1]=t+"#"+_CCC.inf[SEL-1];
			}
			else{
				string poo=_CCC.inf[SEL-1];
				poo.erase(poo.begin(),poo.begin()+poo.find('#')+1);
				if(t!="")_CCC.inf[SEL-1]=t+"#"+poo;
			}
		}
		ButtonDraw(_tc,"SetOffset",87,10,14);
		FSWt();
	}
	else if(x>=88&&x<=96&&(y>12&&y<16)){//log
		_CCC.CON=!_CCC.CON;
		ButtonDraw(_tc,"  LOG  ",88,13,6 + 8*_CCC.CON);
	}
	else if(x>=88&&x<=96&&(y>15&&y<19)){//ignorer//no ignoring anymore
		_CCC.EMPTTRCKREM=!_CCC.EMPTTRCKREM;
		ButtonDraw(_tc,"ETR REM",88,16,5 + 8*_CCC.EMPTTRCKREM);
	}
	else if(x>=88&&x<=96&&(y>18&&y<22)){//remnants
		_CCC.REM=!_CCC.REM;
		ButtonDraw(_tc,"REMNANT",88,19,3 + 8*_CCC.REM);
	}
	else if(x>=88&&x<=96&&(y>21&&y<25)){//tempomap
		_CCC.TMP=1;
		string t="";int q=0;
		_tc.GotoXY(89,23);
		_tc.Out<<"       ";
		_tc.GotoXY(89,23);
		_tc.In>>_CCC.TMP;
		q=_CCC.TMP;
		while(q){
			t="0"+t;
			t[0]+=q%10;
			q/=10;
		}
		while(t.size()<5)t=" "+t;
		if(_CCC.TMP)ButtonDraw(_tc,"T:"+t,88,22,15);
		else ButtonDraw(_tc,"SETTEMP",88,22,7);
	}
	else if(x>=88&&x<=96&&(y>24&&y<28)){//excluder//programm override
		_tc.GotoXY(1,49);
		//_tc.Out<<((_CCC.PORD)?"PCEO off ":"PCEO on  ");
		_CCC.PORD = !_CCC.PORD;
		ButtonDraw(_tc,"PROGORD",88,25,((7+8*_CCC.PORD)));
		//ocr=!ocr;
		//ButtonDraw(_tc,"MakeOCR",88,25,7+8*ocr);
		//ButtonDraw(_cn,"  OCR  ",88,25,((7+8*ocr)&0)|4);
	}
	else if(x>=88&&x<=96&&(y>29&&y<33)){//S2
		OPENFILENAME ofn;       // common dialog box structure
        char szFile[1000];       // buffer for file name
        HWND hwnd;              // owner window
        HANDLE hf;              // file handle

        // Initialize OPENFILENAME
        ZeroMemory(&ofn, sizeof(ofn));
        ofn.lStructSize = sizeof(ofn);
        ofn.hwndOwner = NULL;
        ofn.lpstrFile = szFile;
        // Set lpstrFile[0] to '\0' so that GetOpenFileName does not 
        // use the contents of szFile to initialize itself.
        ofn.lpstrFile[0] = '\0';
        ofn.nMaxFile = sizeof(szFile);
        ofn.lpstrFilter = "MIDI Files(*.mid)\0*.mid\0";
        ofn.nFilterIndex = 1;
        ofn.lpstrFileTitle = NULL;
        ofn.nMaxFileTitle = 0;
        ofn.lpstrInitialDir = NULL;
        ofn.Flags = OFN_PATHMUSTEXIST;

        // Display the Open dialog box.
        if ( GetOpenFileName(&ofn)==TRUE ) {
        	string q="";
        	for(int i=0;i<1000&&szFile[i]!='\0';i++){
        		q.push_back(szFile[i]);
			}
        	_CCC.ouf="\"S2:"+q+"\"";
			_tc.GotoXY(1,49);
			_tc.Out<<_CCC.ouf;
		}
	}
	else if(x>=88&&x<=96&&(y>32&&y<36)){//start
		string FOUT="",paramsout="";
		if(_CCC.REM)FOUT=FOUT+"\\r ";
		if(_CCC.CON)FOUT=FOUT+"\\c ";
		if(_CCC.IGN)FOUT=FOUT+"\\i ";
		if(_CCC.PORD)FOUT=FOUT+"\\p ";
		if(_CCC.EMPTTRCKREM)FOUT=FOUT+"\\m ";
		//if(ocr)FOUT=FOUT+"\\o ";//disabled
		if(_CCC.TMP){
			FOUT=FOUT+"\\t";
			int q=_CCC.TMP;
			string t="";
			while(q){
				t="0"+t;
				t[0]+=q%10;
				q/=10;
			}
			FOUT=FOUT+t+" ";
		}
		if(_CCC.FOR){
			FOUT=FOUT+"\\f";
			int q=_CCC.FOR;
			string t="";
			while(q){
				t="0"+t;
				t[0]+=q%10;
				q/=10;
			}
			FOUT=FOUT+t+" ";
		}
		paramsout=FOUT;
		if(_CCC.ouf.size()){
			FOUT=FOUT+_CCC.ouf+" ";
		}
		if(_CCC.inf.size()){
			for(int i=0;i<_CCC.inf.size();i++){
				FOUT=FOUT+"\""+_CCC.inf[i]+"\" ";
			}
			_tc.GotoXY(1,49);
			_tc.Out<<"PARAMS: "<<paramsout;
			if(/*!is64bit() ||*/ 1){
				int abc = static_cast<int>(reinterpret_cast<uintptr_t>(ShellExecuteA(NULL,"open","SAFC.exe",FOUT.c_str(),wayto.c_str(),SW_SHOWNORMAL)));
				if(abc==0)CreateMess("0","SAFC opening error. (ShellExecuteA)");
				else if(abc==ERROR_FILE_NOT_FOUND)CreateMess("ERROR_FILE_NOT_FOUND","SAFC opening error. (ShellExecuteA)");
				else if(abc==ERROR_PATH_NOT_FOUND)CreateMess("ERROR_PATH_NOT_FOUND","SAFC opening error. (ShellExecuteA)");
				else if(abc==ERROR_BAD_FORMAT)CreateMess("ERROR_BAD_FORMAT","SAFC opening error. (ShellExecuteA)");
				else if(abc==SE_ERR_ACCESSDENIED)CreateMess("SE_ERR_ACCESSDENIED","SAFC opening error. (ShellExecuteA)");
				else if(abc==SE_ERR_ASSOCINCOMPLETE)CreateMess("SE_ERR_ASSOCINCOMPLETE","SAFC opening error. (ShellExecuteA)");
				else if(abc==SE_ERR_DDEBUSY)CreateMess("SE_ERR_DDEBUSY","SAFC opening error. (ShellExecuteA)");
				else if(abc==SE_ERR_DDEFAIL)CreateMess("SE_ERR_DDEFAIL","SAFC opening error. (ShellExecuteA)");
				else if(abc==SE_ERR_DDETIMEOUT)CreateMess("SE_ERR_DDETIMEOUT","SAFC opening error. (ShellExecuteA)");
				else if(abc==SE_ERR_DLLNOTFOUND)CreateMess("SE_ERR_DLLNOTFOUND","SAFC opening error. (ShellExecuteA)");
				else if(abc==SE_ERR_FNF)CreateMess("SE_ERR_FNF","SAFC opening error. (ShellExecuteA)");
				else if(abc==SE_ERR_NOASSOC)CreateMess("SE_ERR_NOASSOC","SAFC opening error. (ShellExecuteA)");
				else if(abc==SE_ERR_OOM)CreateMess("SE_ERR_OOM","SAFC opening error. (ShellExecuteA)");
				else if(abc==SE_ERR_PNF)CreateMess("SE_ERR_PNF","SAFC opening error. (ShellExecuteA)");
				else if(abc==SE_ERR_SHARE)CreateMess("SE_ERR_SHARE","SAFC opening error. (ShellExecuteA)");
			}
			else{
				//HINSTANCE abc64 = ShellExecuteA(NULL,"open","SAFC64.exe",FOUT.c_str(),wayto.c_str(),SW_SHOWNORMAL);
			}
		}
	}
	else if(x>=86&&x<=92&&(y>45&&y<49)){
		string  ADD="Click to select file and add it to list",
				RMV="Removes selected file from list",
				PPQ1="Forces PPQ to be equal some numer",
				PPQ2="Click on that to enter value and press enter",
				OFF1="Adds offset (relative to offsetless midis) to selected midi",
				OFF2="Type here number of ticks!",
				OFF3="TIP: If you will type in 0 in any input field, it will set value back to default",
				LOG="Enables logging in SAFC.exe (which is slow)",
				TMP1="Overrides all tempo events to specified one",
				TMP2="Gets less accurate on high numbers",
				IGN="Enables removing \"empty\" tracks",
				REM="Remnants of merging (*.mid_.mid) will be removed if checked",
				SA2="Opens saving dialog",
				STA="Starts SAFC.exe",
				UPD="Recreates interface",
				HEL="Shows help for 10(!) seconds",
				OCR="Overrides program (instrument) change events to Piano",
				FLD1="That is list for added midi files",
				FLD2="Click on it, to select file";
		TConsole _t;
		_t.TextBackground(0);
		_t.TextColor(3);
		_t.GotoXY(87-ADD.size(),2);
		_t.Out<<ADD;
		_t.GotoXY(87-RMV.size(),5);
		_t.Out<<RMV;
		_t.GotoXY(86-PPQ1.size(),7);
		_t.Out<<PPQ1;
		_t.GotoXY(86-PPQ2.size(),8);
		_t.Out<<PPQ2;
		_t.GotoXY(86-OFF1.size(),10);
		_t.Out<<OFF1;
		_t.GotoXY(86-OFF2.size(),11);
		_t.Out<<OFF2;
		_t.GotoXY(86-OFF3.size(),12);
		_t.Out<<OFF3;
		_t.GotoXY(87-LOG.size(),14);
		_t.Out<<LOG;
		_t.GotoXY(87-IGN.size(),17);
		_t.Out<<IGN;
		_t.GotoXY(87-REM.size(),20);
		_t.Out<<REM;
		_t.GotoXY(87-TMP1.size(),22);
		_t.Out<<TMP1;
		_t.GotoXY(87-TMP2.size(),23);
		_t.Out<<TMP2;
		_t.GotoXY(87-OCR.size(),26);
		_t.Out<<OCR;
		_t.GotoXY(87-SA2.size(),31);
		_t.Out<<SA2;
		_t.GotoXY(87-STA.size(),34);
		_t.Out<<STA;
		_t.GotoXY(98-UPD.size(),44);
		_t.Out<<UPD;
		_t.GotoXY(85-HEL.size(),46);
		_t.Out<<HEL;
		_t.GotoXY(1,1);
		_t.Out<<FLD1;
		_t.GotoXY(1,3);
		_t.Out<<FLD2;
		_t.TextColor(15);
		Sleep(10000);
		system("cls");
		UIDraw();
	}
	else if(x>=94&&x<=98&&(y>45&&y<49)){
		system("cls");
		UIDraw();
	}
}
void EvalMove(int _pos){
	int x=_pos>>8,y=_pos&0xFF,px=_ppos>>8,py=_ppos&0xFF;
	TConsole _tc;//IO.GotoXY(0,__MP.cols.size()+5);IO.Out<<x<<":"<<y<<"     ";
	//_tc.GotoXY(0,49);_tc.Out<<x<<":"<<y<<"     ";
	if(x>0&&x<85){//list move
		if(y<=_CCC.inf.size()&&y>0 &&y<48){
			if(y!=SEL){
				_tc.TextColor(15);
				_tc.TextBackground(3);
			}
			else{
				_tc.TextColor(0);
				_tc.TextBackground(11);
			}
			_tc.GotoXY(1,y);
        	for(int j=0;j<84&&j<_CCC.inf[y-1].size();j++)_tc.Out<<_CCC.inf[y-1][j];
			for(int j=_CCC.inf[y-1].size();j<84;j++)_tc.Out<<" ";
			if(_CCC.inf[y-1].size()>=85)_tc.Out<<(char)0xDB;
		}
		if(py>0&&py!=y&&py<=_CCC.inf.size() && py<48){
			_tc.GotoXY(1,py);
			if(py!=SEL){
				_tc.TextColor(15);
				_tc.TextBackground(0);
			}
			else{
				_tc.TextColor(0);
				_tc.TextBackground(15);
			}
        	for(int j=0;j<84&&j<_CCC.inf[py-1].size();j++)_tc.Out<<_CCC.inf[py-1][j];
			for(int j=_CCC.inf[py-1].size();j<84;j++)_tc.Out<<" ";
			if(_CCC.inf[py-1].size()>=85)_tc.Out<<(char)0xDB;
		}
		_tc.TextColor(15);
		_tc.TextBackground(0);
	}
	else if(px>0&&px<85){
		if(py>0&&py<=_CCC.inf.size() && py<48){
			_tc.GotoXY(1,py);
			if(py!=SEL){
				_tc.TextColor(15);
				_tc.TextBackground(0);
			}
			else{
				_tc.TextColor(0);
				_tc.TextBackground(15);
			}
        	for(int j=0;j<84&&j<_CCC.inf[py-1].size();j++)_tc.Out<<_CCC.inf[py-1][j];
			for(int j=_CCC.inf[py-1].size();j<84;j++)_tc.Out<<" ";
			if(_CCC.inf[py-1].size()>=85)_tc.Out<<(char)0xDB;
		}
		_tc.TextColor(15);
		_tc.TextBackground(0);
	}
	_ppos=_pos;
}
void MBUTTAWAITER(){
	HANDLE hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    DWORD Mode;
    GetConsoleMode(hStdInput, &Mode);
    SetConsoleMode(hStdInput, (Mode & ~ENABLE_QUICK_EDIT_MODE) | ENABLE_MOUSE_INPUT);
    for(;1;){
    	hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    	WaitForSingleObject(hStdInput, 2000000000);
	    INPUT_RECORD InRec;
	    DWORD NumEvents;
	    //cout<<rand()%10;
	    BOOL b = ReadConsoleInputW(hStdInput, &InRec, 1, &NumEvents);
	    if ( (0 != NumEvents) && (MOUSE_EVENT == InRec.EventType) ){
	        DWORD BtnState = InRec.Event.MouseEvent.dwButtonState;
	        int pos;
	        if (BtnState & FROM_LEFT_1ST_BUTTON_PRESSED){
	            pos = Charlink(GetCCC());
	            EvalClickPos(pos);
	            pos=0;
	        }
			else if(InRec.Event.MouseEvent.dwEventFlags & MOUSE_MOVED){
				pos = Charlink(GetCCC());
				EvalMove(pos);
				pos=0;
			}
	    }
	}
	MBUTTAWAITER();
}
void UIDraw(){
	TConsole _cn;
	int PPQF=_CCC.FOR,tmpot=_CCC.TMP;string PPQNo="",tmpo="";
	while(PPQF){
		PPQNo="0"+PPQNo;
		PPQNo[0]+=(PPQF%10);
		PPQF/=10;
	}
	while(tmpot){
		tmpo="0"+tmpo;
		tmpo[0]+=tmpot%10;
		tmpot/=10;
	}
	while(PPQNo.size()<5)PPQNo=" "+PPQNo;
	while(tmpo.size()<5)tmpo=" "+tmpo;
	for(int j=0;j<49;j++){
		for(int i=0;i<100;i++){//because i want so
			if(i==85 && j==0)_cn.Out<<(char)0xD1;
			else if(i==85 && j==48)_cn.Out<<(char)0xCF;
			else if(i==85)_cn.Out<<(char)0xB3;
			else if((i==0||i==99)&&(j==0|j==48))_cn.Out<<(char)0xCE;
			else if(j==0||j==48)_cn.Out<<(char)0xCD;
			else if(i>0&&i<99)_cn.Out<<' ';
			else _cn.Out<<(char)0xBA;
			if(i==99)_cn.Out<<endl;
		}
	}
	ButtonDraw(_cn,"  ADD  ",88,1,10);
	ButtonDraw(_cn,"  REM  ",88,4,12);
	if(!_CCC.FOR)ButtonDraw(_cn,"PPQ FORCE",87,7,7);
	else ButtonDraw(_cn,"PPQ:"+PPQNo,87,7,15);
	ButtonDraw(_cn,"SetOffset",87,10,14);
	ButtonDraw(_cn,"  LOG  ",88,13,6 + 8*_CCC.CON);
	ButtonDraw(_cn,"ETR REM",88,16,5 + 8*_CCC.EMPTTRCKREM);
	ButtonDraw(_cn,"REMNANT",88,19,3 + 8*_CCC.REM);
	if(!_CCC.TMP)ButtonDraw(_cn,"SETTEMP",88,22,7);
	else ButtonDraw(_cn,"T:"+tmpo,88,22,15);
	//ButtonDraw(_cn,"  OCR  ",88,25,((7+8*ocr)&0)|4);  _CCC.PORD
	ButtonDraw(_cn,"PROGORD",88,25,((7+8*_CCC.PORD)));
	ButtonDraw(_cn,"SAVE TO",88,30,15);
	ButtonDraw(_cn," START ",88,33,10);
	ButtonDraw(_cn,"HELP?",86,45,15);
	ButtonDraw(_cn,"UPD",94,45,9);
	FSWt();
	if(FStart){
		MBUTTAWAITER();
		FStart=0;
	}
}
int main(int argc, char** argv){
	SetConsoleOutputCP(866);
	cout<<"IBM DOS CodePage\n";
	wayto=argv[0];
	int pos=wayto.rfind('\\');
	if(pos>0)wayto.erase(wayto.begin()+pos,wayto.end());
	cout<<wayto;
	cout<<(is64bit()?"64bit Detected":"32bit Detected")<<endl;
	system("color 0F");
	_CCC._();//because fuck you
	SetWindowLong(__CNSL, GWL_STYLE, GetWindowLong(__CNSL, GWL_STYLE) &(~WS_MAXIMIZEBOX) &(~WS_THICKFRAME)/* & (~ WS_CAPTION)*/);// GetWindowLong(__CNSL, GWL_STYLE) & (~ WS_CAPTION) &&
	HANDLE hConsole = GetStdHandle( STD_OUTPUT_HANDLE );
    CONSOLE_FONT_INFOEX fontInfo;
    fontInfo.cbSize = sizeof( fontInfo );
    GetCurrentConsoleFontEx( hConsole, TRUE, &fontInfo );
    wcscpy( fontInfo.FaceName, L"Terminal" );
    fontInfo.dwFontSize.Y = 8;
    fontInfo.dwFontSize.X = 6;
    SetCurrentConsoleFontEx( hConsole, TRUE, &fontInfo );
    fontInfo.dwFontSize.Y = _Fy;
    fontInfo.dwFontSize.X = _Fx;
    SetCurrentConsoleFontEx( hConsole, TRUE, &fontInfo );
	system("mode CON: COLS=101 LINES=51");
    
    GetCurrentConsoleFontEx( hConsole, TRUE, &fontInfo );//I'll call it Back-Check fix
    _Fy = fontInfo.dwFontSize.Y;
    _Fx = fontInfo.dwFontSize.X;
    
    UIDraw();
}
