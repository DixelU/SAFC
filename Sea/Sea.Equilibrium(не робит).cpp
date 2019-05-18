#include "stdafx.h"
#include <ctime>
#include <random>
#include <cstdlib>
#include <vector>
#include <string>
#include <cmath>
#include <Windows.h>
#include <iostream>

#include "glut.h"

#define BYTE unsigned char
#define PI 3.14159265358979323846264338328
#define ANGLE 2.5
#define DLENARROW 0.9
#define RES 5
#define ANGTORAD(a) (0.0174532925*(a))
#define RADTOANG(a) (57.2957795785*(a))

#define RANDFLOAT(range)  ((0 - range) + ((float)rand()/((float)RAND_MAX/(2*range)))) // from -range to range
#define RANDSIGN ((rand()&0x100)?(1):(-1))

bool ANIMATION_IS_ACTIVE = 0, FIRSTBOOT = 1;
using namespace std;

default_random_engine ___eng;
uniform_int_distribution<int> ___dis(0, 32768);
#define DRANDFLOAT(range)  ( (0 - range) + ( (float)___dis(___eng) )/( 16384. / range ) )
#define SLOWDPROG(a,b,fract) ((a + (fract - 1)*b)/fract)

struct __FLOATINGSEMITRASNPARENTTHINGS{
	float _cx, _cy,_cdx,_cdy,_dang;
	float _rad, _ang;
	int _angleamount;
	__FLOATINGSEMITRASNPARENTTHINGS(float x=0, float y=0,float size=1,float angle=0,float sides=3) {
		_cx = x; _cy = y; _rad = size; _ang = angle; _angleamount = sides;
		_cdx = DRANDFLOAT(0.1);
		_cdy = DRANDFLOAT(0.1);
		_dang = DRANDFLOAT(15);
	}
	void Process(float dt=0.1) {
		_dang = SLOWDPROG(DRANDFLOAT(15), _dang, 64);
		_cdx = SLOWDPROG(DRANDFLOAT(0.1), _cdx, 64);
		_cdy = SLOWDPROG(DRANDFLOAT(0.1), _cdy, 64);
		_ang += _dang*dt;
		_cx += _cdx*dt;
		_cy += _cdy*dt;
		if (sqrt(_cx*_cx + _cy*_cy) > 1.5 * RES) {
			_cx = 0 - _cx;
			_cy = 0 - _cy;
		}
	}
	void Draw() const {
		glBegin(GL_POLYGON);
		for (float i = 0 + _ang; i < 360. + _ang; i += 360. / _angleamount)
			glVertex2f(_rad*cos(ANGTORAD(i + _ang)) + _cx, _rad*sin(ANGTORAD(i + _ang)) + _cy);
		glEnd();
	}
};

struct __FLOATDCONTAINER {
	float _x, _y;
	__FLOATDCONTAINER(float x = 0, float y = 0) { _x = x; _y = y; }
	void Set(float x, float y) { _x = x; _y = y; }
};
struct __WAVE{
	float _hgt;
	float _freq, _offset;
	float _y, _wdth;
	float _doffset, _dhgt;
	__WAVE(float height = 1,float Frequency = 1, float Offset = 0, float Yposition = 0, float WaveWidth = 5) {
		_freq = Frequency; _offset = Offset; _y = Yposition; _wdth = WaveWidth; _hgt = height;
		_doffset = DRANDFLOAT(1);
		_dhgt = DRANDFLOAT(0.5);
	}
	void Process(float dt=0.1) {
		_dhgt = SLOWDPROG(DRANDFLOAT(0.5), _dhgt, 64);
		_doffset = SLOWDPROG(DRANDFLOAT(1), _doffset, 64);
		_hgt += _dhgt*dt;
		_offset += _doffset*dt;
	}
	void Draw() const {
		glLineWidth(_wdth);
		glBegin(GL_LINE_STRIP);
		for (float i = -RES; i < RES; i += 0.01) {
			glVertex2f(i, _hgt*sin(_freq*i + _offset) + _y);
		}
		glEnd();
		glLineWidth(1);
	}
};

struct __DOTTEDSYMBOLS {
	float _x, _y, _xus, _yus;//center (5)
	string _renderway;
	__FLOATDCONTAINER _points[9];
	__DOTTEDSYMBOLS(string RenderWay = "", float x = 0, float y = 0, float xUnitSize = 0.15, float yUnitSize = 0.25) {
		_renderway = RenderWay; _x = x; _y = y; _xus = xUnitSize; _yus = yUnitSize;
		for (int i = -1; i <= 1; i++) {
			for (int j = -1; j <= 1; j++) {
				_points[(i + 1) * 3 + (j + 1)].Set(_x + _xus*(j), _y + _yus*(i));
			}
		}
	}
	void Draw() {
		glBegin(GL_LINE_STRIP);
		BYTE IO;
		for (int i = 0; i < _renderway.length(); i++) {
			if (_renderway[i] == ' ') {
				glEnd();
				glBegin(GL_LINE_STRIP);
				continue;
			}
			IO = _renderway[i] - '1';
			glVertex2f(_points[IO]._x, _points[IO]._y);
		}
		glEnd();
	}
};
__DOTTEDSYMBOLS *ds;
__DOTTEDSYMBOLS RD;
const string SYMBOLS[] = {
	"741236987",//0
	"485213",//1
	"789654123",//2
	"789656321",//3
	"745693",//4
	"987456321",//5
	"9874123654",//6
	"78952",//7
	"98745693214",//8
	"9874569321",//9
	"1478963 654",//A
	"98741235459",//B
	"9874123",//C
	"7412687",//D
	"987454123",//E
	"9874541",//F
	"987412365",//G
	"74145693",//H
	"7985213",//I
	"789621",//J
	"71459 53",//K
	"74123",//L
	"1475963",//M
	"1475369",//N
	"987412369",//O
	"14789654",//P
	"98741269 53",//Q
	"14789654 53",//R
	"987456321",//S
	"79852",//T
	"7412369",//U
	"74269",//V
	"7415369",//W
	"73519",//X
	"75259",//Y
	"7895123",//Z
};
__FLOATINGSEMITRASNPARENTTHINGS *fst;
__WAVE *waves;

void INIT() {
	ds = new __DOTTEDSYMBOLS[36];
	for (int i = 0; i < 36; i++) {
		ds[i] = __DOTTEDSYMBOLS(SYMBOLS[i], (float)(0 - RES + 0.5) + (2. * RES / 50 + 0.05)*i, 0, 0.1, 0.15);
	}
	RD = __DOTTEDSYMBOLS("", 0, -1, 0.2, 0.30);
	fst = new __FLOATINGSEMITRASNPARENTTHINGS[36];
	for (int i = 0; i < 36; i++) {
		fst[i] = __FLOATINGSEMITRASNPARENTTHINGS(DRANDFLOAT(RES), DRANDFLOAT(RES),
			DRANDFLOAT(1)*0.25, DRANDFLOAT(360), rand() % 7 + 3);
	}
	waves = new __WAVE[5];
	for (int i = 0; i < 5; i++) {
		waves[i] = __WAVE(DRANDFLOAT(1)*0.5, DRANDFLOAT(1)*0.5, DRANDFLOAT(PI), RANDFLOAT(RES-1), DRANDFLOAT(25) + 5);
	}
}
float abc = 0,aabc = 0;
void onTimer(int v);
void mDisplay() {
	glClear(GL_COLOR_BUFFER_BIT);
	if (FIRSTBOOT) {
		INIT();
		FIRSTBOOT = 0;
		ANIMATION_IS_ACTIVE = !ANIMATION_IS_ACTIVE;
		onTimer(0);
	}

	RD._renderway = SYMBOLS[rand() % 10];
	glLineWidth(3);
	glColor4f(1, 0.5, 0, 0.66);
	RD.Draw();

	glLineWidth(1);
	for (int i = 0; i < 36; i++) {
		if (i < 5) {
			glColor4f(0, 0.5, 1, 0.66);
			waves[i].Process();
			waves[i].Draw();
		}
		glColor4f(1, 1, 1, 0.333);
		fst[i].Process();
		fst[i].Draw();
		glColor4f(1, 1, 1, 1);
		ds[i].Draw(); 
	}
	glutSwapBuffers();
	cout << (abc = RANDFLOAT(0.5)) << " " << (aabc += abc*0.1) << endl;

	//glFlush();
}
void mInit() {
	glClearColor((clock()&0x1)?0.6:0, 0.1, 0.6, 1); 
	glMatrixMode(GL_PROJECTION);
	glPointSize(1);
	glLoadIdentity();
	gluOrtho2D(0 - RES, RES, 0 - RES, RES);
}

void onTimer(int v) {
	glutTimerFunc(33, onTimer, 0);
	if (ANIMATION_IS_ACTIVE) {
		mDisplay();
	}
}

void mKey(BYTE k, int x, int y) {
	std::cout << (int)k << '\n';
	if (k == '1') {
		if (FIRSTBOOT) { FIRSTBOOT = 0; onTimer(0); }
		ANIMATION_IS_ACTIVE = !ANIMATION_IS_ACTIVE;

	}
	else if (k == 27)exit(1);
}

int main(int argc, char ** argv) {
	srand(clock());
	glutInit(&argc, argv);
	glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA);
	glutInitWindowSize(800, 800);
	glutCreateWindow("OpenGL window");

	glBlendFunc(GL_SRC_ALPHA, GL_ONE);
	glEnable(GL_BLEND);

	glutKeyboardFunc(mKey);
	glutDisplayFunc(mDisplay);
	mInit();
	glutMainLoop();
	return 0;
}