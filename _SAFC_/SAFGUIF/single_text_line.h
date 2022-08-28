#pragma once
#ifndef SAFGUIF_STL 
#define SAFGUIF_STL

#include "header_utils.h"
#include "symbols.h"

struct SingleTextLine 
{
	std::string _CurrentText;
	float CXpos, CYpos;
	float SpaceWidth;
	std::uint32_t RGBAColor, gRGBAColor;
	float CalculatedWidth, CalculatedHeight;
	bool isBicolored, isListedFont;
	float _XUnitSize, _YUnitSize;
	std::vector<DottedSymbol*> Chars;
	~SingleTextLine() 
	{
		for (auto i = Chars.begin(); i != Chars.end(); i++)
			if (*i)delete* i;
		Chars.clear();
	}
	SingleTextLine(std::string Text, float CXpos, float CYpos, float XUnitSize, float YUnitSize, float SpaceWidth, std::uint8_t LineWidth = 2, std::uint32_t RGBAColor = 0xFFFFFFFF, std::uint32_t* RGBAGradColor = NULL, std::uint8_t OrigNGradPoints = ((5 << 4) | 5), bool isListedFont = false) 
	{
		if (!Text.size())Text = " ";
		this->_CurrentText = Text;
		CalculatedHeight = 2 * YUnitSize;
		CalculatedWidth = Text.size() * 2.f * XUnitSize + (Text.size() - 1) * SpaceWidth;
		this->CXpos = CXpos;
		this->CYpos = CYpos;
		this->RGBAColor = RGBAColor;
		this->SpaceWidth = SpaceWidth;
		this->_XUnitSize = XUnitSize;
		this->_YUnitSize = YUnitSize;
		this->isListedFont = isListedFont;
		float CharXPosition = CXpos - (CalculatedWidth * 0.5) + XUnitSize, CharXPosIncrement = 2.f * XUnitSize + SpaceWidth;
		for (int i = 0; i < Text.size(); i++)
		{
			if (!RGBAGradColor && !isListedFont)Chars.push_back(
				new DottedSymbol(Text[i], CharXPosition, CYpos, XUnitSize, YUnitSize, LineWidth, new std::uint32_t(RGBAColor))
				);
			else if (!isListedFont) Chars.push_back(
				new BiColoredDottedSymbol(Text[i], CharXPosition, CYpos, XUnitSize, YUnitSize, LineWidth, new std::uint32_t(RGBAColor), new std::uint32_t(*RGBAGradColor), (std::uint8_t)(OrigNGradPoints >> 4), (std::uint8_t)(OrigNGradPoints & 0xF))
				);
			else Chars.push_back(
				new lFontSymbol(Text[i], CharXPosition, CYpos, XUnitSize, YUnitSize, RGBAColor)
				);
			CharXPosition += CharXPosIncrement;
		}
		if (RGBAGradColor)
		{
			this->isBicolored = true;
			this->gRGBAColor = *RGBAGradColor;
			delete RGBAGradColor;
		}
		else
			this->isBicolored = false;
	}
	void SafeColorChange(std::uint32_t NewRGBAColor)
	{
		if (isBicolored)
		{
			SafeColorChange(NewRGBAColor, gRGBAColor,
				((BiColoredDottedSymbol*)(this->Chars.front()))->_PointData >> 4,
				((BiColoredDottedSymbol*)(this->Chars.front()))->_PointData & 0xF
				);
			return;
		}
		std::uint8_t R = (NewRGBAColor >> 24), G = (NewRGBAColor >> 16) & 0xFF, B = (NewRGBAColor >> 8) & 0xFF, A = (NewRGBAColor) & 0xFF;
		RGBAColor = NewRGBAColor;
		for (int i = 0; i < Chars.size(); i++)
		{
			Chars[i]->R = R;
			Chars[i]->G = G;
			Chars[i]->B = B;
			Chars[i]->A = A;
		}
	}
	void SafeColorChange(std::uint32_t NewBaseRGBAColor, std::uint32_t NewGRGBAColor, std::uint8_t BasePoint, std::uint8_t gPoint)
	{
		if (!isBicolored)
			return SafeColorChange(NewBaseRGBAColor);
		for (int i = 0; i < Chars.size(); i++)
			Chars[i]->RefillGradient(new std::uint32_t(NewBaseRGBAColor), new std::uint32_t(NewGRGBAColor), BasePoint, gPoint);
	}
	void SafeChangePosition(float NewCXPos, float NewCYPos)
	{
		NewCXPos = NewCXPos - CXpos;
		NewCYPos = NewCYPos - CYpos;
		SafeMove(NewCXPos, NewCYPos);
	}
	void SafeMove(float dx, float dy)
	{
		CXpos += dx;
		CYpos += dy;
		for (int i = 0; i < Chars.size(); i++)
			Chars[i]->SafeCharMove(dx, dy);
	}
	bool SafeReplaceChar(int i, char CH)
	{
		if (i >= Chars.size()) return 0;
		if (isListedFont)
		{
			((lFontSymbol*)Chars[i])->Symb = CH;
		}
		else
		{
			Chars[i]->RenderWay = ASCII[CH];
			Chars[i]->UpdatePointPlacementPositions();
		}
		return 1;
	}
	bool SafeReplaceChar(int i, std::string CHrenderway)
	{
		if (i >= Chars.size())return 0;
		if (isListedFont) return 0;
		Chars[i]->RenderWay = CHrenderway;
		Chars[i]->UpdatePointPlacementPositions();
		return 1;
	}
	void RecalculateWidth()
	{
		CalculatedWidth = Chars.size() * (2.f * _XUnitSize) + (Chars.size() - 1) * SpaceWidth;
		float CharXPosition = CXpos - (CalculatedWidth * 0.5f) + _XUnitSize, CharXPosIncrement = 2.f * _XUnitSize + SpaceWidth;
		for (int i = 0; i < Chars.size(); i++)
		{
			Chars[i]->Xpos = CharXPosition;
			CharXPosition += CharXPosIncrement;
		}
	}
	void SafeChangePosition_Argumented(std::uint8_t Arg, float newX, float newY)
	{
		///STL_CHANGE_POSITION_ARGUMENT_LEFT
		float CW = 0.5f * (
			(INT32)((bool)(GLOBAL_LEFT & Arg))
			- (INT32)((bool)(GLOBAL_RIGHT & Arg))
			) * CalculatedWidth,
			CH = 0.5f * (
				(INT32)((bool)(GLOBAL_BOTTOM & Arg))
				- (INT32)((bool)(GLOBAL_TOP & Arg))
				) * CalculatedHeight;
		SafeChangePosition(newX + CW, newY + CH);
	}
	void SafeStringReplace(std::string NewString)
	{
		if (!NewString.size()) NewString = " ";
		_CurrentText = NewString;
		while (NewString.size() > Chars.size())
		{
			if (isBicolored)
				Chars.push_back(new BiColoredDottedSymbol((*((BiColoredDottedSymbol*)(Chars.front())))));
			else if (isListedFont)
				Chars.push_back(new lFontSymbol(*((lFontSymbol*)(Chars.front()))));
			else
				Chars.push_back(new DottedSymbol(*(Chars.front())));
		}
		while (NewString.size() < Chars.size())
		{
			delete Chars.back();
			Chars.pop_back();
		}
		for (int i = 0; i < Chars.size(); i++)
			SafeReplaceChar(i, NewString[i]);
		RecalculateWidth();
	}
	void Draw() {
		for (int i = 0; i < Chars.size(); i++) 
		{
			Chars[i]->Draw();
		}
	}
};

#endif