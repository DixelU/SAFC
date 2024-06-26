#pragma once
#ifndef SAFGUIF_BS
#define SAFGUIF_BS

#include "button.h"

struct ButtonSettings
{
	std::string ButtonText, TipText;
	void(*OnClick)();
	float Xpos, Ypos, Width, Height, CharHeight;
	std::uint32_t RGBAColor, gRGBAColor, RGBABackground, RGBABorder, HoveredRGBAColor, HoveredRGBABackground, HoveredRGBABorder;
	std::uint8_t BasePoint, GradPoint, BorderWidth;
	bool STLSBasedSettings;
	SingleTextLineSettings* Tip, * STLS;
	ButtonSettings(std::string ButtonText, void(*OnClick)(), float Xpos, float Ypos, float Width, float Height, float CharHeight, std::uint32_t RGBAColor, std::uint32_t gRGBAColor, std::uint8_t BasePoint, std::uint8_t GradPoint, std::uint8_t BorderWidth, std::uint32_t RGBABackground, std::uint32_t RGBABorder, std::uint32_t HoveredRGBAColor, std::uint32_t HoveredRGBABackground, std::uint32_t HoveredRGBABorder, SingleTextLineSettings* Tip, std::string TipText)
	{
		this->STLSBasedSettings = 0;
		this->STLS = nullptr;
		this->ButtonText = std::move(ButtonText);
		this->TipText = std::move(TipText);
		this->OnClick = OnClick;
		this->Tip = Tip;
		this->Xpos = Xpos;
		this->Ypos = Ypos;
		this->Width = Width;
		this->Height = Height;
		this->CharHeight = CharHeight;
		this->RGBAColor = RGBAColor;
		this->gRGBAColor = gRGBAColor;
		this->BasePoint = BasePoint;
		this->GradPoint = GradPoint;
		this->BorderWidth = BorderWidth;
		this->RGBABackground = RGBABackground;
		this->RGBABorder = RGBABorder;
		this->HoveredRGBAColor = HoveredRGBAColor;
		this->HoveredRGBABackground = HoveredRGBABackground;
		this->HoveredRGBABorder = HoveredRGBABorder;
	}
	ButtonSettings(std::string ButtonText, SingleTextLineSettings* ButtonTextSTLS, void(*OnClick)(), float Xpos, float Ypos, float Width, float Height, std::uint8_t BorderWidth, std::uint32_t RGBABackground, std::uint32_t RGBABorder, std::uint32_t HoveredRGBAColor, std::uint32_t HoveredRGBABackground, std::uint32_t HoveredRGBABorder, SingleTextLineSettings* Tip, std::string TipText = " ")
	{
		this->STLSBasedSettings = 1;
		this->ButtonText = std::move(ButtonText);
		this->TipText = std::move(TipText);
		this->OnClick = OnClick;
		this->Tip = Tip;
		this->Xpos = Xpos;
		this->Ypos = Ypos;
		this->Width = Width;
		this->Height = Height;
		this->STLS = ButtonTextSTLS;
		this->BorderWidth = BorderWidth;
		this->gRGBAColor = 0;
		this->BasePoint = 0;
		this->GradPoint = 0;
		this->RGBAColor = ButtonTextSTLS->RGBAColor;
		this->RGBABackground = RGBABackground;
		this->RGBABorder = RGBABorder;
		this->HoveredRGBAColor = HoveredRGBAColor;
		this->HoveredRGBABackground = HoveredRGBABackground;
		this->HoveredRGBABorder = HoveredRGBABorder;
	}
	ButtonSettings(std::string ButtonText, void(*OnClick)(), float Xpos, float Ypos, float Width, float Height, float CharHeight, std::uint32_t RGBAColor, std::uint8_t BorderWidth, std::uint32_t RGBABackground, std::uint32_t RGBABorder, std::uint32_t HoveredRGBAColor, std::uint32_t HoveredRGBABackground, std::uint32_t HoveredRGBABorder, SingleTextLineSettings* Tip, std::string TipText) :ButtonSettings(ButtonText, OnClick, Xpos, Ypos, Width, Height, CharHeight, RGBAColor, 0, 15, 15, BorderWidth, RGBABackground, RGBABorder, HoveredRGBAColor, HoveredRGBABackground, HoveredRGBABorder, Tip, TipText) {}
	ButtonSettings(std::string ButtonText, SingleTextLineSettings* ButtonTextSTLS, void(*OnClick)(), float Xpos, float Ypos, float Width, float Height, std::uint8_t BorderWidth, std::uint32_t RGBABackground, std::uint32_t RGBABorder, std::uint32_t HoveredRGBAColor, std::uint32_t HoveredRGBABackground, std::uint32_t HoveredRGBABorder) :ButtonSettings(ButtonText, ButtonTextSTLS, OnClick, Xpos, Ypos, Width, Height, BorderWidth, RGBABackground, RGBABorder, HoveredRGBAColor, HoveredRGBABackground, HoveredRGBABorder, NULL, " ") {}
	ButtonSettings(std::string ButtonText, void(*OnClick)(), float Xpos, float Ypos, float Width, float Height, float CharHeight, std::uint32_t RGBAColor, std::uint8_t BorderWidth, std::uint32_t RGBABackground, std::uint32_t RGBABorder, std::uint32_t HoveredRGBAColor, std::uint32_t HoveredRGBABackground, std::uint32_t HoveredRGBABorder) :ButtonSettings(ButtonText, OnClick, Xpos, Ypos, Width, Height, CharHeight, RGBAColor, 0, 15, 15, BorderWidth, RGBABackground, RGBABorder, HoveredRGBAColor, HoveredRGBABackground, HoveredRGBABorder, NULL, " ") {}

	ButtonSettings(SingleTextLineSettings* ButtonTextSTLS, void(*OnClick)(), float Xpos, float Ypos, float Width, float Height, std::uint8_t BorderWidth, std::uint32_t RGBABackground, std::uint32_t RGBABorder, std::uint32_t HoveredRGBAColor, std::uint32_t HoveredRGBABackground, std::uint32_t HoveredRGBABorder) :ButtonSettings(" ", ButtonTextSTLS, OnClick, Xpos, Ypos, Width, Height, BorderWidth, RGBABackground, RGBABorder, HoveredRGBAColor, HoveredRGBABackground, HoveredRGBABorder, NULL, " ") {}
	ButtonSettings(std::string ButtonText, void(*OnClick)(), float Xpos, float Ypos, float Width, float Height, float CharHeight, std::uint32_t RGBAColor, std::uint32_t gRGBAColor, std::uint8_t BasePoint, std::uint8_t GradPoint, std::uint8_t BorderWidth, std::uint32_t RGBABackground, std::uint32_t RGBABorder, std::uint32_t HoveredRGBAColor, std::uint32_t HoveredRGBABackground, std::uint32_t HoveredRGBABorder) :ButtonSettings(ButtonText, OnClick, Xpos, Ypos, Width, Height, CharHeight, RGBAColor, gRGBAColor, BasePoint, GradPoint, BorderWidth, RGBABackground, RGBABorder, HoveredRGBAColor, HoveredRGBABackground, HoveredRGBABorder, NULL, " ") {}
	ButtonSettings(SingleTextLineSettings* ButtonTextSTLS, float Xpos, float Ypos, float Width, float Height, std::uint8_t BorderWidth, std::uint32_t RGBABackground, std::uint32_t RGBABorder, std::uint32_t HoveredRGBAColor, std::uint32_t HoveredRGBABackground, std::uint32_t HoveredRGBABorder) :ButtonSettings(" ", ButtonTextSTLS, NULL, Xpos, Ypos, Width, Height, BorderWidth, RGBABackground, RGBABorder, HoveredRGBAColor, HoveredRGBABackground, HoveredRGBABorder, NULL, " ") {}
	ButtonSettings(float Xpos, float Ypos, float Width, float Height, float CharHeight, std::uint32_t RGBAColor, std::uint8_t BorderWidth, std::uint32_t RGBABackground, std::uint32_t RGBABorder, std::uint32_t HoveredRGBAColor, std::uint32_t HoveredRGBABackground, std::uint32_t HoveredRGBABorder) :ButtonSettings(" ", NULL, Xpos, Ypos, Width, Height, CharHeight, RGBAColor, 0, 15, 15, BorderWidth, RGBABackground, RGBABorder, HoveredRGBAColor, HoveredRGBABackground, HoveredRGBABorder, NULL, " ") {}
	ButtonSettings(Button* Example, bool KeepText = false)
	{
		this->STLSBasedSettings = 1;
		this->ButtonText = (KeepText) ? Example->STL->_CurrentText : " ";
		if (!this->ButtonText.size())this->ButtonText = " ";
		
		this->TipText = " ";
		this->Tip = NULL;
		
		this->OnClick = Example->OnClick;
		this->Xpos = Example->Xpos;
		this->Ypos = Example->Ypos;
		this->Width = Example->Width;
		this->Height = Example->Height;
		this->HoveredRGBAColor = Example->HoveredRGBAColor;
		this->RGBAColor = Example->RGBAColor;
		this->STLS = new SingleTextLineSettings(Example->STL);
		this->gRGBAColor = this->STLS->gRGBAColor;
		this->BorderWidth = Example->BorderWidth;
		this->RGBABackground = Example->RGBABackground;
		this->RGBABorder = Example->RGBABorder;
		this->HoveredRGBABackground = Example->HoveredRGBABackground;
		this->HoveredRGBABorder = Example->HoveredRGBABorder;
	}
	void Move(float dx, float dy)
	{
		Xpos += dx;
		Ypos += dy;
	}
	void ChangePosition(float NewX, float NewY)
	{
		NewX -= Xpos;
		NewY -= Ypos;
		Move(NewX, NewY);
	}
	Button* CreateOne(const std::string& ButtonText, bool KeepText = false) 
	{
		if (STLS && STLSBasedSettings)
			return new Button(((KeepText) ? this->ButtonText : ButtonText), STLS, OnClick, Xpos, Ypos, Width, Height, BorderWidth, RGBABackground, RGBABorder, HoveredRGBAColor, HoveredRGBABackground, HoveredRGBABorder, Tip, TipText);
		else
			return new Button(((KeepText) ? this->ButtonText : ButtonText), OnClick, Xpos, Ypos, Width, Height, CharHeight, RGBAColor, gRGBAColor, BasePoint, GradPoint, BorderWidth, RGBABackground, RGBABorder, HoveredRGBAColor, HoveredRGBABackground, HoveredRGBABorder, Tip, TipText);
	}
};

#endif // !SAFGUIF_BS
