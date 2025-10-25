#pragma once

#include "../core/header_utils.h"

#include <sstream>
#include <cstdio>
#include <log_c/log.h>

#ifdef _MSC_VER
#include <commdlg.h>

std::vector<std_unicode_string> OpenFileDlg(const cchar_t* Title)
{
	OPENFILENAME ofn;
	cchar_t szFile[50000];
	std::vector<std_unicode_string> InpLinks;
	ZeroMemory(&ofn, sizeof(ofn));
	ZeroMemory(szFile, 50000 * sizeof(cchar_t));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = NULL;
	ofn.lpstrFile = szFile;
	ofn.lpstrFile[0] = '\0';
	ofn.nMaxFile = sizeof(szFile);
	ofn.lpstrFilter = to_cchar_t("MIDI Files(*.mid)\0*.mid\0");
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
			case CDERR_DIALOGFAILURE:	log_error("CDERR_DIALOGFAILURE\n");   break;
			case CDERR_FINDRESFAILURE:	log_error("CDERR_FINDRESFAILURE\n");  break;
			case CDERR_INITIALIZATION:	log_error("CDERR_INITIALIZATION\n"); break;
			case CDERR_LOADRESFAILURE:	log_error("CDERR_LOADRESFAILURE\n"); break;
			case CDERR_LOADSTRFAILURE:	log_error("CDERR_LOADSTRFAILURE\n"); break;
			case CDERR_LOCKRESFAILURE:	log_error("CDERR_LOCKRESFAILURE\n"); break;
			case CDERR_MEMALLOCFAILURE:	log_error("CDERR_MEMALLOCFAILURE\n"); break;
			case CDERR_MEMLOCKFAILURE:	log_error("CDERR_MEMLOCKFAILURE\n"); break;
			case CDERR_NOHINSTANCE:		log_error("CDERR_NOHINSTANCE\n"); break;
			case CDERR_NOHOOK:		    log_error("CDERR_NOHOOK\n"); break;
			case CDERR_NOTEMPLATE:		log_error("CDERR_NOTEMPLATE\n"); break;
			case CDERR_STRUCTSIZE:		log_error("CDERR_STRUCTSIZE\n"); break;
			case FNERR_BUFFERTOOSMALL:	log_error("FNERR_BUFFERTOOSMALL\n"); break;
			case FNERR_INVALIDFILENAME:	log_error("FNERR_INVALIDFILENAME\n"); break;
			case FNERR_SUBCLASSFAILURE:	log_error("FNERR_SUBCLASSFAILURE\n"); break;
		}
		return std::vector<std::wstring>{L""};
	}
}

std::wstring SaveFileDlg(const cchar_t* Title)
{
	cchar_t filename[MAX_PATH];
	OPENFILENAME ofn;
	ZeroMemory(&filename, sizeof(filename));
	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = NULL;  // If you have a window to center over, put its HANDLE here
	ofn.lpstrFilter = to_cchar_t("MIDI Files(*.mid)\0*.mid\0");
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
			case CDERR_DIALOGFAILURE:	log_error("CDERR_DIALOGFAILURE\n");   break;
			case CDERR_FINDRESFAILURE:	log_error("CDERR_FINDRESFAILURE\n");  break;
			case CDERR_INITIALIZATION:	log_error("CDERR_INITIALIZATION\n"); break;
			case CDERR_LOADRESFAILURE:	log_error("CDERR_LOADRESFAILURE\n"); break;
			case CDERR_LOADSTRFAILURE:	log_error("CDERR_LOADSTRFAILURE\n"); break;
			case CDERR_LOCKRESFAILURE:	log_error("CDERR_LOCKRESFAILURE\n"); break;
			case CDERR_MEMALLOCFAILURE:	log_error("CDERR_MEMALLOCFAILURE\n"); break;
			case CDERR_MEMLOCKFAILURE:	log_error("CDERR_MEMLOCKFAILURE\n"); break;
			case CDERR_NOHINSTANCE:		log_error("CDERR_NOHINSTANCE\n"); break;
			case CDERR_NOHOOK:		    log_error("CDERR_NOHOOK\n"); break;
			case CDERR_NOTEMPLATE:		log_error("CDERR_NOTEMPLATE\n"); break;
			case CDERR_STRUCTSIZE:		log_error("CDERR_STRUCTSIZE\n"); break;
			case FNERR_BUFFERTOOSMALL:	log_error("FNERR_BUFFERTOOSMALL\n"); break;
			case FNERR_INVALIDFILENAME:	log_error("FNERR_INVALIDFILENAME\n"); break;
			case FNERR_SUBCLASSFAILURE:	log_error("FNERR_SUBCLASSFAILURE\n"); break;
		}
		return L"";
	}
}
#else

// The stuff on linux
std::vector<std_unicode_string> OpenFileDlg(const cchar_t* Title)
{
    std::ostringstream zenity_cmd;
    std::vector<std_unicode_string> files;
    zenity_cmd << "zenity --file-selection --multiple --file-filter='.mid' --title='" << Title << "'";
    
    FILE* pipe = popen(zenity_cmd.str().c_str(), "r");
    if(!pipe)
    {
        log_error("popen() Failed\n");
        return {};
    }
    
    char buffer[256];
    std::string files_raw_str;
    while(fgets(buffer, sizeof(buffer), pipe) != nullptr)
        files_raw_str += buffer;
    
    std::stringstream ss(files_raw_str);
    std::string file_path;
    
    while(std::getline(ss, file_path, '|'))
        files.push_back(file_path);
    
    pclose(pipe);
    
    
    return files;
};

std_unicode_string SaveFileDlg(const cchar_t* Title)
{ 
    std::ostringstream zenity_cmd;
    zenity_cmd << "zenity --file-selection --save --file-filter='.mid' --title='" << Title << "'";
    
    FILE* pipe = popen(zenity_cmd.str().c_str(), "r");
    if(!pipe)
    {
        log_error("popen() Failed\n");
        return "";
    }
    
    char buffer[256];
    std::string save_file_path;
    while(fgets(buffer, sizeof(buffer), pipe) != nullptr)
    {
        std::string line(buffer);
        if(!line.empty() && line.back() == '\n')
            line.pop_back();  // remove newline
        save_file_path += line;
    }
    
    return save_file_path;
}

#endif