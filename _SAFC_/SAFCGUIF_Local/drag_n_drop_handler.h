#pragma once
#ifndef SAFGUIF_L_DND
#define SAFGUIF_L_DND

// #include <dwmapi.h>
#include <Windows.h>
// #include "../SAFGUIF/SAFGUIF.h"
// #include "../SAFC_InnerModules/include_all.h"

#include <vector>

#pragma comment (lib, "dwmapi.lib")

struct drag_n_drop_handler : IDropTarget
{
	ULONG ref_count;
	wchar_t buffer[8000];

	drag_n_drop_handler()
	{
		ref_count = 1;
	}

	HRESULT STDMETHODCALLTYPE QueryInterface(const IID& riid, void** ppv_object) override
	{
		if (riid == IID_IUnknown || riid == IID_IDropTarget)
		{
			*ppv_object = this;
			AddRef();
			return NOERROR;
		}

		*ppv_object = NULL;
		return ResultFromScode(E_NOINTERFACE);
	}

	ULONG STDMETHODCALLTYPE AddRef() override
	{
		return ++ref_count;
	}

	ULONG STDMETHODCALLTYPE Release() override
	{
		if (--ref_count == 0)
		{
			delete this;
			return 0;
		}
		return ref_count;
	}

	HRESULT STDMETHODCALLTYPE DragEnter(IDataObject*, DWORD, POINTL, DWORD* effect) override
	{
		//MessageBox(hWnd, "dragenter", "Drag", MB_ICONINFORMATION);
		//cout << "a" << endl;
		DRAG_OVER = 1;
		*effect = DROPEFFECT_COPY;
		return NOERROR;
	}

	HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL, DWORD* effect) override
	{
		//MessageBox(hWnd, "dragover", "Drag", MB_ICONINFORMATION);
		*effect = DROPEFFECT_COPY;
		return NOERROR;
	}

	HRESULT STDMETHODCALLTYPE DragLeave() override
	{
		//MessageBox(hWnd, "dragleave", "Drag", MB_ICONINFORMATION);
		DRAG_OVER = 0;
		return NOERROR;
	}

	HRESULT STDMETHODCALLTYPE Drop(IDataObject* dataObject, DWORD keyState, POINTL mousePos, DWORD* effect) override
	{
		//MessageBox(hWnd, "drop", "Drag", MB_ICONINFORMATION);
		//cout << "drop " << ref_count << endl;

		FORMATETC fdrop = { CF_HDROP, 0, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
		std::vector<std::wstring> wide_char_buffer(1, L"");

		if (SUCCEEDED(dataObject->QueryGetData(&fdrop)))
		{
			STGMEDIUM stg_medium = { 0 };
			stg_medium.tymed = TYMED_HGLOBAL;
			HRESULT hresult = dataObject->GetData(&fdrop, &stg_medium);
			if (SUCCEEDED(hresult))
			{
				PVOID data = GlobalLock(stg_medium.hGlobal);
				if (!data)
					return NOERROR;

				for (int i = *((BYTE*)(data));; i += 2)
				{
					wide_char_buffer.back().push_back(((*((BYTE*)(data)+i + 1)) << 8) | (*((BYTE*)(data)+i)));
					if (wide_char_buffer.back().back() == 0 && wide_char_buffer.back().size() > 1)
					{
						wide_char_buffer.back().pop_back();
						wide_char_buffer.push_back(L"");
						continue;
					}
					else if (wide_char_buffer.back().back() == 0)
					{
						wide_char_buffer.pop_back();
						break;
					}
				}

				add_files(wide_char_buffer);
			}
			else
				throw_alert_error("drag_and_drop: GetData");
		}
		else
			throw_alert_error("drag_and_drop: QueryGetData");

		DRAG_OVER = 0;
		*effect = DROPEFFECT_COPY;
		return NOERROR;
	}
};

drag_n_drop_handler global_drag_and_drop_handler;

#endif // !SAFGUIF_L_DND
