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
	band.config.tooltipRefresh = true; band.config.tooltipStatus = true;
	band.tooltipHwnd = CreateWindowExW(0, TOOLTIPS_CLASSW, nullptr, WS_POPUP | TTS_NOPREFIX,
		0, 0, 0, 0, nullptr, nullptr, g_hinstDll, nullptr);
	Require(band.tooltipHwnd != nullptr, "native tooltip window for actual font measurements");
	band.config.locationMode = LocationMode::Custom;
	band.config.url = BuildForecastUrl(L"35",L"139",L"auto");
	Require(band.BuildTooltip().find(L"No weather data yet") != std::wstring::npos, "empty hover state");
	time_t now = time(nullptr);
	tm local{};
	Require(BreakLocalTime(now, band.UtcOffsetSeconds(), local), "tooltip local time");
	auto& day = band._weather.cache.days[0];
	day.dateKey = DateKeyFromUtc(now, band.UtcOffsetSeconds());
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
	for (unsigned fields : { 0u, 1u, 128u, 129u }) {
		band.config.tooltipFields = fields;
		std::wstring text = band.BuildTooltip();
		Require((text.find(L"Tomorrow") != std::wstring::npos) == !!(fields & 1u), "tomorrow independent switch");
		Require((text.find(L"Day after tomorrow") != std::wstring::npos) == !!(fields & 128u), "day after independent switch");
		if (fields) Require(text.find(L"Unknown weather -- / --") != std::wstring::npos, "missing future data retains both positions");
	}
	for (int i = 1; i <= 2; ++i) {
		auto& future = band._weather.cache.days[i];
		future.dateKey = DateKeyFromUtc(now + i * 86400, band.UtcOffsetSeconds());
		future.code = 2; future.minTemperature = L"13℃"; future.maxTemperature = L"22℃";
	}
	band.config.tooltipFields = 1;
	Require(band.BuildTooltip().find(L"Tomorrow: Partly cloudy 13℃ / 22℃") != std::wstring::npos, "short entry stays on one row with low first");
	band._weather.cache.days[1].minTemperature = std::wstring(80, L'9');
	Require(band.BuildTooltip().find(L"Tomorrow\nPartly cloudy") != std::wstring::npos, "long entry splits date from content");
	band._weather.cache.days[1].minTemperature = L"13℃";
	band.config.tooltipFields = 255;
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
		for (unsigned mask = 0; mask < 256; ++mask) {
			band.config.tooltipFields = mask;
			std::wstring text = band.BuildTooltip();

   int limit = static_cast<int>(SendMessageW(band.tooltipHwnd, TTM_GETMAXTIPWIDTH, 0, 0)) - 2;
   size_t footer = text.find(L"\n" + std::wstring(Text(language,L"上次刷新：",L"Last refresh: ",L"最終更新：")));
   if (footer == std::wstring::npos) footer = 0;
   for (size_t row = 0; row < footer;) {
    size_t end = text.find(L'\n', row);
    Require(measure(text.substr(row, end - row)) <= limit, "optional tooltip rows fit bounded width in all languages");
    row = end + 1;
   }
		}
		SelectObject(dc, previous); ReleaseDC(band.tooltipHwnd, dc);
		band.config.tooltipFields = 255;
	}
	band.config.language = Language::English;
	band.lastFetchFailed = true;
	Require(band.BuildTooltip().find(L"previous forecast") != std::wstring::npos, "failure uses cached state");
	band.lastSuccessfulFetch = now - 9 * 3600;
	Require(band.BuildTooltip().find(L"stale") != std::wstring::npos, "stale forecasts are identified");
	band.config.language = Language::Japanese;
	Require(band.BuildTooltip().find(L"予報") != std::wstring::npos, "Japanese hover text");

 band.config.language=Language::English; band.config.tooltipFields=0;
 for (bool refresh : {false,true}) for (bool status : {false,true}) {
  band.config.tooltipRefresh=refresh; band.config.tooltipStatus=status;
  auto text=band.BuildTooltip();
  Require((text.find(L"Last refresh:")!=std::wstring::npos)==refresh && (text.find(L"Status:")!=std::wstring::npos)==status,"footer combinations are independent");
  Require(text.empty()==(!refresh && !status),"all unchecked produces no empty popup content");
  Require(text.empty() || (text.front()!=L'\n' && text.back()!=L'\n' && text.find(L"\n\n")==std::wstring::npos),"no empty separator lines");
 }
 band.config.tooltipRefresh=true; band.config.tooltipStatus=false;
 band.lastSuccessfulFetch=0; Require(band.BuildTooltip()==L"Last refresh: Not refreshed yet","no successful refresh has explicit text");
 band.lastSuccessfulFetch=now; band._weather.cache.timezoneValid=true;
 band._weather.cache.utcOffsetSeconds=20700; auto systemStamp=band.BuildTooltip();
 band._weather.cache.utcOffsetSeconds=-12600;
 Require(band.BuildTooltip()==systemStamp && systemStamp.find(L"UTC")==std::wstring::npos,"refresh display is independent of weather timezone");
 ULARGE_INTEGER ticks{};ticks.QuadPart=(static_cast<ULONGLONG>(now)+11644473600ULL)*10000000ULL;
 FILETIME file{ticks.LowPart,ticks.HighPart}; FILETIME localFile{}; SYSTEMTIME expected{};
 Require(FileTimeToLocalFileTime(&file,&localFile) && FileTimeToSystemTime(&localFile,&expected),"system timezone reference conversion");
 wchar_t formatted[40]{}; swprintf_s(formatted,L"%04u-%02u-%02u %02u:%02u",expected.wYear,expected.wMonth,expected.wDay,expected.wHour,expected.wMinute);
 Require(systemStamp==L"Last refresh: "+std::wstring(formatted),"refresh uses computer local time without offset label");
 auto accepted=band.lastSuccessfulFetch;
 band.config.language=Language::Japanese; band.BuildTooltip(); band.config.language=Language::English;
 Require(band.lastSuccessfulFetch==accepted,"language and rendering never update successful time");
 band.failedUrl=band.config.url; band.generation=6;
 auto* failure=new FetchResult();failure->owner=&band;failure->generation=6;failure->ok=false;band.OnWeather(failure);
 Require(band.lastSuccessfulFetch==accepted,"failed request does not change successful time");
	band.generation = 7;
	band.inFlight = true;
	auto* old = new FetchResult();
	old->owner = &band;
	old->generation = 6;
	old->ok = true;
	band.OnWeather(old);
	Require(!band.inFlight && band._weather.cache.hasData && band.lastSuccessfulFetch==accepted, "old requests never replace cache or successful time");
}

void TestUnselectedLocation() {
 BandConfig empty = DefaultBandConfig(Language::English);
 Require(!HasConfiguredLocation(empty) && empty.url.empty() && empty.latitude.empty() && empty.longitude.empty() && empty.locationName.empty() && empty.searchCountry.empty(), "fresh settings have no implicit location");
 Require(SaveBandConfig(empty), "save empty location with appearance");
 CDeskBand band;
 Require(!HasConfiguredLocation(band.config), "restart remains unselected");
 HWND fixture = CreateWindowExW(0,L"STATIC",L"test",WS_POPUP,0,0,100,100,nullptr,nullptr,g_hinstDll,nullptr);
 Require(fixture!=nullptr,"isolated scheduling window");
 band.m_hwnd=fixture;
 band.StartFetch(); band.ArmTimer(); band.OnTimer();
 Require(!band.inFlight && !band.followUp && band.armedUntil==0 && !band.armedForSchedule, "unselected refresh and timers never start weather");
 band.BeginSettingsPreview();
 auto selected = empty; selected.locationMode=LocationMode::Coordinates; selected.latitude=L"0";selected.longitude=L"0";selected.locationName=L"0, 0";selected.url=BuildForecastUrl(L"0",L"0",L"auto");
 ForecastCache response; response.hasData=true;response.timezoneValid=true;response.timezone=L"Etc/GMT";response.utcOffsetSeconds=0;
 band.ApplyPreviewConfig(selected,&response);
 Require(HasConfiguredLocation(band.config) && band.armedUntil>0,"validated zero coordinates enable scheduling");
 auto obsoleteGeneration=band.generation;
 band.CancelSettingsPreview();
 Require(!HasConfiguredLocation(band.config) && band.armedUntil==0 && !band._weather.cache.hasData && !band._weather.cache.timezoneValid,"cancel first location clears cache and stops schedule");
 auto* obsolete=new FetchResult();obsolete->owner=&band;obsolete->generation=obsoleteGeneration;obsolete->ok=true;obsolete->cache=response;
 band.OnWeather(obsolete);
 Require(!band._weather.cache.hasData && !band.inFlight,"late preview result cannot revive unselected location");
 band.config.tooltipStatus=true; auto tooltip=band.BuildTooltip();Require(tooltip.find(L"Choose a location")!=std::wstring::npos,"unselected hover guides user to settings");
 band.m_hwnd=nullptr;DestroyWindow(fixture);
 Require(SaveBandConfig(selected),"save selected zero-coordinate location");
 auto restored=LoadBandConfig();Require(restored.url==selected.url && restored.latitude==L"0" && restored.longitude==L"0","saved valid location restores exactly");
}
