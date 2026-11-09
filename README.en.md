# WeatherBar

Lightweight weather for the classic Windows taskbar.

[简体中文](README.md) | **English** | [日本語](README.ja.md)

**Current version: 1.1** · [Stable release](https://github.com/zdxxm10/WeatherBar/releases/tag/v1.1) · [Download WeatherBar.exe](https://github.com/zdxxm10/WeatherBar/releases/download/v1.1/WeatherBar.exe)

## Install

Download `WeatherBar.exe` from [Releases](https://github.com/zdxxm10/WeatherBar/releases/latest), put it in a writable dedicated folder and run it. The EXE contains the DLL and icons. Allow Windows to display the toolbar, or enable WeatherBar in the classic taskbar's Toolbars menu. Installation registers the component and startup entry for the current user.

The release targets x64 Windows 10 classic taskbars. The native Windows 11 taskbar does not support DeskBands. Other classic taskbar implementations and 32-bit installation are not verified.

Only the EXE is needed from the release page; it includes the DLL, icons and licenses. The release notes list its SHA-256 checksum, so no separate checksum or license TXT download is required. Source license links are below.

## Settings

Right-click the bar to open Basic settings, starting with Language and location search. Custom URLs are edited under Custom location settings. All sources use response-provided timezone and UTC offset; no manual offset is required.

Appearance and language changes preview immediately. Click a color button for swatches and RGB sliders/values; clicking elsewhere hides the palette. The URL field wraps at the available width and shows three lines. The default width is the smallest complete two-column layout that fits Chinese, English and Japanese, measured with the actual font and DPI. The shared breakpoint keeps language changes in two columns; the narrower single-column minimum is separate. Small work areas use scrolling. Scrolling moves only the content, leaving the action bar fixed. The page, candidate list and URL field show native scrollbars only when needed, with standard arrows retained.

**Save** writes the configuration; **Cancel** or closing restores the original settings and forecast cache. **Reset appearance** keeps location, timezone and language. Invalid unfinished data input does not interrupt appearance preview. Only explicit Search/Apply actions request location data.

Languages: Simplified Chinese, English and Japanese. System default maps all Chinese regions to Simplified Chinese, Japanese to Japanese, and others to English. Install/uninstall always follows the system independently of the setting.

Forecasts refresh at local 00/06/12/18 hours with a default 15-minute stagger (`StaggerSeconds`, 0–1800). The tooltip is enabled by default, selecting only tomorrow, day after tomorrow and last refresh time. Status and all other items default off. Refresh time and status are independent switches. Disabling the master keeps selections; no content means no popup. Existing choices remain; only missing fields use defaults. Missing values show `--`; up to two items share a row based on measured text width. Tooltip changes preview immediately and roll back on Cancel. Additional weather fields share the existing request and refresh schedule; wind speed uses m/s. Check updates contacts GitHub only when clicked; it never downloads or installs updates automatically. If the API fails, it checks the official latest-release redirect for this repository; failures report an HTTP or WinHTTP code.

## Update / uninstall

Uninstall while keeping settings, wait for Explorer to restart, replace the EXE in the same folder and run again. Existing custom SVG files are preserved; missing icons are added. Old config names migrate with the original retained. Back up XML and icons before the first upgrade from an older release; use the old EXE to uninstall that version first.

Default uninstall keeps configuration, backups, logs, icons and EXE; it removes the inactive DLL, registration and startup entry. Complete removal also deletes app settings, backups and icons, then uses built-in Windows PowerShell to delete the EXE after exit and the folder if empty. Unrelated files and junction targets are preserved. Organization policies blocking PowerShell may require manual EXE deletion. Explorer restarts and open folder windows close: save your work first.

## Build / test

Use Visual Studio C++ desktop tools v145 and Windows SDK. Plugin: C++20; launcher: C++17. No additional UI framework or old package dependencies are required.

```powershell
MSBuild WeatherBar.sln /m:1 /nr:false /p:Configuration=Release /p:Platform=x64 /v:minimal
MSBuild tests\RegressionTests.vcxproj /m:1 /nr:false /p:Configuration=Release /p:Platform=x64 /v:minimal
.\tests\x64\Release\RegressionTests.exe
MSBuild tests\LauncherTests.vcxproj /m:1 /nr:false /p:Configuration=Release /p:Platform=x64 /v:minimal
.\tests\x64\LauncherRelease\LauncherTests.exe
```

Output: `x64/Release/WeatherBar.exe`. Tests use isolated output folders, do not install or restart Explorer. For a settings preview, pass `--preview-settings en` to RegressionTests.exe. Win32 logic tests are supported; real taskbar, startup, network and cross-monitor DPI need separate validation. See [architecture](docs/architecture.md) and [verification](docs/verification.md).

## Credits / license

Based on [Nusudo-junior/WeatherDeskBand](https://github.com/Nusudo-junior/WeatherDeskBand), retaining its [MIT license](LICENSE). Weather: [Open-Meteo](https://open-meteo.com/). Icons: Bas Milius [Meteocons](https://github.com/basmilius/meteocons), [MIT](src/icons/METEOCONS-LICENSE.txt). [Report issues](https://github.com/zdxxm10/WeatherBar/issues).

## 1.1 Future weather tooltip

Tomorrow and Day after tomorrow have independent switches controlling each complete date entry, weather description and low / high temperatures. Both default on for new installations. Schema 3 marks the one-time migration of the old tooltip high/low switch: enabled becomes both days enabled; explicitly disabled becomes both disabled; absent fields use current defaults. New independent choices are preserved; migration creates no extra backup file; failed atomic saves preserve the original configuration. Taskbar today's extremes are unchanged.

Descriptions directly map Open-Meteo daily weather_code (the day's most severe phenomenon) to a single summary, without describing transitions. Drizzle and showers map to Rain, freezing drizzle to Freezing rain, snow grains and snow showers to Snow, and thunderstorms with hail to Thunderstorm; intensity is omitted. Unknown or absent codes show Unknown weather. Match today and the next two days by the configured location's local date. Each missing temperature shows `--`, never another date's value; short custom responses remain missing.

Each date prefers one row, always low / high, e.g. `Tomorrow: Partly cloudy 13℃ / 22℃`. Actual tooltip font measurement decides whether to split the date and content into two rows, without blank rows between dates. Existing requests, cache, failure status and refresh cadence are reused.

### Refresh time, status and country filter

Last refresh records the instant when valid weather was successfully accepted. Failures, stale replies, repainting and language changes never update it. Display uses the computer's current system local time without a timezone/UTC suffix; no record shows “Not refreshed yet”. A computer in Japan displays Japan's refresh time even for Beijing weather. Weather hours, today/tomorrow, sunrise/sunset and scheduling still use the weather location's timezone.

Data status appears only when selected. Priority: protected configuration, no location, updating, no weather, stale forecast, failed update with cache, healthy. Disabling status preserves existing operation-error feedback. Disabled rows leave no empty separators.

Country/region, Place and Search share one row; Place takes remaining width. Fixed order in every language: Any; China; Hong Kong, China; Macao, China; Taiwan, China; Japan; South Korea; Singapore; Malaysia; Thailand; Indonesia; Philippines; Vietnam; India; United Kingdom; France; Germany; Italy; Spain; Russia; United States; Canada; Mexico; Brazil; Australia; New Zealand; South Africa; United Arab Emirates. Unlisted countries remain searchable with Any, which sends no filter. Selections persist by code, never index. HK/MO/TW names change for display; request codes remain HK/MO/TW. New search selections store country code, name and administrative information separately for localized restoration. Legacy free text without a known code is kept rather than blindly replaced.

## Settings window layout

Native cards group the page. Basic settings spans the width; sufficient space places Display and Spacing on the left, and Hover tooltip followed by About & updates on the right. The two rows (Display/Tooltip and Spacing/About) align their top and bottom edges using the larger required height. Narrow windows use Basic, Display, Spacing, Hover tooltip, then About in one column. Actual text width, language and DPI determine the column breakpoint and control sizes. Main text is not shrunk. The country selector fits the longest option across all three languages, aligns with the language selector, and leaves remaining width to Place.

Default width is the measured minimum that fully fits both columns, card padding/gaps, window margins, scrollbar and native frame, while also fitting the basic search/status row. All three languages are measured with the actual font and DPI, and their maximum defines the shared default width and column breakpoint; single-column minimum width is separate. Only insufficient monitor work area allows a single-column default.

About keeps the version, update check, release page and result feedback. Data sources is a compact text link to the existing Open-Meteo/GeoNames/licence dialog. One native control owns search status. Only that control is fully repainted on boundary or text changes, clearing old text and drawing one centered line.

The title bar keeps Close and the border remains resizable. Reset appearance, Save and Cancel stay in a fixed bottom bar; only content above it scrolls. Small windows, high DPI and expanded custom location settings remain scrollable. The candidate list and action row are always present, preventing layout jumps during search. Advanced drafts, focus recovery, preview/save/cancel and all existing configuration behavior are retained.

The tooltip master checkbox sits at the right of its card heading with its full label. In two columns, Display/Tooltip and Spacing/About form two rows with matching top and bottom edges; each row uses the larger required height. Single-column cards keep their natural heights. Language and country dropdowns share the smallest measured width that fits all three languages, with one native arrow/scrollbar allowance. The country popup prefers all options, constrained to the content area and monitor work area; it scrolls or opens upward when needed. Moving/resizing the window or scrolling the page closes the popup; reopening recalculates its position without covering the fixed action bar.

## First run and location setup

Only users without an existing configuration file start with all six appearance levels at Large: current temperature, icon, high and low temperatures, and both gaps. Saved levels remain unchanged; absent fields in existing files retain former defaults. Reset appearance uses Large for all six within its existing scope; location, language and tooltip selections are retained.

First run has **no preset location**. Country is Any; city, custom URL and coordinate inputs are empty. Until a location is selected, temperatures remain placeholders, no weather fetch/schedule runs, and Refresh is disabled. Language changes and Reset appearance never choose a place. Appearance/language settings may be saved before choosing a location.

### Search, apply and save

1. Select a country or Any in Basic settings.
2. Enter a city/district and click Search; typing sends no request.
3. Check the candidate's country, administrative areas and coordinates, explicitly select it, then Apply location.
4. Wait for validated weather/timezone preview, then Save. Cancel/close restores the previous state; cancelling the first preview returns to no location and stops scheduling.

The compact result list, hint and Apply location button remain visible before searching. Extra results scroll inside the list; Apply stays disabled until a valid candidate is selected, and the first result is never selected automatically. Status is short, centered single-line text without scroll arrows. Search, coordinate and URL errors stay in their respective areas. This is not arbitrary address parsing; district coverage depends on the service. Try a city name or coordinates if empty. Huicheng District below is a documentation example, not a placeholder/default in the program.

### WGS84 (GPS) coordinates and custom URLs

Expand Custom location settings and choose coordinates or URL. Switching retains drafts; explicitly Apply to preview. Latitude is -90 to 90, longitude -180 to 180, in decimal degrees. Negatives and zero are valid; missing values are not zero. Convert degrees/minutes/seconds using degrees+minutes/60+seconds/3600, with negative signs for south/west.

A verified WGS84 workflow: open the [Open-Meteo geocoding page](https://open-meteo.com/en/docs/geocoding-api), enter city, country and language, and inspect its API URL/JSON. Check name, country and admin1…admin4, then copy latitude/longitude. The [Tokyo query example](https://geocoding-api.open-meteo.com/v1/search?name=Tokyo&countryCode=JP&count=10&language=en&format=json) can be inspected directly. The official API and [GeoNames](https://www.geonames.org/about.html) specify WGS84. Alternatively use GPS exports explicitly documented as WGS84 decimal degrees, following that device's instructions.

WGS84 is a global reference system; [Amap documents GCJ-02](https://developer.amap.com/faq/advisory/others/39838), and [Baidu describes BD-09](https://lbsyun.baidu.com/docs/jsapi?title=jsapi4/guide/concept/coord) as another transformation of GCJ-02. Do not interchange them without verification. WeatherBar provides no conversion library/API, map picker or reverse geocoding. Do not assume coordinates copied from every Google/Apple Maps page are WGS84; verify the specific tool/interface.

### Obtain a URL from the official weather page

Open [Open-Meteo forecast docs](https://open-meteo.com/en/docs). Under Location and Time enter verified coordinates and select timezone auto. Select every hourly/daily variable listed below, and in Settings select Celsius, m/s and Unix time. Copy API URL from API Response, paste it into Custom URL, Apply URL, then Save. Page layout/language can change; check the final parameter names.

Complete recommended parameters for icons, daily extremes, the next two days and all tooltip fields:

```text
hourly=temperature_2m,weather_code,uv_index,wind_direction_10m,wind_speed_10m,relative_humidity_2m,apparent_temperature,precipitation_probability&daily=temperature_2m_max,temperature_2m_min,weather_code,sunrise,sunset&temperature_unit=celsius&wind_speed_unit=ms&timezone=auto&timeformat=unixtime
```

[Complete Tokyo example URL](https://api.open-meteo.com/v1/forecast?latitude=35.6895&longitude=139.69171&hourly=temperature_2m,weather_code,uv_index,wind_direction_10m,wind_speed_10m,relative_humidity_2m,apparent_temperature,precipitation_probability&daily=temperature_2m_max,temperature_2m_min,weather_code,sunrise,sunset&temperature_unit=celsius&wind_speed_unit=ms&timezone=auto&timeformat=unixtime) is documentation only; it is never loaded as a default. latitude and longitude must not be swapped.

Every source validates response timezone/utc_offset_seconds before local-date parsing. Custom URLs keep an explicit timezone; missing/empty values become auto only in the request, with the last nonempty duplicate winning. Original URLs and unrelated options stay intact. Missing timezone is reported, failed requests preserve valid cache, and fractional-hour offsets work. A response may use one fixed offset across a DST transition; this is not full DST support.

### Configuration, updates and reinstalling

WeatherBar.xml is beside the program/DLL. The current filename takes precedence; only the same-folder legacy filename is a fallback. Saves are atomic in that folder. Schema 5 represents no location. Existing valid URLs (including old default Beijing URLs), locations and appearance are retained. Damaged/newer configurations are explicitly protected from writes, not silently treated as first run; no migration backup is generated.

Default uninstall keeps XML/icons and does not remove the directory. Reinstall the EXE in that same retained folder to restore settings. When moving the whole folder, include XML/icons; moving only the EXE elsewhere does not scan the disk for configuration. Deleted XML/folders, complete removal, a different Windows user or unreadable files cannot be promised automatic recovery. You may copy XML/icons yourself before changes. Payload extraction does not overwrite XML with an empty configuration. Reset appearance remains appearance-only.

Combo wheel protection, future-day tooltips and automatic timezone remain. About & updates → Data sources includes Open-Meteo, GeoNames and [CC BY 4.0/source links](https://open-meteo.com/en/licence). App version is 1.1; see the [release page](https://github.com/zdxxm10/WeatherBar/releases/tag/v1.1) for the executable and checksum.

## Copyable AI prompts

Replace only the location. AI is a lookup aid: some tools cannot browse, and answers may be inaccurate. Verify sources, same-name places and the URL.

### A: Generate an Open-Meteo URL

```text
Browse for WGS84 decimal-degree coordinates for [Huicheng District, Huizhou, Guangdong, China]. Disambiguate same-name places, verify coordinate sources, and generate a WeatherBar Open-Meteo forecast URL.
Use https://api.open-meteo.com/v1/forecast with verified latitude and longitude and all these parameters:
hourly=temperature_2m,weather_code,uv_index,wind_direction_10m,wind_speed_10m,relative_humidity_2m,apparent_temperature,precipitation_probability&daily=temperature_2m_max,temperature_2m_min,weather_code,sunrise,sunset&temperature_unit=celsius&wind_speed_unit=ms&timezone=auto&timeformat=unixtime
Return the complete copyable URL, place name, separately labelled latitude/longitude and source links. Do not invent coordinates. State uncertainty if unverified; do not substitute GCJ-02 or BD-09 for WGS84.
```

### B: Find WGS84 coordinates

```text
Browse for a representative point in [Huicheng District, Huizhou, Guangdong, China] in WGS84 decimal degrees. Label latitude and longitude separately and provide verifiable source links. Explain which point inside the city/district the coordinate represents; do not claim it represents every point in the administrative area. Do not supply GCJ-02 or BD-09. If the coordinate system is uncertain, say so rather than guessing.
```
