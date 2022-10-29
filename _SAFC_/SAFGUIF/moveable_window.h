#pragma once
#ifndef SAFGUIF_MW 
#define SAFGUIF_MV 

#include <map>

#include "handleable_ui_part.h"
#include "single_text_line_settings.h"

constexpr int WindowHeapSize = 15;
struct MoveableWindow : HandleableUIPart
{
	float XWindowPos, YWindowPos;//leftup corner coordinates
	float Width, Height;
	std::uint32_t RGBABackground, RGBAThemeColor, RGBAGradBackground;
	SingleTextLine* WindowName;
	std::map<std::string, HandleableUIPart*> WindowActivities;
	bool Drawable;
	bool HoveredCloseButton;
	bool CursorFollowMode;
	bool HUIP_MapWasChanged;
	float PCurX, PCurY;
	~MoveableWindow() override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		delete WindowName;
		for (auto i = WindowActivities.begin(); i != WindowActivities.end(); i++)
			delete i->second;
		WindowActivities.clear();
	}
	MoveableWindow(std::string WindowName, SingleTextLineSettings* WindowNameSettings, float XPos, float YPos, float Width, float Height, std::uint32_t RGBABackground, std::uint32_t RGBAThemeColor, std::uint32_t RGBAGradBackground = 0)
	{
		if (WindowNameSettings)
		{
			WindowNameSettings->SetNewPos(XPos, YPos);
			this->WindowName = WindowNameSettings->CreateOne(WindowName);
			this->WindowName->SafeMove(this->WindowName->CalculatedWidth * 0.5 + WindowHeapSize * 0.5f, 0 - WindowHeapSize * 0.5f);
		}
		else
			this->WindowName = nullptr;
		this->HUIP_MapWasChanged = false;
		this->XWindowPos = XPos;
		this->YWindowPos = YPos;
		this->Width = Width;
		this->Height = (Height < WindowHeapSize) ? WindowHeapSize : Height;
		this->RGBABackground = RGBABackground;
		this->RGBAThemeColor = RGBAThemeColor;
		this->RGBAGradBackground = RGBAGradBackground;
		this->CursorFollowMode = 0;
		this->HoveredCloseButton = 0;
		this->Drawable = 1;
		this->PCurX = 0.;
		this->PCurY = 0.;
	}
	void KeyboardHandler(char CH)
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		for (auto i = WindowActivities.begin(); i != WindowActivities.end(); i++) {
			i->second->KeyboardHandler(CH);
			if (HUIP_MapWasChanged) {
				HUIP_MapWasChanged = false;
				break;
			}
		}
	}
	bool MouseHandler(float mx, float my, CHAR Button/*-1 left, 1 right, 0 move*/, CHAR State /*-1 down, 1 up*/) override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		if (!Drawable) 
			return 0;
		HoveredCloseButton = 0;
		if (mx > XWindowPos + Width - WindowHeapSize && mx < XWindowPos + Width && my < YWindowPos && my > YWindowPos - WindowHeapSize)
		{///close button
			if (Button && State == 1)
			{
				Drawable = 0;
				CursorFollowMode = false;
				return 1;
			}
			else if (!Button)
			{
				HoveredCloseButton = 1;
			}
		}
		else if (mx - XWindowPos < Width && mx - XWindowPos>0 && my<YWindowPos && my>YWindowPos - WindowHeapSize)
		{
			if (Button == -1)
			{///window header
				if (State == -1)
				{
					CursorFollowMode = !CursorFollowMode;
					PCurX = mx;
					PCurY = my;
				}
				else if (State == 1)
				{
					CursorFollowMode = !CursorFollowMode;
				}
			}
		}
		if (CursorFollowMode)
		{
			SafeMove(mx - PCurX, my - PCurY);
			PCurX = mx;
			PCurY = my;
			return 1;
		}

		bool flag = 0;
		auto Y = WindowActivities.begin();
		while (Y != WindowActivities.end())
		{
			if (Y->second)
				flag = Y->second->MouseHandler(mx, my, Button, State);
			if (HUIP_MapWasChanged) {
				HUIP_MapWasChanged = false;
				break;
			}
			Y++;
		}

		if (mx - XWindowPos < Width && mx - XWindowPos > 0 && YWindowPos - my > 0 && YWindowPos - my < Height)
			if (Button)
				return 1;
			else
			{
				return flag;
			}
		else 
			return flag;
	}
	void SafeChangePosition(float NewXpos, float NewYpos) override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		NewXpos -= XWindowPos;
		NewYpos -= YWindowPos;
		SafeMove(NewXpos, NewYpos);
	}
	bool DeleteUIElementByName(std::string ElementName, bool DeleteElement = true)
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		HUIP_MapWasChanged = true;
		auto ptr = WindowActivities.find(ElementName);
		if (ptr == WindowActivities.end()) 
			return 0;
		auto deletable = ptr->second;
		WindowActivities.erase(ElementName);
		if(DeleteElement)
			delete deletable;
		return 1;
	}
	bool AddUIElement(std::string ElementName, HandleableUIPart* Elem)
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		HUIP_MapWasChanged = true;
		auto ans = WindowActivities.insert_or_assign(ElementName, Elem);
		return ans.second;
	}
	void SafeMove(float dx, float dy) override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		XWindowPos += dx;
		YWindowPos += dy;
		WindowName->SafeMove(dx, dy);
		for (auto Y = WindowActivities.begin(); Y != WindowActivities.end(); Y++)
			if (Y->second)
				Y->second->SafeMove(dx, dy);
	}
	void SafeChangePosition_Argumented(std::uint8_t Arg, float NewX, float NewY)
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		float CW = 0.5f * (
			(INT32)(!!(GLOBAL_LEFT & Arg))
			- (INT32)(!!(GLOBAL_RIGHT & Arg))
			- 1) * Width,
			CH = 0.5f * (
				(INT32)(!!(GLOBAL_BOTTOM & Arg))
				- (INT32)(!!(GLOBAL_TOP & Arg))
				+ 1) * Height;
		SafeChangePosition(NewX + CW, NewY + CH);
	}
	void Draw() override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		if (!Drawable) 
			return;
		GLCOLOR(RGBABackground);
		glBegin(GL_QUADS);
		glVertex2f(XWindowPos, YWindowPos);
		glVertex2f(XWindowPos + Width, YWindowPos);
		if (RGBAGradBackground)GLCOLOR(RGBAGradBackground);
		glVertex2f(XWindowPos + Width, YWindowPos - Height);
		glVertex2f(XWindowPos, YWindowPos - Height);
		glEnd();
		GLCOLOR(RGBAThemeColor);
		glLineWidth(1);
		glBegin(GL_LINE_LOOP);
		glVertex2f(XWindowPos, YWindowPos);
		glVertex2f(XWindowPos + Width, YWindowPos);
		glVertex2f(XWindowPos + Width, YWindowPos - Height);
		glVertex2f(XWindowPos, YWindowPos - Height);
		glEnd();
		glBegin(GL_QUADS);
		glVertex2f(XWindowPos, YWindowPos);
		glVertex2f(XWindowPos + Width, YWindowPos);
		glVertex2f(XWindowPos + Width, YWindowPos - WindowHeapSize);
		glVertex2f(XWindowPos, YWindowPos - WindowHeapSize);
		glColor4ub(255, 32 + 32 * HoveredCloseButton, 32 + 32 * HoveredCloseButton, 255);
		glVertex2f(XWindowPos + Width, YWindowPos);
		glVertex2f(XWindowPos + Width, YWindowPos + 1 - WindowHeapSize);
		glVertex2f(XWindowPos + Width - WindowHeapSize, YWindowPos + 1 - WindowHeapSize);
		glVertex2f(XWindowPos + Width - WindowHeapSize, YWindowPos);
		glEnd();

		if (WindowName)
			WindowName->Draw();

		for (auto Y = WindowActivities.begin(); Y != WindowActivities.end(); Y++)
			if (Y->second)
				Y->second->Draw();
	}
	void _NotSafeResize(float NewHeight, float NewWidth)
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		this->Height = NewHeight;
		this->Width = NewWidth;
	}
	void _NotSafeResize_Centered(float NewHeight, float NewWidth)
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		float dx, dy;
		XWindowPos += (dx = -0.5f * (NewWidth - Width));
		YWindowPos += (dy = 0.5f * (NewHeight - Height));
		WindowName->SafeMove(dx, dy);
		Width = NewWidth;
		Height = NewHeight;
	}
	void SafeStringReplace(std::string NewWindowTitle) override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		SafeWindowRename(NewWindowTitle);
	}
	void SafeWindowRename(std::string NewWindowTitle)
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		if (WindowName) {
			WindowName->SafeStringReplace(NewWindowTitle);
			WindowName->SafeChangePosition_Argumented(GLOBAL_LEFT, XWindowPos + WindowHeapSize * 0.5f, WindowName->CYpos);
		}
	}
	HandleableUIPart*& operator[](std::string ID)
	{
		return WindowActivities[ID];
	}
	std::uint32_t TellType()
	{
		return TT_MOVEABLE_WINDOW;
	}
};



#endif 