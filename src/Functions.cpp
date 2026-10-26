#include"pch.h"
#include <windows.h>
#include <shlwapi.h>
#include "CClassFactory.h"
#include"ClsId.h"
#include <stdio.h>
#include <string>


#pragma comment (lib, "Shlwapi.lib")

LONG g_lLocks = 0;
extern HINSTANCE g_hinstDll;

void LockModule(BOOL bLock)
{
	if (bLock)
		InterlockedIncrement(&g_lLocks);
	else
		InterlockedDecrement(&g_lLocks);
}

BOOL CreateRegistryKey(HKEY hKeyRoot, LPCWSTR lpszKey, LPCWSTR lpszValue, LPCWSTR lpszData)
{
	HKEY hKey;
	LONG lResult;
	DWORD dwSize;

	lResult = RegCreateKeyEx(hKeyRoot, lpszKey, 0, NULL, REG_OPTION_NON_VOLATILE, KEY_WRITE, NULL, &hKey, NULL);
	if (lResult != ERROR_SUCCESS)
		return FALSE;

	if (lpszData != NULL)
		dwSize = (lstrlen(lpszData) + 1) * sizeof(WCHAR);
	else
		dwSize = 0;

	lResult = RegSetValueEx(hKey, lpszValue, 0, REG_SZ, (LPBYTE)lpszData, dwSize);
	RegCloseKey(hKey);
	return lResult == ERROR_SUCCESS;
}

std::wstring UserClsidKey()
{
	return std::wstring(L"Software\\Classes\\CLSID\\") + g_szClsid;
}

STDAPI DllCanUnloadNow()
{
	return g_lLocks == 0 ? S_OK : S_FALSE;
}

STDAPI DllGetClassObject(REFCLSID rclsid, REFIID riid, LPVOID* ppv)
{
	static CClassFactory serverFactory;
	HRESULT hr;

	*ppv = NULL;

	if (IsEqualCLSID(rclsid, CLSID_WeatherBar))
		hr = serverFactory.QueryInterface(riid, ppv);
	else
		hr = CLASS_E_CLASSNOTAVAILABLE;

	return hr;
}

STDAPI DllRegisterServer(void)
{
	wchar_t modulePath[MAX_PATH];
	DWORD length = GetModuleFileNameW(g_hinstDll, modulePath, MAX_PATH);
	if (length == 0 || length >= MAX_PATH)
		return E_FAIL;

	std::wstring clsidKey = UserClsidKey();
	std::wstring serverKey = clsidKey + L"\\InprocServer32";
	std::wstring categoryKey = clsidKey + L"\\Implemented Categories\\{00021492-0000-0000-C000-000000000046}";
	if (!CreateRegistryKey(HKEY_CURRENT_USER, clsidKey.c_str(), NULL, L"WeatherBar"))
		return E_FAIL;
	if (!CreateRegistryKey(HKEY_CURRENT_USER, serverKey.c_str(), NULL, modulePath))
		return E_FAIL;
	if (!CreateRegistryKey(HKEY_CURRENT_USER, serverKey.c_str(), L"ThreadingModel", L"Apartment"))
		return E_FAIL;
	if (!CreateRegistryKey(HKEY_CURRENT_USER, categoryKey.c_str(), NULL, L""))
		return E_FAIL;
	return S_OK;
}

STDAPI DllUnregisterServer(void)
{
	LONG result = SHDeleteKeyW(HKEY_CURRENT_USER, UserClsidKey().c_str());
	if (result != ERROR_SUCCESS && result != ERROR_FILE_NOT_FOUND)
		return E_FAIL;
	return S_OK;
}

BOOL WINAPI DllMain(HINSTANCE hinstDll, DWORD dwReason, LPVOID lpReserved)
{
	switch (dwReason) {

	case DLL_PROCESS_ATTACH:
		g_hinstDll = hinstDll;
		DisableThreadLibraryCalls(hinstDll);
		return TRUE;

	}

	return TRUE;
}
