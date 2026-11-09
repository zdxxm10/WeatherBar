#include "TestSupport.h"
#include "../src/Location.cpp"
#include "../src/DeskBand.h"
#include <functional>

extern std::string ReadConfigBytes();
extern void WriteConfigText(const std::string& text);
void TestLocationFeatures() {
	const wchar_t* expectedCodes[] = {L"",L"CN",L"HK",L"MO",L"TW",L"JP",L"KR",L"SG",L"MY",L"TH",L"ID",L"PH",L"VN",L"IN",L"GB",L"FR",L"DE",L"IT",L"ES",L"RU",L"US",L"CA",L"MX",L"BR",L"AU",L"NZ",L"ZA",L"AE"};
	size_t count=0; auto countryItems=LocationCountries(count);
	Require(count==std::size(expectedCodes),"exact compact country list");
	for(size_t i=0;i<count;++i) Require(std::wstring(countryItems[i].code)==expectedCodes[i],"country order is fixed across languages");
	for (const auto& code : {L"TW",L"HK",L"MO"}) {
		for(Language language : {Language::Chinese,Language::English,Language::Japanese}) {
			auto body=std::string("{\"results\":[{\"name\":\"Place\",\"country\":\"raw country\",\"country_code\":\"")+winrt::to_string(code)+"\",\"timezone\":\"Asia/Shanghai\",\"latitude\":0,\"longitude\":0}]}";
			auto place=ParseLocations(body,language).front();
			auto name=LocationCountryName(code,language);
			Require(!name.empty() && place.label.find(name)!=std::wstring::npos && place.label.find(L"raw country")==std::wstring::npos,"structured country names apply to candidate labels");
			Require(LocationSearchUrl(L"Place",code,language).find(L"countryCode="+std::wstring(code))!=std::wstring::npos,"display names never change request country codes");
			auto config=DefaultBandConfig();config.locationMode=LocationMode::Search;config.locationName=place.name;config.locationCountryCode=place.countryCode;config.locationCountry=place.country;config.locationAdmin=L"Admin";config.searchCountry=code;config.url=BuildForecastUrl(L"0",L"0",L"auto");
			Require(SaveBandConfig(config),"save structured region metadata");auto loaded=LoadBandConfig();
			Require(loaded.locationCountryCode==code && loaded.searchCountry==code && loaded.locationAdmin==L"Admin","country codes and admin metadata survive reload");
		}
	}
	Require(LocationCountryName(L"TW",Language::Chinese)==L"中国台湾" && LocationCountryName(L"HK",Language::English)==L"Hong Kong, China" && LocationCountryName(L"MO",Language::Japanese)==L"中国マカオ","specified localized region names");
	for (Language language : {Language::Chinese, Language::Japanese, Language::English}) {
		auto fresh = DefaultBandConfig(language);
		Require(fresh.locationMode == LocationMode::None && fresh.url.empty() && fresh.latitude.empty() && fresh.longitude.empty() && fresh.searchCountry.empty(), "fresh configuration has no location in every language");
		Require(SaveBandConfig(fresh), "save unselected appearance configuration");
		auto loaded = LoadBandConfig();
		Require(loaded.locationMode == LocationMode::None && loaded.url.empty() && loaded.searchCountry.empty(), "empty location survives restart");
		Require(ReadConfigBytes().find("<ForecastUrl") == std::string::npos, "no hidden URL serialized");
	}

 for(const auto& country : {L"Taiwan",L"Hong Kong",L"Macao"}) {
  WriteConfigText("<WeatherBar version=\"5\"><Location><Mode value=\"1\"/><Name value=\"City · "+winrt::to_string(country)+" · Admin (0, 0)\"/><Latitude value=\"0\"/><Longitude value=\"0\"/></Location></WeatherBar>");
  auto legacy=LoadBandConfig();
  Require(legacy.locationName==L"City" && legacy.locationAdmin==L"Admin" && !legacy.locationCountryCode.empty(),"legacy country component migrates without altering city or admin");
 }
 WriteConfigText("<WeatherBar version=\"5\"><Location><Mode value=\"1\"/><Name value=\"Hong Kong Road · France (0, 0)\"/><Latitude value=\"0\"/><Longitude value=\"0\"/></Location></WeatherBar>");
 Require(LoadBandConfig().locationName==L"Hong Kong Road · France (0, 0)","country migration never replaces arbitrary name substrings");
	std::wstring oldDefault = BuildForecastUrl(L"39.9286",L"116.4103",L"Asia/Shanghai");
	WriteConfigText("<WeatherBar version=\"2\"><Data><ForecastUrl><![CDATA[" + winrt::to_string(oldDefault) + "]]></ForecastUrl></Data></WeatherBar>");
	auto oldLocation = LoadBandConfig(); Require(HasConfiguredLocation(oldLocation) && oldLocation.url == oldDefault, "old valid default URL remains a configured location");
	std::wstring prefix = L"https://api.open-meteo.com/v1/forecast?latitude=1&longitude=2&models=best_match&";
	for (const auto& query : {L"timezone=", L"timezone", L"timezone=%20+", L"other_timezone=UTC"}) {
		auto completed = PrepareForecastUrl(prefix + query);
		Require(completed.find(L"&timezone=auto") != std::wstring::npos && completed.find(L"models=best_match") != std::wstring::npos, "missing or empty timezone is automatic without touching other parameters");
		Require(PrepareForecastUrl(completed) == completed, "timezone completion is idempotent");
	}
	for (const auto& query : {L"timezone=Asia%2FTokyo", L"time%7Aone=Asia%2FTokyo", L"timezone=&timezone=Asia%2FTokyo&timezone="}) {
		auto completed = PrepareForecastUrl(prefix + query);
		Require(completed.find(L"&timezone=Asia%2FTokyo") != std::wstring::npos && completed.find(L"timezone=auto") == std::wstring::npos, "explicit encoded timezone is respected");
	}
	Require(PrepareForecastUrl(prefix + L"timezone=Asia%2FTokyo&timezone=Europe%2FLondon").ends_with(L"timezone=Europe%2FLondon"), "last nonempty duplicate timezone wins");

	for (const auto& value:{L"-90",L"90",L"-0.123456789",L"+45.25",L" 12.25 "}) Require(ValidCoordinate(value,90),"valid finite coordinate");
	for (const auto& value:{L"",L" ",L"NaN",L"inf",L"1e999",L"90.1",L"-90.1",L"1x",L"1,2"}) Require(!ValidCoordinate(value,90),"invalid latitude rejected");
	Require(ValidCoordinate(L"-180",180) && ValidCoordinate(L"180",180) && !ValidCoordinate(L"180.01",180),"longitude boundaries");
	auto url=LocationSearchUrl(L"杭州",L"CN",Language::Chinese);
	Require(url.find(L"%E6%9D%AD%E5%B7%9E")!=std::wstring::npos && url.find(L"countryCode=CN")!=std::wstring::npos && url.find(L"language=zh")!=std::wstring::npos,"UTF-8 query and country filter");
	Require(LocationSearchUrl(L"東京",L"",Language::Japanese).find(L"countryCode")==std::wstring::npos,"unlimited country omits parameter");
	Require(LocationSearchUrl(L"Paris",L"FR",Language::English).find(L"language=en")!=std::wstring::npos,"English search");
	auto candidates=ParseLocations(R"({"results":[{"name":"\u676d\u5dde","country":"中国","admin1":"浙江","admin2":"杭州","latitude":30.2741,"longitude":120.1551,"timezone":"Asia/Shanghai"},{"name":"Paris","country":"France","latitude":48.85,"longitude":2.35,"timezone":"Europe/Paris"}]})");
	Require(candidates.size()==2 && candidates[0].name==L"杭州" && candidates[0].label.find(L"杭州",1)==std::wstring::npos,"multiple candidates, escaped Unicode and duplicate regions");
	Require(candidates[1].label.find(L"France")!=std::wstring::npos,"missing admin fields accepted");
	Require(ParseLocations("{}").empty() && ParseLocations("{\"results\":[]}").empty(),"empty search response");
	for (const auto& response:{"bad json","{\"error\":true}","{\"results\":[{}]}","{\"results\":null}"}) { bool failed=false; try { ParseLocations(response); } catch (...) {failed=true;} Require(failed,"bad responses fail parsing instead of appearing empty"); }
	auto forecastUrl=PrepareForecastUrl(BuildForecastUrl(L"-33.123456789",L"151.2",L"auto"));
	for (const auto& field:{L"temperature_2m",L"weather_code",L"temperature_2m_max",L"temperature_2m_min",L"sunrise",L"sunset",L"uv_index",L"relative_humidity_2m",L"timezone=auto",L"timeformat=unixtime"}) Require(forecastUrl.find(field)!=std::wstring::npos,"generated URL retains fields");
	auto custom=PrepareForecastUrl(L"https://api.open-meteo.com/v1/forecast?latitude=1&longitude=2&forecast_days=1&models=best_match&hourly=temperature_2m&timezone=auto");
	Require(custom.find(L"forecast_days=1&models=best_match")!=std::wstring::npos && custom.find(L"daily=")!=std::wstring::npos,"custom parameters survive field completion");
	Require(PrepareForecastUrl(custom)==custom,"field completion is idempotent");
	Require(forecastUrl.find(L"-33.123456789")!=std::wstring::npos,"coordinate precision and sign retained");
	time_t now=time(nullptr);
	auto autoBody=[&](int seconds){return std::string("{\"timezone\":\"Asia/Kathmandu\",\"utc_offset_seconds\":")+std::to_string(seconds)+",\"current\":{\"time\":"+std::to_string(now)+",\"temperature_2m\":12}}";};
	for (int seconds:{19800,20700,-12600}) {
		auto cache=ParseLocationForecast(autoBody(seconds));
		tm local{}; Require(BreakLocalTime(now,seconds,local),"seconds local time");
		Require(cache.timezoneValid && cache.utcOffsetSeconds==seconds && cache.days[0].dateKey==DateKeyFromUtc(now,seconds) && cache.days[0].hours[local.tm_hour].valid,"automatic non-integer-hour date and current hour");
		Require(NextHourUtc(now,seconds)>now && NextHourUtc(now,seconds)<=now+3600,"fractional offset next hour");
	}
	for (LocationMode mode : {LocationMode::Custom, LocationMode::Search, LocationMode::Coordinates}) {
		auto selected = DefaultBandConfig(Language::English); selected.locationMode = mode;
		selected.latitude=L"27.7172";selected.longitude=L"85.324";selected.url=BuildForecastUrl(selected.latitude,selected.longitude,L"auto");
		auto response = ParseLocationForecast(autoBody(20700)); CDeskBand preview;
		preview.ApplyPreviewConfig(selected,&response);
		Require(preview.UtcOffsetSeconds()==20700 && preview.CurrentForecast().timezone==L"Asia/Kathmandu", "every source uses response seconds and timezone");
	}
	Require(FormatUtcOffset(20700)==L"UTC+05:45" && FormatUtcOffset(-12600)==L"UTC-03:30","offset formatting");
	bool failed=false; try {ParseLocationForecast("{\"current\":{\"temperature_2m\":12}}");} catch(...) {failed=true;} Require(failed,"missing timezone cannot imply a default");
	WriteConfigText(R"(<WeatherBar version="3"><Data><ForecastUrl value="https://api.open-meteo.com/v1/forecast?latitude=1&amp;longitude=2&amp;timezone=Europe%2FBerlin&amp;forecast_days=2"/><TimezoneOffset value="-3"/></Data></WeatherBar>)");
	auto legacy=LoadBandConfig(); Require(legacy.locationMode==LocationMode::Custom && legacy.url.find(L"forecast_days=2")!=std::wstring::npos,"legacy custom URL and manual timezone stay effective");
	BandConfig config=DefaultBandConfig(); config.locationMode=LocationMode::Search; config.locationName=L"杭州 · 中国"; config.latitude=L"30.27"; config.longitude=L"120.15"; config.locationTimezone=L"Asia/Shanghai"; config.searchCountry=L"FR"; config.url=BuildForecastUrl(config.latitude,config.longitude,config.locationTimezone);
	Require(SaveBandConfig(config),"save location metadata");
	auto loaded=LoadBandConfig(); Require(loaded.locationMode==config.locationMode && loaded.locationName==config.locationName && loaded.searchCountry==L"FR" && loaded.locationTimezone==config.locationTimezone,"location config roundtrip");
	CDeskBand band; auto original=band.CurrentConfig(); auto originalCache=band.CurrentForecast(); auto cache=ParseLocationForecast(autoBody(20700));
	band.BeginSettingsPreview(); band.ApplyPreviewConfig(config,&cache); Require(band.UtcOffsetSeconds()==20700,"preview uses validated offset");
	band.CancelSettingsPreview(); Require(band.CurrentConfig().locationMode==original.locationMode && band.CurrentConfig().url==original.url && band.CurrentForecast().timezoneValid==originalCache.timezoneValid,"cancel restores location timezone and cache");

}
