#pragma once
#ifndef SAFGUIF_MRW_H
#define SAFGUIF_MRW_H

#include <functional>
#include "moveable_window.h"

struct MoveableResizeableWindow : MoveableWindow {
	enum class PinSide {
		left, right, bottom, top, center
	};
	BIT ResizeCornerIsActive = false;
	BIT ResizeCornerIsHovered = false;
	float MinHeight = 0, MinWidth = 0;
	std::multimap<std::string, PinSide> PinnedWindowActivities;
	std::function<void(float, float, float, float)> OnResize;
	MoveableResizeableWindow(std::string WindowName, SingleTextLineSettings* WindowNameSettings, float XPos, float YPos, float Width, float Height, DWORD RGBABackground, DWORD RGBAThemeColor, DWORD RGBAGradBackground = 0, std::function<void(float, float, float, float)> OnResize = [](float dH, float dW, float NewHeight, float NewWidth){}) : MoveableWindow(WindowName, WindowNameSettings, XPos, YPos, Width, Height, RGBABackground, RGBAThemeColor, RGBAGradBackground), OnResize(OnResize) {

	}
	bool virtual IsResizeable() {
		return true;
	}
	void SafeResize(float NewHeight, float NewWidth) override {
		Lock.lock();
		if (NewHeight < MinHeight && NewWidth < MinWidth) {
			Lock.unlock();
			return;
		}
		else if (NewHeight < MinHeight) {
			NewHeight = MinHeight;
		}
		else if (NewWidth < MinWidth) {
			NewWidth = MinWidth;
		}
		float dH = NewHeight - Height, dW = NewWidth - Width;
		OnResize(dH, dW, NewHeight, NewWidth);
		_NotSafeResize(NewHeight, NewWidth);
		bool isRight, isBottom;
		for (auto& [UIPartName, PinSideVal] : PinnedWindowActivities) {
			switch (PinSideVal) {
			case PinSide::left: 
			case PinSide::top:  {

			}break;
			case PinSide::right:
			case PinSide::bottom: {
				int isBottom = (int)PinSideVal - (int)PinSide::right;
				auto CurActivity = WindowActivities.find(UIPartName);
				if (CurActivity != WindowActivities.end()) 
					CurActivity->second->SafeMove(dW * (!isBottom), -dH * (!!isBottom));
			}break;
			case PinSide::center: {
				auto CurActivity = WindowActivities.find(UIPartName);
				if (CurActivity != WindowActivities.end())
					CurActivity->second->SafeMove(dW * 0.5, -dH * 0.5);
			}break;
			}
		}
		Lock.unlock();
	}
	void AssignMinDimentions(float MinHeight, float MinWidth) {
		Lock.lock();
		this->MinHeight = MinHeight;
		this->MinWidth = MinWidth;
		Lock.unlock();
	}
	void AssignPinnedActivities(const std::initializer_list<std::string>& list, PinSide side) {
		Lock.lock();
		for (auto& val : list) 
			PinnedWindowActivities.insert({ val, side });
		Lock.unlock();
	}
	BIT MouseHandler(float mx, float my, CHAR Button/*-1 left, 1 right, 0 move*/, CHAR State /*-1 down, 1 up*/) override {
		Lock.lock();
		if (!Drawable) {
			Lock.unlock();
			return 0;
		}
		float CenterDraggableX = XWindowPos + Width, CenterDraggableY = YWindowPos - Height;
		float dW = mx - CenterDraggableX, dH = CenterDraggableY - my;

		if (std::abs(mx - CenterDraggableX) + std::abs(my - CenterDraggableY) < 2.5) {
			SetCursor(NWSECursor);
			ResizeCornerIsHovered = true;
			if (Button == -1 && (State == -1 || State == 1))
				ResizeCornerIsActive = !ResizeCornerIsActive;
		}
		else if (ResizeCornerIsHovered)
			ResizeCornerIsHovered = false;

		if (ResizeCornerIsActive) {
			if(Button == -1 && State == 1)
				ResizeCornerIsActive = !ResizeCornerIsActive;
			SafeResize(Height + dH, Width + dW);
			Lock.unlock();
			return 1;
		}
		auto HandlerResult = MoveableWindow::MouseHandler(mx, my, Button, State);
		Lock.unlock();
		return HandlerResult;
	}
	void Draw() override {
		Lock.lock();
		if (!Drawable) {
			Lock.unlock();
			return;
		}
		float CenterDraggableX = XWindowPos + Width, CenterDraggableY = YWindowPos - Height;

		GLCOLOR(RGBAThemeColor | 0xFFFFFFFF*ResizeCornerIsActive);
		glLineWidth(1 + ResizeCornerIsHovered);
		glBegin(GL_LINE_LOOP);
		glVertex2f(CenterDraggableX + 2.5, CenterDraggableY + 2.5);
		glVertex2f(CenterDraggableX + 2.5, CenterDraggableY - 2.5);
		glVertex2f(CenterDraggableX - 2.5, CenterDraggableY - 2.5);
		glEnd();
		
		MoveableWindow::Draw();
		Lock.unlock();
	}
};

#endif