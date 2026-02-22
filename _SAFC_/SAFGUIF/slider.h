#pragma once
#ifndef SAFGUIF_SLIDER
#define SAFGUIF_SLIDER

#include "header_utils.h"
#include "handleable_ui_part.h"
#include "single_text_line_settings.h"

struct Slider : HandleableUIPart
{
	enum class Orientation { horizontal, vertical };

	SingleTextLine* Label;
	Orientation orientation;
	float Xpos, Ypos;
	float TrackLength;
	float HandleSize;
	float TrackThickness;

	std::uint32_t TrackColor;
	std::uint32_t HandleColor;
	std::uint32_t HandleHoverColor;
	std::uint32_t HandleDragColor;
	std::uint32_t HandleBorderColor;

	float MinValue, MaxValue;
	float CurrentValue;
	float NormalizedPosition; // 0.0 to 1.0

	bool Hovered;
	bool Dragging;
	bool FireOnRelease; // if true, OnValueChange fires only on mouse release, not on every drag move
	float DragOffsetX, DragOffsetY;

	void(*OnValueChange)(float);

	~Slider() override
	{
		if (Label)
			delete Label;
	}

	Slider(
		Orientation orientation,
		float Xpos, float Ypos,
		float TrackLength,
		float MinValue, float MaxValue,
		float DefaultValue,
		void(*OnValueChange)(float),
		std::uint32_t TrackColor = 0x808080FF,
		std::uint32_t HandleColor = 0xFFFFFFFF,
		std::uint32_t HandleHoverColor = 0xAAFFFFFF,
		std::uint32_t HandleDragColor = 0x00FFFFFF,
		std::uint32_t HandleBorderColor = 0x000000FF,
		float HandleSize = 12.0f,
		float TrackThickness = 4.0f,
		SingleTextLineSettings* LabelSTLS = nullptr,
		std::string LabelText = ""
	)
	{
		this->orientation = orientation;
		this->Xpos = Xpos;
		this->Ypos = Ypos;
		this->TrackLength = TrackLength;
		this->MinValue = MinValue;
		this->MaxValue = MaxValue;
		this->CurrentValue = DefaultValue;
		this->OnValueChange = OnValueChange;

		this->TrackColor = TrackColor;
		this->HandleColor = HandleColor;
		this->HandleHoverColor = HandleHoverColor;
		this->HandleDragColor = HandleDragColor;
		this->HandleBorderColor = HandleBorderColor;
		this->HandleSize = HandleSize;
		this->TrackThickness = TrackThickness;

		this->Hovered = false;
		this->Dragging = false;
		this->FireOnRelease = false;
		this->DragOffsetX = 0.0f;
		this->DragOffsetY = 0.0f;

		// Calculate normalized position from default value
		if (MaxValue != MinValue)
			this->NormalizedPosition = (DefaultValue - MinValue) / (MaxValue - MinValue);
		else
			this->NormalizedPosition = 0.0f;

		// Clamp normalized position
		if (this->NormalizedPosition < 0.0f) this->NormalizedPosition = 0.0f;
		if (this->NormalizedPosition > 1.0f) this->NormalizedPosition = 1.0f;

		// Create label if provided
		if (LabelSTLS && !LabelText.empty())
		{
			float labelOffset = TrackLength * 0.5f + HandleSize + TrackThickness;
			this->Label = LabelSTLS->CreateOne(LabelText);
			this->Label->SafeChangePosition_Argumented(
				orientation == Orientation::horizontal ? GLOBAL_LEFT : GLOBAL_TOP,
				Xpos + (orientation == Orientation::horizontal ? labelOffset : 0),
				Ypos + (orientation == Orientation::vertical ? labelOffset : 0)
			);
		}
		else
		{
			this->Label = nullptr;
		}

		this->Enabled = true;
	}

	void GetHandlePosition(float& hx, float& hy)
	{
		if (orientation == Orientation::horizontal)
		{
			hx = Xpos - TrackLength * 0.5f + NormalizedPosition * TrackLength;
			hy = Ypos;
		}
		else // vertical
		{
			hx = Xpos;
			hy = Ypos - TrackLength * 0.5f + NormalizedPosition * TrackLength;
		}
	}

	void Draw() override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		if (!Enabled)
			return;

		// Draw track
		__glcolor(TrackColor);
		glLineWidth(TrackThickness);
		glBegin(GL_LINES);
		if (orientation == Orientation::horizontal)
		{
			glVertex2f(Xpos - TrackLength * 0.5f, Ypos);
			glVertex2f(Xpos + TrackLength * 0.5f, Ypos);
		}
		else // vertical
		{
			glVertex2f(Xpos, Ypos - TrackLength * 0.5f);
			glVertex2f(Xpos, Ypos + TrackLength * 0.5f);
		}
		glEnd();

		// Get handle position
		float hx, hy;
		GetHandlePosition(hx, hy);

		// Draw handle
		std::uint32_t currentHandleColor = Dragging ? HandleDragColor : (Hovered ? HandleHoverColor : HandleColor);

		// Handle background
		__glcolor(currentHandleColor);
		glBegin(GL_QUADS);
		glVertex2f(hx - HandleSize * 0.5f, hy + HandleSize * 0.5f);
		glVertex2f(hx + HandleSize * 0.5f, hy + HandleSize * 0.5f);
		glVertex2f(hx + HandleSize * 0.5f, hy - HandleSize * 0.5f);
		glVertex2f(hx - HandleSize * 0.5f, hy - HandleSize * 0.5f);
		glEnd();

		// Handle border
		__glcolor(HandleBorderColor);
		glLineWidth(2.0f);
		glBegin(GL_LINE_LOOP);
		glVertex2f(hx - HandleSize * 0.5f, hy + HandleSize * 0.5f);
		glVertex2f(hx + HandleSize * 0.5f, hy + HandleSize * 0.5f);
		glVertex2f(hx + HandleSize * 0.5f, hy - HandleSize * 0.5f);
		glVertex2f(hx - HandleSize * 0.5f, hy - HandleSize * 0.5f);
		glEnd();

		// Draw label if it exists
		if (Label)
			Label->Draw();
	}

	bool MouseHandler(float mx, float my, CHAR Button/*-1 left, 1 right, 0 move, 2 wheel up, 3 wheel down*/, CHAR State /*-1 down, 1 up*/) override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		if (!Enabled)
			return 0;

		float hx, hy;
		GetHandlePosition(hx, hy);

		float dx = mx - hx;
		float dy = my - hy;

		// Check if mouse is over handle
		bool overHandle = (fabsf(dx) <= HandleSize * 0.5f && fabsf(dy) <= HandleSize * 0.5f);

		if (Dragging)
		{
			if (Button == -1 && State == 1) // Left button released
			{
				Dragging = false;
				// FireOnRelease: callback deferred until here
				if (FireOnRelease && OnValueChange)
					OnValueChange(CurrentValue);
			}
			else // Continue dragging
			{
				float newPos;
				if (orientation == Orientation::horizontal)
				{
					newPos = mx - DragOffsetX - (Xpos - TrackLength * 0.5f);
				}
				else // vertical
				{
					newPos = my - DragOffsetY - (Ypos - TrackLength * 0.5f);
				}

				// Update normalized position
				NormalizedPosition = newPos / TrackLength;

				// Clamp to [0, 1]
				if (NormalizedPosition < 0.0f) NormalizedPosition = 0.0f;
				if (NormalizedPosition > 1.0f) NormalizedPosition = 1.0f;

				// Update current value
				float oldValue = CurrentValue;
				CurrentValue = MinValue + NormalizedPosition * (MaxValue - MinValue);

				// Call callback if value changed (skipped if FireOnRelease - fires on release instead)
				if (!FireOnRelease && OnValueChange && oldValue != CurrentValue)
					OnValueChange(CurrentValue);

				SetCursor(HandCursor);
			}
			return 1;
		}
		else
		{
			if (overHandle)
			{
				Hovered = true;
				SetCursor(HandCursor);

				if (Button == -1 && State == -1) // Left button pressed
				{
					Dragging = true;
					DragOffsetX = mx - hx;
					DragOffsetY = my - hy;
					return 1;
				}
			}
			else
			{
				Hovered = false;
			}
		}

		return 0;
	}

	void SafeMove(float dx, float dy) override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		Xpos += dx;
		Ypos += dy;
		if (Label)
			Label->SafeMove(dx, dy);
	}

	void SafeChangePosition(float NewX, float NewY) override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		float dx = NewX - Xpos;
		float dy = NewY - Ypos;
		SafeMove(dx, dy);
	}

	void SafeChangePosition_Argumented(std::uint8_t Arg, float NewX, float NewY) override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		float CW = 0.5f * (
			(std::int32_t)((bool)(GLOBAL_LEFT & Arg))
			- (std::int32_t)((bool)(GLOBAL_RIGHT & Arg))
			) * (orientation == Orientation::horizontal ? TrackLength : 0),
			CH = 0.5f * (
				(std::int32_t)((bool)(GLOBAL_BOTTOM & Arg))
				- (std::int32_t)((bool)(GLOBAL_TOP & Arg))
				) * (orientation == Orientation::vertical ? TrackLength : 0);
		SafeChangePosition(NewX + CW, NewY + CH);
	}

	void SafeStringReplace(std::string NewString) override
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		if (Label)
			Label->SafeStringReplace(NewString);
	}

	void KeyboardHandler(char CH) override
	{
		// Sliders typically don't respond to keyboard input
		// but you could add arrow key support here if needed
	}

	// Helper methods
	void SetValue(float value, bool with_trigger = true)
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		CurrentValue = value;
		if (CurrentValue < MinValue) CurrentValue = MinValue;
		if (CurrentValue > MaxValue) CurrentValue = MaxValue;

		if (MaxValue != MinValue)
			NormalizedPosition = (CurrentValue - MinValue) / (MaxValue - MinValue);
		else
			NormalizedPosition = 0.0f;

		if (with_trigger)
			OnValueChange(CurrentValue);
	}

	float GetValue()
	{
		std::lock_guard<std::recursive_mutex> locker(Lock);
		return CurrentValue;
	}

	inline std::uint32_t TellType() override
	{
		return TT_SLIDER;
	}
};

#endif // !SAFGUIF_SLIDER
