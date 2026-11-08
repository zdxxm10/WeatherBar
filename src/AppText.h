#pragma once
#include <windows.h>
#include <commctrl.h>
#include <string>

#pragma comment(lib, "comctl32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

enum class Language { System, Chinese, English, Japanese };
inline constexpr wchar_t kAppVersion[] = L"1.1";
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

// Daily WMO code is the most severe phenomenon, not an hourly transition summary.
// Drizzle/showers -> rain; freezing drizzle -> freezing rain; grains/showers -> snow;
// thunderstorms with hail -> thunderstorm. Unknown codes never imply clear sky.
inline const wchar_t* DailyWeatherText(int code, Language language) {
	switch (code) {
	case 0: return Text(language, L"晴", L"Clear", L"快晴");
	case 1: return Text(language, L"少云", L"Mainly clear", L"晴れ");
	case 2: return Text(language, L"多云", L"Partly cloudy", L"部分的に曇り");
	case 3: return Text(language, L"阴", L"Overcast", L"曇り");
	case 45: case 48: return Text(language, L"雾", L"Fog", L"霧");
	case 51: case 53: case 55: case 61: case 63: case 65: case 80: case 81: case 82:
		return Text(language, L"雨", L"Rain", L"雨");
	case 56: case 57: case 66: case 67: return Text(language, L"冻雨", L"Freezing rain", L"着氷性の雨");
	case 71: case 73: case 75: case 77: case 85: case 86: return Text(language, L"雪", L"Snow", L"雪");
	case 95: case 96: case 99: return Text(language, L"雷暴", L"Thunderstorm", L"雷雨");
	default: return Text(language, L"天气未知", L"Unknown weather", L"天気不明");
	}
}
