#pragma once
#ifndef SAFGUIF_L_PLCV
#define SAFGUIF_L_PLCV

#include "../SAFGUIF/SAFGUIF.h"
// #include "../SAFC_InnerModules/include_all.h"

struct PLC_VolumeWorker : HandleableUIPart
{
	std::shared_ptr<polyline_converter<std::uint8_t, std::uint8_t>> PLC_bb;
	std::pair<std::uint8_t, std::uint8_t> _HoveredPoint;
	SingleTextLine* STL_MSG;
	float CXPos, CYPos, HorizontalSidesSize, VerticalSidesSize;
	float MouseX, MouseY, _xsqsz, _ysqsz;
	bool Hovered, ActiveSetting, RePutMode/*JustPut=0*/;
	std::uint8_t XCP, YCP;
	std::uint8_t FPX, FPY;

	~PLC_VolumeWorker() override
	{
		delete STL_MSG;
	}
	PLC_VolumeWorker(float CXPos, float CYPos, float Width, float Height, std::shared_ptr<polyline_converter<std::uint8_t, std::uint8_t>> PLC_bb = nullptr)
	{
		this->PLC_bb = std::move(PLC_bb);
		this->CXPos = CXPos;
		this->CYPos = CYPos;
		this->_xsqsz = Width / 256;
		this->_ysqsz = Height / 256;
		this->HorizontalSidesSize = Width;
		this->VerticalSidesSize = Height;
		this->RePutMode = false;
		this->STL_MSG = new SingleTextLine("_", CXPos, CYPos, System_White->XUnitSize, System_White->YUnitSize, System_White->SpaceWidth, 2, 0xFFAFFFCF, new std::uint32_t{0xAFFFAFCF}, (7 << 4) | 3);
		this->MouseX = this->MouseY = 0.f;
		this->_HoveredPoint = std::pair<std::uint8_t, std::uint8_t>(0, 0);
		ActiveSetting = Hovered = FPX = FPY = XCP = YCP = 0;
	}
	void Draw() override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
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

		if (Hovered)
		{
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
		if (PLC_bb && PLC_bb->map.size())
		{
			glLineWidth(3);
			glColor4f(1, 0.75f, 1, 0.5f);
			glBegin(GL_LINE_STRIP);
			glVertex2f(begx, begy + _ysqsz * (PLC_bb->at(0) + 0.5f));

			for (auto Y = PLC_bb->map.begin(); Y != PLC_bb->map.end(); Y++)
				glVertex2f(begx + (Y->first + 0.5f) * _xsqsz, begy + (Y->second + 0.5f) * _ysqsz);

			glVertex2f(begx + 255.5f * _xsqsz, begy + _ysqsz * (PLC_bb->at(255) + 0.5f));
			glEnd();
			glColor4f(1, 1, 1, 0.75f);
			glPointSize(5);
			glBegin(GL_POINTS);
			for (auto Y = PLC_bb->map.begin(); Y != PLC_bb->map.end(); Y++)
				glVertex2f(begx + (Y->first + 0.5f) * _xsqsz, begy + (Y->second + 0.5f) * _ysqsz);
			glEnd();
		}
		if (ActiveSetting)
		{
			std::map<std::uint8_t, std::uint8_t>::iterator Y;
			glBegin(GL_LINES);
			glVertex2f(begx + (FPX + 0.5f) * _xsqsz, begy + (FPY + 0.5f) * _ysqsz);
			glVertex2f(begx + (XCP + 0.5f) * _xsqsz, begy + (YCP + 0.5f) * _ysqsz);
			if (FPX < XCP)
			{
				Y = PLC_bb->map.upper_bound(XCP);
				if (Y != PLC_bb->map.end())
				{
					glVertex2f(begx + (Y->first + 0.5f) * _xsqsz, begy + (Y->second + 0.5f) * _ysqsz);
					glVertex2f(begx + (XCP + 0.5f) * _xsqsz, begy + (YCP + 0.5f) * _ysqsz);
				}
				Y = PLC_bb->map.lower_bound(FPX);
				if (Y != PLC_bb->map.end() && Y != PLC_bb->map.begin())
				{
					--Y;
					glVertex2f(begx + (Y->first + 0.5f) * _xsqsz, begy + (Y->second + 0.5f) * _ysqsz);
					glVertex2f(begx + (FPX + 0.5f) * _xsqsz, begy + (FPY + 0.5f) * _ysqsz);
				}
			}
			else {
				Y = PLC_bb->map.upper_bound(FPX);
				if (Y != PLC_bb->map.end())
				{
					glVertex2f(begx + (Y->first + 0.5f) * _xsqsz, begy + (Y->second + 0.5f) * _ysqsz);
					glVertex2f(begx + (FPX + 0.5f) * _xsqsz, begy + (FPY + 0.5f) * _ysqsz);
				}
				Y = PLC_bb->map.lower_bound(XCP);
				if (Y != PLC_bb->map.end() && Y != PLC_bb->map.begin())
				{
					--Y;
					glVertex2f(begx + (Y->first + 0.5f) * _xsqsz, begy + (Y->second + 0.5f) * _ysqsz);
					glVertex2f(begx + (XCP + 0.5f) * _xsqsz, begy + (YCP + 0.5f) * _ysqsz);
				}
			}
			glEnd();
		}
		STL_MSG->Draw();
	}
	void UpdateInfo()
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		std::int16_t tx = (128. + floor(255 * (MouseX - CXPos) / HorizontalSidesSize)),
			ty = (128. + floor(255 * (MouseY - CYPos) / VerticalSidesSize));
		if (tx < 0 || tx>255 || ty < 0 || ty>255)
		{
			if (Hovered)
			{
				STL_MSG->SafeStringReplace("-:-");
				Hovered = 0;
			}
		}
		else
		{
			Hovered = 1;
			STL_MSG->SafeStringReplace(std::to_string(XCP = tx) + ":" + std::to_string(YCP = ty));
		}
	}
	void _MakeMapMoreSimple() 
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		if (PLC_bb)
		{
			std::uint8_t TF, TS;
			for (auto Y = PLC_bb->map.begin(); Y != PLC_bb->map.end();)
			{
				TF = Y->first;
				TS = Y->second;
				Y = PLC_bb->map.erase(Y);
				if (PLC_bb->at(TF) != TS) 
					PLC_bb->map[TF] = TS;
			}
		}
	}
	void SafeMove(float dx, float dy) override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		CXPos += dx;
		CYPos += dy;
		this->STL_MSG->SafeMove(dx, dy);
	}
	void SafeChangePosition(float NewX, float NewY) override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		NewX -= CXPos;
		NewY -= CYPos;
		SafeMove(NewX, NewY);
	}
	void SafeChangePosition_Argumented(std::uint8_t Arg, float NewX, float NewY) override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		float CW = 0.5f * (
			(std::int32_t)((bool)(GLOBAL_LEFT & Arg))
			- (std::int32_t)((bool)(GLOBAL_RIGHT & Arg))
			) * HorizontalSidesSize,
			CH = 0.5f * (
				(std::int32_t)((bool)(GLOBAL_BOTTOM & Arg))
				- (std::int32_t)((bool)(GLOBAL_TOP & Arg))
				) * VerticalSidesSize;
		SafeChangePosition(NewX + CW, NewY + CH);
	}
	void KeyboardHandler(CHAR CH) override
	{
	}
	void SafeStringReplace(std::string Meaningless) override
	{
	}
	void RePutFromAtoB(std::uint8_t A, std::uint8_t B, std::uint8_t ValA, std::uint8_t ValB)
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		if (PLC_bb)
		{
			if (B < A) 
			{
				std::swap(A, B);
				std::swap(ValA, ValB);
			}
			auto itA = PLC_bb->map.lower_bound(A), itB = PLC_bb->map.upper_bound(B);
			while (itA != itB)
				itA = PLC_bb->map.erase(itA);

			PLC_bb->insert(A, ValA);
			PLC_bb->insert(B, ValB);
		}
	}
	void JustPutNewValue(std::uint8_t A, std::uint8_t ValA)
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		if (PLC_bb)
			PLC_bb->insert(A, ValA);
	}
	bool MouseHandler(float mx, float my, CHAR Button, CHAR State) override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		this->MouseX = mx;
		this->MouseY = my;
		UpdateInfo();
		if (Hovered)
		{
			if (Button)
			{
				if (Button == -1)
				{
					if (this->ActiveSetting)
					{
						if (State == 1)
						{
							ActiveSetting = 0;
							RePutFromAtoB(FPX, XCP, FPY, YCP);
						}
					}
					else
					{
						if (this->RePutMode)
						{
							if (State == 1)
							{
								ActiveSetting = 1;
								FPX = XCP;
								FPY = YCP;
							}
						}
						else
						{
							if (State == 1)
							{
								JustPutNewValue(XCP, YCP);
							}
						}
					}
				}
				else
				{
					//Removing some point
					///i give up
				}
			}
			else
			{
				if (PLC_bb)
				{
					//here i should've implement approaching to point.... meh
				}
			}
		}
		return 0;
	}
};
#endif // !SAFGUIF_L_PLCV
