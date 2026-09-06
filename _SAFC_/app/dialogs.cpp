#include "app_state.h"
#include "dialogs.h"

// Keep the native dialog calls on the GUI thread and queue only the selected
// file work. OFN_ENABLEHOOK opts these APIs out of the current Windows dialog.
std::vector<std::wstring> multiple_open_file_dialog(const wchar_t* Title)
{
	OPENFILENAME ofn;       // common dialog box structure
	wchar_t szFile[50000];       // buffer for file name
	std::vector<std::wstring> InpLinks;
	ZeroMemory(&ofn, sizeof(ofn));
	ZeroMemory(szFile, 50000 * sizeof(wchar_t));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = hWnd;
	ofn.lpstrFile = szFile;
	ofn.lpstrFile[0] = '\0';
	ofn.nMaxFile = static_cast<DWORD>(std::size(szFile));
	ofn.lpstrFilter = L"MIDI files(*.mid)\0*.mid\0";
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = NULL;
	ofn.lpstrTitle = Title;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = NULL;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_ALLOWMULTISELECT |
		OFN_EXPLORER;
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
			case CDERR_DIALOGFAILURE:		 throw_alert_error("CDERR_DIALOGFAILURE\n");   break;
			case CDERR_FINDRESFAILURE:		 throw_alert_error("CDERR_FINDRESFAILURE\n");  break;
			case CDERR_INITIALIZATION:	 throw_alert_error("CDERR_INITIALIZATION\n"); break;
			case CDERR_LOADRESFAILURE:	 throw_alert_error("CDERR_LOADRESFAILURE\n"); break;
			case CDERR_LOADSTRFAILURE:	 throw_alert_error("CDERR_LOADSTRFAILURE\n"); break;
			case CDERR_LOCKRESFAILURE:	 throw_alert_error("CDERR_LOCKRESFAILURE\n"); break;
			case CDERR_MEMALLOCFAILURE:	 throw_alert_error("CDERR_MEMALLOCFAILURE\n"); break;
			case CDERR_MEMLOCKFAILURE:	 throw_alert_error("CDERR_MEMLOCKFAILURE\n"); break;
			case CDERR_NOHINSTANCE:		 throw_alert_error("CDERR_NOHINSTANCE\n"); break;
			case CDERR_NOHOOK:			 throw_alert_error("CDERR_NOHOOK\n"); break;
			case CDERR_NOTEMPLATE:		 throw_alert_error("CDERR_NOTEMPLATE\n"); break;
			case CDERR_STRUCTSIZE:		 throw_alert_error("CDERR_STRUCTSIZE\n"); break;
			case FNERR_BUFFERTOOSMALL:	 throw_alert_error("FNERR_BUFFERTOOSMALL\n"); break;
			case FNERR_INVALIDFILENAME:	 throw_alert_error("FNERR_INVALIDFILENAME\n"); break;
			case FNERR_SUBCLASSFAILURE:	 throw_alert_error("FNERR_SUBCLASSFAILURE\n"); break;
		}
		return std::vector<std::wstring>{L""};
	}
}

std::wstring playback_source_open_file_dialog()
{
	wchar_t filename[MAX_PATH]{};
	OPENFILENAME ofn{};
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = nullptr;
	ofn.lpstrFile = filename;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrFilter =
		L"MIDI and archive files\0*.mid;*.midi;*.xz;*.zip;*.7z;*.gz;*.bz2\0"
		L"All files\0*.*\0";
	ofn.nFilterIndex = 1;
	ofn.lpstrTitle = L"Open MIDI or archive";
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_EXPLORER;
	if (GetOpenFileName(&ofn))
		return filename;
	return {};
}

std::wstring syncore_bank_open_file_dialog()
{
	wchar_t filename[MAX_PATH]{};
	OPENFILENAME ofn{};
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = hWnd;
	ofn.lpstrFile = filename;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrFilter =
		L"SoundFont and SFZ banks\0*.sf2;*.sfz\0"
		L"SoundFont banks\0*.sf2\0"
		L"SFZ banks\0*.sfz\0";
	ofn.nFilterIndex = 1;
	ofn.lpstrTitle = L"Choose a bank for embedded SYNCore";
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_EXPLORER;
	if (GetOpenFileName(&ofn))
		return filename;
	return {};
}

std::wstring save_open_file_dialog(const wchar_t* Title)
{
	wchar_t filename[MAX_PATH];
	OPENFILENAME ofn;
	ZeroMemory(&filename, sizeof(filename));
	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = hWnd;
	ofn.lpstrFilter = L"MIDI files(*.mid)\0*.mid\0";
	ofn.lpstrDefExt = L"mid";
	ofn.lpstrFile = filename;
	ofn.nMaxFile = MAX_PATH;
	ofn.lpstrTitle = Title;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_EXPLORER | OFN_NOREADONLYRETURN |
		OFN_HIDEREADONLY;
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = NULL;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = NULL;
	if (GetSaveFileName(&ofn)) return std::wstring(filename);
	else
	{
		switch (CommDlgExtendedError())
		{
			case CDERR_DIALOGFAILURE:		 throw_alert_error("CDERR_DIALOGFAILURE\n");   break;
			case CDERR_FINDRESFAILURE:		 throw_alert_error("CDERR_FINDRESFAILURE\n");  break;
			case CDERR_INITIALIZATION:	 throw_alert_error("CDERR_INITIALIZATION\n"); break;
			case CDERR_LOADRESFAILURE:	 throw_alert_error("CDERR_LOADRESFAILURE\n"); break;
			case CDERR_LOADSTRFAILURE:	 throw_alert_error("CDERR_LOADSTRFAILURE\n"); break;
			case CDERR_LOCKRESFAILURE:	 throw_alert_error("CDERR_LOCKRESFAILURE\n"); break;
			case CDERR_MEMALLOCFAILURE:	 throw_alert_error("CDERR_MEMALLOCFAILURE\n"); break;
			case CDERR_MEMLOCKFAILURE:	 throw_alert_error("CDERR_MEMLOCKFAILURE\n"); break;
			case CDERR_NOHINSTANCE:		 throw_alert_error("CDERR_NOHINSTANCE\n"); break;
			case CDERR_NOHOOK:			 throw_alert_error("CDERR_NOHOOK\n"); break;
			case CDERR_NOTEMPLATE:		 throw_alert_error("CDERR_NOTEMPLATE\n"); break;
			case CDERR_STRUCTSIZE:		 throw_alert_error("CDERR_STRUCTSIZE\n"); break;
			case FNERR_BUFFERTOOSMALL:	 throw_alert_error("FNERR_BUFFERTOOSMALL\n"); break;
			case FNERR_INVALIDFILENAME:	 throw_alert_error("FNERR_INVALIDFILENAME\n"); break;
			case FNERR_SUBCLASSFAILURE:	 throw_alert_error("FNERR_SUBCLASSFAILURE\n"); break;
		}
		return L"";
	}
}
