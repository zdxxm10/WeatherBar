#pragma once
#include <winrt/Windows.Foundation.Collections.h>
#include <winrt/Windows.Data.Json.h>
#include <roapi.h>
#include <stdexcept>
#pragma comment(lib, "windowsapp.lib")

// Use the Windows JSON parser for escaped Unicode and structural validation.
struct JsonApartment {
	HRESULT result = RoInitialize(RO_INIT_MULTITHREADED);
	JsonApartment() { if (FAILED(result) && result != RPC_E_CHANGED_MODE) throw std::runtime_error("JSON apartment"); }
	~JsonApartment() { if (SUCCEEDED(result)) RoUninitialize(); }
};
inline winrt::Windows::Data::Json::JsonObject ServiceJson(const std::string& bytes) {
	auto root = winrt::Windows::Data::Json::JsonObject::Parse(winrt::to_hstring(bytes));
	if (root.GetNamedBoolean(L"error", false)) throw std::runtime_error("service error response");
	return root;
}
inline std::wstring JsonText(const winrt::Windows::Data::Json::JsonObject& object, const wchar_t* key) {
	return std::wstring(object.GetNamedString(key, L""));
}
