#pragma once
#include "ImportConfig.h"
#include "Weather.h"
#include <atomic>
#include <memory>

struct LocationCandidate {
	std::wstring name, country, timezone, latitude, longitude;
	std::wstring label;
	std::wstring countryCode, admin;
};
std::wstring LocationCountryName(const std::wstring& code, Language language, const std::wstring& fallback = L"");
std::wstring LocationLabel(const LocationCandidate& candidate, Language language);
bool ValidCoordinate(const std::wstring& text, double limit);
std::wstring LocationSearchUrl(const std::wstring& query, const std::wstring& country, Language language);
std::vector<LocationCandidate> ParseLocations(const std::string& body, Language language = Language::System);
enum class LocationStatus { Working, Ready, NetworkError, ParseError, ServiceError };
struct LocationRequest {
	std::atomic<bool> done{false};
	LocationStatus status = LocationStatus::Working;
	std::vector<LocationCandidate> candidates;
	ForecastCache forecast;
};
std::shared_ptr<LocationRequest> StartLocationSearch(const std::wstring& query, const std::wstring& country, Language language);
std::shared_ptr<LocationRequest> StartLocationWeather(const BandConfig& config);
struct LocationCountry { const wchar_t* code; const wchar_t* zh; const wchar_t* en; const wchar_t* ja; };
const LocationCountry* LocationCountries(size_t& count);
