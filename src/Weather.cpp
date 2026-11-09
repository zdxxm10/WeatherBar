#include "pch.h"
#include "Weather.h"
#include "OutputLog.h"
#include "ModuleReference.h"
#include "ServiceJson.h"
#include <climits>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>
#include <winhttp.h>
#include <cmath>

#pragma comment(lib, "winhttp.lib")

extern void LockModule(BOOL bLock);

namespace {

long long FloorDiv(long long value, long long divisor) {
	long long quotient = value / divisor;
	long long remainder = value % divisor;
	if (remainder != 0 && value < 0)
		--quotient;
	return quotient;
}

std::wstring FormatTemperature(double value) {
	wchar_t buf[32];
	swprintf_s(buf, L"%.1f\u2103", value);
	return buf;
}

struct NumberValue {
	bool valid = false;
	double value = 0;
};

size_t FindKey(const std::string& body, const char* key, size_t from) {
	std::string pattern = std::string("\"") + key + "\"";
	return body.find(pattern, from);
}

std::vector<NumberValue> ParseNumberArray(const std::string& body, size_t keyPos) {
	std::vector<NumberValue> values;
	if (keyPos == std::string::npos)
		return values;
	size_t bracket = body.find('[', keyPos);
	if (bracket == std::string::npos || bracket > keyPos + 48)
		return values;
	size_t i = bracket + 1;
	while (i < body.size() && body[i] != ']') {
		while (i < body.size() && (body[i] == ' ' || body[i] == '\n' || body[i] == '\r' || body[i] == '\t' || body[i] == ','))
			++i;
		if (i >= body.size() || body[i] == ']')
			break;
		if (body.compare(i, 4, "null") == 0) {
			values.push_back({});
			i += 4;
			continue;
		}
		char* end = nullptr;
		double number = strtod(body.c_str() + i, &end);
		if (end == body.c_str() + i)
			break;
		NumberValue value;
		value.valid = true;
		value.value = number;
		values.push_back(value);
		i = (size_t)(end - body.c_str());
	}
	return values;
}

int HourOf(time_t utc, int offsetSeconds) {
	tm local{};
	if (!BreakLocalTime(utc, offsetSeconds, local))
		return -1;
	return local.tm_hour;
}

void EnsureDay(DayForecast& day, int dateKey) {
	if (day.dateKey < 0)
		day.dateKey = dateKey;
}

int NightCode(int code, int hour, int sunriseHour, int sunsetHour) {
	if (code >= 0 && code <= 2 && (hour < sunriseHour || hour > sunsetHour))
		return code + 100;
	return code;
}

std::string SliceObject(const std::string& body, const char* key) {
	size_t pos = FindKey(body, key, 0);
	if (pos == std::string::npos)
		return {};
	size_t next = pos + strlen(key) + 2;
	if (next < body.size() && body[next] == '_')
		return {};
	size_t brace = body.find('{', pos);
	if (brace == std::string::npos || brace > pos + 64)
		return {};
	int depth = 0;
	for (size_t i = brace; i < body.size(); ++i) {
		if (body[i] == '{')
			++depth;
		else if (body[i] == '}') {
			--depth;
			if (depth == 0)
				return body.substr(brace, i - brace + 1);
		}
	}
	return {};
}

struct Instant {
	bool valid = false;
	time_t utc = 0;
};

bool InstantFromClock(int year, int month, int day, int hour, int minute, int second, int offsetSeconds, Instant& out) {
	if (year < 1970 || month < 1 || month > 12 || day < 1 || day > 31 || hour < 0 || hour > 23 || minute < 0 || minute > 59 || second < 0 || second > 60)
		return false;
	tm local{};
	local.tm_year = year - 1900;
	local.tm_mon = month - 1;
	local.tm_mday = day;
	local.tm_hour = hour;
	local.tm_min = minute;
	local.tm_sec = second;
	time_t stamped = _mkgmtime(&local);
	if (stamped == (time_t)-1)
		return false;
	out.valid = true;
	out.utc = stamped - (time_t)offsetSeconds;
	return true;
}

bool InstantFromText(const std::string& text, int offsetSeconds, Instant& out) {
	int year = 0;
	int month = 0;
	int day = 0;
	int hour = 0;
	int minute = 0;
	int second = 0;
	int count = sscanf_s(text.c_str(), "%d-%d-%dT%d:%d:%d", &year, &month, &day, &hour, &minute, &second);
	if (count < 3)
		count = sscanf_s(text.c_str(), "%d-%d-%d", &year, &month, &day);
	if (count < 3)
		return false;
	return InstantFromClock(year, month, day, hour, minute, second, offsetSeconds, out);
}

size_t SkipListSpace(const std::string& body, size_t i) {
	while (i < body.size() && (body[i] == ' ' || body[i] == '\n' || body[i] == '\r' || body[i] == '\t' || body[i] == ','))
		++i;
	return i;
}

std::vector<Instant> ParseInstantArray(const std::string& body, size_t keyPos, int offsetSeconds) {
	std::vector<Instant> values;
	if (keyPos == std::string::npos)
		return values;
	size_t bracket = body.find('[', keyPos);
	if (bracket == std::string::npos || bracket > keyPos + 48)
		return values;
	size_t i = bracket + 1;
	while (i < body.size() && body[i] != ']') {
		i = SkipListSpace(body, i);
		if (i >= body.size() || body[i] == ']')
			break;
		if (body.compare(i, 4, "null") == 0) {
			values.push_back({});
			i += 4;
			continue;
		}
		if (body[i] == '"') {
			size_t end = body.find('"', i + 1);
			if (end == std::string::npos)
				break;
			Instant instant;
			InstantFromText(body.substr(i + 1, end - i - 1), offsetSeconds, instant);
			values.push_back(instant);
			i = end + 1;
			continue;
		}
		char* end = nullptr;
		double number = strtod(body.c_str() + i, &end);
		if (end == body.c_str() + i)
			break;
		Instant instant;
		instant.valid = true;
		instant.utc = (time_t)number;
		values.push_back(instant);
		i = (size_t)(end - body.c_str());
	}
	return values;
}

Instant ParseInstantField(const std::string& body, const char* key, int offsetSeconds) {
	Instant instant;
	size_t keyPos = FindKey(body, key, 0);
	if (keyPos == std::string::npos)
		return instant;
	size_t colon = body.find(':', keyPos);
	if (colon == std::string::npos || colon > keyPos + 48)
		return instant;
	size_t i = SkipListSpace(body, colon + 1);
	if (i >= body.size())
		return instant;
	if (body[i] == '"') {
		size_t end = body.find('"', i + 1);
		if (end == std::string::npos)
			return instant;
		InstantFromText(body.substr(i + 1, end - i - 1), offsetSeconds, instant);
		return instant;
	}
	char* end = nullptr;
	double number = strtod(body.c_str() + i, &end);
	if (end == body.c_str() + i)
		return instant;
	instant.valid = true;
	instant.utc = (time_t)number;
	return instant;
}

NumberValue ParseNumberField(const std::string& body, const char* key) {
	NumberValue value;
	size_t keyPos = FindKey(body, key, 0);
	if (keyPos == std::string::npos)
		return value;
	size_t colon = body.find(':', keyPos);
	if (colon == std::string::npos || colon > keyPos + 64)
		return value;
	size_t i = SkipListSpace(body, colon + 1);
	if (i < body.size() && body.compare(i, 4, "null") == 0)
		return value;
	char* end = nullptr;
	double number = strtod(body.c_str() + i, &end);
	if (end == body.c_str() + i)
		return value;
	value.valid = true;
	value.value = number;
	return value;
}

NumberValue FirstNumber(const std::string& body, const char* a, const char* b) {
	NumberValue value = ParseNumberField(body, a);
	if (!value.valid && b)
		value = ParseNumberField(body, b);
	return value;
}

void StoreHour(DayForecast& day, int hour, double temperature, int code) {
	if (hour < 0 || hour > 23)
		return;
	HourPoint& point = day.hours[hour];
	point.valid = true;
	point.temperature = FormatTemperature(temperature);
	point.code = code;
}

ForecastCache ParseForecast(const std::string& body, int offsetSeconds, time_t now = time(nullptr), bool automaticTimezone = false) {
	ForecastCache cache;
	if (automaticTimezone) {
		JsonApartment apartment;
		auto root = ServiceJson(body);
		cache.timezone = JsonText(root, L"timezone");
		double seconds = root.GetNamedNumber(L"utc_offset_seconds", 1e9);
		if ((cache.timezone != L"GMT" && cache.timezone != L"UTC" && cache.timezone.find(L'/') == std::wstring::npos)
			|| cache.timezone.size() > 128 || cache.timezone.find_first_not_of(L"ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789/_-+") != std::wstring::npos
			|| !std::isfinite(seconds) || seconds < -43200 || seconds > 50400 || floor(seconds) != seconds)
			throw std::runtime_error("invalid automatic timezone");
		cache.timezoneValid = true;
		cache.utcOffsetSeconds = offsetSeconds = static_cast<int>(seconds);
	}
	std::string hourly = SliceObject(body, "hourly");
	std::string daily = SliceObject(body, "daily");
	std::string current = SliceObject(body, "current");
	if (current.empty())
		current = SliceObject(body, "current_weather");

	auto times = ParseInstantArray(hourly, FindKey(hourly, "time", 0), offsetSeconds);
	auto temps = ParseNumberArray(hourly, FindKey(hourly, "temperature_2m", 0));
	size_t codeKey = FindKey(hourly, "weathercode", 0);
	if (codeKey == std::string::npos)
		codeKey = FindKey(hourly, "weather_code", 0);
	auto codes = ParseNumberArray(hourly, codeKey);
	const char* extraKeys[] = { "uv_index", "wind_direction_10m", "wind_speed_10m", "relative_humidity_2m", "apparent_temperature", "precipitation_probability" };
	std::vector<NumberValue> extras[6];
	for (int field = 0; field < 6; ++field)
		extras[field] = ParseNumberArray(hourly, FindKey(hourly, extraKeys[field], 0));
	auto dayTimes = ParseInstantArray(daily, FindKey(daily, "time", 0), offsetSeconds);
	auto dayCodes = ParseNumberArray(daily, FindKey(daily, "weather_code", 0));
	auto dayMax = ParseNumberArray(daily, FindKey(daily, "temperature_2m_max", 0));
	auto dayMin = ParseNumberArray(daily, FindKey(daily, "temperature_2m_min", 0));
	auto sunrise = ParseInstantArray(daily, FindKey(daily, "sunrise", 0), offsetSeconds);
	auto sunset = ParseInstantArray(daily, FindKey(daily, "sunset", 0), offsetSeconds);

	int today = DateKeyFromUtc(now, offsetSeconds);
	int tomorrow = DateKeyFromUtc(now + 86400, offsetSeconds);

	auto slotFor = [&](int dateKey) -> DayForecast* {
		if (dateKey == today)
			return &cache.days[0];
		if (dateKey == tomorrow)
			return &cache.days[1];
		if (dateKey == DateKeyFromUtc(now + 2 * 86400, offsetSeconds))
			return &cache.days[2];
		return nullptr;
	};

	// Match local calendar dates; missing arrays never borrow another day or zero.
	size_t dailyCount = dayTimes.size();
	for (size_t i = 0; i < dailyCount; ++i) {
		if (!dayTimes[i].valid)
			continue;
		int dateKey = DateKeyFromUtc(dayTimes[i].utc, offsetSeconds);
		DayForecast* day = slotFor(dateKey);
		if (!day)
			continue;
		EnsureDay(*day, dateKey);
		if (i < dayCodes.size() && dayCodes[i].valid && dayCodes[i].value >= 0 && dayCodes[i].value <= 99 && floor(dayCodes[i].value) == dayCodes[i].value)
			day->code = static_cast<int>(dayCodes[i].value);
		if (i < dayMax.size() && dayMax[i].valid)
			day->maxTemperature = FormatTemperature(dayMax[i].value);
		if (i < dayMin.size() && dayMin[i].valid)
			day->minTemperature = FormatTemperature(dayMin[i].value);
		if (i < sunrise.size() && sunrise[i].valid) {
			int hour = HourOf(sunrise[i].utc, offsetSeconds);
			if (hour >= 0)
				day->sunriseHour = hour;
		}
		if (i < sunset.size() && sunset[i].valid) {
			int hour = HourOf(sunset[i].utc, offsetSeconds);
			if (hour >= 0)
				day->sunsetHour = hour;
		}
	}

	size_t hourlyCount = times.size();
	if (temps.size() < hourlyCount)
		hourlyCount = temps.size();
	for (size_t index = 0; index < hourlyCount; ++index) {
		if (!times[index].valid || !temps[index].valid)
			continue;
		tm local{};
		if (!BreakLocalTime(times[index].utc, offsetSeconds, local))
			continue;
		int dateKey = (local.tm_year + 1900) * 1000 + local.tm_yday;
		int hour = local.tm_hour;
		if (hour < 0 || hour > 23)
			continue;
		DayForecast* day = slotFor(dateKey);
		if (!day)
			continue;
		EnsureDay(*day, dateKey);
		int code = index < codes.size() && codes[index].valid ? (int)codes[index].value : -1;
		StoreHour(*day, hour, temps[index].value, NightCode(code, hour, day->sunriseHour, day->sunsetHour));
		HourPoint& point = day->hours[hour];
		std::wstring* values[] = { &point.uvIndex, &point.windDirection, &point.windSpeed, &point.humidity, &point.apparentTemperature, &point.precipitationProbability };
		for (int field = 0; field < 6; ++field) {
			if (index >= extras[field].size() || !extras[field][index].valid) continue;
			double value = extras[field][index].value;
			if (!std::isfinite(value) || (field != 4 && value < 0)
				|| (field == 1 && value > 360) || ((field == 3 || field == 5) && value > 100)) continue;
			wchar_t formatted[384]{};
			swprintf_s(formatted, field == 1 || field == 3 || field == 5 ? L"%.0f" : L"%.1f", value);
			*values[field] = formatted;
		}
	}

	NumberValue currentTemp = FirstNumber(current, "temperature_2m", "temperature");
	NumberValue currentCode = FirstNumber(current, "weather_code", "weathercode");
	if (currentTemp.valid) {
		Instant currentTime = ParseInstantField(current, "time", offsetSeconds);
		time_t stamp = currentTime.valid ? currentTime.utc : time(nullptr);
		int dateKey = DateKeyFromUtc(stamp, offsetSeconds);
		DayForecast* day = slotFor(dateKey);
		if (day) {
			EnsureDay(*day, dateKey);
			tm local{};
			if (BreakLocalTime(stamp, offsetSeconds, local) && local.tm_hour >= 0 && local.tm_hour <= 23 && !day->hours[local.tm_hour].valid) {
				int code = currentCode.valid ? (int)currentCode.value : -1;
				code = NightCode(code, local.tm_hour, day->sunriseHour, day->sunsetHour);
				StoreHour(*day, local.tm_hour, currentTemp.value, code);
			}
		}
	}

	cache.hasData = false;
	for (const DayForecast& day : cache.days) {
		if (day.code >= 0 || !day.maxTemperature.empty() || !day.minTemperature.empty())
			cache.hasData = true;
		for (const HourPoint& hour : day.hours) {
			if (hour.valid)
				cache.hasData = true;
		}
	}
	if (!cache.hasData)
		throw std::runtime_error("forecast did not include a temperature");
	return cache;
}

struct HttpHandle {
	HINTERNET handle = nullptr;
	HttpHandle() = default;
	HttpHandle(const HttpHandle&) = delete;
	HttpHandle& operator=(const HttpHandle&) = delete;
	~HttpHandle() {
		if (handle)
			WinHttpCloseHandle(handle);
	}
};

std::string HttpGet(const std::wstring& url) {
	URL_COMPONENTS parts{};
	parts.dwStructSize = sizeof(parts);
	wchar_t host[256]{};
	wchar_t path[8192]{};
	wchar_t extra[8192]{};
	parts.lpszHostName = host;
	parts.dwHostNameLength = 256;
	parts.lpszUrlPath = path;
	parts.dwUrlPathLength = 8192;
	parts.lpszExtraInfo = extra;
	parts.dwExtraInfoLength = 8192;
	if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts))
		throw std::runtime_error("invalid weather url");

	HttpHandle session;
	session.handle = WinHttpOpen(L"WeatherBar", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
	if (!session.handle)
		throw std::runtime_error("winhttp open failed");
	WinHttpSetTimeouts(session.handle, 5000, 5000, 5000, 5000);

	HttpHandle connection;
	connection.handle = WinHttpConnect(session.handle, host, parts.nPort, 0);
	if (!connection.handle)
		throw std::runtime_error("winhttp connect failed");

	std::wstring object = std::wstring(path, parts.dwUrlPathLength) + std::wstring(extra, parts.dwExtraInfoLength);
	DWORD flags = parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
	HttpHandle request;
	request.handle = WinHttpOpenRequest(connection.handle, L"GET", object.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
	if (!request.handle)
		throw std::runtime_error("winhttp request failed");

	if (!WinHttpAddRequestHeaders(request.handle, L"Accept-Encoding: identity\r\n", (DWORD)-1, WINHTTP_ADDREQ_FLAG_ADD | WINHTTP_ADDREQ_FLAG_REPLACE))
		throw std::runtime_error("winhttp header failed");
	if (!WinHttpSendRequest(request.handle, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
		!WinHttpReceiveResponse(request.handle, nullptr))
		throw std::runtime_error("weather request failed or timed out");

	std::string body;
	body.reserve(64 * 1024);
	char chunk[8192];
	ULONGLONG deadline = GetTickCount64() + 20000;
	for (;;) {
		if (GetTickCount64() > deadline) throw std::runtime_error("request deadline");
		DWORD read = 0;
		if (!WinHttpReadData(request.handle, chunk, sizeof(chunk), &read)) {
			char message[64];
			sprintf_s(message, "weather read failed (%lu)", GetLastError());
			throw std::runtime_error(message);
		}
		if (read == 0)
			break;
		if (body.size() + read > 2 * 1024 * 1024)
			throw std::runtime_error("weather response too large");
		body.append(chunk, read);
	}

	DWORD status = 0;
	DWORD statusSize = sizeof(status);
	WinHttpQueryHeaders(request.handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX);
	if (status != 200) {
		char message[64];
		sprintf_s(message, "weather server returned %lu", status);
		throw WeatherHttpError(message);
	}
	return body;
}

}

std::string WeatherHttpGet(const std::wstring& url) { return HttpGet(url); }

std::wstring FormatUtcOffset(int seconds) {
	wchar_t text[24]{};
	swprintf_s(text, L"UTC%c%02d:%02d", seconds < 0 ? L'-' : L'+', abs(seconds) / 3600, abs(seconds) % 3600 / 60);
	return text;
}

ForecastCache Weather::Fetch() const {
	return ParseLocationForecast(HttpGet(base));
}

bool BreakLocalTime(time_t utc, int offsetSeconds, tm& local) {
	long long shifted = (long long)utc + (long long)offsetSeconds;
	time_t localUtc = (time_t)shifted;
	return gmtime_s(&local, &localUtc) == 0;
}

int DateKeyFromUtc(time_t utc, int offsetSeconds) {
	tm local{};
	if (!BreakLocalTime(utc, offsetSeconds, local))
		return -1;
	return (local.tm_year + 1900) * 1000 + local.tm_yday;
}

time_t NextHourUtc(time_t now, int offsetSeconds) {
	long long offset = (long long)offsetSeconds;
	long long localNow = (long long)now + offset;
	long long nextLocal = (FloorDiv(localNow, 3600LL) + 1) * 3600LL;
	return (time_t)(nextLocal - offset);
}

time_t NextScheduleUtc(time_t now, int offsetSeconds, const int* hours, int hourCount, int delaySeconds) {
	if (!hours || hourCount <= 0)
		return now + 6 * 3600;
	long long offset = (long long)offsetSeconds;
	long long localNow = (long long)now + offset;
	long long dayIndex = FloorDiv(localNow, 86400LL);
	long long best = LLONG_MAX;
	for (int day = 0; day < 4; ++day) {
		for (int i = 0; i < hourCount; ++i) {
			long long localSlot = (dayIndex + day) * 86400LL + (long long)hours[i] * 3600LL + delaySeconds;
			if (localSlot <= localNow)
				continue;
			long long utc = localSlot - offset;
			if (utc < best)
				best = utc;
		}
	}
	if (best == LLONG_MAX)
		return now + 3600;
	return (time_t)best;
}

namespace {
std::wstring DecodeParameter(const std::wstring& value) {
	std::wstring decoded;
	auto hex = [](wchar_t c) { return c >= L'0' && c <= L'9' ? c-L'0' : c >= L'A' && c <= L'F' ? c-L'A'+10 : c >= L'a' && c <= L'f' ? c-L'a'+10 : -1; };
	for (size_t i = 0; i < value.size(); ++i) {
		if (value[i] == L'%' && i+2 < value.size() && hex(value[i+1]) >= 0 && hex(value[i+2]) >= 0) { decoded += static_cast<wchar_t>(hex(value[i+1])*16+hex(value[i+2])); i += 2; }
		else decoded += value[i] == L'+' ? L' ' : value[i];
	}
	return decoded;
}
std::wstring WithForecastTimezone(const std::wstring& input) {
	// Normalize only timezone in the actual request. Last nonempty duplicate wins; stored URL is untouched.
	size_t fragment = input.find(L'#'); std::wstring url = input.substr(0, fragment);
	size_t question = url.find(L'?');
	std::wstring head = url.substr(0, question), query = question == std::wstring::npos ? L"" : url.substr(question+1);
	std::vector<std::wstring> parameters; std::wstring timezone;
	for (size_t begin = 0; begin <= query.size();) {
		size_t end = query.find(L'&', begin); if (end == std::wstring::npos) end = query.size();
		std::wstring pair = query.substr(begin,end-begin); size_t equal = pair.find(L'=');
		if (DecodeParameter(pair.substr(0,equal)) == L"timezone") {
			std::wstring value = equal == std::wstring::npos ? L"" : pair.substr(equal+1);
			if (DecodeParameter(value).find_first_not_of(L" \t\r\n") != std::wstring::npos) timezone = value;
		} else if (!pair.empty()) parameters.push_back(pair);
		if (end == query.size()) break;
		begin = end+1;
	}
	std::wstring result = head + L"?";
	for (const auto& pair : parameters) result += pair + L"&";
	return result + L"timezone=" + (timezone.empty() ? L"auto" : timezone);
}
void AddForecastFields(std::wstring& url, const wchar_t* key, const std::vector<std::wstring>& required) {
 std::wstring parameter = std::wstring(key) + L"=";
 size_t begin = url.find(L"?" + parameter);
 if (begin == std::wstring::npos) begin = url.find(L"&" + parameter);
 if (begin == std::wstring::npos) {
  url += (url.find(L'?') == std::wstring::npos ? L"?" : L"&") + parameter;
  begin = url.size();
 } else begin += parameter.size() + 1;
 size_t end = url.find(L'&', begin);
 if (end == std::wstring::npos) end = url.size();
 std::wstring fields = url.substr(begin, end - begin);
 for (const auto& field : required) {
  std::wstring tokens = L"," + fields + L",";
  if (tokens.find(L"," + field + L",") == std::wstring::npos) {
   if (!fields.empty()) fields += L",";
   fields += field;
  }
 }
 url.replace(begin, end - begin, fields);
}
}
std::wstring PrepareForecastUrl(std::wstring base) {
 base = WithForecastTimezone(base);
 // One field list serves generated and advanced URLs; unrelated query parameters stay intact.
 AddForecastFields(base, L"hourly", {L"temperature_2m", L"weather_code", L"uv_index", L"wind_direction_10m", L"wind_speed_10m", L"relative_humidity_2m", L"apparent_temperature", L"precipitation_probability"});
 AddForecastFields(base, L"daily", {L"temperature_2m_max", L"temperature_2m_min", L"sunrise", L"sunset", L"weather_code"});
 size_t unit=base.find(L"&wind_speed_unit=");
 if (unit==std::wstring::npos) base+=L"&wind_speed_unit=ms";
 else {size_t end=base.find(L'&',unit+1); base.replace(unit,(end==std::wstring::npos ? base.size() : end)-unit,L"&wind_speed_unit=ms");}
 return WithForecastTimezone(base);
}

void Weather::SetURL(std::wstring url) { base = url.empty() ? L"" : PrepareForecastUrl(std::move(url)); }
ForecastCache ParseLocationForecast(const std::string& body) { return ParseForecast(body, 0, time(nullptr), true); }

bool Weather::HasDay(int dateKey) const {
	return FindDay(dateKey) != nullptr;
}

const DayForecast* Weather::FindDay(int dateKey) const {
	if (dateKey < 0)
		return nullptr;
	for (const DayForecast& day : cache.days) {
		if (day.dateKey == dateKey)
			return &day;
	}
	return nullptr;
}

void Weather::BeginFetch(HWND hwnd, void* owner, unsigned long long generation) {
	std::wstring url = base;
	auto result = std::make_unique<FetchResult>();
	result->owner = owner;
	result->generation = generation;
	// 后台线程结束前 DLL 不能卸载；线程创建失败时，捕获对象析构也会归还引用。
	auto moduleReference = std::make_unique<ModuleReference>();
	std::thread([moduleReference = std::move(moduleReference), hwnd, url = std::move(url), result = std::move(result)]() mutable {
		try {
			result->cache = ParseLocationForecast(HttpGet(url));
			result->ok = result->cache.hasData;
			if (!result->ok)
				throw std::runtime_error("forecast did not include today or tomorrow");
		}
		catch (const std::exception& error) {
			result->ok = false;
			OutputLog(error);
		}
		catch (...) {
			result->ok = false;
		}
		// 投递成功后由窗口处理函数接管；窗口关闭导致投递失败时自动释放。
		if (PostMessage(hwnd, WM_WEATHER_READY, 0, reinterpret_cast<LPARAM>(result.get())))
			result.release();
	}).detach();
}
