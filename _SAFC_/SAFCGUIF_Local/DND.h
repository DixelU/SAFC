#pragma once
#ifndef SAFGUIF_L_DND
#define SAFGUIF_L_DND

// #include <dwmapi.h>
#include <Windows.h>
// #include "../SAFGUIF/SAFGUIF.h"
// #include "../SAFC_InnerModules/include_all.h"

#include <vector>

#pragma comment (lib, "dwmapi.lib")

struct DragNDropHandler : IDropTarget
{
	ULONG m_refCount;
	wchar_t m_data[8000];
	DragNDropHandler()
	{
		m_refCount = 1;
	}
	HRESULT STDMETHODCALLTYPE QueryInterface(const IID& riid, void** ppvObject) override
	{
		if (riid == IID_IUnknown || riid == IID_IDropTarget)
		{
			*ppvObject = this;
			AddRef();
			return NOERROR;
		}
		*ppvObject = NULL;
		return ResultFromScode(E_NOINTERFACE);
	}
	ULONG STDMETHODCALLTYPE AddRef() override
	{
		return ++m_refCount;
	}
	ULONG STDMETHODCALLTYPE Release() override
	{
		if (--m_refCount == 0)
		{
			delete this;
			return 0;
		}
		return m_refCount;
	}
	HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* dataObject, DWORD grfKeyState, POINTL mousePos, DWORD* effect) override
	{
		//MessageBox(hWnd, "dragenter", "Drag", MB_ICONINFORMATION);
		//cout << "a" << endl;
		DRAG_OVER = 1;
		*effect = DROPEFFECT_COPY;
		return NOERROR;
	}
	HRESULT STDMETHODCALLTYPE DragOver(DWORD keyState, POINTL mousePos, DWORD* effect) override
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
		//cout << "drop " << m_refCount << endl;

		FORMATETC fdrop = { CF_HDROP, 0, DVASPECT_CONTENT, -1, TYMED_HGLOBAL };
		std::vector<std::wstring> WC(1, L"");

		if (SUCCEEDED(dataObject->QueryGetData(&fdrop)))
		{
			STGMEDIUM stgMedium = { 0 };
			stgMedium.tymed = TYMED_HGLOBAL;
			HRESULT hr = dataObject->GetData(&fdrop, &stgMedium);
			if (SUCCEEDED(hr))
			{
				PVOID data = GlobalLock(stgMedium.hGlobal);
				if (!data)
					return NOERROR;

				for (int i = *((BYTE*)(data));; i += 2)
				{
					WC.back().push_back(((*((BYTE*)(data)+i + 1)) << 8) | (*((BYTE*)(data)+i)));
					if (WC.back().back() == 0 && WC.back().size() > 1)
					{
						WC.back().pop_back();
						WC.push_back(L"");
						continue;
					}
					else if (WC.back().back() == 0)
					{
						WC.pop_back();
						break;
					}
				}
				AddFiles(WC);
			}
			else
				ThrowAlert_Error("DND: GET_DATA");
		}
		else
			ThrowAlert_Error("DND: QUERY_GET_DATA");
		DRAG_OVER = 0;
		*effect = DROPEFFECT_COPY;
		return NOERROR;
	}
};

DragNDropHandler DNDH_Global;

#endif // !SAFGUIF_L_DND
