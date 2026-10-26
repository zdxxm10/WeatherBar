#include "TestSupport.h"
#include "../src/ImportConfig.h"
#include "../src/ImageList.h"
#include "../src/Weather.h"
#include <ShlObj.h>

// 只在测试中访问缓存，避免为验证内部资源状态扩大正式接口。
#define private public
#include "../src/DeskBand.h"
#undef private
#include "../src/DeskBand.cpp"

void TestDisplayResources() {
	CDeskBand band;
	band.EnsureFonts(24, 12, 12);
	band.EnsureVisuals(32);
	Require(band.fontCurrent && band.fontMax && band.fontMin, "GDI fonts must be created");
	Require(band.weathericons.ImageList && !band.weathericons.codetoindex.empty(), "SVG icons must load");
	HFONT currentFont = band.fontCurrent;
	HIMAGELIST icons = band.weathericons.ImageList;
	DWORD resourcesBefore = GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS);
	for (int i = 0; i < 10; ++i) {
		BandConfig next = band.config;
		next.currentColor = RGB(i, 20, 30);
		Require(band.ApplySavedConfig(next), "color settings must save");
		Require(band.builtIconPx == 32 && band.builtCurrentPx == 24, "color-only changes must preserve size caches");
		band.EnsureFonts(24, 12, 12);
		band.EnsureVisuals(32);
		Require(band.fontCurrent == currentFont && band.weathericons.ImageList == icons, "unchanged resources must be reused");
	}
	Require(GetGuiResources(GetCurrentProcess(), GR_GDIOBJECTS) == resourcesBefore, "reapplying colors must not grow GDI resources");
	band.EnsureFonts(30, 15, 15);
	band.EnsureVisuals(40);
	Require(band.builtCurrentPx == 30 && band.builtIconPx == 40, "size changes must rebuild resources");
	band.EnsureVisuals(0);
	Require(!band.weathericons.ImageList, "hidden icons must release the image list");
	band.EnsureVisuals(32);
	Require(!band.weathericons.codetoindex.empty(), "icons must reload after being shown again");
	band.ReleaseGraphics();
	band.ReleaseGraphics();
	Require(!band.fontCurrent && !band.weathericons.ImageList, "graphics cleanup must be repeatable");
}

void TestTooltipAndRequestGeneration() {
	CDeskBand band;
	BandConfig original = band.CurrentConfig();
	band.BeginSettingsPreview();
	BandConfig appearance = original;
	appearance.currentColor = RGB(4,5,6);
	band.ApplyPreviewConfig(appearance);
	DWORD attributes = GetFileAttributesW(ConfigFilePath().c_str());
	Require(attributes != INVALID_FILE_ATTRIBUTES && SetFileAttributesW(ConfigFilePath().c_str(), attributes | FILE_ATTRIBUTE_READONLY), "prepare failed-save preview fixture");
	bool saved = band.ApplySavedConfig(appearance);
	SetFileAttributesW(ConfigFilePath().c_str(), attributes);
	Require(!saved, "failed save must not commit preview snapshot");
	band.CancelSettingsPreview();
	Require(band.CurrentConfig().currentColor == original.currentColor, "cancel restores preview after a failed save");
	band._weather.cache.hasData = true;
	band._weather.cache.days[0].maxTemperature = L"original";
	band.BeginSettingsPreview();
	BandConfig preview = original;
	preview.url = L"https://api.open-meteo.com/v1/forecast?latitude=35&longitude=139&hourly=temperature_2m,weather_code&daily=temperature_2m_max,temperature_2m_min,sunrise,sunset&timezone=Asia%2FTokyo&timeformat=unixtime";
	band.ApplyPreviewConfig(preview);
	band._weather.cache.days[0].maxTemperature = L"preview";
	unsigned long long previewGeneration = band.generation;
	band.CancelSettingsPreview();
	Require(band.CurrentConfig().url == original.url && band._weather.cache.days[0].maxTemperature == L"original" && band.generation > previewGeneration, "rollback restores location, cached weather and rejects preview requests");
	band._weather.cache = {};
	band.config.language = Language::English;
	band.config.tooltipEnabled = true;
	band.config.tooltipFields = 0;
	band.tooltipHwnd = CreateWindowExW(0, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_NOPREFIX,
		0, 0, 0, 0, nullptr, nullptr, g_hinstDll, nullptr);
	Require(band.tooltipHwnd != nullptr, "native tooltip window for actual font measurements");
	Require(band.BuildTooltip().find(L"No weather data yet") != std::wstring::npos, "empty hover state");
	time_t now = time(nullptr);
	tm local{};
	Require(BreakLocalTime(now, band.config.timezoneOffsetHours, local), "tooltip local time");
	auto& day = band._weather.cache.days[0];
	day.dateKey = DateKeyFromUtc(now, band.config.timezoneOffsetHours);
	day.maxTemperature = L"27.0℃";
	day.minTemperature = L"18.0℃";
	day.hours[local.tm_hour] = { true, L"23.5℃", 3 };
	band._weather.cache.hasData = true;
	band.lastSuccessfulFetch = now;
	std::wstring tooltip = band.BuildTooltip();
	Require(tooltip.find(L"23.5℃") == std::wstring::npos && tooltip.find(L"Last refresh") != std::wstring::npos, "hover removes repeated forecast and renames refresh timestamp");
	Require(tooltip.find(L"High / low") == std::wstring::npos && std::count(tooltip.begin(), tooltip.end(), L'\n') == 1, "default hover only includes refresh and status");
	band.config.tooltipEnabled = false;
	Require(band.BuildTooltip().empty(), "disabled tooltip has no content");
	band.config.tooltipEnabled = true;
	band.config.tooltipFields = 127;
	auto& point = day.hours[local.tm_hour];
	point.uvIndex = L"6.2"; point.windDirection = L"45"; point.windSpeed = L"3.5";
	point.humidity = L"65"; point.apparentTemperature = L"24.0"; point.precipitationProbability = L"30";
	for (Language language : { Language::Chinese, Language::English, Language::Japanese }) {
		band.config.language = language;
		tooltip = band.BuildTooltip();
		Require(tooltip.find(L"6.2") != std::wstring::npos && tooltip.find(L"3.5 m/s") != std::wstring::npos
			&& tooltip.find(L"65%") != std::wstring::npos && tooltip.find(L"24.0℃") != std::wstring::npos
			&& tooltip.find(L"30%") != std::wstring::npos, "all optional values in each language");
		Require(std::count(tooltip.begin(), tooltip.end(), L'\n') <= 8, "hover height is bounded");
		HDC dc = GetDC(band.tooltipHwnd);
		HFONT font = reinterpret_cast<HFONT>(SendMessageW(band.tooltipHwnd, WM_GETFONT, 0, 0));
		HGDIOBJ previous = SelectObject(dc, font ? font : GetStockObject(DEFAULT_GUI_FONT));
		auto measure = [&](const std::wstring& text) { SIZE size{}; GetTextExtentPoint32W(dc, text.c_str(), static_cast<int>(text.size()), &size); return size.cx; };
		// 穷举全部勾选组合，保证可选行不超过时间行的实际像素宽度。
		for (unsigned mask = 0; mask < 128; ++mask) {
			band.config.tooltipFields = mask;
			std::wstring text = band.BuildTooltip();
			size_t status = text.rfind(L'\n');
			size_t refresh = text.rfind(L'\n', status - 1);
			size_t start = refresh == std::wstring::npos ? 0 : refresh + 1;
			int limit = measure(text.substr(start, status - start));
			for (size_t row = 0; row < start;) {
				size_t end = text.find(L'\n', row);
				Require(measure(text.substr(row, end - row)) <= limit, "optional tooltip rows fit timestamp width in all languages");
				row = end + 1;
			}
		}
		SelectObject(dc, previous); ReleaseDC(band.tooltipHwnd, dc);
		band.config.tooltipFields = 127;
	}
	band.config.language = Language::English;
	band.lastFetchFailed = true;
	Require(band.BuildTooltip().find(L"previous forecast") != std::wstring::npos, "failure uses cached state");
	band.lastSuccessfulFetch = now - 9 * 3600;
	Require(band.BuildTooltip().find(L"stale") != std::wstring::npos, "stale forecasts are identified");
	band.config.language = Language::Japanese;
	Require(band.BuildTooltip().find(L"予報") != std::wstring::npos, "Japanese hover text");
	band.generation = 7;
	band.inFlight = true;
	auto* old = new FetchResult();
	old->owner = &band;
	old->generation = 6;
	old->ok = true;
	band.OnWeather(old);
	Require(!band.inFlight && band._weather.cache.hasData, "old requests must finish without replacing the current cache");
}
