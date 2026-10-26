#pragma once
#include <windows.h>
#include <commctrl.h>
#include <string>

#pragma comment(lib, "comctl32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

enum class Language { System, Chinese, English, Japanese };
inline constexpr wchar_t kAppVersion[] = L"0.2.0";
inline constexpr wchar_t kReleasePage[] = L"https://github.com/zdxxm10/WeatherBar/releases";

inline Language LanguageForLocale(const std::wstring& locale) {
	// 中文地区统一使用简体中文文案；其他未支持的语言回退到英语。
	if (locale == L"zh" || locale.rfind(L"zh-", 0) == 0)
		return Language::Chinese;
	if (locale == L"ja" || locale.rfind(L"ja-", 0) == 0)
		return Language::Japanese;
	return Language::English;
}

inline Language ResolveLanguage(Language language) {
	if (language != Language::System)
		return language;
	wchar_t locale[LOCALE_NAME_MAX_LENGTH]{};
	// 界面语言与地区格式可以不同；这里只跟随 Windows 显示语言。
	if (!LCIDToLocaleName(MAKELCID(GetUserDefaultUILanguage(), SORT_DEFAULT), locale, LOCALE_NAME_MAX_LENGTH, 0))
		return Language::English;
	return LanguageForLocale(locale);
}

inline const wchar_t* Text(Language language, const wchar_t* chinese, const wchar_t* english, const wchar_t* japanese) {
	language = ResolveLanguage(language);
	return language == Language::Chinese ? chinese : language == Language::Japanese ? japanese : english;
}

inline const wchar_t* LanguageCode(Language language) {
	switch (language) {
	case Language::Chinese: return L"zh-CN";
	case Language::English: return L"en";
	case Language::Japanese: return L"ja";
	default: return L"system";
	}
}

inline Language ParseLanguage(const std::wstring& code) {
	if (code == L"zh-CN") return Language::Chinese;
	if (code == L"en") return Language::English;
	if (code == L"ja") return Language::Japanese;
	return Language::System;
}

inline void ShowNotice(HWND owner, Language language, const wchar_t* heading, const wchar_t* content) {
	TASKDIALOGCONFIG dialog{};
	dialog.cbSize = sizeof(dialog);
	dialog.hwndParent = owner;
	dialog.dwFlags = TDF_SIZE_TO_CONTENT | TDF_POSITION_RELATIVE_TO_WINDOW | TDF_ALLOW_DIALOG_CANCELLATION;
	dialog.pszWindowTitle = L"WeatherBar";
	dialog.pszMainInstruction = heading;
	dialog.pszContent = content;
	TASKDIALOG_BUTTON button{ IDOK, Text(language, L"确定", L"OK", L"OK") };
	dialog.cButtons = 1;
	dialog.pButtons = &button;
	if (FAILED(TaskDialogIndirect(&dialog, nullptr, nullptr, nullptr)))
		MessageBoxW(owner, content, heading, MB_OK | MB_ICONINFORMATION);
}
