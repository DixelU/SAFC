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
		this->WindowName->SafeChangePosition_Argumented(_Align::center, XPos + Width * 0.5f, YPos - WindowHeaderSize * 0.5f);
	}

	~MoveableFuiWindow() override = default;

	void Draw() override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);

		if (!Drawable) 
			return;

		// HEADER 
		float FullHeaderHatWidth = HeadersHatWidth + HeadersHatHeight * 2 * BudgeSlope; // (half height at each side)
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
		glVertex2f(HeadersHatBeginX + 0.5f * HeadersHatHeight, YWindowPos + HeadersHatHeight);
		glVertex2f(HeadersHatBeginX + 0.5f * HeadersHatHeight + HeadersHatWidth, YWindowPos + HeadersHatHeight);
		glVertex2f(HeadersHatBeginX + HeadersHatHeight + HeadersHatWidth, YWindowPos);
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
			glVertex2f(HeadersHatBeginX + 0.5f * HeadersHatHeight, YWindowPos + HeadersHatHeight);
			glVertex2f(HeadersHatBeginX + 0.5f * HeadersHatHeight + HeadersHatWidth, YWindowPos + HeadersHatHeight);
			glVertex2f(HeadersHatBeginX + HeadersHatHeight + HeadersHatWidth, YWindowPos);
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

			glVertex2f(XWindowPos + Width - WindowHeaderSize * 0.5f, YWindowPos - WindowHeaderSize * 0.4f);
			glVertex2f(XWindowPos + Width - WindowHeaderSize * 0.5f + XOffset, YWindowPos - WindowHeaderSize * 0.4f - YOffset);
		}
		glEnd();

		if (WindowName)
			WindowName->Draw();

		for (auto Y = WindowActivities.begin(); Y != WindowActivities.end(); Y++)
			if (Y->second)
				Y->second->Draw();
	}
};

#endif 