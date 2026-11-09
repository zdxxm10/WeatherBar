#pragma once
#include <string>
#include <windows.h>
#include <stdexcept>

struct HourPoint {
	bool valid = false;
	std::wstring temperature;
	int code = -1;
	// 缺失的可选数据保持空字符串，不能把 null 当作 0。
	std::wstring uvIndex, windDirection, windSpeed, humidity, apparentTemperature, precipitationProbability;
};

struct DayForecast {
	int dateKey = -1;
	int code = -1;
	std::wstring maxTemperature;
	std::wstring minTemperature;
	int sunriseHour = 6;
	int sunsetHour = 18;
	HourPoint hours[24]{};
};

struct ForecastCache {
	bool hasData = false;
	bool timezoneValid = false;
	int utcOffsetSeconds = 0;
	std::wstring timezone;
	DayForecast days[3]{};
};

struct FetchResult {
	void* owner = nullptr;
	unsigned long long generation = 0;
	bool ok = false;
	ForecastCache cache;
};

constexpr UINT WM_WEATHER_READY = WM_APP + 1;

class Weather {
public:
	void SetURL(std::wstring url);
	void BeginFetch(HWND hwnd, void* owner, unsigned long long generation);
	ForecastCache Fetch() const;
	bool HasDay(int dateKey) const;
	const DayForecast* FindDay(int dateKey) const;
	ForecastCache cache;

private:
	std::wstring base;
};

class WeatherHttpError : public std::runtime_error {
public:
	explicit WeatherHttpError(const char* message) : std::runtime_error(message) {}
};

ForecastCache ParseLocationForecast(const std::string& body);
std::wstring PrepareForecastUrl(std::wstring url);
std::string WeatherHttpGet(const std::wstring& url);
std::wstring FormatUtcOffset(int seconds);

bool BreakLocalTime(time_t utc, int offsetSeconds, tm& local);
int DateKeyFromUtc(time_t utc, int offsetSeconds);
time_t NextHourUtc(time_t now, int offsetSeconds);
time_t NextScheduleUtc(time_t now, int offsetSeconds, const int* hours, int hourCount, int delaySeconds);
