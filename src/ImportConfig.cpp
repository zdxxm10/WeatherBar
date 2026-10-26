#include "pch.h"
#include "ImportConfig.h"
#include "LegacyNames.h"
#include <atlbase.h>
#include <shlwapi.h>
#include <xmllite.h>
#include <cstdio>
#include <sstream>

#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "xmllite.lib")

namespace {

const wchar_t kExampleLatitude[] = L"39.9286";
const wchar_t kExampleLongitude[] = L"116.4103";
const wchar_t kDefaultUrl[] =
	L"https://api.open-meteo.com/v1/forecast?latitude=39.9286&longitude=116.4103"
	L"&hourly=temperature_2m,weather_code"
	L"&daily=temperature_2m_max,temperature_2m_min,sunrise,sunset"
	L"&timezone=Asia%2FShanghai&timeformat=unixtime";

const wchar_t kData[] = L"Data";
const wchar_t kUrl[] = L"ForecastUrl";
const wchar_t kZone[] = L"TimezoneOffset";
const wchar_t kHours[] = L"RefreshHours";
const wchar_t kDelay[] = L"StaggerSeconds";
const wchar_t kIcon[] = L"Icon";
const wchar_t kShow[] = L"Visible";
const wchar_t kIconSize[] = L"Edge";
const wchar_t kLevel[] = L"Level";
const wchar_t kCurrent[] = L"Current";
const wchar_t kFont[] = L"FontSize";
const wchar_t kColor[] = L"Color";
const wchar_t kMax[] = L"High";
const wchar_t kMin[] = L"Low";
const wchar_t kGap[] = L"Spacing";
const wchar_t kGapIcon[] = L"IconToCurrent";
const wchar_t kGapExtreme[] = L"CurrentToExtremes";
const wchar_t kIconFiles[] = L"IconFiles";

bool Same(const std::wstring& name, const wchar_t* current, const wchar_t* legacy) {
	return name == current || name == legacy;
}

std::wstring CDataText(const std::wstring& text) {
	std::wstring out;
	size_t start = 0;
	while (start <= text.size()) {
		size_t pos = text.find(L"]]>", start);
		if (pos == std::wstring::npos) {
			out += text.substr(start);
			break;
		}
		out += text.substr(start, pos - start);
		out += L"]]]]><![CDATA[>";
		start = pos + 3;
	}
	return out;
}

std::wstring XmlEscape(const std::wstring& text) {
	std::wstring out;
	out.reserve(text.size());
	for (wchar_t ch : text) {
		switch (ch) {
		case L'&': out += L"&amp;"; break;
		case L'<': out += L"&lt;"; break;
		case L'>': out += L"&gt;"; break;
		case L'"': out += L"&quot;"; break;
		default: out += ch; break;
		}
	}
	return out;
}

std::string WideToUtf8(const std::wstring& text) {
	if (text.empty())
		return {};
	int bytes = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0, nullptr, nullptr);
	if (bytes <= 0)
		return {};
	std::string out(bytes, '\0');
	WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), out.data(), bytes, nullptr, nullptr);
	return out;
}

std::wstring Trim(const std::wstring& text) {
	size_t begin = 0;
	while (begin < text.size() && iswspace(text[begin]))
		++begin;
	size_t end = text.size();
	while (end > begin && iswspace(text[end - 1]))
		--end;
	return text.substr(begin, end - begin);
}

bool TryInt(const std::wstring& text, int& value) {
	if (text.empty())
		return false;
	wchar_t* end = nullptr;
	long parsed = wcstol(text.c_str(), &end, 10);
	if (end == text.c_str())
		return false;
	while (*end && iswspace(*end))
		++end;
	if (*end)
		return false;
	value = (int)parsed;
	return true;
}

struct AttrMap {
	bool has = false;
	std::wstring value;
	std::wstring key;
	std::wstring filename;
	std::wstring src;
	std::wstring version;
};

AttrMap ReadAttributes(IXmlReader* reader) {
	AttrMap attrs;
	HRESULT hr = reader->MoveToFirstAttribute();
	if (hr != S_OK)
		return attrs;
	attrs.has = true;
	do {
		const wchar_t* name = nullptr;
		const wchar_t* value = nullptr;
		reader->GetLocalName(&name, nullptr);
		reader->GetValue(&value, nullptr);
		if (!name || !value)
			continue;
		if (lstrcmpW(name, L"value") == 0)
			attrs.value = value;
		else if (lstrcmpW(name, L"key") == 0)
			attrs.key = value;
		else if (lstrcmpW(name, L"filename") == 0)
			attrs.filename = value;
		else if (lstrcmpW(name, L"src") == 0)
			attrs.src = value;
		else if (lstrcmpW(name, L"version") == 0)
			attrs.version = value;
	} while (reader->MoveToNextAttribute() == S_OK);
	reader->MoveToElement();
	return attrs;
}

bool ParseBool(const std::wstring& text, bool& value) {
	if (text == L"1") {
		value = true;
		return true;
	}
	if (text == L"0") {
		value = false;
		return true;
	}
	return false;
}

struct LevelSeen {
	bool icon = false;
	bool current = false;
	bool max = false;
	bool min = false;
};

SizeLevel ParseSizeLevel(const std::wstring& text) {
	if (text == L"小")
		return SizeLevel::Small;
	if (text == L"大")
		return SizeLevel::Large;
	return SizeLevel::Medium;
}

std::wstring FormatSizeLevel(SizeLevel level) {
	if (level == SizeLevel::Small)
		return L"小";
	if (level == SizeLevel::Large)
		return L"大";
	return L"中";
}

SizeLevel LevelFromLegacy(int value, int low, int high) {
	if (high <= low || value <= low)
		return SizeLevel::Small;
	if (value >= high)
		return SizeLevel::Large;
	int pos = (value - low) * 100 / (high - low);
	if (pos >= 67)
		return SizeLevel::Large;
	if (pos >= 34)
		return SizeLevel::Medium;
	return SizeLevel::Small;
}

void ApplyElement(const std::wstring& parent, const std::wstring& name, const AttrMap& attrs, BandConfig& config, LevelSeen& levelSeen) {
	if (parent == L"General" && name == L"Language") {
		config.language = ParseLanguage(attrs.value);
		return;
	}
	if (name == L"URL" && !attrs.src.empty()) {
		config.url = attrs.src;
		return;
	}
	if (name == L"Icon" && !attrs.key.empty() && !attrs.filename.empty()) {
		try {
			config.icons.push_back({ std::stoi(attrs.key), attrs.filename });
		}
		catch (...) {
		}
		return;
	}
	if (!attrs.has)
		return;

	const bool data = Same(parent, kData, L"数据");
	const bool icon = Same(parent, kIcon, L"图标");
	const bool current = Same(parent, kCurrent, L"当前温度");
	const bool high = Same(parent, kMax, L"最高温");
	const bool low = Same(parent, kMin, L"最低温");
	const bool gap = Same(parent, kGap, L"间距");

	if (data && Same(name, kUrl, L"接口地址")) {
		config.url = attrs.value;
	}
	else if (data && Same(name, kZone, L"时区偏移")) {
		int value = config.timezoneOffsetHours;
		if (TryInt(attrs.value, value))
			config.timezoneOffsetHours = value;
	}
	else if (data && Same(name, kDelay, L"错峰秒数")) {
		int value = config.delaySeconds;
		if (TryInt(attrs.value, value))
			config.delaySeconds = value;
	}
	else if (icon && Same(name, kShow, L"显示")) {
		ParseBool(attrs.value, config.showIcon);
	}
	else if (icon && Same(name, kLevel, L"档位")) {
		config.iconLevel = ParseSizeLevel(attrs.value);
		levelSeen.icon = true;
	}
	else if (icon && Same(name, kIconSize, L"边长") && !levelSeen.icon) {
		int value = 0;
		if (TryInt(attrs.value, value))
			config.iconLevel = LevelFromLegacy(value, 16, 48);
	}
	else if (current && Same(name, kLevel, L"档位")) {
		config.currentLevel = ParseSizeLevel(attrs.value);
		levelSeen.current = true;
	}
	else if (current && Same(name, kFont, L"字号") && !levelSeen.current) {
		int value = 0;
		if (TryInt(attrs.value, value))
			config.currentLevel = LevelFromLegacy(value, 8, 24);
	}
	else if (current && Same(name, kColor, L"颜色")) {
		ParseColorText(attrs.value, config.currentColor);
	}
	else if (high && Same(name, kShow, L"显示")) {
		ParseBool(attrs.value, config.showMax);
	}
	else if (high && Same(name, kLevel, L"档位")) {
		config.maxLevel = ParseSizeLevel(attrs.value);
		levelSeen.max = true;
	}
	else if (high && Same(name, kFont, L"字号") && !levelSeen.max) {
		int value = 0;
		if (TryInt(attrs.value, value))
			config.maxLevel = LevelFromLegacy(value, 8, 24);
	}
	else if (high && Same(name, kColor, L"颜色")) {
		ParseColorText(attrs.value, config.maxColor);
	}
	else if (low && Same(name, kShow, L"显示")) {
		ParseBool(attrs.value, config.showMin);
	}
	else if (low && Same(name, kLevel, L"档位")) {
		config.minLevel = ParseSizeLevel(attrs.value);
		levelSeen.min = true;
	}
	else if (low && Same(name, kFont, L"字号") && !levelSeen.min) {
		int value = 0;
		if (TryInt(attrs.value, value))
			config.minLevel = LevelFromLegacy(value, 8, 24);
	}
	else if (low && Same(name, kColor, L"颜色")) {
		ParseColorText(attrs.value, config.minColor);
	}
	else if (gap && Same(name, kLevel, L"档位")) {
		SizeLevel level = ParseSizeLevel(attrs.value);
		config.gapIconLevel = level;
		config.gapExtremeLevel = level;
	}
	else if (gap && Same(name, kGapIcon, L"图标与当前温度")) {
		config.gapIconLevel = ParseSizeLevel(attrs.value);
	}
	else if (gap && Same(name, kGapExtreme, L"当前温度与高低温")) {
		config.gapExtremeLevel = ParseSizeLevel(attrs.value);
	}
}

std::wstring BuildConfigXml(const BandConfig& config) {
	std::wstringstream xml;
	xml << L"<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n";
	xml << L"<WeatherBar version=\"" << kConfigSchemaVersion << L"\">\r\n";
	xml << L"  <General><Language value=\"" << LanguageCode(config.language) << L"\"/></General>\r\n";
	xml << L"  <" << kData << L">\r\n";
	xml << L"    <!-- Open-Meteo \u63a5\u53e3\u5730\u5740\u3002\u65f6\u95f4\u683c\u5f0f\u987b\u4e3a unixtime\uff0c\u5e76\u5305\u542b\u9010\u5c0f\u65f6\u6e29\u5ea6\u3001\u5929\u6c14\u4ee3\u7801\uff0c\u4ee5\u53ca\u6bcf\u65e5\u6700\u9ad8\u6e29\u3001\u6700\u4f4e\u6e29\u3001\u65e5\u51fa\u548c\u65e5\u843d\u3002 -->\r\n";
	xml << L"    <!-- \u5730\u5740\u91cc\u7684 timezone \u8981\u548c\u4e0b\u9762\u7684\u65f6\u533a\u504f\u79fb\u4e00\u81f4\uff0c\u5426\u5219\u8de8\u5348\u591c\u5207\u6362\u7684\u65e5\u671f\u4f1a\u4e0d\u51c6\u3002 -->\r\n";
	xml << L"    <!-- \u8fd9\u91cc\u7c98\u8d34 Open-Meteo \u7ed9\u51fa\u7684\u6574\u6bb5\u5730\u5740\uff0c\u548c\u8bbe\u7f6e\u91cc\u770b\u5230\u7684\u5b8c\u5168\u4e00\u6837\u3002\u7f51\u5740\u91cc\u7684 %2F \u5c31\u662f\u659c\u6760 / \u3002 -->\r\n";
	xml << L"    <" << kUrl << L"><![CDATA[" << CDataText(config.url) << L"]]></" << kUrl << L">\r\n";
	xml << L"    <!-- \u76f8\u5bf9 UTC \u7684\u5c0f\u65f6\u504f\u79fb\u3002\u5c0f\u4e8e -12 \u6309 -12\uff0c\u5927\u4e8e 14 \u6309 14\u3002 -->\r\n";
	xml << L"    <" << kZone << L" value=\"" << config.timezoneOffsetHours << L"\"/>\r\n";
	xml << L"    <!-- \u5237\u65b0\u949f\u70b9\u56fa\u5b9a\u4e3a 0\u30016\u300112\u300118\u3002\u6539\u8fd9\u91cc\u7684\u6570\u5b57\u4e0d\u4f1a\u6539\u53d8\u7a0b\u5e8f\u3002\u542f\u52a8\u540e\u4f1a\u5148\u7acb\u5373\u53d6\u4e00\u6b21\uff0c\u8fd9\u4e9b\u65f6\u523b\u4e0d\u53d7\u624b\u52a8\u5237\u65b0\u5f71\u54cd\u3002 -->\r\n";
	xml << L"    <" << kHours << L" value=\"" << XmlEscape(FormatScheduleHours(config.scheduleHours)) << L"\"/>\r\n";
	xml << L"    <!-- \u9519\u5cf0\u79d2\u6570\u3002\u9ed8\u8ba4 900\uff0c\u5373 15 \u5206\u949f\u3002\u5c0f\u4e8e 0 \u6309 0\uff0c\u5927\u4e8e 1800 \u6309 1800\uff0c\u4e5f\u5c31\u662f\u6700\u957f 30 \u5206\u949f\u3002\u4e0d\u5728\u8bbe\u7f6e\u91cc\u663e\u793a\uff0c\u6539\u8fd9\u4e2a\u6570\u5b57\u4f1a\u751f\u6548\u3002 -->\r\n";
	xml << L"    <" << kDelay << L" value=\"" << config.delaySeconds << L"\"/>\r\n";
	xml << L"  </" << kData << L">\r\n";
	xml << L"  <" << kIcon << L">\r\n";
	xml << L"    <!-- \u662f\u5426\u663e\u793a\u5929\u6c14\u56fe\u6807\u30021 \u663e\u793a\uff0c0 \u9690\u85cf\u3002\u9690\u85cf\u540e\u4e0d\u5360\u5bbd\u5ea6\uff0c\u4e5f\u4e0d\u4f1a\u6210\u4e3a\u70b9\u51fb\u533a\u57df\u3002 -->\r\n";
	xml << L"    <" << kShow << L" value=\"" << (config.showIcon ? 1 : 0) << L"\"/>\r\n";
	xml << L"    <!-- \u5927\u3001\u4e2d\u3001\u5c0f\u3002\u5927\u6863\u9876\u5230\u5929\u6c14\u6761\u9ad8\u5ea6\uff0c\u4e2d\u6863\u7ea6\u516b\u6210\uff0c\u5c0f\u6863\u7ea6\u516d\u6210\u3002 -->\r\n";
	xml << L"    <" << kLevel << L" value=\"" << FormatSizeLevel(config.iconLevel) << L"\"/>\r\n";
	xml << L"  </" << kIcon << L">\r\n";
	xml << L"  <" << kCurrent << L">\r\n";
	xml << L"    <!-- \u5f53\u524d\u6e29\u5ea6\u59cb\u7ec8\u663e\u793a\u3002\u6863\u4f4d\u4e3a\u5927\u3001\u4e2d\u3001\u5c0f\u3002 -->\r\n";
	xml << L"    <" << kLevel << L" value=\"" << FormatSizeLevel(config.currentLevel) << L"\"/>\r\n";
	xml << L"    <!-- \u989c\u8272\u7528 6 \u4f4d\u5341\u516d\u8fdb\u5236 RRGGBB\uff0c\u4e0d\u9700\u8981\u5e26 #\u3002 -->\r\n";
	xml << L"    <" << kColor << L" value=\"" << FormatColorText(config.currentColor) << L"\"/>\r\n";
	xml << L"  </" << kCurrent << L">\r\n";
	xml << L"  <" << kMax << L">\r\n";
	xml << L"    <!-- \u662f\u5426\u663e\u793a\u6700\u9ad8\u6e29\u30021 \u663e\u793a\uff0c0 \u9690\u85cf\u3002\u9690\u85cf\u540e\u4e0d\u5360\u4f4d\u7f6e\u3002\u53ea\u663e\u793a\u6700\u9ad8\u6e29\u6216\u6700\u4f4e\u6e29\u5176\u4e2d\u4e00\u65b9\u65f6\uff0c\u90a3\u4e00\u884c\u5360\u6ee1\u6587\u5b57\u9ad8\u5ea6\u3002 -->\r\n";
	xml << L"    <" << kShow << L" value=\"" << (config.showMax ? 1 : 0) << L"\"/>\r\n";
	xml << L"    <!-- \u6863\u4f4d\u4e3a\u5927\u3001\u4e2d\u3001\u5c0f\u3002\u6700\u9ad8\u6e29\u548c\u6700\u4f4e\u6e29\u540c\u65f6\u663e\u793a\u65f6\uff0c\u5927\u6863\u662f\u5929\u6c14\u6761\u7684\u4e00\u534a\uff1b\u53ea\u663e\u793a\u4e00\u884c\u65f6\uff0c\u5927\u6863\u6309\u6574\u6761\u9ad8\u5ea6\u3002 -->\r\n";
	xml << L"    <" << kLevel << L" value=\"" << FormatSizeLevel(config.maxLevel) << L"\"/>\r\n";
	xml << L"    <" << kColor << L" value=\"" << FormatColorText(config.maxColor) << L"\"/>\r\n";
	xml << L"  </" << kMax << L">\r\n";
	xml << L"  <" << kMin << L">\r\n";
	xml << L"    <!-- \u662f\u5426\u663e\u793a\u6700\u4f4e\u6e29\u30021 \u663e\u793a\uff0c0 \u9690\u85cf\u3002 -->\r\n";
	xml << L"    <" << kShow << L" value=\"" << (config.showMin ? 1 : 0) << L"\"/>\r\n";
	xml << L"    <!-- \u6863\u4f4d\u4e3a\u5927\u3001\u4e2d\u3001\u5c0f\u3002 -->\r\n";
	xml << L"    <" << kLevel << L" value=\"" << FormatSizeLevel(config.minLevel) << L"\"/>\r\n";
	xml << L"    <" << kColor << L" value=\"" << FormatColorText(config.minColor) << L"\"/>\r\n";
	xml << L"  </" << kMin << L">\r\n";
	xml << L"  <" << kGap << L">\r\n";
	xml << L"    <!-- \u4e24\u5904\u7a7a\u9699\u5206\u5f00\u8bbe\u3002\u5927\u7ea6\u4e3a\u5929\u6c14\u6761\u9ad8\u5ea6\u7684 28%\uff0c\u4e2d 16%\uff0c\u5c0f 8%\u3002 -->\r\n";
	xml << L"    <" << kGapIcon << L" value=\"" << FormatSizeLevel(config.gapIconLevel) << L"\"/>\r\n";
	xml << L"    <" << kGapExtreme << L" value=\"" << FormatSizeLevel(config.gapExtremeLevel) << L"\"/>\r\n";
	xml << L"  </" << kGap << L">\r\n";
	xml << L"  <" << kIconFiles << L">\r\n";
	xml << L"    <!-- \u5929\u6c14\u4ee3\u7801\u5bf9\u5e94 icons \u76ee\u5f55\u91cc\u7684\u56fe\u6807\u6587\u4ef6\u540d\uff0c\u4e0d\u542b .svg\u3002\u9047\u5230\u5c0f\u6570\u5b57\uff08\u4ee3\u7801 0\u30011\u30012\uff09\u5728\u65e5\u51fa\u524d\u548c\u65e5\u843d\u540e\u4f1a\u52a0 100\uff0c\u53ef\u53e6\u5916\u914d\u591c\u95f4\u56fe\u6807\u3002 -->\r\n";
	for (const auto& icon : config.icons)
		xml << L"    <Icon key=\"" << icon.first << L"\" filename=\"" << XmlEscape(icon.second) << L"\"/>\r\n";
	xml << L"  </" << kIconFiles << L">\r\n";
	xml << L"</WeatherBar>\r\n";
	return xml.str();
}

}

int ClampInt(int value, int low, int high) {
	if (value < low)
		return low;
	if (value > high)
		return high;
	return value;
}

std::wstring ModuleDirectory() {
	wchar_t path[MAX_PATH];
	DWORD length = GetModuleFileNameW(g_hinstDll, path, MAX_PATH);
	if (length == 0 || length >= MAX_PATH)
		return L"";
	wchar_t* slash = wcsrchr(path, L'\\');
	if (!slash)
		return L"";
	*slash = 0;
	return path;
}

std::wstring ConfigFilePath() {
	std::wstring dir = ModuleDirectory();
	if (dir.empty())
		return L"WeatherBar.xml";
	return dir + L"\\WeatherBar.xml";
}

std::wstring FormatScheduleHours(const std::vector<int>& hours) {
	std::wstring text;
	for (int hour : hours) {
		if (!text.empty())
			text += L",";
		text += std::to_wstring(hour);
	}
	return text;
}

bool ParseColorText(const std::wstring& text, COLORREF& color) {
	std::wstring value = Trim(text);
	if (!value.empty() && value[0] == L'#')
		value.erase(value.begin());
	if (value.size() != 6)
		return false;
	unsigned rgb = 0;
	for (wchar_t ch : value) {
		rgb <<= 4;
		if (ch >= L'0' && ch <= L'9')
			rgb += (unsigned)(ch - L'0');
		else if (ch >= L'a' && ch <= L'f')
			rgb += (unsigned)(ch - L'a' + 10);
		else if (ch >= L'A' && ch <= L'F')
			rgb += (unsigned)(ch - L'A' + 10);
		else
			return false;
	}
	color = RGB((rgb >> 16) & 0xFF, (rgb >> 8) & 0xFF, rgb & 0xFF);
	return true;
}

std::wstring FormatColorText(COLORREF color) {
	wchar_t buf[8];
	swprintf_s(buf, L"%02X%02X%02X", GetRValue(color), GetGValue(color), GetBValue(color));
	return buf;
}

COLORREF TaskbarContrastColor() {
	DWORD light = 0;
	DWORD bytes = sizeof(light);
	LSTATUS status = RegGetValueW(
		HKEY_CURRENT_USER,
		L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
		L"SystemUsesLightTheme",
		RRF_RT_REG_DWORD,
		nullptr,
		&light,
		&bytes);
	if (status != ERROR_SUCCESS)
		return RGB(255, 255, 255);
	return light ? RGB(0x1A, 0x1A, 0x1A) : RGB(255, 255, 255);
}

void AddDefaultIcons(BandConfig& config) {
	const std::pair<int, const wchar_t*> icons[] = {
		{ 0, L"sunny" },
		{ 1, L"sunny" },
		{ 2, L"partlycloudy" },
		{ 3, L"cloudy" },
		{ 45, L"fog" },
		{ 48, L"fog" },
		{ 51, L"drizzle1" },
		{ 53, L"drizzle2" },
		{ 55, L"drizzle3" },
		{ 56, L"freezing_drizzle" },
		{ 57, L"freezing_drizzle" },
		{ 61, L"rainy1" },
		{ 63, L"rainy2" },
		{ 65, L"rainy3" },
		{ 66, L"freezing_rain" },
		{ 67, L"freezing_rain" },
		{ 71, L"snow1" },
		{ 73, L"snow2" },
		{ 75, L"snow3" },
		{ 77, L"snowgrain" },
		{ 80, L"rainy1" },
		{ 81, L"rainy2" },
		{ 82, L"rainy3" },
		{ 85, L"snow1" },
		{ 86, L"snow3" },
		{ 95, L"thunderstorm" },
		{ 96, L"thunderstorm_hail" },
		{ 99, L"thunderstorm_hail" },
		{ 100, L"moon" },
		{ 101, L"moon" },
		{ 102, L"moonpartly" },
		{ -1, L"None" },
	};
	config.icons.clear();
	for (const auto& icon : icons)
		config.icons.push_back({ icon.first, icon.second });
}

BandConfig DefaultBandConfig() {
	BandConfig config;
	config.url = kDefaultUrl;
	config.scheduleHours = { 0, 6, 12, 18 };
	config.currentColor = TaskbarContrastColor();
	AddDefaultIcons(config);
	return config;
}

int HexDigit(wchar_t ch) {
	if (ch >= L'0' && ch <= L'9')
		return ch - L'0';
	if (ch >= L'a' && ch <= L'f')
		return ch - L'a' + 10;
	if (ch >= L'A' && ch <= L'F')
		return ch - L'A' + 10;
	return -1;
}

std::wstring DecodeQuery(const std::wstring& text) {
	std::wstring out;
	for (size_t i = 0; i < text.size(); ++i) {
		if (text[i] == L'+') {
			out.push_back(L' ');
			continue;
		}
		if (text[i] == L'%' && i + 2 < text.size()) {
			int hi = HexDigit(text[i + 1]);
			int lo = HexDigit(text[i + 2]);
			if (hi >= 0 && lo >= 0) {
				out.push_back((wchar_t)((hi << 4) | lo));
				i += 2;
				continue;
			}
		}
		out.push_back(text[i]);
	}
	return out;
}

bool Unreserved(wchar_t ch) {
	return (ch >= L'A' && ch <= L'Z') || (ch >= L'a' && ch <= L'z') || (ch >= L'0' && ch <= L'9') || ch == L'-' || ch == L'_' || ch == L'.' || ch == L'~';
}

std::wstring EncodeQuery(const std::wstring& text) {
	std::wstring out;
	for (wchar_t ch : text) {
		if (Unreserved(ch)) {
			out.push_back(ch);
			continue;
		}
		char utf8[8]{};
		int bytes = WideCharToMultiByte(CP_UTF8, 0, &ch, 1, utf8, 8, nullptr, nullptr);
		if (bytes < 1) {
			out.push_back(ch);
			continue;
		}
		for (int i = 0; i < bytes; ++i) {
			wchar_t hex[8];
			swprintf_s(hex, L"%%%02X", (unsigned char)utf8[i]);
			out += hex;
		}
	}
	return out;
}

bool SameKey(const std::wstring& key, const wchar_t* name) {
	return _wcsicmp(key.c_str(), name) == 0;
}

void QueryPair(const std::wstring& query, const wchar_t* wanted, std::wstring& value) {
	size_t i = 0;
	while (i < query.size()) {
		size_t amp = query.find(L'&', i);
		std::wstring pair = query.substr(i, amp == std::wstring::npos ? std::wstring::npos : amp - i);
		i = amp == std::wstring::npos ? query.size() : amp + 1;
		if (pair.empty())
			continue;
		size_t eq = pair.find(L'=');
		std::wstring key = DecodeQuery(eq == std::wstring::npos ? pair : pair.substr(0, eq));
		if (!SameKey(key, wanted))
			continue;
		value = DecodeQuery(eq == std::wstring::npos ? L"" : pair.substr(eq + 1));
	}
}

bool ReadPlainNumber(const std::wstring& text, size_t& index, std::wstring& number) {
	while (index < text.size() && (text[index] == L' ' || text[index] == L'\t'))
		++index;
	size_t begin = index;
	if (index < text.size() && (text[index] == L'+' || text[index] == L'-'))
		++index;
	bool digit = false;
	bool dot = false;
	while (index < text.size()) {
		if (text[index] >= L'0' && text[index] <= L'9') {
			digit = true;
			++index;
			continue;
		}
		if (text[index] == L'.' && !dot) {
			dot = true;
			++index;
			continue;
		}
		break;
	}
	if (!digit)
		return false;
	number = text.substr(begin, index - begin);
	while (index < text.size() && (text[index] == L' ' || text[index] == L'\t'))
		++index;
	return true;
}

bool ReadCoordinatePair(const std::wstring& text, std::wstring& latitude, std::wstring& longitude) {
	if (text.find(L"://") != std::wstring::npos || text.find(L'=') != std::wstring::npos)
		return false;
	size_t index = 0;
	if (!ReadPlainNumber(text, index, latitude))
		return false;
	if (index < text.size() && (text[index] == L',' || text[index] == L'，' || text[index] == L';' || text[index] == L'；'))
		++index;
	if (!ReadPlainNumber(text, index, longitude))
		return false;
	return index == text.size();
}

std::wstring BuildForecastUrl(const std::wstring& latitude, const std::wstring& longitude, const std::wstring& timezone) {
	std::wstring zone = timezone.empty() ? L"Asia/Shanghai" : timezone;
	return L"https://api.open-meteo.com/v1/forecast?latitude=" + latitude
		+ L"&longitude=" + longitude
		+ L"&hourly=temperature_2m,weather_code"
		+ L"&daily=temperature_2m_max,temperature_2m_min,sunrise,sunset"
		+ L"&timezone=" + EncodeQuery(zone)
		+ L"&timeformat=unixtime";
}

bool CanonicalForecastUrl(const std::wstring& input, std::wstring& url, std::wstring& error) {
	std::wstring text = Trim(input);
	if (text.empty()) {
		error = std::wstring(L"请填写接口地址，或只填纬度和经度，例如 ") + kExampleLatitude + L", " + kExampleLongitude + L"。";
		return false;
	}
	std::wstring latitude;
	std::wstring longitude;
	if (ReadCoordinatePair(text, latitude, longitude)) {
		url = BuildForecastUrl(latitude, longitude, L"Asia/Shanghai");
		error.clear();
		return true;
	}
	size_t queryPos = text.find(L'?');
	std::wstring query = queryPos == std::wstring::npos ? text : text.substr(queryPos + 1);
	size_t hash = query.find(L'#');
	if (hash != std::wstring::npos)
		query.resize(hash);
	QueryPair(query, L"latitude", latitude);
	QueryPair(query, L"longitude", longitude);
	if (latitude.empty() || longitude.empty()) {
		error = std::wstring(L"认不出纬度和经度。可以粘贴 Open-Meteo 网页上的整段地址，或只填 ") + kExampleLatitude + L", " + kExampleLongitude + L"。";
		return false;
	}
	std::wstring timezone;
	QueryPair(query, L"timezone", timezone);
	url = BuildForecastUrl(latitude, longitude, timezone);
	error.clear();
	return true;
}

void NormalizeBandConfig(BandConfig& config) {
	std::wstring canonical;
	std::wstring error;
	if (CanonicalForecastUrl(config.url, canonical, error))
		config.url = canonical;
	else if (Trim(config.url).empty())
		config.url = kDefaultUrl;
	else
		config.url = Trim(config.url);
	config.timezoneOffsetHours = ClampInt(config.timezoneOffsetHours, kTimezoneMin, kTimezoneMax);
	config.scheduleHours = { 0, 6, 12, 18 };
	config.delaySeconds = ClampInt(config.delaySeconds, kDelayMin, kDelayMax);
}

BandConfig LoadBandConfig() {
	BandConfig config = DefaultBandConfig();
	std::wstring sourcePath = ConfigFilePath();
	bool legacyFile = false;
	if (GetFileAttributesW(sourcePath.c_str()) == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND) {
		std::wstring oldPath = ModuleDirectory() + L"\\" + kLegacyConfigFile;
		if (GetFileAttributesW(oldPath.c_str()) != INVALID_FILE_ATTRIBUTES) { sourcePath = oldPath; legacyFile = true; }
	}
	CComPtr<IXmlReader> reader;
	if (FAILED(CreateXmlReader(__uuidof(IXmlReader), reinterpret_cast<void**>(&reader), nullptr))) {
		config.canSave = false;
		return config;
	}
	CComPtr<IStream> stream;
	if (FAILED(SHCreateStreamOnFileW(sourcePath.c_str(), STGM_READ, &stream))) {
		DWORD attributes = GetFileAttributesW(sourcePath.c_str());
		config.canSave = attributes == INVALID_FILE_ATTRIBUTES && GetLastError() == ERROR_FILE_NOT_FOUND;
		return config;
	}
	if (FAILED(reader->SetInput(stream))) {
		config.canSave = false;
		return config;
	}
	// 新档位优先于旧像素值；状态只属于本次读取，避免多个天气条互相干扰。
	LevelSeen levelSeen;
	config.icons.clear();

	std::vector<std::wstring> stack;
	bool legacyUrl = false;
	int sourceVersion = 0;
	bool rootSeen = false;
	bool languageSeen = false;
	bool versionValid = true;
	XmlNodeType nodeType;
	HRESULT readResult;
	while ((readResult = reader->Read(&nodeType)) == S_OK) {
		if (nodeType == XmlNodeType_Element) {
			const wchar_t* localName = nullptr;
			reader->GetLocalName(&localName, nullptr);
			std::wstring name = localName ? localName : L"";
			std::wstring parent = stack.empty() ? L"" : stack.back();
			AttrMap attrs = ReadAttributes(reader);
			if (stack.empty() && (name == L"WeatherBar" || name == kLegacyConfigRoot)) {
				rootSeen = true;
				if (!attrs.version.empty())
					versionValid = TryInt(attrs.version, sourceVersion) && sourceVersion >= 0;
			}
			if (parent == L"General" && name == L"Language")
				languageSeen = true;
			ApplyElement(parent, name, attrs, config, levelSeen);
			if (Same(parent, kData, L"数据") && Same(name, kUrl, L"接口地址") && !attrs.value.empty())
				legacyUrl = true;
			if (reader->IsEmptyElement() != TRUE)
				stack.push_back(name);
		}
		else if (nodeType == XmlNodeType_Text || nodeType == XmlNodeType_CDATA) {
			if (!stack.empty() && Same(stack.back(), kUrl, L"接口地址")) {
				const wchar_t* value = nullptr;
				reader->GetValue(&value, nullptr);
				if (value && Trim(value).size() > 0) {
					config.url = Trim(value);
					legacyUrl = false;
				}
			}
		}
		else if (nodeType == XmlNodeType_EndElement) {
			if (!stack.empty())
				stack.pop_back();
		}
	}
	if (FAILED(readResult) || !rootSeen || !versionValid) {
		// 损坏的 XML 不能当作空白配置覆盖；允许显示默认天气，禁止保存。
		BandConfig fallback = DefaultBandConfig();
		fallback.canSave = false;
		return fallback;
	}
	config.schemaVersion = sourceVersion > kConfigSchemaVersion ? sourceVersion : kConfigSchemaVersion;
	config.canSave = sourceVersion <= kConfigSchemaVersion;
	if (config.icons.empty())
		AddDefaultIcons(config);
	int delayBeforeClamp = config.delaySeconds;
	std::wstring urlBefore = config.url;
	NormalizeBandConfig(config);
	reader.Release();
	stream.Release();
	if (config.canSave && (legacyFile || sourceVersion < kConfigSchemaVersion || !languageSeen || delayBeforeClamp != config.delaySeconds || legacyUrl || urlBefore != config.url)) {
		// 先保留迁移前原件；新版默认值填补缺项，旧值已在读取时覆盖默认值。
		std::wstring backup = sourcePath + L".before-v" + std::to_wstring(kConfigSchemaVersion) + L".bak";
		if (CopyFileW(sourcePath.c_str(), backup.c_str(), TRUE) || GetLastError() == ERROR_FILE_EXISTS)
			SaveBandConfig(config);
	}
	return config;
}

bool SaveBandConfig(const BandConfig& config) {
	if (!config.canSave || config.schemaVersion > kConfigSchemaVersion)
		return false;
	BandConfig normalized = config;
	NormalizeBandConfig(normalized);
	std::string utf8 = WideToUtf8(BuildConfigXml(normalized));
	if (utf8.empty())
		return false;

	// 先在同目录写完临时文件，短写或关闭失败都不能破坏上一份配置。
	std::wstring destination = ConfigFilePath();
	std::wstring directory = ModuleDirectory();
	if (directory.empty())
		directory = L".";
	wchar_t temporary[MAX_PATH]{};
	if (!GetTempFileNameW(directory.c_str(), L"wdb", 0, temporary))
		return false;
	FILE* file = nullptr;
	if (_wfopen_s(&file, temporary, L"wb") != 0 || !file) {
		DeleteFileW(temporary);
		return false;
	}
	const unsigned char bom[3] = { 0xEF, 0xBB, 0xBF };
	bool ok = fwrite(bom, 1, sizeof(bom), file) == sizeof(bom)
		&& fwrite(utf8.data(), 1, utf8.size(), file) == utf8.size();
	// fclose 也可能在刷新缓冲区时失败，不能仅检查 fwrite。
	if (fclose(file) != 0)
		ok = false;
	if (ok) {
		// 替换已有文件时保留其权限；首次保存只移动，不覆盖并发创建的文件。
		ok = ReplaceFileW(destination.c_str(), temporary, nullptr, 0, nullptr, nullptr) != FALSE;
		if (!ok && GetLastError() == ERROR_FILE_NOT_FOUND)
			ok = MoveFileExW(temporary, destination.c_str(), MOVEFILE_WRITE_THROUGH) != FALSE;
	}
	if (!ok)
		DeleteFileW(temporary);
	return ok;
}
