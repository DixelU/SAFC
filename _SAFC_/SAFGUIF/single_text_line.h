#pragma once
#ifndef SAFGUIF_STL 
#define SAFGUIF_STL

#include "header_utils.h"
#include "symbols.h"

struct SingleTextLine {
	std::string _CurrentText;
	float CXpos, CYpos;
	float SpaceWidth;
	DWORD RGBAColor, gRGBAColor;
	float CalculatedWidth, CalculatedHeight;
	BIT isBicolored, isListedFont;
	float _XUnitSize, _YUnitSize;
	std::vector<DottedSymbol*> Chars;
	~SingleTextLine() {
		for (auto i = Chars.begin(); i != Chars.end(); i++)
			if (*i)delete* i;
		Chars.clear();
	}
	SingleTextLine(std::string Text, float CXpos, float CYpos, float XUnitSize, float YUnitSize, float SpaceWidth, BYTE LineWidth = 2, DWORD RGBAColor = 0xFFFFFFFF, DWORD* RGBAGradColor = NULL, BYTE OrigNGradPoints = ((5 << 4) | 5), bool isListedFont = false) {
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
		for (int i = 0; i < Text.size(); i++) {
			if (!RGBAGradColor && !isListedFont)Chars.push_back(
				new DottedSymbol(Text[i], CharXPosition, CYpos, XUnitSize, YUnitSize, LineWidth, new DWORD(RGBAColor))
				);
			else if (!isListedFont) Chars.push_back(
				new BiColoredDottedSymbol(Text[i], CharXPosition, CYpos, XUnitSize, YUnitSize, LineWidth, new DWORD(RGBAColor), new DWORD(*RGBAGradColor), (BYTE)(OrigNGradPoints >> 4), (BYTE)(OrigNGradPoints & 0xF))
				);
			else Chars.push_back(
				new lFontSymbol(Text[i], CharXPosition, CYpos, XUnitSize, YUnitSize, RGBAColor)
				);
			CharXPosition += CharXPosIncrement;
		}
		if (RGBAGradColor) {
			this->isBicolored = true;
			this->gRGBAColor = *RGBAGradColor;
			delete RGBAGradColor;
		}
		else
			this->isBicolored = false;
	}
	void SafeColorChange(DWORD NewRGBAColor) {
		if (isBicolored) {
			SafeColorChange(NewRGBAColor, gRGBAColor,
				((BiColoredDottedSymbol*)(this->Chars.front()))->_PointData >> 4,
				((BiColoredDottedSymbol*)(this->Chars.front()))->_PointData & 0xF
				);
			return;
		}
		BYTE R = (NewRGBAColor >> 24), G = (NewRGBAColor >> 16) & 0xFF, B = (NewRGBAColor >> 8) & 0xFF, A = (NewRGBAColor) & 0xFF;
		RGBAColor = NewRGBAColor;
		for (int i = 0; i < Chars.size(); i++) {
			Chars[i]->R = R;
			Chars[i]->G = G;
			Chars[i]->B = B;
			Chars[i]->A = A;
		}
	}
	void SafeColorChange(DWORD NewBaseRGBAColor, DWORD NewGRGBAColor, BYTE BasePoint, BYTE gPoint) {
		if (!isBicolored) {
			SafeColorChange(NewBaseRGBAColor);
			return;
		}
		for (int i = 0; i < Chars.size(); i++) {
			Chars[i]->RefillGradient(new DWORD(NewBaseRGBAColor), new DWORD(NewGRGBAColor), BasePoint, gPoint);
		}
	}
	void SafeChangePosition(float NewCXPos, float NewCYPos) {
		NewCXPos = NewCXPos - CXpos;
		NewCYPos = NewCYPos - CYpos;
		SafeMove(NewCXPos, NewCYPos);
	}
	void SafeMove(float dx, float dy) {
		CXpos += dx;
		CYpos += dy;
		for (int i = 0; i < Chars.size(); i++)
			Chars[i]->SafeCharMove(dx, dy);
	}
	BIT SafeReplaceChar(int i, char CH) {
		if (i >= Chars.size())return 0;
		if (isListedFont) {
			((lFontSymbol*)Chars[i])->Symb = CH;
		}
		else {
			Chars[i]->RenderWay = ASCII[CH];
			Chars[i]->UpdatePointPlacementPositions();
		}
		return 1;
	}
	BIT SafeReplaceChar(int i, std::string CHrenderway) {
		if (i >= Chars.size())return 0;
		if (isListedFont) return 0;
		Chars[i]->RenderWay = CHrenderway;
		Chars[i]->UpdatePointPlacementPositions();
		return 1;
	}
	void RecalculateWidth() {
		CalculatedWidth = Chars.size() * (2.f * _XUnitSize) + (Chars.size() - 1) * SpaceWidth;
		float CharXPosition = CXpos - (CalculatedWidth * 0.5f) + _XUnitSize, CharXPosIncrement = 2.f * _XUnitSize + SpaceWidth;
		for (int i = 0; i < Chars.size(); i++) {
			Chars[i]->Xpos = CharXPosition;
			CharXPosition += CharXPosIncrement;
		}
	}
	void SafeChangePosition_Argumented(BYTE Arg, float newX, float newY) {
		///STL_CHANGE_POSITION_ARGUMENT_LEFT
		float CW = 0.5f * (
			(INT32)((BIT)(GLOBAL_LEFT & Arg))
			- (INT32)((BIT)(GLOBAL_RIGHT & Arg))
			) * CalculatedWidth,
			CH = 0.5f * (
				(INT32)((BIT)(GLOBAL_BOTTOM & Arg))
				- (INT32)((BIT)(GLOBAL_TOP & Arg))
				) * CalculatedHeight;
		SafeChangePosition(newX + CW, newY + CH);
	}
	void SafeStringReplace(std::string NewString) {
		if (!NewString.size()) NewString = " ";
		_CurrentText = NewString;
		while (NewString.size() > Chars.size()) {
			if (isBicolored)
				Chars.push_back(new BiColoredDottedSymbol((*((BiColoredDottedSymbol*)(Chars.front())))));
			else if (isListedFont)
				Chars.push_back(new lFontSymbol(*((lFontSymbol*)(Chars.front()))));
			else
				Chars.push_back(new DottedSymbol(*(Chars.front())));
		}
		while (NewString.size() < Chars.size()) {
			delete Chars.back();
			Chars.pop_back();
		}
		for (int i = 0; i < Chars.size(); i++)
			SafeReplaceChar(i, NewString[i]);
		RecalculateWidth();
	}
	void Draw() {
		for (int i = 0; i < Chars.size(); i++) {
			Chars[i]->Draw();
		}
	}
};

#endif