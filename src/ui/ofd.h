#pragma once

#include <vector>
#include <string>
#include <commdlg.h>

std::vector<std_unicode_string> MOFD(const wchar_t* Title)
{
	OPENFILENAME ofn;
	wchar_t szFile[50000];
	std::vector<std::wstring> InpLinks;
	ZeroMemory(&ofn, sizeof(ofn));
	ZeroMemory(szFile, 50000 * sizeof(wchar_t));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = NULL;
	ofn.lpstrFile = szFile;
	ofn.lpstrFile[0] = '\0';
	ofn.nMaxFile = sizeof(szFile);
	ofn.lpstrFilter = L"MIDI Files(*.mid)\0*.mid\0";
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = NULL;
	ofn.lpstrTitle = Title;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = NULL;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT | OFN_EXPLORER;

	if (GetOpenFileName(&ofn))
	{
		std::wstring Link = L"", Gen = L"";
		int i = 0, counter = 0;
		for (; i < 600 && szFile[i] != '\0'; i++)
			Link.push_back(szFile[i]);

		for (; i < 49998;)
		{
			counter++;
			Gen.clear();
			for (; i < 49998 && szFile[i] != '\0'; i++)
				Gen.push_back(szFile[i]);
			i++;
			if (szFile[i] == '\0')
			{
				if (counter == 1) InpLinks.push_back(Link);
				else InpLinks.push_back(Link + L"\\" + Gen);
				break;
			}
			else if (Gen != L"")InpLinks.push_back(Link + L"\\" + Gen);
		}
		return InpLinks;
	}
	else
	{
		switch (CommDlgExtendedError())
		{
			case CDERR_DIALOGFAILURE:	ThrowAlert_Error("CDERR_DIALOGFAILURE\n");   break;
			case CDERR_FINDRESFAILURE:	ThrowAlert_Error("CDERR_FINDRESFAILURE\n");  break;
			case CDERR_INITIALIZATION:	ThrowAlert_Error("CDERR_INITIALIZATION\n"); break;
			case CDERR_LOADRESFAILURE:	ThrowAlert_Error("CDERR_LOADRESFAILURE\n"); break;
			case CDERR_LOADSTRFAILURE:	ThrowAlert_Error("CDERR_LOADSTRFAILURE\n"); break;
			case CDERR_LOCKRESFAILURE:	ThrowAlert_Error("CDERR_LOCKRESFAILURE\n"); break;
			case CDERR_MEMALLOCFAILURE:	ThrowAlert_Error("CDERR_MEMALLOCFAILURE\n"); break;
			case CDERR_MEMLOCKFAILURE:	ThrowAlert_Error("CDERR_MEMLOCKFAILURE\n"); break;
			case CDERR_NOHINSTANCE:		ThrowAlert_Error("CDERR_NOHINSTANCE\n"); break;
			case CDERR_NOHOOK:		ThrowAlert_Error("CDERR_NOHOOK\n"); break;
			case CDERR_NOTEMPLATE:		ThrowAlert_Error("CDERR_NOTEMPLATE\n"); break;
			case CDERR_STRUCTSIZE:		ThrowAlert_Error("CDERR_STRUCTSIZE\n"); break;
			case FNERR_BUFFERTOOSMALL:	ThrowAlert_Error("FNERR_BUFFERTOOSMALL\n"); break;
			case FNERR_INVALIDFILENAME:	ThrowAlert_Error("FNERR_INVALIDFILENAME\n"); break;
			case FNERR_SUBCLASSFAILURE:	ThrowAlert_Error("FNERR_SUBCLASSFAILURE\n"); break;
		}
		return std::vector<std::wstring>{L""};
	}
}

std::wstring SOFD(const wchar_t* Title)
{
	wchar_t filename[MAX_PATH];
	OPENFILENAME ofn;
	ZeroMemory(&filename, sizeof(filename));
	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = NULL;  // If you have a window to center over, put its HANDLE here
	ofn.lpstrFilter = L"MIDI Files(*.mid)\0*.mid\0";
	ofn.lpstrFile = filename;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrTitle = Title;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_NOREADONLYRETURN | OFN_HIDEREADONLY;
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = NULL;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = NULL;
	if 
		(GetSaveFileName(&ofn)) return std::wstring(filename);
	else
	{
		switch (CommDlgExtendedError())
		{
			case CDERR_DIALOGFAILURE:	ThrowAlert_Error("CDERR_DIALOGFAILURE\n");   break;
			case CDERR_FINDRESFAILURE:	ThrowAlert_Error("CDERR_FINDRESFAILURE\n");  break;
			case CDERR_INITIALIZATION:	ThrowAlert_Error("CDERR_INITIALIZATION\n"); break;
			case CDERR_LOADRESFAILURE:	ThrowAlert_Error("CDERR_LOADRESFAILURE\n"); break;
			case CDERR_LOADSTRFAILURE:	ThrowAlert_Error("CDERR_LOADSTRFAILURE\n"); break;
			case CDERR_LOCKRESFAILURE:	ThrowAlert_Error("CDERR_LOCKRESFAILURE\n"); break;
			case CDERR_MEMALLOCFAILURE:	ThrowAlert_Error("CDERR_MEMALLOCFAILURE\n"); break;
			case CDERR_MEMLOCKFAILURE:	ThrowAlert_Error("CDERR_MEMLOCKFAILURE\n"); break;
			case CDERR_NOHINSTANCE:		ThrowAlert_Error("CDERR_NOHINSTANCE\n"); break;
			case CDERR_NOHOOK:		ThrowAlert_Error("CDERR_NOHOOK\n"); break;
			case CDERR_NOTEMPLATE:		ThrowAlert_Error("CDERR_NOTEMPLATE\n"); break;
			case CDERR_STRUCTSIZE:		ThrowAlert_Error("CDERR_STRUCTSIZE\n"); break;
			case FNERR_BUFFERTOOSMALL:	ThrowAlert_Error("FNERR_BUFFERTOOSMALL\n"); break;
			case FNERR_INVALIDFILENAME:	ThrowAlert_Error("FNERR_INVALIDFILENAME\n"); break;
			case FNERR_SUBCLASSFAILURE:	ThrowAlert_Error("FNERR_SUBCLASSFAILURE\n"); break;
		}
		return L"";
	}
}