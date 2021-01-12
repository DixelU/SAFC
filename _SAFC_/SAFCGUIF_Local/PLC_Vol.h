#pragma once
#ifndef SAFGUIF_L_PLCV
#define SAFGUIF_L_PLCV

#include "../SAFGUIF/SAFGUIF.h"
#include "../SAFC_InnerModules/SAFC_IM.h"

struct PLC_VolumeWorker :HandleableUIPart {
	PLC<BYTE, BYTE>* PLC_bb;
	std::pair<BYTE, BYTE> _HoveredPoint;
	SingleTextLine* STL_MSG;
	float CXPos, CYPos, HorizontalSidesSize, VerticalSidesSize;
	float MouseX, MouseY, _xsqsz, _ysqsz;
	BIT Hovered, ActiveSetting, RePutMode/*JustPut=0*/;
	BYTE XCP, YCP;
	BYTE FPX, FPY;
	~PLC_VolumeWorker() override {
		delete STL_MSG;
	}
	PLC_VolumeWorker(float CXPos, float CYPos, float Width, float Height, PLC<BYTE, BYTE>* PLC_bb = NULL) {
		this->PLC_bb = PLC_bb;
		this->CXPos = CXPos;
		this->CYPos = CYPos;
		this->_xsqsz = Width / 256;
		this->_ysqsz = Height / 256;
		this->HorizontalSidesSize = Width;
		this->VerticalSidesSize = Height;

		this->STL_MSG = new SingleTextLine("_", CXPos, CYPos, System_White->XUnitSize, System_White->YUnitSize, System_White->SpaceWidth, 2, 0xFFAFFFCF, new DWORD(0xAFFFAFCF), (7 << 4) | 3);
		this->MouseX = this->MouseY = 0.f;
		this->_HoveredPoint = std::pair<BYTE, BYTE>(0, 0);
		ActiveSetting = Hovered = FPX = FPY = XCP = YCP = 0;
	}
	void Draw() override {
		Lock.lock();
		float begx = CXPos - 0.5f * HorizontalSidesSize, begy = CYPos - 0.5f * VerticalSidesSize;
		glColor4f(1, 1, 1, (Hovered) ? 0.85f : 0.5f);
		glLineWidth(1);
		glBegin(GL_LINE_LOOP);
		glVertex2f(begx, begy);
		glVertex2f(begx, begy + VerticalSidesSize);
		glVertex2f(begx + HorizontalSidesSize, begy + VerticalSidesSize);
		glVertex2f(begx + HorizontalSidesSize, begy);
		glEnd();
		glColor4f(0.5, 1, 0.5, 0.05);//showing "safe" for volumes square 
		glBegin(GL_QUADS);
		glVertex2f(begx, begy);
		glVertex2f(begx, begy + 0.5f * VerticalSidesSize);
		glVertex2f(begx + 0.5f * HorizontalSidesSize, begy + 0.5f * VerticalSidesSize);
		glVertex2f(begx + 0.5f * HorizontalSidesSize, begy);
		glEnd();

		if (Hovered) {
			glColor4f(1, 1, 1, 0.25);
			glBegin(GL_LINES);
			glVertex2f(begx + _xsqsz * XCP, begy);
			glVertex2f(begx + _xsqsz * XCP, begy + VerticalSidesSize);
			glVertex2f(begx, begy + _ysqsz * YCP);
			glVertex2f(begx + HorizontalSidesSize, begy + _ysqsz * YCP);
			glVertex2f(begx + _xsqsz * (XCP + 1), begy);
			glVertex2f(begx + _xsqsz * (XCP + 1), begy + VerticalSidesSize);
			glVertex2f(begx, begy + _ysqsz * (YCP + 1));
			glVertex2f(begx + HorizontalSidesSize, begy + _ysqsz * (YCP + 1));
			glEnd();
		}
		if (PLC_bb && PLC_bb->ConversionMap.size()) {
			glLineWidth(3);
			glColor4f(1, 0.75f, 1, 0.5f);
			glBegin(GL_LINE_STRIP);
			glVertex2f(begx, begy + _ysqsz * (PLC_bb->AskForValue(0) + 0.5f));

			for (auto Y = PLC_bb->ConversionMap.begin(); Y != PLC_bb->ConversionMap.end(); Y++)
				glVertex2f(begx + (Y->first + 0.5f) * _xsqsz, begy + (Y->second + 0.5f) * _ysqsz);

			glVertex2f(begx + 255.5f * _xsqsz, begy + _ysqsz * (PLC_bb->AskForValue(255) + 0.5f));
			glEnd();
			glColor4f(1, 1, 1, 0.75f);
			glPointSize(5);
			glBegin(GL_POINTS);
			for (auto Y = PLC_bb->ConversionMap.begin(); Y != PLC_bb->ConversionMap.end(); Y++)
				glVertex2f(begx + (Y->first + 0.5f) * _xsqsz, begy + (Y->second + 0.5f) * _ysqsz);
			glEnd();
		}
		if (ActiveSetting) {
			std::map<BYTE, BYTE>::iterator Y;
			glBegin(GL_LINES);
			glVertex2f(begx + (FPX + 0.5f) * _xsqsz, begy + (FPY + 0.5f) * _ysqsz);
			glVertex2f(begx + (XCP + 0.5f) * _xsqsz, begy + (YCP + 0.5f) * _ysqsz);
			if (FPX < XCP) {
				Y = PLC_bb->ConversionMap.upper_bound(XCP);
				if (Y != PLC_bb->ConversionMap.end()) {
					glVertex2f(begx + (Y->first + 0.5f) * _xsqsz, begy + (Y->second + 0.5f) * _ysqsz);
					glVertex2f(begx + (XCP + 0.5f) * _xsqsz, begy + (YCP + 0.5f) * _ysqsz);
				}
				Y = PLC_bb->ConversionMap.lower_bound(FPX);
				if (Y != PLC_bb->ConversionMap.end() && Y != PLC_bb->ConversionMap.begin()) {
					Y--;
					glVertex2f(begx + (Y->first + 0.5f) * _xsqsz, begy + (Y->second + 0.5f) * _ysqsz);
					glVertex2f(begx + (FPX + 0.5f) * _xsqsz, begy + (FPY + 0.5f) * _ysqsz);
				}
			}
			else {
				Y = PLC_bb->ConversionMap.upper_bound(FPX);
				if (Y != PLC_bb->ConversionMap.end()) {
					glVertex2f(begx + (Y->first + 0.5f) * _xsqsz, begy + (Y->second + 0.5f) * _ysqsz);
					glVertex2f(begx + (FPX + 0.5f) * _xsqsz, begy + (FPY + 0.5f) * _ysqsz);
				}
				Y = PLC_bb->ConversionMap.lower_bound(XCP);
				if (Y != PLC_bb->ConversionMap.end() && Y != PLC_bb->ConversionMap.begin()) {
					Y--;
					glVertex2f(begx + (Y->first + 0.5f) * _xsqsz, begy + (Y->second + 0.5f) * _ysqsz);
					glVertex2f(begx + (XCP + 0.5f) * _xsqsz, begy + (YCP + 0.5f) * _ysqsz);
				}
			}
			glEnd();
		}
		STL_MSG->Draw();
		Lock.unlock();
	}
	void UpdateInfo() {
		Lock.lock();
		SHORT tx = (128. + floor(255 * (MouseX - CXPos) / HorizontalSidesSize)),
			ty = (128. + floor(255 * (MouseY - CYPos) / VerticalSidesSize));
		if (tx < 0 || tx>255 || ty < 0 || ty>255) {
			if (Hovered) {
				STL_MSG->SafeStringReplace("-:-");
				Hovered = 0;
			}
		}
		else {
			Hovered = 1;
			STL_MSG->SafeStringReplace(std::to_string(XCP = tx) + ":" + std::to_string(YCP = ty));
		}
		Lock.unlock();
	}
	void _MakeMapMoreSimple() {
		Lock.lock();
		if (PLC_bb) {
			BYTE TF, TS;
			for (auto Y = PLC_bb->ConversionMap.begin(); Y != PLC_bb->ConversionMap.end();) {
				TF = Y->first;
				TS = Y->second;
				Y = PLC_bb->ConversionMap.erase(Y);
				if (PLC_bb->AskForValue(TF) != TS) {
					PLC_bb->ConversionMap[TF] = TS;
				}
			}
		}
		Lock.unlock();
	}
	void SafeMove(float dx, float dy) override {
		Lock.lock();
		CXPos += dx;
		CYPos += dy;
		this->STL_MSG->SafeMove(dx, dy);
		Lock.unlock();
	}
	void SafeChangePosition(float NewX, float NewY) override {
		Lock.lock();
		NewX -= CXPos;
		NewY -= CYPos;
		SafeMove(NewX, NewY);
		Lock.unlock();
	}
	void SafeChangePosition_Argumented(BYTE Arg, float NewX, float NewY) override {
		Lock.lock();
		float CW = 0.5f * (
			(INT32)((BIT)(GLOBAL_LEFT & Arg))
			- (INT32)((BIT)(GLOBAL_RIGHT & Arg))
			) * HorizontalSidesSize,
			CH = 0.5f * (
				(INT32)((BIT)(GLOBAL_BOTTOM & Arg))
				- (INT32)((BIT)(GLOBAL_TOP & Arg))
				) * VerticalSidesSize;
		SafeChangePosition(NewX + CW, NewY + CH);
		Lock.unlock();
	}
	void KeyboardHandler(CHAR CH) override {
		///hmmmm ... should i... meh...
	}
	void SafeStringReplace(std::string Meaningless) override {
		/// ... ///
	}
	void RePutFromAtoB(BYTE A, BYTE B, BYTE ValA, BYTE ValB) {
		Lock.lock();
		if (PLC_bb) {
			if (B < A) {
				std::swap(A, B);
				std::swap(ValA, ValB);
			}
			auto itA = PLC_bb->ConversionMap.lower_bound(A), itB = PLC_bb->ConversionMap.upper_bound(B);
			while (itA != itB)
				itA = PLC_bb->ConversionMap.erase(itA);

			PLC_bb->InsertNewPoint(A, ValA);
			PLC_bb->InsertNewPoint(B, ValB);
		}
		Lock.unlock();
	}
	void JustPutNewValue(BYTE A, BYTE ValA) {
		Lock.lock();
		if (PLC_bb)
			PLC_bb->InsertNewPoint(A, ValA);
		Lock.unlock();
	}
	BIT MouseHandler(float mx, float my, CHAR Button, CHAR State) override {
		Lock.lock();
		this->MouseX = mx;
		this->MouseY = my;
		UpdateInfo();
		if (Hovered) {
			if (Button) {
				if (Button == -1) {
					if (this->ActiveSetting) {
						if (State == 1) {
							ActiveSetting = 0;
							RePutFromAtoB(FPX, XCP, FPY, YCP);
						}
					}
					else {
						if (this->RePutMode) {
							if (State == 1) {
								ActiveSetting = 1;
								FPX = XCP;
								FPY = YCP;
							}
						}
						else {
							if (State == 1) {
								JustPutNewValue(XCP, YCP);
							}
						}
					}
				}
				else {//Removing some point
					///i give up
				}
			}
			else {
				if (PLC_bb) {
					///no ne///here i should've implement approaching to point.... meh
				}
			}
		}
		Lock.unlock();
		return 0;
	}
};
#endif // !SAFGUIF_L_PLCV
