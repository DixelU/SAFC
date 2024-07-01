#pragma once
#ifndef SAFGUIF_MRW_H
#define SAFGUIF_MRW_H

#include <functional>
#include "moveable_window.h"

struct MoveableResizeableWindow : MoveableWindow 
{
	enum class PinSide
	{
		left, right, bottom, top, center
	};
	bool ResizeCornerIsActive = false;
	bool ResizeCornerIsHovered = false;
	float MinHeight = 0, MinWidth = 0;
	std::multimap<std::string, PinSide> PinnedWindowActivities;
	std::function<void(float, float, float, float)> OnResize;
	MoveableResizeableWindow(std::string WindowName, SingleTextLineSettings* WindowNameSettings, float XPos, float YPos, float Width, float Height, std::uint32_t RGBABackground, std::uint32_t RGBAThemeColor, std::uint32_t RGBAGradBackground = 0, std::function<void(float, float, float, float)> OnResize = [](float dH, float dW, float NewHeight, float NewWidth){}) : MoveableWindow(WindowName, WindowNameSettings, XPos, YPos, Width, Height, RGBABackground, RGBAThemeColor, RGBAGradBackground), OnResize(OnResize)
	{

	}
	bool virtual IsResizeable() override
	{
		return true;
	}
	void SafeResize(float NewHeight, float NewWidth) override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		if (NewHeight < MinHeight && NewWidth < MinWidth) 
			return;
		else if (NewHeight < MinHeight) 
		{
			NewHeight = MinHeight;
		}
		else if (NewWidth < MinWidth)
		{
			NewWidth = MinWidth;
		}
		float dH = NewHeight - Height, dW = NewWidth - Width;
		OnResize(dH, dW, NewHeight, NewWidth);
		_NotSafeResize(NewHeight, NewWidth);
		bool isRight, isBottom;
		for (auto& [UIPartName, PinSideVal] : PinnedWindowActivities)
		{
			switch (PinSideVal)
			{
				case PinSide::left:
				case PinSide::top:
				{

				}
				break;
				case PinSide::right:
				case PinSide::bottom: 
				{
					int isBottom = (int)PinSideVal - (int)PinSide::right;
					auto CurActivity = WindowActivities.find(UIPartName);
					if (CurActivity != WindowActivities.end())
						CurActivity->second->SafeMove(dW * (!isBottom), -dH * (!!isBottom));
				}
				break;
				case PinSide::center:
				{
					auto CurActivity = WindowActivities.find(UIPartName);
					if (CurActivity != WindowActivities.end())
						CurActivity->second->SafeMove(dW * 0.5, -dH * 0.5);
				}
				break;
			}
		}
	}
	void AssignMinDimentions(float MinHeight, float MinWidth)
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		this->MinHeight = MinHeight;
		this->MinWidth = MinWidth;
	}
	void AssignPinnedActivities(const std::initializer_list<std::string>& list, PinSide side)
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		for (auto& val : list) 
			PinnedWindowActivities.insert({ val, side });
	}
	bool MouseHandler(float mx, float my, char Button/*-1 left, 1 right, 0 move*/, char State /*-1 down, 1 up*/) override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		if (!Drawable) 
			return 0;
		float CenterDraggableX = XWindowPos + Width, CenterDraggableY = YWindowPos - Height;
		float dW = mx - CenterDraggableX, dH = CenterDraggableY - my;

		if (std::abs(mx - CenterDraggableX) + std::abs(my - CenterDraggableY) < 2.5)
		{
#ifdef WINDOWS
			SetCursor(NWSECursor);
#else
			glutSetCursor(GLUT_CURSOR_INFO);
#endif
			ResizeCornerIsHovered = true;
			if (Button == -1 && (State == -1 || State == 1))
				ResizeCornerIsActive = !ResizeCornerIsActive;
		}
		else if (ResizeCornerIsHovered)
			ResizeCornerIsHovered = false;

		if (ResizeCornerIsActive)
		{
			if(Button == -1 && State == 1)
				ResizeCornerIsActive = false;
			SafeResize(Height + dH, Width + dW);
			return 1;
		}
		auto HandlerResult = MoveableWindow::MouseHandler(mx, my, Button, State);
		return HandlerResult;
	}
	void Draw() override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		if (!Drawable) 
			return;
		float CenterDraggableX = XWindowPos + Width, CenterDraggableY = YWindowPos - Height;

		__glcolor(RGBAThemeColor | 0xFFFFFFFF*ResizeCornerIsActive);
		glLineWidth(1 + ResizeCornerIsHovered);
		glBegin(GL_LINE_LOOP);
		glVertex2f(CenterDraggableX + 2.5, CenterDraggableY + 2.5);
		glVertex2f(CenterDraggableX + 2.5, CenterDraggableY - 2.5);
		glVertex2f(CenterDraggableX - 2.5, CenterDraggableY - 2.5);
		glEnd();
		
		MoveableWindow::Draw();
	}
};

#endif