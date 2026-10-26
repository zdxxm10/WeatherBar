#pragma once
#include <string>
#include <windows.h>

struct HourPoint {
	bool valid = false;
	std::wstring temperature;
	int code = -1;
	// 缺失的可选数据保持空字符串，不能把 null 当作 0。
	std::wstring uvIndex, windDirection, windSpeed, humidity, apparentTemperature, precipitationProbability;
};

struct DayForecast {
	int dateKey = -1;
	std::wstring maxTemperature;
	std::wstring minTemperature;
	int sunriseHour = 6;
	int sunsetHour = 18;
	HourPoint hours[24]{};
};

struct ForecastCache {
	bool hasData = false;
	DayForecast days[2]{};
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
	void BeginFetch(HWND hwnd, void* owner, unsigned long long generation, int offsetHours);
	bool HasDay(int dateKey) const;
	const DayForecast* FindDay(int dateKey) const;
	ForecastCache cache;

private:
	std::wstring base;
};

bool BreakLocalTime(time_t utc, int offsetHours, tm& local);
int DateKeyFromUtc(time_t utc, int offsetHours);
time_t NextHourUtc(time_t now, int offsetHours);
time_t NextScheduleUtc(time_t now, int offsetHours, const int* hours, int hourCount, int delaySeconds);
