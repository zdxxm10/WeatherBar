#include "pch.h"
#include "Location.h"
#include "ServiceJson.h"
#include "ModuleReference.h"
#include <thread>
#include <cmath>
#include <locale>
#include <sstream>
#include <iomanip>
#include <windowsx.h>

bool ValidCoordinate(const std::wstring& text, double limit) {
	if (text.empty() || text.size() > 64) return false;
	std::wistringstream input(text); input.imbue(std::locale::classic());
	double value = 0;
	input >> value;
	if (input.fail() || !std::isfinite(value) || value < -limit || value > limit) return false;
	input >> std::ws;
	return input.eof();
}

std::wstring LocationSearchUrl(const std::wstring& query, const std::wstring& country, Language language) {
	std::wstring url = L"https://geocoding-api.open-meteo.com/v1/search?count=10&format=json&name=" + EncodeQuery(query);
	url += L"&language=" + std::wstring(Text(language, L"zh", L"en", L"ja"));
	if (!country.empty()) url += L"&countryCode=" + EncodeQuery(country);
	return url;
}

std::wstring LocationLabel(const LocationCandidate& candidate, Language language) {
	// Country codes control names; never replace substrings in a place/address.
	std::wstring label = candidate.name;
	std::wstring country = LocationCountryName(candidate.countryCode, language, candidate.country);
	if (!country.empty() && country != candidate.name) label += L" · " + country;
	if (!candidate.admin.empty()) label += L" · " + candidate.admin;
	if (!candidate.latitude.empty() && !candidate.longitude.empty()) label += L" (" + candidate.latitude + L", " + candidate.longitude + L")";
	return label;
}

std::vector<LocationCandidate> ParseLocations(const std::string& body, Language language) {
	JsonApartment apartment;
	auto root = ServiceJson(body);
	std::vector<LocationCandidate> results;
	if (!root.HasKey(L"results")) return results;
	auto array = root.GetNamedArray(L"results");
	if (array.Size() > 10) throw std::runtime_error("too many locations");
	for (auto value : array) {
		auto item = value.GetObject();
		LocationCandidate candidate;
		candidate.name = JsonText(item, L"name"); candidate.country = JsonText(item, L"country");
		candidate.countryCode = JsonText(item, L"country_code");
		candidate.timezone = JsonText(item, L"timezone");
		auto number = [&](const wchar_t* key) {
			double v = item.GetNamedNumber(key, 1e9);
			std::wostringstream out; out.imbue(std::locale::classic()); out << std::setprecision(15) << v; return out.str();
		};
		candidate.latitude = number(L"latitude"); candidate.longitude = number(L"longitude");
		if (candidate.name.empty() || candidate.timezone.empty() || !ValidCoordinate(candidate.latitude, 90) || !ValidCoordinate(candidate.longitude, 180))
			throw std::runtime_error("invalid location result");
		std::vector<std::wstring> parts{candidate.name, candidate.country, LocationCountryName(candidate.countryCode, language, candidate.country)};
		for (auto key : {L"admin1", L"admin2", L"admin3", L"admin4"}) {
			std::wstring part = JsonText(item, key);
			if (!part.empty() && std::find(parts.begin(), parts.end(), part) == parts.end()) {
				parts.push_back(part);
				if (!candidate.admin.empty()) candidate.admin += L" · ";
				candidate.admin += part;
			}
		}
		candidate.label = LocationLabel(candidate, language);
		results.push_back(std::move(candidate));
	}
	return results;
}

namespace {
std::shared_ptr<LocationRequest> StartRequest(const std::wstring& url, bool weather, Language language = Language::System) {
	auto result = std::make_shared<LocationRequest>();
	auto module = std::make_unique<ModuleReference>();
	std::thread([result, module = std::move(module), url, weather, language] {
		std::string body;
		try { body = WeatherHttpGet(url); }
		catch (const WeatherHttpError&) { result->status = LocationStatus::ServiceError; result->done.store(true, std::memory_order_release); return; }
		catch (...) { result->status = LocationStatus::NetworkError; result->done.store(true, std::memory_order_release); return; }
		try {
			if (weather) {
				// Parse through the same production weather path, with timezone validation.
				result->forecast = ParseLocationForecast(body);
			} else result->candidates = ParseLocations(body, language);
			result->status = LocationStatus::Ready;
		} catch (...) { result->status = LocationStatus::ParseError; }
		result->done.store(true, std::memory_order_release);
	}).detach();
	return result;
}
}
std::shared_ptr<LocationRequest> StartLocationSearch(const std::wstring& query, const std::wstring& country, Language language) {
	return StartRequest(LocationSearchUrl(query, country, language), false, language);
}
std::shared_ptr<LocationRequest> StartLocationWeather(const BandConfig& config) {
	return StartRequest(PrepareForecastUrl(config.url), true);
}

static const LocationCountry countries[] = {
	{L"",L"不限",L"Any",L"指定なし"}, {L"CN",L"中国",L"China",L"中国"},
	{L"HK",L"中国香港",L"Hong Kong, China",L"中国香港"}, {L"MO",L"中国澳门",L"Macao, China",L"中国マカオ"}, {L"TW",L"中国台湾",L"Taiwan, China",L"中国台湾"},
	{L"JP",L"日本",L"Japan",L"日本"}, {L"KR",L"韩国",L"South Korea",L"韓国"}, {L"SG",L"新加坡",L"Singapore",L"シンガポール"},
	{L"MY",L"马来西亚",L"Malaysia",L"マレーシア"}, {L"TH",L"泰国",L"Thailand",L"タイ"}, {L"ID",L"印度尼西亚",L"Indonesia",L"インドネシア"},
	{L"PH",L"菲律宾",L"Philippines",L"フィリピン"}, {L"VN",L"越南",L"Vietnam",L"ベトナム"}, {L"IN",L"印度",L"India",L"インド"},
	{L"GB",L"英国",L"United Kingdom",L"イギリス"}, {L"FR",L"法国",L"France",L"フランス"}, {L"DE",L"德国",L"Germany",L"ドイツ"},
	{L"IT",L"意大利",L"Italy",L"イタリア"}, {L"ES",L"西班牙",L"Spain",L"スペイン"}, {L"RU",L"俄罗斯",L"Russia",L"ロシア"},
	{L"US",L"美国",L"United States",L"アメリカ"}, {L"CA",L"加拿大",L"Canada",L"カナダ"}, {L"MX",L"墨西哥",L"Mexico",L"メキシコ"}, {L"BR",L"巴西",L"Brazil",L"ブラジル"},
	{L"AU",L"澳大利亚",L"Australia",L"オーストラリア"}, {L"NZ",L"新西兰",L"New Zealand",L"ニュージーランド"},
	{L"ZA",L"南非",L"South Africa",L"南アフリカ"}, {L"AE",L"阿联酋",L"United Arab Emirates",L"アラブ首長国連邦"}
};

const LocationCountry* LocationCountries(size_t& count) { count = std::size(countries); return countries; }

std::wstring LocationCountryName(const std::wstring& code, Language language, const std::wstring& fallback) {
	for (const auto& country : countries) if (!code.empty() && code == country.code) return Text(language, country.zh, country.en, country.ja);
	return fallback;
}
