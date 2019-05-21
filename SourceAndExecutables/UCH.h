#include<windows.h>
#include<iostream>
 
//�����
#define COLOR_BLACK 0           //������
#define COLOR_BLUE 1            //�����
#define COLOR_GREEN 2           //�������
#define COLOR_CYAN 3            //�������
#define COLOR_RED 4             //�������
#define COLOR_MAGENTA 5         //����������
#define COLOR_BROWN 6           //����������
#define COLOR_LIGHTGRAY 7       //������-�����
#define COLOR_DARKGRAY 8        //�����-�����
#define COLOR_LIGHTBLUE 9       //������-�����
#define COLOR_LIGHTGREEN 10     //������-�������
#define COLOR_LIGHTCYAN 11      //������-�������
#define COLOR_LIGHTRED 12       //������-�������
#define COLOR_LIGHTMAGENTA 13   //������-����������
#define COLOR_YELLOW 14         //������
#define COLOR_WHITE 15          //�����
 
 
//����� �������
class TConsole
{
private:
    HANDLE InputHandle;                         //���������� ������ �����
    HANDLE OutputHandle;                        //���������� ������ ������
    CONSOLE_SCREEN_BUFFER_INFO ScreenBufInfo;   //���������� �� �������� ������ �������
public:
    std::ostream& Out;              //����� ������
    std::istream& In;               //����� �����
    TConsole();                     //�����������
    void ClrEol();                  //������� �������� �������� �� ������� �� ����� ������
    void ClrScr();                  //������� ������� ������
    void Delay(WORD MS);            //������� �������� ����������
    void DelLine();                 //������� �������� ������
    void GotoXY(int X, int Y);      //������� ����������� �������
    void InsLine();                 //������� ���������� ������
    bool KeyPressed();              //������� �������� ������� �������
    WCHAR ReadKey();                //������� ������ ������� � ����������
    void TextBackground(BYTE Color);//������� ������� ����� ����
    void TextColor(BYTE Color);     //������� ������� ����� ������
    int WhereX();                   //������� ��������� ������� ���������� X �������
    int WhereY();                   //������� ��������� ������� ���������� Y �������
    void Window(BYTE X, BYTE Y);    //������� ������� �������� ����
};
 
 
//-----------------------------------------------------
//�����������
TConsole::TConsole():In(std::cin),Out(std::cout)
{
    InputHandle=GetStdHandle(STD_INPUT_HANDLE);
    OutputHandle=GetStdHandle(STD_OUTPUT_HANDLE);
    GetConsoleScreenBufferInfo(OutputHandle,&ScreenBufInfo);
}
 
//-----------------------------------------------------
//������� �������� �������� �� ������� �� ����� ������
void TConsole::ClrEol()
{
    GetConsoleScreenBufferInfo(OutputHandle,&ScreenBufInfo);
    LPDWORD NOAW;
    ZeroMemory(&NOAW,sizeof(NOAW));
    DWORD Count, Size;
    Size=ScreenBufInfo.srWindow.Right-ScreenBufInfo.dwCursorPosition.X;
    FillConsoleOutputAttribute(OutputHandle,ScreenBufInfo.wAttributes,Size,ScreenBufInfo.dwCursorPosition,&Count);
    FillConsoleOutputCharacter(OutputHandle,' ',
        ScreenBufInfo.dwSize.X-ScreenBufInfo.dwCursorPosition.X,
        ScreenBufInfo.dwCursorPosition,NOAW);
}
 
//-----------------------------------------------------
//������� ������� ������
void TConsole::ClrScr()
{
    LPDWORD NOAW;
    ZeroMemory(&NOAW,sizeof(NOAW));
    ScreenBufInfo.dwCursorPosition.X=0;
    ScreenBufInfo.dwCursorPosition.Y=0;
    FillConsoleOutputCharacter(OutputHandle,' ',ScreenBufInfo.dwSize.X*ScreenBufInfo.dwSize.Y,
        ScreenBufInfo.dwCursorPosition,NOAW);
    GotoXY(0,0);
}
 
//-----------------------------------------------------
//������� �������� ����������
void TConsole::Delay(WORD MS)
{
    SleepEx(MS,FALSE);
}
 
//-----------------------------------------------------
//������� �������� ������
void TConsole::DelLine()
{
    GetConsoleScreenBufferInfo(OutputHandle,&ScreenBufInfo);
    CHAR_INFO CI;
    ScreenBufInfo.srWindow.Top=WhereY();
    CI.Char.UnicodeChar=' ';
    CI.Attributes=ScreenBufInfo.wAttributes;
    COORD Coord;
    Coord.X=ScreenBufInfo.srWindow.Left;
    Coord.Y=ScreenBufInfo.srWindow.Top-1;
    DWORD Size,Count;
    Size=ScreenBufInfo.srWindow.Right-ScreenBufInfo.srWindow.Left+1;
    ScrollConsoleScreenBuffer(OutputHandle,&ScreenBufInfo.srWindow,0,Coord,&CI);
    FillConsoleOutputAttribute(OutputHandle,ScreenBufInfo.wAttributes,Size,Coord,&Count);
}
 
//-----------------------------------------------------
//������� ����������� �������
void TConsole::GotoXY(int X, int Y)
{
    ScreenBufInfo.dwCursorPosition.X=X;
    ScreenBufInfo.dwCursorPosition.Y=Y;
    if(!SetConsoleCursorPosition(OutputHandle,ScreenBufInfo.dwCursorPosition)) 
    {
        GotoXY(0,0);
        DelLine();
    }
}
 
//-----------------------------------------------------
//������� ���������� ������
void TConsole::InsLine()
{
    GetConsoleScreenBufferInfo(OutputHandle,&ScreenBufInfo);
    SMALL_RECT ScreenRect=ScreenBufInfo.srWindow;
    ScreenRect.Top=ScreenBufInfo.dwCursorPosition.Y-1+ScreenBufInfo.srWindow.Top;
    ScreenRect.Bottom=ScreenBufInfo.srWindow.Bottom-1;
    CHAR_INFO CI;
    CI.Char.UnicodeChar=' ';
    CI.Attributes=ScreenBufInfo.wAttributes;
    COORD Coord;
    Coord.X=ScreenRect.Left;
    Coord.Y=ScreenRect.Top+1;
    DWORD dwSize=ScreenRect.Right-ScreenRect.Left+1;
    ScrollConsoleScreenBuffer(OutputHandle,&ScreenRect,0,Coord,&CI);
    Coord.Y--;
    DWORD dwCount;
    FillConsoleOutputAttribute(OutputHandle,ScreenBufInfo.wAttributes,dwSize,Coord,&dwCount);
}
 
//-----------------------------------------------------
//������� �������� ������� �������
bool TConsole::KeyPressed()
{
    bool flag=false;
    DWORD NumberOfEvents;
    GetNumberOfConsoleInputEvents(InputHandle,&NumberOfEvents);
    if(NumberOfEvents>0)
    {
        INPUT_RECORD IR;
        DWORD NumRead;
        if(PeekConsoleInput(InputHandle,&IR,1,&NumRead))
        {
            if((IR.EventType==KEY_EVENT)&&(IR.Event.KeyEvent.bKeyDown)) flag=true;
            else ReadConsoleInput(InputHandle,&IR,1,&NumRead);
        }
    }
    return flag;
}
 
//-----------------------------------------------------
//������� ������ ������� � ����������
WCHAR TConsole::ReadKey()
{
    DWORD NumRead;
    INPUT_RECORD IR;
    do
    {
        do
        {
        }
        while(!KeyPressed());
        ReadConsoleInput(InputHandle,&IR,1,&NumRead);
    }
    while(IR.Event.KeyEvent.uChar.UnicodeChar==0);
    return IR.Event.KeyEvent.uChar.UnicodeChar;
}
 
//-----------------------------------------------------
//������� ������� ����� ����
void TConsole::TextBackground(BYTE Color)
{
    GetConsoleScreenBufferInfo(OutputHandle,&ScreenBufInfo);
    WORD TextAttr=ScreenBufInfo.wAttributes;
    TextAttr=(Color<<4)|(TextAttr & 0x0F);
    SetConsoleTextAttribute(OutputHandle,TextAttr);
}
 
//-----------------------------------------------------
//������� ������� ����� ������
void TConsole::TextColor(BYTE Color)
{
    GetConsoleScreenBufferInfo(OutputHandle,&ScreenBufInfo);
    WORD TextAttr=ScreenBufInfo.wAttributes;
    TextAttr=(Color & 0x0F)|(TextAttr & 0xF0);
    SetConsoleTextAttribute(OutputHandle,TextAttr);
}
 
//-----------------------------------------------------
//������� ��������� ������� ���������� X �������
int TConsole::WhereX()
{
    GetConsoleScreenBufferInfo(OutputHandle,&ScreenBufInfo);
    return ScreenBufInfo.dwCursorPosition.X;
}
 
//-----------------------------------------------------
//������� ��������� ������� ���������� Y �������
int TConsole::WhereY()
{
    GetConsoleScreenBufferInfo(OutputHandle,&ScreenBufInfo);
    return ScreenBufInfo.dwCursorPosition.Y;
}
 
//-----------------------------------------------------
//������� ������� �������� ����
void TConsole::Window(BYTE X, BYTE Y)
{
    SMALL_RECT ConsoleRect;
    ConsoleRect.Top=0;
    ConsoleRect.Left=0;
    ConsoleRect.Right=X-1;
    ConsoleRect.Bottom=Y-1;
    COORD BufSize;
    BufSize.X=X;
    BufSize.Y=Y;
    SetConsoleScreenBufferSize(OutputHandle,BufSize);
    SetConsoleWindowInfo(OutputHandle,TRUE,&ConsoleRect);   
    GotoXY(0,0);
}
