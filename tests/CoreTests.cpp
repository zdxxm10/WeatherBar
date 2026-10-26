#include "TestSupport.h"
#include <windows.h>
#include <atlbase.h>
#include <shlwapi.h>
#include <xmllite.h>
#include <cstdio>
#include <sstream>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <thread>
#include <atomic>
#include <functional>
#include <climits>
#include <memory>
#include <type_traits>

enum class WriteFault { None, ShortWrite, ShortBodyWrite, CloseFailure };
WriteFault writeFault = WriteFault::None;

size_t TestWrite(const void* buffer, size_t size, size_t count, FILE* file) {
	if (writeFault == WriteFault::ShortWrite || (writeFault == WriteFault::ShortBodyWrite && count > 3)) {
		writeFault = WriteFault::None;
		return fwrite(buffer, size, count / 2, file);
	}
	return fwrite(buffer, size, count, file);
}

int TestClose(FILE* file) {
	int result = fclose(file);
	if (writeFault == WriteFault::CloseFailure) {
		writeFault = WriteFault::None;
		return EOF;
	}
	return result;
}

// 在真实保存实现的文件写入边界注入故障，不需要改动正式代码接口。
#define fwrite TestWrite
#define fclose TestClose
#include "../src/ImportConfig.cpp"
#undef fwrite
#undef fclose
#include "../src/Weather.cpp"
#include "../src/Update.h"
#include "../src/DeskBand.h"
#include "../src/Settings.h"

std::atomic<long> moduleLocks{ 0 };
void LockModule(BOOL lock) {
	moduleLocks.fetch_add(lock ? 1 : -1);
}

std::string ReadConfigBytes() {
	std::ifstream input(std::filesystem::path(ConfigFilePath()), std::ios::binary);
	return { std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
}

void WriteConfigText(const std::string& text) {
	std::ofstream output(std::filesystem::path(ConfigFilePath()), std::ios::binary);
	output << text;
	output.close();
	Require(!output.fail(), "test fixture must be written");
}

void TestDefaultsAndRoundTrip() {
	DeleteFileW(ConfigFilePath().c_str());
	BandConfig config = DefaultBandConfig();
	Require(config.scheduleHours == std::vector<int>({ 0, 6, 12, 18 }), "default schedule");
	Require(config.delaySeconds == 900 && config.timezoneOffsetHours == 8, "default delay and timezone");
	Require(config.tooltipEnabled && config.tooltipFields == 0, "default tooltip only shows fixed refresh and status");
	config.currentColor = RGB(12, 34, 56);
	config.showMin = false;
	config.tooltipEnabled = false; config.tooltipFields = 85;
	config.delaySeconds = 123;
	config.icons = { { -1, L"未知图标" }, { 0, L"sunny" } };
	Require(SaveBandConfig(config), "initial config save");
	std::string bytes = ReadConfigBytes();
	Require(bytes.starts_with("\xEF\xBB\xBF"), "UTF-8 BOM must be preserved");
	BandConfig loaded = LoadBandConfig();
	Require(loaded.url == config.url && loaded.currentColor == config.currentColor, "URL and color round trip");
	Require(!loaded.tooltipEnabled && loaded.tooltipFields == 85, "tooltip settings round trip");
	Require(loaded.icons == config.icons && !loaded.showMin && loaded.delaySeconds == 123, "Unicode icon names and hidden/stagger settings round trip");
	Require(ReadConfigBytes() == bytes, "loading current config must not rewrite it");
}

void TestLanguageAndMigration() {
	Require(LanguageForLocale(L"zh-CN") == Language::Chinese && LanguageForLocale(L"zh-SG") == Language::Chinese, "simplified Chinese locales");
	Require(LanguageForLocale(L"ja-JP") == Language::Japanese && LanguageForLocale(L"en-GB") == Language::English, "Japanese and English locales");
	Require(LanguageForLocale(L"zh-TW") == Language::Chinese && LanguageForLocale(L"zh-HK") == Language::Chinese, "Chinese regions use simplified Chinese text");
	Require(LanguageForLocale(L"fr-FR") == Language::English && LanguageForLocale(L"de-DE") == Language::English, "unsupported locales fall back to English");
	for (Language language : { Language::System, Language::Chinese, Language::English, Language::Japanese }) {
		BandConfig config = DefaultBandConfig();
		config.language = language;
		Require(SaveBandConfig(config) && LoadBandConfig().language == language, "language round trip");
	}
	DeleteFileW((ConfigFilePath() + L".before-v2.bak").c_str());
	std::string old = R"(<WeatherBar><Data><ForecastUrl value="39.9286, 116.4103"/><StaggerSeconds value="73"/></Data><Current><Color value="123456"/></Current><Icon key="0" filename="my-sun"/></WeatherBar>)";
	WriteConfigText(old);
	BandConfig migrated = LoadBandConfig();
	Require(migrated.tooltipEnabled && migrated.tooltipFields == 0, "old configuration without tooltip fields uses new defaults");
	Require(migrated.language == Language::System && migrated.delaySeconds == 73 && FormatColorText(migrated.currentColor) == L"123456", "migration retains user values and fills defaults");
	Require(migrated.icons[0].second == L"my-sun", "custom icon mapping is retained");
	Require(ReadConfigBytes().find("<Language") != std::string::npos, "missing language is added to old configuration");
	std::ifstream backup(std::filesystem::path(ConfigFilePath() + L".before-v2.bak"), std::ios::binary);
	std::string backupBytes{ std::istreambuf_iterator<char>(backup), std::istreambuf_iterator<char>() };
	Require(backupBytes == old, "migration backup is an exact copy");
	for (const std::string& invalid : { std::string("<WeatherBar><Data>"), std::string("<OtherApp/>"), std::string("<WeatherBar version=\"invalid\"/>") }) {
		WriteConfigText(invalid);
		BandConfig config = LoadBandConfig();
		Require(!config.canSave && !SaveBandConfig(config) && ReadConfigBytes() == invalid, "damaged or unrelated configuration is never overwritten");
	}
	WriteConfigText(R"(<WeatherBar version="99"><General><Language value="ja"/></General><Future value="keep"/></WeatherBar>)");
	std::string future = ReadConfigBytes();
	BandConfig config = LoadBandConfig();
	Require(!config.canSave && !SaveBandConfig(config) && ReadConfigBytes() == future, "future schemas are protected from downgrade");
	WriteConfigText("<WeatherBar/>");
	Require(LoadBandConfig().canSave, "valid minimal config is migrated");
}

void TestRenamedConfiguration() {
	DeleteFileW(ConfigFilePath().c_str());
	std::wstring oldPath = ModuleDirectory() + L"\\" + kLegacyConfigFile;
	std::wstring backupPath = oldPath + L".before-v2.bak";
	DeleteFileW(backupPath.c_str());
	std::string root = WideToUtf8(kLegacyConfigRoot);
	std::string original = "<" + root + R"( version="2"><General><Language value="ja"/></General><Current><Color value="123456"/></Current></)" + root + ">";
	{ std::ofstream file(std::filesystem::path(oldPath), std::ios::binary); file << original; }
	BandConfig migrated = LoadBandConfig();
	Require(migrated.language == Language::Japanese && FormatColorText(migrated.currentColor) == L"123456", "renaming retains legacy configuration values");
	Require(ReadConfigBytes().find("<WeatherBar") != std::string::npos, "migration creates new branded configuration");
	{ std::ifstream file(std::filesystem::path(oldPath), std::ios::binary); std::string bytes{ std::istreambuf_iterator<char>(file), {} }; Require(bytes == original, "legacy source remains untouched"); }
	WriteConfigText("<Broken");
	Require(!LoadBandConfig().canSave && ReadConfigBytes() == "<Broken", "damaged new file does not silently fall back to legacy values");
	DeleteFileW(ConfigFilePath().c_str());
	std::string future = "<" + root + R"( version="99"/>)";
	{ std::ofstream file(std::filesystem::path(oldPath), std::ios::binary); file << future; }
	Require(!LoadBandConfig().canSave && GetFileAttributesW(ConfigFilePath().c_str()) == INVALID_FILE_ATTRIBUTES, "future legacy schema is not downgraded into new file");
	DeleteFileW(oldPath.c_str()); DeleteFileW(backupPath.c_str());
}

void TestUpdateClassification() {
	std::wstring redirectVersion;
	Require(ClassifyReleaseRedirect(L"https://github.com/zdxxm10/WeatherBar/releases/tag/v99.0.0", redirectVersion) == UpdateStatus::Available, "official latest-release redirect");
	for (const wchar_t* url : { L"https://evil.example/releases/tag/v99.0.0", L"https://github.com/other/WeatherBar/releases/tag/v99.0.0", L"https://github.com/zdxxm10/WeatherBar/releases/tag/v1.0.0-beta", L"https://github.com/zdxxm10/WeatherBar/releases/tag/v1.0.0?x=1" })
		Require(ClassifyReleaseRedirect(url, redirectVersion) == UpdateStatus::Failed, "reject foreign URLs and nonstable redirect tags");
	std::wstring version;
	Require(ClassifyRelease(404, "", version) == UpdateStatus::NoRelease, "no published release");
	Require(ClassifyRelease(403, "rate limited", version) == UpdateStatus::Failed, "rate limiting is not success");
	Require(ClassifyRelease(200, R"({"tag_name":"v1.0"})", version) == UpdateStatus::Current, "two-part current version");
	Require(ClassifyRelease(200, R"({"tag_name":"v1.0.0"})", version) == UpdateStatus::Current, "equivalent three-part version");
	Require(ClassifyRelease(200, R"({"tag_name":"v1.0.1"})", version) == UpdateStatus::Available, "new patch release");
	Require(ClassifyRelease(200, R"({"tag_name":"v1.1"})", version) == UpdateStatus::Available, "new two-part minor release");
	Require(ClassifyReleaseRedirect(L"https://github.com/zdxxm10/WeatherBar/releases/tag/v1.0", version) == UpdateStatus::Current, "two-part release-page fallback");
	for (const wchar_t* invalid : { L"1", L"1.", L"1.0.", L"1.0.0.0", L"1.0-beta", L"1.0?x=1", L"-1.0" }) {
		int parts[3]{};
		Require(!ParseReleaseVersion(invalid, parts), "malformed or nonstable versions rejected");
	}
	Require(ClassifyRelease(200, R"({"tag_name":"v0.0.9"})", version) == UpdateStatus::Current, "older remote version does not trigger downgrade");
	Require(ClassifyRelease(200, R"({"tag_name":"v0.2.0-beta"})", version) == UpdateStatus::Failed, "prerelease labels are not installed");
	Require(ClassifyRelease(200, "not JSON", version) == UpdateStatus::Failed, "invalid response is not latest");
}

void TestLegacyConfig() {
	WriteConfigText(R"(<WeatherBar><数据><接口地址 value="39.9286, 116.4103"/><时区偏移 value="99"/><错峰秒数 value="1900"/></数据><图标><档位 value="中"/><边长 value="48"/></图标><当前温度><字号 value="8"/><档位 value="大"/></当前温度><间距><档位 value="小"/></间距><Icon key="-1" filename="None"/></WeatherBar>)");
	BandConfig config = LoadBandConfig();
	Require(config.timezoneOffsetHours == 14 && config.delaySeconds == 1800, "legacy bounds normalization");
	Require(config.iconLevel == SizeLevel::Medium && config.currentLevel == SizeLevel::Large, "new levels must take precedence over legacy pixel sizes");
	Require(config.gapIconLevel == SizeLevel::Small && config.gapExtremeLevel == SizeLevel::Small, "legacy shared spacing");
	Require(ReadConfigBytes().find("<![CDATA[") != std::string::npos, "legacy URL must migrate to CDATA");
	WriteConfigText(R"(<WeatherBar><Icon><Edge value="48"/></Icon></WeatherBar>)");
	Require(LoadBandConfig().iconLevel == SizeLevel::Large, "level tracking must reset for each load");
}

void TestConfigWriteFailure(WriteFault fault) {
	BandConfig config = DefaultBandConfig();
	Require(SaveBandConfig(config), "save old config before injecting failure");
	std::string oldBytes = ReadConfigBytes();
	config.delaySeconds = 42;
	writeFault = fault;
	bool saved = SaveBandConfig(config);
	writeFault = WriteFault::None;
	Require(!saved, "short writes and close failures must report failure");
	Require(ReadConfigBytes() == oldBytes, "failed writes must preserve previous configuration");
	for (const auto& entry : std::filesystem::directory_iterator(ModuleDirectory()))
		Require(entry.path().extension() != L".tmp", "failed save must clean up temporary files");
}

void TestConfigSharingFailure() {
	BandConfig config = DefaultBandConfig();
	Require(SaveBandConfig(config), "save before locking config");
	std::string oldBytes = ReadConfigBytes();
	HANDLE file = CreateFileW(ConfigFilePath().c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
	Require(file != INVALID_HANDLE_VALUE, "lock test file");
	config.delaySeconds = 73;
	bool saved = SaveBandConfig(config);
	CloseHandle(file);
	Require(!saved && ReadConfigBytes() == oldBytes, "sharing failures must preserve old config");
	Require(SaveBandConfig(config), "saving must recover after lock is released");
}

void TestReadOnlyConfig() {
	BandConfig config = DefaultBandConfig();
	Require(SaveBandConfig(config), "save before read-only test");
	std::string oldBytes = ReadConfigBytes();
	Require(SetFileAttributesW(ConfigFilePath().c_str(), FILE_ATTRIBUTE_READONLY) != FALSE, "make test config read-only");
	config.delaySeconds = 81;
	bool saved = SaveBandConfig(config);
	BOOL restored = SetFileAttributesW(ConfigFilePath().c_str(), FILE_ATTRIBUTE_NORMAL);
	Require(restored != FALSE && !saved && ReadConfigBytes() == oldBytes, "read-only config must remain intact");
	Require(SaveBandConfig(config), "saving must recover after read-only attribute is removed");
}

void TestUrlAndColors() {
	std::wstring url, error;
	Require(CanonicalForecastUrl(L" 39.9286，116.4103 ", url, error), "Chinese coordinate separator");
	Require(url.find(L"timeformat=unixtime") != std::wstring::npos, "required forecast parameters");
	Require(CanonicalForecastUrl(L"https://api.open-meteo.com/v1/forecast?latitude=1&longitude=-2&timezone=Europe%2FBerlin", url, error), "existing URL input");
	Require(url.find(L"Europe%2FBerlin") != std::wstring::npos, "timezone must be preserved");
	Require(!CanonicalForecastUrl(L"", url, error) && !error.empty(), "empty input must have an error");
	COLORREF color = RGB(1, 2, 3);
	Require(ParseColorText(L"#aB00fF", color) && FormatColorText(color) == L"AB00FF", "color compatibility");
	Require(!ParseColorText(L"GG0000", color) && FormatColorText(color) == L"AB00FF", "invalid colors must preserve output");
}

time_t UtcTime(int year, int month, int day, int hour = 0, int minute = 0) {
	tm value{};
	value.tm_year = year - 1900;
	value.tm_mon = month - 1;
	value.tm_mday = day;
	value.tm_hour = hour;
	value.tm_min = minute;
	return _mkgmtime(&value);
}

void TestSchedule() {
	const int hours[] = { 0, 6, 12, 18 };
	time_t now = UtcTime(2026, 12, 31, 16);
	Require(NextScheduleUtc(now, 8, hours, 4, 900) == now + 900, "UTC+8 midnight stagger");
	Require(NextScheduleUtc(now + 900, 8, hours, 4, 900) == now + 6 * 3600 + 900, "exact update time must advance to next slot");
	Require(NextHourUtc(now, 8) == now + 3600, "exact hour must advance");
	Require(NextHourUtc(now + 1, -12) == now + 3600, "negative timezone");
	Require(NextScheduleUtc(now, 14, nullptr, 0, 0) == now + 6 * 3600, "empty schedule fallback");
	Require(DateKeyFromUtc(now, 8) == DateKeyFromUtc(UtcTime(2027, 1, 1), 0), "new year timezone rollover");
	Require(NextHourUtc(-1, 0) == 0, "floor division before Unix epoch");
}

void TestForecastParsing() {
	time_t now = time(nullptr);
	tm today{};
	Require(BreakLocalTime(now, 0, today), "current UTC time");
	today.tm_hour = today.tm_min = today.tm_sec = 0;
	time_t midnight = _mkgmtime(&today);
	std::string stamp = std::to_string(midnight);
	std::string tomorrow = std::to_string(midnight + 86400);
	std::string body = "{\"hourly\":{\"time\":[" + stamp + "," + tomorrow + "],\"temperature_2m\":[21.25,null],\"weather_code\":[0,3]},\"daily\":{\"time\":[" + stamp + "," + tomorrow + "],\"temperature_2m_max\":[30,31],\"temperature_2m_min\":[10,11]}}";
	ForecastCache cache = ParseForecast(body, 0);
	Require(cache.hasData && cache.days[0].hours[0].valid, "valid hourly forecast");
	Require(cache.days[0].hours[0].code == 100, "nighttime clear-sky icon mapping");
	Require(!cache.days[1].hours[0].valid && cache.days[1].maxTemperature == L"31.0℃", "null hourly data and tomorrow extremes");
	std::string fallback = "{\"current_weather\":{\"time\":" + stamp + ",\"temperature\":-5.5,\"weathercode\":3}}";
	Require(ParseForecast(fallback, 0).days[0].hours[0].temperature == L"-5.5℃", "legacy current-weather fallback");
	std::string extra = "{\"hourly\":{\"time\":[" + stamp + "],\"temperature_2m\":[21],\"uv_index\":[6.2],\"wind_direction_10m\":[45],\"wind_speed_10m\":[3.5],\"relative_humidity_2m\":[65],\"apparent_temperature\":[-2.5],\"precipitation_probability\":[30]}}";
	auto point = ParseForecast(extra, 0).days[0].hours[0];
	Require(point.uvIndex == L"6.2" && point.windDirection == L"45" && point.windSpeed == L"3.5"
		&& point.humidity == L"65" && point.apparentTemperature == L"-2.5" && point.precipitationProbability == L"30", "six additional forecast fields and negative feels-like");
	extra.replace(extra.find("[65]"), 4, "[null]");
	extra.replace(extra.find("[30]"), 4, "[101]");
	point = ParseForecast(extra, 0).days[0].hours[0];
	Require(point.humidity.empty() && point.precipitationProbability.empty() && point.valid, "optional null and out-of-range values do not discard temperature");
	Instant instant;
	Require(InstantFromText("2026-01-01T00:30", 8, instant) && instant.utc == UtcTime(2025, 12, 31, 16, 30), "text timestamp timezone conversion");
	bool threw = false;
	try { ParseForecast("{\"hourly\":{\"time\":[],\"temperature_2m\":[]}}", 0); }
	catch (const std::runtime_error&) { threw = true; }
	Require(threw, "empty forecasts must fail explicitly");
}

struct WeatherMessages {
	int received = 0;
	bool failedAsExpected = false;
};

LRESULT CALLBACK WeatherTestWindowProc(HWND hwnd, UINT message, WPARAM wp, LPARAM lp) {
	if (message == WM_WEATHER_READY) {
		std::unique_ptr<FetchResult> result(reinterpret_cast<FetchResult*>(lp));
		auto* state = reinterpret_cast<WeatherMessages*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
		++state->received;
		state->failedAsExpected = !result->ok && result->owner == state;
		return 0;
	}
	return DefWindowProcW(hwnd, message, wp, lp);
}

bool PumpUntilIdle(const WeatherMessages& state, int expectedMessages) {
	ULONGLONG deadline = GetTickCount64() + 5000;
	do {
		MSG message{};
		while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE))
			DispatchMessageW(&message);
		if (moduleLocks == 0 && state.received == expectedMessages)
			return true;
		Sleep(1);
	} while (GetTickCount64() < deadline);
	return false;
}

void TestAsyncLifecycle() {
	static_assert(!std::is_copy_constructible_v<HttpHandle>);
	WNDCLASSW klass{};
	klass.lpfnWndProc = WeatherTestWindowProc;
	klass.hInstance = g_hinstDll;
	klass.lpszClassName = L"WeatherBarRegressionMessages";
	Require(RegisterClassW(&klass) != 0, "register test-only message window");
	HWND hwnd = CreateWindowExW(0, klass.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, g_hinstDll, nullptr);
	Require(hwnd != nullptr, "create message-only test window");
	WeatherMessages state;
	SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&state));
	Weather weather;
	// URL 在解析阶段直接失败，不访问互联网。
	weather.SetURL(L"invalid weather URL");
	int startupFailures = 0;
	for (int failurePoint = 0; failurePoint < 16; ++failurePoint) {
		bool started = false;
		FailAllocationAfter(failurePoint);
		try {
			weather.BeginFetch(hwnd, &state, 1, 8);
			started = true;
		}
		catch (const std::bad_alloc&) { ++startupFailures; }
		FailAllocationAfter(-1);
		if (started) {
			int expectedMessages = state.received + 1;
			Require(PumpUntilIdle(state, expectedMessages), "background request must return and release its DLL lock");
			Require(state.failedAsExpected, "failed requests must notify the owning window");
		}
		else {
			Require(moduleLocks == 0, "thread startup failures must release the DLL lock");
		}
	}
	Require(startupFailures > 0 && state.received > 0, "exercise failed and successful thread startup");
	DestroyWindow(hwnd);
	weather.BeginFetch(hwnd, &state, 2, 8);
	Require(PumpUntilIdle(state, state.received), "closed-window delivery failure must release the DLL lock");
	UnregisterClassW(klass.lpszClassName, g_hinstDll);
}

void TestPackagedResources() {
	std::filesystem::path repository = std::filesystem::path(ModuleDirectory()).parent_path().parent_path().parent_path();
	std::filesystem::path executable = repository / L"x64" / L"Release" / L"WeatherBar.exe";
	// 只读取 EXE 的资源，不调用启动器入口，也不注册或安装插件。
	HMODULE resources = LoadLibraryExW(executable.c_str(), nullptr, LOAD_LIBRARY_AS_DATAFILE);
	Require(resources != nullptr, "open packaged launcher as resource data");
	try {
		HRSRC dll = FindResourceW(resources, MAKEINTRESOURCEW(101), RT_RCDATA);
		Require(dll != nullptr && SizeofResource(resources, dll) > 2, "packaged DLL resource");
		const BYTE* image = static_cast<const BYTE*>(LockResource(LoadResource(resources, dll)));
		Require(image && image[0] == 'M' && image[1] == 'Z', "DLL resource must be a PE image");
		Require(FindResourceW(resources, L"PROJECT", L"LICENSE") && FindResourceW(resources, L"METEOCONS", L"LICENSE"), "standalone EXE includes project and icon license notices");
		for (const auto& entry : std::filesystem::directory_iterator(repository / L"src" / L"icons")) {
			if (entry.path().extension() != L".svg")
				continue;
			std::wstring name = entry.path().stem().wstring();
			HRSRC icon = FindResourceW(resources, name.c_str(), L"SVG");
			Require(icon != nullptr, "all SVG icons must be packaged");
			std::ifstream input(entry.path(), std::ios::binary);
			std::string expected{ std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>() };
			const void* bytes = LockResource(LoadResource(resources, icon));
			Require(bytes && SizeofResource(resources, icon) == expected.size(), "packaged SVG size must match source");
			Require(memcmp(bytes, expected.data(), expected.size()) == 0, "packaged SVG bytes must match source");
		}
	}
	catch (...) {
		FreeLibrary(resources);
		throw;
	}
	FreeLibrary(resources);
}

int wmain(int argc, wchar_t** argv) {
	g_hinstDll = GetModuleHandleW(nullptr);
	HRESULT initialized = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
	if (FAILED(initialized))
		return 2;
	if (argc > 1 && std::wstring(argv[1]) == L"--check-update") {
		auto check = StartUpdateCheck();
		while (!check->done.load(std::memory_order_acquire)) Sleep(20);
		std::wcout << L"status=" << static_cast<int>(check->status) << L" http=" << check->httpStatus
			<< L" fallback=" << check->usedReleasePage << L" win32=" << check->errorCode << L" version=" << check->latestVersion << L'\n';
		CoUninitialize();
		return check->status == UpdateStatus::Failed ? 1 : 0;
	}
	if (argc > 1 && std::wstring(argv[1]) == L"--preview-settings") {
		BandConfig config = DefaultBandConfig();
		config.language = argc > 2 ? ParseLanguage(argv[2]) : Language::Chinese;
		CDeskBand band;
		HWND hwnd = ShowBandSettings(nullptr, &band, config);
		ShowWindow(hwnd, SW_SHOWNORMAL);
		MSG message{};
		while (IsWindow(hwnd) && GetMessageW(&message, nullptr, 0, 0) > 0) { TranslateMessage(&message); DispatchMessageW(&message); }
		CoUninitialize();
		return hwnd ? 0 : 1;
	}
	int failures = 0;
	auto run = [&](const char* name, const std::function<void()>& test) {
		try { test(); std::cout << "PASS " << name << '\n'; }
		catch (const std::exception& error) { ++failures; std::cerr << "FAIL " << name << ": " << error.what() << '\n'; }
	};
	run("defaults and UTF-8 config round trip", TestDefaultsAndRoundTrip);
	run("language fallback and protected migration", TestLanguageAndMigration);
	run("product rename configuration migration", TestRenamedConfiguration);
	run("manual update response classification", TestUpdateClassification);
	run("legacy config migration and precedence", TestLegacyConfig);
	run("short write preserves config", [] { TestConfigWriteFailure(WriteFault::ShortWrite); });
	run("short XML body write preserves config", [] { TestConfigWriteFailure(WriteFault::ShortBodyWrite); });
	run("close failure preserves config", [] { TestConfigWriteFailure(WriteFault::CloseFailure); });
	run("file sharing failure and recovery", TestConfigSharingFailure);
	run("read-only config and recovery", TestReadOnlyConfig);
	run("URL and color compatibility", TestUrlAndColors);
	run("schedule and timezone boundaries", TestSchedule);
	run("forecast parsing and fallback", TestForecastParsing);
	run("async startup, error delivery and closed-window cleanup", TestAsyncLifecycle);
	run("font and SVG resource reuse", TestDisplayResources);
	run("localized save, cancel and appearance reset", TestSettingsBehavior);
	run("hover status and stale request rejection", TestTooltipAndRequestGeneration);
	run("packaged DLL and SVG integrity", TestPackagedResources);
	run("balanced module references", [] { Require(moduleLocks == 0, "module lock must be balanced"); });
	CoUninitialize();
	std::cout << "Failures: " << failures << '\n';
	return failures ? 1 : 0;
}
