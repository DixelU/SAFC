#pragma once
#ifndef _SAFGUIF_MFW_H
#define _SAFGUIF_MFW_H

#include <numbers>

#include "moveable_window.h"

// Moveable futuristic window (non-resizeable)
struct MoveableFuiWindow : public MoveableWindow 
{
	std::uint32_t BorderRGBA;
	float HeadersHatWidth;
	float HeadersHatHeight;
	float TopHandlesHeight;
	float BottomHandlesHeight;
	float HandlesBudgeDepth;

	static constexpr float BudgeSlope = 1.f;

	MoveableFuiWindow(std::string WindowName, SingleTextLineSettings* WindowNameSettings,
		float XPos, float YPos, float Width, float Height,
		float HeadersHatWidth, float HeadersHatHeight,
		float TopHandlesHeight, float BottomHandlesHeight,
		float HandlesBudgeDepth,
		std::uint32_t RGBABackground, std::uint32_t RGBAThemeColor, std::uint32_t BorderRGBA) :
		MoveableWindow(WindowName, WindowNameSettings, XPos, YPos, Width, Height, RGBABackground, RGBAThemeColor),
		BorderRGBA(BorderRGBA),
		HeadersHatWidth(HeadersHatWidth),
		HeadersHatHeight(HeadersHatHeight),
		TopHandlesHeight(TopHandlesHeight),
		BottomHandlesHeight(BottomHandlesHeight),
		HandlesBudgeDepth(HandlesBudgeDepth)
	{
		this->WindowName->SafeChangePosition_Argumented(_Align::center, XPos + Width * 0.5f, YPos - (WindowHeaderSize - HeadersHatHeight) * 0.5f);
	}

	~MoveableFuiWindow() override = default;

	void Draw() override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);

		if (!Drawable) 
			return;

		// HEADER 
		float FullHeaderHatWidth = HeadersHatWidth + HeadersHatHeight * 2 * BudgeSlope;
		float HeadersHatBeginX = XWindowPos + 0.5 * (Width - FullHeaderHatWidth);

		__glcolor(RGBAThemeColor);
		glBegin(GL_QUADS);
		// Main header
		glVertex2f(XWindowPos, YWindowPos);
		glVertex2f(XWindowPos + Width, YWindowPos);
		glVertex2f(XWindowPos + Width, YWindowPos - WindowHeaderSize);
		glVertex2f(XWindowPos, YWindowPos - WindowHeaderSize);

		// header hat
		glVertex2f(HeadersHatBeginX, YWindowPos);
		glVertex2f(HeadersHatBeginX + BudgeSlope * HeadersHatHeight, YWindowPos + HeadersHatHeight);
		glVertex2f(HeadersHatBeginX + BudgeSlope * HeadersHatHeight + HeadersHatWidth, YWindowPos + HeadersHatHeight);
		glVertex2f(HeadersHatBeginX + FullHeaderHatWidth, YWindowPos);
		glEnd();

		// main body (background)
		__glcolor(RGBABackground);
		// Top handles area
		glBegin(GL_POLYGON);
		glVertex2f(XWindowPos, YWindowPos - WindowHeaderSize);
		glVertex2f(XWindowPos, YWindowPos - WindowHeaderSize - TopHandlesHeight);
		glVertex2f(XWindowPos + HandlesBudgeDepth, YWindowPos - WindowHeaderSize - TopHandlesHeight - BudgeSlope * HandlesBudgeDepth);
		glVertex2f(XWindowPos + Width - HandlesBudgeDepth, YWindowPos - WindowHeaderSize - TopHandlesHeight - BudgeSlope * HandlesBudgeDepth);
		glVertex2f(XWindowPos + Width, YWindowPos - WindowHeaderSize - TopHandlesHeight);
		glVertex2f(XWindowPos + Width, YWindowPos - WindowHeaderSize);
		glEnd();

		// Bottom handles area
		glBegin(GL_POLYGON);
		glVertex2f(XWindowPos, YWindowPos - Height);
		glVertex2f(XWindowPos, YWindowPos - Height + BottomHandlesHeight);
		glVertex2f(XWindowPos + HandlesBudgeDepth, YWindowPos - Height + BottomHandlesHeight + BudgeSlope * HandlesBudgeDepth);
		glVertex2f(XWindowPos + Width - HandlesBudgeDepth, YWindowPos - Height + BottomHandlesHeight + BudgeSlope * HandlesBudgeDepth);
		glVertex2f(XWindowPos + Width, YWindowPos - Height + BottomHandlesHeight);
		glVertex2f(XWindowPos + Width, YWindowPos - Height);
		glEnd();

		// Middle area
		glBegin(GL_POLYGON);
		glVertex2f(XWindowPos + HandlesBudgeDepth, YWindowPos - WindowHeaderSize - TopHandlesHeight - BudgeSlope * HandlesBudgeDepth);
		glVertex2f(XWindowPos + Width - HandlesBudgeDepth, YWindowPos - WindowHeaderSize - TopHandlesHeight - BudgeSlope * HandlesBudgeDepth);
		glVertex2f(XWindowPos + Width - HandlesBudgeDepth, YWindowPos - Height + BottomHandlesHeight + BudgeSlope * HandlesBudgeDepth);
		glVertex2f(XWindowPos + HandlesBudgeDepth, YWindowPos - Height + BottomHandlesHeight + BudgeSlope * HandlesBudgeDepth);
		glEnd();

		auto FadingBorderRGBA = BorderRGBA;
		for (size_t LineWidth = 1; LineWidth <= 2; LineWidth *= 2)
		{
			__glcolor(FadingBorderRGBA);

			// Top handles border
			glLineWidth(LineWidth);
			glBegin(GL_LINE_STRIP);
			glVertex2f(XWindowPos + HandlesBudgeDepth, YWindowPos - WindowHeaderSize - TopHandlesHeight - BudgeSlope * HandlesBudgeDepth);
			glVertex2f(XWindowPos, YWindowPos - WindowHeaderSize - TopHandlesHeight);
			glVertex2f(XWindowPos, YWindowPos - WindowHeaderSize);
			glVertex2f(XWindowPos + Width, YWindowPos - WindowHeaderSize);
			glVertex2f(XWindowPos + Width, YWindowPos - WindowHeaderSize - TopHandlesHeight);
			glVertex2f(XWindowPos + Width - HandlesBudgeDepth, YWindowPos - WindowHeaderSize - TopHandlesHeight - BudgeSlope * HandlesBudgeDepth);
			glEnd();

			// Bottom handles border
			glBegin(GL_LINE_STRIP);
			glVertex2f(XWindowPos + HandlesBudgeDepth, YWindowPos - Height + BottomHandlesHeight + BudgeSlope * HandlesBudgeDepth);
			glVertex2f(XWindowPos, YWindowPos - Height + BottomHandlesHeight);
			glVertex2f(XWindowPos, YWindowPos - Height);
			glVertex2f(XWindowPos + Width, YWindowPos - Height);
			glVertex2f(XWindowPos + Width, YWindowPos - Height + BottomHandlesHeight);
			glVertex2f(XWindowPos + Width - HandlesBudgeDepth, YWindowPos - Height + BottomHandlesHeight + BudgeSlope * HandlesBudgeDepth);
			glEnd();

			// header hat border
			glBegin(GL_LINE_STRIP);
			glVertex2f(XWindowPos, YWindowPos - WindowHeaderSize);
			glVertex2f(XWindowPos, YWindowPos);
			glVertex2f(HeadersHatBeginX, YWindowPos);
			glVertex2f(HeadersHatBeginX + BudgeSlope * HeadersHatHeight, YWindowPos + HeadersHatHeight);
			glVertex2f(HeadersHatBeginX + BudgeSlope * HeadersHatHeight + HeadersHatWidth, YWindowPos + HeadersHatHeight);
			glVertex2f(HeadersHatBeginX + FullHeaderHatWidth, YWindowPos);
			glVertex2f(XWindowPos + Width, YWindowPos);
			glVertex2f(XWindowPos + Width, YWindowPos - WindowHeaderSize);
			glEnd();

			FadingBorderRGBA = (FadingBorderRGBA & 0xFFFFFF00) | ((FadingBorderRGBA & 0xFF) >> 1);
		}

		glLineWidth(3 + HoveredCloseButton);
		__glcolor(BorderRGBA | (0xFF * HoveredCloseButton) | 0xFF000000);
		constexpr int CloseButtonSides = 3;
		glBegin(GL_LINES);
		for (int i = 0; i < CloseButtonSides; i++)
		{
			float XOffset = sinf(2 * std::numbers::pi_v<float> * i / float(CloseButtonSides)) * 0.35f * WindowHeaderSize;
			float YOffset = cosf(2 * std::numbers::pi_v<float> * i / float(CloseButtonSides)) * 0.35f * WindowHeaderSize;

			glVertex2f(XWindowPos + Width - WindowHeaderSize * 0.5f, YWindowPos - WindowHeaderSize * 0.45f);
			glVertex2f(XWindowPos + Width - WindowHeaderSize * 0.5f + XOffset, YWindowPos - WindowHeaderSize * 0.45f - YOffset);
		}
		glEnd();

		if (WindowName)
			WindowName->Draw();

		for (auto Y = WindowActivities.begin(); Y != WindowActivities.end(); Y++)
			if (Y->second)
				Y->second->Draw();
	}

	bool MouseHandler(float mx, float my, CHAR Button/*-1 left, 1 right, 0 move*/, CHAR State /*-1 down, 1 up*/) override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);

		if (!Drawable)
			return false;

		HoveredCloseButton = false;

		float CloseButtonX[] =
		{ 
			XWindowPos + Width - WindowHeaderSize, 
			XWindowPos + Width,
			XWindowPos + Width,
			XWindowPos + Width - WindowHeaderSize
		};
		float CloseButtonY[] =
		{
			YWindowPos,
			YWindowPos,
			YWindowPos - WindowHeaderSize,
			YWindowPos - WindowHeaderSize
		};

		float FullHeaderHatWidth = HeadersHatWidth + HeadersHatHeight * 2 * BudgeSlope;
		float HeadersHatBeginX = XWindowPos + 0.5 * (Width - FullHeaderHatWidth);
		float HeaderGeometryX[] = 
		{
			XWindowPos,
			HeadersHatBeginX,
			HeadersHatBeginX + BudgeSlope * HeadersHatHeight,
			HeadersHatBeginX + BudgeSlope * HeadersHatHeight + HeadersHatWidth,
			HeadersHatBeginX + FullHeaderHatWidth,
			XWindowPos + Width,
			XWindowPos + Width,
			XWindowPos
		};
		float HeaderGeometryY[] = 
		{
			YWindowPos, 
			YWindowPos,
			YWindowPos + HeadersHatHeight,
			YWindowPos + HeadersHatHeight,
			YWindowPos,
			YWindowPos,
			YWindowPos - WindowHeaderSize,
			YWindowPos - WindowHeaderSize
		};

		float WindowGeometryX[] = 
		{
			XWindowPos,
			XWindowPos,
			XWindowPos + HandlesBudgeDepth,
			XWindowPos + HandlesBudgeDepth,
			XWindowPos,
			XWindowPos,
			XWindowPos + Width,
			XWindowPos + Width,
			XWindowPos + Width - HandlesBudgeDepth,
			XWindowPos + Width - HandlesBudgeDepth,
			XWindowPos + Width,
			XWindowPos + Width
		};
		float WindowGeometryY[] = 
		{
			YWindowPos - WindowHeaderSize,
			YWindowPos - WindowHeaderSize - TopHandlesHeight,
			YWindowPos - WindowHeaderSize - TopHandlesHeight - BudgeSlope * HandlesBudgeDepth,
			YWindowPos - Height + BottomHandlesHeight + BudgeSlope * HandlesBudgeDepth,
			YWindowPos - Height + BottomHandlesHeight,
			YWindowPos - Height,
			YWindowPos - Height,
			YWindowPos - Height + BottomHandlesHeight,
			YWindowPos - Height + BottomHandlesHeight + BudgeSlope * HandlesBudgeDepth,
			YWindowPos - WindowHeaderSize - TopHandlesHeight - BudgeSlope * HandlesBudgeDepth,
			YWindowPos - WindowHeaderSize - TopHandlesHeight,
			YWindowPos - WindowHeaderSize
		};

		bool InHeader = false;

		///close button
		if (InHeader |= PointInPoly(CloseButtonX, CloseButtonY, mx, my))
		{
			if (Button && State == 1)
			{
				Drawable = false;
				CursorFollowMode = false;
				return true;
			}
			else if (!Button)
				HoveredCloseButton = true;
		}
		///window header
		else if (InHeader |= PointInPoly(HeaderGeometryX, HeaderGeometryY, mx, my))
		{
			if (Button == -1)
			{
				if (State == -1)
				{
					CursorFollowMode = !CursorFollowMode;
					PCurX = mx;
					PCurY = my;
				}
				else if (State == 1)
					CursorFollowMode = !CursorFollowMode;
			}
		}
		if (CursorFollowMode)
		{
			SafeMove(mx - PCurX, my - PCurY);
			PCurX = mx;
			PCurY = my;
			return true;
		}

		bool flag = false;
		auto Y = WindowActivities.begin();
		while (Y != WindowActivities.end())
		{
			if (Y->second)
				flag = Y->second->MouseHandler(mx, my, Button, State);
			if (HUIP_MapWasChanged)
			{
				HUIP_MapWasChanged = false;
				break;
			}
			Y++;
		}

		if (InHeader || PointInPoly(WindowGeometryX, WindowGeometryY, mx, my))
		{
			if (Button)
				return true;
			else
			{
				return flag;
			}
		}
		else
			return flag;
	}

	void SafeWindowRename(const std::string& NewWindowTitle, bool ForceNoShift = false) override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		if (!WindowName)
			return;

		WindowName->SafeStringReplace(NewWindowTitle);
		if (ForceNoShift)
			return;

		this->WindowName->SafeChangePosition_Argumented(
			_Align::center,
			XWindowPos + Width * 0.5f,
			YWindowPos - (WindowHeaderSize - HeadersHatHeight) * 0.5f);
	}
};

#endif 